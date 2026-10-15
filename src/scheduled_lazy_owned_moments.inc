    // Optional complete-statistic producer: native physical moments are shared;
    // ORIGINAL ownership/support is corrected using the exact processing ledger.
    // Unknown preserves the general Direct oracle, never Original admission.
    ColoredDemand lazy_colored_owned_moments(const detail::scheduled_cold_colored::SourceRows& source,
            std::size_t slot,const LazyState::Catalog::Lane& lane,std::uint64_t index,
            long double source_translation,std::size_t base_start,std::uint64_t begin,std::uint64_t end,
            long double displacement,double qmax,double pulse_slope,double jump,std::stop_token stop) {
        namespace own=detail::scheduled_owned_moment_runtime;namespace sparse=detail::scheduled_sparse_owned_spans;
        namespace fast_carrier=detail::scheduled_sparse_carrier;
        namespace windows=detail::scheduled_owned_fft_window_filter;
        namespace orig=detail::scheduled_original_partition;namespace corr=detail::scheduled_owned_moment_correction;
        namespace poly=detail::scheduled_polynomial_chip;namespace cert=detail::scheduled_acquisition_certificate;
        namespace color=detail::scheduled_cold_colored;namespace tile=detail::scheduled_acquisition_tile;
        auto& state=*lazy_state();++state.colored_owned_moment_queries;
        if(code.chip_samples()!=34 || code.symbol_samples()%34 || scheduled_calendar() || scheduled->fhss_shape.enabled ||
           !source.window_halo || !source.sealed || begin>=end || end>state.capture_end || !sparse::environment())return {};
        const auto symbol=code.symbol_samples();const auto owners=static_cast<std::size_t>(symbol/34+2);
        constexpr std::size_t maximum_pieces=8192;
        sparse::Limits limits;limits.max_spans=16384;limits.max_edits=8192;limits.max_edit_rows=16384;
        const auto* window_mode=std::getenv("DATAPUMP_SCHED_COLORED_OWNED_FFT_WINDOWS");
        const bool filter_windows=state.colored_polynomial_fft_requested && window_mode && std::string_view(window_mode)=="1";
        const auto selector_bytes=filter_windows?windows::View::reservation(owners):0;
        const auto base_transient=tile::add_size(65536,tile::add_size(sparse::Proof::reservation(owners,limits),
            tile::add_size(tile::multiply_size(2*maximum_pieces,sizeof(orig::Piece)),
                tile::add_size(tile::multiply_size(2*limits.max_edit_rows,sizeof(corr::RowEdit)),
                    tile::multiply_size(2*owners,sizeof(corr::OwnerCount)+sizeof(corr::OwnerDelta)+sizeof(corr::MathCell))))));
        // Independent fixed fetch backing cannot overwrite the sparse512
        // point tile. Charge its complete arrays and alignment/slack BEFORE
        // the loan, even though the additional stack storage is under1KiB.
        constexpr auto moment_fetch_bytes=sizeof(std::array<float,34>)+sizeof(std::array<Complex,34>)+
            sizeof(std::array<std::uint8_t,34>)+128;
        const auto transient=tile::add_size(moment_fetch_bytes,tile::add_size(base_transient,
            tile::add_size(selector_bytes,state.colored_owned_fast_carrier_requested?
                sizeof(std::array<fast_carrier::Fence,512>)+128:0)));
        auto& model=state.colored_polynomial_sources[slot];
        const bool rebuild=!model || !model->matches_native(source);
        const auto model_bytes=rebuild?poly::SourceModels::reservation(symbol):0;
        const auto cache_bytes=state.colored_polynomial_cache?0:poly::MomentCache::reservation(8192);
        const auto needed=tile::add_size(transient,tile::add_size(model_bytes,cache_bytes));
        const auto affordable=[&] {accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            return sizeof(PatternCorrelator)+accounted_bytes<=budget && needed<=budget-sizeof(PatternCorrelator)-accounted_bytes;};
        if(!affordable()) {
            while(!affordable()) {
                cancelled(stop);require(!state.screens_running,"owned moment loan overlaps numerical workers");
                const auto binding=detail::scheduled_source_samples::binding;if(!binding.reclaim)break;
                const auto before=budget;const auto granted=binding.reclaim(binding.pressure_context);cancelled(stop);
                if(!granted)break;require(budget>before,"owned moment cohort reclaim did not grow workspace");
            }
        }
        if(!affordable()){++state.colored_owned_moment_workspace;return {};}
        struct Loan {Impl& self;std::size_t bytes;
            Loan(Impl& s,std::size_t n):self(s),bytes(n){self.lazy_state()->transient_reservation+=bytes;}
            void retain(std::size_t n){require(n<=bytes,"owned moment reservation exceeded");bytes-=n;self.lazy_state()->transient_reservation-=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        } loan(*this,needed);
        own::Counters actual_work;std::uint64_t actual_input_rows=0;
        struct WorkCounts {LazyState& s;own::Counters& c;std::uint64_t& rows;
            ~WorkCounts(){s.colored_owned_moment_poly_failures+=c.polynomial_failures;s.colored_owned_moment_poly_refinements+=c.polynomial_refinements;s.colored_owned_moment_poly_cells+=c.polynomial_cells;s.colored_owned_moment_direct_cells+=c.direct_cells;
                s.colored_owned_moment_direct_rows+=c.direct_rows;s.colored_owned_moment_edit_rows+=c.edit_rows;
                s.colored_owned_moment_source_segments+=c.source_segments;s.colored_owned_moment_input_rows+=rows;
                s.colored_demand_input_rows+=rows;s.colored_demand_pairs+=c.direct_rows+c.edit_rows;}
        } work_counts{state,actual_work,actual_input_rows};
        try {
            if(rebuild) {
                state.colored_partial_sources[slot].reset();
                auto fresh=std::make_unique<poly::SourceModels>(source,symbol,poly::SourceModels::NativeTemplateTag{},stop);
                const auto charge=fresh->working_bytes();require(charge<=model_bytes,"owned source model reservation exceeded");
                cancelled(stop);model=std::move(fresh);loan.retain(charge);++state.colored_polynomial_source_builds;
            }
            if(!state.colored_polynomial_cache) {
                auto fresh=std::make_unique<poly::MomentCache>(8192,cache_bytes);
                const auto charge=fresh->working_bytes();require(charge<=cache_bytes,"owned moment cache reservation exceeded");
                cancelled(stop);state.colored_polynomial_cache=std::move(fresh);loan.retain(charge);
            }
            orig::Event event;
            // These producer IDs have event-local lifetime. No partition/field
            // object is reused after this pinned source/ledger/candidate call.
            event.identity={state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),
                reinterpret_cast<std::uintptr_t>(&source),1,index,lane.phase_lower,lane.id+1,lane.id+1};
            event.epoch_sample=scheduled->epoch_sample;event.phase=lane.phase_lower;event.index=index;
            event.symbol_samples=symbol;event.chip_samples=34;event.anchor=lane.anchor;event.origin=lane.origin;event.rate=lane.rate;
            event.symbol_source=static_cast<long double>(index)*symbol;event.sample_rate=config.sample_rate;
            event.begin=event.native_begin=begin;event.end=event.native_end=end;event.history_first=lazy_history_first();
            event.capture_end=state.capture_end;event.displacement=displacement;event.left_guard=event.right_guard=1;
            if(!sparse::valid(event))return {};
            std::vector<orig::Piece> pieces;pieces.reserve(maximum_pieces);
            auto cursor=begin;
            state.processing->processing_fences().visit(begin,end-begin,[&](const auto& raw) {
                cancelled(stop);if(raw.original_first>cursor)throw Error("owned moments processing ledger gap");
                if(raw.original_count>std::numeric_limits<std::uint64_t>::max()-raw.original_first)
                    throw Error("owned moments processing ledger overflow");
                const auto finish=std::min(end,raw.original_first+raw.original_count);
                const orig::ProcessingFence fence{raw.original_first+1,event.identity.capture,cursor,finish};
                while(cursor<finish) {
                    if(pieces.size()==maximum_pieces)throw Error("owned moments processing piece cap");
                    auto piece=sparse::make_piece(event,fence,cursor,*scheduled_map(),stop);
                    cursor=piece.end;pieces.push_back(std::move(piece));
                }
            });
            if(cursor!=end)return {};
            const auto translation=source_translation+base_start-source.shift;
            const sparse::NativeInventoryView<poly::SourceModels::NativeOwnerSpan> inventory{
                sparse::source_identity(event.identity),source.key.rate,symbol,34,model->native_spans,true};
            auto proof=sparse::partition(event,pieces,inventory,translation,limits,stop);
            state.colored_owned_moment_geometry_probes+=proof.probes.total();
            if(!proof.complete){++state.colored_owned_moment_partition_fallbacks;return {};}
            // One index per pinned event. No Original membership claim is
            // attached to prefetched physical moments or the optional +17 row.
            std::optional<windows::View> window_filter;
            if(filter_windows) {
                const auto* next_mode=std::getenv("DATAPUMP_SCHED_COLORED_OWNED_FFT_WINDOWS_NEXT17");
                const bool next17=!next_mode || std::string_view(next_mode)!="0";
                window_filter.emplace(source,*model,translation,windows::Crop{event.native_begin,event.native_end,
                    event.history_first,std::min(event.capture_end,state.screen_capture_end)},next17,stop);
                require(window_filter->working_bytes()<=selector_bytes,"owned FFT selector exceeded reservation");
            }
            const auto frequency=banks[lane.frequency].frequency;const auto step=std::polar(1.,tau*frequency/config.sample_rate);
            std::array<float,512> pcm{};std::array<Complex,512> oscillator{};std::array<std::uint8_t,512> observed{};
            std::array<float,34> moment_pcm{};std::array<Complex,34> moment_oscillator{};
            std::array<std::uint8_t,34> moment_observed{};
            std::uint64_t tile_first=std::numeric_limits<std::uint64_t>::max();
            std::unique_ptr<std::array<fast_carrier::Fence,512>> sparse_fences;
            if(state.colored_owned_fast_carrier_requested)
                sparse_fences=std::make_unique<std::array<fast_carrier::Fence,512>>();
            std::size_t sparse_fence_count=0;bool sparse_pcm_tile=false;
            // Both scratch owners retain the SAME complete raw recurrence.
            // Only writes to the512-sample owner invalidate its point cache.
            const auto gather_into=[&](std::uint64_t first,std::size_t n,auto& target_pcm,
                    auto& target_oscillator,auto& target_observed) {
                if(n>512 || n>target_pcm.size() || n>target_oscillator.size() || n>target_observed.size() ||
                   first>state.capture_end || n>state.capture_end-first)throw Error("owned moments physical extent unavailable");
                target_pcm.fill(0);target_oscillator.fill({});target_observed.fill(0);
                lazy_visit_blocks(first,first+n,[&](const auto& raw) {
                    cancelled(stop);const auto a=std::max(first,raw.first),b=std::min(first+n,raw.first+raw.pcm.size());
                    auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(raw.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    ++state.colored_owned_moment_raw_fences;
                    if(state.shared_capture)state.colored_owned_moment_pcm_copy_rows+=raw.pcm.size();
                    for(auto p=raw.first;p<a;++p){o*=step;++state.colored_owned_moment_recurrence_steps;}
                    for(auto p=a;p<b;++p){const auto i=static_cast<std::size_t>(p-first);target_pcm[i]=raw.pcm[p-raw.first];target_oscillator[i]=o;target_observed[i]=1;
                        ++state.colored_owned_moment_pcm_copy_rows;
                        if(cert::norm(o)>2)throw Error("owned moment carrier exceeds Original cap");o*=step;++state.colored_owned_moment_recurrence_steps;}
                },false);
                actual_input_rows+=n;
            };
            const auto gather=[&](std::uint64_t first,std::size_t n) {
                tile_first=std::numeric_limits<std::uint64_t>::max();sparse_pcm_tile=false;sparse_fence_count=0;
                gather_into(first,n,pcm,oscillator,observed);
            };
            // Sparse queries copy PCM and bind actual raw fences ONCE per tile;
            // they perform no earlier-row carrier recurrence or norm traversal.
            const auto gather_sparse_pcm=[&](std::uint64_t first,std::size_t n) {
                tile_first=std::numeric_limits<std::uint64_t>::max();sparse_pcm_tile=false;sparse_fence_count=0;
                if(!sparse_fences || n>512 || first>state.capture_end || n>state.capture_end-first)
                    throw Error("owned sparse carrier physical extent unavailable");
                pcm.fill(0);observed.fill(0);
                lazy_visit_blocks(first,first+n,[&](const auto& raw) {
                    cancelled(stop);const auto a=std::max(first,raw.first),b=std::min(first+n,raw.first+raw.pcm.size());
                    if(a>=b)return;
                    if(raw.pcm.empty() || raw.pcm.size()>512 || raw.first>std::numeric_limits<std::uint64_t>::max()-raw.pcm.size() ||
                       sparse_fence_count==sparse_fences->size())
                        throw Error("owned sparse raw fence capacity exceeded");
                    const auto seed=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(raw.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    auto& fence=(*sparse_fences)[sparse_fence_count];
                    fence={state.screen_capture_end,reinterpret_cast<std::uintptr_t>(state.processing.get()),lane.frequency+1,
                        raw.first,raw.pcm.size(),seed,step};
                    if(sparse_fence_count && raw.first<(*sparse_fences)[sparse_fence_count-1].first+(*sparse_fences)[sparse_fence_count-1].count)
                        throw Error("owned sparse raw fences overlap");
                    ++sparse_fence_count;++state.colored_owned_moment_raw_fences;
                    std::copy_n(raw.pcm.begin()+static_cast<std::ptrdiff_t>(a-raw.first),static_cast<std::size_t>(b-a),
                        pcm.begin()+static_cast<std::ptrdiff_t>(a-first));
                    std::fill(observed.begin()+static_cast<std::ptrdiff_t>(a-first),observed.begin()+static_cast<std::ptrdiff_t>(b-first),1);
                    state.colored_owned_moment_pcm_copy_rows+=b-a;
                    // lazy_visit_blocks copies full raw fences into its shared
                    // capture scratch before exposing the clipped view.
                    if(state.shared_capture)state.colored_owned_moment_pcm_copy_rows+=raw.pcm.size();
                },false);
                actual_input_rows+=n;tile_first=first;sparse_pcm_tile=true;
            };
            const auto sample=[&](std::uint64_t p)->std::optional<corr::RowSample> {
                if(p<event.history_first || p>=event.capture_end)return {};
                const auto first=std::max(p-p%512,event.history_first);
                const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(512,event.capture_end-first));
                if(tile_first!=first){
                    if(sparse_fences)gather_sparse_pcm(first,n);
                    else {gather(first,n);tile_first=first;}
                }
                const auto i=static_cast<std::size_t>(p-first);
                if(i>=512 || !observed[i])return {};
                if(sparse_pcm_tile) {
                    const auto fence_begin=sparse_fences->begin(),fence_end=fence_begin+static_cast<std::ptrdiff_t>(sparse_fence_count);
                    auto found=std::upper_bound(fence_begin,fence_end,p,[](auto physical,const auto& fence){return physical<fence.first;});
                    if(found!=fence_begin) {
                        --found;++state.colored_owned_moment_carrier_points;
                        const auto point=fast_carrier::carrier_at(*found,static_cast<std::size_t>(p-found->first));
                        if(fast_carrier::carrier_point_matches(point,*found,p) &&
                           cert::add(fast_carrier::carrier_norm_upper(point.center),point.radius)<=2)
                            return corr::RowSample{pcm[i],point.center,{}, {},point.radius};
                    }
                    ++state.colored_owned_moment_carrier_fallbacks;
                    // Unknown/cap failure rejoins the exact tile producer. Keep
                    // that exact tile for subsequent sparse rows until another
                    // point tile is requested. Independent34-row moment fetches
                    // cannot overwrite this owner or invalidate its marker.
                    gather(first,n);tile_first=first;
                    if(!observed[i])return {};
                }
                return corr::RowSample{pcm[i],oscillator[i],{}, {}};
            };
            const auto before=state.colored_polynomial_cache->counters();
            struct MomentCounts {LazyState& s;poly::MomentCache& cache;poly::MomentCache::Counters before;
                ~MomentCounts(){const auto after=cache.counters();s.colored_polynomial_moment_builds+=after.builds-before.builds;
                    s.colored_polynomial_moment_hits+=after.hits-before.hits;s.colored_polynomial_physical_rows+=after.physical_sample_rows-before.physical_sample_rows;}
            } moment_counts{state,*state.colored_polynomial_cache,before};
            const auto fetch=[&](const poly::SourceModels::Cell& cell,std::uint64_t first) {
                if(first>state.screen_capture_end || 34>state.screen_capture_end-first)return poly::CellResult{};
                const poly::PhysicalKey key{state.screen_capture_end,lane.frequency+1,state.screen_capture_end,
                    std::bit_cast<std::uint64_t>(frequency),first,1,state.screen_capture_end};
                auto* moments=state.colored_polynomial_cache->find(key,stop);
                if(!moments && state.colored_polynomial_fft_requested) {
                    lazy_colored_polynomial_fft_fill(key,stop,window_filter?&*window_filter:nullptr);moments=state.colored_polynomial_cache->find(key,stop);
                }
                if(!moments) {
                    // A moment miss owns separate34-row storage. The current
                    // sparse/full512 point tile and its exact fence binding
                    // remain valid across this synchronous cache insertion.
                    gather_into(first,34,moment_pcm,moment_oscillator,moment_observed);
                    if(std::all_of(moment_observed.begin(),moment_observed.end(),[](auto x){return x!=0;}))
                        moments=&state.colored_polynomial_cache->insert(key,moment_pcm,moment_oscillator,stop);
                }
                // Consume the borrowed cache entry before any later mutation.
                return moments?poly::contract_math(cell.model,*moments):poly::CellResult{};
            };
            static const auto derivative=[] {
                double maximum=0;
                for(unsigned k=0;k<256;++k){double sum=0;
                    for(int j=-9;j<=9;++j){const auto a=static_cast<double>(k)/256-j-.5,b=static_cast<double>(k+1)/256-j-.5;
                        if(a< -8 || b>8)continue;
                        const auto d=cert::minus(cert::Interval::point(pattern_pulse(b)),cert::Interval::point(pattern_pulse(a)));
                        sum=cert::add(sum,cert::mul(256,std::max(std::abs(d.lo),std::abs(d.hi))));}
                    maximum=std::max(maximum,sum);}
                return cert::mul(2,maximum);
            }();
            (void)pulse_slope; // no shortened final chip in the supported C34 scope
            const corr::Identity identity{reinterpret_cast<std::uintptr_t>(&event),reinterpret_cast<std::uintptr_t>(&source),
                reinterpret_cast<std::uintptr_t>(&proof),reinterpret_cast<std::uintptr_t>(model.get()),
                reinterpret_cast<std::uintptr_t>(&frequency),reinterpret_cast<std::uintptr_t>(state.processing.get())};
            const own::Bounds bounds{qmax,derivative,jump,state.summary_union_energy,lazy_colored_carrier_cap,2,block_samples};
            auto result=own::contract(source,*model,event,pieces,proof,translation,identity,bounds,fetch,sample,actual_work,stop,state.colored_owned_cell_refine);
            cancelled(stop);
            const auto trace_cell=[&](const char* scope,const own::CellDiagnostic& diagnostic) {
                audio::trace::record("scheduled_owned_moment_cell",[&](auto& f){
                    f.add("scope",scope);f.add("epoch",config.stream_epoch);f.add("index",index);f.add("lane",lane.id);
                    f.add("terminal_failure",own::failure_name(result.failure));f.add("cell_failure",own::failure_name(diagnostic.failure));
                    f.add("failure_owner",result.failure_owner);f.add("owner",diagnostic.owner);f.add("bit",diagnostic.bit);
                    f.add("polynomial",diagnostic.polynomial);f.add("native_count",diagnostic.native_count);f.add("original_count",diagnostic.original_count);
                    f.add("errors_complete",diagnostic.errors_complete);f.add("originalized",diagnostic.originalized);
                    f.add("target_rate",lane.rate);f.add("reference_rate",source.key.rate);f.add("displacement",displacement);
                    const auto bit=std::min(diagnostic.bit,1U);const auto& fit=diagnostic.fields[bit];
                    f.add("q0_square",diagnostic.q0_square[bit]);f.add("residual_square",diagnostic.residual_square[bit]);
                    f.add("dot_error",diagnostic.errors[bit].dot);f.add("gram_error",diagnostic.errors[bit].gram);f.add("energy_error",diagnostic.errors[bit].energy);
                    f.add("fields_finite",color::finite(fit));
                    if(color::finite(fit)) {
                        const auto projection=color::projection(fit);const auto trace=cert::plus(fit.cc,fit.ss),det=color::determinant(fit);
                        const auto cutoff=cert::times(cert::times(cert::Interval::point(1e-12),trace),trace);
                        f.add("projection_valid",projection.valid);f.add("projection_nested",projection.nested);f.add("projection_rank",projection.rank);
                        f.add("trace_lower",trace.lo);f.add("trace_upper",trace.hi);f.add("determinant_lower",det.lo);f.add("determinant_upper",det.hi);
                        f.add("cutoff_lower",cutoff.lo);f.add("cutoff_upper",cutoff.hi);
                        f.add("cc_lower",fit.cc.lo);f.add("cc_upper",fit.cc.hi);f.add("ss_lower",fit.ss.lo);f.add("ss_upper",fit.ss.hi);
                        f.add("cs_lower",fit.cs.lo);f.add("cs_upper",fit.cs.hi);f.add("energy_lower",fit.energy.lo);f.add("energy_upper",fit.energy.hi);
                    }
                });
            };
            if(result.first_refinement.failure!=own::Failure::none && !state.colored_owned_moment_poly_failures)
                trace_cell("first failed polynomial owner before local Direct retry",result.first_refinement);
            if(!result.valid){if(!state.colored_owned_moment_numerical_fallbacks)trace_cell("first terminal owned query failure",result.terminal);
                ++state.colored_owned_moment_numerical_fallbacks;return {};}
            ++state.colored_owned_moment_completed;
            ColoredDemand out{result.chips,0,0,0,true};out.whole=result.whole;out.owned_whole=true;return out;
        } catch(const Error&) {cancelled(stop);++state.colored_owned_moment_errors;return {};}
          catch(const std::bad_alloc&) {cancelled(stop);++state.colored_owned_moment_workspace;return {};}
    }
