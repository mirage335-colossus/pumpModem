#include "resource_test_margin.hpp"
#include "datapump/symbol_burst.hpp"
#include "datapump/symbol_schedule.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

// Standalone-test allocation probe, as in the scheduled capture layout tests.
// Cached per-sample calendar lookup must not regenerate a crypto byte stream.
namespace {std::atomic<std::size_t> allocations{0};}
void* operator new(std::size_t size) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* result=std::malloc(size?size:1))return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* pointer) noexcept {std::free(pointer);}
void operator delete[](void* pointer) noexcept {std::free(pointer);}
void operator delete(void* pointer,std::size_t) noexcept {std::free(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {std::free(pointer);}

using namespace datapump;
using namespace datapump::modem;

namespace {
constexpr std::uint64_t epoch=1710000000;
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation,const char* message) {
    try {operation();} catch(const Error&) {return;}
    throw std::runtime_error(message);
}
Crypto test_key() {
    std::array<std::uint8_t,32> master{};
    for(unsigned i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    return Crypto(master);
}
Config config(double seconds=1,unsigned wait=1,bool shaped=false) {
    Config c;c.sample_rate=8000;c.bandwidth_hz=160;c.carrier_hz=1500;
    c.integration_seconds=seconds;c.pulse_shaping=shaped;c.symbol_burst={wait,0};
    return c;
}
Bytes from_hex(std::string_view text) {
    const auto digit=[](char c)->unsigned {
        if(c>='0' && c<='9')return static_cast<unsigned>(c-'0');
        if(c>='a' && c<='f')return static_cast<unsigned>(c-'a')+10;
        throw std::runtime_error("invalid hex fixture");
    };
    Bytes result;
    for(std::size_t i=0;i<text.size();i+=2)
        result.push_back(static_cast<std::uint8_t>((digit(text[i])<<4)|digit(text[i+1])));
    return result;
}

void test_policy_and_disabled_compatibility() {
    for(const auto wait:{0U,1U,2U,3U,5U,10U})for(unsigned random=0;random<=3;++random) {
        auto c=config(1,wait);c.symbol_burst.random_limit=random;validate(c);
        check(c.symbol_burst.enabled()==(wait!=0),"Burst enabled state changed");
    }
    auto c=config();c.symbol_burst.wait_symbols=4;
    rejects([&]{validate(c);},"unsupported minimum wait accepted");
    c.symbol_burst={0,4};
    rejects([&]{validate(c);},"invalid disabled random preference accepted");
    for(const auto value:{"off","1","2","3","5","10"})
        check(parse_symbol_burst_wait(value)==(value==std::string_view("off")?0U:
            value==std::string_view("10")?10U:static_cast<unsigned>(value[0]-'0')),
            "minimum wait parser changed");
    for(const auto value:{"0","1","2","3"})
        check(parse_symbol_burst_random(value)==static_cast<unsigned>(value[0]-'0'),
              "random wait parser changed");
    for(const auto value:{"0","4","01","OFF","","-1"})
        rejects([&]{(void)parse_symbol_burst_wait(value);},"invalid wait spelling accepted");
    for(const auto value:{"off","4","01","","-1"})
        rejects([&]{(void)parse_symbol_burst_random(value);},"invalid random spelling accepted");

    c=config(.125,0);c.symbol_burst.random_limit=3;c.stream_phase_samples=137;
    validate(c);
    rejects([&]{SymbolBurstPlan disabled(c);},"disabled calendar constructed");
    const auto address=symbol_stream_address(c,epoch,137,3);
    check(address.epoch==epoch && address.sample_in_second==3137 && address.ordinal==3,
          "disabled mode changed independent contiguous address");
    const auto later=symbol_stream_address(c,epoch,137,8);
    check(later.epoch==epoch+1 && later.sample_in_second==137 && later.ordinal==0,
          "disabled mode changed next-second address");
    check(pattern_transmission_sample_count(c,3,false)==3000,
          "disabled mode changed finite waveform duration");
    check(std::abs(bit_rate(c)-8)<1e-12,"disabled mode changed useful-symbol rate");

    c=config();c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{3,1});
    rejects([&]{validate(c);},"Sched_Hop and Burst conflict accepted");
    c.symbol_burst.wait_symbols=0;validate(c);
    c=config();auto hop=std::make_shared<FhssPlan>();hop->defer_symbols=true;c.fake_hopping=hop;
    rejects([&]{validate(c);},"diagnostic whole-symbol deferral and Burst conflict accepted");
}

void test_arithmetic_and_grid() {
    // Independent handoff examples, with no waveform padding or random wait.
    struct Example {double seconds;unsigned wait;std::uint64_t spacing;};
    for(const auto example:std::array<Example,4>{{{1,1,60},{25,1,60},{40,1,90},{120,2,360}}}) {
        const SymbolBurstPlan plan(config(example.seconds,example.wait));
        const auto first=plan.first_slot({epoch,0}),next=plan.successor(first);
        check(first.support_begin==SymbolBurstCoordinate{epoch,0},"exact grid point advanced");
        check(next.support_begin==SymbolBurstCoordinate{epoch+example.spacing,0},
              "independent minimum-spacing vector changed");
        check(plan.minimum_spacing_samples()==example.spacing*8000,
              "minimum spacing used random draws or lost rounding");
        check(bit_rate(config(example.seconds,example.wait))==1/example.seconds,
              "Burst changed the decoder's useful-symbol rate");
    }
    const SymbolBurstPlan plan(config());
    check(plan.first_slot({epoch,1}).support_begin==SymbolBurstCoordinate{epoch+30,0},
          "fractional UTC start rounded backward");
    check(plan.first_slot({epoch+1,0}).support_begin==SymbolBurstCoordinate{epoch+30,0},
          "whole-second UTC start rounded backward");
    rejects([&]{(void)plan.first_slot({epoch,8000});},"invalid phase accepted for first slot");
    rejects([&]{(void)plan.bind_payload(epoch,8000);},"invalid bound payload phase accepted");
    rejects([&]{(void)plan.bind_payload(epoch+1,0);},"off-grid real origin was silently rounded");
    check(plan.add({epoch,7999},1)==SymbolBurstCoordinate{epoch+1,0},"sample carry changed");
    check(plan.distance({epoch,7999},{epoch+1,1})==2,"sample borrow changed");
    rejects([&]{(void)plan.distance({epoch+1,0},{epoch,0});},"negative distance accepted");
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    rejects([&]{(void)plan.add({maximum,0},8000);},"UTC addition wrapped");
    rejects([&]{(void)plan.first_slot({maximum,1});},"UTC rounding wrapped");
    rejects([&]{(void)plan.distance({0,0},{maximum,0});},"unrepresentable distance wrapped");
    const auto seconds=maximum/8000+1;
    check(plan.distance({0,7999},{seconds,0})==(seconds-1)*8000+1,
          "representable borrowed distance rejected before borrowing");
    const auto last=plan.first_slot({maximum-maximum%30,0});
    rejects([&]{(void)plan.successor(last);},"successor UTC floor wrapped");
    auto forged=plan.first_slot({epoch,0});++forged.support_end.phase;
    rejects([&]{(void)plan.successor(forged);},"foreign slot geometry accepted");
    forged=plan.first_slot({epoch,0});forged.index=maximum;
    rejects([&]{(void)plan.successor(forged);},"logical symbol position wrapped");
    rejects([&]{(void)symbol_burst_detail::multiply(maximum,2);},"wait multiplication wrapped");
}

void test_padding_and_estimate_origin() {
    auto c=config(30,1,true);c.bandwidth_hz=32; // chip=500, padding=4000=.5 seconds.
    const SymbolBurstPlan plan(c);const auto first=plan.first_slot({epoch,0});
    check(plan.padding_samples()==4000 && plan.symbol_samples()==240000,
          "shaped fixture geometry changed");
    check(first.payload_begin==SymbolBurstCoordinate{epoch,4000} &&
          first.payload_end==SymbolBurstCoordinate{epoch+30,4000} &&
          first.support_end==SymbolBurstCoordinate{epoch+31,0},
          "full finite pulse support lost");
    check(plan.successor(first).support_begin==SymbolBurstCoordinate{epoch+90,0},
          "pulse support was clipped to the 60-second floor");
    check(plan.bind_payload(epoch,4000)==first,"canonical payload origin did not bind exactly");
    rejects([&]{(void)plan.bind_payload(0,0);},"negative placeholder support was accepted");
    rejects([&]{(void)plan.bind_payload(epoch,4001);},"off-grid payload phase was accepted");
    check(plan.minimum_spacing_samples()==90*8000,"minimum spacing omitted finite support");

    // An unset origin is valid geometry. Estimation alone supplies a representative.
    check(c.stream_epoch==0 && c.stream_phase_samples==0,"fixture has a hidden anchor");
    const auto estimate=scheduled_estimate_config(c);
    check(estimate.stream_epoch==epoch && estimate.stream_phase_samples==4000,
          "unset estimator origin was not normalized to canonical support");
    c.stream_epoch=epoch+60;c.stream_phase_samples=4000;
    const auto bound=scheduled_estimate_config(c);
    check(bound.stream_epoch==c.stream_epoch && bound.stream_phase_samples==c.stream_phase_samples,
          "valid explicit estimator origin was replaced");
    c=config(1,1,false);
    const auto unshaped=scheduled_estimate_config(c);
    check(unshaped.stream_epoch==epoch && unshaped.stream_phase_samples==0,
          "unshaped placeholder epoch zero was treated as a bound estimate");
}

void test_draw_budget_and_domain_vectors() {
    std::array<std::uint8_t,32> bytes{};bytes.fill(255);
    rejects([&]{(void)symbol_burst_detail::choice(bytes,3);},"exhausted draw fell back to modulo");
    bytes[31]=254;
    check(symbol_burst_detail::choice(bytes,3)==2,"rejection did not remain in the 32-byte budget");
    check(symbol_burst_detail::choice(bytes,2)==1 && symbol_burst_detail::choice(bytes,4)==3,
          "power-of-two choice rejected legal bytes");
    rejects([&]{(void)symbol_burst_detail::choice(std::span(bytes).first(31),3);},
            "partial canonical draw block accepted");
    rejects([&]{(void)symbol_burst_detail::choice(bytes,0);},"empty choice count accepted");
    rejects([&]{(void)symbol_burst_detail::choice(bytes,5);},"unsupported choice count accepted");

    // Independent Python hashlib/hmac HKDF and openssl enc AES-256-CTR.
    // Master bytes00..1f, purpose datapump/v1/fhss, epoch BE64,
    // IV ASCII sb-wai01 || BE64(2*phase), 32 zero plaintext bytes.
    struct Vector {std::uint64_t epoch;std::uint32_t phase;std::string_view hex;std::array<unsigned,3> draws;};
    const std::array<Vector,4> vectors{{
        {epoch,0,"9c4c6eddb98ec351bb0a54cb8539ff51d56fde70fd92b031be9177ece428b685",{0,0,0}},
        {epoch,800,"6339547391772d44d1e3fa545cf9786459daa025e599d63d2274acd67cd04d48",{1,0,3}},
        {epoch+30,0,"a68b00341d9ccb64907e8805c95b5c0bddc49c76de30be827c5eb87b9016cdf8",{0,1,2}},
        {epoch+30,800,"c950bea673e9556b32038b91bfcd4e0619774cadd44b50edbf08d555c9b1e577",{1,0,1}}
    }};
    const auto key=test_key();
    for(const auto& vector:vectors) {
        const auto actual=key.stream(StreamPurpose::Fhss,vector.epoch,32ULL*vector.phase,32,
                                     StreamDomain::SymbolBurstWaitV1);
        check(actual==from_hex(vector.hex),"independent Symbol_Burst domain vector changed");
        for(unsigned random=1;random<=3;++random) {
            auto c=config();c.data_key=key;c.symbol_burst.random_limit=random;
            const SymbolBurstPlan plan(c);
            check(plan.additional_wait({vector.epoch,vector.phase})==vector.draws[random-1],
                  "independent wait draw vector changed");
        }
    }
    const auto rejected=key.stream(StreamPurpose::Fhss,epoch,32*24,32,StreamDomain::SymbolBurstWaitV1);
    check(rejected==from_hex("ff2a8a438605b1b12f3dfc1a495d228deab256c244d025ffaa167f72eb08f928") &&
          symbol_burst_detail::choice(rejected,3)==0,"natural leading-255 rejection vector changed");
    for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler}) {
        rejects([&]{(void)key.stream(purpose,epoch,0,1,StreamDomain::SymbolBurstWaitV1);},
                "wait domain accepted a different purpose key");
        rejects([&]{(void)key.stream(purpose,epoch,0,0,StreamDomain::SymbolBurstWaitV1);},
                "empty wait stream bypassed purpose validation");
    }
    const auto wait=key.stream(StreamPurpose::Fhss,epoch,0,32,StreamDomain::SymbolBurstWaitV1);
    for(const auto old:{StreamDomain::Payload,StreamDomain::Preamble,StreamDomain::Suppression,
        StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2,StreamDomain::FakeFhssV1,
        StreamDomain::FhssCycleV2,StreamDomain::FhssUtcV3,StreamDomain::CoverPreambleV1,
        StreamDomain::CoverSuppressionV1,StreamDomain::CoverWaitV1,
        StreamDomain::SchedHopKeyV1,StreamDomain::SchedHopUtcV1})
        check(wait!=key.stream(StreamPurpose::Fhss,epoch,0,32,old),"wait domain aliases an existing stream");
}

