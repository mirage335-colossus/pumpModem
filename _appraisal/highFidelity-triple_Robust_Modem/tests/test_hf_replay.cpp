#include "appraisal/hf_replay.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace appraisal;
std::size_t checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
void near(double actual, double expected, double tolerance, const char* why) {
    check(std::isfinite(actual) && std::abs(actual-expected) <= tolerance,why);
}
template<class F> void rejects(F function, const char* why) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,why);
}
void analytic() {
    HfMeasuredTrace trace;
    trace.sample_rate_hz = 2;
    trace.raw_mean_power = 1;
    trace.samples = {{1,0},{0,1},{-1,0},{0,-1},{1,0}};
    validate_hf_replay_trace(trace);
    near(hf_replay_support_seconds(trace),2,0,"last-knot support");
    const auto midpoint = hf_replay_gain(trace,.25);
    near(midpoint.real(),.5,0,"complex interpolation real component");
    near(midpoint.imag(),.5,0,"complex interpolation imaginary component");
    const auto stats = hf_replay_statistics(trace);
    near(stats.mean_power,1,0,"knot mean power");
    near(stats.linear_interpolation_mean_power,2./3,1e-15,"linear chord energy, no hidden renormalization");
    near(stats.fitted_phase_slope_radians_per_second,std::numbers::pi,1e-14,"phase unwrap linear slope");
    near(stats.unwrapped_phase_linear_detrended_std_radians,0,1e-14,"constant phase rate detrends to zero");
    trace.part = HfReplayPart::amplitude_only;
    near(std::norm(hf_replay_gain(trace,.25)),.5,1e-15,"amplitude ablation retains instantaneous power");
    near(hf_replay_gain(trace,.25).imag(),0,0,"amplitude ablation removes phase");
    trace.part = HfReplayPart::phase_only;
    near(std::norm(hf_replay_gain(trace,.25)),1,1e-15,"phase ablation has explicitly constructed unit magnitude");
    near(std::arg(hf_replay_gain(trace,.25)),std::numbers::pi/4,1e-15,"phase ablation retains angle");
    trace.samples = {{1,0},{-1,0}};
    near(std::norm(hf_replay_gain(trace,.25)),0,0,"phase at exact null remains absent");
}
void physical_time_and_bounds() {
    auto trace = *load_ding_2021_trace();
    check(trace.samples.size()==2048,"deposited record has 2048 complex values");
    near(hf_replay_support_seconds(trace),40.94,1e-13,"conditional 50 Hz last-knot support");
    check(trace.source_sha256=="8b2b663989bf294e8279622b6e30dc7d9a278aaf8914634e0a6e38ddca7f6c53",
          "original MAT identity remains attached");
    check(trace.serialized_iq_sha256.size()==64,"serialized exact binary64 identity attached");
    const auto stats = hf_replay_statistics(trace);
    near(stats.mean_power,1,5e-15,"fixed full-record normalization");
    check(stats.linear_interpolation_mean_power>0 && stats.linear_interpolation_mean_power<=1.001,
          "piecewise-linear continuous energy is reported independently");
    check(stats.minimum_amplitude>0 && stats.maximum_amplitude>stats.minimum_amplitude,
          "measured amplitude variation remains present");
    check(stats.unwrapped_phase_linear_detrended_std_radians>0,
          "measured nonlinear phase remains present");
    // The publication names linear trend removal without its estimator. One
    // predeclared endpoint-vs-LS discriminator checks that convention without
    // searching for a slope that reproduces the published statistic.
    std::vector<long double> phase(trace.samples.size());
    phase[0]=std::arg(trace.samples[0]);
    for(std::size_t i=1;i<phase.size();++i) {
        long double value=std::arg(trace.samples[i]);
        while(value-phase[i-1]>std::numbers::pi_v<long double>)value-=2*std::numbers::pi_v<long double>;
        while(value-phase[i-1]<-std::numbers::pi_v<long double>)value+=2*std::numbers::pi_v<long double>;
        phase[i]=value;
    }
    long double mean=0;
    const long double first_phase=phase.front();
    const long double endpoint_slope=(phase.back()-phase.front())/(phase.size()-1);
    for(std::size_t i=0;i<phase.size();++i) {phase[i]-=first_phase+endpoint_slope*i;mean+=phase[i];}
    mean/=phase.size();
    long double variance=0;
    for(const auto value:phase)variance+=(value-mean)*(value-mean);
    const double endpoint_std=std::sqrt(static_cast<double>(variance/phase.size()));
    check(stats.unwrapped_phase_linear_detrended_std_radians<=endpoint_std+1e-12,
          "global LS trend minimizes residual phase variance versus fixed endpoint trend");
    for (std::size_t i=0; i<trace.samples.size(); i+=31) {
        const auto value = hf_replay_gain(trace,static_cast<long double>(i)/trace.sample_rate_hz);
        check(std::abs(value-trace.samples[i])<2e-13,"original complex knots preserved");
    }
    rejects([&]{(void)hf_replay_gain(trace,-.0001);},"no negative-time extrapolation");
    rejects([&]{(void)hf_replay_gain(trace,40.941);},"no repeat or zero padding beyond captured record");
    rejects([&]{(void)hf_replay_gain(trace,std::numeric_limits<double>::quiet_NaN());},"NaN replay time rejected");
    const auto duration = load_ding_2021_trace(HfReplayPart::full,2048./41);
    near(hf_replay_support_seconds(*duration),2047.*41/2048,1e-13,"alternative stated 41-second duration convention");
    const long double knot = 1300.25;
    check(std::abs(hf_replay_gain(trace,knot/50)-hf_replay_gain(*duration,knot/duration->sample_rate_hz))<1e-13,
          "timing convention changes physical clock, not recorded voltages");
    rejects([]{(void)load_ding_2021_trace(HfReplayPart::full,0);},"zero knot rate rejected");
    trace.samples[0] = {std::numeric_limits<double>::infinity(),0};
    rejects([&]{validate_hf_replay_trace(trace);},"nonfinite voltage record rejected");
    std::cout << std::setprecision(17) << "measured trace: mean_power=" << stats.mean_power
              << " interpolated_mean_power=" << stats.linear_interpolation_mean_power
              << " detrended_phase_std=" << stats.unwrapped_phase_linear_detrended_std_radians
              << " phase_slope_rad_s=" << stats.fitted_phase_slope_radians_per_second
              << " min_amplitude=" << stats.minimum_amplitude
              << " max_amplitude=" << stats.maximum_amplitude
              << " endpoint_detrended_phase_std=" << endpoint_std
              << " endpoint_phase_slope_rad_s=" << static_cast<double>(endpoint_slope*trace.sample_rate_hz) << '\n';
}
void independent_numerics() {
    HfMeasuredTrace trace;
    trace.sample_rate_hz=50;
    trace.raw_mean_power=1;
    // A 1 Hz analytic phasor has a known exact continuous response. This
    // tests the interpolation approximation at a declared resolved frequency.
    for (unsigned i=0;i<=100;++i)
        trace.samples.push_back(std::polar(1.,2*std::numbers::pi*i/50));
    double maximum_error=0;
    long double energy=0;
    constexpr unsigned count=20000;
    for (unsigned i=0;i<count;++i) {
        const long double t=2.L*(i+.5L)/count;
        const auto value=hf_replay_gain(trace,t);
        const auto exact=std::polar(1.,2*std::numbers::pi*static_cast<double>(t));
        maximum_error=std::max(maximum_error,std::abs(value-exact));
        energy+=std::norm(value);
    }
    check(maximum_error<.00198,"linear interpolation resolves 1 Hz phasor at 50 Hz knots");
    const double expected=(2+std::cos(2*std::numbers::pi/50))/3;
    near(static_cast<double>(energy/count),expected,1e-7,"independent dense quadrature verifies interpolated energy");
    auto refined=trace;
    refined.samples.clear();refined.sample_rate_hz=100;
    for(std::size_t i=0;i+1<trace.samples.size();++i) {
        refined.samples.push_back(trace.samples[i]);
        refined.samples.push_back((trace.samples[i]+trace.samples[i+1])*.5);
    }
    refined.samples.push_back(trace.samples.back());
    for(unsigned i=0;i<111;++i) {
        const long double t=2.L*i/110;
        check(std::abs(hf_replay_gain(trace,t)-hf_replay_gain(refined,t))<1e-14,
              "equivalent linear subdivision preserves waveform; no chunk clocks");
    }
}
}
int main() {
    try {analytic();physical_time_and_bounds();independent_numerics();
        std::cout << checks << " measured HF replay checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
