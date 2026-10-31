#include "datapump/helper_complete_conditioner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using C=datapump::helper::HelperCompleteConditioner;
using B=datapump::helper::HelperDelayedBuffer;
using S=C::Sample;
constexpr long double tau=2*std::numbers::pi_v<long double>;
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
long double wrapped(long double x){return std::remainder(x,tau);}
std::uint64_t mono(long double sample,long double period){return 1'000'000'000ULL+static_cast<std::uint64_t>(std::llround(sample*period));}
C::CaptureBlock block(std::uint64_t first,std::size_t count,long double period=1'000'000.L){
    C::CaptureBlock b;b.first_sample=first;b.sample_count=count;b.sample_period_nanoseconds=period;
    b.capture_monotonic_nanoseconds=mono(first,period);
    b.capture_system_nanoseconds=1'700'000'000'000'000'000LL+static_cast<std::int64_t>(std::llround(first*period));
    b.capture_uncertainty_nanoseconds=2;b.capture_monotonic_uncertainty_nanoseconds=1;
    b.sample_period_uncertainty_nanoseconds=1e-5L;b.capture_clock_epoch=1;return b;
}
C::CorrectionEpoch epoch(std::uint64_t first,std::uint64_t end,std::uint32_t fs,double a,double b,double fp,double phase){
    C::CorrectionEpoch e;e.first_sample=first;e.end_sample=end;e.schedule_slot=first/(end-first);
    e.anchor_input_sample=first+.63L*(end-first);e.audio_scale=a;e.common_frequency_hz=b;
    e.pilot_frequency_hz=fp;e.measured_pilot_phase_radians=static_cast<double>(wrapped(tau*(fp*a+b)*e.anchor_input_sample/fs+phase));
    e.phase_valid=e.clock_valid=e.model_qualified=true;
    e.phase_support_first_sample=first+(end-first)/5;e.phase_support_end_sample=end-(end-first)/5;
    e.latest_helper_observation_end_sample=end;
    e.latest_helper_observation_monotonic_nanoseconds=mono(end,1e9L/fs);
    e.source_time_valid=true;e.source_seconds_at_anchor=e.anchor_input_sample*a/fs;
    e.source_half_width_seconds=1e-5L;e.reference_input_sample=17.25L;return e;
}
void phase_and_timestamp(double delay){
    constexpr std::uint32_t fs=2400;constexpr std::uint64_t dwell=960,total=12*dwell;
    constexpr double a=1.0001,b=7.3,fp=300,probe=175;
    constexpr long double capture_period=1e9L/(fs*1.000037L);
    const std::array<double,8> phases{.4,-2.5,1.8,-.9,2.9,.01,-1.7,.7};
    C c({fs,12000,128,static_cast<std::uint64_t>(std::llround(delay*1e9)),1'000'000});
    std::array<C::CorrectionEpoch,12> plans;
    for(std::size_t i=0;i<plans.size();++i){plans[i]=epoch(i*dwell,(i+1)*dwell,fs,a,b,fp,phases[i%phases.size()]);
        plans[i].latest_helper_observation_monotonic_nanoseconds=mono((i+1)*dwell,capture_period);}
    std::uint64_t captured=0,release_call_ns=0;std::vector<C::Event> output;std::array<C::CorrectionEpoch,4> supplied{};
    const auto provider=[&](const C::ReleasedBlock& released){
        const auto first=released.capture.first_sample/dwell;
        const auto last=(released.capture.first_sample+released.capture.sample_count-1)/dwell;
        require(last-first<supplied.size(),"test epoch capacity");
        for(auto i=first;i<=last;++i){supplied[i-first]=plans[i];require(supplied[i-first].latest_helper_observation_end_sample<=captured,"phase observation used before capture");}
        return std::span<const C::CorrectionEpoch>(supplied.data(),static_cast<std::size_t>(last-first+1));
    };
    const auto sink=[&](const C::Event& e){require(e.release_monotonic_nanoseconds==release_call_ns,"release timestamp backdated at endpoint");output.push_back(e);};
    const std::array<std::size_t,6> chunks{137,701,53,411,19,1031};std::size_t k=0;
    bool before_eof=false;
    while(captured<total){
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(chunks[k++%chunks.size()],total-captured));
        std::vector<S> raw(n);
        for(std::size_t j=0;j<n;++j){const auto idx=captured+j;
            raw[j]=static_cast<S>(std::polar(1.L,tau*(probe*a+b)*idx/fs+phases[(idx/dwell)%phases.size()]));}
        auto stamp=block(captured,n,capture_period);
        require(c.push(stamp,raw)==B::PushResult::accepted,"capture push failed");captured+=n;
        release_call_ns=mono(captured,capture_period);c.release(release_call_ns,release_call_ns,provider,sink);
        if(captured<total&&!output.empty())before_eof=true;
    }
    release_call_ns=mono(total,capture_period)+static_cast<std::uint64_t>((delay+.01)*1e9);
    c.release(release_call_ns,mono(total,capture_period),provider,sink,true);
    require(before_eof,"output deferred until EOF");require(c.queued_blocks()==0,"drain queue nonempty");
    require(c.captured_samples_processed()==total,"captured samples lost");
    require(output.front().source_input_sample==0&&output.back().source_input_sample==total-1,"capture endpoints omitted");
    long double max_phase=0;std::uint64_t outside=0,guarded=0;long double previous=-1;
    for(const auto& e:output){
        require(e.samples_present,"unexpected gap");require(e.source_input_sample>previous,"output order failure");previous=e.source_input_sample;
        require(e.capture_timestamp_valid,"original UTC unavailable");
        const auto expected=1'700'000'000'000'000'000.L+e.source_input_sample*capture_period;
        require(std::abs(e.capture_system_nanoseconds-expected)<=3,"buffer shifted capture UTC");
        require(e.release_monotonic_nanoseconds+1>=e.capture_monotonic_nanoseconds+delay*1e9,"early release");
        require(e.phase_applied&&e.clock_applied,"fresh correction not applied across complete dwell");
        const auto error=wrapped(std::arg(std::complex<long double>(e.sample))-tau*probe*e.relative_source_seconds);
        max_phase=std::max(max_phase,std::abs(error));
        if(e.well_supported)++guarded;else ++outside;
        require(e.reference_input_sample==17.25L&&!e.reference_mismatch,"permanent reference changed");
        require(e.transmitter_time_valid&&std::abs(e.clock_model_discrepancy_seconds)<1e-9L,"source clock gauge drifted");
        // A downstream pattern lookup uses original capture UTC, never release
        // wall time. Their slots differ routinely with these intentional delays.
        const auto input_slot=static_cast<unsigned>(std::floor(e.source_input_sample*capture_period/400'000'000.L));
        const auto consumer_slot=static_cast<unsigned>(std::floor((e.capture_system_nanoseconds-1'700'000'000'000'000'000.L)/400'000'000.L));
        require(input_slot==consumer_slot,"capture-time pattern lookup mismatch");
    }
    require(guarded&&outside,"support mask does not distinguish extrapolation");
    require(max_phase<.002L,"wrong hop phase, extrapolation sign/date, or time stretch");
    require(c.reference_preserved(),"reference not preserved");
    std::cout<<"delay="<<delay<<" input="<<total<<" output="<<output.size()<<" max_phase_rad="<<static_cast<double>(max_phase)
             <<" extrapolated="<<outside<<" bytes="<<c.allocated_bytes()<<'\n';
}
void gaps_and_reference(){
    constexpr std::uint32_t fs=1000;C c({fs,2000,16,0,1'000'000});
    std::vector<C::Event> output;std::array<C::CorrectionEpoch,1> plans;
    const auto provider=[&](const C::ReleasedBlock& r){
        plans[0]=epoch(r.capture.first_sample,r.capture.first_sample+r.capture.sample_count,fs,1,7,100,.7);
        if(r.capture.first_sample>=500)plans[0].reference_input_sample=99;
        return std::span<const C::CorrectionEpoch>(plans);
    };
    const auto sink=[&](const C::Event& e){output.push_back(e);};
    const auto present=[&](std::uint64_t first,std::size_t count){std::vector<S> x(count);
        for(std::size_t i=0;i<count;++i)x[i]=static_cast<S>(std::polar(1.L,tau*7*(first+i)/fs+.7));
        require(c.push(block(first,count),x)==B::PushResult::accepted,"gap present push failed");
        c.release(mono(first+count,1e6L)+100,mono(first+count,1e6L),provider,sink);
    };
    present(0,200);
    auto missing=block(200,50);missing.samples_present=false;
    require(c.push(missing,{})==B::PushResult::accepted,"explicit gap push failed");
    c.release(mono(250,1e6L)+100,mono(250,1e6L),provider,sink);
    present(250,200);present(500,200); // another fifty implicit missing indices
    c.release(mono(700,1e6L)+100,mono(700,1e6L),provider,sink,true);
    require(c.captured_samples_processed()==600&&c.missing_samples()==100,"gap accounting failure");
    std::size_t gaps=0,payload=0;long double prior=-1;
    for(const auto& e:output){if(!e.samples_present){++gaps;require(e.missing_sample_count==50,"wrong gap extent");continue;}
        ++payload;require(e.source_input_sample>prior,"gap concatenated or reversed samples");prior=e.source_input_sample;
        require(!(e.source_input_sample>=200&&e.source_input_sample<250)&&!(e.source_input_sample>=450&&e.source_input_sample<500),"invented gap payload");
        require(std::abs(e.relative_source_seconds-e.source_input_sample/fs)<1e-12L,"source clock concatenated across missing input");
        require(e.reference_input_sample==17.25L,"reference reset after gap");
        if(e.source_input_sample>=500)require(e.reference_mismatch&&!e.transmitter_time_valid&&!e.well_supported,"conflicting reference admitted");
    }
    require(payload==600&&gaps==2&&!c.reference_preserved(),"gap/reference final count");
}
void utc_discontinuity_and_bad_future(){
    constexpr std::uint32_t fs=1000;C c({fs,1000,16,0,1000});
    std::array<C::CorrectionEpoch,1> plan;std::vector<C::Event> output;
    const auto provider=[&](const C::ReleasedBlock& r){
        plan[0]=epoch(r.capture.first_sample,r.capture.first_sample+r.capture.sample_count,fs,1.0001,0,100,.2);
        return std::span<const C::CorrectionEpoch>(plan);
    };
    const auto sink=[&](const C::Event& e){output.push_back(e);};
    std::vector<S> raw(200,{1,0});auto a=block(0,200);auto b=block(200,200);
    b.capture_system_nanoseconds+=2'000'000'000LL; // same epoch, unannounced wall step
    require(c.push(a,raw)==B::PushResult::accepted,"UTC first push");
    c.release(mono(200,1e6L)+10,mono(200,1e6L),provider,sink);
    require(c.push(b,raw)==B::PushResult::accepted,"UTC second push");
    c.release(mono(400,1e6L)+10,mono(400,1e6L),provider,sink,true);
    bool cross=false,post=false;
    for(const auto& e:output){
        if(e.source_input_sample>199&&e.source_input_sample<200){cross=true;require(!e.capture_timestamp_valid&&e.capture_time_discontinuity,"interpolated UTC through wall jump");}
        if(e.source_input_sample>201){post=true;require(e.capture_timestamp_valid&&e.capture_system_nanoseconds>1'700'000'002'000'000'000.L,"post-step original UTC lost");}
    }
    require(cross&&post,"UTC cross-block fractional branch untested");
    C fallback({fs,1000,16,0,1000});output.clear();
    auto future=[&](const C::ReleasedBlock& r){plan[0]=epoch(r.capture.first_sample,r.capture.first_sample+r.capture.sample_count,fs,1,0,100,.2);
        plan[0].latest_helper_observation_monotonic_nanoseconds=mono(500,1e6L);return std::span<const C::CorrectionEpoch>(plan);};
    require(fallback.push(a,raw)==B::PushResult::accepted,"fallback push");
    fallback.release(mono(200,1e6L)+10,mono(200,1e6L),future,sink,true);
    require(output.size()==raw.size(),"invalid plan omitted payload");
    for(const auto& e:output)require(!e.phase_applied&&!e.clock_applied&&!e.well_supported&&e.sample==S(1,0),"future Helper plan affected output");
}

