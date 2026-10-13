#include "datapump/sched_hop.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace datapump;
using namespace datapump::modem;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
Crypto test_key(std::uint8_t delta = 0) {
    Bytes key(32);
    for (unsigned i = 0; i < key.size(); ++i) key[i] = static_cast<std::uint8_t>(i);
    key[0] ^= delta;
    return Crypto(key);
}
Bytes from_hex(std::string_view text) {
    const auto digit = [](char value) -> unsigned {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value-'0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value-'a')+10;
        throw std::runtime_error("invalid hexadecimal fixture");
    };
    Bytes result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>((digit(text[i])<<4)|digit(text[i+1])));
    return result;
}
void test_policy() {
    for (const auto multiplier : {1U, 10U, 100U, 1000U, 10000U})
        check(sched_hop_multiplier(sched_hop_id(multiplier)) == multiplier,
              "scheduled-hop identifiers do not round trip");
    for (const auto id : {"off", "disabled", "1"})
        check(sched_hop_multiplier(id) == 1, "disabled alias changed");
    check(sched_hop_multiplier("1000") == 1000 && sched_hop_multiplier("10000") == 10000,
          "numeric multiplier alias changed");
    for (const auto id : {"0", "2", "100k", "", "ten"})
        rejects([&] { (void)sched_hop_multiplier(id); }, "invalid multiplier accepted");
    rejects([] { (void)sched_hop_id(2); }, "invalid exported multiplier accepted");
    rejects([] { SchedHopPlan{10,0}.validate(); }, "empty active group accepted");
    rejects([] { SchedHopPlan{2,1}.validate(); }, "unsupported multiplier accepted");
    rejects([] { SchedHopPlan{10,11}.validate(); }, "more active hops than frame hops accepted");
    rejects([] { SchedHopPlan{10,std::numeric_limits<std::uint64_t>::max()}.validate(); },
            "overflowing frame length accepted");
}
void test_draws() {
    // For count=3, zero lies below the rejection threshold (1). A bounded
    // all-zero draw must fail, rather than borrow the next frame's bytes.
    std::array<std::uint8_t,16> draw{};
    rejects([&] { (void)sched_hop_detail::choice(draw,3); }, "biased/exhausted draw accepted");
    draw[15] = 5;
    check(sched_hop_detail::choice(draw,3) == 2, "rejection did not advance within draw budget");
    check(sched_hop_detail::choice(draw,1) == 0, "singleton choice failed");
    rejects([&] { (void)sched_hop_detail::choice(std::span(draw).first(7),3); },
            "partial draw word accepted");
    rejects([&] { (void)sched_hop_detail::choice(draw,0); }, "zero choice count accepted");
    std::array<std::uint8_t,8> endian{1,2,3,4,5,6,7,8};
    check(sched_hop_detail::choice(endian,1ULL<<56) == 0x02030405060708ULL,
          "draw words changed big-endian interpretation");
    constexpr auto count = (1ULL<<63)+1;
    // threshold=2^63-1. The first word is threshold-1 and must be rejected;
    // threshold itself is accepted. This also fixes equality at the gate.
    draw.fill(255);draw[0]=draw[8]=127;draw[7]=254;
    check(sched_hop_detail::choice(draw,count) == (1ULL<<63)-1,
          "draw threshold equality or rejection changed");
    endian.fill(255);
    check(sched_hop_detail::choice(endian,count) == (1ULL<<63)-2,
          "maximum draw word changed reduction");
}
void test_frozen_vectors() {
    // Independent Python hashlib/hmac HKDF reference + openssl enc
    // AES-256-CTR CLI. The master consists of bytes00..1f; no production
    // secret is exposed. These fix pads, geometry addresses and endian order.
    const auto key = test_key();
    const auto child = key.stream(StreamPurpose::Fhss,2,10*32,32,StreamDomain::SchedHopKeyV1);
    check(child == from_hex("18521c25fe0f63fdbf61ffc4b618cf6dd6ef4c557c13811baf918e483cc619b9"),
          "scheduled-hop geometry child-root vector changed");
    check(Crypto(child).stream(StreamPurpose::Fhss,0,0,32,StreamDomain::SchedHopUtcV1) ==
          from_hex("51cc33d3dda6f72f71b6b525c3d71101b20730b0c7bb29be25a65dca9d3bb419"),
          "scheduled-hop frame stream vector changed");
    const SchedHopSchedule schedule({10,2},key);
    constexpr std::array<std::uint64_t,8> starts{3,11,22,33,40,54,64,72};
    for (std::size_t q = 0; q < starts.size(); ++q)
        check(schedule.frame(q).active_begin_hop == starts[q], "scheduled-hop UTC frame vector changed");
}
void test_separation() {
    const auto key = test_key();
    std::vector<Bytes> all;
    for (const auto domain : {StreamDomain::Payload, StreamDomain::FakeFhssV1,
         StreamDomain::FhssCycleV2, StreamDomain::FhssUtcV3,
         StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2,
         StreamDomain::CoverPreambleV1,StreamDomain::CoverSuppressionV1,StreamDomain::CoverWaitV1,
         StreamDomain::SchedHopKeyV1, StreamDomain::SchedHopUtcV1}) {
        const auto bytes = key.stream(StreamPurpose::Fhss,17,0,512,domain);
        check(std::find(all.begin(),all.end(),bytes) == all.end(),
              "scheduled-hop domain reused an existing schedule stream");
        const auto part = key.stream(StreamPurpose::Fhss,17,253,131,domain);
        check(std::equal(part.begin(),part.end(),bytes.begin()+253),
              "scheduled-hop stream restarted at a block boundary");
        all.push_back(bytes);
    }
    for (const auto purpose : {StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler})
        for (const auto domain : {StreamDomain::SchedHopKeyV1,StreamDomain::SchedHopUtcV1})
            rejects([&] { (void)key.stream(purpose,17,0,1,domain); },
                    "scheduled-hop role accepted another key purpose");
    for (const auto purpose : {StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler})
        for (const auto domain : {StreamDomain::SchedHopKeyV1,StreamDomain::SchedHopUtcV1})
            rejects([&] { (void)key.stream(purpose,17,0,0,domain); },
                    "empty scheduled-hop request bypassed purpose validation");
    check(key.stream(StreamPurpose::Fhss,1,10*32,32,StreamDomain::SchedHopKeyV1) !=
          key.stream(StreamPurpose::Fhss,2,10*32,32,StreamDomain::SchedHopKeyV1),
          "active-group geometry reused a child root");
    check(key.stream(StreamPurpose::Fhss,1,10*32,32,StreamDomain::SchedHopKeyV1) !=
          key.stream(StreamPurpose::Fhss,1,100*32,32,StreamDomain::SchedHopKeyV1),
          "duty geometry reused a child root");
}
void test_calendar() {
    const auto key = test_key();
    const auto other_key = test_key(1);
    const SchedHopSchedule disabled({1,1},key);
    constexpr auto final = std::numeric_limits<std::uint64_t>::max();
    check(disabled.rank(final) == final && disabled.select(final) == final &&
          disabled.first_active_at_or_after(final) == final && disabled.is_active(final),
          "disabled schedule changed continuous coordinates");
    check(disabled.minimum_null_hops() == 0 && disabled.maximum_null_hops() == 0,
          "disabled schedule introduced null hops");
    for (const auto multiplier : {10U,100U,1000U,10000U})
        for (const auto group : {1ULL,3ULL,17ULL}) {
            if (group > multiplier) continue;
            const SchedHopSchedule schedule({multiplier,group},key);
            const SchedHopSchedule same({multiplier,group},key);
            const SchedHopSchedule other({multiplier,group},other_key);
            const auto period = multiplier;
            check(schedule.frame_hops() == period, "total frame hop count changed with active count");
            bool changed_key = false;
            std::uint64_t previous_end = 0;
            for (std::uint64_t q = 0; q < 32; ++q) {
                const auto f = schedule.frame(q);
                check(f.begin_hop == q*period && f.end_hop == (q+1)*period,
                      "frame coordinates changed");
                check(f.active_begin_hop >= f.begin_hop && f.active_end_hop-f.active_begin_hop == group &&
                      f.active_begin_hop-f.begin_hop <= schedule.maximum_jitter_hops(),
                      "active group escaped first-half jitter allowance");
                check(f.active_begin_hop == same.frame(q).active_begin_hop,
                      "same secret schedule is nondeterministic");
                changed_key |= f.active_begin_hop != other.frame(q).active_begin_hop;
                if (q) check(f.active_begin_hop-previous_end >= schedule.minimum_null_hops() &&
                             f.active_begin_hop-previous_end <= schedule.maximum_null_hops(),
                             "adjacent frame quiet bound failed");
                previous_end = f.active_end_hop;
                for (std::uint64_t i = 0; i < group; ++i) {
                    const auto hop = f.active_begin_hop+i;
                    const auto rank = q*group+i;
                    check(schedule.is_active(hop) && schedule.rank(hop) == rank && schedule.select(rank) == hop,
                          "active rank/select are not inverses");
                }
                check(!schedule.is_active(f.active_end_hop) &&
                      schedule.rank(f.active_end_hop) == (q+1)*group &&
                      schedule.first_active_at_or_after(f.active_end_hop) == schedule.select((q+1)*group),
                      "null endpoint did not select the next fresh group");
                if (f.active_begin_hop > f.begin_hop)
                    check(!schedule.is_active(f.begin_hop) && schedule.rank(f.begin_hop) == q*group,
                          "leading null interval consumed an active address");
            }
            check(changed_key, "different keys produced the same tested calendar");
            for (const auto q : {31ULL,0ULL,17ULL,4ULL,28ULL}) {
                const auto f = schedule.frame(q);
                check(schedule.select(q*group) == f.active_begin_hop,
                      "out-of-order access changed schedule");
            }
            const auto last_frame = final/period-1;
            const auto f = schedule.frame(last_frame);
            check(f.end_hop <= final && schedule.select(last_frame*group) == f.active_begin_hop,
                  "large random-access frame failed");
            rejects([&] { (void)schedule.frame(final); }, "frame counter wrapped");
            const auto first_unrepresentable = final/period;
            rejects([&] { (void)schedule.frame(first_unrepresentable); },
                    "first incomplete future frame accepted");
            rejects([&] { (void)schedule.select(first_unrepresentable*group); },
                    "select entered an incomplete future frame");
            rejects([&] { (void)schedule.rank(first_unrepresentable*period); },
                    "rank entered an incomplete future frame");
        }
}
void test_rational_samples() {
    const auto key = test_key();
    for (const auto fs : {512U,6120U,6000U,48000U}) {
        for (std::uint64_t hop = 0; hop < 1000; ++hop) {
            const auto floor = SchedHopSchedule::sample_boundary(hop,fs);
            const auto ceil = SchedHopSchedule::sample_boundary(hop,fs,true);
            check(floor == 2ULL*fs*hop/5 && ceil == (2ULL*fs*hop+4)/5 && ceil-floor <= 1,
                  "rational hop boundary accumulated rounding error");
            check(SchedHopSchedule::sample_boundary(hop+5,fs)-floor == 2ULL*fs,
                  "five base hops did not equal two seconds");
            check(SchedHopSchedule::hop_containing_sample(floor,fs) == hop,
                  "rounded boundary inverse selected previous hop");
            if (hop) check(SchedHopSchedule::hop_containing_sample(floor-1,fs) == hop-1,
                           "boundary inverse skipped previous sample");
        }
        for (const auto multiplier : {10U,100U,1000U,10000U}) {
            const SchedHopSchedule schedule({multiplier,3},key);
            const auto quiet = schedule.minimum_null_hops();
            for (unsigned residue = 0; residue < 5; ++residue)
                check(SchedHopSchedule::sample_boundary(residue+quiet,fs)-
                      SchedHopSchedule::sample_boundary(residue,fs) >= schedule.minimum_null_samples(fs),
                      "rational quiet sample bound overstated processing time");
        }
    }
    constexpr auto final = std::numeric_limits<std::uint64_t>::max();
    rejects([] { (void)SchedHopSchedule::sample_boundary(1,0); }, "zero sample rate accepted");
    rejects([&] { (void)SchedHopSchedule::sample_boundary(final,48000); }, "sample boundary wrapped");
    rejects([&] { (void)SchedHopSchedule::hop_containing_sample(final,1); }, "hop inverse wrapped");
    rejects([] { (void)SchedHopSchedule::hop_containing_sample(1,0); }, "zero inverse rate accepted");
    constexpr std::uint64_t ceiling_edge = 15372286728091293013ULL;
    check(SchedHopSchedule::sample_boundary(ceiling_edge,3) == final,
          "last floor-rounded sample boundary rejected");
    rejects([] { (void)SchedHopSchedule::sample_boundary(ceiling_edge,3,true); },
            "ceiling-only sample boundary overflow accepted");
    constexpr auto largest_rate = std::numeric_limits<std::uint32_t>::max();
    check(SchedHopSchedule::sample_boundary(5,largest_rate) == 2ULL*largest_rate,
          "largest portable sample rate overflowed a small exact boundary");
}
void test_source_map() {
    const auto key = test_key();
    for (const auto fs : {512U,6120U,8000U,48000U})
        for (const auto group : {1ULL,2ULL,3ULL}) {
            SchedHopPlan plan{10,group};
            const SchedHopSourceMap map(plan,key,fs);
            const auto quantum = 2ULL*fs/5;
            const auto size = group*quantum;
            check(map.source_samples_per_hop() == quantum && map.source_samples_per_group() == size,
                  "source quantum changed with frame or block timing");
            for (std::uint64_t q = 0; q < 32; ++q) {
                const auto f = map.schedule().frame(q);
                const auto start = SchedHopSchedule::sample_boundary(f.active_begin_hop,fs,true);
                const auto end = start+size;
                const auto location = map.locate(start);
                check(location.active && location.source.sample == q*size &&
                      location.source_begin == q*size && location.source_end == (q+1)*size &&
                      location.physical_begin == start && location.physical_end == end,
                      "physical/source group geometry disagreed");
                check(map.source_to_physical({q*size,0}).sample == start &&
                      map.source_to_physical({(q+1)*size,0},true).sample == end,
                      "start/end inverse changed null-gap boundary convention");
                check(map.locate(end).source.sample == (q+1)*size && !map.locate(end).active,
                      "end/null sample advanced source clock");
                check(map.source_to_physical({(q+1)*size,0}).sample > end,
                      "right inverse did not skip quiet interval");
                const auto actual_end = SchedHopSchedule::sample_boundary(f.active_end_hop,fs,true);
                check(end <= actual_end && actual_end-end <= group,
                      "integer-hop rounding extended physical active allocation");
                for (const auto offset : {0ULL,1ULL,quantum/2,quantum-1,size/2,size-1}) {
                    const auto point = SchedHopCoordinate{start+offset,.375L};
                    const auto found = map.locate(point);
                    const auto back = map.source_to_physical(found.source);
                    check(found.active && back.sample == point.sample && back.fraction == point.fraction,
                          "fractional source inverse lost or shifted samples");
                }
                check(SchedHopSourceMap::gain(location,{location.rise_begin,0}) == 0 &&
                      SchedHopSourceMap::gain(location,{location.flat_begin,0}) == 1 &&
                      SchedHopSourceMap::gain(location,{location.flat_end,0}) == 1 &&
                      SchedHopSourceMap::gain(location,{location.fall_end,0}) == 0,
                      "shared gate endpoints do not meet zero/flat power invariant");
                check(location.flat_end-location.flat_begin == map.minimum_flat_samples(),
                      "minimum flat-sample count disagreed with gate");
                if (start) {
                    const auto prefix = map.locate(start-1,.875L);
                    check(!prefix.active && prefix.source.sample == q*size && prefix.source.fraction == 0,
                          "quiet group prefix advanced source coordinates");
                }
                const auto tail = map.locate(end,.875L);
                check(!tail.active && tail.source.sample == (q+1)*size && tail.source.fraction == 0,
                      "quiet group tail advanced source coordinates");
            }
        }
    // Dilution equal to frame count is continuous, with no artificial rounded
    // source-clock gaps or periodic power ramps.
    const SchedHopSourceMap continuous({10,10},key,512);
    const SchedHopCoordinate point{123456789012345ULL,.625L};
    const auto found = continuous.locate(point);
    check(found.active && found.source.distance(point) == 0 &&
          continuous.source_to_physical(point).distance(point) == 0 &&
          SchedHopSourceMap::gain(found,point) == 1,
          "fully active schedule did not retain continuous source clock");
    const auto shifted = point.add_signed(-3.75L);
    check(shifted.sample == point.sample-4 && shifted.fraction == .875L &&
          shifted.distance(point) == -3.75L, "local signed coordinate adjustment lost precision");
    check(point.add_samples(17).sample == point.sample+17,
          "integer source-coordinate advancement changed");
    const auto tiny = SchedHopCoordinate{1,0}.add_signed(-1e-30L);
    tiny.validate();
    check(std::abs(tiny.distance({1,0})) < 1e-18L,
          "subresolution negative delta produced an invalid coordinate");
    const auto large = SchedHopCoordinate{1,.125L}.add_signed(std::ldexp(1.L,62));
    check(large.sample == (1ULL<<62)+1 && large.fraction == .125L,
          "large integer adjustment erased fractional source coordinate");
    check(SchedHopSourceMap::gain(continuous.locate(std::numeric_limits<std::uint64_t>::max()),
          {std::numeric_limits<std::uint64_t>::max(),0}) == 1,
          "last continuous coordinate acquired a false zero-power gate");
    rejects([] { (void)SchedHopCoordinate{0,0}.add_signed(-.125L); }, "negative UTC coordinate accepted");
    rejects([] { (void)SchedHopCoordinate{0,1}.add_signed(0); }, "unnormalized coordinate accepted");
    rejects([] { (void)SchedHopCoordinate{std::numeric_limits<std::uint64_t>::max(),.5L}.add_signed(.5L); },
            "fractional carry wrapped source coordinate");
    rejects([&] { (void)SchedHopSourceMap({10,1},key,0); }, "zero source sample rate accepted");
    SchedHopPlan unsafe{10,1};unsafe.audio_error_seconds=.4;
    rejects([&] { (void)SchedHopSourceMap(unsafe,key,6120); }, "entirely guarded active group accepted");
    SchedHopPlan precise{10,2};
    precise.clock_error_seconds=.003;precise.peer_clock_error_seconds=.007;
    precise.region_seconds=.011;precise.audio_error_seconds=.021;precise.ramp_seconds=.007;
    const SchedHopSourceMap guards(precise,key,512);
    const auto first = guards.source_to_physical({0,0});
    const auto geometry = guards.locate(first);
    // B=.003+.007+.011/2=.0155 seconds; ceil(Fs*B)=8,
    // ceil(Fs*(B+A))=19, ceil(Fs*R)=4; G*floor(.4Fs)=408.
    check(geometry.rise_begin-first.sample == 8 && geometry.flat_begin-first.sample == 12 &&
          geometry.physical_end-geometry.fall_end == 19 &&
          geometry.physical_end-geometry.flat_end == 23 && guards.minimum_flat_samples() == 373,
          "nondefault endpoint/region/one-sided audio guard algebra changed");
    double previous = 0;
    for (unsigned i = 0; i <= 32; ++i) {
        const auto point = SchedHopCoordinate{geometry.rise_begin,0}.add_signed(i/8.L);
        const auto gain = SchedHopSourceMap::gain(geometry,point);
        check(gain >= previous && gain >= 0 && gain <= 1, "shared smooth ramp is not monotone/bounded");
        previous = gain;
    }
    const SchedHopSourceMap terminal({10,9},key,1250);
    constexpr auto final = std::numeric_limits<std::uint64_t>::max();
    constexpr auto last_group = final/5000;
    rejects([&] { (void)terminal.source_to_physical({last_group*4500,0}); },
            "source inverse returned an unrepresentable complete physical group");
}
}

int main() {
    try {
        test_policy();test_draws();test_frozen_vectors();test_separation();test_calendar();
        test_rational_samples();test_source_map();
        std::cout << "scheduled-hop primitive tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scheduled-hop primitive test failure: " << error.what() << '\n';
        return 1;
    }
}
