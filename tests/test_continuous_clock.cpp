#include "datapump/continuous_clock.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump::audio;
constexpr long double epoch = 1793980800.L;
constexpr std::uint32_t rate = 48000;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(long double a, long double b, long double tolerance, const char* message) {
    check(std::abs(a-b) <= tolerance, message);
}
template<class F> void rejects(F operation, const char* message) {
    try { operation(); } catch (const datapump::Error&) { return; }
    throw std::runtime_error(message);
}
FrameTimestamp timestamp(long double native_seconds, long double elapsed, long double step = 0,
                         double uncertainty = .0001) {
    return {native_seconds*rate, epoch+elapsed+step, 500000.L+elapsed,
            uncertainty, .000001, TimingQuality::estimated, 17};
}

void directions_and_steps() {
    ContinuousClock tx(rate, false), rx(rate, true);
    check(tx.observe(timestamp(0,0)) && rx.observe(timestamp(0,0)), "initial timestamp rejected");
    tx.observe(timestamp(60,60,2)); rx.observe(timestamp(60,60,2));
    const auto a = tx.update(60*rate,60,0), b = rx.update(60*rate,60,0);
    check(a.target_correction > 0 && b.target_correction < 0, "step used wrong resampling direction");
    near((1+a.target_correction)*(1+b.target_correction), 1, 2e-16L, "TX/RX rates are not inverse");
    check(a.phase_correction_limited && b.phase_correction_limited && !a.saturated && !b.saturated,
          "phase limiting was confused with total converter saturation");
    near(a.target_correction,50e-6L,1e-15L,"UTC catch-up ignored gentle phase limit");
    near(a.q_estimate,1,1e-15,"UTC step poisoned device-rate estimate");
    check(a.clock_steps == 1, "clock step not reported");
    tx.observe(timestamp(120,120,-2));
    const auto reverse = tx.update(120*rate,120,0);
    check(reverse.target_correction < 0, "reverse correction waited behind an old FIFO");
    near(reverse.cumulative_clock_step_seconds,-2,1e-8,"clock-step accounting reset");
}

void rate_and_shared_slew() {
    for (const auto q : {1.L+20e-6L,1.L-100e-6L,1.L+100e-6L,1.L+400e-6L}) {
        ContinuousClock tx(rate,false), rx(rate,true);
        for (int i=0;i<=1800;++i) {
            tx.observe(timestamp(i,i*q)); rx.observe(timestamp(i,i*q));
        }
        const auto a=tx.update(1800*rate,1800*q,0), b=rx.update(1800*rate,1800*q,0);
        near(a.q_estimate,q,1e-10L,"continuous frame/steady rate not learned");
        near(a.target_correction,q-1,2e-10L,"playback feedforward sign or magnitude wrong");
        near(b.target_correction,1/q-1,2e-10L,"capture feedforward is not inverse");
        check(!a.phase_correction_limited && !b.phase_correction_limited,
              "device-rate feedforward was limited as UTC phase catch-up");
        check(a.clock_steps==0,"shared UTC/monotonic slew misclassified as steps");
    }
    ContinuousClock excessive(rate,false);
    for (int i=0;i<=1800;++i) excessive.observe(timestamp(i,i*1.002L));
    const auto limited=excessive.update(1800*rate,1800*1.002L,0);
    check(limited.saturated && !limited.phase_correction_limited,
          "excessive device rate did not report separate total-rate saturation");
    near(limited.target_correction,.001,1e-15L,"total converter rate limit changed");
}

