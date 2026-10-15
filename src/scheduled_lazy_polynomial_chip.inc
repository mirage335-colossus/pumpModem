    // Diagnostic-only producer for the SAME Original per-chip statistic. The
    // cache is physical and source-independent; models remain private-source
    // objects. Every incomplete owner is projected only after all its physical
    // fragments have joined through the existing RuntimeProxy.
    bool lazy_colored_polynomial_query(const detail::scheduled_cold_colored::SourceRows& source,
            const detail::scheduled_polynomial_chip::SourceModels& models,
            long double source_translation,std::size_t base_start,std::size_t frequency_index,
            std::span<const std::size_t> relative_starts,
            std::span<const std::uint64_t> observed_begin,std::span<const std::uint64_t> observed_end,
            std::span<bool> history_missing,std::span<bool> capture_missing,
            std::vector<std::array<detail::scheduled_cold_colored::Summary,2>>& completed,
            std::uint64_t& direct_pairs,std::uint64_t& input_rows,std::stop_token stop,
            const detail::scheduled_partial_colored_runtime::SourceWitnesses* partial_witnesses=nullptr,
            const detail::scheduled_partial_colored_runtime::Request* partial_request=nullptr,
            detail::scheduled_partial_colored_runtime::Reply* partial_reply=nullptr) {
        namespace poly=detail::scheduled_polynomial_chip;
        namespace color=detail::scheduled_cold_colored;
        namespace cert=detail::scheduled_acquisition_certificate;
        namespace tile=detail::scheduled_acquisition_tile;
        if(!lazy_state()->colored_polynomial_cache || !models.matches(source) ||
           relative_starts.empty() || observed_begin.size()!=relative_starts.size() ||
           observed_end.size()!=relative_starts.size() || history_missing.size()!=relative_starts.size() ||
           capture_missing.size()!=relative_starts.size())return false;
        const auto lanes=relative_starts.size();
        auto& moment_cache=*lazy_state()->colored_polynomial_cache;
        const auto before=moment_cache.counters();
        struct MomentCounters {
            LazyState& state;detail::scheduled_polynomial_chip::MomentCache& cache;
            detail::scheduled_polynomial_chip::MomentCache::Counters before;
            ~MomentCounters(){const auto after=cache.counters();
                state.colored_polynomial_moment_builds+=after.builds-before.builds;
                state.colored_polynomial_moment_hits+=after.hits-before.hits;
                state.colored_polynomial_physical_rows+=after.physical_sample_rows-before.physical_sample_rows;}
        } moment_counters{*lazy_state(),moment_cache,before};
        completed.assign(lanes,{});
        for(auto& lane:completed)for(auto& bit:lane)bit.owner_set_certified=true;
        const auto frequency=banks[frequency_index].frequency;
        const auto step=std::polar(1.,tau*frequency/config.sample_rate);
        std::array<float,512> pcm{};
        std::array<Complex,512> oscillator{};
        std::array<std::uint8_t,512> observed{};
        const auto gather=[&](long double real,std::size_t count) {
            if(std::trunc(real)!=real || count>pcm.size())throw Error("polynomial physical input unsupported");
            pcm.fill(0);oscillator.fill(Complex{});observed.fill(0);
            const auto available_first=std::max({0.L,real,static_cast<long double>(lazy_history_first())});
            const auto available_end=std::min(real+count,static_cast<long double>(lazy_state()->capture_end));
            if(available_first>=available_end)return;
            const auto first=static_cast<std::uint64_t>(available_first),end=static_cast<std::uint64_t>(available_end);
            lazy_visit_blocks(first,end,[&](const auto& block) {
                cancelled(stop);
                const auto a=std::max(first,block.first),b=std::min(end,block.first+block.pcm.size());
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                for(auto i=block.first;i<a;++i)o*=step;
                for(auto i=a;i<b;++i) {
                    const auto index=static_cast<std::size_t>(static_cast<long double>(i)-real);
                    pcm[index]=block.pcm[i-block.first];oscillator[index]=o;observed[index]=1;
                    if(cert::norm(o)>2)throw Error("polynomial carrier exceeded original bound");
                    o*=step;
                }
            },false);
            input_rows+=count;
        };
        namespace partial=detail::scheduled_partial_colored_runtime;
        std::unique_ptr<partial::Session> partial_session;
        bool partial_done=false;
        if(partial_reply)*partial_reply={};
        if(partial_witnesses && partial_request && partial_reply &&
           partial_witnesses->matches(source,models,step)) {
          try {
            auto request=*partial_request;
            request.physical_translation=source_translation+base_start+relative_starts[0]-source.shift;
            request.observed_begin=observed_begin[0];request.observed_end=observed_end[0];
            request.available_begin=lazy_history_first();request.available_end=lazy_state()->capture_end;
            partial::CarrierFences fences(step);
            // Enumerate only actual required source runs, not all null time.
            // Keep ORIGINAL fence starts/counts and evaluate their actual seeds.
            for(const auto& run:source.runs) {
                cancelled(stop);
                const auto first=static_cast<long double>(run.physical_offset)+request.physical_translation;
                const auto end=first+run.count;
                const auto a=std::max({0.L,first,static_cast<long double>(request.observed_begin),static_cast<long double>(lazy_history_first())});
                const auto b=std::min({end,static_cast<long double>(request.observed_end),static_cast<long double>(lazy_state()->capture_end)});
                if(a<b && std::trunc(a)==a && std::trunc(b)==b)
                    lazy_visit_fences(static_cast<std::uint64_t>(a),static_cast<std::uint64_t>(b),[&](const auto& raw) {
                        const auto seed=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(raw.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                        fences.add(raw.first,raw.count,seed);
                    });
            }
            partial_session=std::make_unique<partial::Session>(source,models,*partial_witnesses,fences,request,*partial_reply,stop);
          }catch(const Error&){cancelled(stop);*partial_reply={};}
           catch(const std::bad_alloc&){cancelled(stop);*partial_reply={};}
        }
        const auto completed_owner=[&](std::size_t j,std::uint64_t owner) {
            if(j || !partial_session || !partial_session->supported())return;
            // Every cell here was finalized across ALL its runs before join.
            // Future microbatch lanes have different charged thresholds and
            // must still finish their own complete production statistics.
            if(partial_session->completed_prefix(owner,completed[0])) {
                partial_done=true;
                for(auto& bit:completed[0])bit.valid=false; // never cache as full Summary
            }
        };
        const std::array<std::size_t,1> zero_start{0};
        ++lazy_state()->colored_polynomial_queries;
        for(const auto& cell:models.cells) {
            cancelled(stop);
            const auto original=cell.full?poly::original_errors(cell,lazy_state()->summary_union_energy,
                lazy_colored_carrier_cap,2,block_samples):std::array<cert::ReferenceErrors,2>{};
            for(std::size_t j=0;j<lanes;++j) {
                if(j==0 && partial_done)continue;
                const auto real=(static_cast<long double>(cell.physical)-source.shift)+source_translation+base_start+relative_starts[j];
                const bool complete_domain=cell.full && std::trunc(real)==real && real>=observed_begin[j] &&
                    real+34<=observed_end[j] && real>=lazy_history_first() && real+34<=lazy_state()->capture_end &&
                    real+34<=lazy_state()->screen_capture_end;
                bool used=false;
                if(complete_domain) {
                    // Within this receiver, an existing captured prefix and its
                    // Original restart fences are immutable. A different screen
                    // batch/frequency/input origin gets a distinct cache key.
                    const poly::PhysicalKey key{lazy_state()->screen_capture_end,frequency_index+1,
                        lazy_state()->screen_capture_end,std::bit_cast<std::uint64_t>(frequency),static_cast<std::uint64_t>(real),
                        1,lazy_state()->screen_capture_end};
                    auto* moments=lazy_state()->colored_polynomial_cache->find(key,stop);
                    if(!moments && lazy_state()->colored_polynomial_fft_requested) {
                        // FFT failure preserves the existing FIR miss path.
                        lazy_colored_polynomial_fft_fill(key,stop);
                        moments=lazy_state()->colored_polynomial_cache->find(key,stop);
                    }
                    if(!moments) {
                        gather(real,34);
                        if(std::all_of(observed.begin(),observed.begin()+34,[](auto value){return value!=0;}))
                            moments=&lazy_state()->colored_polynomial_cache->insert(key,
                                std::span<const float>(pcm).first(34),std::span<const Complex>(oscillator).first(34),stop);
                    }
                    if(moments) {
                        const auto fields=poly::contract(cell.model,*moments,original);
                        if(fields.supported) {
                            std::array<color::Summary,2> bounded;
                            for(unsigned bit=0;bit<2;++bit){bounded[bit].owner_set_certified=true;bounded[bit].retain(fields.intervals[bit]);}
                            if(bounded[0].valid && bounded[1].valid && bounded[0].nested && bounded[1].nested) {
                                for(unsigned bit=0;bit<2;++bit)poly::join(completed[j][bit],bounded[bit]);
                                ++lazy_state()->colored_polynomial_cells;used=true;
                            }
                        }
                    }
                    if(!used)++lazy_state()->colored_polynomial_refined_cells;
                }
                if(used){completed_owner(j,cell.owner);continue;}
                color::RuntimeProxy direct(1,34,lazy_state()->summary_union_energy,lazy_colored_carrier_cap,2,block_samples,0,true);
                for(std::size_t run_index=cell.run;run_index<source.runs.size();++run_index) {
                    const auto& run=source.runs[run_index];
                    if(run.first>=cell.end)break;
                    auto at=std::max(cell.first,run.first);
                    const auto end=std::min(cell.end,run.first+run.count);
                    while(at<end) {
                        cancelled(stop);
                        const auto n=std::min<std::size_t>(512,end-at);
                        const auto physical=tile::add_coordinate(run.physical_offset,at-run.first);
                        const auto first=(static_cast<long double>(physical)-source.shift)+source_translation+base_start+relative_starts[j];
                        if(std::trunc(first)!=first)throw Error("polynomial direct physical input unsupported");
                        const color::RuntimeProxy::Domain domain{
                            static_cast<std::size_t>(std::clamp(static_cast<long double>(observed_begin[j])-first,0.L,static_cast<long double>(n))),
                            static_cast<std::size_t>(std::clamp(static_cast<long double>(observed_end[j])-first,0.L,static_cast<long double>(n)))};
                        if(domain.first<domain.end) {
                            history_missing[j]=history_missing[j] || first+domain.first<lazy_history_first();
                            capture_missing[j]=capture_missing[j] || first+domain.end>lazy_state()->capture_end;
                            gather(first,n);
                            direct.add({std::span<const float>(pcm).first(n),std::span<const Complex>(oscillator).first(n),
                                    std::span<const std::uint8_t>(observed).first(n),physical},
                                {std::span<const std::array<Complex,2>>(source.rows).subspan(at,n),
                                    std::span<const std::uint8_t>(source.mask).subspan(at,n),physical},zero_start,
                                std::span<const color::ProxyRow>(source.geometry).subspan(at,n),stop,
                                std::span<const std::array<double,2>>(source.norms).subspan(at,n),
                                std::span<const color::RuntimeProxy::Domain>(&domain,1));
                        }
                        at+=n;
                    }
                }
                auto bounded=direct.finish();
                for(unsigned bit=0;bit<2;++bit)poly::join(completed[j][bit],bounded.front()[bit]);
                direct_pairs+=direct.sample_pairs();++lazy_state()->colored_polynomial_direct_cells;
                completed_owner(j,cell.owner);
            }
        }
        cancelled(stop);
        for(std::size_t j=0;j<lanes;++j)if(history_missing[j] || capture_missing[j])
            for(auto& bit:completed[j])bit.valid=false;
        return true;
    }
