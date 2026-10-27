#include "datapump/helper_tone_tracker.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
constexpr unsigned rate=8000;constexpr double tau=2*std::numbers::pi,carrier=2300.37;
double ramp(double t){const auto local=std::fmod(t,.4);return .5*(1-std::cos(std::numbers::pi*std::clamp(std::min(local/.004,(.4-local)/.004),0.,1.)));}
void continuous_phase_survives_ramps_and_exceeds_old_history() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};auto old=c;old.continuous_pilot=false;
    helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    helper::ToneTracker baseline(old,helper::ToneTracker::workspace_required(old));
    // Isolate the waveform invariant from statistical model-change false
    // alarms. No phase diffusion or AWGN is injected in this ramp-only check;
    // the separate seeded component ensemble measures noisy availability.
    std::array<float,80> pcm{};double phase=.37;
    for(std::uint64_t n=0;n<40ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*carrier/rate;pcm[i]=static_cast<float>(.6*ramp(t)*std::cos(phase));}
        tracker.push(pcm,n);baseline.push(pcm,n);
    }
    const auto s=tracker.snapshot(),b=baseline.snapshot();
    check(s.qualified && s.frequency_continuous_phase,"continuous pilot did not qualify from causal broad acquisition");
    check(s.coherent_aperture_seconds>36 && s.coherent_aperture_seconds<=40,"ramps discarded continuous pilot phase history");
    check(s.uncertainty_hz<.02 && s.uncertainty_hz<b.uncertainty_hz/5,"continuous phase did not improve measured frequency precision");
    check(std::abs(s.frequency_hz-carrier)<=s.uncertainty_hz,"continuous oscillator interval escaped deterministic seeded truth");
    const auto dated=s.frequency_at(s.sample-rate*2/5);
    check(dated.valid && std::abs(dated.frequency_hz-carrier)<=dated.uncertainty_hz,"continuous frequency covariance lost its actual support");
    tracker.begin_pilot_segment(40ULL*rate,2000,1500,true);
    const auto hop=tracker.snapshot();
    check(!hop.frequency_continuous_phase && !hop.qualified && !hop.phase_fit_qualified &&
        !hop.phase_fit_support_uncertainty_cycles,"unknown retune phase retained continuous authority");
    std::cout<<"continuous ramp support="<<s.coherent_aperture_seconds<<" uncertainty="<<s.uncertainty_hz<<" baseline="<<b.uncertainty_hz<<'\n';
}
void model_change_restarts_numeric_fit_and_loss_withdraws() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};helper::ToneTracker tracker(c,helper::ToneTracker::workspace_required(c));
    std::mt19937 random{49172};std::normal_distribution<double> normal;std::array<float,80> pcm{};double phase=.37;
    helper::ToneTrackerSnapshot recovered;std::size_t escaped=0;std::uint64_t last=UINT64_MAX;
    for(std::uint64_t n=0;n<48ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){const auto t=(n+i)/static_cast<double>(rate);phase+=tau*(carrier+.03*std::max(0.,t-30))/rate;pcm[i]=static_cast<float>((t<44?.6*ramp(t)*std::cos(phase):0)+.3*std::sqrt(rate/10000.)*normal(random));}
        tracker.push(pcm,n);const auto s=tracker.snapshot();
        if(s.qualified && s.sample!=last){last=s.sample;const auto t=s.sample/static_cast<double>(rate);escaped+=std::abs(s.frequency_hz-carrier-.03*std::max(0.,t-30))>s.uncertainty_hz;}
        if(s.qualified && s.continuous_model_resets && s.first_sample<s.continuous_last_model_reset_sample)
            std::cout<<"rejected-history publication date="<<s.sample/static_cast<double>(rate)<<" first="<<s.first_sample/static_cast<double>(rate)<<" reset="<<s.continuous_last_model_reset_sample/static_cast<double>(rate)<<" continuous="<<s.frequency_continuous_phase<<" U="<<s.uncertainty_hz<<'\n';
        if(s.qualified && s.continuous_model_resets)check(s.first_sample>=s.continuous_last_model_reset_sample,
            "post-change frequency fallback reused rejected oscillator history");
        if(n==43ULL*rate)recovered=s;
    }
    check(recovered.qualified && recovered.frequency_continuous_phase && recovered.continuous_model_resets>0,
        "late drift change did not restart and recover the numeric oscillator fit");
    check(recovered.uncertainty_hz<.02,"model-change recovery retained broad acquisition cost");
    const auto lost=tracker.snapshot();check(!lost.qualified && !lost.phase_fit_qualified && !lost.phase_fit_support_uncertainty_cycles,
        "final Helper loss retained frequency or phase authority");
    std::cout<<"continuous model changes="<<recovered.continuous_model_resets<<" escaped_estimates_including_detection_latency="<<escaped<<'\n';
}
void long_observation_gap_is_callback_independent() {
    helper::ToneTrackerConfig c{rate,2000,1500,0,.5,true,true};
    helper::ToneTracker small(c,helper::ToneTracker::workspace_required(c));
    helper::ToneTracker large(c,helper::ToneTracker::workspace_required(c));
    std::array<float,80> pcm{};double phase=.37;
    for(std::uint64_t n=0;n<4ULL*rate;n+=pcm.size()) {
        for(std::size_t i=0;i<pcm.size();++i){phase+=tau*carrier/rate;pcm[i]=static_cast<float>(.6*std::cos(phase));}
        small.push(pcm,n);large.push(pcm,n);
    }
    check(small.snapshot().frequency_continuous_phase,"gap fixture did not first acquire continuous phase");
    // One call contains both the genuine missing observation interval and the
    // returning pilot. It must not hide that loss by ending on a good row.
    std::array<float,rate/2> gap_and_return{};
    for(std::size_t i=0;i<gap_and_return.size();++i){phase+=tau*carrier/rate;gap_and_return[i]=i<3*rate/10?0:static_cast<float>(.6*std::cos(phase));}
    large.push(gap_and_return,4ULL*rate);
    for(std::size_t i=0;i<gap_and_return.size();i+=pcm.size())small.push(std::span(gap_and_return).subspan(i,pcm.size()),4ULL*rate+i);
    const auto a=small.snapshot(),b=large.snapshot();
    check(a.continuous_phase_resets==b.continuous_phase_resets && a.continuous_phase_resets>0,
        "observation-gap reset depended on input callback size");
    check(a.frequency_continuous_phase==b.frequency_continuous_phase && a.sample==b.sample &&
        a.first_sample==b.first_sample && std::abs(a.frequency_hz-b.frequency_hz)<1e-12 &&
        std::abs(a.uncertainty_hz-b.uncertainty_hz)<1e-12,
        "returning pilot concealed a genuine phase gap in one large callback");
}

}
int main(){try{continuous_phase_survives_ramps_and_exceeds_old_history();model_change_restarts_numeric_fit_and_loss_withdraws();long_observation_gap_is_callback_independent();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