unsigned gentle_phase_recovery() {
    unsigned slowest=0;
    for (const bool capture : {false,true}) for (const int sign : {-1,1}) {
        ContinuousClock clock(rate,capture);
        long double logical=0;
        double applied=0;
        unsigned recovered=0;
        for (unsigned i=0;i<=14*3600+60;++i) {
            clock.observe(timestamp(i,i,i>=60 ? 2.L*sign : 0.L));
            const auto r=clock.update(static_cast<long double>(i)*rate,logical,applied);
            check(!r.saturated,"gentle phase catch-up saturated full device-rate range");
            const auto canonical_rate=capture ? 1.L/(1.L+r.target_correction)-1.L : r.target_correction;
            check(std::abs(canonical_rate)<=50e-6L+1e-15L,"phase catch-up exceeded 50ppm default");
            if (i==3660) check(std::abs(r.phase_error_seconds)>1.8,"two-second UTC step caught up implausibly fast");
            if (i==36060) check(std::abs(r.phase_error_seconds)>.19 && std::abs(r.phase_error_seconds)<.21,
                                "ten-hour phase residual disagrees with bounded catch-up rate");
            if (i>60 && !recovered && std::abs(r.phase_error_seconds)<.001) recovered=i-60;
            if (i==14*3600+60) check(std::abs(r.phase_error_seconds)<.001,"isolated UTC step did not eventually settle");
            const auto next=applied+std::clamp(r.target_correction-applied,-1e-5,1e-5);
            logical+=capture ? 1.L/(1.L+.5*(applied+next)) : 1.L+.5*(applied+next);
            applied=next;
        }
        check(recovered>=40000 && recovered<14*3600,"recovery time violates gentle-rate expectation");
        slowest=std::max(slowest,recovered);
    }
    return slowest;
}

void uncertain_rate_window() {
    constexpr long double q=1.L+100e-6L;
    ContinuousClock adaptive(rate,false);
    for (int i=0;i<=600;++i) {
        adaptive.observe(timestamp(i,i*q,0,.1));
        if (i==60) {
            const auto first=adaptive.update(60*rate,60*q,0);
            check(first.rate_updates==0 && first.rejected==0,
                  "plausible uncertain slope was prematurely rejected");
        }
    }
    const auto learned=adaptive.update(600*rate,600*q,0);
    check(learned.rate_updates==2 && learned.rejected==0,
          "large timestamp uncertainty prevented finite-window rate learning");
    near(learned.q_estimate,1.L+75e-6L,1e-12L,"extended baseline learned wrong rate");

    ContinuousClockOptions short_window;
    short_window.maximum_rate_window_seconds=120;
    ContinuousClock bounded(rate,false,short_window);
    for (int i=0;i<=240;++i) bounded.observe(timestamp(i,i*q,0,.1));
    const auto expired=bounded.update(240*rate,240*q,0);
    check(expired.rate_updates==0 && expired.rejected==2,
          "uninformative rate anchor exceeded its declared maximum window");

    // An implausible fit resets immediately; its stale anchor must not poison
    // the next otherwise valid finite-window estimate.
    ContinuousClock outlier(rate,false);
    outlier.observe(timestamp(0,0));
    outlier.observe(timestamp(60,120));
    outlier.observe(timestamp(120,180));
    const auto recovered=outlier.update(120*rate,180,0);
    check(recovered.rejected==1 && recovered.rate_updates==1,
          "implausible slope did not reset for the next valid rate window");

    auto invalid=short_window;invalid.maximum_rate_window_seconds=59;
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},
            "rate window maximum smaller than minimum accepted");
    invalid.maximum_rate_window_seconds=std::numeric_limits<double>::infinity();
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},
            "nonfinite rate window maximum accepted");
}

struct Result {long double final_error=0,maximum_after_step=0;std::uint64_t steps=0;double maximum_rate=0;};
Result long_run(bool capture, long double drift, unsigned days) {
    ContinuousClock clock(rate,capture);
    long double logical=0, steps=0;
    double applied=0;
    Result result;
    for (unsigned second=0;second<=days*86400u;++second) {
        // 100ms per hour plus alternating 10ms updates. Total phase correction
        // exceeds ninety seconds over38days; no lifetime two-second budget.
        if (second && second%3600==0) steps+=.1L;
        if (second && second%900==0) steps+=(second/900%2 ? .01L : -.01L);
        if (second==86400)steps+=2;
        if (second==2*86400)steps-=2;
        const long double physical=second*(1+drift);
        const long double jitter=.00002L*std::sin(second*.37L);
        clock.observe(timestamp(second,physical+jitter,steps,.0001));
        const auto r=clock.update(static_cast<long double>(second)*rate,logical,applied);
        check(r.ready && !r.holdover,"regular long stream lost its clock association");
        check(std::abs(r.target_correction)<=.001+1e-15,"target escaped software rate domain");
        result.maximum_rate=std::max(result.maximum_rate,std::abs(r.target_correction));
        if(second>3*86400 && second%3600==3599)
            result.maximum_after_step=std::max(result.maximum_after_step,std::abs(static_cast<long double>(r.phase_error_seconds)));
        result.final_error=r.phase_error_seconds;result.steps=r.clock_steps;
        const auto next=applied+std::clamp(r.target_correction-applied,-1e-5,1e-5);
        const auto midpoint=.5*(applied+next);
        logical += capture ? 1.L/(1.L+midpoint) : 1.L+midpoint;
        applied=next;
    }
    check(std::abs(steps)>2,"test did not exceed former correction budget");
    check(result.steps>days*24,"repeated corrections were not retained");
    // This is a declared ideal-frame synthetic controller test, not a device
    // uncertainty bound. The narrow limit applies before each hourly jump.
    check(result.maximum_after_step<.02L,"sustainable corrections accumulated a backlog");
    return result;
}

