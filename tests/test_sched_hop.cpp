#include "datapump/sched_hop.hpp"
#include "datapump/modem.hpp"

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
void test_source_map_frame_cache() {
    const auto key = test_key();
    // These deliberately collide in a simple index-modulo-capacity cache.
    // A broad timing bank revisits a small set of neighboring frame identities
    // for each frequency; entry identity must not depend on visit order.
    constexpr std::array<std::uint64_t,3> frames{8192,8200,8208};
    constexpr std::array<unsigned,9> order{0,2,1,0,1,2,2,0,1};
    for (const auto fs : {512U,6120U})
        for (const auto multiplier : {10U,100U,1000U,10000U})
            for (const auto active : {1ULL,3ULL}) {
                const SchedHopPlan plan{multiplier,active};
                const SchedHopSourceMap map(plan,key,fs);
                const SchedHopSchedule oracle(plan,key);
                const auto group = map.source_samples_per_group();
                std::array<std::uint64_t,frames.size()> starts{};
                for (std::size_t i=0;i<frames.size();++i)
                    starts[i]=SchedHopSchedule::sample_boundary(oracle.frame(frames[i]).active_begin_hop,fs,true);
                for (unsigned repeat=0;repeat<32;++repeat)
                    for (const auto which : order) {
                        const auto source=SchedHopCoordinate{frames[which]*group+group/2,.375L};
                        const auto physical=map.source_to_physical(source);
                        check(physical.sample == starts[which]+group/2 && physical.fraction == .375L,
                              "multi-frame cache changed an exact keyed inverse");
                        const auto found=map.locate(physical);
                        check(found.active && found.frame_index == frames[which] &&
                              found.source.sample == source.sample && found.source.fraction == source.fraction,
                              "multi-frame cache changed fractional physical/source mapping");
                    }
                const auto statistics=map.cache_statistics();
                check(statistics.misses == frames.size() &&
                      statistics.generated_bytes == frames.size()*SchedHopSchedule::frame_bytes &&
                      statistics.hits == 32*order.size()*2-frames.size(),
                      "revisiting a retained frame regenerated its keyed draw");
                check(map.working_bytes() == sizeof(map) &&
                      map.working_bytes() <= sizeof(SchedHopSchedule)+
                          SchedHopSourceMap::cached_frame_capacity*sizeof(SchedHopFrame)+128,
                      "bounded frame cache storage was omitted or grew unexpectedly");

                // Copies own their entries and counters, including assignment
                // over another key/geometry. No shared mutable cache can leak a
                // frame or a counter update between search workers.
                auto copy=map;
                copy.reset_cache_statistics();
                (void)copy.source_to_physical({frames[0]*group,0});
                check(copy.cache_statistics().hits == 1 && copy.cache_statistics().misses == 0 &&
                      map.cache_statistics().hits == statistics.hits,
                      "map copies shared cache/counter mutations or reset evicted exact entries");
                SchedHopSourceMap assigned({10,2},test_key(1),fs);
                (void)assigned.source_to_physical({0,0});
                assigned=map;
                const auto physical=assigned.source_to_physical({frames[1]*group,0});
                check(physical.sample == starts[1] && assigned.cache_statistics().misses == statistics.misses,
                      "map assignment retained the preceding key's cached calendar");
            }

    const SchedHopSourceMap evict({10,1},key,6120);
    const auto group=evict.source_samples_per_group();
    for (std::uint64_t q=0;q<SchedHopSourceMap::cached_frame_capacity;++q)
        (void)evict.source_to_physical({q*group,0});
    check(evict.cache_statistics().misses == SchedHopSourceMap::cached_frame_capacity,
          "cache fill constructed a retained frame more than once");
    (void)evict.source_to_physical({0,0}); // Keep frame zero; frame one is oldest.
    (void)evict.source_to_physical({SchedHopSourceMap::cached_frame_capacity*group,0});
    (void)evict.source_to_physical({0,0});
    check(evict.cache_statistics().hits == 2,
          "least-recent eviction discarded a recently reused frame");
    const auto recomputed=evict.source_to_physical({group,0});
    const SchedHopSchedule oracle({10,1},key);
    check(recomputed.sample == SchedHopSchedule::sample_boundary(oracle.frame(1).active_begin_hop,6120,true) &&
          evict.cache_statistics().misses == SchedHopSourceMap::cached_frame_capacity+2 &&
          evict.cache_statistics().generated_bytes ==
              (SchedHopSourceMap::cached_frame_capacity+2)*SchedHopSchedule::frame_bytes,
          "exhausted cache did not recompute the identical keyed frame");

    const SchedHopSourceMap terminal({10000,1},key,64);
    (void)terminal.source_to_physical({0,0});
    const auto before=terminal.cache_statistics();
    rejects([&] { (void)terminal.source_to_physical({std::numeric_limits<std::uint64_t>::max(),0}); },
            "cached source map accepted a wrapping future frame");
    const auto after=terminal.cache_statistics();
    check(after.misses == before.misses+1 && after.generated_bytes == before.generated_bytes,
          "failed frame generation consumed or misreported cryptographic bytes");
    (void)terminal.source_to_physical({0,0});
    check(terminal.cache_statistics().hits == before.hits+1,
          "failed generation evicted a previously valid frame");

    const SchedHopSourceMap continuous({10,10},key,6120);
    (void)continuous.locate(1000,.25L);
    (void)continuous.source_to_physical({1000,.25L});
    check(continuous.cache_statistics().hits == 0 && continuous.cache_statistics().misses == 0 &&
          continuous.cache_statistics().generated_bytes == 0,
          "continuous schedule unnecessarily generated frame draws");
}
void test_physical_span_bounds() {
    Config config;
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    check(scheduled_physical_span_samples(config,maximum)==maximum,
          "disabled physical-span bound changed the source span");
    config.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,10});
    check(scheduled_physical_span_samples(config,maximum)==maximum &&
          scheduled_physical_span_samples(config,0)==0,
          "continuous/empty physical-span bound gained a quiet interval");

    // An independent walk through adversarial, otherwise valid calendars.
    // Enumerate every rational 2/5-second boundary residue, all endpoint jitter
    // choices for M=10, and their extrema for larger M. The point set includes
    // both sides of active/null boundaries and partial active groups; lengths
    // include exact source-group endpoints, which must use the LEFT inverse.
    std::uint64_t comparisons=0;
    bool attained=false;
    for(const auto fs:{64U,65U,67U,511U,6120U})
        for(const auto multiplier:{10U,100U,1000U,10000U})
            for(const auto active:{1ULL,2ULL,3ULL,static_cast<unsigned long long>(multiplier-1)}) {
                config.sample_rate=fs;
                const SchedHopPlan plan{multiplier,active};
                config.scheduled_hopping=std::make_shared<SchedHopPlan>(plan);
                const std::uint64_t group=(2ULL*fs/5)*active;
                const std::uint64_t jitter=(multiplier-active)/2;
                std::vector<std::uint64_t> draws{0,jitter};
                if(multiplier==10) {
                    draws.clear();
                    for(std::uint64_t j=0;j<=jitter;++j)draws.push_back(j);
                }
                for(std::uint64_t residue=0;residue<5;++residue)
                    for(const auto first:draws)for(const auto last:draws) {
                        std::array<std::uint64_t,5> starts{};
                        for(std::size_t q=0;q<starts.size();++q) {
                            const auto draw=q==0?first:last;
                            starts[q]=SchedHopSchedule::sample_boundary(
                                residue+q*multiplier+draw,fs,true);
                        }
                        const auto end=starts[0]+group;
                        const std::array points{starts[0],starts[0]+1,starts[0]+group/2,end-1,
                            end,end+1,starts[1]-1,starts[1]};
                        for(const auto length:std::array<std::uint64_t,6>{1,group-1,group,group+1,2*group,2*group+1}) {
                            const auto bound=scheduled_physical_span_samples(config,length);
                            const auto old_groups=2+(length-1)/group;
                            const auto old_frame=(2ULL*fs*multiplier+4)/5;
                            check(bound<=old_groups*old_frame,
                                  "tight physical-span bound exceeded the preceding whole-frame bound");
                            for(const auto begin:points) {
                                auto position=begin,remaining=length;
                                std::size_t q=0;
                                while(remaining) {
                                    while(q<starts.size() && position>=starts[q]+group)++q;
                                    check(q<starts.size(),"span oracle exhausted its calendar");
                                    position=std::max(position,starts[q]);
                                    const auto take=std::min(remaining,starts[q]+group-position);
                                    position+=take;remaining-=take;
                                }
                                check(position-begin<=bound,
                                      "physical-span bound excluded an admitted partial/null start");
                                attained=attained || position-begin==bound;
                                ++comparisons;
                            }
                        }
                    }
            }
    check(comparisons>100000 && attained,"physical-span oracle did not cover boundary/extreme cases");

    // Also exercise the real keyed inverse, so the analytic calendar oracle
    // does not substitute for testing the implementation's endpoint convention.
    for(const auto fs:{65U,6120U})for(const auto multiplier:{10U,100U,1000U,10000U})
        for(const auto active:{1ULL,2ULL,static_cast<unsigned long long>(multiplier-1)})
            for(const auto delta:{std::uint8_t{0},std::uint8_t{137}}) {
                config.sample_rate=fs;
                const SchedHopPlan plan{multiplier,active};
                config.scheduled_hopping=std::make_shared<SchedHopPlan>(plan);
                const SchedHopSourceMap map(plan,test_key(delta),fs);
                const auto group=map.source_samples_per_group();
                for(std::uint64_t q=1;q<=8;++q) {
                    const auto start=map.source_to_physical({q*group,0}).sample;
                    for(const auto begin:{start-1,start,start+group/2,start+group-1,start+group}) {
                        const auto source=map.locate(begin).source;
                        for(const auto length:std::array<std::uint64_t,4>{1,group,group+1,2*group}) {
                            const auto end=map.source_to_physical(source.add_samples(length),true);
                            check(end.sample-begin<=scheduled_physical_span_samples(config,length),
                                  "physical-span bound excluded an actual keyed source interval");
                        }
                    }
                }
            }

    // Products in the unreduced formula exceed uint64 here although its final
    // answer fits. Exact positive quotient/remainder arithmetic must not reject
    // them. Conversely, unrepresentable final spans must never wrap.
    config.sample_rate=65;
    config.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,9});
    const auto large=maximum/2;
    const auto groups=1+(large-1)/234;
    check(scheduled_physical_span_samples(config,large)==large+26*groups,
          "representable physical span rejected/truncated a wide intermediate");
    config.sample_rate=64;
    config.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10000,9999});
    const auto count=1+(large-1)/249975;
    const auto extra=count*6025; // gap=30125, J=0, so ceil(count*gap/5).
    check(scheduled_physical_span_samples(config,large)==large+extra,
          "rational-boundary remainder widened or truncated a large span");
    rejects([&] { (void)scheduled_physical_span_samples(config,maximum); },
            "unrepresentable final physical span wrapped");
    config.sample_rate=0;
    rejects([&] { (void)scheduled_physical_span_samples(config,1); },
            "empty scheduled source group accepted");
}
}

int main() {
    try {
        test_policy();test_draws();test_frozen_vectors();test_separation();test_calendar();
        test_rational_samples();test_source_map();test_source_map_frame_cache();test_physical_span_bounds();
        std::cout << "scheduled-hop primitive tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scheduled-hop primitive test failure: " << error.what() << '\n';
        return 1;
    }
}
