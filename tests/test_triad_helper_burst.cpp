#include "datapump/triad_helper_burst.hpp"
#include "datapump/triad_stream.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stop_token>
#include <stdexcept>
#include <utility>
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
        check(p.stride_sample_range()==std::pair<std::uint64_t,std::uint64_t>{
                  static_cast<std::uint64_t>(e.expected)*1000,static_cast<std::uint64_t>(e.expected)*1000},
              "fixed Helper interval range changed its exact recurrence");
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

void compact_scheduled_calendar() {
    auto c=config(8,0,false);
    c.compact_stride_seconds=24;
    const HelperBurstCoordinate earliest{epoch+2,1};
    const HelperBurstPlan calendar(c);
    const auto first=calendar.first_slot(earliest);
    check(first.support_begin==HelperBurstCoordinate{epoch+3,0},
          "compact Scheduled calendar did not round to the next UTC second");
    constexpr std::uint64_t index=1'000'001;
    const auto distant=calendar.slot_at(earliest,index);
    check(distant.support_begin==HelperBurstCoordinate{epoch+3+24*index,0} &&
          distant.pattern_begin.phase==100 &&
          calendar.successor(distant)==calendar.slot_at(earliest,index+1),
          "compact Scheduled lookup changed the global index or source phase");
    check(calendar.stride_sample_range()==std::pair<std::uint64_t,std::uint64_t>{24'000,24'000},
          "compact Scheduled interval range lost its exact stride");
    auto unsupported=c;
    unsupported.helper_enabled=true;
    rejects([&]{HelperBurstPlan profile(unsupported);},"compact Scheduled Helper accepted");
    unsupported=c;
    unsupported.wait_bits=1;
    rejects([&]{HelperBurstPlan profile(unsupported);},"compact Scheduled symbol wait accepted");
    unsupported=c;
    unsupported.random_extra_bits=1;
    rejects([&]{HelperBurstPlan profile(unsupported);},"compact Scheduled random wait accepted");
    unsupported=c;
    unsupported.continuous_data=true;
    rejects([&]{HelperBurstPlan profile(unsupported);},"compact Scheduled continuous cadence accepted");
    unsupported=c;
    unsupported.compact_stride_seconds=8;
    rejects([&]{HelperBurstPlan profile(unsupported);},"compact Scheduled stride truncated pulse support");

    PublicProfileConfig configured;
    configured.format.sample_rate=6000;
    configured.format.epoch=epoch;
    configured.format.symbol_ordinal=73;
    configured.format.inner_rate_hz=100;
    configured.format.bit_seconds=8;
    configured.capture_lead_seconds=7.25;
    configured.capture_trail_seconds=.25;
    configured.arrival_low_seconds=-7;
    configured.arrival_high_seconds=7.07;
    configured.receiver_filter_halo_samples=97;
    const PublicProfile ordinary(configured);
    configured.compact_stride_seconds=24;
    const PublicProfile compact(configured);
    const auto slot=compact.slot(5);
    const auto legacy=ordinary.slot(2);
    check(slot.tx.pattern_begin==legacy.tx.pattern_begin && slot.symbol_ordinal==78 &&
          compact.first_slot_not_before(slot.tx.helper_begin)==5 &&
          compact.first_slot_not_before(compact.calendar().add(slot.tx.helper_begin,1))==6,
          "compact Scheduled recurrence changed address geometry or UTC lookup");
    check(slot.observation.first>legacy.observation.first && slot.observation.stop==legacy.observation.stop &&
          slot.quiet_aperture.first>=slot.observation.first && slot.quiet_aperture.stop<=slot.observation.stop &&
          slot.quiet_aperture.stop>slot.quiet_aperture.first,
          "compact Scheduled capture lost its quiet aperture or trailing coverage");
    check(compact.maximum_slot_opportunities_per_day()==3601 && ordinary.maximum_slot_opportunities_per_day()==1441,
          "compact Scheduled cadence bypassed calendar-derived admission");
    configured.scheduled_gap_fill=true;
    rejects([&]{PublicProfile profile(configured);},"compact Scheduled guarded fill accepted");
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

void continuous_data_calendar() {
    auto packed=config(8,0,false);
    packed.bit_samples+=371;
    packed.support_samples+=371;
    packed.continuous_data=true;
    const HelperBurstPlan plan(packed);
    check(plan.stride_sample_range()==std::pair<std::uint64_t,std::uint64_t>{
              packed.bit_samples,packed.bit_samples},
          "continuous DATA interval range changed its exact sample stride");
    const HelperBurstCoordinate earliest{epoch+1,73};
    const auto first=plan.first_slot(earliest);
    check(first.support_begin==HelperBurstCoordinate{epoch+30,0},
          "continuous DATA changed the first UTC anchor");
    check(plan.minimum_stride_samples()==packed.bit_samples,
          "continuous DATA stride changed the active sample count");
    auto current=first;
    for(std::uint64_t i=0;i<128;++i) {
        check(plan.slot_at(earliest,i)==current,"continuous indexing lost the sample phase");
        const auto next=plan.successor(current);
        check(current.pattern_end==next.pattern_begin && current.support_end>next.support_begin,
              "continuous DATA inserted a gap or discarded padded template support");
        current=next;
    }
    constexpr std::uint64_t far_index=1'000'003;
    const auto far=plan.slot_at(earliest,far_index);
    check(plan.distance(first.pattern_begin,far.pattern_begin)==far_index*packed.bit_samples &&
          plan.successor(far)==plan.slot_at(earliest,far_index+1),
          "continuous DATA lost exact spacing at a distant ordinal");
    rejects([&]{plan.slot_at(earliest,std::numeric_limits<std::uint64_t>::max());},
            "continuous DATA accepted a source sample overflow");
    packed.helper_enabled=true;
    rejects([&]{HelperBurstPlan invalid(packed);},"continuous DATA bypassed Helper lead constraints");
    packed.helper_enabled=false;
    packed.wait_bits=1;
    rejects([&]{HelperBurstPlan invalid(packed);},"continuous DATA discarded a fixed wait");
    packed.wait_bits=0;
    packed.random_extra_bits=1;
    rejects([&]{HelperBurstPlan invalid(packed);},"continuous DATA discarded a random wait");

    PublicProfileConfig profile;
    profile.format.sample_rate=6000;
    profile.format.epoch=epoch;
    profile.format.symbol_ordinal=std::numeric_limits<std::uint64_t>::max()-3;
    profile.continuous_data=true;
    profile.capture_lead_seconds=2;
    const PublicProfile clear(profile);
    check(clear.last_slot()==3 && clear.slot(3).symbol_ordinal==std::numeric_limits<std::uint64_t>::max(),
          "continuous DATA ignored public ordinal exhaustion");
    const auto boundary=clear.transmit_slot(2).helper_begin;
    check(clear.first_slot_not_before(boundary)==2 &&
          clear.first_slot_not_before(clear.calendar().add(boundary,1))==3,
          "continuous start lookup rounded off the sample grid");
    check(clear.maximum_slot_opportunities_per_day()>1441,
          "daily presence accounting kept the scheduled minimum stride");
    auto fractional_profile=profile;
    fractional_profile.format.inner_rate_hz=3.6;
    fractional_profile.format.added_symbols=1;
    fractional_profile.format.bit_seconds=160.4;
    const PublicProfile fractional(fractional_profile);
    const double seconds=fractional.geometry().bit_seconds;
    const double binary64_rows=seconds*6000.;
    check(binary64_rows==962400. && std::fma(seconds,6000.,-binary64_rows)>0 &&
          fractional.calendar().minimum_stride_samples()==962401,
          "continuous DATA rounded a fractional tail sample into the neighboring slot");
    profile.arrival_low_seconds=-.5;
    profile.arrival_high_seconds=.5;
    profile.receiver_filter_halo_samples=17;
    const PublicProfile wide(profile);
    const auto stationary=clear.slot(1),crowded=wide.slot(1);
    check(crowded.observation.first<stationary.observation.first &&
          crowded.observation.stop>stationary.observation.stop &&
          crowded.quiet_aperture.size()<stationary.quiet_aperture.size(),
          "continuous receive window or quiet aperture ignored adjacent signal uncertainty");
    check(crowded.quiet_aperture.first==crowded.previous_signal_stop &&
          crowded.quiet_aperture.stop==crowded.next_signal_first,
          "continuous quiet aperture extended into a modeled neighboring DATA tail");
    profile.format.epoch=(std::uint64_t{1}<<32)-500;
    profile.format.symbol_ordinal=0;
    const PublicProfile near_end(profile);
    const auto last=near_end.last_slot();
    (void)near_end.transmit_slot(last);
    rejects([&]{near_end.transmit_slot(last+1);},"continuous recurrence exceeded the keyed epoch limit");
}

void continuous_emission_boundaries() {
    PublicProfileConfig config;
    config.format.sample_rate=1500;
    config.format.epoch=epoch;
    config.format.inner_rate_hz=120;
    config.format.bit_seconds=1.2;
    config.data_if_hz=375;
    config.continuous_data=true;
    const PublicProfile profile(config);
    const auto key=datapump::Crypto::amateur();
    const auto offset=[&](HelperBurstCoordinate point) {
        return profile.calendar().distance(profile.origin(),point);
    };
    const auto first=profile.slot(0),second=profile.slot(1);
    const auto start=offset(first.tx.pattern_begin),boundary=offset(second.tx.pattern_begin);
    const auto stop=offset(second.tx.pattern_end),end=offset(second.tx.support_end);
    check(boundary==offset(first.tx.pattern_end) && boundary-start==stop-boundary,
          "continuous emitter fixture has an inter-bit gap");
    check(end<65536,"continuous emitter fixture exceeds its sample budget");
    constexpr std::size_t chunks=997;
    for(std::uint8_t a:{0,1})for(std::uint8_t b:{0,1}) {
        const std::array bits{a,b};
        Emission direct(profile,key,EmissionOptions{.bits=bits});
        std::array<unsigned,2> resolved{};
        Emission staged(profile,key,EmissionOptions{.slot_count=2,.bit_at=[&](auto i) {
            ++resolved.at(i);
            return bits.at(i);
        }});
        auto source=staged.source();
        check(source.geometry.source_samples==end,"continuous DATA source retained intra-message padding");
        std::vector<Complex> block(end);
        for(std::size_t i=0;i<block.size();) {
            const auto count=std::min(chunks,block.size()-i);
            check(direct.read(std::span(block).subspan(i,count))==count,
                  "continuous DATA ended before its final support");
            i+=count;
        }
        PatternSource first_pattern(profile.basis_format(first),key,a);
        PatternSource second_pattern(profile.basis_format(second),key,b);
        const auto gain=direct.report().data_gain;
        for(std::uint64_t i=0;i<end;++i) {
            Complex expected{};
            if(i>=start && i<boundary)expected=first_pattern.sample_at(i-offset(first.tx.support_begin))*gain;
            if(i>=boundary && i<stop)expected=second_pattern.sample_at(i-offset(second.tx.support_begin))*gain;
            check(std::abs(block[i]-expected)<1e-14,
                  "continuous DATA changed the addressed waveform, taper, or edge silence");
        }
        check(direct.report().data_energy>0 && block[boundary]==Complex{},
              "continuous DATA lost its intended boundary taper or active energy");
        source.readers->open(2,chunks);
        rejects([&]{source.readers->plan(0,chunks+1);},"staged DATA exceeded its admitted sample count");
        std::exception_ptr error;
        for(std::uint64_t i=0;i<end;) {
            const auto count=std::min<std::uint64_t>(chunks,end-i);
            source.readers->plan(i,count);
            for(std::uint64_t j=0;j<count;++j)
                check(source.readers->evaluate(j%2,i+j)==block[i+j],
                      "staged continuous source changed samples across a slot transition");
            check(source.readers->accept(count,false,error)==count && !error,
                  "staged continuous source rejected valid samples");
            i+=count;
        }
        check(resolved==std::array<unsigned,2>{1,1},
              "staged continuous source resolved a DATA bit once per halo instead of once per slot");
        check(staged.report().data_energy==direct.report().data_energy &&
              staged.report().generated_samples==direct.report().generated_samples,
              "staged continuous source changed energy or cursor accounting");
    }
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
        const auto [minimum_stride,maximum_stride]=p.stride_sample_range();
        check(minimum_stride==*std::min_element(strides.begin(),strides.begin()+extra+1)*c.sample_rate &&
              maximum_stride==*std::max_element(strides.begin(),strides.begin()+extra+1)*c.sample_rate,
              "random Helper interval range omitted a configured UTC-rounded wait");
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

void scheduled_gap_fill_support() {
    PublicProfileConfig config;
    config.format.sample_rate=6000;
    config.format.inner_rate_hz=3.6;
    config.format.added_symbols=1;
    config.format.minimum_carrier_spacing_hz=265;
    config.format.bit_seconds=160.4;
    config.format.epoch=epoch;
    config.format.symbol_ordinal=std::numeric_limits<std::uint64_t>::max()-2;
    config.capture_lead_seconds=7.25;
    config.capture_trail_seconds=.25;
    config.arrival_low_seconds=-7;
    config.arrival_high_seconds=7.07;
    config.spread_delay_ms=21;
    config.selective_signal_tail=true;
    config.receiver_filter_halo_samples=97;
    const PublicProfile scheduled(config);
    config.scheduled_gap_fill=true;
    const PublicProfile filled(config);
    check(!scheduled.gap_fill_after(0) && !filled.gap_fill_after(2),
          "gap fill escaped the selected mode or public calendar");

    const auto rate=filled.effective_sample_rate();
    const auto source_rows=[&](HelperBurstCoordinate coordinate) {
        return filled.calendar().distance(filled.origin(),coordinate);
    };
    const auto active_rows=static_cast<std::uint64_t>(std::ceil(filled.geometry().bit_seconds*rate));
    const auto lower_offset=rate*(config.capture_lead_seconds+config.arrival_low_seconds);
    const auto upper_offset=rate*(config.capture_lead_seconds+config.arrival_high_seconds+
        config.spread_delay_ms/1000+2/(3*filled.geometry().nominal_carrier_bandwidth_hz));
    const auto halo=config.receiver_filter_halo_samples;
    const auto first_arrival=[&](std::uint64_t source) { return source+lower_offset-32-halo; };
    const auto stop_arrival=[&](std::uint64_t source) { return source+upper_offset+32+halo; };
    for (std::uint64_t index=0; index<2; ++index) {
        const auto prev=filled.slot(index), next=filled.slot(index+1);
        const auto prev_scheduled=scheduled.slot(index), next_scheduled=scheduled.slot(index+1);
        check(prev.tx==prev_scheduled.tx && next.tx==next_scheduled.tx &&
              prev.observation.first==prev_scheduled.observation.first &&
              prev.observation.stop==prev_scheduled.observation.stop &&
              next.observation.first==next_scheduled.observation.first &&
              next.observation.stop==next_scheduled.observation.stop,
              "gap fill changed the scheduled transmit or receive calendar");
        const auto fill=filled.gap_fill_after(index);
        check(fill && fill->size()>18*rate && fill->size()<23*rate,
              "broad scheduled profile lost its usable central gap");
        const auto previous_end=source_rows(prev.tx.pattern_begin)+active_rows;
        const auto next_begin=source_rows(next.tx.pattern_begin);
        check(fill->first>previous_end && fill->stop<next_begin &&
              std::floor(first_arrival(fill->first))>=std::ceil(stop_arrival(previous_end))-1 &&
              std::ceil(stop_arrival(fill->stop))<=std::floor(first_arrival(next_begin))+1,
              "gap fill reached a wanted gate after arrival, channel and FIR bounds");
        check(prev.gap_fill_may_enter_observation && next.gap_fill_may_enter_observation &&
              prev.protected_data_aperture.stop+1>=std::ceil(stop_arrival(previous_end)) &&
              next.protected_data_aperture.first<=std::floor(first_arrival(next_begin))+1 &&
              prev.protected_data_aperture.stop<=std::floor(first_arrival(fill->first))+1 &&
              next.protected_data_aperture.first+1>=std::ceil(stop_arrival(fill->stop)),
              "gap fill crop dropped reachable wanted DATA or admitted possible fill");
    }

    auto widened_config=filled.config();
    widened_config.capture_lead_seconds+=.75;
    widened_config.arrival_low_seconds-=.75;
    widened_config.arrival_high_seconds+=.75;
    widened_config.receiver_filter_halo_samples+=47;
    const PublicProfile widened(widened_config);
    auto widened_baseline_config=widened_config;
    widened_baseline_config.scheduled_gap_fill=false;
    const auto widened_quiet=PublicProfile(widened_baseline_config).slot(1).quiet_aperture;
    const auto base=filled.slot(1), local=widened.slot(1);
    for (const auto index : {0,1}) {
        const auto original=filled.gap_fill_after(index), local_range=widened.gap_fill_after(index);
        check(original && local_range && original->first==local_range->first && original->stop==local_range->stop,
              "receiver-only capture widening moved the public source fill");
    }
    check(local.protected_data_aperture.first>base.protected_data_aperture.first &&
          local.protected_data_aperture.stop<base.protected_data_aperture.stop &&
          local.protected_data_aperture.first<=local.quiet_aperture.first &&
          local.quiet_aperture.stop<=local.protected_data_aperture.stop &&
          local.quiet_aperture.first>=widened_quiet.first && local.quiet_aperture.stop<=widened_quiet.stop,
          "local capture widening did not conservatively exclude the unchanged fill");

    auto transport_config=filled.config();
    constexpr std::uint64_t transport_halo=240;
    transport_config.gap_fill_transport_halo_samples=transport_halo;
    const PublicProfile with_transport(transport_config);
    const auto transported=with_transport.slot(1);
    const auto base_fill=filled.gap_fill_after(1), transported_fill=with_transport.gap_fill_after(1);
    check(base_fill && transported_fill && base_fill->first==transported_fill->first &&
          base_fill->stop==transported_fill->stop && transported.observation.first==base.observation.first &&
          transported.observation.stop==base.observation.stop &&
          transported.protected_data_aperture.first==base.protected_data_aperture.first+transport_halo &&
          transported.protected_data_aperture.stop+transport_halo==base.protected_data_aperture.stop,
          "transport support was ignored or changed the public source or capture");

    // An endpoint with tighter local bounds can receive the same source
    // calendar while its entire observation lies outside possible fill.
    auto narrow_config=filled.config();
    narrow_config.capture_lead_seconds=.25;
    narrow_config.arrival_low_seconds=-.01;
    narrow_config.arrival_high_seconds=.01;
    const PublicProfile narrow(narrow_config);
    narrow_config.scheduled_gap_fill=false;
    const auto ordinary_narrow=PublicProfile(narrow_config).slot(1), filled_narrow=narrow.slot(1);
    check(narrow.gap_fill_after(0) && narrow.gap_fill_after(1) &&
          !filled_narrow.gap_fill_may_enter_observation && filled_narrow.protected_data_aperture.size()==0 &&
          filled_narrow.quiet_aperture.first==ordinary_narrow.quiet_aperture.first &&
          filled_narrow.quiet_aperture.stop==ordinary_narrow.quiet_aperture.stop,
          "fill outside the complete capture modified ordinary receive support");

    config.arrival_low_seconds=-100;
    config.arrival_high_seconds=100;
    config.capture_lead_seconds=100.25;
    const PublicProfile crowded(config);
    check(!crowded.gap_fill_after(0) && !crowded.slot(1).gap_fill_may_enter_observation &&
          crowded.slot(1).protected_data_aperture.size()==0,
          "covered DATA arrivals left a putative gap fill interval");
}

void scheduled_fill_in_distant_receive_window() {
    PublicProfileConfig config;
    config.format.sample_rate=6000;
    config.format.inner_rate_hz=3.6;
    config.format.added_symbols=1;
    config.format.minimum_carrier_spacing_hz=265;
    config.format.bit_seconds=160.4;
    config.format.epoch=epoch;
    config.format.symbol_ordinal=91;
    config.capture_lead_seconds=15.25;
    config.capture_trail_seconds=.25;
    config.arrival_low_seconds=-15;
    config.arrival_high_seconds=15;
    config.spread_delay_ms=21;
    config.selective_signal_tail=true;
    config.random_extra_bits=1;
    config.scheduled_gap_fill=true;
    const PublicProfile source(config);
    constexpr double widening=105;
    auto local_config=source.config();
    local_config.capture_lead_seconds+=widening;
    local_config.arrival_low_seconds-=widening;
    local_config.arrival_high_seconds+=widening;
    local_config.receiver_filter_halo_samples=97;
    local_config.gap_fill_transport_halo_samples=240;
    const PublicProfile local(local_config);
    const auto rate=local.effective_sample_rate();
    const auto offset_first=rate*(local_config.capture_lead_seconds+local_config.arrival_low_seconds);
    const auto offset_stop=rate*(local_config.capture_lead_seconds+local_config.arrival_high_seconds+
        local_config.spread_delay_ms/1000+2/(3*local.geometry().nominal_carrier_bandwidth_hz));
    const auto halo=local_config.receiver_filter_halo_samples+local_config.gap_fill_transport_halo_samples;

    bool earlier_witness=false, later_witness=false;
    for (std::uint64_t index=2; index<64 && (!earlier_witness || !later_witness); ++index) {
        if (source.gap_fill_after(index-1) || source.gap_fill_after(index)) continue;
        const auto previous_gap=source.gap_fill_after(index-2), next_gap=source.gap_fill_after(index+1);
        const auto original=source.slot(index), slot=local.slot(index);
        const auto overlaps=[&](const std::optional<SampleRange>& gap) {
            return gap && std::floor(gap->first+offset_first-32-halo)<slot.observation.stop &&
                std::ceil(gap->stop+offset_stop+32+halo)>slot.observation.first;
        };
        const bool earlier=overlaps(previous_gap), later=overlaps(next_gap);
        if (!earlier && !later) continue;
        check(!original.gap_fill_may_enter_observation && slot.gap_fill_may_enter_observation,
              "local capture missed possible fill beyond its two adjacent gaps");
        check(slot.protected_data_aperture.size()>0 &&
              slot.protected_data_aperture.first>=slot.previous_signal_stop &&
              slot.protected_data_aperture.stop<=slot.next_signal_first &&
              slot.quiet_aperture.first>=slot.protected_data_aperture.first &&
              slot.quiet_aperture.stop<=slot.protected_data_aperture.stop,
              "adjacent DATA bounds did not protect the distant-fill aperture");
        earlier_witness=earlier_witness || earlier;
        later_witness=later_witness || later;
    }
    check(earlier_witness && later_witness,"deterministic waits did not expose both distant fill cases");
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
        grid_and_geometry();waits_and_quiet_intervals();compact_scheduled_calendar();protected_tail();
        indexed_public_calendar();continuous_data_calendar();continuous_emission_boundaries();
        random_extra_waits();public_epoch_and_ordinal_boundaries();
        emission_modes_and_cancellation();centered_receiver_filter_support();scheduled_gap_fill_support();
        scheduled_fill_in_distant_receive_window();rejection_boundaries();
        std::cout<<"triad Helper whole-bit calendar checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
