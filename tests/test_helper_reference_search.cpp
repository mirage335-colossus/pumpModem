#include "../src/helper_reference_search.hpp"
#include "../src/helper_reference_tracking_estimate.hpp"
#include "../src/live_helper_frontend.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include <cmath>
#include <iostream>
#include <set>
using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
void near(double a,double b,const char* text){check(std::isfinite(a)&&std::abs(a-b)<=1e-11*std::max({1.,std::abs(a),std::abs(b)}),text);}
}
int main(){try {
    live::detail::HelperViewBlock::ClockMeasurement clock{{-2,2},100,200,201,48000};
    check(clock.valid(48000),"dated marker evidence declined its captured source clock");
    check(!clock.valid(48960),"marker evidence crossed physical sample-clock domains");
    clock.captured_end_sample=200;
    check(!clock.valid(48000),"marker evidence used an uncaptured support endpoint");
    clock.captured_end_sample=26602;
    check(!clock.valid(48000),"stale marker evidence retained clock authority");
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_pattern,false,1500).config;
    c.oscillator_search.emplace();auto& o=*c.oscillator_search;
    o.lf={100,.1};o.rf={100,.1};o.margin=3;o.rf_shift_hz=30000000;
    o.reference=modem::OscillatorReference::independent_audio;
    const auto original=modem::oscillator_pattern_search(c);
    auto reference=modem::helper_reference_search(c,.15,2000,0,0);
    near(reference.clock_frequency_hz,-500,"helper search changed K=D-H");
    near(reference.rf_half_width_hz,.15,"helper search invented RF uncertainty");
    near(reference.catalogue.requested_clock_half_width_ppm,300,"helper tone removed unresolved LF");
    check(reference.catalogue.hypotheses.size()<original.hypotheses.size(),"helper correction did not reduce actual catalogue work");
    std::set<double> clocks;for(const auto& pair:reference.catalogue.hypotheses)clocks.insert(pair.clock_error_ppm);
    check(*clocks.begin()==-300&&*clocks.rbegin()==300,"residual catalogue omitted original LF endpoints");
    const auto utc=modem::helper_reference_search(c,.15,2000,0,.0003);
    check(utc.rf_half_width_hz>=.15+2000*.0003*(1+.0003),"helper correction silently removed peer UTC RF uncertainty");
    check(utc.catalogue.requested_clock_half_width_ppm>reference.catalogue.requested_clock_half_width_ppm,
        "helper correction omitted UTC DATA clock union");
    const auto shifted=modem::helper_reference_search(c,.15,2000,.125,0);
    check(shifted.catalogue.hypotheses.size()==reference.catalogue.hypotheses.size(),"residual center changed constructor LF alternatives");
    for(std::size_t i=0;i<shifted.catalogue.hypotheses.size();++i)
        near(shifted.catalogue.hypotheses[i].frequency_offset_hz-reference.catalogue.hypotheses[i].frequency_offset_hz,.125,
            "residual center was not applied to every exact pair");
    modem::PatternSearch applied;modem::apply_helper_reference_search(std::move(reference),applied);
    check(applied.hypotheses==modem::helper_reference_search(c,.15,2000,0,0).catalogue.hypotheses&&
        !applied.qualified_clock_interval&&applied.admitted_clock_half_width_ppm==300,
        "helper application pruned LF or changed the exact shared catalogue");
    auto audio=c;audio.oscillator_search->rf_shift_hz=0;
    const auto audio_reference=modem::helper_reference_search(audio,.15,audio.carrier_hz,0,0);
    check(audio_reference.catalogue.hypotheses.size()>1&&audio.oscillator_search->rf_shift_hz==0,
        "audio-only residual synthesis changed physical mapping or collapsed unknown LF");
    transfer::Options options;options.modem=c;options.dsp_workspace_bytes=256*1024*1024;
    transfer::Estimate one;one.wire_bits=1;one.total_seconds=modem::symbol_seconds(c);
    modem::ChannelConfig channel;channel.snr_db=10;
    const auto ordinary=simulation::estimate(one,options,true,channel,{},1,false,100,0,
        simulation::ReceiverWorkMode::hardware_fallback);
    const auto same=simulation::estimate_with_receiver_search(one,options,true,channel,original,1,100,
        simulation::ReceiverWorkMode::hardware_fallback);
    near(same.receiver_cpu_seconds,ordinary.receiver_cpu_seconds,"explicit original search changed ordinary cost model");
    check(same.frequency_rate_hypotheses==ordinary.frequency_rate_hypotheses&&
        same.receiver_workspace_supported==ordinary.receiver_workspace_supported,
        "explicit original search changed ordinary constructor geometry");
    const auto corrected=simulation::estimate_with_receiver_search(one,options,true,channel,
        modem::helper_reference_search(c,.15,2000,0,0).catalogue,1,100,simulation::ReceiverWorkMode::hardware_fallback);
    check(corrected.frequency_rate_hypotheses==applied.hypotheses.size()&&
        corrected.requested_clock_search_half_width_ppm==300&&
        !corrected.confidence_available&&!corrected.reference_probability_available,
        "conditional work used different pairs, removed LF or manufactured a reception probability");
    // A constant-variance contiguous endpoint chain is the explicit ideal
    // forecast, not a claim that the modulated helper reaches this precision.
    constexpr auto cn0=30.020599913279625;
    const auto sigma=std::hypot(.5,.05)*std::numbers::pi/180;
    const auto ideal=helper::steady_tone_estimate(48000,cn0,sigma);
    check(ideal.available,"30 dBHz ideal contiguous forecast declined");
    near(ideal.uncertainty_hz,.0604611302075783,"ideal forecast diverged from runtime endpoint covariance");
    check(ideal.accepted_observations==384&&ideal.observation_seconds==7.7&&
        ideal.reason.find("uninterrupted")!=std::string::npos&&
        ideal.reason.find("zero excess residual")!=std::string::npos&&
        ideal.reason.find("can widen uncertainty")!=std::string::npos,
        "ideal precision lost its uninterrupted/zero-excess scope");
    // Independently sum the tridiagonal phase-difference covariance in closed
    // form: OLS weights are arithmetic, so endpoint squares are
    // w0^2+wlast^2+(n-1)*step^2. No production endpoint loop is reused here.
    const auto thermal_only=helper::steady_tone_estimate(48000,cn0,0);
    const auto n=static_cast<double>(thermal_only.accepted_observations);
    const auto dt=thermal_only.block_seconds;
    const auto mean=-(n+1)*dt/2+1./48000;
    const auto tt=dt*dt*n*(n*n-1)/12;
    const auto first=1/n+mean*(n-1)*dt/(2*tt);
    const auto step=-mean*dt/tt,last=first+(n-1)*step;
    const auto endpoint_square=first*first+last*last+(n-1)*step*step;
    const auto v=thermal_only.noise_bandwidth_hz/(2*std::pow(10.,cn0/10));
    const auto tau=2*std::numbers::pi;
    near(thermal_only.uncertainty_hz,.005+3*std::sqrt(v*endpoint_square/(tau*tau*dt*dt)),
        "ideal thermal variance differs from independent OLS covariance");
    const auto louder=helper::steady_tone_estimate(48000,cn0+10,sigma);
    const auto more_process=helper::steady_tone_estimate(48000,cn0,2*sigma);
    const auto lower_rate=helper::steady_tone_estimate(8000,cn0,sigma);
    const auto higher_rate=helper::steady_tone_estimate(96000,cn0,sigma);
    check(louder.available&&more_process.available&&louder.uncertainty_hz<ideal.uncertainty_hz&&
        more_process.uncertainty_hz>ideal.uncertainty_hz&&ideal.uncertainty_hz>thermal_only.uncertainty_hz,
        "ideal precision lost monotonic CNO or phase-process scaling");
    // These exact 20 ms sample rates preserve observation dates; increasing the
    // Hann sample count slightly reduces its equivalent noise bandwidth.
    check(lower_rate.available&&higher_rate.available&&
        lower_rate.noise_bandwidth_hz>ideal.noise_bandwidth_hz&&
        ideal.noise_bandwidth_hz>higher_rate.noise_bandwidth_hz&&
        lower_rate.uncertainty_hz>ideal.uncertainty_hz&&
        ideal.uncertainty_hz>higher_rate.uncertainty_hz&&
        lower_rate.observation_seconds==ideal.observation_seconds&&
        higher_rate.observation_seconds==ideal.observation_seconds,
        "ideal rate scaling changed physical aperture or Hann bandwidth");
    const auto level_rejected=helper::steady_tone_estimate(48000,24,sigma);
    const auto unwrap_rejected=helper::steady_tone_estimate(48000,cn0,3.);
    check(!level_rejected.available&&level_rejected.reason.find("level gate")!=std::string::npos&&
        !unwrap_rejected.available&&unwrap_rejected.reason.find("following gate")!=std::string::npos,
        "ideal forecast bypassed real-time level or unwrap gate");
    const auto strong=helper::steady_tone_estimate(48960,49.7403126773,.5*std::numbers::pi/180);
    const auto weaker=helper::steady_tone_estimate(48960,40,.5*std::numbers::pi/180);
    const auto weak=helper::steady_tone_estimate(24000,20.7609125906,.007071*std::numbers::pi/180);
    check(strong.available&&weaker.available&&strong.uncertainty_hz<weaker.uncertainty_hz&&
        strong.observation_seconds<=8&&strong.observation_seconds>=7.6,
        "conditional tone covariance lost CNO, support or process scaling");
    check(!weak.available&&!weak.reason.empty(),"weak expected tone bypassed actual following level gate");
    std::cout.precision(17);
    std::cout<<"ideal_uninterrupted_U_hz="<<ideal.uncertainty_hz
        <<" observation_seconds="<<ideal.observation_seconds<<'\n';
    std::cout<<"helper reference catalogue/ordinary work/conditional covariance PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
