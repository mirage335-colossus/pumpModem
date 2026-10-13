#include "datapump/audio_provider_timing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace datapump;
using namespace datapump::audio;
namespace {
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& fn) {
    bool failed=false;try {fn();}catch(const Error&){failed=true;}
    check(failed,"invalid provider evidence was accepted");
}
constexpr long double epoch=1800000000.L;
constexpr std::uint32_t rate=48000;
ProviderTimingCapabilities disciplined() {
    ProviderTimingCapabilities c;c.timestamp_quality=TimingQuality::bounded;
    c.hardware_dated_capture=c.timestamped_playback=c.continuous_sample_counters=true;
    c.clock_quality_reports=c.certified_future_rate=c.explicit_discontinuity_evidence=true;
    return c;
}
ProviderTimingObservation observed(std::uint64_t frame,long double time,
        double lower=1-1e-8,double upper=1+1e-8,long double until=epoch+10) {
    ProviderTimingObservation o;
    o.timestamp={static_cast<long double>(frame),epoch+time,time+1,2e-7,2e-8,TimingQuality::bounded,19};
    o.sample_counter=frame;o.counter_generation=7;o.clock=ProviderClockState::locked;
    o.future_rate=ProviderFutureRateBound{lower,upper,epoch-1,until,7};return o;
}
void declared_capabilities() {
    ProviderTimingCapabilities host;
    ProviderTimingContract ordinary(host,rate,false);
    ProviderTimingObservation o;o.timestamp={0,epoch,1,.01,1e-6,TimingQuality::estimated,3};
    ordinary.observe(o);
    check(!ordinary.future_rate_interval(epoch+750),"ordinary host acquired a future hardware guarantee");
    auto promoted=o;promoted.timestamp.quality=TimingQuality::bounded;
    rejects([&]{ordinary.observe(promoted);});
    check(ordinary.latest()->timestamp.quality==TimingQuality::estimated,
        "failed quality promotion mutated the established provider");
    auto bad=host;bad.timestamp_quality=TimingQuality::bounded;rejects([&]{bad.validate();});
    bad=host;bad.timestamped_playback=true;rejects([&]{bad.validate();});
    bad=host;bad.certified_future_rate=bad.continuous_sample_counters=true;rejects([&]{bad.validate();});
    bad=disciplined();bad.timestamped_playback=false;
    rejects([&]{ProviderTimingContract wrong_direction(bad,rate,false);});
    ProviderTimingContract capture(bad,rate,true);capture.observe(observed(0,0));
    bad=disciplined();bad.timestamp_quality=TimingQuality::unavailable;rejects([&]{bad.validate();});
    rejects([&]{ProviderTimingContract no_rate(disciplined(),0,false);});
}
void future_bounds_and_origin() {
    for(const bool capture:{false,true})for(const double q:{1-1e-8,1.,1+1e-8}) {
        ProviderTimingContract c(disciplined(),rate,capture);c.observe(observed(0,0));
        for(unsigned second=1;second<=8;++second)c.observe(observed(second*rate,second*q));
        const auto bound=c.future_rate_interval(epoch+9);
        check(bound && bound->q_lower==1-1e-8 && bound->q_upper==1+1e-8,
            "provider future rate changed its q units or interval");
        check(c.origin()->timestamp.utc_seconds==epoch && c.origin()->sample_counter==0 &&
            c.origin()->counter_generation==7,"provider observation renewed its original clock reference");
        rejects([&]{c.future_rate_interval(epoch+10.001);});
        auto late=observed(11*rate,11*q,1-1e-8,1+1e-8,epoch+30);
        rejects([&]{c.observe(late);});
        check(c.latest()->sample_counter==8*rate,"expired gap changed the retained timing state");
        c.observe(observed(9*rate,9*q,1-1e-8,1+1e-8,epoch+30));
        c.observe(late);
        check(c.future_rate_interval(epoch+29).has_value(),"timely renewed provider certificate was rejected");
    }
    ProviderTimingContract holdover(disciplined(),rate,false);holdover.observe(observed(0,0));
    auto h=observed(rate,1,1-2e-6,1+2e-6,epoch+2);h.clock=ProviderClockState::holdover;
    holdover.observe(h);
    check(holdover.future_rate_interval(epoch+1.9)->q_upper==1+2e-6,
        "holdover discarded its independently certified wider future bound");
    rejects([&]{holdover.future_rate_interval(epoch+2.01);});
    h=observed(2*rate,2);h.clock=ProviderClockState::unlocked;rejects([&]{holdover.observe(h);});
    h.clock=ProviderClockState::free_running;rejects([&]{holdover.observe(h);});
    h.clock=ProviderClockState::unknown;rejects([&]{holdover.observe(h);});
    check(holdover.origin()->timestamp.utc_seconds==epoch && holdover.latest()->clock==ProviderClockState::holdover,
        "lock failure reset the timing history");
}
void trajectories_and_discontinuities() {
    // A certified future interval covers actual slow rate changes; the mean
    // past rate is not substituted for either future endpoint.
    ProviderTimingContract wander(disciplined(),rate,false);wander.observe(observed(0,0,1-2e-5,1+2e-5,epoch+40));
    long double time=0;
    for(unsigned second=1;second<=30;++second) {
        time+=1+(second%7-3.L)*4e-6L;
        wander.observe(observed(second*rate,time,1-2e-5,1+2e-5,epoch+40));
    }
    check(wander.origin()->sample_counter==0,"slow wander reset the original sample origin");
    for(unsigned scenario=0;scenario<7;++scenario) {
        ProviderTimingContract c(disciplined(),rate,false);c.observe(observed(0,0));
        auto bad=observed(rate,1);
        if(scenario==0)bad.counter_generation=8;
        if(scenario==1)bad.timestamp.continuity=20;
        if(scenario==2)bad.timestamp.utc_seconds+=.002;
        if(scenario==3)bad.sample_counter=rate+1;
        if(scenario==4)bad.future_rate->q_lower=1.0002;
        if(scenario==5)bad.future_rate->valid_until_utc=epoch+.5;
        if(scenario==6) {bad.future_rate.reset();bad.clock=ProviderClockState::free_running;}
        rejects([&]{c.observe(bad);});
        check(c.latest()->sample_counter==0 && c.events().events==0,
            "rejected counter/clock evidence changed provider history");
    }
    // Large absolute counters retain their exact differences even on targets
    // whose long double cannot represent every uint64 integer.
    constexpr std::uint64_t origin=std::uint64_t{1}<<60;
    ProviderTimingContract large(disciplined(),rate,false);large.observe(observed(origin,0));
    large.observe(observed(origin+rate,1));
    check(large.latest()->sample_counter==origin+rate,"large hardware counter lost its exact identity");
    auto rewind=observed(origin+rate-1,1.1L);rejects([&]{large.observe(rewind);});
}
void bounded_event_ledger() {
    for(const auto kind:{ProviderDiscontinuityKind::dropped,ProviderDiscontinuityKind::inserted,
        ProviderDiscontinuityKind::repeated,ProviderDiscontinuityKind::displaced,ProviderDiscontinuityKind::mixed}) {
        ProviderTimingContract c(disciplined(),rate,false);c.observe(observed(0,0));
        auto o=observed(4800,.1L);
        ProviderDiscontinuityEvidence e;e.event=1;e.counter_generation=7;e.first_frame=4700;e.end_frame=4800;
        e.kind=kind;e.bounded=true;
        if(kind==ProviderDiscontinuityKind::dropped || kind==ProviderDiscontinuityKind::mixed)e.lost_frames=12;
        if(kind==ProviderDiscontinuityKind::inserted || kind==ProviderDiscontinuityKind::mixed)e.inserted_frames=3;
        if(kind==ProviderDiscontinuityKind::repeated || kind==ProviderDiscontinuityKind::mixed)e.repeated_frames=2;
        if(kind==ProviderDiscontinuityKind::displaced || kind==ProviderDiscontinuityKind::mixed)
            e.displacement_lower_frames=-2,e.displacement_upper_frames=4;
        o.discontinuity=e;c.observe(o);
        check(c.events().events==1 && c.events().lost_frames==e.lost_frames &&
            c.events().inserted_frames==e.inserted_frames && c.events().repeated_frames==e.repeated_frames &&
            c.events().affected_frames==100 && c.origin()->sample_counter==0 &&
            c.events().displacement_lower_frames==e.displacement_lower_frames &&
            c.events().displacement_upper_frames==e.displacement_upper_frames,
            "bounded event changed the origin or lost sample accounting");
        auto second=observed(9600,.2L);auto next=e;next.event=2;next.first_frame=9500;next.end_frame=9600;
        second.discontinuity=next;c.observe(second);
        check(c.events().events==2 && c.events().lost_frames==2*e.lost_frames &&
            c.events().affected_frames==200 && c.events().displacement_lower_frames==2*e.displacement_lower_frames &&
            c.events().displacement_upper_frames==2*e.displacement_upper_frames,
            "provider events renewed an accumulated count or displacement budget");
        auto duplicate=observed(14400,.3L);duplicate.discontinuity=e;
        rejects([&]{c.observe(duplicate);});
        check(c.events().events==2 && c.latest()->sample_counter==9600,"duplicate event was charged or applied twice");
    }
    for(unsigned scenario=0;scenario<7;++scenario) {
        auto capabilities=disciplined();if(scenario==6)capabilities.explicit_discontinuity_evidence=false;
        ProviderTimingContract c(capabilities,rate,false);c.observe(observed(0,0));
        auto o=observed(4800,.1L);ProviderDiscontinuityEvidence e;
        e.event=1;e.counter_generation=7;e.first_frame=4700;e.end_frame=4800;e.lost_frames=12;
        e.kind=ProviderDiscontinuityKind::dropped;e.bounded=true;
        if(scenario==0)e.bounded=false;
        if(scenario==1)e.kind=ProviderDiscontinuityKind::unknown;
        if(scenario==2)e.end_frame=4801;
        if(scenario==3)e.lost_frames=101;
        if(scenario==4)e.counter_generation=8;
        if(scenario==5)e.displacement_lower_frames=2,e.displacement_upper_frames=-2;
        o.discontinuity=e;rejects([&]{c.observe(o);});
        check(c.events().events==0 && c.latest()->sample_counter==0,
            "unbounded event partially mutated the provider ledger");
    }
    ProviderTimingContract overflow(disciplined(),rate,false);overflow.observe(observed(0,0));
    auto first=observed(4800,.1L);ProviderDiscontinuityEvidence huge;
    huge.event=1;huge.counter_generation=7;huge.first_frame=4700;huge.end_frame=4800;
    huge.kind=ProviderDiscontinuityKind::displaced;huge.bounded=true;
    huge.displacement_lower_frames=huge.displacement_upper_frames=std::numeric_limits<std::int64_t>::max();
    first.discontinuity=huge;overflow.observe(first);
    auto second=observed(9600,.2L);huge.event=2;huge.first_frame=9500;huge.end_frame=9600;
    huge.displacement_lower_frames=huge.displacement_upper_frames=1;second.discontinuity=huge;
    rejects([&]{overflow.observe(second);});
    check(overflow.events().events==1 && overflow.latest()->sample_counter==4800 &&
        overflow.events().displacement_upper_frames==std::numeric_limits<std::int64_t>::max(),
        "displacement ledger overflow partially committed an event");
}
}
int main() {try {
    declared_capabilities();future_bounds_and_origin();trajectories_and_discontinuities();bounded_event_ledger();
    std::cout<<"Audio-provider capabilities, certified q horizons, immutable counters and bounded events passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
