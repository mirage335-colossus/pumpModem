#include "datapump/helper_phase_clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
bool finite_nonnegative(double x) {return std::isfinite(x)&&x>=0;}
}

HelperPhaseClock::HelperPhaseClock(Config c):config_(c) {
    if(!std::isfinite(c.high_hz)||!std::isfinite(c.low_hz)||c.low_hz<=0||c.high_hz<=c.low_hz||
       !std::isfinite(c.minimum_audio_scale)||!std::isfinite(c.maximum_audio_scale)||
       c.minimum_audio_scale<=0||c.minimum_audio_scale>c.maximum_audio_scale||
       !std::isfinite(c.maximum_gap_seconds)||c.maximum_gap_seconds<=0||
       !std::isfinite(c.total_phase_failure_probability)||c.total_phase_failure_probability<=0||
       c.total_phase_failure_probability>=.1||!c.maximum_observations||c.maximum_observations>4096||
       !finite_nonnegative(c.phase_noise_radians_per_sqrt_second)||
       !finite_nonnegative(c.numerical_phase_half_width_radians)||
       !std::isfinite(c.transmitted_phase_difference_radians)||
       !finite_nonnegative(c.differential_phase_half_width_radians))
        throw std::invalid_argument("invalid Helper phase clock configuration");
}

void HelperPhaseClock::reset() noexcept {
    seeded_=have_=seen_=false;onset_lower_=onset_upper_=last_seen_=0;previous_={};count_=0;
}

bool HelperPhaseClock::seed(const HelperStartupTimingResult& s) noexcept {
    reset();
    if(!s.valid||s.work_limited||!std::isfinite(s.onset_seconds)||
       !finite_nonnegative(s.support_radius_seconds)||
       !std::isfinite(s.onset_seconds-s.support_radius_seconds)||
       !std::isfinite(s.onset_seconds+s.support_radius_seconds))return false;
    onset_lower_=s.onset_seconds-s.support_radius_seconds;
    onset_upper_=s.onset_seconds+s.support_radius_seconds;
    seeded_=true;return true;
}

HelperPhaseClock::Observation HelperPhaseClock::observe(const HelperDwellMeasurement& m) {
    Observation out;out.input_seconds=m.first_sample_time_seconds;
    if(!seeded_||count_>=config_.maximum_observations)return out;
    const double now=m.first_sample_time_seconds;
    if(!std::isfinite(now)||(seen_&&now<=last_seen_))return out;
    last_seen_=now;seen_=true;++count_;out.observations=count_;
    if(!m.valid||!std::isfinite(m.end_sample_time_seconds)||m.end_sample_time_seconds<=now||
       !std::isfinite(m.high_phase_radians)||!std::isfinite(m.low_phase_radians)||
       !std::isfinite(m.high_cn0_db_hz)||!std::isfinite(m.low_cn0_db_hz))return out;
    const double duration=m.end_sample_time_seconds-now;
    const double high_energy=std::pow(10.,m.high_cn0_db_hz/10)*duration;
    const double low_energy=std::pow(10.,m.low_cn0_db_hz/10)*duration;
    if(!(high_energy>0)||!(low_energy>0)||!std::isfinite(high_energy)||!std::isfinite(low_energy))return out;

    // First-sample phase includes the fitted frequency's phase extrapolation;
    // its local variance is four times the centered known-frequency value.
    // Sum the two radii rather than assuming their errors independent. The
    // common phase process cancels ideally; an explicit two-sided allowance
    // also covers imperfect local-fit cancellation under the declared model.
    const double multiplier=std::sqrt(2*std::log(8*static_cast<double>(config_.maximum_observations)/
        config_.total_phase_failure_probability));
    const double phase_radius=multiplier*(std::sqrt(2/high_energy)+std::sqrt(2/low_energy)+
        2*config_.phase_noise_radians_per_sqrt_second*std::sqrt(duration))+
        2*config_.numerical_phase_half_width_radians+config_.differential_phase_half_width_radians;
    if(!std::isfinite(phase_radius)||phase_radius>=std::numbers::pi)return out;
    const double separation=config_.high_hz-config_.low_hz;
    const double radius=phase_radius/(tau*separation);
    out.phase_half_width_seconds=radius;

    double lower=0,upper=0;
    if(have_) {
        const double gap=now-previous_.input_seconds;
        if(!(gap>0)||gap>config_.maximum_gap_seconds) {have_=false;return out;}
        lower=previous_.lower_source_seconds+gap*config_.minimum_audio_scale;
        upper=previous_.upper_source_seconds+gap*config_.maximum_audio_scale;
    } else {
        // Only the initial qualified startup interval may seed a fresh chain.
        // After a lost long-gap chain this broad interval naturally becomes
        // ambiguous; it cannot borrow a simulator or payload timestamp.
        if(now<onset_upper_)return out;
        lower=(now-onset_upper_)*config_.minimum_audio_scale;
        upper=(now-onset_lower_)*config_.maximum_audio_scale;
    }
    if(!std::isfinite(lower)||!std::isfinite(upper)||lower>upper)return out;
    const double cycles=std::remainder(m.high_phase_radians-m.low_phase_radians-
        config_.transmitted_phase_difference_radians,tau)/tau;
    const long double first=std::ceil(static_cast<long double>(separation)*(lower-radius)-cycles);
    const long double last=std::floor(static_cast<long double>(separation)*(upper+radius)-cycles);
    if(!std::isfinite(first)||!std::isfinite(last)||first!=last) {have_=false;return out;}
    const double center=static_cast<double>((static_cast<long double>(cycles)+first)/separation);
    const double lo=std::max(lower,center-radius),hi=std::min(upper,center+radius);
    if(!std::isfinite(center)||!std::isfinite(lo)||!std::isfinite(hi)||lo>hi)return out;
    // The point is the midpoint of the intersection of independently declared
    // rate support and current phase support; both limits remain observable.
    out.valid=true;out.source_seconds=(lo+hi)/2;
    out.lower_source_seconds=lo;out.upper_source_seconds=hi;
    previous_=out;have_=true;return out;
}
} // namespace datapump::helper
