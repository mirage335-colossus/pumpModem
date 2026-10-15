    // General membership experiment. It enumerates the ORIGINAL processing
    // pieces, with at most512 local ORIGINAL private coefficient refinements.
    // Stored SOURCE q0 and physical PCM feed an independently bounded Direct
    // statistic. Exact owner/mask/count decisions are event-local, not cached
    // as fractional SourceRows geometry or reused by the strict FFT producer.
    ColoredDemand lazy_colored_owned_query(const detail::scheduled_cold_colored::SourceRows& source,
            const LazyState::Catalog::Lane& lane,std::uint64_t index,long double source_translation,
            std::size_t base_start,std::uint64_t observed_begin,std::uint64_t observed_end,
            long double displacement,double qmax,double pulse_slope,double gate_slope,double jump,
            std::stop_token stop) {
        namespace color=detail::scheduled_cold_colored;namespace cert=detail::scheduled_acquisition_certificate;
        namespace tile=detail::scheduled_acquisition_tile;
        ++lazy_state()->colored_general_queries;
        if(scheduled_calendar() || !source.sealed || source.runs.empty() ||
           !std::isfinite(displacement) || displacement<=0 || displacement>.25L ||
           observed_begin>=observed_end || observed_end>lazy_state()->capture_end ||
           observed_begin<lazy_history_first() || !lazy_reference_environment())return {};
        const auto symbol=code.symbol_samples(),chip=code.chip_samples();
        if(!symbol || !chip || chip>1023 || index>=0x10000000000000ULL/symbol)return {};
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(lane.phase_lower);
        const auto frequency=banks[lane.frequency].frequency;
        const auto step=std::polar(1.,tau*frequency/config.sample_rate);
        color::OwnedProxy proxy(color::SourceRows::maximum_rows,lazy_state()->summary_union_energy,lazy_colored_carrier_cap,2,block_samples);
        // Existing caller scratch covers these fixed arrays and the two small
        // reducers. Check explicitly rather than silently exceeding its loan.
        constexpr std::size_t block=512;
        constexpr std::size_t fixed_bytes=block*(sizeof(float)+sizeof(Complex)+2*sizeof(std::uint8_t)+
            sizeof(std::array<Complex,2>)+sizeof(std::array<double,2>)+sizeof(color::ProxyRow));
        if(tile::add_size(fixed_bytes,proxy.working_bytes())>65536)return {};
        std::array<float,block> pcm{};std::array<Complex,block> oscillator{};
        std::array<std::uint8_t,block> observed{},mask{};
        std::array<std::array<Complex,2>,block> rows{};
        std::array<std::array<double,2>,block> norms{};
        std::array<color::ProxyRow,block> geometry{};
        std::size_t run_index=0,row_offset=0;
        bool complete=true;
        std::uint64_t geometry_rows=0,input_rows=0,pieces=0,owner_changes=0,mask_changes=0,original_rows=0;
        struct Work {
            LazyState& state;color::OwnedProxy& proxy;
            std::uint64_t &geometry_rows,&input_rows,&pieces,&owner_changes,&mask_changes,&original_rows;
            ~Work(){state.colored_general_geometry_rows+=geometry_rows;state.colored_general_pieces+=pieces;
                state.colored_general_owner_changes+=owner_changes;state.colored_general_mask_entrants+=mask_changes;
                state.colored_general_original_rows+=original_rows;
                state.colored_general_chip_pairs+=proxy.chip_pairs();state.colored_general_whole_pairs+=proxy.whole_pairs();
                state.colored_demand_pairs+=proxy.chip_pairs()+proxy.whole_pairs();state.colored_demand_input_rows+=input_rows;}
        } work{*lazy_state(),proxy,geometry_rows,input_rows,pieces,owner_changes,mask_changes,original_rows};
        const auto row_at=[&](std::uint64_t physical)->std::optional<std::size_t> {
            while(run_index<source.runs.size()) {
                const auto& run=source.runs[run_index];
                const auto first=(static_cast<long double>(run.physical_offset)-source.shift)+source_translation+base_start;
                if(std::trunc(first)!=first || !std::isfinite(first))throw Error("owned proxy source translation is not integral");
                const auto end=first+run.count;
                if(static_cast<long double>(physical)>=end){++run_index;row_offset=0;continue;}
                if(static_cast<long double>(physical)<first)return {};
                row_offset=static_cast<std::size_t>(static_cast<long double>(physical)-first);
                return run.first+row_offset;
            }
            return {};
        };
        const auto fp=cert::mul(2048*std::numeric_limits<double>::epsilon(),cert::add(1,qmax));
        const auto delta=cert::add(cert::up(static_cast<double>(displacement)),
            cert::mul(2*std::numeric_limits<double>::epsilon(),cert::add(1,symbol)));
        // The regular finite row is a SUBSET of integer translates of the
        // public piecewise-linear pulse. On each of its 256 common phase
        // intervals sum |p'| first, then take the maximum. |private chip|<2
        // is the same mapper bound used by the preceding 34*max|p'| contract.
        // This is a public derivative bound, not an estimated template error.
        // Endpoint jumps at +/-8 remain covered by the separate jump term.
        static const auto translates=[] {
            double maximum=0;
            for(unsigned k=0;k<256;++k){double sum=0;
                for(int j=-9;j<=9;++j){const auto a=static_cast<double>(k)/256-j-.5,b=static_cast<double>(k+1)/256-j-.5;
                    if(a< -8 || b>8)continue;
                    const auto difference=cert::minus(cert::Interval::point(pattern_pulse(b)),cert::Interval::point(pattern_pulse(a)));
                    sum=cert::add(sum,cert::mul(256,std::max(std::abs(difference.lo),std::abs(difference.hi))));}
                maximum=std::max(maximum,sum);}
            return cert::mul(2,maximum);
        }();
        const auto derivative=cert::add(translates,symbol%chip?cert::mul(2,pulse_slope):0);
        bool original_prepared=false;
        auto& pattern=lazy_execution().code;
        const auto original_row=[&](long double within,double gain,std::size_t i) {
            // Missing central support or an unproved coordinate relation is
            // refined LOCALLY. The cap prevents this optional bridge from
            // silently becoming a whole private-source replay.
            if(original_rows==512)return false;
            if(!original_prepared){pattern.set_stream_phase_samples(lane.phase_lower);
                pattern.prepare_symbol(index*pattern.chips_per_symbol(),stop);original_prepared=true;}
            auto q=pattern.shaped_values(index*pattern.chips_per_symbol(),static_cast<double>(within));
            const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
            for(unsigned bit=0;bit<2;++bit){q[bit]=gain*pattern_limit_pcm(amplitude*q[bit])/amplitude;
                if(!cert::finite(q[bit]))return false;norms[i][bit]=cert::norm(q[bit]);}
            rows[i]=q;++original_rows;return true;
        };
        const auto symbol_source=static_cast<long double>(index)*symbol;
        auto cursor=observed_begin;
        lazy_state()->processing->processing_fences().visit(observed_begin,observed_end-observed_begin,[&](const auto& fence) {
            cancelled(stop);if(!complete)return;
            if(fence.original_first>cursor)throw Error("owned proxy processing history has a gap");
            const auto limit=std::min(observed_end,fence.original_first+fence.original_count);
            while(cursor<limit && complete) {
                cancelled(stop);
                // SAME anchoring, null advance, active end and association as
                // accumulate_scheduled, including restart at every actual fence.
                const auto tick=phase.add_signed((static_cast<long double>(cursor)-lane.origin)*lane.rate);
                const auto location=scheduled_map()->locate(tick);
                if(!location.active) {
                    const auto next=scheduled_map()->source_to_physical(location.source,false);
                    const auto n=std::max(1.L,std::ceil(next.distance(tick)/lane.rate));
                    cursor+=static_cast<std::uint64_t>(std::min(n,static_cast<long double>(limit-cursor)));continue;
                }
                const auto count=std::max(1.L,std::ceil(SchedHopCoordinate{location.physical_end,0}.distance(tick)/lane.rate));
                const auto piece_end=cursor+static_cast<std::uint64_t>(std::min(count,static_cast<long double>(limit-cursor)));
                const auto piece_first=cursor;
                const auto source0=location.source.distance(lane.anchor)-symbol_source;
                ++pieces;
                for(auto first=piece_first;first<piece_end && complete;) {
                    cancelled(stop);const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(block,piece_end-first));
                    pcm.fill(0);oscillator.fill(Complex{});observed.fill(0);mask.fill(0);geometry.fill({});
                    bool any=false;
                    for(std::size_t i=0;i<n;++i) {
                        const auto physical=first+i;
                        const auto within=source0+static_cast<long double>(physical-piece_first)*lane.rate;
                        ++geometry_rows;
                        if(within<0)continue;
                        if(within>=2.L*symbol)throw Error("owned proxy source exceeded Original carry range");
                        if(within>=symbol)continue; // Original next-symbol carry, not this cold event.
                        const auto at=tick.add_signed(static_cast<long double>(physical-piece_first)*lane.rate);
                        auto gain=SchedHopSourceMap::gain(location,at);
                        if(scheduled->fhss_shape.enabled)gain*=scheduled->fhss_shape.gain(0,
                            at.sample%(2ULL*config.sample_rate)+at.fraction,config.sample_rate);
                        if(gain==0)continue;
                        if(!std::isfinite(gain) || gain<0 || gain>color::smooth_gain_upper()){complete=false;break;}
                        const auto cached=row_at(physical);
                        const auto owner=static_cast<std::uint64_t>(within/chip);
                        if(owner>=code.chips_per_symbol()){complete=false;break;}
                        // Executable complete-support proof: EVERY selected
                        // Original row is either a checked cached row or an
                        // explicitly bounded local Original construction.
                        if(!cached || !std::isfinite(source.source[*cached]) ||
                           std::abs(within-source.source[*cached])>displacement) {
                            if(!cached)++lazy_state()->colored_general_missing_rows;
                            if(!original_row(within,gain,i)){complete=false;break;}
                            geometry[i]={owner,0,fp};mask[i]=1;any=true;continue;
                        }
                        const auto k=*cached;const auto s=source.source[k];
                        rows[i]=source.rows[k];norms[i]=source.norms[k];mask[i]=1;any=true;
                        // These are the ACTUAL FP64 arguments supplied by the
                        // cached and Original shapers. The broad lattice delta
                        // admits this representation; it need not be charged
                        // as every selected row's coefficient displacement.
                        const auto argument_difference=cert::minus(cert::Interval::point(static_cast<double>(within)),
                            cert::Interval::point(static_cast<double>(s)));
                        const auto local_delta=std::max(std::abs(argument_difference.lo),std::abs(argument_difference.hi));
                        // Smooth error concerns the actual double shaper arguments;
                        // only pre-cast support-edge tests need the cast reserve.
                        const auto jump_delta=cert::add(local_delta,
                            cert::mul(2*std::numeric_limits<double>::epsilon(),cert::add(1,symbol)));
                        if(local_delta>delta){if(!original_row(within,gain,i)){complete=false;break;}
                            geometry[i]={owner,0,fp};continue;}
                        auto shape=cert::mul(local_delta,cert::divide(derivative,chip));
                        const auto edge=s/chip-.5L;
                        if(std::abs(edge-std::round(edge))*chip<=static_cast<long double>(jump_delta)+1e-10L)shape=cert::add(shape,jump);
                        const auto tail=symbol%chip;
                        if(tail){const auto center=static_cast<long double>(symbol-tail)+.5L*tail;
                            if(std::min(std::abs(s-center-8.L*chip),std::abs(s-center+8.L*chip))<=static_cast<long double>(jump_delta)+1e-10L)
                                shape=cert::add(shape,jump);}
                        const auto gd=cert::minus(cert::Interval::point(gain),cert::Interval::point(source.gain[k]));
                        const auto gain_error=std::max(std::abs(gd.lo),std::abs(gd.hi));
                        const auto residual=cert::add(cert::add(cert::mul(source.gain[k],shape),cert::mul(gain_error,cert::add(qmax,fp))),fp);
                        if(!std::isfinite(residual)){complete=false;break;}
                        geometry[i]={owner,0,residual};
                        if(!source.mask[k])++mask_changes;
                        else if(source.source[k]>=0 && source.source[k]<symbol &&
                                static_cast<std::uint64_t>(source.source[k]/chip)!=owner)++owner_changes;
                    }
                    if(!complete)break;
                    if(any) {
                        lazy_visit_blocks(first,first+n,[&](const auto& raw) {
                            cancelled(stop);const auto a=std::max(first,raw.first),b=std::min(first+n,raw.first+raw.pcm.size());
                            auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(raw.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                            for(auto p=raw.first;p<a;++p)o*=step;
                            for(auto p=a;p<b;++p){const auto i=static_cast<std::size_t>(p-first);
                                pcm[i]=raw.pcm[p-raw.first];oscillator[i]=o;observed[i]=1;
                                if(cert::norm(o)>2)throw Error("owned proxy carrier exceeded bound");o*=step;}
                        },false);
                        for(std::size_t i=0;i<n;++i)if(mask[i] && !observed[i]){complete=false;break;}
                        if(!complete)break;
                        input_rows+=n;
                        proxy.add({std::span<const float>(pcm).first(n),std::span<const Complex>(oscillator).first(n),
                            std::span<const std::uint8_t>(observed).first(n),first},
                            {std::span<const std::array<Complex,2>>(rows).first(n),std::span<const std::uint8_t>(mask).first(n),first},
                            std::span<const color::ProxyRow>(geometry).first(n),std::span<const std::array<double,2>>(norms).first(n),stop);
                    }
                    first+=n;
                }
                cursor=piece_end;
            }
        });
        // Count actual duplicate contractions and geometry work even if the
        // certificate later fails rank or a support check. No work vanishes
        // behind an Original-refinement fallback in these diagnostics.
        cancelled(stop);const auto result=proxy.finish(complete && cursor==observed_end,stop);
        if(!result.valid){++lazy_state()->colored_general_unknown;return {};}
        ++lazy_state()->colored_general_completed;
        ColoredDemand out{result.chips,0,0,0,true};out.whole=result.whole;out.owned_whole=true;return out;
    }