void unavailable_and_discontinuity() {
    ContinuousClock clock(rate,false);
    auto r=clock.update(0,0,0);
    check(!r.ready && r.holdover,"undated converter silently qualified");
    auto t=timestamp(0,0);
    clock.observe(t);
    clock.unavailable();
    r=clock.update(0,0,.0002);
    check(r.holdover && r.target_correction==.0002,"missing timestamp did not hold current applied rate");
    t=timestamp(1,1);
    clock.observe(t);
    check(!clock.update(rate,1,.0002).holdover,"valid observation did not restore control");
    auto bad=timestamp(2,2);bad.utc_seconds=std::numeric_limits<long double>::quiet_NaN();
    check(!clock.observe(bad),"nonfinite timestamp accepted");
    check(clock.update(2*rate,2,.0002).holdover,"invalid timestamp silently retained qualification");
    clock.observe(timestamp(3,3));
    check(clock.update(7*rate,7,.0002).holdover,"stale extrapolation remained qualified");
    auto gap=timestamp(8,8);gap.continuity++;
    rejects([&]{clock.observe(gap);},"device counter generation silently reset");
    rejects([&]{clock.observe(timestamp(2,2));},"backward device frame silently reset");
    rejects([&]{clock.update(6*rate,8,0);},"backward converter frontier accepted");
    rejects([]{ContinuousClock invalid_clock(0,false);},"zero native rate accepted");
    ContinuousClockOptions invalid;invalid.maximum_rate_correction=.002;
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},"excessive converter rate accepted");
    invalid={};invalid.maximum_phase_correction=.002;
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},"phase rate larger than total rate accepted");
    invalid.maximum_phase_correction=0;
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},"zero phase rate accepted");
    invalid.maximum_phase_correction=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{ContinuousClock invalid_clock(rate,false,invalid);},"nonfinite phase rate accepted");
}

void uncertainty_and_unsustainable_updates() {
    ContinuousClock clock(rate,false);
    clock.observe(timestamp(0,0,0,.02));
    clock.observe(timestamp(1,1,.01,.02));
    near(clock.update(rate,1,0).target_correction,0,0,"timestamp uncertainty drove false phase correction");
    ContinuousClock overloaded(rate,false);
    long double logical=0;
    double applied=0;
    ClockDisciplineReport r;
    for(unsigned i=0;i<=10000;++i) {
        const auto step=.1L*std::floor(static_cast<long double>(i)/10);
        overloaded.observe(timestamp(i,i,step));
        r=overloaded.update(static_cast<long double>(i)*rate,logical,applied);
        const auto next=applied+std::clamp(r.target_correction-applied,-1e-5,1e-5);
        logical+=1.L+.5*(applied+next);applied=next;
    }
    check(r.phase_correction_limited && !r.saturated && r.phase_error_seconds>99,
          "unsustainable reference rate falsely reported tracking or full-rate saturation");
    check(r.clock_steps>=999,"overload reset prior correction history");
}
} // namespace

int main() {
    try {
        directions_and_steps();rate_and_shared_slew();uncertain_rate_window();unavailable_and_discontinuity();
        uncertainty_and_unsustainable_updates();
        const auto phase_recovery=gentle_phase_recovery();
        const auto tx=long_run(false,20e-6L,38),rx=long_run(true,-100e-6L,38);
        std::cout << "continuous clock tests passed;38-day pre-step residual TX="
                  << static_cast<double>(tx.maximum_after_step) << "s RX="
                  << static_cast<double>(rx.maximum_after_step) << "s;2s-step recovery to1ms="
                  << phase_recovery << "s\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "continuous clock: " << e.what() << '\n';return 1;
    }
}
