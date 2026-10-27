#include "datapump/helper_tone_tracker.hpp"
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
            const auto sign=punctuation && std::fmod(t,.4)>=.24?-1.:1.;
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
    check(!s.qualified || s.uncertainty_hz>.2,"weak EME helper invented high frequency precision");
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
// few Hz even at high C/N0. The fine loop must pull in before the minimum2s
// helper lead ends, without deriving qualification from the seed itself.
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
    for(const auto c:cases) {
        helper::Generator generator(options,helper::Plan{helper_hz,.6,0,40},epoch,true,key);
        const helper::ToneTrackerConfig config{rate,helper_hz,90.7,.01,.5};
        helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
        Source source;std::vector<float> block(c.batch);std::uint64_t first=0,first_qualified=0;
        helper::ToneTrackerSnapshot initial;
        while(first<2ULL*rate) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),2ULL*rate-first));
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
            <<" initial_U="<<initial.uncertainty_hz<<'\n';
        check(first_qualified && first_qualified<2ULL*rate,"actual helper fine capture missed the minimum helper-only lead");
        check(std::abs(initial.frequency_hz-helper_hz-c.offset)<=initial.uncertainty_hz,
            "first actual helper qualification escaped its engineering interval");
        const auto last=tracker.snapshot();
        check(last.qualified && std::abs(last.frequency_hz-helper_hz-c.offset)<=last.uncertainty_hz,
            "actual helper capture did not hand off to ordinary tracking");
    }
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
    check(aperture<=8.05,"sparse valid segments retained unbounded old frequency history");
}
void stable_full_capacity_keeps_moving() {
    constexpr std::uint32_t rate=48000;
    const helper::ToneTrackerConfig c{rate,2062.5,20,0,.5};
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));Source source;
    for(std::uint64_t first=0;first<10ULL*rate;first+=960) {
        auto block=source.block(rate,first,960,2063.2,0,.6,.02);
        tracker.push(block,first);const auto s=tracker.snapshot();
        if(first>=5ULL*rate) {
            check(s.qualified,"stable full-capacity history lost qualification");
            const auto span=static_cast<double>(s.sample-s.first_sample+1)/rate;
            check(span<=7.73,"full history stopped evicting its oldest observation on each accepted update");
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
void rolling_fifo_and_realistic_helper_modulation() {
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
            // Match Generator's400ms grid,4ms ramps,8ms C2 reversal and
            // three8ms APSK blends ending388ms plus8ms return. The137ms
            // offset deliberately makes transitions cut through loop windows.
            const auto local=std::fmod(t+.137,.4);
            const auto envelope=smooth(std::min(local,.4-local)/.004);
            const auto punct=1-2*smooth((local-.236)/.008);
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
    check(s.first_sample>static_cast<std::uint64_t>(3.95*rate),"realistic FIFO retained observations beyond its source-date window");
}
struct ClockPayloadStats {
    std::size_t polls=0,qualified=0;
    std::vector<double> uncertainties;
    double maximum_error=0;
};
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
                shape==2?1.:shape==3 && (dwell&1)?1.:1-2*smooth((local-(period*.6-.004))/.008);
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
void ordinary_payload_does_not_obscure_punctuation_clock() {
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
void actual_encrypted_helper_boundary_does_not_poison_clock() {
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
    // The real encrypted3-symbol timecode and return/ramp implementation,
    // using a public fixed test key. No live/private key is stored in this test.
    helper::Generator generator(options,helper::Plan{2062.5,.6,0,40},epoch,true,key);
    const helper::ToneTrackerConfig config{rate,2062.5,9000,.01,.5};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    Source source;std::vector<float> block(733);std::uint64_t first=0;
    const auto end=payload.size()+8ULL*rate;std::size_t polls=0,qualified=0;
    double maximum_uncertainty=0;
    modem::PatternSearch search;search.admitted_clock_half_width_ppm=300;
    search.admitted_clock_cell_half_width_ppm=37.5;
    const auto rf_step=.25*rate/modem::symbol_sample_count(data);
    for(int lf=-4;lf<=4;++lf)for(int rf=-20;rf<=20;++rf)
        search.hypotheses.push_back({rf*rf_step-562.5*(lf*75.)*1e-6,lf*75.});
    modem::detail::PatternHelperFrequencySelector selector;
    check(selector.initialize(search,{0,1,562.5,37.5}),"measured frequency work geometry unavailable");
    auto passthrough_selector=selector;
    std::vector<double> frequency_uncertainties,selected_pairs,passthrough_pairs;
    double maximum_frequency_error=0;std::size_t frequency_interval_escapes=0;
    while(first<end) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),end-first));
        for(std::size_t i=0;i<count;++i) {
            const auto sample=first+i;
            const auto seconds=static_cast<long double>(sample)/rate+.137L;
            double value=generator.mix(0,seconds,rate);
            if(sample>=2ULL*rate && sample<2ULL*rate+payload.size())value+=gain*payload[sample-2ULL*rate];
            value+=.0083*source.noise(source.rng);block[i]=static_cast<float>(std::round(value*32767)/32767);
        }
        tracker.push(std::span(block).first(count),first);first+=count;
        if(first>6ULL*rate && first<2ULL*rate+payload.size()) {
            ++polls;const auto result=tracker.snapshot();
            if(result.clock_qualified) {++qualified;maximum_uncertainty=std::max(maximum_uncertainty,result.clock_uncertainty_ppm);}
            if(result.qualified && result.clock_qualified) {
                frequency_uncertainties.push_back(result.uncertainty_hz);
                maximum_frequency_error=std::max(maximum_frequency_error,std::abs(result.frequency_hz-2062.5));
                frequency_interval_escapes+=std::abs(result.frequency_hz-2062.5)>result.uncertainty_hz;
                const auto measured_offset=result.frequency_hz-2062.5;
                const auto applied_measured_translation=measured_offset;
                // Conditional geometry AFTER translating this measured tone,
                // as Live's normal corrected view does. No decoder/NCO timing
                // work is measured by this selector-only counterfactual.
                const auto corrected_center=measured_offset-applied_measured_translation;
                const modem::PatternHelperClockInterval clock{result.clock_error_ppm-result.clock_uncertainty_ppm,
                    result.clock_error_ppm+result.clock_uncertainty_ppm};
                selector.update({corrected_center,result.uncertainty_hz,562.5,37.5,clock});
                passthrough_selector.update({measured_offset,result.uncertainty_hz,562.5,37.5,clock});
                selected_pairs.push_back(static_cast<double>(selector.selected));
                passthrough_pairs.push_back(static_cast<double>(passthrough_selector.selected));
            }
        }
    }
    const auto fraction=polls?static_cast<double>(qualified)/polls:0;
    std::cout<<"actual encrypted helper payload_seconds="<<static_cast<double>(payload.size())/rate
        <<" qualified_fraction="<<fraction<<" maximum_uncertainty="<<maximum_uncertainty<<'\n';
    check(polls>100 && fraction>.99,"actual encrypted helper boundary poisoned the clock aperture");
    std::sort(frequency_uncertainties.begin(),frequency_uncertainties.end());
    std::sort(selected_pairs.begin(),selected_pairs.end());
    std::sort(passthrough_pairs.begin(),passthrough_pairs.end());
    check(!frequency_uncertainties.empty(),"actual helper produced no measured frequency work evidence");
    std::cout<<"actual measured frequency median_U="<<frequency_uncertainties[frequency_uncertainties.size()/2]
        <<" p90_U="<<frequency_uncertainties[frequency_uncertainties.size()*9/10]
        <<" max_error="<<maximum_frequency_error<<" interval_escapes="<<frequency_interval_escapes<<" rf_step="<<rf_step
        <<" corrected_counterfactual_selected_median="<<selected_pairs[selected_pairs.size()/2]
        <<" corrected_counterfactual_selected_p90="<<selected_pairs[selected_pairs.size()*9/10];
    double selected_sum=0;std::size_t three_or_less=0;
    for(const auto value:selected_pairs) {selected_sum+=value;three_or_less+=value<=3;}
    std::cout<<" corrected_counterfactual_selected_mean="<<selected_sum/selected_pairs.size()
        <<" corrected_counterfactual_selected_le3_fraction="<<static_cast<double>(three_or_less)/selected_pairs.size()<<'\n';
    double passthrough_sum=0;std::size_t passthrough_three=0;
    for(const auto value:passthrough_pairs) {passthrough_sum+=value;passthrough_three+=value<=3;}
    std::cout<<"raw_passthrough_geometry selected_median="<<passthrough_pairs[passthrough_pairs.size()/2]
        <<" selected_p90="<<passthrough_pairs[passthrough_pairs.size()*9/10]
        <<" selected_mean="<<passthrough_sum/passthrough_pairs.size()
        <<" selected_le3_fraction="<<static_cast<double>(passthrough_three)/passthrough_pairs.size()<<'\n';
    check(!frequency_interval_escapes,"actual helper frequency measurement escaped its engineering interval");
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
int main() {
    try {
        strong_wide_tone_and_drift();representative_hf_noise_and_eme_level();narrow_range_and_noise();actual_helper_cold_capture_before_payload();sparse_segments_keep_recent_aperture();stable_full_capacity_keeps_moving();rolling_fifo_and_realistic_helper_modulation();shared_endpoint_precision_of_stable_tone();ordinary_payload_does_not_obscure_punctuation_clock();actual_encrypted_helper_boundary_does_not_poison_clock();off_cadence_feature_does_not_poison_established_clock();independent_punctuation_clock();out_of_range_or_large_evolving_shift_declines();
        std::cout<<"helper realtime tone tracker checks passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