void test_keyed_and_plain_calendars() {
    auto c=config(25,2,true);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan plan(c);
    check(plan.padding_samples()==800,"keyed padding fixture changed");
    constexpr std::array<std::uint64_t,9> starts{0,180,360,510,630,750,870,990,1080};
    constexpr std::array<unsigned,8> draws{3,3,2,1,1,1,1,0};
    auto slot=plan.first_slot({epoch,0});
    for(std::size_t i=0;i<draws.size();++i) {
        check(slot.index==i && slot.support_begin==SymbolBurstCoordinate{epoch+starts[i],0} &&
              slot.payload_begin==SymbolBurstCoordinate{epoch+starts[i],800} &&
              slot.support_end==SymbolBurstCoordinate{epoch+starts[i]+25,1600},
              "independent keyed calendar vector changed");
        check(plan.additional_wait(slot.payload_begin)==draws[i],"keyed draw used a noncanonical address");
        slot=plan.successor(slot);
    }
    check(slot.support_begin==SymbolBurstCoordinate{epoch+starts.back(),0},"last keyed successor changed");
    check(plan.minimum_spacing_samples()==90*8000,"random wait became essential to minimum spacing");
    c.data_key.reset();const SymbolBurstPlan plain(c);
    check(plain.random_limit()==0 && c.symbol_burst.random_limit==3,
          "plaintext randomness or saved random preference changed");
    slot=plain.first_slot({epoch,0});
    for(unsigned i=0;i<8;++i) {
        check(slot.support_begin==SymbolBurstCoordinate{epoch+90*i,0} &&
              plain.additional_wait(slot.payload_begin)==0,"plaintext calendar used secret random waits");
        slot=plain.successor(slot);
    }
    c=config(25,1);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan jitter(c);const auto three=jitter.first_slot({epoch+150,0});
    check(jitter.additional_wait(three.payload_begin)==3 &&
          jitter.successor(three).support_begin==SymbolBurstCoordinate{epoch+300,0},
          "independent 25-second J3 example changed");
    c=config(120,2);c.data_key=test_key();c.symbol_burst.random_limit=2;
    const SymbolBurstPlan long_plan(c);const auto one=long_plan.first_slot({epoch+30,0});
    check(long_plan.additional_wait(one.payload_begin)==1 &&
          long_plan.successor(one).support_begin==SymbolBurstCoordinate{epoch+510,0},
          "independent 120-second J1 example changed");
}

