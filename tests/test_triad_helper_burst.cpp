#include "datapump/triad_helper_burst.hpp"
#include "datapump/triad_stream.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stop_token>
#include <stdexcept>
#include <vector>

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

void indexed_public_calendar() {
    // Compare direct indexing with the original recurrence across Helper
    // punctuation, fractional halos, waits, and long-bit geometries.
    for(bool helper:{false,true})for(unsigned wait:{0U,1U,3U})
        for(std::uint64_t duration:{8ULL,40ULL,38ULL*86400}) {
            auto c=config(duration,wait,helper);
            c.leading_halo_samples=17;
            c.support_samples=c.bit_samples+193;
            c.helper_tail_samples=helper?37:0;
            HelperBurstPlan p(c);
            const HelperBurstCoordinate earliest{epoch+1,73};
            auto current=p.first_slot(earliest);
            for(std::uint64_t i=0;i<128;++i) {
                check(p.slot_at(earliest,i)==current,"indexed calendar differs from successors");
                current=p.successor(current);
            }
            const auto first=p.slot_at(earliest,0),next=p.slot_at(earliest,1);
            const auto distant=p.slot_at(earliest,1000000);
            check(distant.index==1000000 &&
                  distant.support_begin.epoch-first.support_begin.epoch==
                    1000000*(next.support_begin.epoch-first.support_begin.epoch),
                  "distant indexed public slot lost its constant stride");
            check(distant.pattern_begin.phase==first.pattern_begin.phase,
                  "indexed public slot lost fractional pulse phase");
        }
    HelperBurstPlan p(config());
    rejects([&]{p.slot_at({epoch,0},std::numeric_limits<std::uint64_t>::max());},
            "indexed public calendar multiplication overflow accepted");
    rejects([&]{p.slot_at({std::numeric_limits<std::uint64_t>::max()-100,0},2);},
            "indexed public calendar addition overflow accepted");
}

void random_extra_waits() {
    // A 40-second bit distinguishes all wait choices after UTC30 rounding.
    // Two full cycles check the equal-count contract and independent orders.
    constexpr std::uint64_t cycle_slots = 252;
    const HelperBurstCoordinate earliest{epoch,0};
    for(bool helper:{false,true})for(unsigned extra:{1U,2U,3U}) {
        auto c=config(40,1,helper);
        c.random_extra_bits=extra;
        c.random_anchor_epoch=epoch;
        c.random_initial_ordinal=19;
        const HelperBurstPlan p(c);
        std::array<std::uint64_t,4> strides{};
        for(unsigned i=0;i<=extra;++i) {
            const HelperBurstPlan fixed(config(40,1+i,helper));
            const auto first=fixed.first_slot(earliest);
            strides[i]=fixed.successor(first).support_begin.epoch-first.support_begin.epoch;
        }
        auto current=p.first_slot(earliest);
        std::array<unsigned,cycle_slots> first_cycle{};
        bool different_second_cycle=false;
        for(std::uint64_t cycle=0;cycle<2;++cycle) {
            std::array<unsigned,4> counts{};
            for(std::uint64_t i=0;i<cycle_slots;++i) {
                check(p.slot_at(earliest,cycle*cycle_slots+i)==current,"random indexed slot differs from its successor");
                const auto next=p.successor(current);
                const auto stride=next.support_begin.epoch-current.support_begin.epoch;
                const auto choice=std::find(strides.begin(),strides.begin()+extra+1,stride);
                check(choice!=strides.begin()+extra+1,"random wait left configured UTC-aligned strides");
                const auto index=static_cast<unsigned>(choice-strides.begin());
                ++counts[index];
                check(p.distance(current.helper_end,next.helper_begin)>=(1+index)*c.bit_samples,
                    "random wait did not preserve its real quiet gap");
                if(cycle==0)first_cycle[i]=index;
                else different_second_cycle|=first_cycle[i]!=index;
                current=next;
            }
            for(unsigned i=0;i<=extra;++i)
                check(counts[i]==cycle_slots/(extra+1),"random wait cycle changed its invariant total stride");
        }
        check(different_second_cycle,"random wait sequence repeated across cycles");

        constexpr std::uint64_t distant=1'000'000*cycle_slots+73;
        const auto far=p.slot_at(earliest,distant);
        check(p.successor(far)==p.slot_at(earliest,distant+1),"distant random slot lost exact successor geometry");
        const auto changed_anchor=HelperBurstPlan([&]{auto other=c;other.random_anchor_epoch+=30;return other;}());
        std::array<std::uint8_t,32> alternate_root{1};
        const HelperBurstPlan changed_key(c,datapump::Crypto(alternate_root));
        bool anchor_changes=false,key_changes=false;
        for(unsigned i=1;i<32;++i) {
            const auto expected=p.slot_at(earliest,i);
            anchor_changes|=expected!=changed_anchor.slot_at(earliest,i);
            key_changes|=expected!=changed_key.slot_at(earliest,i);
        }
        check(anchor_changes&&key_changes,"random wait seed ignored configured anchor or key");
        rejects([&]{p.slot_at(earliest,std::numeric_limits<std::uint64_t>::max());},
            "random calendar overflow accepted");
    }
}

