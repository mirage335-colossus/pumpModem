#include "datapump/triad_helper_burst.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump::triad::v3;
constexpr std::uint64_t epoch=1710000000;
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation,const char* message) {
    try {operation();} catch(const datapump::Error&) {return;}
    throw std::runtime_error(message);
}
HelperBurstConfig config(std::uint64_t bit_seconds=8,unsigned wait=0,bool helper=true) {
    return {1000,bit_seconds*1000,bit_seconds*1000+200,100,wait,helper,0};
}

void grid_and_geometry() {
    HelperBurstPlan p(config());
    const auto first=p.first_slot({epoch,0});
    check(first.support_begin==HelperBurstCoordinate{epoch,0},"exact UTC grid point advanced");
    check(first.helper_begin==HelperBurstCoordinate{epoch-38,0},"fresh lead does not precede full DATA support");
    check(first.pattern_begin==HelperBurstCoordinate{epoch,100},"pulse halo lost fractional UTC address");
    check(first.pattern_end==HelperBurstCoordinate{epoch+8,100},"logical duration changed");
    check(first.support_end==HelperBurstCoordinate{epoch+8,200},"trailing pulse support omitted");
    check(p.first_slot({epoch,1}).support_begin==HelperBurstCoordinate{epoch+30,0},
          "fractional earliest support rounded backwards");
    check(p.first_slot({epoch-1,999}).support_begin==HelperBurstCoordinate{epoch,0},
          "near-grid earliest support rounded incorrectly");
    check(p.first_slot({0,0}).helper_begin==HelperBurstCoordinate{22,0},
          "first representable Helper lead underflowed UTC");
    check(p.successor(first).support_begin==HelperBurstCoordinate{epoch+90,0},
          "76-second Helper minimum was not rounded on UTC30 grid");
    check(p.distance(first.helper_begin,first.support_begin)==38000,"lead is not exactly38seconds");
    check(p.relative_seconds(first.helper_begin,first.pattern_begin)==38.1L,
          "local Generator coordinate changed fractional sample phase");
    const auto q=p.add({epoch,999},1002);
    check(q==HelperBurstCoordinate{epoch+2,1} && p.distance({epoch,999},q)==1002,
          "checked integer addition/borrow changed sample count");

    // The absolute epoch may exceed floating-point integer resolution. All
    // source-grid/key coordinates retain their integer epoch and sample phase.
    const auto distant=(std::uint64_t{1}<<60)/30*30;
    auto c=config();c.leading_halo_samples=17;c.support_samples=c.bit_samples+34;
    HelperBurstPlan far(c);
    const auto slot=far.first_slot({distant,0});
    check(slot.pattern_begin==HelperBurstCoordinate{distant,17},"distant UTC lost sample phase");
    check(far.relative_seconds(slot.support_begin,slot.pattern_begin)==.017L,
          "local duration used absolute floating UTC");
}

void waits_and_quiet_intervals() {
    struct Example {unsigned wait;std::uint64_t expected;};
    for(const auto e:std::array<Example,3>{{{0,90},{1,120},{3,210}}}) {
        HelperBurstPlan p(config(40,e.wait));
        const auto first=p.first_slot({epoch,0}),second=p.successor(first);
        check(second.index==1 && second.support_begin.epoch==epoch+e.expected,
              "whole-bit wait recurrence changed");
        check(p.distance(first.helper_end,second.helper_begin)>=e.wait*p.config().bit_samples,
              "next Helper lead consumed the receiver's quiet processing interval");
        check(p.distance(second.helper_begin,second.support_begin)==38000,
              "successor lost fresh Helper lead");
    }
    auto c=config(38*86400,3);
    HelperBurstPlan p(c);
    const auto first=p.first_slot({epoch,0}),second=p.successor(first);
    check(p.distance(first.helper_end,second.helper_begin)>=3*c.bit_samples,
          "multi-day wait overflowed or truncated");
    check(second.support_begin.epoch%30==0 && second.support_begin.phase==0,
          "multi-day successor left UTC30 grid");

    // Without Helper, preserve the ordinary complete-symbol recurrence rather
    // than imposing the assistance floor or a phantom lead/tail.
    for(const auto e:std::array<Example,3>{{{0,60},{1,90},{3,180}}}) {
        HelperBurstPlan plain(config(40,e.wait,false));
        const auto a=plain.first_slot({epoch,0}),b=plain.successor(a);
        check(b.support_begin.epoch==epoch+e.expected,"Helper-disabled recurrence changed");
        check(a.helper_begin==a.support_begin && a.helper_end==a.support_end,
              "Helper-disabled calendar introduced Helper airtime");
    }
}

