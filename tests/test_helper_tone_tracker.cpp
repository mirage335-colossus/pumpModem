#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_reference_precision.hpp"
#include "datapump/helper_phase_precision.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
struct Source {
    std::mt19937 rng{12397};std::normal_distribution<double> noise{0,1};
    double phase=.4;
    std::vector<float> block(std::uint32_t rate,std::uint64_t first,std::size_t count,double hz,
            double drift,double amplitude,double noise_sigma,bool punctuation=false) {
        std::vector<float> out(count);
        for(std::size_t i=0;i<count;++i) {
            const auto t=static_cast<double>(first+i)/rate;
            phase+=2*std::numbers::pi*(hz+drift*t)/rate;
            const auto local=std::fmod(t,.4);
            const auto sign=punctuation && local>=.24 && local<.28?-1.:1.;
            out[i]=static_cast<float>(amplitude*sign*std::cos(phase)+noise_sigma*noise(rng));
        }
        return out;
    }
};
helper::ToneTrackerSnapshot run(helper::ToneTracker& tracker,Source& source,std::uint32_t rate,
        std::uint64_t first,double seconds,double hz,double drift,double amplitude,double noise_sigma,
        bool punctuation=false) {
    const auto end=first+static_cast<std::uint64_t>(seconds*rate);
    while(first<end) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(733,end-first));
        auto block=source.block(rate,first,count,hz,drift,amplitude,noise_sigma,punctuation);
        tracker.push(block,first);first+=count;
    }
    return tracker.snapshot();
}
void segmented_pilot_dates_phase_and_retunes() {
    constexpr unsigned rate=48000;constexpr double tau=2*std::numbers::pi;
    // Preserve this independent-aperture regression alongside the separate
    // continuous-pilot tests; its200ms/36s bounds remain intentional.
    helper::ToneTrackerConfig c{rate,2062.5,9000,0,.5,true,false};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    std::array<float,480> block{};double phase=.37,scale=1;unsigned qualified=0,phases=0,phase_fits=0;
    constexpr double common_error=300.37;
    for(std::uint64_t n=0;n<8ULL*rate;n+=block.size()) {
        if(n && n%(rate*2/5)==0) {
            const auto hop=n/(rate*2/5);
            // Explicit RF model, with laboratory-known zero audio-clock error.
            // Alternating sidebands and7/7.7/8.4/9.1MHz actual RF hops.
            constexpr double rf_hz[]{7e6,7.7e6,8.4e6,9.1e6};
            scale=(hop%2?-1.:1.)*rf_hz[hop%4]/7e6;
            phase=std::remainder(.173*hop*hop,tau);
            tracker.begin_pilot_segment(n,2062.5,9000,true,scale);
            check(!tracker.snapshot().phase_qualified && !tracker.snapshot().phase_fit_qualified &&
                !tracker.snapshot().phase_fit_support_uncertainty_cycles,"retune retained phase authority");
        }
        const auto f=2062.5+common_error*scale;
        for(std::size_t i=0;i<block.size();++i)block[i]=n>=static_cast<std::uint64_t>(2.8*rate) && n<static_cast<std::uint64_t>(3.2*rate)?0:static_cast<float>(.6*std::cos(phase+tau*f*static_cast<double>(n+i)/rate));
        tracker.push(block,n);const auto s=tracker.snapshot();
        check(s.captured_end_sample==n+block.size(),"pilot advanced or lost captured frontier");
        check(!s.phase_qualified,"diagnostic projection qualified pilot phase correction");
        if(s.qualified) {
            ++qualified;check(s.first_sample<=s.sample && s.sample<s.captured_end_sample,"pilot support dates invalid");
            check(std::abs(s.frequency_hz-f)<=s.uncertainty_hz+1e-7,"mapped independent-phase RF interval escape");
            check(s.coherent_aperture_seconds<=.20001 && s.frequency_support_seconds<=36,"pilot invented coherent aperture");
        }
        if(s.projection_phase_valid) {
            ++phases;const auto t=(static_cast<double>(s.phase_sample)+s.phase_sample_fraction)/rate;
            check(std::abs(std::remainder(s.phase_cycles-phase/tau-f*t,1.))<1e-5,"pilot absolute phase sign or fractional date changed");
        }
        if(s.phase_fit_qualified) {
            ++phase_fits;const auto t=(static_cast<double>(s.phase_fit_sample)+s.phase_fit_sample_fraction)/rate;
            check(s.phase_fit_support_uncertainty_cycles.has_value() &&
                std::isfinite(*s.phase_fit_support_uncertainty_cycles) && *s.phase_fit_support_uncertainty_cycles>=0,
                "qualified pilot phase fit omitted its joint whole-support bound");
            check(s.phase_fit_first_sample<=s.phase_fit_sample && s.phase_fit_sample<s.phase_fit_end_sample &&
                s.phase_fit_end_sample<=s.captured_end_sample,"pilot phase fit support escaped capture");
            check(s.phase_fit_end_sample-s.phase_fit_first_sample<=rate/5,"pilot phase fit invented coherence");
            check(std::abs(std::remainder(s.phase_fit_cycles-phase/tau-f*t,1.))<1e-5,"context absolute phase sign or date changed");
            check(std::abs(s.phase_fit_frequency_hz-f)<=s.phase_fit_frequency_uncertainty_hz+1e-6,"context local frequency escaped interval");
        }
    }
        // Context fits may support only their actual dated input. Their local
        // frequency and absolute phase have no cross-retune authority.
    check(qualified>100 && phases>100 && phase_fits>100,"independent-phase pilot did not acquire usefully");
    block.fill(0);for(std::uint64_t n=8ULL*rate;n<10ULL*rate;n+=block.size())tracker.push(block,n);
    check(!tracker.snapshot().present && !tracker.snapshot().qualified && !tracker.snapshot().phase_fit_qualified &&
        !tracker.snapshot().phase_fit_support_uncertainty_cycles,"pilot loss retained authority");
}
void segmented_frequency_query_dates_and_covariance() {
    constexpr unsigned rate=48000;
    constexpr auto origin=(std::uint64_t{1}<<60)+1234;
    constexpr double f0=5062.87,drift=.5,tau=2*std::numbers::pi;
    helper::ToneTrackerConfig c{rate,2062.5,9000,0,.5,true,false};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c),origin);
    std::array<float,480> block{};unsigned queries=0;
    for(unsigned n=0;n<3*rate;n+=block.size()) {
        for(unsigned i=0;i<block.size();++i) {
            const auto t=(n+i)/static_cast<double>(rate);
            block[i]=static_cast<float>(.6*std::cos(.37+tau*(f0*t+.5*drift*t*t)));
        }
        tracker.push(block,origin+n);const auto s=tracker.snapshot();if(!s.qualified)continue;
        const auto at=s.sample-rate*2/5;const auto query=s.frequency_at(at,.5);
        check(query.valid,"observed dated frequency query declined");
        const auto truth=f0+drift*(static_cast<double>(at-origin)+.5)/rate;
        check(std::abs(query.frequency_hz-truth)<=query.uncertainty_hz+1e-5,"dated trend frequency escaped covariance");
        const auto endpoint=s.frequency_at(s.sample);
        check(endpoint.valid && std::abs(endpoint.uncertainty_hz-s.uncertainty_hz)<1e-12,"frequency endpoint covariance mismatch");
        check(!s.frequency_at(s.sample+1).valid && !s.frequency_at(s.sample,.5).valid &&
            !s.frequency_at(s.first_sample-1).valid,"frequency query extrapolated beyond observed support");
        ++queries;
    }
    check(queries>100,"dated frequency query did not acquire enough support");
}
void segmented_frequency_fallback_is_disjoint_and_frequency_only() {
    constexpr unsigned rate=48000;constexpr double tau=2*std::numbers::pi,f=5062.87;
    helper::ToneTrackerConfig c{rate,2062.5,9000,.01,.5,true,false};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    std::mt19937 rng{49271};std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> phase_jump(-std::numbers::pi,std::numbers::pi);
    const auto noise=.3*std::sqrt(rate/std::pow(10.,2.4));
    std::array<float,480> block{};std::uint64_t last_context=0,last_end=0;
    unsigned contexts=0,fallbacks=0,qualified=0,escapes=0;int hop=-1;double phase=0;
    for(unsigned n=0;n<10*rate;n+=block.size()) {
        for(unsigned j=0;j<block.size();++j) {
            const auto t=(n+j)/static_cast<double>(rate);const auto h=static_cast<int>(std::floor((t-.137)/.4));
            if(h!=hop){hop=h;phase=phase_jump(rng);}
            block[j]=static_cast<float>((t<8?.6*std::cos(tau*f*t+phase):0)+noise*normal(rng));
        }
        tracker.push(block,n);const auto s=tracker.snapshot();
        check(!s.clock_qualified,"frequency-only gap model manufactured clock authority");
        if(s.frequency_used_fallback)check(!s.phase_fit_qualified && !s.phase_fit_support_uncertainty_cycles,
            "gap model published phase authority");
        if(s.frequency_context_end_sample!=last_context) {
            last_context=s.frequency_context_end_sample;
            if(last_context) {
                ++contexts;fallbacks+=s.frequency_used_fallback;
                check(s.frequency_context_end_sample-s.frequency_context_first_sample==rate/5 &&
                    s.frequency_context_first_sample>=last_end,"frequency contexts overlap or change aperture");
                check(s.frequency_gap_index<10 && s.coherent_aperture_seconds<=.20001 &&
                    s.frequency_aperture_cube_seconds3<=.0080001,"gap fit invented uninterrupted coherence");
                last_end=s.frequency_context_end_sample;
            }
        }
        if(s.qualified && n<8*rate) {
            ++qualified;check(std::abs(s.frequency_hz-f)<10,"frequency fallback selected a wrong tone");
            escapes+=std::abs(s.frequency_hz-f)>s.uncertainty_hz;
        }
    }
    std::cout<<"frequency-only fallback contexts="<<contexts<<" fallbacks="<<fallbacks
        <<" qualified_polls="<<qualified<<" escaped_polls="<<escapes<<'\n';
    check(contexts>=20 && fallbacks>=15 && qualified>=200,"24dBHz frequency fallback did not provide useful observations");
    check(!tracker.snapshot().qualified && !tracker.snapshot().present && !tracker.snapshot().phase_fit_qualified,
        "frequency fallback survived final reference loss");
}
void strong_wide_tone_and_drift() {
    const helper::ToneTrackerConfig c{48960,2062.5,9000,.01,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    std::cout<<"workspace_bytes="<<tracker.workspace_bytes()<<'\n';
    auto s=run(tracker,source,c.sample_rate,0,5,5062.5,.08,.6,.02,true);
    std::cout<<"wide frequency="<<s.frequency_hz<<" expected="<<5062.9<<" uncertainty="<<s.uncertainty_hz
        <<" CNO="<<s.carrier_to_noise_db_hz<<" slope="<<s.frequency_rate_hz_per_second
        <<" present="<<s.present<<" qualified="<<s.qualified<<'\n';
    check(s.qualified && s.present,"strong +3kHz tone did not qualify with punctuation");
    check(std::abs(s.frequency_hz-5062.9)<.25,"realtime helper lost bounded oscillator drift");
    check(s.uncertainty_hz<.5,"strong helper tracking did not narrow frequency usefully");
    check(tracker.workspace_bytes()==helper::ToneTracker::workspace_required(c),"tracker actual grant mismatch");
    s=run(tracker,source,c.sample_rate,s.captured_end_sample,2,5062.5,0,0,.02);
    check(!s.qualified && !s.present,"helper loss retained current qualification");
}
void representative_hf_noise_and_eme_level() {
    const helper::ToneTrackerConfig c{48960,2062.5,9000,.01,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    // With amplitude .6, these white-noise levels represent approximately
    // 49.7dBHz and31dBHz physical C/N0 before punctuation leakage/noise fitting.
    auto s=run(tracker,source,c.sample_rate,0,7,5062.5,.03,.6,.216,true);
    std::cout<<"HFnoise frequency="<<s.frequency_hz<<" expected="<<5062.71<<" uncertainty="<<s.uncertainty_hz
        <<" CNO="<<s.carrier_to_noise_db_hz<<" qualified="<<s.qualified<<'\n';
    check(s.qualified && std::abs(s.frequency_hz-5062.71)<.5,"HF49.7dBHz helper did not track");
    tracker.reset();source=Source{};
    s=run(tracker,source,c.sample_rate,0,10,5062.5,0,.6,1.87,true);
    std::cout<<"EMEnoise frequency="<<s.frequency_hz<<" uncertainty="<<s.uncertainty_hz
        <<" CNO="<<s.carrier_to_noise_db_hz<<" present="<<s.present<<" qualified="<<s.qualified<<'\n';
    // The precision estimator now uses independently dated phase slopes;
    // a fixed old-estimator width is not a physical noise limit. Retain the
    // actual weak observation's truth, process guard and measured support.
    check(!s.qualified || (std::abs(s.frequency_hz-5062.5)<=s.uncertainty_hz && s.uncertainty_hz>=.005+3*c.phase_noise_radians_per_sqrt_second/
            (2*std::numbers::pi*std::sqrt(std::max(.02,static_cast<double>(s.sample-s.first_sample+1)/c.sample_rate)))-1e-12 &&
        s.first_sample<=s.sample && s.sample<s.captured_end_sample),
        "weak noisy helper escaped its measured interval or process/support guard");
}
void narrow_range_and_noise() {
    const helper::ToneTrackerConfig c{6000,2062.5,3,0,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    auto s=run(tracker,source,c.sample_rate,0,5,2063.2,0,.6,.04);
    std::cout<<"narrow frequency="<<s.frequency_hz<<" uncertainty="<<s.uncertainty_hz<<" qualified="<<s.qualified<<'\n';
    check(s.qualified && std::abs(s.frequency_hz-2063.2)<.1,"narrow range below eight FFT bins failed");
    tracker.reset();source=Source{};
    s=run(tracker,source,c.sample_rate,0,8,2063.2,0,0,.1);
    check(!s.qualified,"fixed noise control falsely qualified");
    tracker.reset(900000);
    check(!tracker.snapshot().qualified && tracker.snapshot().captured_end_sample==900000,"reset retained stale state");
    bool rejected=false;try{tracker.push(std::vector<float>(1),900001);}catch(const Error&){rejected=true;}
    check(rejected,"tracker silently accepted a capture gap");
}
// Actual encrypted Generator windows can bias a4096-point FFT seed by a
// few Hz even at high C/N0. The complete measured reference must qualify
// within the same bounded precision lead-in planned for this physical tone;
// neither a seed nor elapsed airtime supplies runtime qualification.
void actual_helper_cold_capture_before_payload() {
    constexpr std::uint32_t rate=48000;
    constexpr std::uint64_t epoch=1793050572;
    constexpr double helper_hz=2062.5;
    struct Case {double offset,source_phase;std::size_t batch;};
    constexpr Case cases[]={{30,.287,1200},{-30,.287,1200},{0,.137,2400},
        {5.859375,.017,1200},{-5.859375,.217,2400},{11.6015625,.399,1200},
        {29.296875,.071,2400},{.137,.311,1200}};
    const Crypto key{std::array<std::uint8_t,32>{}}; // Public nonsecret fixture.
    helper::Options options;options.power_w=40;options.data_power_w=4;
    const helper::ToneTrackerConfig config{rate,helper_hz,90.7,.01,.5};
    helper::TonePrecisionRequest precision;
    precision.sample_rate=rate;
    // Real PCM: sinusoid power=A_peak²/2 and one-sided white N0=2*variance/Fs.
    precision.straight_cn0_db_hz=10*std::log10((.6*.6/2)/(2*.083*.083/rate));
    precision.phase_noise_radians_per_sqrt_second=config.phase_noise_radians_per_sqrt_second;
    precision.maximum_tracking_slew_hz_per_second=config.maximum_tracking_slew_hz_per_second;
    precision.metadata_symbols=3;precision.punctuate=true;
    precision.maximum_source_capture_ratio=1;
    precision.baseline_lead_seconds=2;precision.max_extra_seconds=36;precision.target_half_width_hz=.1;
    const auto planned=helper::predict_tone_precision(precision);
    check(std::isfinite(planned.total_lead_seconds) && planned.total_lead_seconds>=2 &&
        planned.total_lead_seconds<=38 && planned.extra_lead_seconds>=0 && planned.extra_lead_seconds<=36,
        "actual helper cold-capture plan lost its bounded lead-in");
    check(planned.supported && planned.target_reached,
        "strong physical helper did not predict the requested precision within the lead cap");
    const auto lead_samples=static_cast<std::uint64_t>(std::llround(planned.total_lead_seconds*rate));
    for(const auto c:cases) {
        helper::Generator generator(options,helper::Plan{helper_hz,.6,0,40},epoch,true,key);
        helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
        Source source;std::vector<float> block(c.batch);std::uint64_t first=0,first_qualified=0;
        helper::ToneTrackerSnapshot initial;
        while(first<lead_samples) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),lead_samples-first));
            for(std::size_t i=0;i<n;++i) {
                const auto sample=first+i;
                auto value=generator.mix_analytic({},static_cast<long double>(sample)/rate+c.source_phase,rate)*
                    std::polar(1.,2*std::numbers::pi*c.offset*static_cast<double>(sample)/rate);
                const auto real=std::clamp(value.real()+.083*source.noise(source.rng),-1.,32767./32768.);
                block[i]=static_cast<float>(std::round(real*32768)/32768.);
            }
            tracker.push(std::span(block).first(n),first);first+=n;
            const auto snapshot=tracker.snapshot();
            if(snapshot.qualified && !first_qualified){first_qualified=first;initial=snapshot;}
        }
        std::cout<<"actual cold capture offset="<<c.offset<<" source_phase="<<c.source_phase
            <<" first_qualified_seconds="<<static_cast<double>(first_qualified)/rate
            <<" first_error="<<initial.frequency_hz-helper_hz-c.offset
            <<" initial_U="<<initial.uncertainty_hz<<" expected_CNO="<<precision.straight_cn0_db_hz
            <<" planned_lead="<<planned.total_lead_seconds<<" planned_extra="<<planned.extra_lead_seconds
            <<" predicted_U="<<planned.frequency_half_width_hz<<'\n';
        check(first_qualified && first_qualified<=lead_samples,
            "actual helper reference did not qualify before its planned payload start");
        check(std::abs(initial.frequency_hz-helper_hz-c.offset)<=initial.uncertainty_hz,
            "first actual helper qualification escaped its engineering interval");
        const auto last=tracker.snapshot();
        check(last.qualified && std::abs(last.frequency_hz-helper_hz-c.offset)<=last.uncertainty_hz,
            "actual helper capture did not hand off to ordinary tracking");
    }
}
void phase_kernel_covariance_joint_fit_and_provenance() {
    using namespace helper::phase_detail;
    constexpr unsigned rate=48000;
    constexpr std::uint64_t origin=1ULL<<60;
    constexpr std::size_t block=960;
    constexpr double reference=2062.5,sigma=.01,amplitude=.75;
    std::array<double,context_size> times{},noise{};
    for(std::size_t i=0;i<context_size;++i){times[i]=(.5+i)*.02;noise[i]=.01*(1+.15*i);}
    const auto covariance=joint_covariance(times,noise,amplitude*amplitude,sigma,.2);
    check(covariance.valid,"joint covariance rejected heterogeneous observation weights");
    auto shifted=times,scaled_noise=noise;
    for(auto& t:shifted)t+=17.25;
    for(auto& n:scaled_noise)n*=4;
    const auto translated=joint_covariance(shifted,noise,amplitude*amplitude,sigma,.2);
    const auto scaled=joint_covariance(times,scaled_noise,amplitude*amplitude,sigma,.2);
    const auto close=[](double a,double b){return std::abs(a-b)<=1e-10*std::max({1e-12,std::abs(a),std::abs(b)});};
    check(translated.valid && close(translated.frequency_variance,covariance.frequency_variance) &&
        close(translated.phase_variance,covariance.phase_variance),"joint covariance depends on an arbitrary date origin");
    check(scaled.valid && close(scaled.thermal_phase_variance,4*covariance.thermal_phase_variance) &&
        close(scaled.thermal_frequency_variance,4*covariance.thermal_frequency_variance) &&
        close(scaled.process_frequency_variance,covariance.process_frequency_variance),
        "joint covariance confuses thermal gain with oscillator process noise");
    // Dense point-Brownian covariance is an independent lower comparison for
    // the deliberately conservative actual-weight/full-Hann-support bound.
    double sw=0,st=0;for(std::size_t i=0;i<context_size;++i){sw+=1/noise[i];st+=times[i]/noise[i];}
    const auto mean=st/sw;double tt=0;
    for(std::size_t i=0;i<context_size;++i)tt+=(times[i]-mean)*(times[i]-mean)/noise[i];
    double dense_process=0;
    for(std::size_t i=0;i<context_size;++i)for(std::size_t j=0;j<context_size;++j)
        dense_process+=sigma*sigma*std::min(times[i],times[j])*(times[i]-mean)*(times[j]-mean)/
            (noise[i]*noise[j]*tt*tt*tau*tau);
    check(covariance.process_frequency_variance+1e-15>=dense_process,
        "joint process bound dropped weighted Brownian covariance");
    // Concentrating weight on neighboring 20 ms rows exposes the old
    // uniform-spread process bound: dense Brownian slope variance approaches
    // 50*sigma²/tau², exceeding its 10*sigma²/tau² assumption for 200 ms.
    std::array<double,context_size> concentrated_noise{};concentrated_noise.fill(1e8);
    concentrated_noise[4]=concentrated_noise[5]=1;
    const auto concentrated=joint_covariance(times,concentrated_noise,amplitude*amplitude,sigma,.2);
    double concentrated_sw=0,concentrated_st=0;
    for(std::size_t i=0;i<context_size;++i){concentrated_sw+=1/concentrated_noise[i];concentrated_st+=times[i]/concentrated_noise[i];}
    const auto concentrated_mean=concentrated_st/concentrated_sw;double concentrated_tt=0;
    for(std::size_t i=0;i<context_size;++i)
        concentrated_tt+=(times[i]-concentrated_mean)*(times[i]-concentrated_mean)/concentrated_noise[i];
    double concentrated_dense=0;
    for(std::size_t i=0;i<context_size;++i)for(std::size_t j=0;j<context_size;++j)
        concentrated_dense+=sigma*sigma*std::min(times[i],times[j])*(times[i]-concentrated_mean)*(times[j]-concentrated_mean)/
            (concentrated_noise[i]*concentrated_noise[j]*concentrated_tt*concentrated_tt*tau*tau);
    const auto obsolete_uniform_bound=2*sigma*sigma/(tau*tau*.2);
    check(concentrated_dense>4*obsolete_uniform_bound,
        "concentrated covariance fixture did not distinguish the obsolete uniform-spread bound");
    check(concentrated.valid && concentrated.process_frequency_variance+1e-15>=concentrated_dense,
        "joint process bound fails concentrated-weight Brownian covariance");
    double ws=0,w2=0;
    for(std::size_t i=0;i<block;++i){const auto w=.5-.5*std::cos(tau*i/(block-1));ws+=w;w2+=w*w;}
    const auto bandwidth=rate*w2/(ws*ws);
    const auto row=[&](std::uint64_t index,std::uint64_t generation=7){
        Projection p;p.first=origin+index*block;p.end=p.first+block;p.generation=generation;p.origin=origin;p.rate=rate;
        const auto t=(static_cast<double>(p.first-origin)+.5*(block-1))/rate;
        p.value=amplitude*std::polar(1.,tau*(.123+.2*t));
        p.complex_noise_variance=amplitude*amplitude*bandwidth/1e4;
        p.fll_hz=reference+.15;p.fll_uncertainty=.1;p.reference_hz=reference;p.qualified=true;
        return p;
    };
    std::array<Projection,context_size> rows{};
    for(std::size_t i=0;i<context_size;++i)rows[i]=row(i);
    const auto joint=joint_context(rows,sigma);
    check(joint.valid && std::abs(joint.hz-reference-.2)<1e-6 &&
        std::abs(std::remainder(joint.phase-(.123+.2*joint.seconds),1.))<1e-7,
        "joint complex fit lost frequency or actual large-origin phase coordinates");
    auto scaled_rows=rows;for(auto& r:scaled_rows){r.value*=2;r.complex_noise_variance*=4;}
    const auto same=joint_context(scaled_rows,sigma);
    check(same.valid && close(same.variance,joint.variance) && close(same.frequency_sigma,joint.frequency_sigma),
        "joint reference precision changes when signal and noise receive the same gain");
    auto bad=rows;bad[3].first+=1;
    check(!joint_context(bad,sigma).valid,"joint context accepted a PCM date gap");
    bad=rows;++bad[3].generation;
    check(!joint_context(bad,sigma).valid,"joint context combined different NCO generations");
    bad=rows;for(auto& r:bad)r.complex_noise_variance*=1e6;
    check(!joint_context(bad,sigma).valid,"joint context qualified insufficient aggregate tone SNR");
    bad=rows;bad[context_size/2].value*=-1;
    check(!joint_context(bad,sigma).valid,"joint complex residual accepted a disturbed context");
    // A narrow FLL interval is not a restriction on this independent local
    // likelihood maximum; ordinary final FLL compatibility is checked later.
    bad=rows;for(auto& r:bad){r.fll_hz=reference;r.fll_uncertainty=.001;}
    check(joint_context(bad,sigma).valid,"old narrow FLL boundary censored an independent joint maximum");
    bad=rows;
    for(auto& r:bad){const auto t=(static_cast<double>(r.first-origin)+.5*(block-1))/rate;
        r.value=amplitude*std::polar(1.,tau*(.123+2.15*t));}
    check(!joint_context(bad,sigma).valid,"joint fit accepted a clipped coherent-lobe endpoint");
    Runtime runtime;
    for(std::uint64_t i=0;i<80;++i)runtime.observe(row(i),sigma);
    const auto now=80.*block/rate-1./rate;
    const auto measured=runtime.estimate(now,sigma);
    check(measured.valid && std::abs(measured.hz-reference-.2)<=measured.uncertainty,
        "phase-only history did not contain deterministic joint frequency truth");
    check(measured.first>=origin && measured.end<=origin+80*block && measured.first<measured.end,
        "phase history manufactured raw support dates");
    for(std::size_t i=1;i<runtime.used;++i)
        check(runtime.anchors[i].first>=runtime.anchors[i-1].end,
            "phase history treated overlapping context thermal noise as independent");
    const auto anchor_count=runtime.used;
    auto disturbed=row(80);disturbed.value*=std::complex<double>{0,1};runtime.observe(disturbed,sigma);
    check(runtime.used==anchor_count && runtime.estimate(now+.02,sigma).valid,
        "impure current context discarded a fresh independently qualified frequency reference");
    check(!runtime.estimate(runtime.anchors[runtime.used-1].seconds+.500001,sigma).valid,
        "phase history leased qualification beyond its actual 0.5 second freshness bound");
    runtime.observe(row(81,8),sigma);
    check(!runtime.used && !runtime.estimate(now+.04,sigma).valid,
        "new mixer generation retained old phase support");
    runtime.reset();check(!runtime.used && !runtime.current,"phase reset retained a qualified reference");
    std::cout<<"joint phase covariance/fit/provenance checks passed\n";
}