void unknown_phase_chunking(){
    constexpr std::uint32_t fs=2400;constexpr std::size_t total=2400;
    const auto run=[&](bool split){
        C c({fs,4800,64,0,1000});std::vector<C::Event> out;
        auto p=epoch(0,total,fs,1.0001,17.3,300,.7);p.phase_valid=false;p.model_qualified=false;
        p.latest_helper_observation_end_sample=0;p.latest_helper_observation_monotonic_nanoseconds=1;
        const auto provider=[&](const C::ReleasedBlock&){return std::span<const C::CorrectionEpoch>(&p,1);};
        const auto sink=[&](const C::Event& e){out.push_back(e);};
        std::size_t first=0;
        while(first<total){const std::size_t n=std::min(split?std::size_t{137}:total,total-first);std::vector<S> x(n);
            for(std::size_t i=0;i<n;++i)x[i]=static_cast<S>(std::polar(1.L,tau*(91*1.0001+17.3)*(first+i)/fs+.7));
            require(c.push(block(first,n,1e9L/fs),x)==B::PushResult::accepted,"unknown-phase push");
            first+=n;c.release(mono(first,1e9L/fs)+10,mono(first,1e9L/fs),provider,sink);
        }
        c.release(mono(total,1e9L/fs)+1000,mono(total,1e9L/fs),provider,sink,true);return out;
    };
    const auto whole=run(false),parts=run(true);require(whole.size()==parts.size(),"chunking changed output count");
    for(std::size_t i=0;i<whole.size();++i){require(std::abs(whole[i].sample-parts[i].sample)<2e-6F,"missing-phase frequency fallback reset at chunk boundary");
        require(std::abs(whole[i].relative_source_seconds-parts[i].relative_source_seconds)<1e-15L,"chunking changed retained clock");
        require(!parts[i].phase_applied,"unknown phase presented as measured");}
}


