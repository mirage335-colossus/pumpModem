// Include in Impl after scheduled_lazy_default_linear.inc. Default point only.
    static bool lazy_default_point_fft_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_FFT");
        if(!value||std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","Default point FFT requires 0 or 1");return lazy_default_linear_requested();
    }
    template<class T>detail::scheduled_default_linear_runtime::Decision lazy_default_point_fft(
            const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,double charged_threshold,std::stop_token stop) {
        if(lazy_default_point_bulk_requested())return lazy_default_point_bulk<T>(lane,current,charged_threshold,stop);
        namespace wrapper=detail::scheduled_default_point_fft_runtime;
        namespace archive_runtime=detail::scheduled_default_point_guard_archive_runtime;
        namespace archive_storage=detail::scheduled_default_point_guard_archive;
        namespace family=detail::scheduled_default_point_family;
        namespace runtime=detail::scheduled_default_linear_runtime;
        namespace observation=detail::scheduled_default_point_observation;
        namespace original=detail::scheduled_original_partition;
        namespace tile=detail::scheduled_acquisition_tile;
        namespace cert=detail::scheduled_acquisition_certificate;
        runtime::Decision unknown;auto& state=*lazy_state();
        if(!lazy_default_point_fft_requested()||config.search_arithmetic!=SearchArithmetic::default_mode||
           options.raw_reference||options.scheduled_raw_score_reference||options.amplitude_only||
           scheduled_calendar()||!shaped||!lazy_reference_environment()||
           drift_sections!=1||differential_window||!std::isfinite(charged_threshold)||resident_slot(lane.id))return unknown;
        const auto q=lazy_linear_grid_subdivisions();if(q!=2&&q!=4&&q!=8)return unknown;
        rx_cpu::Scope cpu_whole_point(rx_cpu::Stage::shared_frontend);
        cancelled(stop);++state.default_fft_queries;
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
        const auto spacing=static_cast<long double>(code.chip_samples())/(q*reference.rate);
        const auto padding=static_cast<long double>(step+2)*spacing;
        const auto cell_count=std::ceil((end-begin+2*padding)/spacing);
        if(!std::isfinite(cell_count)||cell_count<=0||cell_count>0x40000000ULL)return unknown;
        const wrapper::Key key{state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),
            state.capture_fences?reinterpret_cast<std::uintptr_t>(state.capture_fences.get()):reinterpret_cast<std::uintptr_t>(&state.capture),range.index,range.family,range.first,range.end,q,begin-padding,spacing};
        const auto ledger_end=state.processing->processing_fences().end();
        const auto through=std::min({ledger_end,state.block_end,state.screen_capture_end,state.capture_end});
        auto& pool=state.default_fft_pool;const auto found=pool.find(key);
        if(found<pool.entries.size()&&pool.entries[found]->processing_end<=ledger_end&&origin<pool.entries[found]->whole.size()) {
            ++state.default_fft_cache_hits;pool.touch(*pool.entries[found]);return wrapper::score(*pool.entries[found],origin);
        }
        // Due fences are monotone on this unchanged family lattice. Prepare
        // only the current due prefix, never future catalogue hypotheses.
        std::size_t lo=0,hi=range.end-range.first;
        while(lo<hi){const auto mid=lo+(hi-lo)/2;const auto candidate=lazy_family_lane(range.family,range.first+mid);
            if(state.catalog->due(candidate,range.index,*state.cursor)<=through)lo=mid+1;else hi=mid;}
        auto prefix=lo;
        // Preserve a clipped terminal catalogue hypothesis as ordinary
        // fallback, without invalidating the preceding numerical prefix.
        for(std::size_t global=0;global<prefix;++global) {
            const auto candidate=lazy_family_lane(range.family,range.first+global);
            if(candidate.frequency!=reference.frequency||candidate.rate!=reference.rate||candidate.phase_lower!=reference.phase_lower||
               candidate.anchor.distance(reference.anchor)!=0||std::abs(candidate.origin-reference.origin-
                static_cast<long double>(global)*step*spacing)>.001L){prefix=global;break;}
        }
        if(origin>=prefix){++state.default_fft_lattice_unknown;return unknown;}
        struct Loan {
            Impl& self;std::size_t bytes=0;
            void add(std::size_t n){require(n<=SIZE_MAX-bytes&&n<=SIZE_MAX-self.lazy_state()->transient_reservation,"point FFT reservation overflow");bytes+=n;self.lazy_state()->transient_reservation+=n;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
                self.work.peak_workspace_bytes=std::max(self.work.peak_workspace_bytes,sizeof(PatternCorrelator)+self.accounted_bytes);}
            void retain(std::size_t n){require(n<=bytes,"point FFT retained reservation exceeded");bytes-=n;self.lazy_state()->transient_reservation-=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        } loan{*this};
        // Move the old prefix into a local charged owner. Optional pool reclaim
        // can now evict other entries without invalidating any borrowed fits.
        std::unique_ptr<wrapper::Entry> previous;
        if(found<pool.entries.size()){previous=std::move(pool.entries[found]);loan.add(previous->working_bytes());}
        struct RestorePrevious {
            wrapper::Pool& pool;std::unique_ptr<wrapper::Entry>& previous;Loan& loan;bool committed=false;
            ~RestorePrevious(){if(previous&&!committed){const auto bytes=previous->working_bytes();const auto slot=pool.victim();
                pool.touch(*previous);pool.entries[slot]=std::move(previous);loan.retain(bytes);}}
        } restore_previous{pool,previous,loan};
        const auto first=previous?previous->whole.size():0;
        if(first>=prefix)return unknown;
        family::Geometry geometry{key.physical_base,spacing,lazy_history_first(),through,static_cast<std::size_t>(cell_count),
            prefix-first,first,step,1024};
        if(!family::valid_geometry(geometry))return unknown;
        const auto fences=[&](auto a,auto b,auto consume){state.processing->processing_fences().visit(a,b-a,consume);};
        struct Binding {original::Event event;std::size_t pieces=0;};
        try {
            const auto reserve=[&](std::size_t bytes) {
                cancelled(stop);accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                const auto pinned=(state.serial_execution&&state.serial_execution->screen_active)||state.colored_source_active||state.screens_running||
                    std::any_of(state.executions.begin(),state.executions.end(),[](const auto& e){return e->screen_active;});
                auto used=sizeof(PatternCorrelator)+accounted_bytes;
                if(!pinned&&(used>budget||bytes>budget-used)){room_for(bytes);used=sizeof(PatternCorrelator)+accounted_bytes;}
                if(used>budget||bytes>budget-used){++state.default_fft_workspace;throw Error("point FFT workspace unavailable");}
                loan.add(bytes);
            };
            const auto retained=wrapper::Entry::reservation(prefix),core_bytes=family::reservation<T>(geometry);
            std::size_t reusable_guard_scratch=tile::add_size(core_bytes,tile::add_size(16384,
                tile::multiply_size(geometry.outputs,sizeof(Binding)+sizeof(observation::Selection))));
            reserve(tile::add_size(retained,reusable_guard_scratch));
            std::vector<Binding> bindings(geometry.outputs);std::vector<observation::Selection> selections(geometry.outputs);
            const auto make_binding=[&](std::size_t global) {
                Binding binding;const auto candidate=lazy_family_lane(range.family,range.first+global);
                if(candidate.frequency!=reference.frequency||candidate.rate!=reference.rate||candidate.phase_lower!=reference.phase_lower||
                   candidate.anchor.distance(reference.anchor)!=0||std::abs(candidate.origin-reference.origin-
                    static_cast<long double>(global)*step*spacing)>.001L)throw Error("point FFT candidate off global lattice");
                const auto left=state.catalog->boundary(candidate,source_first,false,*state.cursor);
                const auto right=state.catalog->due(candidate,range.index,*state.cursor);
                const auto a=static_cast<std::uint64_t>(std::max(0.L,std::ceil(left)));
                if(a<lazy_history_first()||right>through||a>=right)throw Error("point FFT Original interval unavailable");
                auto& e=binding.event;e.identity={key.capture,reinterpret_cast<std::uintptr_t>(state.processing.get()),
                    reinterpret_cast<std::uintptr_t>(&state),1,range.index,candidate.phase_lower,range.family+1,candidate.id+1};
                e.epoch_sample=scheduled->epoch_sample;e.phase=candidate.phase_lower;e.index=range.index;
                e.symbol_samples=code.symbol_samples();e.chip_samples=code.chip_samples();e.anchor=candidate.anchor;
                e.origin=candidate.origin;e.rate=candidate.rate;e.symbol_source=source_first;e.sample_rate=config.sample_rate;
                e.fhss=scheduled->fhss_shape;
                e.begin=e.native_begin=a;e.end=e.native_end=right;e.history_first=lazy_history_first();e.capture_end=through;e.left_guard=e.right_guard=1;
                binding.pieces=runtime::Context::piece_count(e,*scheduled_map(),fences,stop);return binding;
            };
            const auto reference_binding=make_binding(0);std::size_t maximum_pieces=0;
            for(std::size_t j=0;j<bindings.size();++j){bindings[j]=make_binding(first+j);maximum_pieces=std::max(maximum_pieces,bindings[j].pieces);}
            const auto contexts=tile::add_size(runtime::Context::reservation(reference_binding.pieces,1),runtime::Context::reservation(maximum_pieces,1));
            reserve(contexts);reusable_guard_scratch=tile::add_size(reusable_guard_scratch,contexts);
            auto context=std::make_unique<runtime::Context>(1,0);
            context->add(0,reference_binding.event,*scheduled_map(),fences,stop,reference_binding.pieces);
            for(std::size_t j=0;j<bindings.size();++j) {
                runtime::Context temporary(1,0);temporary.add(first+j,bindings[j].event,*scheduled_map(),fences,stop,bindings[j].pieces);
                const auto pieces=temporary.pieces(first+j);const auto measured=observation::measure(bindings[j].event,pieces,{},stop);
                if(!measured.valid)throw Error("point FFT exact selection measurement unknown");
                const auto bytes=observation::Selection::reservation(measured.runs);reserve(bytes);
                reusable_guard_scratch=tile::add_size(reusable_guard_scratch,bytes);
                selections[j]=observation::build(bindings[j].event,pieces,measured,bytes,{},stop);
                if(!selections[j].valid)throw Error("point FFT exact selection unknown");
                state.default_fft_selection_runs+=selections[j].runs.size();
            }
            require(state.processing->processing_fences().end()==ledger_end&&state.screen_capture_end==key.capture,
                "point FFT Original ledger changed during preparation");
            // Only top up beyond the already-charged producer temporaries.
            // No dense archive is mandatory: the later guard retains its normal
            // bounded events::run path if optional preparation cannot fit.
            try {
                auto future_geometry=geometry;future_geometry.origin_first=0;future_geometry.outputs=prefix;
                const auto future=lazy_default_point_guard_provision(future_geometry,
                    std::max(maximum_pieces,reference_binding.pieces),prefix);
                if(future>reusable_guard_scratch)reserve(future-reusable_guard_scratch);
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
            require(range.index<=UINT64_MAX/pattern.chips_per_symbol(),"point FFT private chip overflow");
            const auto first_chip=range.index*pattern.chips_per_symbol();pattern.prepare_symbol(first_chip,stop);
            auto source_nodes=lazy_point_source_nodes(reference.phase_lower,range.index,q,stop);
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            const auto cell=[&](std::size_t,long double a,long double b) {return context->cell(0,a,b,q,amplitude,
                [&](std::uint64_t chip){return pattern.values(first_chip+chip);},
                [&](double source){return source_nodes.value(source,[&]{++state.default_fft_endpoint_shapes;return pattern.shaped_values(first_chip,source);});},stop);};
            const auto visit=[&](std::uint64_t a,std::uint64_t b,auto emit) {
                auto cursor=a;if(a==b)return;
                lazy_visit_blocks(a,b,[&](const auto& block) {
                    cancelled(stop);const auto left=std::max(a,block.first),right=std::min<std::uint64_t>(b,block.first+block.pcm.size());
                    if(left!=cursor)throw Error("point FFT raw physical gap");
                    const auto frequency=banks[reference.frequency].frequency;
                    auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    const auto rotation=std::polar(1.,tau*frequency/config.sample_rate);
                    for(auto n=block.first;n<right;++n){if((n&255U)==0)cancelled(stop);if(n>=left){
                        if(cert::norm(oscillator)>2)throw Error("point FFT raw carrier outside execution contract");
                        const auto x=static_cast<double>(block.pcm[n-block.first]);++state.default_fft_input_rows;
                        emit(n,detail::CorrelationProjection{x*oscillator.real(),x*oscillator.imag(),oscillator.real()*oscillator.real(),
                            oscillator.imag()*oscillator.imag(),oscillator.real()*oscillator.imag(),x*x});}oscillator*=rotation;}
                    cursor=right;
                },false);if(cursor!=b)throw Error("point FFT raw physical history incomplete");
            };
            observation::PieceCache direct;
            struct CountPieces {LazyState& state;observation::PieceCache& direct;~CountPieces(){state.default_fft_direct_piece_builds+=direct.piece_builds;}} count_pieces{state,direct};
            const auto original_row=[&](std::size_t global,std::uint64_t n) {
                if(global<first||global-first>=bindings.size())throw Error("point FFT Original lane unavailable");
                const auto exact=direct.evaluate(bindings[global-first].event,n,*scheduled_map(),fences,stop);
                if(!exact.valid())throw Error("point FFT Original row evaluation unknown");
                family::OriginalRow row;row.observed=exact.current;
                if(row.observed){++state.default_fft_original_shapes;const auto raw=pattern.shaped_values(first_chip,static_cast<double>(exact.within));
                    for(unsigned bit=0;bit<2;++bit)row.q[bit]=exact.gain*pattern_limit_pcm(amplitude*raw[bit])/amplitude;}return row;
            };
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
            auto result=family::run<T>(geometry,core_bytes,cell,visit,std::span<const observation::Selection>(selections),original_row,{},stop,
                fresh_archive?&fresh_archive->archive:nullptr,template_pool);
            auto fresh=std::make_unique<wrapper::Entry>();fresh->key=key;fresh->processing_end=ledger_end;fresh->reserved=retained;
            fresh->whole.reserve(prefix);fresh->valid.reserve(prefix);
            if(previous){fresh->whole.insert(fresh->whole.end(),previous->whole.begin(),previous->whole.end());fresh->valid.insert(fresh->valid.end(),previous->valid.begin(),previous->valid.end());}
            fresh->whole.insert(fresh->whole.end(),result.whole.begin(),result.whole.end());fresh->valid.insert(fresh->valid.end(),result.valid.begin(),result.valid.end());
            require(fresh->whole.size()==prefix&&fresh->valid.size()==prefix&&sizeof(wrapper::Entry)+fresh->whole.capacity()*sizeof(fresh->whole[0])+fresh->valid.capacity()<=retained,
                "point FFT retained fields exceeded reservation");
            cancelled(stop);
            if(fresh_archive) {
                const auto bytes=fresh_archive->working_bytes();
                if(state.default_point_guard_archive.publish(fresh_archive,stop)){loan.retain(bytes);archive_loan=0;}
            }
            // Completed prior guards are immutable under append-only actual
            // fences. Transfer ownership with the unchanged exact Key; the
            // prepass can grow the sparse Store later under a separate loan.
            if(previous)fresh->guard_summaries=std::move(previous->guard_summaries);
            const auto published_bytes=fresh->working_bytes();
            const auto slot=pool.victim();pool.touch(*fresh);pool.entries[slot]=std::move(fresh);loan.retain(published_bytes);restore_previous.committed=true;
            ++state.default_fft_batches;state.default_fft_lanes+=geometry.outputs;state.default_fft_completed_mask_rows+=result.work.mask_rows;
            state.default_fft_completed_input_ffts+=result.work.transforms.input_ffts;
            state.default_fft_completed_template_ffts+=result.work.transforms.template_ffts;
            state.default_fft_completed_inverse_ffts+=result.work.transforms.inverse_ffts;
            state.default_fft_completed_template_environment_bypasses+=result.work.transforms.template_cache_environment_bypasses;
            return wrapper::score(*pool.entries[slot],origin);
        }catch(const Error&){if(stop.stop_requested())throw;++state.default_fft_unknown;return unknown;}
        catch(const std::bad_alloc&){cancelled(stop);++state.default_fft_unknown;return unknown;}
    }
