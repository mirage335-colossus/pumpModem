// Include in Impl after the whole-point entry points. Numerical preparation
// only: never advances lanes, trials, owners, diagnostics, absence or capture.
    static bool lazy_default_point_guard_prepass_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_POINT_GUARD_PREPASS");
        if(!value||std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","point guard prepass requires0or1");
        return lazy_default_point_fft_requested()&&lazy_default_linear_requested();
    }
    std::size_t lazy_default_point_guard_provision(
            const detail::scheduled_default_point_family::Geometry& family,std::size_t maximum_pieces,std::size_t summary_outputs) {
        namespace runtime=detail::scheduled_default_linear_runtime;
        namespace events=detail::scheduled_default_linear_events;
        namespace tile=detail::scheduled_acquisition_tile;
        if(!lazy_default_point_guard_prepass_requested())return 0;
        events::Geometry g;g.physical_base=family.physical_base;g.node_spacing=family.node_spacing;
        g.capture_first=family.capture_first;g.capture_end=family.capture_end;g.symbol_samples=code.symbol_samples();g.chip_samples=code.chip_samples();
        const auto cohort=detail::scheduled_default_guard_cohort::parse(std::getenv("DATAPUMP_SCHED_GUARD_COHORT"));
        const auto maximum_cohort=cohort==detail::scheduled_default_guard_cohort::Policy::single?1U:runtime::maximum_lanes;
        g.template_cells=family.template_cells;g.outputs=std::min<std::size_t>(maximum_cohort,family.outputs);
        g.origin_first=family.origin_first;g.step=family.step;g.fragment_cells=family.fragment_cells;
        g.subdivisions=lazy_linear_grid_subdivisions();g.sections=drift_sections;g.differential_width=differential_window;g.outer=outer_presence;
        if(!events::valid_geometry(g))return 0;
        const auto retained=tile::add_size(sizeof(runtime::Cache)+128,events::retained_reservation(g.outputs));
        const auto context_lanes=std::min(runtime::maximum_lanes+1,g.outputs+1);
        const auto context=runtime::Context::reservation(tile::multiply_size(maximum_pieces,context_lanes),context_lanes);
        if(context==SIZE_MAX)return 0;
        const auto core=std::max(events::reservation(g),tile::add_size(detail::scheduled_default_prepared_events::reservation(g),
            detail::scheduled_default_point_guard_archive_runtime::ProjectionCache::reservation()));
        const auto summaries=detail::scheduled_default_point_guard_summary::Store<detail::scheduled_default_point_fft_runtime::Key>::reservation(summary_outputs);
        const auto selection=tile::add_size(8192,tile::multiply_size(summary_outputs,sizeof(std::size_t)));
        return tile::add_size(tile::add_size(retained,context),tile::add_size(core,tile::add_size(summaries,selection)));
    }
    bool lazy_default_point_guard_identity(const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,
            detail::scheduled_default_point_fft_runtime::Key& key,
            detail::scheduled_default_point_family::Geometry& geometry,const LazyState::DueRange*& frozen,
            std::uint64_t& through) {
        namespace tile=detail::scheduled_acquisition_tile;
        auto& state=*lazy_state();
        if(!lazy_default_point_guard_prepass_requested()||config.search_arithmetic!=SearchArithmetic::default_mode||
           options.raw_reference||options.scheduled_raw_score_reference||options.amplitude_only||scheduled_calendar()||
           (scheduled->fhss_shape.enabled&&!scheduled->fhss_shape.smooth)||!shaped||!lazy_reference_environment()||
           drift_sections!=1||differential_window||resident_slot(lane.id))return false;
        const auto q=lazy_linear_grid_subdivisions();if(q!=2&&q!=4&&q!=8)return false;
        const auto at=std::find_if(state.batch_ranges.begin(),state.batch_ranges.end(),[&](const auto& r){
            return r.family==current.family&&r.index==current.index&&lane.origin_index>=r.first&&lane.origin_index<r.end;});
        frozen=at==state.batch_ranges.end()?&current:&*at;
        if(frozen->first>=frozen->end||frozen->end-frozen->first>tile::maximum_start_span||lane.origin_index<frozen->first||
           lane.origin_index>=frozen->end||frozen->index>UINT64_MAX/code.symbol_samples()||code.symbol_samples()>1048576||
           code.symbol_samples()%code.chip_samples()||code.symbol_samples()/code.chip_samples()<16)return false;
        const auto reference=lazy_family_lane(frozen->family,frozen->first);if(reference.rate<.99L||reference.rate>1.01L)return false;
        const auto source=static_cast<long double>(frozen->index)*code.symbol_samples();
        const auto begin=state.catalog->boundary(reference,source,false,*state.cursor);
        const auto end=state.catalog->boundary(reference,source+code.symbol_samples(),false,*state.cursor);
        const auto step=static_cast<std::size_t>(q/2);
        const auto spacing=static_cast<long double>(code.chip_samples())/(q*reference.rate),padding=static_cast<long double>(step+2)*spacing;
        const auto cells=std::ceil((end-begin+2*padding)/spacing);
        if(!std::isfinite(cells)||cells<=0||cells>0x40000000ULL)return false;
        key={state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),
            state.capture_fences?reinterpret_cast<std::uintptr_t>(state.capture_fences.get()):reinterpret_cast<std::uintptr_t>(&state.capture),
            frozen->index,frozen->family,frozen->first,frozen->end,q,begin-padding,spacing};
        key.pending_bulk=lazy_default_point_bulk_requested();key.bulk_scalar_bytes=key.pending_bulk?sizeof(float):0;
        key.compact_bulk=key.pending_bulk&&lazy_default_point_compact_requested();
        through=std::min({state.processing->processing_fences().end(),state.block_end,state.screen_capture_end,state.capture_end});
        geometry={key.physical_base,spacing,lazy_history_first(),through,static_cast<std::size_t>(cells),frozen->end-frozen->first,0,step,1024};
        return detail::scheduled_default_point_family::valid_geometry(geometry);
    }
    detail::scheduled_default_linear_runtime::Decision lazy_default_point_guard_summary(
            const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,double charged_threshold,
            std::stop_token stop,detail::scheduled_default_point_guard_summary::Summary* completed_summary=nullptr) {
        namespace wrapper=detail::scheduled_default_point_fft_runtime;
        namespace summary=detail::scheduled_default_point_guard_summary;
        detail::scheduled_default_linear_runtime::Decision out;
        if(completed_summary)*completed_summary={};
        if(!std::isfinite(charged_threshold)||charged_threshold<0)return out;
        cancelled(stop);wrapper::Key key;detail::scheduled_default_point_family::Geometry g;const LazyState::DueRange* frozen=nullptr;
        std::uint64_t through=0;if(!lazy_default_point_guard_identity(lane,current,key,g,frozen,through))return out;
        auto& pool=lazy_state()->default_fft_pool;const auto found=pool.find(key);if(found==pool.entries.size())return out;
        const auto& entry=*pool.entries[found];
        if(!entry.guard_summaries||entry.processing_end>lazy_state()->processing->processing_fences().end())return out;
        const auto origin=lane.origin_index-frozen->first;
        if(const auto* value=entry.guard_summaries->find(key,origin,through)) {
            out.scores=summary::scores(*value,charged_threshold);out.ready=out.scores.valid;
            if(out.ready){++pool.guard_work.hits;if(completed_summary)*completed_summary=*value;}
        }
        return out;
    }
    void lazy_default_point_guard_prepass(const LazyState::Catalog::Lane& lane,const LazyState::DueRange& current,
            double conservative_floor,std::stop_token stop) {
        namespace wrapper=detail::scheduled_default_point_fft_runtime;
        namespace summary=detail::scheduled_default_point_guard_summary;
        namespace tile=detail::scheduled_acquisition_tile;
        if(!std::isfinite(conservative_floor)||conservative_floor<0)return;
        wrapper::Key key;detail::scheduled_default_point_family::Geometry geometry;const LazyState::DueRange* frozen=nullptr;
        std::uint64_t through=0;if(!lazy_default_point_guard_identity(lane,current,key,geometry,frozen,through))return;
        auto& state=*lazy_state();auto& pool=state.default_fft_pool;auto& work=pool.guard_work;++work.calls;
        const auto ledger_end=state.processing->processing_fences().end(),block_end=state.block_end;
        const auto found=pool.find(key);if(found==pool.entries.size())return;
        // Detach before any allocation/room_for: optional cache reclamation may
        // clear other pool entries, never this completed prefix and its Store.
        struct Loan {Impl& self;std::size_t bytes=0;
            void add(std::size_t n){require(n<=SIZE_MAX-bytes&&n<=SIZE_MAX-self.lazy_state()->transient_reservation,"guard prepass loan overflow");
                bytes+=n;self.lazy_state()->transient_reservation+=n;}
            void retain(std::size_t n){require(n<=bytes,"guard prepass retained loan exceeded");bytes-=n;self.lazy_state()->transient_reservation-=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        }loan{*this};
        auto entry=std::move(pool.entries[found]);loan.add(entry->working_bytes());
        struct Restore {wrapper::Pool& pool;std::unique_ptr<wrapper::Entry>& entry;Loan& loan;
            ~Restore(){if(entry){const auto bytes=entry->working_bytes();const auto slot=pool.victim();pool.touch(*entry);pool.entries[slot]=std::move(entry);loan.retain(bytes);}}
        }restore{pool,entry,loan};
        try {
            cancelled(stop);
            if(entry->processing_end>ledger_end||entry->whole.empty()||entry->valid.size()!=entry->whole.size())return;
            const auto finalized=entry->pending?entry->finalized_prefix:entry->whole.size();
            if(!finalized||finalized>entry->whole.size())return;
            const auto visit_actual=[&](auto consume) {
                // Read the remaining ACTUAL due ranges, never lazy_for_each_due:
                // that routine commits its iterators and per-event checkpoint.
                for(const auto& due:state.due_ranges) {
                    if(due.family!=frozen->family||due.index!=frozen->index)continue;
                    const auto first=std::max(due.first,frozen->first),end=std::min(due.end,frozen->end);
                    for(auto at=first;at<end;++at) {
                        if((at&63U)==0)cancelled(stop);const auto global=at-frozen->first;
                        if(global>=finalized)break;
                        const auto candidate=lazy_family_lane(frozen->family,at);
                        const auto physical_end=state.catalog->due(candidate,frozen->index,*state.cursor);
                        if(physical_end>through)break;
                        if(resident_slot(candidate.id))continue;
                        const auto whole=wrapper::score(*entry,global);
                        if(!whole.ready||std::max(whole.scores.score[0],whole.scores.score[1])<conservative_floor)continue;
                        if(entry->guard_summaries&&entry->guard_summaries->find(key,global,through))continue;
                        consume(global,physical_end);
                    }
                }
            };
            std::size_t count=0;visit_actual([&](auto,auto){++count;});if(!count)return;
            const auto vector_bytes=tile::add_size(8192,tile::multiply_size(count,sizeof(std::size_t)));
            const bool grow_store=!entry->guard_summaries||entry->guard_summaries->capacity()<entry->whole.size();
            const auto store_bytes=grow_store?summary::Store<wrapper::Key>::reservation(entry->whole.size()):0;
            const auto needed=tile::add_size(vector_bytes,store_bytes);
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            auto used=sizeof(PatternCorrelator)+accounted_bytes;
            if(used>budget||needed>budget-used) {
                // No borrowed archive/source/candidate is live. Restore owns
                // this prefix even if unrelated optional entries are evicted.
                room_for(needed);accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
                used=sizeof(PatternCorrelator)+accounted_bytes;
            }
            if(used>budget||needed>budget-used){++work.workspace_fallbacks;return;}
            loan.add(needed);
            std::vector<std::size_t> selected;selected.reserve(count);
            visit_actual([&](auto global,auto){require(selected.size()<count,"guard prepass actual-due selection grew");selected.push_back(global);});
            if(selected.empty())return;
            require(selected.capacity()*sizeof(selected[0])+8192<=vector_bytes,"guard prepass selection capacity exceeded");
            std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
            if(grow_store) {
                auto fresh=std::make_unique<summary::Store<wrapper::Key>>(key,entry->whole.size(),entry->whole.size(),store_bytes);
                if(entry->guard_summaries)for(std::size_t global=0;global<entry->whole.size();++global) {
                    if((global&63U)==0)cancelled(stop);
                    if(const auto* old=entry->guard_summaries->find(key,global,through)) {
                        const auto candidate=lazy_family_lane(frozen->family,frozen->first+global);
                        const auto physical_end=state.catalog->due(candidate,frozen->index,*state.cursor);
                        require(fresh->insert(key,global,*old,physical_end,through,stop),"guard prepass summary prefix transfer failed");
                    }
                }
                const auto previous=entry->guard_summaries?entry->guard_summaries->working_bytes():0;
                entry->guard_summaries=std::move(fresh);
                // The old owner was part of the detached Entry's transient
                // loan; release it only after destruction, not on allocation.
                if(previous)loan.retain(previous);
            }
            work.peak_summary_bytes=std::max(work.peak_summary_bytes,entry->guard_summaries->working_bytes());
            // Cover only the current published-due cohort. No future Pieces or
            // speculative pending whole outputs enter the archive rebuild.
            const auto cohort_first=selected.front()/8*8;
            const auto cohort_end=std::min(finalized,tile::add_size(selected.back()/8*8,8));
            geometry.origin_first=cohort_first;geometry.outputs=cohort_end-cohort_first;
            for(const auto global:selected) {
                cancelled(stop);
                if(entry->guard_summaries->find(key,global,through))continue;
                const auto candidate=lazy_family_lane(frozen->family,frozen->first+global);
                if(resident_slot(candidate.id))continue;
                const auto physical_end=state.catalog->due(candidate,frozen->index,*state.cursor);
                if(physical_end>through)continue;
                const auto first8=global/8*8;
                const auto a=std::lower_bound(selected.begin(),selected.end(),first8);
                const auto b=std::lower_bound(a,selected.end(),tile::add_size(first8,8));
                const auto width=b-a>1?8U:1U;
                summary::Summary completed;
                ++work.candidates;
                auto decision=lazy_default_linear_point(candidate,current,conservative_floor,stop,true,&completed,width,&geometry);
                // Dense archive pressure or unsupported reconstruction must not
                // make the existing bounded direct guard unavailable.
                if(!decision.ready||!completed.valid)
                    decision=lazy_default_linear_point(candidate,current,conservative_floor,stop,true,&completed,width,nullptr);
                if((!decision.ready||!completed.valid)&&width==8)
                    decision=lazy_default_linear_point(candidate,current,conservative_floor,stop,true,&completed,0,nullptr);
                if(!decision.ready||!completed.valid){++work.guard_fallbacks;continue;}
                require(state.processing->processing_fences().end()==ledger_end&&state.block_end==block_end&&state.screen_capture_end==key.capture,
                    "guard prepass actual ledger changed");
                if(entry->guard_summaries->insert(key,global,completed,physical_end,through,stop))++work.completed;
            }
            cancelled(stop);
        }catch(const Error&){if(stop.stop_requested())throw;++work.workspace_fallbacks;}
        catch(const std::bad_alloc&){cancelled(stop);++work.workspace_fallbacks;}
    }