void test_bounded_cursor() {
    const SymbolBurstPlan plan(config(1,2));const auto first=plan.first_slot({epoch,0});
    SymbolBurstCursor cursor(plan,first);const auto bytes=cursor.working_bytes();
    check(datapump::testing::benchmark_below(bytes,std::size_t{2048},"test_symbol_burst.bytes","workspace_bytes"),"cursor stores an unbounded schedule");
    for(unsigned i=0;i<10000;++i) {
        check(cursor.current().index==i &&
              cursor.current().payload_begin==SymbolBurstCoordinate{epoch+60*i,0},
              "forward cursor calendar changed");
        cursor.advance();
    }
    check(cursor.working_bytes()==bytes,"cursor grew with elapsed message length");
    for(unsigned i=0;i<100;++i) {
        check(cursor.at(9998).payload_begin.epoch==epoch+60*9998,"previous pulse neighbor changed");
        check(cursor.at(10000).payload_begin.epoch==epoch+60*10000,"current pulse neighbor changed");
        check(cursor.at(9999).payload_begin.epoch==epoch+60*9999,"middle pulse neighbor changed");
    }
    auto retained=plan.bind_payload(epoch+60*9000,0);retained.index=9000;
    cursor.checkpoint(retained);
    rejects([&]{(void)cursor.at(8999);},"lookup escaped the retained recovery window");
    check(cursor.at(9010).payload_begin.epoch==epoch+60*9010,"recovery cursor did not preserve source index");
    check(cursor.seek_payload_at_or_after({epoch+60*9010,0}).index==9010,
          "lower bound skipped an exact payload start");
    check(cursor.seek_payload_at_or_after({epoch+60*9010,1}).index==9011,
          "lower bound returned an earlier payload");
    check(cursor.seek_payload_at_or_after({epoch,0}).index==9000,
          "lower bound escaped its checkpoint");
    auto copy=cursor;copy.advance();
    check(copy.current().index==9001 && cursor.current().index==9000,
          "cursor copy shared mutable traversal state");
    auto forged=retained;++forged.payload_end.phase;
    rejects([&]{cursor.checkpoint(forged);},"foreign checkpoint geometry accepted");
    check(cursor.current().index==9000,"failed checkpoint altered current traversal");
}

