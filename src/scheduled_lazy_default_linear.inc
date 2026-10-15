// Include inside PatternCorrelator::Impl. Explicit opt-in point experiment.
// This does not export a cold upper bound or any Original field certificate.
    static bool lazy_default_linear_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_LINEAR");
        if(!value || std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","Default linear experiment requires 0 or 1");return true;
    }
    static bool lazy_default_linear_primary_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY");
        if(!value || std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","Default linear primary experiment requires 0 or 1");
        return lazy_default_linear_requested();
    }
    bool lazy_default_linear_primary_eligible(const LazyState::DueRange& range) {
        const auto q=lazy_linear_grid_subdivisions();
        if(!lazy_default_linear_primary_requested() || config.search_arithmetic!=SearchArithmetic::default_mode ||
           options.raw_reference || options.scheduled_raw_score_reference || options.amplitude_only ||
           scheduled_calendar() || (scheduled->fhss_shape.enabled && !scheduled->fhss_shape.smooth) || !shaped || !lazy_reference_environment() ||
           drift_sections!=1 || differential_window || (q!=2 && q!=4 && q!=8) || range.first>=range.end ||
           code.symbol_samples()>1048576 || code.symbol_samples()%code.chip_samples() || code.symbol_samples()/code.chip_samples()<16)return false;
        const auto first=lazy_family_lane(range.family,range.first);
        if(first.rate<.99L || first.rate>1.01L)return false;
        if(range.end-range.first>1) {
            const auto second=lazy_family_lane(range.family,range.first+1);
            if(second.rate!=first.rate || second.frequency!=first.frequency || second.phase_lower!=first.phase_lower ||
               second.anchor.distance(first.anchor)!=0 || std::abs(second.origin-first.origin-
                static_cast<long double>(code.chip_samples())/(2*first.rate))>.001L)return false;
        }
        return true;
    }
    detail::scheduled_default_linear_runtime::Decision lazy_default_linear_point(
            const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,double charged_threshold,
            std::stop_token stop,bool whole_prefilter_ready=false,
            detail::scheduled_default_point_guard_summary::Summary* completed_summary=nullptr,
            std::size_t prepass_cohort=0,
            const detail::scheduled_default_point_family::Geometry* rebuild_geometry=nullptr) {
        namespace runtime=detail::scheduled_default_linear_runtime;
        namespace archive_runtime=detail::scheduled_default_point_guard_archive_runtime;
        namespace prepared_events=detail::scheduled_default_prepared_events;
        namespace events=detail::scheduled_default_linear_events;
        namespace orig=detail::scheduled_original_partition;
        namespace cert=detail::scheduled_acquisition_certificate;
        namespace tile=detail::scheduled_acquisition_tile;
        static_assert(runtime::maximum_lanes==detail::scheduled_default_guard_cohort::maximum_lanes);
        runtime::Decision decision;auto& state=*lazy_state();
        if(completed_summary)*completed_summary={};
        if(!lazy_default_linear_requested() || config.search_arithmetic!=SearchArithmetic::default_mode ||
           options.raw_reference || options.scheduled_raw_score_reference || options.amplitude_only ||
           scheduled_calendar() || (scheduled->fhss_shape.enabled && !scheduled->fhss_shape.smooth) || !shaped || !lazy_reference_environment() ||
           !std::isfinite(charged_threshold) || charged_threshold<0 || resident_slot(lane.id))return decision;
        const auto subdivisions=lazy_linear_grid_subdivisions();
        if(subdivisions!=2 && subdivisions!=4 && subdivisions!=8)return decision;
        // Existing exact deferred diagnostic scorer is coherent-only.
        // The Context/core support quarter/window statistics, but this
        // experiment must not enqueue an unsupported Original diagnostic.
        if(drift_sections!=1 || differential_window)return decision;
        rx_cpu::Scope cpu_point_guards(rx_cpu::Stage::guards);
        cancelled(stop);
        ++state.default_linear_queries;
        const auto prepared=std::find_if(state.batch_ranges.begin(),state.batch_ranges.end(),[&](const auto& r){
            return r.family==current.family && r.index==current.index && lane.origin_index>=r.first && lane.origin_index<r.end;});
        const auto& range=prepared==state.batch_ranges.end()?current:*prepared;
        if(range.first>=range.end || lane.origin_index<range.first || lane.origin_index>=range.end ||
           range.index>UINT64_MAX/code.symbol_samples() || code.symbol_samples()%code.chip_samples() ||
           code.symbol_samples()/code.chip_samples()<16)return decision;
        const auto origin=lane.origin_index-range.first;
        namespace cohort_policy=detail::scheduled_default_guard_cohort;
        auto policy=cohort_policy::parse(std::getenv("DATAPUMP_SCHED_GUARD_COHORT"));
        // Only an actual-due prepass may select the existing bounded eight-lane
        // engine automatically. Explicit diagnostic cohort overrides still win.
        if(completed_summary&&prepass_cohort==8&&policy==cohort_policy::Policy::automatic)
            policy=cohort_policy::Policy::eight;
        const auto cohort=cohort_policy::choose(origin,range.end-range.first,whole_prefilter_ready,policy);
        const auto cohort_first=cohort.first;
        const auto ledger_end=state.processing->processing_fences().end();
        const auto through=std::min({ledger_end,state.block_end,state.screen_capture_end,state.capture_end});
        auto outputs=cohort.outputs;
        while(outputs) {
            const auto last=lazy_family_lane(range.family,range.first+cohort_first+outputs-1);
            if(state.catalog->due(last,range.index,*state.cursor)<=through)break;
            --outputs;
        }
        if(!outputs || origin-cohort_first>=outputs)return decision;
        const runtime::CacheKey key{state.screen_capture_end,ledger_end,state.block_end,range.index,
            range.family,range.first,range.end,cohort_first,outputs,subdivisions};
        const auto score_cached=[&](const runtime::Cache& cache) {
            runtime::Decision out;const auto j=origin-cache.key.origin_first;
            if(j>=cache.result.events.size())return out;
            if(completed_summary)*completed_summary=detail::scheduled_default_point_guard_summary::summarize(cache.result.events[j],cache.geometry);
            out.scores=events::scores(cache.result.events[j],cache.geometry,charged_threshold);
            out.ready=out.scores.valid;return out;
        };
        if(state.default_linear_cache && state.default_linear_cache->key==key) {
            ++state.default_linear_cache_hits;return score_cached(*state.default_linear_cache);
        }
        // A mismatched cache is optional and has no active borrower. Do not
        // force a two-cache peak merely to replace this one bounded cohort.
        state.default_linear_cache.reset();
        const auto reference=lazy_family_lane(range.family,range.first);
        if(reference.rate<.99L || reference.rate>1.01L)return decision;
        const auto step=static_cast<std::size_t>(subdivisions/2);
        const auto spacing=static_cast<long double>(code.chip_samples())/(subdivisions*reference.rate);
        const auto source_first=static_cast<long double>(range.index)*code.symbol_samples();
        const auto begin=state.catalog->boundary(reference,source_first,false,*state.cursor);
        const auto end=state.catalog->boundary(reference,source_first+code.symbol_samples(),false,*state.cursor);
        const auto padding=static_cast<long double>(step+2)*spacing;
        const auto cell_count=std::ceil((end-begin+2*padding)/spacing);
        if(!std::isfinite(cell_count) || cell_count<=0 || cell_count>0x40000000ULL)return decision;
        const auto cells=static_cast<std::size_t>(cell_count);
        events::Geometry geometry;
        geometry.physical_base=begin-padding;geometry.node_spacing=spacing;
        geometry.capture_first=lazy_history_first();geometry.capture_end=through;
        geometry.symbol_samples=code.symbol_samples();geometry.chip_samples=code.chip_samples();
        geometry.template_cells=cells;geometry.outputs=outputs;geometry.origin_first=cohort_first;
        geometry.step=step;geometry.fragment_cells=1024;geometry.subdivisions=subdivisions;
        geometry.sections=drift_sections;geometry.differential_width=differential_window;geometry.outer=outer_presence;
        if(!events::valid_geometry(geometry))return decision;
        struct Binding {std::size_t global=0,pieces=0;orig::Event event;long double observed_begin=0;};
        std::array<Binding,runtime::maximum_lanes+1> bindings{};std::size_t binding_count=0,total_pieces=0;
        const auto visit_fences=[&](auto a,auto b,auto consume) {state.processing->processing_fences().visit(a,b-a,consume);};
        try {
            const auto preflight_lane=[&](std::size_t global) {
                cancelled(stop);const auto candidate=lazy_family_lane(range.family,range.first+global);
                if(candidate.frequency!=reference.frequency || candidate.rate!=reference.rate || candidate.phase_lower!=reference.phase_lower ||
                   candidate.anchor.distance(reference.anchor)!=0 || std::abs(candidate.origin-reference.origin-
                    static_cast<long double>(global)*step*spacing)>.001L)
                    throw Error("Default linear lane is not on the global grid");
                const auto left=state.catalog->boundary(candidate,source_first,false,*state.cursor);
                const auto right=state.catalog->due(candidate,range.index,*state.cursor);
                const auto first=static_cast<std::uint64_t>(std::max(0.L,std::ceil(left)));
                if(first<lazy_history_first() || right>through || first>=right)
                    throw Error("Default linear Original crop unavailable");
                auto& binding=bindings[binding_count];binding.global=global;
                auto& event=binding.event;
                event.identity={state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),
                    reinterpret_cast<std::uintptr_t>(&state),1,range.index,candidate.phase_lower,range.family+1,candidate.id+1};
                event.epoch_sample=scheduled->epoch_sample;event.phase=candidate.phase_lower;event.index=range.index;
                event.symbol_samples=code.symbol_samples();event.chip_samples=code.chip_samples();
                event.anchor=candidate.anchor;event.origin=candidate.origin;event.rate=candidate.rate;
                event.symbol_source=source_first;event.sample_rate=config.sample_rate;event.fhss=scheduled->fhss_shape;
                event.begin=event.native_begin=first;event.end=event.native_end=right;
                event.history_first=lazy_history_first();event.capture_end=through;event.left_guard=event.right_guard=1;
                binding.observed_begin=candidate.observed_begin-source_first;
                binding.pieces=runtime::Context::piece_count(event,*scheduled_map(),visit_fences,stop);
                total_pieces=tile::add_size(total_pieces,binding.pieces);++binding_count;
            };
            if(cohort_first)preflight_lane(0);
            for(std::size_t j=0;j<outputs;++j)preflight_lane(cohort_first+j);
            if(state.processing->processing_fences().end()!=ledger_end || state.block_end!=key.block_end ||
               state.screen_capture_end!=key.capture)throw Error("Default linear processing ledger changed during preflight");
        } catch(const Error&) {if(stop.stop_requested())throw;++state.default_linear_fallbacks;return decision;}
        const auto retained=tile::add_size(sizeof(runtime::Cache)+128,events::retained_reservation(outputs));
        const auto archive_key=lazy_default_point_guard_archive_key(reference,range,geometry,subdivisions);
        auto& archive_slot=state.default_point_guard_archive;
        bool archive_plan=lazy_default_point_guard_archive_requested()&&archive_slot.matches(archive_key,geometry);
        detail::scheduled_default_point_family::Geometry rebuild{};
        std::size_t rebuild_bytes=0,rebuild_scratch=0;
        if(!archive_plan&&lazy_default_point_guard_archive_requested()&&rebuild_geometry) {
            rebuild=*rebuild_geometry;
            // This is ONE bounded family, never a bank of dense archives. A
            // failed optional extent/loan simply retains ordinary events::run.
            if(rebuild.physical_base==geometry.physical_base&&rebuild.node_spacing==geometry.node_spacing&&
               rebuild.template_cells==geometry.template_cells&&rebuild.step==geometry.step&&
               rebuild.capture_first<=geometry.capture_first&&rebuild.capture_end>=geometry.capture_end&&
               rebuild.origin_first<=geometry.origin_first&&rebuild.outputs&&
               geometry.origin_first-rebuild.origin_first<rebuild.outputs&&
               geometry.outputs<=rebuild.outputs-(geometry.origin_first-rebuild.origin_first)) {
                try {
                    namespace storage=detail::scheduled_default_point_guard_archive;
                    const storage::Layout layout{rebuild.physical_base,rebuild.node_spacing,rebuild.capture_first,rebuild.capture_end,
                        rebuild.template_cells,tile::multiply_size(rebuild.origin_first,rebuild.step),
                        tile::add_size(rebuild.template_cells,tile::multiply_size(rebuild.outputs-1,rebuild.step)),
                        rebuild.origin_first,rebuild.outputs,rebuild.step};
                    rebuild_bytes=archive_runtime::Entry::reservation(layout,0);
                    rebuild_scratch=detail::scheduled_default_point_guard_archive_rebuild::reservation(rebuild);
                }catch(const Error&){if(stop.stop_requested())throw;rebuild_bytes=0;rebuild_scratch=0;}
            }
        }
        const auto normal_core=events::reservation(geometry);
        const auto prepared_core=prepared_events::reservation(geometry);
        const auto context_reserve=runtime::Context::reservation(total_pieces,binding_count);
        std::size_t core_reserve=0,needed=0;
        // No archive borrow crosses room_for. A pressure eviction can change
        // the chosen path once; recompute the ordinary workspace before use.
        bool funded=false;
        for(unsigned attempt=0;attempt<3;++attempt) {
            core_reserve=archive_plan?prepared_core:normal_core;
            if(rebuild_bytes)core_reserve=std::max({normal_core,prepared_core,rebuild_scratch});
            const auto projection_reserve=(archive_plan||rebuild_bytes)?archive_runtime::ProjectionCache::reservation():0;
            needed=tile::add_size(rebuild_bytes,tile::add_size(retained,tile::add_size(core_reserve,tile::add_size(context_reserve,projection_reserve))));
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            auto used=sizeof(PatternCorrelator)+accounted_bytes;
            if(used>budget||needed>budget-used) {
                const auto execution_pinned=(state.serial_execution&&state.serial_execution->screen_active)||
                    std::any_of(state.executions.begin(),state.executions.end(),[](const auto& e){return e->screen_active;});
                if(!state.screens_running&&!state.colored_source_active&&!execution_pinned&&!archive_slot.borrowed()) {
                    try {cancelled(stop);room_for(needed);cancelled(stop);}
                    catch(const Error&){if(stop.stop_requested())throw;}
                    catch(const std::bad_alloc&){cancelled(stop);}
                    accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                    used=sizeof(PatternCorrelator)+accounted_bytes;
                }
            }
            if(archive_plan&&!archive_slot.matches(archive_key,geometry)){archive_plan=false;continue;}
            if(used>budget||needed>budget-used) {
                if(rebuild_bytes){rebuild_bytes=0;rebuild_scratch=0;continue;}
                ++state.default_linear_workspace;return decision;
            }
            funded=true;break;
        }
        if(!funded){++state.default_linear_workspace;return decision;}
        require(!state.screens_running,"Default linear point overlaps numerical workers");
        struct Loan {
            Impl& self;std::size_t bytes;
            Loan(Impl& s,std::size_t n):self(s),bytes(n){self.lazy_state()->transient_reservation+=n;}
            void retain(std::size_t n){require(n<=bytes,"Default linear retained workspace undercharged");bytes-=n;self.lazy_state()->transient_reservation-=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        } loan(*this,needed);
        auto archive_pin=archive_plan?archive_slot.pin(archive_key,geometry):archive_runtime::Slot::Pin{};
        if(archive_plan&&!archive_pin){++state.default_linear_fallbacks;return decision;}
        auto& execution=lazy_execution();
        struct Pin {LazyState::Execution& e;bool previous;
            explicit Pin(LazyState::Execution& x):e(x),previous(x.screen_active){e.screen_active=true;}
            ~Pin(){e.screen_active=previous;}
        } pin(execution);
        try {
            cancelled(stop);
            // Destruct unpublished private state BEFORE the surrounding Loan
            // releases its charge on any exception/cancellation path.
            std::unique_ptr<archive_runtime::Entry> rebuilt_archive;
            auto fresh=std::make_unique<runtime::Cache>();fresh->key=key;fresh->geometry=geometry;fresh->reserved_bytes=retained;
            runtime::Context context(drift_sections,differential_window);
            for(std::size_t k=0;k<binding_count;++k) {
                const auto& binding=bindings[k];
                context.add(binding.global,binding.event,*scheduled_map(),visit_fences,stop,binding.pieces);
                if(binding.global>=cohort_first && binding.global-cohort_first<outputs)
                    fresh->observed_source_begin[binding.global-cohort_first]=binding.observed_begin;
            }
            require(state.processing->processing_fences().end()==ledger_end && state.block_end==key.block_end &&
                state.screen_capture_end==key.capture,"Default linear processing ledger changed after preflight");
            fresh->bind();
            auto& pattern=execution.code;
            pattern.set_stream_phase_samples(reference.phase_lower);
            require(range.index<=UINT64_MAX/pattern.chips_per_symbol(),"Default linear private chip overflow");
            const auto first_chip=range.index*pattern.chips_per_symbol();pattern.prepare_symbol(first_chip,stop);
            auto source_nodes=lazy_point_source_nodes(reference.phase_lower,range.index,subdivisions,stop);
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            const auto coefficients=[&](std::uint64_t local){return pattern.values(first_chip+local);};
            const auto cell=[&](std::size_t,long double a,long double b) {
                cancelled(stop);return context.cell(0,a,b,subdivisions,amplitude,coefficients,
                    [&](double source){return source_nodes.value(source,[&]{++state.default_linear_endpoint_shapes;return pattern.shaped_values(first_chip,source);});},stop);
            };
            const auto visit=[&](std::uint64_t first,std::uint64_t finish,auto emit) {
                if(first>=finish)return;auto cursor=first;
                lazy_visit_blocks(first,finish,[&](const auto& block) {
                    cancelled(stop);const auto a=std::max(first,block.first);
                    const auto b=std::min<std::uint64_t>(finish,block.first+block.pcm.size());
                    if(a!=cursor)throw Error("Default linear physical input gap");
                    const auto frequency=banks[reference.frequency].frequency;
                    auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
                        static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    const auto rotation=std::polar(1.,tau*frequency/config.sample_rate);
                    // Restart at the ACTUAL raw capture fence, including its
                    // complete prefix. Processing Piece anchors are separate.
                    for(auto n=block.first;n<b;++n) {
                        if((n&255U)==0)cancelled(stop);
                        if(n>=a) {
                            if(cert::norm(oscillator)>2)throw Error("Default linear raw carrier magnitude unsupported");
                            const auto x=static_cast<double>(block.pcm[n-block.first]);
                            ++state.default_linear_input_rows;
                            emit(n,detail::CorrelationProjection{x*oscillator.real(),x*oscillator.imag(),
                                oscillator.real()*oscillator.real(),oscillator.imag()*oscillator.imag(),
                                oscillator.real()*oscillator.imag(),x*x});
                        }
                        oscillator*=rotation;
                    }
                    cursor=b;
                },false);
                if(cursor!=finish)throw Error("Default linear captured input incomplete");
            };
            if(rebuild_bytes) {
                namespace storage=detail::scheduled_default_point_guard_archive;
                const storage::Layout layout{rebuild.physical_base,rebuild.node_spacing,rebuild.capture_first,rebuild.capture_end,
                    rebuild.template_cells,tile::multiply_size(rebuild.origin_first,rebuild.step),
                    tile::add_size(rebuild.template_cells,tile::multiply_size(rebuild.outputs-1,rebuild.step)),
                    rebuild.origin_first,rebuild.outputs,rebuild.step};
                rebuilt_archive=std::make_unique<archive_runtime::Entry>(archive_key,layout,rebuild_bytes,stop,0);
                rebuilt_archive->processing_end=ledger_end;rebuilt_archive->block_end=state.block_end;
                rebuilt_archive->capture_through=rebuild.capture_end;
                const auto work=detail::scheduled_default_point_guard_archive_rebuild::run(rebuild,rebuild_scratch,
                    rebuilt_archive->archive,cell,visit,stop);
                require(state.processing->processing_fences().end()==ledger_end&&state.screen_capture_end==key.capture,
                    "Default guard archive rebuild ledger changed");
                cancelled(stop);
                if(archive_slot.publish(rebuilt_archive,stop)) {
                    loan.retain(rebuild_bytes);++state.default_fft_pool.guard_work.rebuilds;
                    state.default_fft_pool.guard_work.rebuild_rows+=work.input_rows;
                    archive_pin=archive_slot.pin(archive_key,geometry);
                    require(static_cast<bool>(archive_pin),"Default guard archive rebuild failed compatibility");
                }
                rebuilt_archive.reset();
            }
            const auto original=[&](std::size_t global,std::uint64_t n,bool need_q) {
                return context.row(global,n,need_q,[&](long double within,double gain) {
                    ++state.default_linear_source_rows;
                    const auto raw=pattern.shaped_values(first_chip,static_cast<double>(within));
                    events::Pair q;for(unsigned bit=0;bit<2;++bit)q[bit]=gain*pattern_limit_pcm(amplitude*raw[bit])/amplitude;
                    return q;
                });
            };
            std::unique_ptr<archive_runtime::ProjectionCache> projection_cache;
            struct CountProjection {
                archive_runtime::Statistics& stats;std::unique_ptr<archive_runtime::ProjectionCache>& cache;
                ~CountProjection(){if(cache){stats.projection_requests+=cache->requests;stats.projection_hits+=cache->hits;
                    stats.projection_fills+=cache->fills;stats.projection_input_rows+=cache->input_rows;}}
            } count_projection{archive_slot.stats,projection_cache};
            const auto classify=[&](std::size_t global,std::uint64_t a,std::uint64_t b){return context.classify(global,a,b);};
            if(archive_pin) {
                prepared_events::StripWork strip_work{};
                struct CountStrips {
                    archive_runtime::Statistics& stats;prepared_events::StripWork& work;
                    ~CountStrips(){stats.strip_attempts+=work.attempts;stats.strip_recovered_cells+=work.recovered_cells;
                        stats.strip_bulk_interiors+=work.bulk_interiors;stats.strip_edge_rows+=work.edge_rows;
                        stats.strip_projection_requests_avoided+=work.projection_requests_avoided;
                        stats.strip_coverage_fallbacks+=work.coverage_fallbacks;stats.strip_numeric_fallbacks+=work.numeric_fallbacks;}
                } count_strips{archive_slot.stats,strip_work};
                projection_cache=std::make_unique<archive_runtime::ProjectionCache>();
                const auto projection=[&](std::uint64_t row)->const detail::CorrelationProjection& {
                    return projection_cache->get(row,[&](std::uint64_t requested,auto emit) {
                        lazy_visit_blocks(requested,requested+1,[&](const auto& block) {
                            cancelled(stop);
                            const auto frequency=banks[reference.frequency].frequency;
                            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
                                static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                            const auto rotation=std::polar(1.,tau*frequency/config.sample_rate);
                            emit(block.first,block.pcm.size(),[&](std::uint64_t n) {
                                if(cert::norm(oscillator)>2)throw Error("Default prepared raw carrier magnitude unsupported");
                                const auto x=static_cast<double>(block.pcm[n-block.first]);++state.default_linear_input_rows;
                                const detail::CorrelationProjection p{x*oscillator.real(),x*oscillator.imag(),
                                    oscillator.real()*oscillator.real(),oscillator.imag()*oscillator.imag(),
                                    oscillator.real()*oscillator.imag(),x*x};
                                oscillator*=rotation;return p;
                            });
                        },false);
                    },stop);
                };
                fresh->result=prepared_events::run(fresh->geometry,core_reserve,archive_pin.archive(),projection,
                    classify,original,coefficients,stop,&strip_work);
                if(fresh->result.valid){++archive_slot.stats.prepared_batches;archive_slot.stats.prepared_lanes+=outputs;}
            }else fresh->result=events::run(fresh->geometry,core_reserve,cell,visit,classify,original,coefficients,stop);
            const auto& work=fresh->result.work;
            // Completed-run partition counts are separate from the visit and
            // shaping counters, which also retain partial/cancelled work.
            state.default_linear_partitioned_cells+=work.partitioned_cells;
            if(!fresh->result.valid){++state.default_linear_fallbacks;return decision;}
            require(sizeof(runtime::Cache)+128+fresh->result.working_bytes()<=retained,"Default linear cache reservation exceeded");
            cancelled(stop);state.default_linear_cache=std::move(fresh);loan.retain(retained);
            ++state.default_linear_batches;state.default_linear_lanes+=outputs;
            return score_cached(*state.default_linear_cache);
        } catch(const Error&) {if(stop.stop_requested())throw;++state.default_linear_fallbacks;return decision;}
        catch(const std::bad_alloc&) {cancelled(stop);++state.default_linear_fallbacks;return decision;}
    }