void posterior_clock_recovery(){
    constexpr std::uint32_t fs=2400;constexpr std::uint64_t dwell=960,total=8*dwell;
    constexpr double true_audio=1.00005,wrong_audio=1.0004,fp=300,probe=175,b=7.3;
    C c({fs,12000,16,400'000'000,1000});std::array<C::CorrectionEpoch,8> p;
    for(std::size_t i=0;i<p.size();++i){
        p[i]=epoch(i*dwell,(i+1)*dwell,fs,true_audio,b,fp,.37*static_cast<double>(i));
        p[i].reference_input_sample=0;
        if(i<2){p[i].audio_scale=wrong_audio;p[i].source_seconds_at_anchor=p[i].anchor_input_sample*wrong_audio/fs;p[i].model_qualified=false;}
        p[i].source_half_width_seconds=1e-6L;
    }
    std::vector<C::Event> out;std::array<C::CorrectionEpoch,1> supplied;
    const auto provider=[&](const C::ReleasedBlock& r){supplied[0]=p[r.capture.first_sample/dwell];return std::span<const C::CorrectionEpoch>(supplied);};
    const auto sink=[&](const C::Event& e){out.push_back(e);};
    for(std::size_t i=0;i<p.size();++i){
        if(i==3){auto gap=block(i*dwell,dwell,1e9L/fs);gap.samples_present=false;
            require(c.push(gap,{})==B::PushResult::accepted,"posterior recovery gap push");
            c.release(mono((i+1)*dwell,1e9L/fs),mono((i+1)*dwell,1e9L/fs),provider,sink);continue;}
        std::vector<S> raw(dwell);
        for(std::size_t n=0;n<raw.size();++n)raw[n]=static_cast<S>(std::polar(1.L,tau*(probe*true_audio+b)*(i*dwell+n)/fs+.37L*i));
        require(c.push(block(i*dwell,dwell,1e9L/fs),raw)==B::PushResult::accepted,"posterior recovery push");
        c.release(mono((i+1)*dwell,1e9L/fs),mono((i+1)*dwell,1e9L/fs),provider,sink);
    }
    c.release(mono(total,1e9L/fs)+500'000'000,mono(total,1e9L/fs),provider,sink,true);
    bool saw_servo=false,saw_recovered=false;long double late_phase=0,late_clock=0;
    for(const auto& e:out){require(e.reference_input_sample==0&&!e.reference_mismatch,"posterior selected a new reference");
        if(!e.samples_present)continue;
        const auto true_clock=e.source_input_sample*true_audio/fs;
        if(e.source_input_sample>2*dwell&&e.source_input_sample<3*dwell){
            if(std::abs(e.clock_model_discrepancy_seconds)>1e-6L){saw_servo=true;require(!e.well_supported,"clock servo hid real model discrepancy");}
        }
        if(e.source_input_sample>=4*dwell){saw_recovered=true;late_clock=std::max(late_clock,std::abs(e.relative_source_seconds-true_clock));
            const auto phase=wrapped(std::arg(std::complex<long double>(e.sample))-tau*probe*e.relative_source_seconds);
            late_phase=std::max(late_phase,std::abs(phase));}
    }
    require(saw_servo&&saw_recovered,"posterior servo transition not tested");
    require(late_clock<1e-10L&&late_phase<.002L,"posterior did not correct accumulated clock and phase error");
    std::cout<<"posterior_clock_recovery_late_max_seconds="<<static_cast<double>(late_clock)<<" late_phase_rad="<<static_cast<double>(late_phase)<<'\n';
}