void sparse_segments_keep_recent_aperture() {
    const helper::ToneTrackerConfig c{48000,2062.5,20,0,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    auto s=run(tracker,source,c.sample_rate,0,2,2065,0,.6,.02);
    check(s.qualified,"sparse control failed initial tone acquisition");
    auto first=s.captured_end_sample;
    const auto end=first+12ULL*c.sample_rate;
    while(first<end) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(960,end-first));
        const auto active=(first%(c.sample_rate*2/5))<c.sample_rate*2/25;
        auto block=source.block(c.sample_rate,first,count,2065,0,active?.6:0.,.02);
        tracker.push(block,first);first+=count;
    }
    s=tracker.snapshot();
    const auto aperture=static_cast<double>(s.sample-s.first_sample+1)/c.sample_rate;
    std::cout<<"sparse actual_measurement_aperture="<<aperture<<'\n';
    check(s.sample>2ULL*c.sample_rate,"sparse control never formed a later actual fit");
    check(aperture<=36.05,"sparse valid segments retained unbounded old frequency history");
}
void stable_full_capacity_keeps_moving() {
    constexpr std::uint32_t rate=48000;
    const helper::ToneTrackerConfig c{rate,2062.5,20,0,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    for(std::uint64_t first=0;first<40ULL*rate;first+=960) {
        auto block=source.block(rate,first,960,2063.2,0,.6,.02);
        tracker.push(block,first);const auto s=tracker.snapshot();
        if(first>=5ULL*rate) {
            check(s.qualified,"stable full-capacity history lost qualification");
            const auto span=static_cast<double>(s.sample-s.first_sample+1)/rate;
            check(span<=36.05,"numeric phase history exceeded its36s source support bound");
            if(first>=38ULL*rate)check(s.first_sample>=static_cast<std::uint64_t>(1.9*rate),
                "full precision history stopped advancing its source support");
        }
    }
}
void shared_endpoint_precision_of_stable_tone() {
    const helper::ToneTrackerConfig c{48000,2062.5,20,.01,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    const auto s=run(tracker,source,c.sample_rate,0,8,2063.2,0,.6,.083);
    std::cout<<"stable58dBHz frequency="<<s.frequency_hz<<" error="<<std::abs(s.frequency_hz-2063.2)
        <<" uncertainty="<<s.uncertainty_hz<<" CNO="<<s.carrier_to_noise_db_hz<<'\n';
    check(s.qualified && std::abs(s.frequency_hz-2063.2)<=s.uncertainty_hz,
        "stable-tone frequency escaped its engineering uncertainty");
    check(s.uncertainty_hz<.035,"stable tone precision discarded shared phase-endpoint covariance");
}
double smooth(double x) {
    x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));
}
void legacy_paired_modulation_rolling_fifo() {
    constexpr std::uint32_t rate=48000;
    const helper::ToneTrackerConfig c{rate,2062.5,20,.01,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    double phase=.37;
    std::vector<float> samples(733);
    const auto end=12ULL*rate;std::uint64_t first=0;
    while(first<end) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),end-first));
        for(std::size_t i=0;i<n;++i) {
            const auto t=static_cast<double>(first+i)/rate;
            const auto hz=2062.5+.08*std::max(0.,t-3.);
            phase+=2*std::numbers::pi*hz/rate;
            // Historical single-tone fixture:400ms grid,4ms ramps,paired8ms C2 reversals and
            // three8ms APSK blends ending388ms plus8ms return. The137ms
            // offset deliberately makes transitions cut through loop windows.
            const auto local=std::fmod(t+.137,.4);
            const auto envelope=smooth(std::min(local,.4-local)/.004);
            const auto punct=1-2*smooth((local-.236)/.008)+2*smooth((local-.276)/.008);
            std::complex<double> modulation{punct,0};
            if(local>=.364) {
                const auto position=(local-.364)/.008;
                const auto index=static_cast<unsigned>(std::floor(position));
                const auto dwell=static_cast<unsigned>(std::floor((t+.137)/.4));
                const auto point=[&](unsigned j) {
                    const auto symbol=((dwell+17)*(373+j*227))&((1U<<21)-1);
                    return std::polar(.9+.1*(symbol>>11)/1023.,
                        2*std::numbers::pi*(symbol&2047U)/2048.);
                };
                if(index<3) {
                    const auto previous=index?point(index-1):std::complex<double>{1,0};
                    modulation=punct*(previous+(point(index)-previous)*smooth(position-index));
                }else if(index==3)modulation=punct*(point(2)+(std::complex<double>{1,0}-point(2))*smooth(position-index));
            }
            samples[i]=static_cast<float>(.6*envelope*std::real(modulation*std::polar(1.,phase))+.083*source.noise(source.rng));
        }
        tracker.push(std::span(samples).first(n),first);first+=n;
    }
    const auto s=tracker.snapshot();
    const auto expected=2062.5+.08*9;
    std::cout<<"realistic rolling frequency="<<s.frequency_hz<<" expected="<<expected
        <<" uncertainty="<<s.uncertainty_hz<<" CNO="<<s.carrier_to_noise_db_hz
        <<" support_first_seconds="<<static_cast<double>(s.first_sample)/rate<<'\n';
    check(s.qualified && std::abs(s.frequency_hz-expected)<.2,"realistic rolling helper did not track after multiple history capacities");
    check(std::abs(s.frequency_hz-expected)<=s.uncertainty_hz,
        "realistic rolling helper escaped its measured model interval");
    check(s.first_sample<=s.sample && s.sample<s.captured_end_sample &&
        static_cast<double>(s.sample-s.first_sample+1)/rate<=36.05,
        "realistic numeric phase support exceeded its source-date window");
}
struct ClockPayloadStats {
    std::size_t polls=0,qualified=0;
    std::vector<double> uncertainties;
    double maximum_error=0;
};
// Explicit legacy paired-punctuation waveform. This preserves the independent
// clock-kernel, cadence, interference and provenance regressions; it does not
// describe the current two-tone Generator or qualify its pure pilot clock.
helper::ToneTrackerSnapshot clock_wave(double ppm,double seconds,double noise_sigma,unsigned shape=0,
        std::uint32_t rate=48000,double rf_offset=0,bool loss=false,std::uint64_t origin=0,
        std::span<const float> payload={},ClockPayloadStats* payload_stats=nullptr) {
    const helper::ToneTrackerConfig c{rate,2062.5,9000,.01,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c),origin);Source source;
    std::vector<float> samples(733);std::uint64_t first=origin;
    const auto end=origin+static_cast<std::uint64_t>(seconds*rate);
    while(first<end) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),end-first));
        for(std::size_t i=0;i<n;++i) {
            const auto t=static_cast<double>(first+i-origin)/rate;
            const auto source_time=t*(1+ppm*1e-6)+.137;
            const auto period=shape==4?.2:shape==5?.41:.4;
            const auto local=std::fmod(source_time,period);
            const auto dwell=static_cast<unsigned>(std::floor(source_time/period));
            const auto envelope=smooth(std::min(local,period-local)/.004);
            auto punct=shape==1?(local<.24?1.:-1.):
                shape==2?1.:shape==3 && (dwell&1)?1.:
                    1-2*smooth((local-(period*.6-.004))/.008)+
                      2*smooth((local-(period*.6+.036))/.008);
            if(shape==6 && (dwell==12 || dwell==25))
                punct*=1-2*smooth((local-.096)/.008);
            std::complex<double> modulation{punct,0};
            if((shape<4 || shape==6) && local>=.364) {
                const auto position=(local-.364)/.008;
                const auto index=static_cast<unsigned>(std::floor(position));
                const auto point=[&](unsigned j) {
                    const auto symbol=((dwell+17)*(373+j*227))&((1U<<21)-1);
                    return std::polar(.9+.1*(symbol>>11)/1023.,
                        2*std::numbers::pi*(symbol&2047U)/2048.);
                };
                if(index<3) {
                    const auto previous=index?point(index-1):std::complex<double>{1,0};
                    modulation=punct*(previous+(point(index)-previous)*smooth(position-index));
                }else if(index==3)modulation=punct*(point(2)+(std::complex<double>{1,0}-point(2))*smooth(position-index));
            }
            const auto phase=2*std::numbers::pi*(2062.5*source_time+rf_offset*t)+.371;
            const auto amplitude=loss && t>seconds-1.?.0:.6;
            auto value=amplitude*envelope*std::real(modulation*std::polar(1.,phase))+noise_sigma*source.noise(source.rng);
            if(first+i-origin<payload.size())value+=payload[static_cast<std::size_t>(first+i-origin)];
            samples[i]=static_cast<float>(std::round(value*32767)/32767);
        }
        tracker.push(std::span(samples).first(n),first);first+=n;
        if(payload_stats && first-origin>=6ULL*rate && first-origin<payload.size()) {
            const auto snapshot=tracker.snapshot();++payload_stats->polls;
            if(snapshot.clock_qualified) {
                ++payload_stats->qualified;payload_stats->uncertainties.push_back(snapshot.clock_uncertainty_ppm);
                payload_stats->maximum_error=std::max(payload_stats->maximum_error,std::abs(snapshot.clock_error_ppm-ppm));
            }
        }
    }
    return tracker.snapshot();
}
void legacy_paired_clock_survives_ordinary_payload() {
    constexpr std::uint32_t rate=48000;
    modem::Config data;data.sample_rate=rate;data.carrier_hz=1500;data.bandwidth_hz=45;
    data.dsss_factor=10;data.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    data.spreading_factor=64;data.integration_seconds=14.2;data.training_seconds=2;data.scramble=true;
    data.memory_limit=64ULL*1024*1024;
    data.data_key=Crypto{std::array<std::uint8_t,32>{}};
    modem::PatternTransmitter transmitter(Bytes{1},data,1800000000,0,false);
    std::vector<float> ordinary(static_cast<std::size_t>(transmitter.total_samples()));
    std::size_t emitted=0;
    while(emitted<ordinary.size()) {
        const auto count=transmitter.read(std::span(ordinary).subspan(emitted));
        check(count>0,"ordinary payload interference generator stopped early");emitted+=count;
    }
    double energy=0;for(const auto value:ordinary)energy+=static_cast<double>(value)*value;
    check(energy>0,"ordinary payload interference had no physical power");
    // A .6-peak helper CW has .18 mean-square power. The independently
    // generated ordinary Robust PCM is normalized to one tenth that power.
    const auto gain=std::sqrt(.018*ordinary.size()/energy);
    std::vector<float> payload(2ULL*rate+ordinary.size(),0);
    for(std::size_t i=0;i<ordinary.size();++i)payload[2ULL*rate+i]=static_cast<float>(ordinary[i]*gain);
    ClockPayloadStats stats;
    const auto end=clock_wave(0,static_cast<double>(payload.size())/rate,.0083,0,rate,.317,false,0,payload,&stats);
    std::sort(stats.uncertainties.begin(),stats.uncertainties.end());
    const auto fraction=stats.polls?static_cast<double>(stats.qualified)/stats.polls:0;
    const auto median=stats.uncertainties.empty()?std::numeric_limits<double>::infinity():
        stats.uncertainties[stats.uncertainties.size()/2];
    const auto p90=stats.uncertainties.empty()?std::numeric_limits<double>::infinity():
        stats.uncertainties[stats.uncertainties.size()*9/10];
    std::cout<<"ordinary payload power_ratio_db=10 bandwidth="<<modem::waveform_bandwidth_hz(data)
        <<" seconds="<<static_cast<double>(ordinary.size())/rate<<" clock_qualified_fraction="<<fraction
        <<" clock_median_uncertainty="<<median<<" clock_p90_uncertainty="<<p90
        <<" clock_max_error="<<stats.maximum_error<<" final_qualified="<<end.clock_qualified<<'\n';
    check(stats.polls>100 && fraction>.9,"ordinary payload repeatedly removed independent punctuation clock");
    check(p90<20,"ordinary payload contaminated narrowband punctuation clock precision");
}
void actual_two_tone_pilot_keeps_clock_unresolved() {
    constexpr unsigned rate=48000;
    constexpr std::uint64_t epoch=1793050572;
    modem::Config data;data.sample_rate=rate;data.carrier_hz=1500;data.bandwidth_hz=45;
    data.dsss_factor=10;data.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    data.spreading_factor=64;data.integration_seconds=14.2;data.training_seconds=2;data.scramble=true;
    data.memory_limit=64ULL*1024*1024;
    const Crypto key{std::array<std::uint8_t,32>{}};data.data_key=key;
    modem::PatternTransmitter transmitter(Bytes{1,0,1,1,0,0,1,0},data,epoch,0,false);
    std::vector<float> payload(static_cast<std::size_t>(transmitter.total_samples()));
    std::size_t emitted=0;
    while(emitted<payload.size()) {
        const auto count=transmitter.read(std::span(payload).subspan(emitted));
        check(count>0,"actual helper interference generator stopped early");emitted+=count;
    }
    double energy=0;for(const auto value:payload)energy+=static_cast<double>(value)*value;
    check(energy>0,"actual helper interference had no physical power");
    const auto gain=std::sqrt(.018*payload.size()/energy);
    helper::Options options;options.power_w=40;options.data_power_w=4;
    auto placement=helper::plan(options,helper::placement_band(data),rate);
    // Actual current-format geometry, with total Helper straight-tone power.18
    // and ordinary payload power.018 in this component mixture. Pilot power is
    // .9*.18; auxiliary power is .1*.18. The public fixed key enables the actual
    // encrypted auxiliary timecode. No metadata is consumed by either tracker.
    placement.helper_gain=.6;placement.data_gain=0;
    helper::Generator generator(options,placement,epoch,true,key);
    auto pilot_placement=placement;pilot_placement.auxiliary_frequency_hz=0;
    helper::Generator pilot_only(options,pilot_placement,epoch,true,key);
    const auto pilot_hz=placement.frequency_hz;
    const auto clock_coefficient=pilot_hz-data.carrier_hz;
    const helper::ToneTrackerConfig config{rate,pilot_hz,9000,.01,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    helper::ToneTracker pilot_tracker(config,helper::ToneTracker::workspace_required(config));
    Source source;std::vector<float> block(733),pilot_block(733);std::uint64_t first=0;
    const auto end=payload.size()+8ULL*rate;std::size_t polls=0,qualified=0,pilot_qualified=0;
    double auxiliary_energy=0,maximum_frequency_difference=0;
    modem::PatternSearch search;search.admitted_clock_half_width_ppm=300;
    search.admitted_clock_cell_half_width_ppm=37.5;
    const auto rf_step=.25*rate/modem::symbol_sample_count(data);
    for(int lf=-4;lf<=4;++lf)for(int rf=-20;rf<=20;++rf)
        search.hypotheses.push_back({rf*rf_step-clock_coefficient*(lf*75.)*1e-6,lf*75.});
    modem::detail::PatternHelperFrequencySelector selector;
    check(selector.initialize(search,{0,1,clock_coefficient,37.5}),"measured frequency work geometry unavailable");
    auto passthrough_selector=selector;const auto selector_bytes=selector.working_bytes();
    std::vector<double> frequency_uncertainties,selected_pairs,passthrough_pairs;
    double maximum_frequency_error=0;std::size_t frequency_interval_escapes=0;
    while(first<end) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),end-first));
        for(std::size_t i=0;i<count;++i) {
            const auto sample=first+i;
            const auto seconds=static_cast<long double>(sample)/rate+.137L;
            const auto composite=generator.mix(0,seconds,rate),pilot=pilot_only.mix(0,seconds,rate);
            auxiliary_energy+=static_cast<double>(composite-pilot)*(composite-pilot);
            double common=.0083*source.noise(source.rng);
            if(sample>=2ULL*rate && sample<2ULL*rate+payload.size())common+=gain*payload[sample-2ULL*rate];
            block[i]=static_cast<float>(std::round((composite+common)*32767)/32767);
            pilot_block[i]=static_cast<float>(std::round((pilot+common)*32767)/32767);
        }
        tracker.push(std::span(block).first(count),first);
        pilot_tracker.push(std::span(pilot_block).first(count),first);first+=count;
        if(first>6ULL*rate && first<2ULL*rate+payload.size()) {
            ++polls;const auto result=tracker.snapshot(),control=pilot_tracker.snapshot();
            check(!result.clock_qualified && !control.clock_qualified && result.clock_sample==0 &&
                control.clock_sample==0,"pure pilot or auxiliary metadata manufactured a source clock");
            pilot_qualified+=control.qualified;
            if(result.qualified) {
                ++qualified;frequency_uncertainties.push_back(result.uncertainty_hz);
                maximum_frequency_error=std::max(maximum_frequency_error,std::abs(result.frequency_hz-pilot_hz));
                frequency_interval_escapes+=std::abs(result.frequency_hz-pilot_hz)>result.uncertainty_hz;
                if(control.qualified) {
                    maximum_frequency_difference=std::max(maximum_frequency_difference,std::abs(result.frequency_hz-control.frequency_hz));
                    check(std::abs(result.frequency_hz-control.frequency_hz)<=result.uncertainty_hz+control.uncertainty_hz,
                        "auxiliary modulation changed the accepted pure-pilot interval");
                }
                const auto measured_offset=result.frequency_hz-pilot_hz;
                const auto applied_measured_translation=measured_offset;
                // Frequency-only counterfactual in the ordinary corrected
                // view. No qualified source-clock interval is supplied.
                const auto corrected_center=measured_offset-applied_measured_translation;
                selector.update({corrected_center,result.uncertainty_hz,clock_coefficient,37.5});
                passthrough_selector.update({measured_offset,result.uncertainty_hz,clock_coefficient,37.5});
                check(!selector.window.clock_interval && selector.working_bytes()==selector_bytes,
                    "frequency assistance invented clock authority or allocated new geometry");
                for(int lf=-4;lf<=4;++lf) {
                    bool retained=false;
                    for(std::size_t i=0;i<search.hypotheses.size();++i)
                        retained|=search.hypotheses[i].clock_error_ppm==lf*75. && selector.keep(i);
                    check(retained,"pure pilot discarded an unresolved original clock alternative");
                }
                check(selector.selected>=9 && selector.selected<search.hypotheses.size(),
                    "pure-pilot frequency did not narrow RF work across the original clock domain");
                selected_pairs.push_back(static_cast<double>(selector.selected));
                passthrough_pairs.push_back(static_cast<double>(passthrough_selector.selected));
            }
        }
    }
    const auto fraction=polls?static_cast<double>(qualified)/polls:0;
    const auto control_fraction=polls?static_cast<double>(pilot_qualified)/polls:0;
    std::cout<<"actual two-tone helper payload_seconds="<<static_cast<double>(payload.size())/rate
        <<" pilot_hz="<<pilot_hz<<" auxiliary_hz="<<placement.auxiliary_frequency_hz
        <<" frequency_qualified_fraction="<<fraction<<" pilot_only_fraction="<<control_fraction
        <<" auxiliary_energy="<<auxiliary_energy<<" maximum_auxiliary_frequency_difference="<<maximum_frequency_difference<<'\n';
    check(polls>100 && fraction>.99 && control_fraction>.99 && auxiliary_energy>0,
        "actual two-tone pilot failed independent frequency qualification");
    std::sort(frequency_uncertainties.begin(),frequency_uncertainties.end());
    std::sort(selected_pairs.begin(),selected_pairs.end());std::sort(passthrough_pairs.begin(),passthrough_pairs.end());
    check(!frequency_uncertainties.empty(),"actual helper produced no measured frequency work evidence");
    std::cout<<"actual pilot frequency median_U="<<frequency_uncertainties[frequency_uncertainties.size()/2]
        <<" p90_U="<<frequency_uncertainties[frequency_uncertainties.size()*9/10]
        <<" max_error="<<maximum_frequency_error<<" interval_escapes="<<frequency_interval_escapes<<" rf_step="<<rf_step
        <<" unresolved_clock_cells=9 corrected_counterfactual_selected_median="<<selected_pairs[selected_pairs.size()/2]
        <<" corrected_counterfactual_selected_p90="<<selected_pairs[selected_pairs.size()*9/10]
        <<" raw_passthrough_selected_median="<<passthrough_pairs[passthrough_pairs.size()/2]<<'\n';
    check(!frequency_interval_escapes,"actual pure pilot frequency escaped its engineering interval");
}
void off_cadence_feature_does_not_poison_established_clock() {
    constexpr unsigned rate=48000;
    // A locally exact extra8msC2 reversal after independent qualification
    // represents a rare ramp/metadata false feature. It has no400ms dwell
    // alias. Rejecting it must preserve fresh genuine markers, not renew any
    // old interval or poison the whole retained6s clock aperture.
    std::vector<float> no_payload(16ULL*rate,0);
    ClockPayloadStats stats;
    const auto result=clock_wave(0,16,.0083,6,rate,.317,false,0,no_payload,&stats);
    const auto fraction=stats.polls?static_cast<double>(stats.qualified)/stats.polls:0;
    std::cout<<"off_cadence qualified_fraction="<<fraction<<" final_clock="<<result.clock_qualified
        <<" final_support="<<result.clock_last_sample<<" captured_end="<<result.captured_end_sample<<'\n';
    check(stats.polls>100 && fraction>.99 && result.clock_qualified,
        "off-cadence feature poisoned an independently established clock");
    check(result.clock_sample==result.clock_last_sample && result.clock_last_sample<result.captured_end_sample &&
        result.captured_end_sample-1-result.clock_last_sample<=static_cast<std::uint64_t>(.55*rate),
        "off-cadence rejection manufactured fresh marker support");
}
void independent_punctuation_clock() {
    // The narrower8ms marker window and explicit overlap covariance require
    // a longer independent clock aperture. Keep the20ppm precision target;
    // frequency assistance does not wait for this optional clock warmup.
    for(const auto ppm:{150.,-300.,300.,-150.}) {
        const auto early=clock_wave(ppm,2.3,.0083,0,48000,ppm<0?700.371:0.137);
        std::cout<<"clock early truth="<<ppm<<" measured="<<early.clock_error_ppm
            <<" uncertainty="<<early.clock_uncertainty_ppm<<" qualified="<<early.clock_qualified<<'\n';
        check(early.clock_qualified,"independent punctuation clock did not qualify within2.3seconds");
        check(std::abs(early.clock_error_ppm-ppm)<=early.clock_uncertainty_ppm,
            "early punctuation source clock escaped its reported interval");
        const auto s=clock_wave(ppm,6.3,.0083,0,48000,ppm<0?700.371:0.137);
        std::cout<<"clock strong truth="<<ppm<<" measured="<<s.clock_error_ppm
            <<" uncertainty="<<s.clock_uncertainty_ppm<<" qualified="<<s.clock_qualified<<'\n';
        check(s.clock_qualified,"strong punctuation source clock did not qualify after6.3seconds warmup");
        check(std::abs(s.clock_error_ppm-ppm)<=s.clock_uncertainty_ppm,"punctuation source clock escaped its interval");
        check(s.clock_uncertainty_ppm<20,"strong punctuation source clock did not reach20ppm");
        check(s.clock_first_sample<=s.clock_sample && s.clock_sample==s.clock_last_sample &&
            s.clock_last_sample<s.captured_end_sample,"clock proof manufactured sample/support provenance");
    }
    const auto moderate=clock_wave(150,8,.083,0,48000,.317);
    std::cout<<"clock58dBHz truth=150 measured="<<moderate.clock_error_ppm
        <<" uncertainty="<<moderate.clock_uncertainty_ppm<<" qualified="<<moderate.clock_qualified<<'\n';
    check(moderate.clock_qualified && moderate.clock_uncertainty_ppm<40 &&
        std::abs(moderate.clock_error_ppm-150)<=moderate.clock_uncertainty_ppm,"moderate helper failed useful conservative clock precision");
    const auto missing=clock_wave(-150,7.7,.0083,3,48960,3000.123);
    check(missing.clock_qualified && std::abs(missing.clock_error_ppm+150)<=missing.clock_uncertainty_ppm,
        "missing punctuation selected the wrong integer dwell alias");
    std::cout<<"clock missing truth=-150 measured="<<missing.clock_error_ppm<<" uncertainty="<<missing.clock_uncertainty_ppm<<" qualified="<<missing.clock_qualified<<'\n';
    const auto half_period=clock_wave(150,8,.0083,4);
    check(!half_period.clock_qualified,"intervening200ms reversals were discarded to fake a400ms clock");
    const auto wrong_period=clock_wave(150,8,.0083,5);
    check(!wrong_period.clock_qualified,"wrong410ms cadence impersonated the400ms source clock");
    const auto fake=clock_wave(150,8,.0083,1);
    check(!fake.clock_qualified,"instant phase jumps impersonated the known smooth punctuation");
    const auto fades=clock_wave(150,8,.0083,2);
    check(!fades.clock_qualified,"ordinary ramp/fade or metadata impersonated punctuation");
    const auto weak=clock_wave(150,8,1.87);
    check(!weak.clock_qualified,"weak noisy tone invented a precise punctuation clock");
    const auto large_origin=clock_wave(150,8,.0083,0,48000,0,false,UINT64_MAX-10ULL*48000);
    check(large_origin.clock_qualified && std::abs(large_origin.clock_error_ppm-150)<=large_origin.clock_uncertainty_ppm &&
        large_origin.clock_sample<large_origin.captured_end_sample,"large integer capture origin corrupted fractional clock provenance");
    const auto lost=clock_wave(150,5,.0083,0,48000,0,true);
    check(!lost.clock_qualified,"punctuation clock qualification survived helper loss");
}
void out_of_range_or_large_evolving_shift_declines() {
    const helper::ToneTrackerConfig c{48000,2062.5,20,0,.1};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    auto s=run(tracker,source,c.sample_rate,0,5,2065,0,.6,.02);
    check(s.qualified,"stationary in-range tone failed before shift control");
    // A newly evolving 10Hz/s offset exceeds the explicit following policy.
    s=run(tracker,source,c.sample_rate,s.captured_end_sample,2,2065-50,10,.6,.02);
    check(!s.qualified,"large changing offset received oscillator-following authority");
}
}
void phase_segment_covariance_and_precision_prediction() {
    using namespace datapump::helper;

 for(const auto m:{4u,8u,16u})for(const bool hetero:{false,true}) {
  std::array<precision_detail::Edge,16> e{};std::array<double,17> phase{},v{};
  constexpr double dt=.02;for(unsigned j=0;j<=m;++j){phase[j]=.031*std::sin(j*1.7)+.4*j*dt;v[j]=hetero?.02+.003*j:.02;}
  for(unsigned j=0;j<m;++j)e[j]={(phase[j+1]-phase[j])/dt,v[j]/(dt*dt),v[j+1]/(dt*dt)};
  precision_detail::Segment s;check(precision_detail::make_segment(std::span(e).first(m),dt,0,0,(m+1)*960,s),"kernel rejected valid edge chain");
  double mp=0;for(unsigned j=0;j<=m;++j)mp+=phase[j];mp/=m+1;double tt=0,tp=0,thermal=0;
  for(unsigned j=0;j<=m;++j){const auto u=(j-.5*m)*dt;tt+=u*u;tp+=u*(phase[j]-mp);}
  for(unsigned j=0;j<=m;++j){const auto u=(j-.5*m)*dt;thermal+=v[j]*(u/tt)*(u/tt);}
  check(std::abs(s.hz-tp/tt)<1e-12,"edge weights differ from independent phase OLS");
  check(s.variance+1e-12>=thermal,"kernel dropped dense endpoint covariance");
  auto bad=e;bad[1].first_variance+=.1;check(!precision_detail::make_segment(std::span(bad).first(m),dt,0,0,100,s),"inconsistent shared endpoint accepted");
 }
 for(const bool curved:{false,true}) {
  std::array<precision_detail::Segment,64> records{};constexpr unsigned rate=48000;constexpr std::uint64_t now=40ULL*rate-1;
  for(std::size_t i=0;i<records.size();++i){auto& e=records[i];e.first=(4ULL*rate)+i*rate/2;e.end=e.first+rate/10;
   const auto t=(double(e.first)+.5*(e.end-e.first-1)-now)/rate;e.hz=100+.08*t+(curved?.002*t*t:0);e.variance=.0001*(1+i%3);}
  const auto e=precision_detail::pool(records,nullptr,now,rate,0);
  check(e.valid && std::abs(e.hz-100)<=e.uncertainty,"two-scale geometry failed exact linear/quadratic current truth");
  auto bad=records;bad[2].first=bad[1].end-1;check(!precision_detail::pool(bad,nullptr,now,rate,0).valid,"overlapping phase support pooled as independent");
 }
 TonePrecisionRequest r;r.sample_rate=48000;r.straight_cn0_db_hz=30.020599913279625;r.phase_noise_radians_per_sqrt_second=.008770170951;
 const auto predicted=predict_tone_precision(r);std::cout<<"expected30 supported="<<predicted.supported<<" total="<<predicted.total_lead_seconds<<" extra="<<predicted.extra_lead_seconds<<" U="<<predicted.frequency_half_width_hz<<" usable="<<predicted.usable_tone_seconds<<" segments="<<predicted.expected_segments<<" reached="<<predicted.target_reached<<" capped="<<predicted.extension_capped<<'\n';
 check(predicted.supported && predicted.extra_lead_seconds<=36 && predicted.extra_lead_seconds>=0,"prediction lost cap");
 r.max_extra_seconds=.3999999995;const auto fractional=predict_tone_precision(r);
 check(fractional.extra_lead_seconds<=r.max_extra_seconds,"fractional precision cap overshot");
 r.max_extra_seconds=36;r.baseline_lead_seconds=1e300;
 check(!predict_tone_precision(r).supported,"huge baseline entered unbounded forecast");
 r.baseline_lead_seconds=2;r.maximum_source_capture_ratio=1e-300;
 check(!predict_tone_precision(r).supported,"tiny source ratio entered unbounded forecast");
 r.maximum_source_capture_ratio=1;r.straight_cn0_db_hz=13;check(!predict_tone_precision(r).supported,"weak following gate forecast implied qualification");
 std::cout<<"pure precision checks passed\n";

}
void phase_anchor_precision_prediction() {
 using namespace datapump::helper;
 // The entire source calendar is unknown. Adjacent CLOSED intervals
 // share endpoints, leaving no uncovered phase between0and400ms.
 check(forecast_detail::source_phase_bins==40,"forecast lost bounded full-source-phase policy");
 double last_phase=0;
 for(unsigned bin=0;bin<forecast_detail::source_phase_bins;++bin) {
  const auto lo=.4*bin/forecast_detail::source_phase_bins;
  const auto hi=.4*(bin+1)/forecast_detail::source_phase_bins;
  check(lo==last_phase && hi>lo && hi-lo<=.0100000000000001,"source-phase intervals have a gap or wrong bound");last_phase=hi;
 }
 check(last_phase==.4,"source-phase intervals miss calendar tail");
 TonePrecisionRequest geometry;geometry.sample_rate=48000;
 check(forecast_detail::clean_window(9600,960,geometry,0,.01),"full-bin clean pre-punctuation support declined");
 check(!forecast_detail::clean_window(9600,960,geometry,.01,.02),"phase-bin envelope crossed punctuation but stayed clean");
 check(!forecast_detail::clean_window(0,960,geometry,0,.01),"phase-bin startup ramp counted as clean");
 check(!forecast_detail::clean_window(13440,960,geometry,0,.01),"paired return transition counted as clean");
 check(!forecast_detail::clean_window(16704,960,geometry,0,.01),"metadata-spanning phase interval counted as clean");
 auto no_metadata=geometry;no_metadata.metadata_symbols=0;
 check(forecast_detail::clean_window(17472,960,no_metadata,0,.01) &&
       !forecast_detail::clean_window(17472,960,geometry,0,.01),"metadata count did not constrain whole phase interval");
 check(!forecast_detail::clean_window(18624,960,no_metadata,0,.01),"phase interval crossing400ms wrap counted as clean");
 TonePrecisionRequest r;r.sample_rate=48000;r.straight_cn0_db_hz=30.020599913279625;
 r.phase_noise_radians_per_sqrt_second=.008770170951;
 const auto p=predict_tone_precision(r);
 check(p.supported && p.target_reached && p.frequency_half_width_hz<=r.target_half_width_hz,
       "dated-anchor forecast failed conditional30dBHz precision target");
 check(p.extra_lead_seconds>=0 && p.extra_lead_seconds<=36 && p.total_lead_seconds==r.baseline_lead_seconds+p.extra_lead_seconds,
       "phase precision forecast relabeled or exceeded baseline/extra lead");
 check(p.expected_segments>=5 && p.usable_tone_seconds>0 &&
       p.usable_tone_seconds<=p.total_lead_seconds/r.maximum_source_capture_ratio,
       "phase forecast invented captured anchor support");
 check(p.sustained_supported && p.sustained_at_seconds==r.baseline_lead_seconds+36 &&
       p.sustained_frequency_half_width_hz>0 && p.total_lead_seconds<=p.sustained_at_seconds,
       "sustained forecast missing or relabeled selected TX lead");
 auto easier=r;easier.target_half_width_hz=.2;
 const auto e=predict_tone_precision(easier);
 check(e.total_lead_seconds<=p.total_lead_seconds && e.sustained_supported &&
       e.sustained_at_seconds==p.sustained_at_seconds &&
       e.sustained_frequency_half_width_hz==p.sustained_frequency_half_width_hz,
       "selected initial target changed sustained covariance/aperture");
 auto final_aperture=r;final_aperture.baseline_lead_seconds=p.sustained_at_seconds;final_aperture.max_extra_seconds=0;
 const auto final=predict_tone_precision(final_aperture);
 check(final.supported && final.extra_lead_seconds==0 &&
       final.frequency_half_width_hz==p.sustained_frequency_half_width_hz,
       "sustained field differs from same dated kernel at the explicit final aperture");
 auto strong=r;strong.straight_cn0_db_hz=60;
 const auto s=predict_tone_precision(strong);
 check(s.supported && s.target_reached && s.extra_lead_seconds<=p.extra_lead_seconds,
       "stronger conditional phase observations required more lead");
 auto short_lead=r;short_lead.baseline_lead_seconds=.1;short_lead.max_extra_seconds=0;
 const auto cold=predict_tone_precision(short_lead);
 check(!cold.supported && cold.total_lead_seconds==.1 && cold.extra_lead_seconds==0,
       "phase precision forecast manufactured acquisition before two fresh seeds");
 auto fractional=r;fractional.max_extra_seconds=.3999999995;
 const auto f=predict_tone_precision(fractional);
 check(f.extra_lead_seconds<=fractional.max_extra_seconds && f.total_lead_seconds==fractional.baseline_lead_seconds+f.extra_lead_seconds,
       "phase precision fractional extra cap overshot or relabeled baseline");
 auto alias=r;alias.sample_rate=96000;
 check(!predict_tone_precision(alias).supported,"unresolved original coarse-seed cycle alias forecast as precision");
 auto weak=r;weak.straight_cn0_db_hz=13;
 const auto w=predict_tone_precision(weak);
 check(!w.supported && w.extension_capped && !w.target_reached && w.frequency_half_width_hz==0 &&
       !w.sustained_supported && w.sustained_frequency_half_width_hz==0 && w.sustained_at_seconds==38,
       "weak software decline created initial or sustained phase precision");
 // Independent dense frequency/phase-endpoint OLS variance confirms that the
 // forecast's cycle guide is actual expected FLL covariance, not a magic U.
 forecast_detail::CleanFll history;constexpr unsigned rate=48000,block=960;
 constexpr double phase_variance=.0375;constexpr std::uint64_t origin=8192;
 for(unsigned i=0;i<32;++i)history.append(origin+(i+1)*block,origin+(i+2)*block-1,rate);
 const auto now=origin+33*block-1;
 const auto actual=history.uncertainty(now,rate,block,phase_variance,0);
 double mt=0;for(unsigned i=0;i<32;++i)mt+=(static_cast<double>(origin+(i+1)*block)-now)/rate;mt/=32;
 double tt=0;for(unsigned i=0;i<32;++i){const auto t=(static_cast<double>(origin+(i+1)*block)-now)/rate-mt;tt+=t*t;}
 std::array<double,33> phase_weights{};
 for(unsigned i=0;i<32;++i){const auto t=(static_cast<double>(origin+(i+1)*block)-now)/rate-mt;const auto q=(1./32-mt*t/tt)/(.02*precision_detail::tau);phase_weights[i]-=q;phase_weights[i+1]+=q;}
 double variance=0;for(const auto q:phase_weights)variance+=q*q*phase_variance;
 check(std::abs(actual-(3*std::sqrt(variance)+.005))<1e-12,
       "conditional FLL guide differs from independently assembled endpoint covariance");
 // Unknown complex amplitude and frequency are both present in the
 // shared200ms covariance. A centered uniform independent row matrix has
 // this independently derived Fisher diagonal.
 std::array<double,phase_detail::context_size> times{},noise{};
 constexpr double cn0=1000,sigma=.01,duration=.2;
 double ws=0,w2=0;
 for(unsigned j=0;j<960;++j){const auto w=.5-.5*std::cos(precision_detail::tau*j/959);ws+=w;w2+=w*w;}
 const auto bandwidth=48000*w2/(ws*ws),complex_noise=bandwidth/cn0;
 double sumt2=0;
 for(std::size_t j=0;j<times.size();++j){times[j]=(static_cast<double>(j)-.5*(times.size()-1))*.02;noise[j]=complex_noise;sumt2+=times[j]*times[j];}
 const auto joint=phase_detail::joint_covariance(times,noise,1,sigma,duration);
 check(joint.valid && std::abs(joint.center)<1e-15,"shared joint covariance lost actual centered date");
 check(std::abs(joint.thermal_phase_variance-complex_noise/(2*times.size()*precision_detail::tau*precision_detail::tau))<1e-15 &&
       std::abs(joint.thermal_frequency_variance-complex_noise/(2*sumt2*precision_detail::tau*precision_detail::tau))<1e-15,
       "joint forecast omitted complex-amplitude/frequency nuisance covariance");
 check(std::abs(joint.process_phase_variance-2*sigma*sigma*duration/(precision_detail::tau*precision_detail::tau))<1e-15,
       "joint forecast used20ms instead of full200ms Brownian support");
 double absolute_frequency_weight=0;for(const auto time:times)absolute_frequency_weight+=std::abs(time/sumt2);
 check(std::abs(joint.process_frequency_variance-sigma*sigma*duration*absolute_frequency_weight*absolute_frequency_weight/
       (precision_detail::tau*precision_detail::tau))<1e-15,
       "joint forecast lost actual-weight Minkowski process-frequency bound");
 std::cout<<"phase-anchor forecast total="<<p.total_lead_seconds<<" U="<<p.frequency_half_width_hz
          <<" anchors="<<p.expected_segments<<" support="<<p.usable_tone_seconds
          <<" sustained_at="<<p.sustained_at_seconds<<" sustained_U="<<p.sustained_frequency_half_width_hz<<'\n';
}
void full_settled_aperture_kernel_and_compatible_history() {
    constexpr unsigned rate=32000;constexpr double tau=2*std::numbers::pi;
    for(const auto origin:std::array<std::uint64_t,2>{0,1ULL<<60}) {
        helper::ToneTrackerConfig c{rate,2500,120,0,.5,true,true};
        helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c),origin);
        std::array<float,256> pcm{};helper::ToneTrackerSnapshot last;
        for(const auto bad_end:std::array<std::uint64_t,3>{origin+383,origin+384,origin+384+31*640+1}) {
            bool rejected=false;try{tracker.begin_pilot_segment(origin,2500,120,false,1,origin+384,true,bad_end);}
            catch(const Error&){rejected=true;}
            check(rejected && tracker.snapshot().captured_end_sample==origin,
                "invalid full-aperture end mutated tracker state or escaped the bounded row guard");
        }
        for(unsigned hop=0;hop<8;++hop) {
            const auto first=origin+static_cast<std::uint64_t>(hop)*12800;
            const auto scale=hop%4==0?1.:hop%4==1?2.:hop%4==2?-1.:-2.;
            tracker.begin_pilot_segment(first,2500,120,hop!=0,scale,first+384,false,first+12672);
            const auto reset=tracker.snapshot();
            check(!reset.phase_fit_qualified && !reset.frequency_observation.valid && !reset.frequency_continuous_phase,
                "known retune retained a prior dwell phase or raw frequency certificate");
            for(unsigned n=0;n<12800;n+=pcm.size()) {
                for(unsigned j=0;j<pcm.size();++j) {
                    const auto local=(n+j)/static_cast<double>(rate),t=hop*.4+local;
                    const auto cycles=2500*local+scale*(7.3*local+.1*(t*t-hop*.4*hop*.4));
                    pcm[j]=static_cast<float>(std::cos(tau*cycles+.37+.61*hop*hop));
                }
                tracker.push(pcm,first+n);
                if(n+pcm.size()<12672)check(!tracker.snapshot().frequency_observation.valid,
                    "full aperture exported a future or incomplete fit");
            }
            last=tracker.snapshot();const auto& k=last.frequency_observation;
            check(k.valid && k.first_sample==first+384 && k.end_sample==first+12672 &&
                k.state_endpoint_sample==first+12671 && k.end_sample<=last.captured_end_sample,
                "current full-aperture certificate lost its actual causal support");
            check(k.independent_variance_hz2>0 && k.residual_variance_hz2>=0 &&
                std::abs(k.mass-1)<1e-10 && std::abs(k.k_prime_at_end-1)<1e-10 &&
                std::abs(k.k_at_end_seconds-k.mu_seconds)<1e-10 && k.integral_k_squared_seconds3>=0,
                "full-aperture kernel or independent covariance is invalid");
            const auto lag=static_cast<long double>(k.state_endpoint_sample-k.mean_sample)-k.mean_sample_fraction;
            check(std::abs(static_cast<double>(lag)/rate-k.mu_seconds)<1e-12,
                "full frequency mean date lost its fractional kernel lag");
            const auto mean_t=(static_cast<long double>(k.mean_sample-origin)+k.mean_sample_fraction)/rate;
            const auto truth=2500+scale*(7.3+.2*static_cast<double>(mean_t));
            check(std::abs(k.frequency_hz-truth)<1e-5,"full fit differs from chirp truth at its kernel mean");
            if(hop>=3) {
                const auto endpoint_t=static_cast<double>(last.sample-origin)/rate;
                check(last.qualified && !last.frequency_continuous_phase && last.first_sample<first &&
                    std::abs(last.frequency_hz-(2500+scale*(7.3+.2*endpoint_t)))<1e-5,
                    "compatible full-aperture retunes discarded or misdated mapped frequency history");
            }
        }
        const auto boundary=origin+8ULL*12800;
        tracker.begin_pilot_segment(boundary,2500,120,false,1,boundary+384,true,boundary+12672);
        for(unsigned n=0;n<12800;n+=pcm.size()) {
            for(unsigned j=0;j<pcm.size();++j){const auto t=(n+j)/static_cast<double>(rate);
                pcm[j]=static_cast<float>(std::cos(tau*2507.3*t+.5*std::sin(tau*t/.2)));}
            tracker.push(pcm,boundary+n);
        }
        check(!tracker.snapshot().frequency_observation.valid,
            "a rejected full fit exported another candidate's frequency kernel");
    }
}

int main() {
    try {
        full_settled_aperture_kernel_and_compatible_history();segmented_pilot_dates_phase_and_retunes();segmented_frequency_query_dates_and_covariance();segmented_frequency_fallback_is_disjoint_and_frequency_only();phase_segment_covariance_and_precision_prediction();phase_anchor_precision_prediction();phase_kernel_covariance_joint_fit_and_provenance();strong_wide_tone_and_drift();representative_hf_noise_and_eme_level();narrow_range_and_noise();actual_helper_cold_capture_before_payload();sparse_segments_keep_recent_aperture();stable_full_capacity_keeps_moving();legacy_paired_modulation_rolling_fifo();shared_endpoint_precision_of_stable_tone();legacy_paired_clock_survives_ordinary_payload();actual_two_tone_pilot_keeps_clock_unresolved();off_cadence_feature_does_not_poison_established_clock();independent_punctuation_clock();out_of_range_or_large_evolving_shift_declines();
        std::cout<<"helper realtime tone tracker checks passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