void test_cached_keyed_quiet_lookup() {
    auto c=config(25,2,true);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan plan(c);const auto first=plan.first_slot({epoch,0});
    SymbolBurstCursor cursor(plan,first);
    const auto before_cold=allocations.load(std::memory_order_relaxed);
    const auto next=cursor.at(1);
    check(next.payload_begin==SymbolBurstCoordinate{epoch+180,800},"keyed quiet fixture changed");
    check(allocations.load(std::memory_order_relaxed)>before_cold,
          "cold keyed traversal did not exercise the crypto allocation probe");
    const auto before_cached=allocations.load(std::memory_order_relaxed);
    for(unsigned i=0;i<4096;++i) {
        const auto found=cursor.seek_payload_at_or_after({epoch+30+i%90,123});
        check(found==next && cursor.current()==next,"cached quiet lookup changed its selected slot");
        (void)cursor.at(0); // Pulse rendering also revisits the preceding symbol.
        check(cursor.seek_payload_at_or_after({epoch,801})==next,
              "cached successor changed immediately after the prior payload start");
    }
    check(allocations.load(std::memory_order_relaxed)==before_cached,
          "quiet PCM lookups regenerated the keyed wait stream or allocated calendar state");
}

void test_pure_spacing_estimates() {
    auto c=config(25,2,true);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan plan(c);
    const auto before=allocations.load(std::memory_order_relaxed);
    for(unsigned additional=0;additional<=3;++additional)
        check(plan.spacing_samples_for_additional_wait(additional)==(90+30*additional)*8000,
              "independent per-draw shaped spacing estimate changed");
    check(allocations.load(std::memory_order_relaxed)==before,
          "pure per-draw spacing performed crypto work or allocated state");
    rejects([&]{(void)plan.spacing_samples_for_additional_wait(4);},
            "spacing estimate accepted a draw beyond its configured limit");
    rejects([&]{(void)plan.spacing_samples_for_additional_wait(std::numeric_limits<unsigned>::max());},
            "spacing estimate added an unchecked external draw");

    c.integration_seconds=1;const SymbolBurstPlan short_plan(c);
    for(unsigned additional=0;additional<=3;++additional)
        check(short_plan.spacing_samples_for_additional_wait(additional)==60*8000,
              "random draws forced observable jitter below the 60-second floor");
    c=config(40,1);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan uneven(c);
    constexpr std::array<std::uint64_t,4> spacing{90,120,180,210};
    for(unsigned additional=0;additional<=3;++additional)
        check(uneven.spacing_samples_for_additional_wait(additional)==spacing[additional]*8000,
              "per-draw UTC rounding was replaced by an average wait");

    c.data_key.reset();const SymbolBurstPlan plain(c);
    check(plain.spacing_samples_for_additional_wait(0)==90*8000 && plain.random_limit()==0,
          "plaintext spacing retained secret random geometry");
    rejects([&]{(void)plain.spacing_samples_for_additional_wait(1);},
            "plaintext spacing accepted an inactive saved random preference");

    c=config(1e15,1);c.data_key=test_key();c.symbol_burst.random_limit=3;
    const SymbolBurstPlan huge(c);
    check(huge.minimum_spacing_samples()>=2*huge.symbol_samples(),
          "large representable minimum spacing was rejected");
    rejects([&]{(void)huge.spacing_samples_for_additional_wait(3);},
            "maximum random spacing overflow wrapped");
}
} // namespace

int main() {
    try {
        test_policy_and_disabled_compatibility();
        test_arithmetic_and_grid();
        test_padding_and_estimate_origin();
        test_draw_budget_and_domain_vectors();
        test_keyed_and_plain_calendars();
        test_bounded_cursor();
        test_cached_keyed_quiet_lookup();
        test_pure_spacing_estimates();
        std::cout<<"Symbol_Burst calendar tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
