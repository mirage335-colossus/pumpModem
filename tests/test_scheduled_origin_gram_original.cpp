// Actual scalar/family origin-Gram execution, cropped capture and Original oracle.
// Root integrates/builds. No copy of the fractional contraction implementation.
#include "../src/pattern_correlator.cpp"
#include <bit>
#include <iostream>
#include <stop_token>
namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;using Range=State::LazyState::DueRange;
    static void check(bool b,const char* m){if(!b)throw Error(m);}
    static void mode(const char* k,const char* v){
#if defined(_WIN32)
        check(_putenv_s(k,v?v:"")==0,"cannot set basis test mode");
#else
        check((v?setenv(k,v,1):unsetenv(k))==0,"cannot set basis test mode");
#endif
    }
    static Config config(bool outer,unsigned multiplier,bool fhss,SearchArithmetic arithmetic) {
        Config c;c.sample_rate=outer?6120:512;c.carrier_hz=outer?1500:128;
        c.bandwidth_hz=outer?36:128;c.integration_seconds=outer?std::nextafter(8705./6120,0.):2.;
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.search_arithmetic=arithmetic;
        if(outer){c.dsss_factor=10;c.dsss=true;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;}
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        if(fhss){auto h=std::make_shared<FhssPlan>();h->channels=2;h->shifts[0]=30000000;h->shifts[1]=31000000;
            h->dwell_seconds=.4;h->power_ramps=h->mandatory_ramps=h->smooth_ramps=true;
            h->clock_error_seconds=h->peer_clock_error_seconds=h->audio_error_seconds=h->region_seconds=0;
            h->ramp_seconds=.006;c.fake_hopping=h;}
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto midpoint=first.sample+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;
        return c;
    }
    static PatternSearch search(const Config& c,double ppm) {
        PatternSearch q;q.start_offset_seconds=136.375/c.sample_rate;q.start_uncertainty_seconds=136./c.sample_rate;
        q.search_stream_phases=false;q.prefer_streamed_templates=true;q.worker_threads=1;q.drift_tolerant=false;
        // Paired hypotheses are essential: the marginal-vector legacy grid is
        // floored to an integer step and would not prove fractional execution.
        q.hypotheses={{0,ppm}};q.bit_limit=4;q.chunk_bits=1;q.candidate_limit=256;q.track_limit=32;q.retain_score=0;
        return q;
    }
    static void original_capture(State& state,std::uint64_t first,std::uint64_t end) {
        namespace journal=detail::scheduled_processing;
        check(first<end,"empty conditional original capture");
        state.lazy_state()->capture.clear();state.lazy_state()->capture_allocated_bytes=0;state.lazy_state()->history_samples=0;
        state.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{64,64,64,state.budget},first);
        state.lazy_state()->processing->append_push(first,end-first);
        for(auto at=first;at<end;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(512,end-at));
            State::LazyState::Block block;block.first=at;block.pcm.resize(count);block.energy.resize(count+1);
            for(std::size_t n=0;n<count;++n){const auto absolute=static_cast<double>(at+n);
                const auto x=static_cast<float>(.31*std::sin(.031*absolute)+.13*std::cos(.271*absolute));
                block.pcm[n]=x;block.energy[n+1]=block.energy[n]+static_cast<double>(x)*x;
                block.maximum=std::max(block.maximum,std::abs(static_cast<double>(x)));}
            state.lazy_state()->capture_allocated_bytes+=sizeof(block)+128+block.pcm.capacity()*sizeof(float)+block.energy.capacity()*sizeof(double);
            state.lazy_state()->capture.push_back(std::move(block));state.lazy_state()->history_samples+=count;at+=count;
        }
        while(state.lazy_state()->processing->pending()) {
            const auto block=state.lazy_state()->processing->prepare([](std::size_t maximum){return maximum;},
                [&](std::uint64_t position){const auto at=first+(position-first)/512*512;
                    return journal::Fence{at,std::min<std::uint64_t>(512,end-at),position,
                        std::min<std::uint64_t>(512,end-at)-(position-at)};});
            check(bool(block),"conditional original fence was not recorded");state.lazy_state()->processing->advance(*block);
        }
        state.lazy_state()->capture_end=end;state.lazy_state()->block_end=end;state.sample=end;
        state.accounted_bytes=state.working_bytes();
        check(sizeof(PatternCorrelator)+state.accounted_bytes<=state.budget,"conditional fixture exceeded workspace");
    }

    static std::uint64_t pcm_identity(const State& s){std::uint64_t h=1469598103934665603ULL;
        for(const auto& b:s.lazy_state()->capture)for(float x:b.pcm){h^=std::bit_cast<std::uint32_t>(x);h*=1099511628211ULL;}return h;}
    static auto detector(const State& s){return std::tuple{s.trials,s.sample,s.lazy_state()->processed,
        s.lazy_state()->history_generation,s.history.size(),s.emissions.size(),s.bursts.size()};}
    static auto transforms(const State& s){const auto& x=*s.lazy_state();const auto& e=*x.serial_execution;
        return std::tuple{x.input_transforms,x.template_transforms,x.inverse_transforms,
            e.input_transforms,e.template_transforms,e.inverse_transforms};}
    struct MaskRows {std::uint64_t original=0,proxy=0,difference=0,boundaries=0;long double original_energy=0,proxy_energy=0;};
    static MaskRows mask_rows(State& s,const Range& r,std::size_t j) {
        const auto reference=s.lazy_family_lane(r.family,r.first),lane=s.lazy_family_lane(r.family,r.first+j);
        const auto k=std::round(lane.origin-reference.origin);
        const auto phase=SchedHopCoordinate{s.scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        const auto source_first=static_cast<long double>(r.index)*s.code.symbol_samples();MaskRows result;
        const auto membership=[&](const auto& selected,long double p,bool* boundary) {
            const auto tick=phase.add_signed((p-selected.origin)*selected.rate);const auto loc=s.scheduled_map()->locate(tick);
            const auto source=loc.source.distance(selected.anchor)-source_first;
            const auto gain=s.lazy_screen_gain(loc,tick);
            const bool usable=loc.active && gain>0 && source>=0 && source<s.code.symbol_samples();
            if(boundary && loc.active){const auto edge=2*selected.rate;
                *boundary=std::abs(source)<=edge || std::abs(source-s.code.symbol_samples())<=edge ||
                    std::abs(tick.distance(SchedHopCoordinate{loc.flat_begin,0}))<=edge ||
                    std::abs(tick.distance(SchedHopCoordinate{loc.flat_end,0}))<=edge;}
            return usable;
        };
        for(const auto& block:s.lazy_state()->capture)for(std::size_t i=0;i<block.pcm.size();++i) {
            const auto p=static_cast<long double>(block.first+i);bool boundary=false;
            const bool original=membership(lane,p,&boundary),proxy=membership(reference,p-k,nullptr);
            const long double energy=static_cast<long double>(block.pcm[i])*block.pcm[i];
            if(original){++result.original;result.original_energy+=energy;}
            if(proxy){++result.proxy;result.proxy_energy+=energy;}
            result.difference+=original!=proxy;result.boundaries+=boundary && (original || proxy);
        }return result;
    }
    static void field(double actual,double proxy,double radius,const char* name) {
        namespace cert=detail::scheduled_acquisition_certificate;
        const auto interval=cert::Interval::disk(proxy,radius);
        if(!interval.bounded() || actual<interval.lo || actual>interval.hi){
            std::cerr<<"field="<<name<<" actual="<<actual<<" proxy="<<proxy<<" radius="<<radius<<'\n';
            throw Error("Original whole Fit escaped fractional-basis certificate");}
    }

    struct Stats {std::uint64_t boundaries=0,changes=0,finite_scores=0,rounding=0;double dot=0,gram=0;};
    static Stats verify(State& s,const Config& c,Range& r,std::uint64_t identity) {
        namespace cert=detail::scheduled_acquisition_certificate;
        namespace proxy=detail::scheduled_template_proxy_certificate;
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(c);
        const auto qmax=cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),detail::scheduled_cold_colored::smooth_gain_upper());
        std::uint64_t boundary_rows=0,mask_changes=0,finite_score_uppers=0,maximum_original_owner_rounding=0;double maximum_dot_radius=0,maximum_gram_radius=0;
        for(std::size_t j=0;j<r.screen.size();++j){const auto lane=s.lazy_family_lane(r.family,r.first+j);
            const auto rows=mask_rows(s,r,j);boundary_rows+=rows.boundaries;mask_changes+=rows.difference;
            const auto& value=r.screen[j];check(value.certificate.valid && r.reference[j].valid,"basis has invalid whole/reference certificate");
            check(rows.proxy==value.fits[0].count && rows.proxy==value.fits[1].count,
                "basis count differs from exact central-mask row ownership");
            check(rows.difference<=r.mask_difference,"actual original/proxy row ownership escaped mask bound");
            check(rows.proxy_energy>=value.certificate.energy_lower && rows.proxy_energy<=value.certificate.energy_upper,
                "retained PCM central-mask energy escaped FFT certificate");
            const auto slot=s.lazy_ensure_slot(lane.id,r.index);
            const auto due=s.lazy_state()->catalog->due(lane,r.index,*s.lazy_state()->cursor);s.lazy_replay_to(slot,std::min(due,s.lazy_state()->capture_end),{});
            const auto carrier=s.lazy_carrier_restart(lane.frequency);
            check(std::isfinite(carrier) && s.lazy_state()->carrier_restart_magnitude[lane.frequency]<=2,
                "Original replay carrier reference unavailable");
            for(unsigned bit=0;bit<2;++bit){const auto& fit=s.hypotheses[slot].fits[bit];const auto& estimate=value.fits[bit];
                // Original piece association has its own producer-rounding
                // allowance. Never demand ideal-mask/Original count equality.
                const auto owner_rounding=fit.count>rows.original?fit.count-rows.original:rows.original-fit.count;
                maximum_original_owner_rounding=std::max(maximum_original_owner_rounding,owner_rounding);
                check(owner_rounding<=r.mask_difference,"Original row association escaped the stated boundary allowance");
                const auto count_difference=fit.count>estimate.count?fit.count-estimate.count:estimate.count-fit.count;
                check(count_difference<=r.mask_difference,"Original count escaped central-mask boundary allowance");
                auto original=r.reference[j];
                const auto p=proxy::errors(estimate,value.certificate,bit,r.proxy_residual_square,2,r.mask_difference,r.maximum,
                    r.proxy_residual_l1,r.proxy_residual_gram);check(p.valid,"basis proxy residual was unusable");
                const auto n=static_cast<double>(estimate.count+r.mask_difference);
                original.dot=cert::add(original.dot,p.dot);original.gram=cert::add(original.gram,p.gram);
                original.dot=cert::add(original.dot,cert::mul(carrier*qmax,cert::root(cert::mul(n,s.lazy_state()->summary_union_energy))));
                original.gram=cert::add(original.gram,cert::mul(n*qmax*qmax,cert::add(
                    cert::mul(2*s.lazy_state()->carrier_restart_magnitude[lane.frequency],carrier),cert::mul(carrier,carrier))));
                const auto dr=cert::add(value.certificate.dot_radius[bit],original.dot),gr=cert::add(value.certificate.gram_radius[bit],original.gram);
                maximum_dot_radius=std::max(maximum_dot_radius,dr);maximum_gram_radius=std::max(maximum_gram_radius,gr);
                field(fit.xc,estimate.xc,dr,"xc");field(fit.xs,estimate.xs,dr,"xs");
                field(fit.cc,estimate.cc,gr,"cc");field(fit.ss,estimate.ss,gr,"ss");field(fit.cs,estimate.cs,gr,"cs");
                const auto debit=cert::mul(static_cast<double>(r.mask_difference),cert::mul(r.maximum,r.maximum));
                const auto lo=std::max(0.,cert::down(cert::down(value.certificate.energy_lower-debit)-original.energy));
                const auto hi=cert::add(cert::add(value.certificate.energy_upper,debit),original.energy);
                check(fit.energy>=lo && fit.energy<=hi && rows.original_energy>=lo && rows.original_energy<=hi,
                    "Original retained PCM energy escaped mask/reference allowance");
                auto adjusted=estimate;adjusted.count+=r.mask_difference;auto certificate=value.certificate;
                certificate.energy_lower=std::max(0.,cert::down(certificate.energy_lower-debit));
                const auto upper=cert::score_upper(adjusted,certificate,bit,original);
                finite_score_uppers+=std::isfinite(upper);
                check(!std::isnan(upper) && fit.score()<=upper,"Original coherent score escaped actual basis/reference upper");
            }
        }
        check(boundary_rows>0 && pcm_identity(s)==identity,"oracle omitted boundary rows or modified original PCM");
        return {boundary_rows,mask_changes,finite_score_uppers,maximum_original_owner_rounding,maximum_dot_radius,maximum_gram_radius};
    }
    static bool selected(State& s,const Range& r,long double p) {
        const auto q=s.lazy_family_lane(r.family,r.first);
        const auto phase=SchedHopCoordinate{s.scheduled->epoch_sample,0}.add_samples(q.phase_lower);
        const auto t=phase.add_signed((p-q.origin)*q.rate);const auto loc=s.scheduled_map()->locate(t);
        const auto source=loc.source.distance(q.anchor)-static_cast<long double>(r.index)*s.code.symbol_samples();
        return loc.active && s.lazy_screen_gain(loc,t)>0 && source>=0 && source<s.code.symbol_samples();
    }
    static std::uint64_t cropped_end(State& s,const Range& r,std::uint64_t full) {
        const auto q=s.lazy_family_lane(r.family,r.first),last=s.lazy_family_lane(r.family,r.end-1);
        const auto width=static_cast<std::uint64_t>(std::round(last.origin-q.origin))+3;
        const auto middle=static_cast<std::uint64_t>(std::ceil(s.lazy_state()->catalog->boundary(q,
            static_cast<long double>(r.index)*s.code.symbol_samples()+s.code.symbol_samples()/2.L,false,*s.lazy_state()->cursor)));
        // Deliberately end inside an active interval, with every shifted lane
        // losing a different number of selected rows. A full-due capture does
        // not exercise per-origin external-Gram cropping.
        for(auto end=std::max(width,middle);end+2<full;++end) {
            bool flat=true;for(auto p=end-width;p<=end+1;++p)if(!selected(s,r,p)){flat=false;break;}
            if(flat)return end;
        }
        throw Error("cannot construct a bounded partial active capture");
    }
    struct Delta {std::uint64_t input=0,templates=0,inverse=0,fp32=0,groups=0,fragments=0,moments=0;};
    static Delta counters(const State& s){const auto& e=*s.lazy_state()->serial_execution;
        return {e.input_transforms,e.template_transforms,e.inverse_transforms,e.fp32_ranges,
            e.frequency_family_work.groups,e.frequency_family_work.fragments,e.frequency_family_work.moment_groups};}
    static Delta subtract(Delta a,Delta b){return {a.input-b.input,a.templates-b.templates,a.inverse-b.inverse,
        a.fp32-b.fp32,a.groups-b.groups,a.fragments-b.fragments,a.moments-b.moments};}
    static void proxy_agreement(const Range& a,const Range& b) {
        namespace cert=detail::scheduled_acquisition_certificate;
        check(a.screen.size()==b.screen.size(),"external-Gram changed timing outputs");
        for(std::size_t j=0;j<a.screen.size();++j)for(unsigned bit=0;bit<2;++bit) {
            const auto& x=a.screen[j];const auto& y=b.screen[j];
            check(x.certificate.valid && y.certificate.valid && x.fits[bit].count==y.fits[bit].count &&
                x.fits[bit].energy==y.fits[bit].energy,"external-Gram changed the original captured observation domain");
            // Same mathematical A/B/D proxy on identical original PCM: this
            // tight check excludes the much wider target-vs-proxy allowance.
            const auto dr=cert::add(x.certificate.dot_radius[bit],y.certificate.dot_radius[bit]);
            const auto gr=cert::add(x.certificate.gram_radius[bit],y.certificate.gram_radius[bit]);
            field(y.fits[bit].xc,x.fits[bit].xc,dr,"proxy-xc");field(y.fits[bit].xs,x.fits[bit].xs,dr,"proxy-xs");
            field(y.fits[bit].cc,x.fits[bit].cc,gr,"proxy-cc");field(y.fits[bit].ss,x.fits[bit].ss,gr,"proxy-ss");
            field(y.fits[bit].cs,x.fits[bit].cs,gr,"proxy-cs");
        }
    }
    static void fixture(bool outer,unsigned multiplier,bool fhss,double ppm,bool family,bool crop,std::size_t family_targets=2) {
        auto c=config(outer,multiplier,fhss,SearchArithmetic::default_mode);c.carrier_hz+=.125;
        auto q=search(c,ppm);q.hypotheses.clear();
        const std::size_t targets=family?family_targets:1;
        const std::array<double,4> frequencies{-.017,.023,-.005,.011};
        check(targets<=frequencies.size(),"invalid bounded frequency family count");
        for(std::size_t i=0;i<targets;++i)q.hypotheses.push_back({frequencies[i],ppm});
        PatternCorrelator rx(c,q,128*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state() && s.shaped,"origin-Gram oracle lacks actual shaped implicit backend");
        check(s.code.chip_samples()==(outer?34:8),"origin-Gram oracle changed C8/C34 geometry");
        check(!outer || s.code.symbol_samples()%s.code.chip_samples()!=0,"DSSS origin-Gram fixture lost partial final fine chip");
        check(s.lazy_state()->catalog->families().size()==targets,"frequency fixture changed its exact family count");
        auto& ranges=s.lazy_state()->due_ranges;ranges.resize(targets);std::vector<Range*> cohort;cohort.reserve(targets);
        std::uint64_t full=0;
        for(std::size_t f=0;f<targets;++f){auto& r=ranges[f];r.family=f;
            const auto& catalog_family=s.lazy_state()->catalog->families()[f];r.first=catalog_family.origin_first+1;r.end=r.first+7;
            check(r.end<catalog_family.origin_end,"origin-Gram fixture lacks regular admitted timing outputs");
            const auto first=s.lazy_family_lane(f,r.first);r.index=first.index;r.id=first.id;
            check(first.rate!=1 && first.origin!=std::round(first.origin),"origin-Gram fixture lost nonunit/fractional identity");
            check(s.banks[first.frequency].frequency!=c.carrier_hz,"frequency target collapsed onto the coarse carrier");
            bool fractional=false;for(auto i=r.first;i<r.end;++i){const auto lane=s.lazy_family_lane(f,i);
                check(lane.index==r.index,"origin-Gram range crosses current-symbol ownership");
                const auto delta=lane.origin-first.origin;fractional|=delta!=std::round(delta);
                full=std::max(full,s.lazy_state()->catalog->due(lane,r.index,*s.lazy_state()->cursor));}
            check(fractional && s.lazy_fractional_basis_needed(r),"origin-Gram fixture bypassed fractional dispatch");
            r.screen.reserve(7);r.reference.reserve(7);cohort.push_back(&r);
        }
        const auto end=crop?cropped_end(s,ranges.front(),full):full;
        check(end>1024 && (!crop || end<full),"fixture lacks multiple actual raw seed/fence crossings or partial capture");
        original_capture(s,0,end);s.lazy_state()->screen_capture_end=end;s.lazy_capture_summary();
        auto& execution=*s.lazy_state()->serial_execution;
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        execution.available_bytes=s.budget-sizeof(PatternCorrelator)-s.accounted_bytes+execution.numeric_bytes();
        const auto identity=pcm_identity(s);const auto before=detector(s);
        const auto run=[&]{return family?s.lazy_screen_fractional_family<float>(cohort,{}):s.lazy_screen_fractional_basis<float>(ranges.front(),{});};
        mode("DATAPUMP_SCHED_ORIGIN_GRAM","0");const auto cost0=counters(s);check(run(),"complete-plane scalar/family basis failed");
        const auto full_cost=subtract(counters(s),cost0);const auto full_ranges=ranges;
        mode("DATAPUMP_SCHED_ORIGIN_GRAM","1");const auto cost1=counters(s);const bool ready=run();
        if(!ready)std::cerr<<"origin-Gram stage="<<execution.last_screen_stage<<" line="<<execution.last_screen_line<<'\n';
        check(ready,"actual origin-Gram scalar/family returned fallback");const auto gram_cost=subtract(counters(s),cost1);
        check(gram_cost.fp32==targets && gram_cost.inverse==6*targets && gram_cost.inverse<full_cost.inverse &&
            gram_cost.templates>0 && full_cost.templates==3*gram_cost.templates,
            "actual dispatch did not execute the reduced external-Gram planes");
        if(!family)check(full_cost.inverse==18 && full_cost.input==3*gram_cost.input,"scalar full/reduced-plane counters differ");
        else {check(gram_cost.groups==1 && full_cost.groups==1 && gram_cost.fragments>0,
                "scalar preparation was mistaken for actual family execution");
            if(targets>=4)check(gram_cost.moments>0 && full_cost.moments>0,
                "four-target fixture bypassed integrated moment/frequency-family execution");}
        check(detector(s)==before && pcm_identity(s)==identity && !execution.screen_active && execution.scratch_reservation==0,
            "origin-Gram preparation altered authoritative detector/PCM or leaked scratch");
        std::uint64_t min_count=std::numeric_limits<std::uint64_t>::max(),max_count=0;
        for(std::size_t f=0;f<targets;++f){auto& r=ranges[f];
            check(r.independent_whole_template && !r.exact_observation_mask && r.screen.size()==7,"origin-Gram lost its independent whole proof");
            proxy_agreement(full_ranges[f],r);
            for(const auto& value:r.screen){min_count=std::min(min_count,value.fits[0].count);max_count=std::max(max_count,value.fits[0].count);}
            const auto stats=verify(s,c,r,identity);
            std::cout<<"origin-Gram family="<<family<<" target="<<f<<" C="<<s.code.chip_samples()<<" M="<<multiplier
                <<" crop="<<crop<<" moments="<<gram_cost.moments<<" boundary_rows="<<stats.boundaries<<" finite_score_uppers="<<stats.finite_scores<<'\n';
        }
        check(!crop || max_count>min_count,"partial capture did not exercise distinct per-origin observation crops");
        const auto before_cancel=detector(s);std::stop_source stopped;stopped.request_stop();bool cancelled=false;
        try{if(family)(void)s.lazy_screen_fractional_family<float>(cohort,stopped.get_token());
            else (void)s.lazy_screen_fractional_basis<float>(ranges.front(),stopped.get_token());}
        catch(const Error&){cancelled=true;}
        check(cancelled && detector(s)==before_cancel && pcm_identity(s)==identity && !execution.screen_active &&
            execution.scratch_reservation==0 && !s.lazy_state()->transient_reservation,"origin-Gram cancellation leaked or altered detector/PCM");
        // Actual unsupported physical extent returns false. It must not turn
        // missing/unsupported evidence into a Gram proof or detector progress.
        const auto saved_end=s.lazy_state()->capture_end;const auto numeric=transforms(s);
        s.lazy_state()->capture_end=static_cast<std::uint64_t>(detail::scheduled_origin_gram::Powers::maximum_exponent)+1;
        bool unsupported=false;try{unsupported=!run();}catch(...){s.lazy_state()->capture_end=saved_end;throw;}
        s.lazy_state()->capture_end=saved_end;
        check(unsupported && transforms(s)==numeric && detector(s)==before_cancel && pcm_identity(s)==identity &&
            !execution.screen_active && execution.scratch_reservation==0,"unsupported origin-Gram extent produced numerical work or detector mutation");
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"origin-Gram fixture exceeded bounded workspace");
        std::cout<<"origin-Gram target_count="<<targets<<" full_inverse="<<full_cost.inverse<<" reduced_inverse="<<gram_cost.inverse
            <<" full_template="<<full_cost.templates<<" reduced_template="<<gram_cost.templates
            <<" cropped_count_span="<<(max_count-min_count)<<'\n';
    }
    static void fp64_bypass() {
        auto c=config(false,3,false,SearchArithmetic::fp64);PatternCorrelator rx(c,search(c,300),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});auto& s=*rx.impl_;
        check(s.lazy_state() && s.shaped,"FP64 origin-Gram fixture lost implicit backend");Range r;r.family=0;
        const auto& f=s.lazy_state()->catalog->families().front();r.first=f.origin_first+1;r.end=r.first+7;
        std::array<Range*,1> cohort{&r};const auto before=detector(s);const auto numeric=transforms(s);
        mode("DATAPUMP_SCHED_ORIGIN_GRAM","1");
        check(!s.lazy_screen_fractional_basis<float>(r,{}) && !s.lazy_screen_fractional_family<float>(cohort,{}) &&
            !r.independent_whole_template && r.screen.empty() && detector(s)==before && transforms(s)==numeric,
            "FP64 force entered origin-Gram scalar/family approximation");
    }
    static void run(){fixture(false,3,false,300,false,false);fixture(false,5,true,-300,true,true,4);
        fixture(true,3,false,300,true,true);fixture(true,5,true,-300,false,true);
        fixture(true,3,false,300,false,false);fp64_bypass();}
};
}
int main(){using A=datapump::modem::PatternCorrelatorTimingTestAccess;try{
    A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    A::mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","1");A::mode("DATAPUMP_SCHED_FINE_CURVES","0");
    A::mode("DATAPUMP_SCHED_FREQUENCY_FAMILY_WIDTH","4");
    if(!A::State::lazy_reference_environment()){std::cout<<"Original environment unavailable; actual origin-Gram coverage omitted\n";return 77;}
    A::run();std::cout<<"actual scalar/family reduced-plane/crop/Original origin-Gram checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