void public_epoch_and_ordinal_boundaries() {
    PublicProfileConfig c;
    c.format.sample_rate=6000;
    c.format.epoch=epoch;
    c.format.symbol_ordinal=std::numeric_limits<std::uint64_t>::max()-2;
    c.wait_bits=1;
    c.random_extra_bits=3;
    const PublicProfile p(c);
    check(p.last_slot()==2,"public calendar ignored ordinal exhaustion");
    const auto last=p.slot(2);
    check(last.symbol_ordinal==std::numeric_limits<std::uint64_t>::max(),"configured ordinal was reset");
    check(last.quiet_aperture.first>=last.observation.first&&last.quiet_aperture.stop<=last.observation.stop,
        "last public quiet aperture is outside its observation");
    const auto exact=last.tx.helper_begin;
    check(p.first_slot_not_before(exact)==2,"calendar scheduler advanced an exact Helper boundary");
    rejects([&]{p.first_slot_not_before(p.calendar().add(exact,1));},"calendar scheduler accepted a past-end slot");
    rejects([&]{p.slot(3);},"public ordinal exhaustion accepted");

    c.format.epoch=(std::uint64_t{1}<<32)-500;
    c.format.symbol_ordinal=9;
    const PublicProfile near_epoch_limit(c);
    const auto end=near_epoch_limit.last_slot();
    check(near_epoch_limit.transmit_slot(end).helper_end.epoch<(std::uint64_t{1}<<32),
        "final supported slot crossed Helper metadata range");
    rejects([&]{near_epoch_limit.transmit_slot(end+1);},"calendar allowed source past supported epoch");
}

void emission_modes_and_cancellation() {
    PublicProfileConfig c;
    c.format.sample_rate=6000;
    c.format.epoch=epoch;
    c.format.inner_rate_hz=100;
    c.format.added_symbols=1;
    c.format.bit_seconds=8;
    c.format.encrypt=false;
    const PublicProfile plain(c);
    const auto key=datapump::Crypto::amateur();
    EmissionOptions request;
    request.mode=EmissionMode::Noise;
    std::stop_source stop;
    request.stop=stop.get_token();
    Emission noise(plain,key,request),other_noise(plain,key,request);
    check(noise.source().format.encrypt&&other_noise.source().format.encrypt,
        "Noise did not force encryption independently of the selected profile");
    constexpr auto chunk=8192;
    std::vector<Complex> first(chunk),second(chunk);
    check(noise.read(first)==chunk&&other_noise.read(second)==chunk&&first!=second,
        "independent Noise transmissions reused the same emitted samples");
    check(noise.report().data_energy>0,"Noise emitted no DATA energy");
    stop.request_stop();
    rejects([&]{noise.read(first);},"cancelled emission kept generating samples");
    check(noise.report().generated_samples==chunk,"cancellation changed the emitted cursor");

    request.stop={};
    request.mode=EmissionMode::HelperOnly;
    rejects([&]{Emission disabled(plain,key,request);},"Helper-only was accepted with Helper off");
    c.helper_enabled=true;
    c.helper.power_w=c.helper.data_power_w+1;
    c.helper.callsign="TRIAD";
    const PublicProfile assisted(c);
    request.source_first_sample=assisted.calendar().distance(assisted.origin(),assisted.transmit_slot(0).helper_begin);
    Emission helper(assisted,key,request);
    check(helper.read(first)==chunk&&helper.report().helper_energy>0&&helper.report().data_energy==0,
        "Helper-only playback did not preserve a Helper-only source");
    check(helper.report().first_data_sample==std::uint64_t{helper_lead_seconds}*c.format.sample_rate,
        "local playback changed the configured lead or first DATA coordinate");
}

void centered_receiver_filter_support() {
    PublicProfileConfig config;
    config.format.sample_rate=6000;
    config.format.epoch=epoch;
    const PublicProfile unfiltered(config);
    constexpr std::uint64_t halo=97;
    config.receiver_filter_halo_samples=halo;
    const PublicProfile filtered(config);
    const auto clear=unfiltered.slot(1),widened=filtered.slot(1);
    check(widened.observation.first+halo==clear.observation.first &&
          widened.observation.stop==clear.observation.stop+halo,
          "centered filter did not grow both sides of the capture");
    check(widened.quiet_aperture.first==clear.quiet_aperture.first &&
          widened.quiet_aperture.stop==clear.quiet_aperture.stop,
          "extra filter support qualified additional quiet samples");
    const auto start=filtered.slot(0);
    check(start.observation.first==0 && start.previous_signal_stop==0,
          "filter support underflowed the capture origin or added a predecessor");

    // A wide public arrival range lets the preceding and following signal
    // envelopes both bind the aperture, without selecting any realized path.
    const auto gap=unfiltered.calendar().relative_seconds(
        unfiltered.transmit_slot(0).helper_end,unfiltered.transmit_slot(1).helper_begin);
    config.arrival_high_seconds=static_cast<double>(gap*.6L);
    config.arrival_low_seconds=-config.arrival_high_seconds;
    config.capture_lead_seconds=config.arrival_high_seconds+1;
    const auto crowded=PublicProfile(config).slot(1);
    config.receiver_filter_halo_samples=0;
    const auto nominal=PublicProfile(config).slot(1);
    check(nominal.quiet_aperture.first==nominal.previous_signal_stop &&
          nominal.quiet_aperture.stop==nominal.next_signal_first,
          "fixture did not bind both neighboring quiet limits");
    check(crowded.quiet_aperture.first==nominal.quiet_aperture.first+halo &&
          crowded.quiet_aperture.stop+halo==nominal.quiet_aperture.stop,
          "centered filter tails remained in the quiet aperture");
    check(crowded.data_support.first==nominal.data_support.first &&
          crowded.data_support.stop==nominal.data_support.stop,
          "receiver filter changed current DATA core geometry");
    config.receiver_filter_halo_samples=std::numeric_limits<std::uint64_t>::max();
    rejects([&]{PublicProfile(config).slot(1);},"filter support overflow accepted");
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
        grid_and_geometry();waits_and_quiet_intervals();protected_tail();
        indexed_public_calendar();random_extra_waits();public_epoch_and_ordinal_boundaries();
        emission_modes_and_cancellation();centered_receiver_filter_support();rejection_boundaries();
        std::cout<<"triad Helper whole-bit calendar checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