void protected_tail() {
    auto c=config();c.leading_halo_samples=80;c.support_samples=8160;
    HelperBurstPlan p(c);
    auto s=p.first_slot({epoch,0});
    const auto reference=datapump::helper::protected_endpoint(s.helper_begin.epoch,0,
        p.relative_seconds(s.helper_begin,s.support_end));
    const auto actual=p.relative_seconds(s.helper_begin,s.helper_end);
    check(actual>=reference-1e-14L && actual<=reference+.001L+1e-14L,
          "calendar failed production punctuation protection or added excessive tail");
    check(p.distance(s.support_end,s.helper_end)>=32 &&
          p.distance(s.support_end,s.helper_end)<=33,"punctuation was cut or a new mandatory tail was added");
    c.helper_tail_samples=2000;
    HelperBurstPlan with_tail(c);s=with_tail.first_slot({epoch,0});
    check(with_tail.distance(s.support_end,s.helper_end)>=2032 &&
          with_tail.distance(s.support_end,s.helper_end)<=2033,
          "caller-supplied existing tail was not retained");
    // At the exact protected end no extension is needed.
    c.helper_tail_samples=0;c.support_samples=8192;
    HelperBurstPlan exact(c);s=exact.first_slot({epoch,0});
    check(s.helper_end==s.support_end,"exact protected endpoint was extended");
}

void rejection_boundaries() {
    auto c=config();c.sample_rate=0;
    rejects([&]{HelperBurstPlan p(c);},"zero sample clock accepted");
    c=config();c.leading_halo_samples=c.support_samples;
    rejects([&]{HelperBurstPlan p(c);},"logical bit extends beyond full support");
    c=config();c.bit_samples=0;
    rejects([&]{HelperBurstPlan p(c);},"empty logical bit accepted");
    c=config();c.helper_enabled=false;c.helper_tail_samples=1;
    rejects([&]{HelperBurstPlan p(c);},"phantom disabled Helper tail accepted");
    c=config();c.bit_samples=std::numeric_limits<std::uint64_t>::max();
    c.support_samples=c.bit_samples;c.leading_halo_samples=0;c.wait_bits=2;
    rejects([&]{HelperBurstPlan p(c);},"recurring wait multiplication overflow accepted");
    HelperBurstPlan p(config());
    rejects([&]{p.first_slot({epoch,1000});},"out-of-range sample phase accepted");
    rejects([&]{p.distance({epoch,1},{epoch,0});},"negative sample distance accepted");
    rejects([&]{p.relative_seconds({epoch,1},{epoch,0});},"negative local Generator time accepted");
    rejects([&]{p.first_slot({std::numeric_limits<std::uint64_t>::max(),1});},
            "grid rounding overflow accepted");
    rejects([&]{p.distance({0,0},{std::numeric_limits<std::uint64_t>::max(),0});},
            "unrepresentable sample distance accepted");
    auto s=p.first_slot({epoch,0});s.pattern_begin.phase++;
    rejects([&]{p.successor(s);},"mutated slot bypassed calendar validation");
    s=p.first_slot({epoch,0});s.index=std::numeric_limits<std::uint64_t>::max();
    rejects([&]{p.successor(s);},"slot ordinal overflow accepted");
}
} // namespace

int main() {
    try {
        grid_and_geometry();waits_and_quiet_intervals();protected_tail();rejection_boundaries();
        std::cout<<"triad Helper whole-bit calendar checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