void band_response(){
    constexpr std::uint32_t fs=24000;constexpr std::uint64_t total=24000;constexpr double a=1.0001;
    for(const double frequency:{1780.,10800.}){
        C c({fs,48000,8,0,1000});std::vector<S> raw(total);std::array<C::CorrectionEpoch,1> p{epoch(0,total,fs,a,0,2400,.3)};
        for(std::size_t i=0;i<raw.size();++i)raw[i]=static_cast<S>(std::polar(1.L,tau*frequency*a*i/fs+.3));
        require(c.push(block(0,total,1e9L/fs),raw)==B::PushResult::accepted,"band push");
        long double error2=0,maximum=0;std::size_t count=0;
        const auto provider=[&](const C::ReleasedBlock&){return std::span<const C::CorrectionEpoch>(p);};
        c.release(mono(total,1e9L/fs)+10,mono(total,1e9L/fs),provider,[&](const C::Event& e){
            const auto truth=std::polar(1.L,tau*frequency*e.relative_source_seconds);const auto error=std::abs(std::complex<long double>(e.sample)-truth);
            error2+=error*error;maximum=std::max(maximum,error);++count;},true);
        const auto evm=std::sqrt(error2/count);
        if(frequency<2000)require(evm<.028L&&maximum<.028L,"linear interpolator declared probe-band error");
        else require(maximum>.5L,"Nyquist response limitation not exercised");
        std::cout<<"linear_interpolation_tone_hz="<<frequency<<" evm="<<static_cast<double>(evm)<<" max="<<static_cast<double>(maximum)<<'\n';
    }
}
}
int main(){try{
    for(double delay:{.4,1.,2.,3.})phase_and_timestamp(delay);
    gaps_and_reference();utc_discontinuity_and_bad_future();unknown_phase_chunking();posterior_clock_recovery();band_response();
    std::cout<<"Helper complete conditioner checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
