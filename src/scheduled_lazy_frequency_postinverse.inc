// OPTIONAL source prototype. DATAPUMP_SCHED_POSTINVERSE=1 requests this path;
// unknown geometry/proof/workspace returns to the original family backend.
// No automatic enable/crossover or complete-execution improvement is claimed.
// Included inside PatternCorrelator::Impl, after lazy_plan. Depends on the
// released parallel Execution accessor and the metadata-preserving visitors.
// The old lazy_screen_range and original cold/refinement path remain available.
template<class T> bool lazy_screen_postinverse_family(std::span<LazyState::DueRange* const> ranges,std::stop_token stop) {
    namespace tile=detail::scheduled_acquisition_tile;
    namespace cert=detail::scheduled_acquisition_certificate;
    namespace fusion=detail::scheduled_frequency_moments;
    namespace recurrence=detail::scheduled_frequency_recurrence;
    namespace runtime=detail::scheduled_lazy_family_runtime;
    namespace post=detail::scheduled_frequency_postinverse;
    rx_cpu::Scope cpu_screen(rx_cpu::Stage::search_residual);
    auto& execution=lazy_execution();auto& pattern=execution.code;auto& cursor=execution.cursor;
    if(ranges.empty())return true;
    for(auto* range:ranges)if(range)range->exact_observation_mask=false;
    if(!lazy_reference_environment() || !shaped || scheduled->fhss_shape.enabled ||
       config.search_arithmetic==SearchArithmetic::fp64 || drift_sections!=1 || differential_window ||
       static_cast<double>(pattern.symbol_samples())/config.sample_rate<6)return false;
    struct ResetOnFailure {
        std::span<LazyState::DueRange* const> ranges;bool complete=false;
        ~ResetOnFailure(){if(!complete)for(auto* range:ranges)if(range){tile::wipe(range->screen);range->screen.clear();range->reference.clear();range->colored.clear();range->exact_observation_mask=false;}}
    } reset{ranges};
    struct Geometry {
        LazyState::DueRange* range=nullptr;LazyState::Catalog::Lane reference;
        long double displacement=0;double residual_square=0,residual_l1=0,residual_gram=0;
    };
    std::vector<Geometry> geometry;std::vector<std::size_t> starts;
    const auto old_scratch=execution.scratch_reservation;
    struct RestoreScratch {LazyState::Execution& execution;std::size_t previous;
        ~RestoreScratch(){execution.scratch_reservation=previous;}} restore{execution,old_scratch};
    const auto check=[&](std::size_t live) {
        execution.scratch_reservation=tile::add_size(old_scratch,live);
        require(tile::add_size(execution.numeric_bytes(),execution.scratch_reservation)<=execution.available_bytes,
            "implicit frequency family exceeds worker workspace");
        execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,
            tile::add_size(execution.numeric_bytes(),execution.scratch_reservation));
    };
    check(tile::add_size(tile::helper_reservation,tile::add_size(tile::multiply_size(ranges.size(),sizeof(Geometry)),
        tile::multiply_size(tile::maximum_start_span,sizeof(std::size_t)))));
    if(!ranges.front() || ranges.front()->end<=ranges.front()->first ||
       ranges.front()->end-ranges.front()->first>tile::maximum_start_span)return false;
    geometry.reserve(ranges.size());starts.reserve(ranges.front()->end-ranges.front()->first);
    for(auto* range:ranges) {
        cancelled(stop);
        if(!range || range->end<=range->first || range->end-range->first>tile::maximum_start_span)return false;
        // Optional postinverse prototype has no independent mask certificate.
        range->exact_observation_mask=false;
        range->colored.clear();range->colored_mode=0;range->colored_gate=1;range->colored_selected=0;range->colored_sample_pairs=0;
        if(range->index==std::numeric_limits<std::uint64_t>::max())return false;
        Geometry item;item.range=range;const auto target=lazy_family_lane(range->family,range->first);item.reference=target;
        if(config.search_arithmetic!=SearchArithmetic::fp64)for(std::size_t pair=0;pair<lazy_state()->catalog->pairs().size();++pair) {
            if(lazy_state()->catalog->pairs()[pair].rate!=1)continue;
            const auto id=lazy_state()->catalog->rank(pair,target.origin_index,target.phase_lower);if(!id)continue;
            const auto central=lazy_state()->catalog->describe(*id,cursor);if(central.anchor.distance(target.anchor)!=0)continue;
            item.reference=central;item.reference.frequency=target.frequency;break;
        }
        const auto& reference=item.reference;
        const auto source_first=static_cast<long double>(range->index)*pattern.symbol_samples();
        const auto first=lazy_state()->catalog->boundary(reference,source_first,false,cursor);
        const auto last=lazy_state()->catalog->boundary(reference,source_first+pattern.symbol_samples(),false,cursor);
        const auto aperture=std::max(std::abs(first-reference.origin),std::abs(last-reference.origin))+tile::maximum_start_span+2;
        std::size_t position=0,previous=0;
        for(auto i=range->first;i<range->end;++i,++position) {
            const auto lane=lazy_family_lane(range->family,i);const auto integer=std::round(lane.origin-reference.origin);
            if(integer<0 || integer>=tile::maximum_start_span || (position && integer<=previous))return false;
            const auto start=static_cast<std::size_t>(integer);previous=start;
            if(geometry.empty())starts.push_back(start);else if(position>=starts.size() || starts[position]!=start)return false;
            const auto rate_difference=std::abs(lane.rate-reference.rate);
            const auto origin_difference=std::abs(lane.rate*(reference.origin-lane.origin)+reference.rate*integer);
            item.displacement=std::max(item.displacement,origin_difference+rate_difference*aperture);
        }
        if(position!=starts.size())return false;
        if(item.displacement>0 && config.search_arithmetic==SearchArithmetic::fp64)return false;
        item.displacement+=256*std::numeric_limits<long double>::epsilon()*(1+scheduled->complex_geometry.physical_aperture_samples+
            std::abs(reference.origin)+pattern.symbol_samples());
        if(item.displacement>.25L)return false;
        if(!geometry.empty()) {
            const auto& common=geometry.front();const auto& base=common.reference;
            if(reference.origin!=base.origin || reference.rate!=base.rate || reference.phase_lower!=base.phase_lower ||
               reference.anchor.distance(base.anchor)!=0 || range->index!=common.range->index ||
               range->first!=common.range->first || range->end!=common.range->end)return false;
        }
        // All final target outputs are precharged by parallel preparation. No
        // worker may allocate/grow them, including the failure/direct fallback.
        if(range->screen.capacity()<starts.size() || range->reference.capacity()<starts.size())return false;
        geometry.push_back(item);
    }
    const auto span=starts.back()+1;const auto shape=detail::scheduled_parallel_preparation::shape(span);
    const auto fragment_samples=shape.maximum_fragment;const auto input_samples=tile::add_size(span,fragment_samples-1);
    auto& templates=[&]() -> auto& {if constexpr(std::is_same_v<T,float>)return execution.fp32_templates;else return execution.fp64_templates;}();
    const auto& reference=geometry.front().reference;const auto index=geometry.front().range->index;
    if(!templates.empty()) {
        const auto& previous=templates.front()->key;
        if(previous.origin!=reference.origin || previous.rate!=reference.rate || previous.phase!=reference.phase_lower || previous.index!=index)
            execution.drop_templates();
    }
    const auto& plan=lazy_plan<T>(shape,stop);
    struct Pin {LazyState::Execution& execution;explicit Pin(LazyState::Execution& e):execution(e){require(!e.screen_active,"nested implicit family screen");e.screen_active=true;}
        ~Pin(){execution.screen_active=false;}} pin{execution};
    const auto shift=static_cast<std::uint64_t>(std::ceil(std::max(0.L,-reference.origin))+fragment_samples);
    const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
    const auto begin=lazy_state()->catalog->boundary(reference,static_cast<long double>(index)*pattern.symbol_samples(),false,cursor);
    const auto central_end=lazy_state()->catalog->boundary(reference,static_cast<long double>(index+1)*pattern.symbol_samples(),false,cursor);
    if(static_cast<long double>(lazy_history_first())>std::max(0.L,std::ceil(begin)))return false;
    const auto first_v=static_cast<std::uint64_t>(std::max(0.L,std::floor(begin+shift)-1));
    const auto until=static_cast<std::uint64_t>(std::max(0.L,std::ceil(central_end+shift)+2));
    const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
    const auto qmax=cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),detail::scheduled_cold_colored::smooth_gain_upper());
    static const auto pulse_slope=[] {double value=0;for(unsigned i=0;i<2048;++i)value=std::max(value,
        std::abs(pattern_pulse((i+1)/256.)-pattern_pulse(i/256.))*256);return std::nextafter(value*1.000000000001,std::numeric_limits<double>::infinity());}();
    const auto gate_slope=cert::divide(1.875,std::max(1.,std::ceil(config.sample_rate*config.scheduled_hopping->ramp_seconds)));
    const auto jump=cert::mul(cert::up(4*std::abs(pattern_pulse(8.))),detail::scheduled_cold_colored::smooth_gain_upper());
    lazy_capture_summary();const auto maximum=lazy_state()->summary_maximum,union_energy=lazy_state()->summary_union_energy;
    pattern.set_stream_phase_samples(reference.phase_lower);
    require(index<=std::numeric_limits<std::uint64_t>::max()/pattern.chips_per_symbol(),"implicit family template chip overflow");
    const auto chip_first=index*pattern.chips_per_symbol();pattern.prepare_symbol(chip_first,stop);
    detail::scheduled_source_samples::Reader source_samples(config,pattern,chip_first,reference.rate);
    const auto metadata_bytes=[&] {return tile::add_size(tile::helper_reservation,tile::add_size(
        tile::multiply_size(geometry.capacity(),sizeof(Geometry)),tile::multiply_size(starts.capacity(),sizeof(std::size_t))));};
    using Piece=typename LazyState::template TemplatePiece<T>;
    const auto piece_bound=tile::add_size(sizeof(Piece),tile::add_size(tile::multiply_size(fragment_samples,
        sizeof(std::array<Complex,2>)+sizeof(long double)+sizeof(double)+sizeof(std::uint64_t)+3),
        tile::add_size(sizeof(tile::PreparedFragment<T>)+tile::helper_reservation,
            tile::add_size(tile::multiply_size(6*plan.size,sizeof(std::complex<T>)),tile::multiply_size((fragment_samples+1)/2,sizeof(tile::Run))))));
    const auto run_bound=tile::add_size((input_samples+511)/512,4);
    const auto array_bound=tile::add_size(tile::multiply_size(input_samples,sizeof(float)+2*sizeof(Complex)+1),
        tile::multiply_size(run_bound,sizeof(runtime::OriginalRun)+sizeof(recurrence::Segment)));
    runtime::Work cost;cost.maximum_fragment=fragment_samples;
    std::size_t next=0;
    while(next<geometry.size()) {
        cancelled(stop);
        std::size_t count=std::min<std::size_t>(post::maximum_targets,geometry.size()-next);
        std::size_t coarse=next+count/2;
        const auto compatible_width=[&] {
            for(std::size_t t=next;t<next+count;++t)
                if(fusion::taylor_tail(input_samples,config.sample_rate,banks[geometry[t].reference.frequency].frequency-
                    banks[geometry[coarse].reference.frequency].frequency,3).angle>.125)return false;
            return true;
        };
        while(count>1 && !compatible_width()){--count;coarse=next+count/2;}
        if(!compatible_width())return false;
        const auto chain_bound=[&] {return runtime::moment_bytes(plan,3);};
        const auto available=[&] {const auto used=tile::add_size(execution.numeric_bytes(),old_scratch);
            return used<execution.available_bytes?execution.available_bytes-used:0;};
        const auto base_bound=[&] {return tile::add_size(metadata_bytes(),tile::add_size(array_bound,
            tile::add_size(piece_bound,tile::multiply_size(count,sizeof(post::Binding)+sizeof(std::size_t)))));};
        while(!compatible_width() || tile::add_size(tile::add_size(base_bound(),chain_bound()),
            post::Streaming<T>::reservation(plan,starts.size(),count))>available()) {
            if(count==1)return false;--count;coarse=next+count/2;
        }
        std::unique_ptr<post::Streaming<T>> streaming;
        std::vector<post::Binding> bindings;bindings.reserve(count);
        std::vector<std::size_t> residual_sources(count);
        for(std::size_t f=0;f<count;++f){residual_sources[f]=f;
            for(std::size_t previous=0;previous<f;++previous)
                if(geometry[next+f].displacement==geometry[next+previous].displacement){residual_sources[f]=previous;break;}}
        std::vector<float> pcm(input_samples);std::vector<Complex> oscillator(input_samples);
        std::vector<std::uint8_t> observed(input_samples);
        std::vector<runtime::OriginalRun> runs;runs.reserve(run_bound);
        std::vector<recurrence::Segment> segments;segments.reserve(run_bound);
        std::unique_ptr<Piece> temporary;
        std::unique_ptr<fusion::MomentInput<T>> moment;
        const auto live=[&] {
            auto bytes=metadata_bytes();bytes=tile::add_size(bytes,tile::multiply_size(bindings.capacity(),sizeof(bindings[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(residual_sources.capacity(),sizeof(residual_sources[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(pcm.capacity(),sizeof(float)));
            bytes=tile::add_size(bytes,tile::multiply_size(oscillator.capacity(),sizeof(Complex)));
            bytes=tile::add_size(bytes,observed.capacity());bytes=tile::add_size(bytes,tile::multiply_size(runs.capacity(),sizeof(runs[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(segments.capacity(),sizeof(segments[0])));
            if(streaming)bytes=tile::add_size(bytes,streaming->working_bytes());
            if(temporary)bytes=tile::add_size(bytes,temporary->working_bytes());if(moment)bytes=tile::add_size(bytes,moment->working_bytes());
            return bytes;
        };
        const auto remaining=[&] {check(live());const auto used=tile::add_size(execution.numeric_bytes(),execution.scratch_reservation);
            return execution.available_bytes-used;};
        check(live());
        const auto reserve=tile::add_size(piece_bound,chain_bound());const auto room=remaining();
        if(room<=reserve)return false;
        streaming=std::make_unique<post::Streaming<T>>(plan,0,starts,count,room-reserve,
            post::Scope{1,false,false,static_cast<double>(pattern.symbol_samples())/config.sample_rate},stop);check(live());
        runtime::add_count(cost.groups,1);runtime::add_count(cost.moment_groups,1);
        std::uint64_t folded_inverses=0;
        const auto fold_post_inverses=[&] {
            const auto inverses=streaming->work().inverses;
            require(inverses>=folded_inverses,"post-inverse work counter moved backwards");
            runtime::add_count(execution.inverse_transforms,inverses-folded_inverses);folded_inverses=inverses;
        };
        std::uint64_t groups=0,last_group=std::numeric_limits<std::uint64_t>::max(),fragments=0;
        for(std::size_t t=next;t<next+count;++t){geometry[t].residual_square=geometry[t].residual_l1=geometry[t].residual_gram=0;}
        auto v=first_v;
        while(v<until) {
            cancelled(stop);require(++fragments<=tile::maximum_fragments,"implicit family fragment bound exceeded");
            const auto real=static_cast<long double>(v)-shift;const auto tick=phase.add_signed((real-reference.origin)*reference.rate);
            const auto location=execution.map.locate(tick);
            if(!location.active){const auto next_tick=execution.map.source_to_physical(location.source,false);
                const auto target=static_cast<std::uint64_t>(std::max(0.L,std::floor(reference.origin+next_tick.distance(phase)/reference.rate+shift)-1));
                if(target>v+1){v=std::min(target,until);continue;}}
            const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(fragment_samples,until-v));
            const typename LazyState::TemplateKey key{&plan,reference.origin,reference.rate,reference.phase_lower,index,v,shift,length};
            Piece* piece=nullptr;for(const auto& item:templates)if(item->key==key){piece=item.get();break;}
            if(piece)++execution.spectral_template_hits;
            else {
                ++execution.spectral_template_misses;check(tile::add_size(live(),piece_bound));
                temporary=std::make_unique<Piece>();temporary->key=key;temporary->rows.resize(fragment_samples);temporary->mask.resize(fragment_samples);
                temporary->source.resize(length);temporary->gain.resize(length);temporary->active.resize(length);temporary->frame.resize(length);check(live());
                piece=temporary.get();rx_cpu::Scope cpu_template(rx_cpu::Stage::template_build);
                for(std::size_t i=0;i<length;++i) {
                    if((i&63U)==0)cancelled(stop);
                    const auto p=static_cast<long double>(v+i)-shift;const auto t=phase.add_signed((p-reference.origin)*reference.rate);
                    const auto l=execution.map.locate(t);const auto source=l.source.distance(reference.anchor)-static_cast<long double>(index)*pattern.symbol_samples();
                    const auto gain=l.active?SchedHopSourceMap::gain(l,t):0.;piece->source[i]=source;piece->gain[i]=gain;piece->active[i]=l.active;piece->frame[i]=l.frame_index;
                    if(gain>0 && source>=0 && source<pattern.symbol_samples()) {
                        const auto q=source_samples.value(source,stop);piece->mask[i]=1;
                        for(unsigned bit=0;bit<2;++bit)piece->rows[i][bit]=gain*pattern_limit_pcm(amplitude*q[bit])/amplitude;
                    }
                }
                piece->prepared=std::make_unique<tile::PreparedFragment<T>>(plan,tile::Fragment{
                    std::span<const std::array<Complex,2>>(piece->rows).first(length),std::span<const std::uint8_t>(piece->mask).first(length),v},remaining(),stop);
                runtime::add_count(execution.template_transforms,piece->prepared->work.template_transforms);check(live());
            }
            require(piece->prepared && piece->prepared->original.physical_offset==v,"implicit family cached fragment identity changed");
            for(std::size_t i=0;i<length;++i) {
                if((i&63U)==0)cancelled(stop);
                const auto source=piece->source[i];const auto gain=piece->gain[i];const bool active=piece->active[i];
                if(active && piece->frame[i]!=last_group){++groups;last_group=piece->frame[i];}
                for(std::size_t t=next;t<next+count;++t) {
                    if(residual_sources[t-next]!=t-next)continue;
                    auto& g=geometry[t];const auto displacement=g.displacement;
                    const auto smooth=cert::mul(static_cast<double>(displacement),cert::add(cert::mul(cert::divide(34*pulse_slope,pattern.chip_samples()),detail::scheduled_cold_colored::smooth_gain_upper()),cert::mul(gate_slope,qmax)));
                    double residual=0;
                    if(gain>0 && source>=0 && source<pattern.symbol_samples()) {
                        residual=smooth;const auto edge=source/pattern.chip_samples()-.5L;
                        if(std::abs(edge-std::round(edge))*pattern.chip_samples()<=displacement+1e-10L)residual=cert::add(residual,jump);
                        const auto tail=pattern.symbol_samples()%pattern.chip_samples();
                        if(tail){const auto center=static_cast<long double>(pattern.symbol_samples()-tail)+.5L*tail;
                            if(std::min(std::abs(source-center-8.L*pattern.chip_samples()),std::abs(source-center+8.L*pattern.chip_samples()))<=displacement+1e-10L)
                                residual=cert::add(residual,jump);}
                        if(source<=displacement || pattern.symbol_samples()-source<=displacement)residual=cert::add(residual,qmax);
                    }else {
                        residual=cert::mul(static_cast<double>(displacement),cert::mul(gate_slope,qmax));
                        if(gain>0 && std::min(std::abs(source),std::abs(source-pattern.symbol_samples()))<=displacement+1e-10L)residual=cert::add(residual,qmax);
                    }
                    g.residual_square=cert::add(g.residual_square,cert::mul(residual,residual));
                    g.residual_l1=cert::add(g.residual_l1,residual);
                    g.residual_gram=cert::add(g.residual_gram,cert::add(cert::mul(2*qmax,residual),cert::mul(residual,residual)));
                }
            }
            rx_cpu::Scope cpu_mix(rx_cpu::Stage::mixing);
            std::fill(pcm.begin(),pcm.end(),0);std::fill(observed.begin(),observed.end(),0);std::fill(oscillator.begin(),oscillator.end(),Complex{});runs.clear();
            const auto input_first=static_cast<long double>(v)-shift;
            const auto captured_first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),std::max(0.L,input_first)));
            const auto captured_end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,input_first+input_samples)));
            const auto coarse_frequency=banks[geometry[coarse].reference.frequency].frequency;
            // Caller pushes may have ANY original fence size <=512. Count the
            // exact metadata before reserving; ceil(N/512) is not an upper
            // bound when the original caller supplied one-sample pushes.
            std::size_t descriptor_count=0,covered=0;
            const auto count_empty_until=[&](std::size_t end) {
                require(end>=covered && end<=input_samples,"implicit family fence metadata overlaps");
                descriptor_count=tile::add_size(descriptor_count,(end-covered+511)/512);covered=end;
            };
            if(captured_first<captured_end)lazy_visit_fences(captured_first,captured_end,[&](const auto& fence) {
                require(fence.count<=recurrence::maximum_reset_segment,"implicit carrier original fence exceeds 512");
                const auto a=static_cast<std::uint64_t>(std::max<long double>(fence.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(tile::add_coordinate(fence.first,fence.count),input_first+input_samples));
                if(a>=b)return;const auto local=static_cast<std::size_t>(static_cast<long double>(a)-input_first);
                count_empty_until(local);descriptor_count=tile::add_size(descriptor_count,1);covered=local+static_cast<std::size_t>(b-a);
            });
            count_empty_until(input_samples);
            require(descriptor_count<=input_samples,"implicit family fence metadata exceeded physical cells");
            const auto reserve_descriptors=[&](auto& values) {
                if(values.capacity()>=descriptor_count)return;
                using Element=typename std::remove_reference_t<decltype(values)>::value_type;
                const auto bytes=tile::multiply_size(descriptor_count,sizeof(Element));check(tile::add_size(live(),bytes));
                // reserve may temporarily retain the old allocation; account
                // the complete replacement as well until the swap releases it.
                std::vector<Element> replacement;replacement.reserve(descriptor_count);
                check(tile::add_size(live(),tile::multiply_size(replacement.capacity(),sizeof(Element))));values.swap(replacement);
            };
            reserve_descriptors(runs);reserve_descriptors(segments);check(live());
            const auto append_run=[&](runtime::OriginalRun run) {
                require(runs.size()<runs.capacity(),"implicit family fence metadata changed during copy");runs.push_back(run);
            };
            const auto empty_until=[&](std::size_t end) {
                auto first=runs.empty()?0:runs.back().end;
                while(first<end){const auto last=std::min(end,first+recurrence::maximum_reset_segment);append_run({first,last,0,0,{},{},false});first=last;}
            };
            if(captured_first<captured_end)lazy_visit_blocks(captured_first,captured_end,[&](const auto& block) {
                require(block.pcm.size()<=recurrence::maximum_reset_segment,"implicit carrier original fence exceeds 512");
                const auto a=static_cast<std::uint64_t>(std::max<long double>(block.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(block.first+block.pcm.size(),input_first+input_samples));if(a>=b)return;
                const auto local=static_cast<std::size_t>(static_cast<long double>(a)-input_first);empty_until(local);
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*coarse_frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*coarse_frequency/config.sample_rate);
                for(auto n=block.first;n<a;++n)o*=step;
                append_run({local,local+static_cast<std::size_t>(b-a),block.first,static_cast<std::size_t>(a-block.first),o,step,true});
                for(auto n=a;n<b;++n){const auto at=static_cast<std::size_t>(static_cast<long double>(n)-input_first);
                    pcm[at]=block.pcm[n-block.first];oscillator[at]=o;observed[at]=1;
                    require(cert::norm(o)<=2,"original carrier magnitude exceeded family contract");o*=step;}
            },false); // one original PCM copy, no redundant energy-prefix rebuild
            empty_until(input_samples);require(runs.size()==descriptor_count,"implicit family fence identity changed during copy");check(live());
            const auto first_observed=std::find_if(runs.begin(),runs.end(),[](const auto& run){return run.observed;});
            const bool any_observed=first_observed!=runs.end();
            const tile::Input coarse_input{pcm,oscillator,observed,v};
            {
                moment=std::make_unique<fusion::MomentInput<T>>(plan,coarse_input,3,remaining(),stop);check(live());
                runtime::add_count(execution.input_transforms,moment->work.shared_forward_transforms);
                runtime::add_count(cost.moment_transforms,moment->work.shared_forward_transforms);
            }
            cpu_mix.finish();
            bindings.clear();
            for(std::size_t t=next;t<next+count;++t) {
                cancelled(stop);const auto frequency=banks[geometry[t].reference.frequency].frequency;
                const auto target_step=std::polar(1.,tau*frequency/config.sample_rate);
                const auto seed=[&](const runtime::OriginalRun& run) {
                    auto value=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(run.block_first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    for(std::size_t i=0;i<run.block_offset;++i)value*=target_step;return value;
                };
                if(moment) {
                    segments.clear();for(const auto& run:runs) {
                        require(segments.size()<segments.capacity(),"implicit family descriptor capacity changed");
                        const auto target_seed=run.observed?seed(run):Complex{};
                        // Certify the same <=2 original-carrier contract for
                        // EVERY target recurrence, not only the coarse input.
                        // This uses its actual seed/step and IEEE operation
                        // bound, without assuming an accuracy bound for polar.
                        if(run.observed && recurrence::oscillator_magnitudes(target_seed,target_step,
                            run.end-run.begin).maximum>2)return false;
                        segments.push_back({run.begin,run.end,run.coarse_seed,target_seed,run.coarse_step,
                            run.observed?target_step:Complex{},run.block_first,run.block_offset});
                    }check(live());
                    const auto delta=frequency-coarse_frequency;
                    auto coefficients=any_observed?recurrence::anchored_coefficients(*moment,delta,config.sample_rate,0,
                        first_observed->begin,first_observed->coarse_seed,seed(*first_observed)):
                        fusion::coefficients(*moment,delta,config.sample_rate,0);
                    const tile::Input target{pcm,t==coarse?std::span<const Complex>(oscillator):std::span<const Complex>{},observed,v};
                    const auto proof=recurrence::certify(*moment,coefficients,target,segments,{true},stop);
                    if(!proof.remainder.valid || !runtime::capture_origin(proof.capture_origin,v,shift,any_observed))return false;
                    require(bindings.size()<bindings.capacity(),"post-inverse binding reservation changed");
                    bindings.push_back({target,coefficients,proof.remainder});
                    runtime::add_count(cost.recurrence_segments,proof.work.segments);
                }
            }
            try {streaming->add(*moment,*piece->prepared,bindings,stop);}
            catch(...){fold_post_inverses();throw;}
            fold_post_inverses();
            moment.reset();check(live());runtime::add_count(cost.fragments,1);
            if(temporary && templates.size()<tile::maximum_fragments) {
                // No extra private spectrum or backing copy. Publication moves
                // the same immutable owner after all borrowers have finished.
                const auto next_capacity=templates.size()==templates.capacity()?std::max<std::size_t>(8,tile::multiply_size(templates.capacity(),2)):templates.capacity();
                // reserve holds the complete OLD pointer allocation until the
                // complete NEW pointer allocation has been constructed.
                const auto pointer_allocation=next_capacity!=templates.capacity()?tile::multiply_size(next_capacity,sizeof(templates[0])):0;
                if(pointer_allocation<=remaining()) {
                    try {if(pointer_allocation){check(tile::add_size(live(),pointer_allocation));templates.reserve(next_capacity);check(live());}
                        templates.push_back(std::move(temporary));}
                    catch(const std::bad_alloc&){} // optional cache, all required state remains owned
                    check(live());
                }
            }
            temporary.reset();check(live());v=tile::add_coordinate(v,length);
        }
        for(std::size_t f=0;f<count;++f){
            const auto& representative=geometry[next+residual_sources[f]];auto& g=geometry[next+f];
            g.residual_square=representative.residual_square;g.residual_l1=representative.residual_l1;g.residual_gram=representative.residual_gram;
            g.range->screen.resize(starts.size());
        }
        streaming->finish([&](std::size_t f,std::size_t j,const tile::Value& value) {
            geometry[next+f].range->screen[j]=value;
        },stop);
        fold_post_inverses();
        audio::trace::record("scheduled_postinverse_family",[&](auto& f) {
            const auto& w=streaming->work();
            f.add("targets",w.targets);f.add("timing_starts",w.starts);f.add("fragments",w.fragments);
            f.add("transform",w.transform);f.add("native_spectral_products",w.spectral_products);
            f.add("wide_contractions",w.wide_products);f.add("inverse_transforms",w.inverses);
            f.add("energy_refinements",w.energy_refinements);f.add("working_bytes",w.working_bytes);
            f.add("bulk_precision",std::is_same_v<T,float>?"FP32":"FP64");
            f.add("final_contraction","FP64 compensated; original guards/refinement retained");
            f.add("prepared_chunk_only",true);
        });
        for(std::size_t t=next;t<next+count;++t) {
            auto& g=geometry[t];auto& range=*g.range;range.reference.resize(starts.size());
            range.screen_first=range.first;range.mask_difference=4*groups+8;range.maximum=maximum;
            range.proxy_residual_square=g.residual_square;range.proxy_residual_l1=g.residual_l1;range.proxy_residual_gram=g.residual_gram;
            for(std::size_t j=0;j<starts.size();++j) {
                const auto n=range.screen[j].fits[0].count+range.mask_difference;
                if(n>(std::numeric_limits<std::size_t>::max()-128*block_samples-128)/128)return false;
                const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                    cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
                const auto dot=cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),union_energy)));
                const auto gram=cert::mul(32*rho*n,qmax*qmax);
                range.reference[j]={dot,gram,cert::mul(4*rho,union_energy),n>2?1e-8/(.5*static_cast<double>(n-2)):0.,true};
            }
            if constexpr(std::is_same_v<T,float>)++execution.fp32_ranges;else ++execution.fp64_ranges;
            if(g.displacement>1e-9L)++execution.proxy_ranges;
        }
        streaming.reset();bindings.clear();check(live());next+=count;
    }
    cancelled(stop);runtime::publish(execution,cost);reset.complete=true;return true;
}

template<class T> bool lazy_screen_family_optional_post(std::span<LazyState::DueRange* const> ranges,std::stop_token stop) {
    const auto* requested=std::getenv("DATAPUMP_SCHED_POSTINVERSE");
    if(requested && std::string_view(requested)=="1" && config.search_arithmetic!=SearchArithmetic::fp64) {
        try {if(lazy_screen_postinverse_family<T>(ranges,stop)){
            audio::trace::record("scheduled_postinverse_ready",[&](auto& f){f.add("ranges",ranges.size());});return true;}}
        catch(const std::bad_alloc&) {cancelled(stop);}
        catch(const Error&) {cancelled(stop);}
    }
    if(requested && std::string_view(requested)=="1")audio::trace::record("scheduled_postinverse_fallback",[&](auto& f){
            f.add("ranges",ranges.size());f.add("original_family_retained",true);
            f.add("partial_setup_or_transform_possible",true);
        });
    return lazy_screen_family<T>(ranges,stop);
}
