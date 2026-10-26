#include "datapump/helper_tone_tracker.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
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
    check(aperture<=3.05,"sparse valid segments retained unbounded old frequency history");
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
        strong_wide_tone_and_drift();representative_hf_noise_and_eme_level();narrow_range_and_noise();sparse_segments_keep_recent_aperture();out_of_range_or_large_evolving_shift_declines();
        std::cout<<"helper realtime tone tracker checks passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
