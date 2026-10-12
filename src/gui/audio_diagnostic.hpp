#pragma once
#include "datapump/audio.hpp"
#include "datapump/resampler.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <numbers>
#include <numeric>
#include <ostream>
#include <thread>

// Developer instrumentation only. Public, quiet probes; no modem/key/route changes.
namespace datapump::gui::audio_diagnostic {
inline constexpr unsigned capture_rate=8000, playback_rate=48000;
inline constexpr double amplitude=.005, pulse_seconds=.08, search_seconds=.65;
inline double probe(double seconds,unsigned ordinal) {
    if(seconds<0 || seconds>=pulse_seconds)return 0;
    const auto f0=650.+double((ordinal*137)%450),f1=2850.-double((ordinal*71)%350);
    const auto phase=2*std::numbers::pi*(f0*seconds+(f1-f0)*seconds*seconds/(2*pulse_seconds));
    return amplitude*.5*(1-std::cos(2*std::numbers::pi*seconds/pulse_seconds))*std::sin(phase);
}
inline std::vector<unsigned> ordinals(double seconds) {
    if(!std::isfinite(seconds)||seconds<5||seconds>300)throw Error("audio timing test duration must be 5..300 seconds");
    std::vector<unsigned> result;
    for(unsigned k=1;k+1<seconds;++k)result.push_back(k);
    return result;
}
inline void fft(std::vector<std::complex<double>>& x,bool inverse) {
    const auto n=x.size();
    for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
    for(std::size_t len=2;len<=n;len*=2) {
        const auto w=std::polar(1.,(inverse?2:-2)*std::numbers::pi/double(len));
        for(std::size_t i=0;i<n;i+=len){std::complex<double> v{1,0};for(std::size_t j=0;j<len/2;++j,v*=w){
            const auto a=x[i+j],b=x[i+j+len/2]*v;x[i+j]=a+b;x[i+j+len/2]=a-b;
        }}
    }
    if(inverse)for(auto& v:x)v/=double(n);
}
struct Match {unsigned ordinal=0;double capture_sample=0,correlation=0,peak_ratio=0,delay_seconds=0;};
struct Analysis {
    std::vector<Match> matches;
    std::size_t attempted=0;
    double median_delay_seconds=0,delay_min_seconds=0,delay_max_seconds=0,drift_ppm=0,residual_rms_seconds=0;
    bool sufficient=false;
};
// Immutable nominal sample maps expose relative drift; fresh driver predictions
// are reported separately, never used to erase a physical-return delay trend.
inline Analysis analyze(std::span<const float> pcm,double seconds,long double capture_origin,
                        long double scheduled_playback_origin,std::stop_token stop={}) {
    Analysis out;const auto ids=ordinals(seconds);out.attempted=ids.size();
    if(!std::isfinite(capture_origin)||!std::isfinite(scheduled_playback_origin))return out;
    constexpr std::size_t m=std::size_t(pulse_seconds*capture_rate);
    for(const auto id:ids) {
        if(stop.stop_requested())throw Error("audio timing analysis cancelled");
        const auto expected=static_cast<double>((scheduled_playback_origin+id-capture_origin)*capture_rate);
        const auto lower=std::max(0.,std::floor(expected-search_seconds*capture_rate));
        const auto upper=std::min(double(pcm.size()),std::ceil(expected+search_seconds*capture_rate+m));
        if(!(upper-lower>=m))continue;
        const auto first=static_cast<std::size_t>(lower),count=static_cast<std::size_t>(upper-lower);
        std::size_t n=1;while(n<count+m)n*=2;
        std::vector<std::complex<double>> a(n),b(n);double energy=0;
        for(std::size_t i=0;i<count;++i){if(!std::isfinite(pcm[first+i]))throw Error("nonfinite diagnostic PCM");a[i]=pcm[first+i];}
        for(std::size_t i=0;i<m;++i){const auto v=probe(double(i)/capture_rate,id);b[m-1-i]=v;energy+=v*v;}
        fft(a,false);fft(b,false);for(std::size_t i=0;i<n;++i)a[i]*=b[i];fft(a,true);
        std::size_t best=0;double peak=0,second=0;
        for(std::size_t k=0;k+m<=count;++k)if(const auto v=std::abs(a[k+m-1].real());v>peak){peak=v;best=k;}
        for(std::size_t k=0;k+m<=count;++k)if(std::abs(double(k)-double(best))>16)second=std::max(second,std::abs(a[k+m-1].real()));
        double observed_energy=0;for(std::size_t i=0;i<m;++i)observed_energy+=double(pcm[first+best+i])*pcm[first+best+i];
        const auto rho=observed_energy>0?peak/std::sqrt(energy*observed_energy):0;
        const auto ratio=second>0?peak/second:0;
        // Heuristic return-quality checks, NOT a modem false-accept criterion.
        if(rho<.35 || ratio<1.4 || best==0 || best+m>=count)continue;
        // Fit the actual analytic chirp instead of a parabola whose fractional
        // bias depends on chirp frequency and can imitate slow sample drift.
        const auto objective=[&](double fraction) {
            double dot=0,e=0;for(std::size_t i=0;i<m;++i){const auto v=probe((double(i)-fraction)/capture_rate,id);dot+=pcm[first+best+i]*v;e+=v*v;}
            return e>0?dot*dot/e:0;
        };
        double lo=-.5,hi=.5;constexpr double golden=.6180339887498948482;
        double x1=hi-golden*(hi-lo),x2=lo+golden*(hi-lo),v1=objective(x1),v2=objective(x2);
        for(unsigned iteration=0;iteration<20;++iteration) {
            if(v1<v2){lo=x1;x1=x2;v1=v2;x2=lo+golden*(hi-lo);v2=objective(x2);}
            else{hi=x2;x2=x1;v2=v1;x1=hi-golden*(hi-lo);v1=objective(x1);}
        }
        const auto fraction=(lo+hi)/2;
        const auto sample=double(first+best)+fraction;
        out.matches.push_back({id,sample,rho,ratio,double(capture_origin+sample/capture_rate-scheduled_playback_origin-id)});
    }
    out.sufficient=out.matches.size()>=3 && out.matches.back().ordinal-out.matches.front().ordinal>=2;
    if(out.matches.empty())return out;
    std::vector<double> delays;double mx=0,my=0;
    for(const auto& m:out.matches){delays.push_back(m.delay_seconds);mx+=m.ordinal;my+=m.delay_seconds;}
    mx/=out.matches.size();my/=out.matches.size();std::sort(delays.begin(),delays.end());
    out.median_delay_seconds=delays[delays.size()/2];out.delay_min_seconds=delays.front();out.delay_max_seconds=delays.back();
    double xx=0,xy=0;for(const auto& m:out.matches){xx+=(m.ordinal-mx)*(m.ordinal-mx);xy+=(m.ordinal-mx)*(m.delay_seconds-my);}
    const auto slope=xx>0?xy/xx:0;out.drift_ppm=slope*1e6;
    for(const auto& m:out.matches){const auto e=m.delay_seconds-my-slope*(m.ordinal-mx);out.residual_rms_seconds+=e*e;}
    out.residual_rms_seconds=std::sqrt(out.residual_rms_seconds/out.matches.size());return out;
}
inline void quoted(std::ostream& out,const std::string& text) {
    out<<'"';for(const unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c>=32&&c<127)out<<char(c);else out<<'?';}out<<'"';
}
inline int run_audio_timing_test(double seconds,const std::string& device,std::ostream& out,std::stop_token cancel={}) {
    using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
    const auto ids=ordinals(seconds);const auto start=Clock::now();const auto deadline=start+std::chrono::duration<double>(seconds+12);
    const auto wall=[] {return std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();};
    const auto host=[&]{return std::chrono::duration<double>(Clock::now()-start).count();};
    struct Anchor {std::size_t first=0,count=0;double delivered=0;std::optional<audio::TimePrediction> timing;};
    std::vector<float> pcm;pcm.reserve(std::size_t((seconds+10)*capture_rate));
    const auto anchor_limit=std::size_t((seconds+12)*200);
    std::vector<Anchor> anchors;anchors.reserve(anchor_limit);
    std::vector<double> requested(ids.size(),-1);
    audio::StreamFormat capture_format,playback_format;
    std::vector<audio::TimingStatus> capture_status,playback_status;
    std::string capture_error,playback_error,startup_error,analysis_error;
    std::optional<audio::TimePrediction> pending;
    long double capture_origin=std::numeric_limits<long double>::quiet_NaN(),source_origin=capture_origin;
    bool scheduled=false;std::atomic<bool> ready=false,tx_done=false,rx_done=false;
    std::atomic<unsigned> input_gaps=0,output_gaps=0;std::stop_source stop;
    std::mutex mutex;std::condition_variable changed;
    std::stop_callback external(cancel,[&]{stop.request_stop();changed.notify_all();});
    auto options=[] {
        audio::Options o;o.follow_system_clock=true;o.maximum_utc_error_seconds=1;
        o.maximum_timing_rate_correction=0;o.timing_stop_duration_seconds=.2;o.allow_timing_fallback=true;return o;
    };
    // A jthread's own stop also reaches audio, including partial construction unwind.
    std::jthread receiver([&](std::stop_token local) {
        std::stop_callback bridge(local,[&]{stop.request_stop();});
        try {
            // Negotiate the ordinary 48k device route, then filter/downsample
            // only the diagnostic record. Asking a server for an8k device can
            // select a different resampling/latency policy from modem playback.
            audio::Resampler converter(playback_rate,capture_rate);
            std::array<float,2048> converted;std::uint64_t input_count=0;
            auto o=options();
            // Measure the returned probe without requiring a valid ADC UTC
            // estimator. Some server pointers stay frozen while startup PCM is
            // drained. The full-GUI --verbose path separately diagnoses those
            // native timestamps; it must not silently qualify them here.
            o.follow_system_clock=false;
            capture_status.push_back({false,0,INFINITY,"Ordinary input observation; ADC UTC/analog delay uncalibrated"});
            o.timing_status=[&](const auto& s){if(capture_status.size()>=32)throw Error("diagnostic status bound exhausted");capture_status.push_back(s);};
            o.capture_timing=[&](const auto& t){pending=t;};
            o.capture_discontinuity=[&]{++input_gaps;throw Error("diagnostic capture continuity lost");};
            audio::capture(playback_rate,device,[&](std::span<const float> chunk) {
                if(chunk.empty())return !stop.stop_requested();
                const auto delivered=host();
                if(!input_count)capture_origin=pending?pending->utc_seconds:wall()-static_cast<long double>(chunk.size())/playback_rate;
                std::size_t consumed=0;
                do {
                    const auto p=converter.process(chunk.subspan(consumed),converted);consumed+=p.consumed;
                    if(p.produced) {
                        if(p.produced>pcm.capacity()-pcm.size())throw Error("diagnostic capture bound exhausted");
                        if(anchors.size()>=anchor_limit)throw Error("diagnostic timestamp bound exhausted");
                        auto timing=pending;
                        if(timing) {
                            const auto delta=static_cast<long double>(pcm.size())*playback_rate/capture_rate-input_count;
                            timing->utc_seconds+=delta*timing->seconds_per_frame;
                            timing->uncertainty_seconds+=static_cast<double>(std::abs(delta)/playback_rate*.002005L);
                            timing->seconds_per_frame*=double(playback_rate)/capture_rate;
                        }
                        anchors.push_back({pcm.size(),p.produced,delivered,timing});pcm.insert(pcm.end(),converted.begin(),converted.begin()+p.produced);
                    }
                    if(!p.consumed&&!p.produced)break;
                }while(consumed<chunk.size());
                input_count+=chunk.size();pending.reset();
                ready=true;changed.notify_all();return !stop.stop_requested();
            },stop.get_token(),[&](const auto& f){capture_format=f;},o);
        }catch(const std::exception& e){if(!stop.stop_requested())capture_error=e.what();stop.request_stop();}
        rx_done=true;changed.notify_all();
    });
    std::jthread transmitter;
    {
        std::unique_lock lock(mutex);
        while(!ready&&!rx_done&&!stop.stop_requested()&&Clock::now()<start+3s)changed.wait_for(lock,20ms);
    }
    if(!ready) {if(!stop.stop_requested())startup_error="diagnostic capture did not become ready";stop.request_stop();}
    else transmitter=std::jthread([&](std::stop_token local) {
        std::stop_callback bridge(local,[&]{stop.request_stop();});
        try {
            auto o=options();const auto total=std::uint64_t(std::ceil(seconds*playback_rate));std::uint64_t supplied=0;
            o.timing_status=[&](const auto& s){
                if(playback_status.size()>=32)throw Error("diagnostic status bound exhausted");
                playback_status.push_back(s);
                if(s.following_system_clock){source_origin=wall()+audio::minimum_lead_seconds()+.25;audio::schedule_output(double(source_origin),stop.get_token());scheduled=true;}
            };
            o.playback_discontinuity=[&](const auto&){++output_gaps;throw Error("diagnostic playback continuity lost");};
            o.timing_source_end=[&]{return std::optional<std::uint64_t>(total);};
            audio::playback(playback_rate,device,[&](std::span<float> chunk) {
                if(!supplied&&!scheduled)source_origin=wall();
                const auto count=std::size_t(std::min<std::uint64_t>(chunk.size(),total-supplied));
                for(std::size_t j=0;j<ids.size();++j){const auto at=std::uint64_t(ids[j])*playback_rate;if(at>=supplied&&at<supplied+count)requested[j]=host();}
                for(std::size_t i=0;i<count;++i){const auto t=double(supplied+i)/playback_rate;const auto k=unsigned(t);chunk[i]=float(k>=1&&k+1<seconds?probe(t-k,k):0);}
                supplied+=count;return count;
            },stop.get_token(),[&](const auto& f){playback_format=f;},audio::ChannelMode::left_mono,o);
        }catch(const std::exception& e){if(!stop.stop_requested())playback_error=e.what();stop.request_stop();}
        tx_done=true;changed.notify_all();
    });
    {
        std::unique_lock lock(mutex);
        while(!tx_done&&!rx_done&&!stop.stop_requested()&&Clock::now()<deadline)changed.wait_for(lock,20ms);
    }
    const bool early_capture=rx_done&&!tx_done&&!stop.stop_requested();
    if(early_capture)capture_error="capture ended before playback completed";
    const bool timeout=!tx_done&&!stop.stop_requested();stop.request_stop();
    if(transmitter.joinable())transmitter.join();
    receiver.join();
    struct RestoreFormat {std::ostream& out;std::ios_base::fmtflags flags;std::streamsize precision;~RestoreFormat(){out.flags(flags);out.precision(precision);}} restore{out,out.flags(),out.precision()};
    out<<std::fixed<<std::setprecision(9);
    out<<"{\"event\":\"audio_timing_test\",\"seconds\":"<<seconds<<",\"device\":";quoted(out,device);
    out<<",\"tx_logical_rate\":"<<playback_rate<<",\"rx_logical_rate\":"<<playback_rate<<",\"analysis_rate\":"<<capture_rate<<",\"tx_hardware_rate\":"<<playback_format.hardware_rate
       <<",\"rx_hardware_rate\":"<<capture_format.hardware_rate<<",\"amplitude_fs\":"<<amplitude<<",\"capture_bytes\":"<<pcm.size()*sizeof(float)
       <<",\"scheduled_utc\":"<<scheduled<<",\"rate_correction\":0,\"capture_gaps\":"<<input_gaps<<",\"playback_gaps\":"<<output_gaps<<"}\n";
    for(const auto& pair:{std::pair{"capture",&capture_status},std::pair{"playback",&playback_status}})for(const auto& status:*pair.second) {
        out<<"{\"event\":\"audio_timing_provider\",\"direction\":";quoted(out,pair.first);out<<",\"estimated_utc_active\":"<<status.following_system_clock
           <<",\"uncertainty_seconds\":";if(std::isfinite(status.modeled_uncertainty_seconds))out<<status.modeled_uncertainty_seconds;else out<<"null";
        out<<",\"reason\":";quoted(out,status.reason);out<<"}\n";
    }
    bool failed=timeout||early_capture||cancel.stop_requested()||!startup_error.empty()||!capture_error.empty()||!playback_error.empty()||input_gaps||output_gaps;
    Analysis analysis;try{if(!failed)analysis=analyze(pcm,seconds,capture_origin,source_origin,cancel);}catch(const std::exception& e){analysis_error=e.what();failed=true;}
    const bool cancelled=cancel.stop_requested();failed|=cancelled;
    std::vector<double> host_roundtrips;
    for(const auto& match:analysis.matches) {
        out<<"{\"event\":\"audio_return\",\"probe\":"<<match.ordinal<<",\"capture_sample\":"<<match.capture_sample<<",\"correlation\":"<<match.correlation
           <<",\"peak_ratio\":"<<match.peak_ratio<<",\"nominal_map_delay_seconds\":"<<match.delay_seconds;
        const auto at=std::find_if(anchors.begin(),anchors.end(),[&](const auto& a){return match.capture_sample>=a.first&&match.capture_sample<a.first+a.count;});
        const auto index=std::find(ids.begin(),ids.end(),match.ordinal)-ids.begin();
        out<<",\"host_callback_roundtrip_seconds\":";if(at!=anchors.end()&&requested[index]>=0) {const auto delay=at->delivered-requested[index];host_roundtrips.push_back(delay);out<<delay;}else out<<"null";
        out<<",\"estimated_adc_minus_scheduled_dac_seconds\":";
        if(scheduled&&at!=anchors.end()&&at->timing)out<<double(at->timing->utc_seconds+(match.capture_sample-at->first)*at->timing->seconds_per_frame-source_origin-match.ordinal);else out<<"null";
        out<<",\"capture_plus_playback_preflight_radius_seconds\":";if(scheduled&&at!=anchors.end()&&at->timing)out<<at->timing->uncertainty_seconds+playback_format.timing_uncertainty_seconds+1./capture_rate;else out<<"null";
        out<<"}\n";
    }
    out<<"{\"event\":\"audio_timing_result\",\"result\":";quoted(out,failed?"error":analysis.sufficient?"input_return_measured":"insufficient_or_no_return");
    out<<",\"attempted\":"<<ids.size()<<",\"matched\":"<<analysis.matches.size()<<",\"all_probes_matched\":"<<(analysis.matches.size()==ids.size());
    const auto measured=[&](const char* name,double v,bool available){out<<",\""<<name<<"\":";if(available)out<<v;else out<<"null";};
    measured("median_nominal_delay_seconds",analysis.median_delay_seconds,!analysis.matches.empty());
    measured("observed_delay_min_seconds",analysis.delay_min_seconds,!analysis.matches.empty());
    measured("observed_delay_max_seconds",analysis.delay_max_seconds,!analysis.matches.empty());
    measured("nominal_delay_slope_ppm",analysis.drift_ppm,analysis.sufficient);
    measured("detrended_jitter_rms_seconds",analysis.residual_rms_seconds,analysis.sufficient);
    std::sort(host_roundtrips.begin(),host_roundtrips.end());
    measured("host_roundtrip_median_seconds",host_roundtrips.empty()?0:host_roundtrips[host_roundtrips.size()/2],!host_roundtrips.empty());
    measured("host_roundtrip_min_seconds",host_roundtrips.empty()?0:host_roundtrips.front(),!host_roundtrips.empty());
    measured("host_roundtrip_max_seconds",host_roundtrips.empty()?0:host_roundtrips.back(),!host_roundtrips.empty());
    out<<",\"timeout\":"<<timeout<<",\"cancelled\":"<<cancelled<<",\"startup_error\":";quoted(out,startup_error);out<<",\"capture_error\":";quoted(out,capture_error);out<<",\"playback_error\":";quoted(out,playback_error);
    out<<",\"analysis_error\":";quoted(out,analysis_error);
    out<<",\"scope\":\"Public chirp-return diagnostic only; delay slope includes clock, path and buffer changes; nominal delay includes unknown analog and driver offsets; host roundtrip includes buffers and callback quantization; driver times are estimates; no GPS, affine, modem or statistical qualification\"}\n";
    return failed?1:analysis.sufficient?0:2;
}
} // namespace datapump::gui::audio_diagnostic
