// Include in Impl before the legacy whole pointFFT entry point. Experimental
// Default only. Numerical lookahead is captured-PCM-only, never future Pieces.
    static bool lazy_default_point_bulk_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_BULK");
        if(!value||std::string_view(value)=="1")return true;
        require(std::string_view(value)=="0","Default point bulk requires 0 or 1");return false;
    }
    static bool lazy_default_point_compact_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT");
        if(!value||std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","Default point compact requires 0 or 1");return true;
    }
    template<class T>detail::scheduled_default_linear_runtime::Decision lazy_default_point_bulk(
            const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,double charged_threshold,std::stop_token stop) {
        namespace wrapper=detail::scheduled_default_point_fft_runtime;
        namespace archive_runtime=detail::scheduled_default_point_guard_archive_runtime;
        namespace archive_storage=detail::scheduled_default_point_guard_archive;
        namespace family=detail::scheduled_default_point_family;
        namespace bulk=detail::scheduled_default_point_bulk;
        namespace recipe=detail::scheduled_default_point_reference_recipe;
        namespace compact=detail::scheduled_default_compact_chords;
        using CompactTile=compact::Tile<recipe::LookupIdentity>;
        namespace runtime=detail::scheduled_default_linear_runtime;
        namespace observation=detail::scheduled_default_point_observation;
        namespace original=detail::scheduled_original_partition;
        namespace tile=detail::scheduled_acquisition_tile;
        namespace cert=detail::scheduled_acquisition_certificate;
        runtime::Decision unknown;auto& state=*lazy_state();
        if(!lazy_default_point_fft_requested()||config.search_arithmetic!=SearchArithmetic::default_mode||
           options.raw_reference||options.scheduled_raw_score_reference||options.amplitude_only||scheduled_calendar()||
           !shaped||!lazy_reference_environment()||drift_sections!=1||differential_window||
           !std::isfinite(charged_threshold)||resident_slot(lane.id))return unknown;
        const auto q=lazy_linear_grid_subdivisions();if(q!=2&&q!=4&&q!=8)return unknown;
        const auto compact_requested=lazy_default_point_compact_requested();
        const auto retention=compact_requested?bulk::Retention::recipe:bulk::Retention::dense;
        rx_cpu::Scope cpu_whole_point(rx_cpu::Stage::shared_frontend);cancelled(stop);++state.default_fft_queries;
        const auto prepared=std::find_if(state.batch_ranges.begin(),state.batch_ranges.end(),[&](const auto& r){
            return r.family==current.family&&r.index==current.index&&lane.origin_index>=r.first&&lane.origin_index<r.end;});
        const auto& range=prepared==state.batch_ranges.end()?current:*prepared;
        if(range.first>=range.end||range.end-range.first>tile::maximum_start_span||lane.origin_index<range.first||
           lane.origin_index>=range.end||range.index>UINT64_MAX/code.symbol_samples()||code.symbol_samples()>1048576||
           code.symbol_samples()%code.chip_samples()||code.symbol_samples()/code.chip_samples()<16)return unknown;
        const auto reference=lazy_family_lane(range.family,range.first);if(reference.rate<.99L||reference.rate>1.01L)return unknown;
        const auto source_first=static_cast<long double>(range.index)*code.symbol_samples();
        const auto begin=state.catalog->boundary(reference,source_first,false,*state.cursor);
        const auto end=state.catalog->boundary(reference,source_first+code.symbol_samples(),false,*state.cursor);
        const auto step=static_cast<std::size_t>(q/2),origin=lane.origin_index-range.first;
        const auto spacing=static_cast<long double>(code.chip_samples())/(q*reference.rate),padding=static_cast<long double>(step+2)*spacing;
        const auto count=std::ceil((end-begin+2*padding)/spacing);
        if(!std::isfinite(count)||count<=0||count>0x40000000ULL)return unknown;
        wrapper::Key key{state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),
            state.capture_fences?reinterpret_cast<std::uintptr_t>(state.capture_fences.get()):reinterpret_cast<std::uintptr_t>(&state.capture),
            range.index,range.family,range.first,range.end,q,begin-padding,spacing};key.pending_bulk=true;key.bulk_scalar_bytes=sizeof(T);key.compact_bulk=compact_requested;
        const auto ledger_end=state.processing->processing_fences().end();
        const auto through=std::min({ledger_end,state.block_end,state.screen_capture_end,state.capture_end});
        auto& pool=state.default_fft_pool;const auto found=pool.find(key);
        if(found<pool.entries.size()&&pool.entries[found]->pending&&origin<pool.entries[found]->finalized_prefix&&
           pool.entries[found]->processing_end<=ledger_end){++state.default_fft_cache_hits;pool.touch(*pool.entries[found]);return wrapper::score(*pool.entries[found],origin);}
        struct Loan {Impl& self;std::size_t bytes=0;
            void add(std::size_t n){require(n<=SIZE_MAX-bytes&&n<=SIZE_MAX-self.lazy_state()->transient_reservation,"point bulk reservation overflow");
                bytes+=n;self.lazy_state()->transient_reservation+=n;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
                self.work.peak_workspace_bytes=std::max(self.work.peak_workspace_bytes,sizeof(PatternCorrelator)+self.accounted_bytes);}
            void retain(std::size_t n){require(n<=bytes,"point bulk retained reservation exceeded");bytes-=n;self.lazy_state()->transient_reservation-=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        } loan{*this};
        std::unique_ptr<wrapper::Entry> entry;
        if(found<pool.entries.size()){entry=std::move(pool.entries[found]);loan.add(entry->working_bytes());}
        struct Restore {wrapper::Pool& pool;std::unique_ptr<wrapper::Entry>& entry;Loan& loan;bool committed=false;
            ~Restore(){if(entry&&!committed){const auto bytes=entry->working_bytes();const auto slot=pool.victim();pool.touch(*entry);pool.entries[slot]=std::move(entry);loan.retain(bytes);}}
        } restore{pool,entry,loan};
        struct Binding {original::Event event;std::size_t pieces=0;};
        const auto fences=[&](auto a,auto b,auto consume){state.processing->processing_fences().visit(a,b-a,consume);};
        try {
            const auto reserve=[&](std::size_t bytes){cancelled(stop);
                accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                const auto pinned=(state.serial_execution&&state.serial_execution->screen_active)||state.colored_source_active||state.screens_running||
                    std::any_of(state.executions.begin(),state.executions.end(),[](const auto& e){return e->screen_active;});
                auto used=sizeof(PatternCorrelator)+accounted_bytes;
                if(!pinned&&(used>budget||bytes>budget-used)){room_for(bytes);used=sizeof(PatternCorrelator)+accounted_bytes;}
                if(used>budget||bytes>budget-used){++state.default_fft_workspace;throw Error("point bulk workspace unavailable");}loan.add(bytes);
            };
            // Required finalizer scratch is reserved before any optional cache
            // can consume the fresh family loan. Allocation occurs only while
            // its exact retained recipe remains owned and pinned locally.
            if(compact_requested)reserve(CompactTile::reservation());
            const auto make_binding=[&](std::size_t global){Binding binding;const auto candidate=lazy_family_lane(range.family,range.first+global);
                if(candidate.frequency!=reference.frequency||candidate.rate!=reference.rate||candidate.phase_lower!=reference.phase_lower||
                   candidate.anchor.distance(reference.anchor)!=0||std::abs(candidate.origin-reference.origin-static_cast<long double>(global)*step*spacing)>.001L)
                    throw Error("point bulk candidate off global lattice");
                const auto left=state.catalog->boundary(candidate,source_first,false,*state.cursor);const auto right=state.catalog->due(candidate,range.index,*state.cursor);
                const auto a=static_cast<std::uint64_t>(std::max(0.L,std::ceil(left)));
                if(a<lazy_history_first()||right>through||a>=right)throw Error("point bulk ACTUAL Original interval unavailable");
                auto& e=binding.event;e.identity={key.capture,reinterpret_cast<std::uintptr_t>(state.processing.get()),reinterpret_cast<std::uintptr_t>(&state),1,
                    range.index,candidate.phase_lower,range.family+1,candidate.id+1};
                e.epoch_sample=scheduled->epoch_sample;e.phase=candidate.phase_lower;e.index=range.index;e.symbol_samples=code.symbol_samples();e.chip_samples=code.chip_samples();
                e.anchor=candidate.anchor;e.origin=candidate.origin;e.rate=candidate.rate;e.symbol_source=source_first;e.sample_rate=config.sample_rate;e.fhss=scheduled->fhss_shape;
                e.begin=e.native_begin=a;e.end=e.native_end=right;e.history_first=lazy_history_first();e.capture_end=through;e.left_guard=e.right_guard=1;
                binding.pieces=runtime::Context::piece_count(e,*scheduled_map(),fences,stop);return binding;
            };
            // Resolve capture-complete and ACTUAL published-due extents before
            // optional caches spend surplus workspace. This is metadata only;
            // future candidate Pieces and observations are still forbidden.
            auto planned_geometry=entry?entry->pending->geometry:family::Geometry{
                key.physical_base,spacing,lazy_history_first(),state.screen_capture_end,static_cast<std::size_t>(count),
                range.end-range.first,0,step,1024};
            auto prefix=planned_geometry.outputs;
            if(!entry) {
                prefix=bulk::capture_complete_prefix(planned_geometry);
                for(std::size_t global=0;global<prefix;++global){const auto candidate=lazy_family_lane(range.family,range.first+global);
                    if(candidate.frequency!=reference.frequency||candidate.rate!=reference.rate||candidate.phase_lower!=reference.phase_lower||
                       candidate.anchor.distance(reference.anchor)!=0||std::abs(candidate.origin-reference.origin-static_cast<long double>(global)*step*spacing)>.001L){prefix=global;break;}}
                planned_geometry.outputs=prefix;
            }
            if(origin>=prefix){++state.default_fft_lattice_unknown;return unknown;}
            std::size_t lo=0,hi=prefix;
            while(lo<hi){const auto mid=lo+(hi-lo)/2;const auto candidate=lazy_family_lane(range.family,range.first+mid);
                if(state.catalog->due(candidate,range.index,*state.cursor)<=through)lo=mid+1;else hi=mid;}
            const auto first=entry?entry->finalized_prefix:0,due=lo;
            if(origin<first){++state.default_fft_cache_hits;return wrapper::score(*entry,origin);}
            if(origin>=due||first>=due)return unknown;
            auto finalize_geometry=planned_geometry;finalize_geometry.origin_first=first;finalize_geometry.outputs=due-first;
            const auto final_core=bulk::finalize_reservation(finalize_geometry);
            const auto final_metadata=tile::add_size(16384,tile::multiply_size(due-first,sizeof(Binding)+sizeof(observation::Selection)));
            std::size_t reusable_guard_scratch=tile::add_size(final_core,final_metadata);
            reserve(reusable_guard_scratch);
            std::vector<Binding> bindings(due-first);std::vector<observation::Selection> selections(due-first);std::size_t maximum_pieces=0;
            for(std::size_t j=0;j<bindings.size();++j){bindings[j]=make_binding(first+j);maximum_pieces=std::max(maximum_pieces,bindings[j].pieces);}
            const auto selection_context=runtime::Context::reservation(maximum_pieces,1);
            const auto context_scratch=selection_context;
            reserve(selection_context);reusable_guard_scratch=tile::add_size(reusable_guard_scratch,selection_context);
            for(std::size_t j=0;j<bindings.size();++j){runtime::Context context(1,0);context.add(first+j,bindings[j].event,*scheduled_map(),fences,stop,bindings[j].pieces);
                const auto pieces=context.pieces(first+j);const auto measured=observation::measure(bindings[j].event,pieces,{},stop);
                if(!measured.valid)throw Error("point bulk actual Selection measurement unavailable");const auto bytes=observation::Selection::reservation(measured.runs);reserve(bytes);reusable_guard_scratch=tile::add_size(reusable_guard_scratch,bytes);
                selections[j]=observation::build(bindings[j].event,pieces,measured,bytes,{},stop);if(!selections[j].valid)throw Error("point bulk actual Selection unavailable");
                state.default_fft_selection_runs+=selections[j].runs.size();}
            // The exact due cohort is owned/precharged once. Its temporary
            // Context bound can also cover reference0's construction: all
            // Selection-building Contexts have already left scope.
            std::size_t completed_bulk_scratch=0,completed_context_scratch=0;
            if(!entry){
                const auto& geometry=planned_geometry;
                require(first==0&&!bindings.empty(),"fresh point bulk lost reference0 Binding");
                const auto reference_binding=bindings.front();
                auto retained=tile::add_size(wrapper::Entry::reservation(prefix),bulk::PendingBulk::reservation(geometry,retention));
                const auto core=bulk::reservation<T>(geometry,retention);
                reserve(tile::add_size(retained,core));
                auto fresh=std::make_unique<wrapper::Entry>();fresh->key=key;fresh->reserved=retained;
                fresh->whole.resize(prefix);fresh->valid.resize(prefix);
                runtime::Context context(1,0);context.add(0,reference_binding.event,*scheduled_map(),fences,stop,reference_binding.pieces);
                const auto recipe_bytes=compact_requested?recipe::FrozenReference::reservation(context):0;
                std::size_t recipe_extra=0;
                if(compact_requested) {
                    // Transfer the already reserved reference Context vector
                    // capacities into retained ownership. During the move both
                    // inline objects coexist, but the vectors never duplicate.
                    const auto overlap=tile::add_size(sizeof(runtime::Context),recipe_bytes);
                    if(overlap>context_scratch){recipe_extra=overlap-context_scratch;reserve(recipe_extra);}
                    retained=tile::add_size(retained,recipe_bytes);fresh->reserved=retained;
                }
                // Guard preparation runs AFTER this function returns. Reserve
                // max(producer scratch, later guard scratch), not their sum,
                // before optional archive/template caches consume the surplus.
                try {
                    const auto future=lazy_default_point_guard_provision(planned_geometry,maximum_pieces,prefix);
                    // Context ownership transferred to the persistent recipe is not
                    // reusable guard scratch. Only the released part covers this peak.
                    const auto reusable=tile::add_size(tile::add_size(reusable_guard_scratch,core),recipe_extra)-recipe_bytes;
                    if(future>reusable)reserve(future-reusable);
                }catch(const Error&){if(stop.stop_requested())throw;++state.default_fft_pool.guard_work.workspace_fallbacks;}
                catch(const std::bad_alloc&){cancelled(stop);++state.default_fft_pool.guard_work.workspace_fallbacks;}
                std::unique_ptr<archive_runtime::Entry> fresh_archive;
                std::size_t archive_loan=0;
                const auto rollback_archive=[&] {
                    fresh_archive.reset();
                    if(archive_loan){loan.retain(archive_loan);archive_loan=0;}
                    accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                };
                if(lazy_default_point_guard_archive_requested()) {
                    try {
                        const auto first_input=tile::multiply_size(geometry.origin_first,geometry.step);
                        const auto input_cells=tile::add_size(geometry.template_cells,tile::multiply_size(geometry.outputs-1,geometry.step));
                        const archive_storage::Layout layout{geometry.physical_base,geometry.node_spacing,
                            geometry.capture_first,geometry.capture_end,geometry.template_cells,first_input,input_cells,
                            geometry.origin_first,geometry.outputs,geometry.step};
                        const auto requested_edges=lazy_default_point_guard_edge_width(q,geometry.node_spacing);
                        const auto edge_width=archive_runtime::fitting_edge_width(layout,requested_edges);
                        if(edge_width!=requested_edges)++state.default_point_guard_archive.stats.strip_capacity_fallbacks;
                        const auto bytes=archive_runtime::Entry::reservation(layout,edge_width);
                        // No archive/plan/source borrow is live during this optional
                        // preflight. Existing completed whole prefixes stay locally
                        // owned and charged if pressure clears other cache entries.
                        reserve(bytes);
                        archive_loan=bytes;
                        fresh_archive=std::make_unique<archive_runtime::Entry>(
                            lazy_default_point_guard_archive_key(reference,range,geometry,q),layout,bytes,stop,edge_width);
                        fresh_archive->processing_end=ledger_end;fresh_archive->block_end=state.block_end;
                        fresh_archive->capture_through=geometry.capture_end;
                    }catch(const Error&){rollback_archive();if(stop.stop_requested())throw;++state.default_point_guard_archive.stats.workspace_fallbacks;}
                    catch(const std::bad_alloc&){rollback_archive();cancelled(stop);++state.default_point_guard_archive.stats.allocation_fallbacks;}
                }
                auto& execution=lazy_execution();
                struct Pin {LazyState::Execution& e;bool previous;explicit Pin(LazyState::Execution& x):e(x),previous(x.screen_active){e.screen_active=true;}~Pin(){e.screen_active=previous;}} pin(execution);
                auto& pattern=execution.code;pattern.set_stream_phase_samples(reference.phase_lower);
                require(range.index<=UINT64_MAX/pattern.chips_per_symbol(),"point bulk chip overflow");const auto first_chip=range.index*pattern.chips_per_symbol();pattern.prepare_symbol(first_chip,stop);
                auto source_nodes=lazy_point_source_nodes(reference.phase_lower,range.index,q,stop);
                const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
                const auto cell=[&](std::size_t,long double a,long double b){return context.cell(0,a,b,q,amplitude,
                    [&](std::uint64_t local){return pattern.values(first_chip+local);},
                    [&](double source){return source_nodes.value(source,[&]{++state.default_fft_endpoint_shapes;return pattern.shaped_values(first_chip,source);});},stop);};
                const auto visit=[&](std::uint64_t a,std::uint64_t b,auto emit){lazy_point_bulk_visit(reference.frequency,a,b,emit,stop);};
                // Optional exact source-only FFT archive. All mandatory family,
                // observation/context and other archive loans are already charged.
                // Cache shortage never changes the candidate, decoder or fallback.
                using TemplatePool=detail::scheduled_default_point_template_cache::Pool<T>;
                TemplatePool* template_pool=nullptr;
                if(const auto* mode=std::getenv("DATAPUMP_SCHED_POINT_TEMPLATE_FFT_CACHE")) {
                    const auto choice=std::string_view(mode);require(choice=="0"||choice=="1","point template FFT cache requires0or1");
                    if(choice=="1") {
                        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                        const auto used=sizeof(PatternCorrelator)+accounted_bytes;
                        const auto available=used<=budget?budget-used:0;
                        const auto fragment=std::min(geometry.fragment_cells,geometry.template_cells);
                        const auto extent=tile::add_size(fragment,tile::multiply_size(geometry.outputs-1,geometry.step));
                        const auto transform=std::bit_ceil(extent);
                        template_pool=state.default_fft_templates.template ensure<T>(available,TemplatePool::entry_reservation(fragment,transform),stop);
                        accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"point template FFT cache exceeds owner loan");
                        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
                    }
                }
                fresh->pending=std::make_unique<bulk::PendingBulk>(bulk::prepare<T>(geometry,core,cell,visit,stop,
                    fresh_archive?&fresh_archive->archive:nullptr,template_pool,retention));
                if(compact_requested) {
                    // Bank-private PatternCode/config/key are immutable for
                    // owner &state/revision1. Phase/index/model/Q/amplitude and
                    // physical epoch are explicit; recipe instance binds the
                    // exact original Event/Pieces/grid, not a later ledger.
                    const recipe::SourceIdentity source{reinterpret_cast<std::uintptr_t>(&state),1,
                        scheduled->epoch_sample,reference.phase_lower,range.index,code.symbol_samples(),code.chip_samples(),q,1,amplitude};
                    fresh->reference_recipe=std::make_unique<recipe::FrozenReference>(std::move(context),geometry,source,recipe_bytes,stop);
                    require(fresh->pending->directory&&fresh->pending->directory->sealed()&&
                        fresh->reference_recipe->working_bytes()==recipe_bytes,"point compact ownership/charge mismatch");
                }
                if(fresh_archive)fresh_archive->archive.seal(stop);
                const auto& work=fresh->pending->work;
                state.default_fft_completed_input_ffts+=work.transforms.input_ffts;state.default_fft_completed_template_ffts+=work.transforms.template_ffts;
                state.default_fft_completed_inverse_ffts+=work.transforms.inverse_ffts;++state.default_bulk_preparations;state.default_bulk_outputs+=prefix;
                require(fresh->core_payload_bytes()<=retained,
                    "point bulk retained state undercharged");
                cancelled(stop);
                if(fresh_archive){const auto bytes=fresh_archive->working_bytes();
                    if(state.default_point_guard_archive.publish(fresh_archive,stop)){loan.retain(bytes);archive_loan=0;}}
                entry=std::move(fresh);completed_bulk_scratch=core;
                if(compact_requested)completed_context_scratch=tile::add_size(context_scratch,recipe_extra)-recipe_bytes;
            }
            // Bulk engine/reference Context/source-node borrows have ended.
            // Retain the entry plus already-owned exact due cohort loans.
            if(completed_bulk_scratch)loan.retain(completed_bulk_scratch);
            // The old inline Context has now ended. Leave exactly recipe_bytes
            // of its loan retained with Entry; no double vector-capacity loan.
            if(completed_context_scratch)loan.retain(completed_context_scratch);
            auto& execution=lazy_execution();struct Pin {LazyState::Execution& e;bool previous;explicit Pin(LazyState::Execution& x):e(x),previous(x.screen_active){e.screen_active=true;}~Pin(){e.screen_active=previous;}} pin(execution);
            auto& pattern=execution.code;pattern.set_stream_phase_samples(reference.phase_lower);const auto first_chip=range.index*pattern.chips_per_symbol();pattern.prepare_symbol(first_chip,stop);
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            observation::PieceCache direct;
            struct CountPieces {LazyState& s;observation::PieceCache& c;~CountPieces(){s.default_fft_direct_piece_builds+=c.piece_builds;}} count_pieces{state,direct};
            const auto actual_row=[&](std::size_t global,std::uint64_t n){if(global<first||global-first>=bindings.size())throw Error("point bulk Original lane missing");
                const auto exact=direct.evaluate(bindings[global-first].event,n,*scheduled_map(),fences,stop);if(!exact.valid())throw Error("point bulk Original row unavailable");
                family::OriginalRow row;row.observed=exact.current;if(row.observed){++state.default_fft_original_shapes;
                    const auto raw=pattern.shaped_values(first_chip,static_cast<double>(exact.within));for(unsigned bit=0;bit<2;++bit)row.q[bit]=exact.gain*pattern_limit_pcm(amplitude*raw[bit])/amplitude;}return row;};
            const auto visit=[&](std::uint64_t a,std::uint64_t b,auto emit){lazy_point_bulk_visit(reference.frequency,a,b,emit,stop);};
            auto result=[&] {
                if(!compact_requested)return bulk::finalize_due_batch(*entry->pending,first,selections,final_core,visit,actual_row,stop);
                require(entry->pending->directory&&entry->reference_recipe,"point compact pending lost its frozen recipe");
                const recipe::SourceIdentity source{reinterpret_cast<std::uintptr_t>(&state),1,
                    scheduled->epoch_sample,reference.phase_lower,range.index,code.symbol_samples(),code.chip_samples(),q,1,amplitude};
                require(entry->reference_recipe->identity()==source,"point compact immutable source identity changed");
                const auto identity=entry->reference_recipe->lookup_identity();
                auto payload=std::make_unique<CompactTile>(identity,CompactTile::reservation(),stop);
                auto source_nodes=lazy_point_source_nodes(reference.phase_lower,range.index,q,stop);
                const auto provider=[&](std::size_t k){return entry->reference_recipe->chord(k,source,
                    [&](std::uint64_t local){return pattern.values(first_chip+local);},
                    [&](double at){return source_nodes.value(at,[&]{++state.default_fft_endpoint_shapes;return pattern.shaped_values(first_chip,at);});},stop);};
                const auto resolve=[&](std::size_t k){return payload->get(*entry->pending->directory,k,identity,provider,stop);};
                return bulk::finalize_due_batch(*entry->pending,first,selections,final_core,visit,actual_row,stop,family::Limits{},resolve);
            }();
            require(state.processing->processing_fences().end()==ledger_end&&state.screen_capture_end==key.capture,"point bulk actual ledger changed during finalize");
            cancelled(stop);
            for(std::size_t j=0;j<result.whole.size();++j){entry->whole[first+j]=result.whole[j];entry->valid[first+j]=result.valid[j];}
            entry->processing_end=ledger_end;entry->finalized_prefix=due;
            state.default_fft_completed_mask_rows+=result.work.mask_rows;++state.default_fft_batches;state.default_fft_lanes+=due-first;
            ++state.default_bulk_finalizations;state.default_bulk_finalized_outputs+=due-first;
            const auto slot=pool.victim(),retained=entry->working_bytes();pool.touch(*entry);pool.entries[slot]=std::move(entry);loan.retain(retained);restore.committed=true;
            return wrapper::score(*pool.entries[slot],origin);
        }catch(const Error&){if(stop.stop_requested())throw;++state.default_fft_unknown;return unknown;}
        catch(const std::bad_alloc&){cancelled(stop);++state.default_fft_unknown;return unknown;}
    }
    template<class Emit>void lazy_point_bulk_visit(std::size_t frequency_index,std::uint64_t a,std::uint64_t b,Emit emit,std::stop_token stop){
        namespace cert=detail::scheduled_acquisition_certificate;auto& state=*lazy_state();auto cursor=a;if(a==b)return;
        lazy_visit_blocks(a,b,[&](const auto& block){cancelled(stop);const auto left=std::max(a,block.first),right=std::min<std::uint64_t>(b,block.first+block.pcm.size());
            if(left!=cursor)throw Error("point bulk raw physical gap");const auto frequency=banks[frequency_index].frequency;
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
            const auto rotation=std::polar(1.,tau*frequency/config.sample_rate);
            for(auto n=block.first;n<right;++n){if((n&255U)==0)cancelled(stop);if(n>=left){
                if(cert::norm(oscillator)>2)throw Error("point bulk raw carrier outside contract");const auto x=static_cast<double>(block.pcm[n-block.first]);++state.default_fft_input_rows;
                emit(n,detail::CorrelationProjection{x*oscillator.real(),x*oscillator.imag(),oscillator.real()*oscillator.real(),oscillator.imag()*oscillator.imag(),oscillator.real()*oscillator.imag(),x*x});}oscillator*=rotation;}
            cursor=right;},false);if(cursor!=b)throw Error("point bulk raw physical history unavailable");
    }
