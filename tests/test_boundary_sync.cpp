#include "datapump/boundary_sync.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace datapump;
namespace byte_sync = datapump::boundary_sync;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F function, const char* message) {
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
Bytes random_bits(std::size_t count) {
    std::mt19937 random(19073);
    Bytes result(count);
    for (auto& bit : result) bit = static_cast<std::uint8_t>(random() & 1U);
    return result;
}
Bytes marker() {
    auto wire = byte_sync::insert(Bytes(byte_sync::interval_bits));
    wire.resize(byte_sync::marker_bits);
    return wire;
}
Bytes erase(Bytes bits, std::size_t at, std::size_t count) {
    bits.erase(bits.begin() + static_cast<std::ptrdiff_t>(at),
               bits.begin() + static_cast<std::ptrdiff_t>(at + count));
    return bits;
}
Bytes unpack(const byte_sync::Interval& interval) {
    Bytes result(byte_sync::interval_bits);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto mask = static_cast<std::uint8_t>(1U << (7 - i % 8));
        result[i] = interval.erasure_bits[i / 8] & mask ? byte_sync::unknown_bit :
            static_cast<std::uint8_t>((interval.bytes[i / 8] & mask) != 0);
        check(interval.erasures[i / 8] == (interval.erasure_bits[i / 8] != 0),
              "byte and bit erasure masks must agree");
    }
    return result;
}
std::vector<byte_sync::Interval> collect(const Bytes& bits, std::size_t chunk,
                                   std::uint64_t first = 0, std::size_t missing = 0,
                                   bool complete = false) {
    byte_sync::Collector collector(first, missing);
    std::vector<byte_sync::Interval> result;
    const auto sink = [&](const auto& interval) { result.push_back(interval); };
    for (std::size_t at = 0; at < bits.size(); at += std::min(chunk, bits.size() - at)) {
        collector.push(std::span(bits).subspan(at, std::min(chunk, bits.size() - at)), sink);
        check(collector.buffered_bits() <= byte_sync::interval_bits,
              "incremental collection must retain bounded scratch");
    }
    collector.finish(complete, sink);
    return result;
}
void fixed_wire_geometry() {
    check(byte_sync::interval_bytes == 128 && byte_sync::interval_bits == 1024 &&
          byte_sync::marker_bits == 192 && byte_sync::false_match_bits == 84,
          "fixed stream geometry changed");
    check(byte_sync::insert({}).empty() && byte_sync::encoded_size(0) == 0,
          "an empty coded stream must not transmit a marker");
    const auto word = marker();
    check(std::equal(word.begin(), word.begin() + 96, word.begin() + 96),
          "the runtime marker must repeat its 96-bit word");
    for (std::size_t count = 1; count <= 4; ++count) {
        const auto original = random_bits(count * byte_sync::interval_bits);
        const auto wire = byte_sync::insert(original);
        check(wire.size() == count * (byte_sync::interval_bits + byte_sync::marker_bits) &&
              wire.size() == byte_sync::encoded_size(original.size()),
              "exactly one marker must precede each complete coded interval");
        check(std::equal(original.end() - 128, original.end(), wire.end() - 128),
              "the final interval must end in its coded bits, without a terminal marker");
        check(byte_sync::recover(wire) == original, "fixed stream roundtrip");
        for (const std::size_t chunk : {1U, 7U, 113U, 512U, 65536U}) {
            const auto intervals = collect(wire, chunk);
            check(intervals.size() == count, "complete intervals must drain before stream completion");
            for (std::size_t i = 0; i < count; ++i) {
                const Bytes expected(original.begin() + static_cast<std::ptrdiff_t>(i * byte_sync::interval_bits),
                                     original.begin() + static_cast<std::ptrdiff_t>((i + 1) * byte_sync::interval_bits));
                check(unpack(intervals[i]) == expected && intervals[i].marker_recognized &&
                      intervals[i].first_stream_symbol == byte_sync::marker_bits + i * (byte_sync::marker_bits + byte_sync::interval_bits),
                      "chunking must not change data, framing, or canonical stream positions");
            }
        }
    }
}
void marker_damage_and_missing_positions() {
    const auto original = random_bits(3 * byte_sync::interval_bits);
    const auto pristine = byte_sync::insert(original);
    for (const auto lost : {1U, 7U, 32U, 64U, 80U}) {
        for (const auto gap : {0U, 17U, 64U}) {
            for (const auto start : {std::size_t{0}, byte_sync::marker_bits + byte_sync::interval_bits}) {
                const auto damaged = erase(pristine, start + gap, lost);
                const auto intervals = collect(damaged, 17);
                check(intervals.size() == 3 && byte_sync::recover(damaged) == original,
                      "bounded leading or periodic marker deletion must preserve complete coded intervals");
                for (std::size_t i = 0; i < intervals.size(); ++i)
                    check(intervals[i].first_stream_symbol == byte_sync::marker_bits + i * (byte_sync::marker_bits + byte_sync::interval_bits),
                          "deleted marker positions must remain in the canonical symbol schedule");
            }
        }
        const auto missing_prefix = erase(pristine, 0, lost);
        const auto acquired = collect(missing_prefix, 7, lost, lost);
        check(acquired.size() == 3 && acquired.front().first_stream_symbol == byte_sync::marker_bits &&
              unpack(acquired.front()) == Bytes(original.begin(), original.begin() + byte_sync::interval_bits),
              "an acquired leading offset must not count its missing prefix twice");
    }
    for (const auto changed : {1U, 4U, 8U}) {
        auto damaged = pristine;
        for (std::size_t i = 0; i < changed; ++i) {
            damaged[i * 23] ^= 1;
            damaged[byte_sync::marker_bits + byte_sync::interval_bits + i * 23] ^= 1;
        }
        check(byte_sync::recover(damaged) == original, "bounded marker substitutions must retain data");
    }
    auto unknown_marker = pristine;
    for (const auto start : {std::size_t{0}, byte_sync::marker_bits + byte_sync::interval_bits})
        for (const auto bit : {0U, 47U, 96U, 157U, 191U}) unknown_marker[start + bit] = byte_sync::unknown_bit;
    check(byte_sync::recover(unknown_marker) == original, "unknown marker slots must add no data erasures");

    auto partial = erase(byte_sync::insert(Bytes(byte_sync::interval_bits)), 0, 80);
    partial[111] = byte_sync::unknown_bit;
    check(collect(partial, 19).empty(), "an inferred deleted marker must have an observed exact trailing anchor");
    check(collect(partial, 19, 80, 80).size() == 1,
          "a known endpoint may retain an unknown trailing marker bit");

    auto damaged_slot = pristine;
    std::fill_n(damaged_slot.begin() + static_cast<std::ptrdiff_t>(byte_sync::marker_bits + byte_sync::interval_bits),
                byte_sync::marker_bits, byte_sync::unknown_bit);
    const auto nominal = collect(damaged_slot, 37);
    check(nominal.size() == 3 && !nominal[1].marker_recognized &&
          nominal[2].marker_recognized && byte_sync::recover(damaged_slot) == original,
          "a damaged marker on an established cadence must retain its nominal slot");
}
void erasures_and_true_completion() {
    const auto original = random_bits(byte_sync::interval_bits);
    auto wire = byte_sync::insert(original);
    wire[byte_sync::marker_bits + 3] = byte_sync::unknown_bit;
    wire.pop_back();
    byte_sync::Collector collector;
    std::vector<byte_sync::Interval> intervals;
    const auto sink = [&](const auto& interval) { intervals.push_back(interval); };
    collector.push(wire, sink);
    collector.finish(false, sink);
    check(intervals.empty() && collector.buffered_bits() == byte_sync::interval_bits - 1,
          "an interruption must not fill or emit an incomplete final interval");
    collector.finish(true, sink);
    check(intervals.size() == 1 && intervals[0].erasure_bits.front() == 0x10 &&
          intervals[0].erasure_bits.back() == 1,
          "true completion must preserve observed unknown slots and mark only the unobserved tail");
    auto expected = original;
    expected[3] = expected.back() = byte_sync::unknown_bit;
    check(unpack(intervals.front()) == expected && byte_sync::recover(wire, default_memory_limit, true) == expected,
          "recovered utilities must retain2 rather than silently claiming guessed zero bits");
    collector.finish(true, sink);
    check(intervals.size() == 1, "repeated completion must not duplicate an interval");
    rejects([&] { collector.push(Bytes{0}, sink); }, "completed collectors must reject further input");
    check(collect(marker(), 17, 0, 0, true).empty(),
          "a lone marker with no observed coded slots must not manufacture an empty interval");
    check(collect(Bytes{}, 1, 0, 0, true).empty(), "completion of an empty stream must emit nothing");
    auto one_slot = marker(); one_slot.push_back(1);
    const auto truncated = collect(one_slot, 11, 0, 0, true);
    check(truncated.size() == 1 && truncated[0].bytes[0] == 0x80 &&
          truncated[0].erasure_bits[0] == 0x7f,
          "one observed coded slot can anchor the fixed interval's erased remainder");
}
void confidence_and_ambiguous_endpoints() {
    auto sufficient = marker();
    bool retained_zero = false;
    for (auto& bit : sufficient) if (!bit) {
        if (!retained_zero) retained_zero = true;
        else bit = byte_sync::unknown_bit;
    }
    sufficient.insert(sufficient.end(), byte_sync::interval_bits, 0);
    check(collect(sufficient, 31).size() == 1,
          "103 known matching bits meet the first lifetime-budgeted marker threshold");
    auto insufficient = sufficient;
    *std::find(insufficient.begin(), insufficient.begin() + byte_sync::marker_bits, 0) = byte_sync::unknown_bit;
    check(collect(insufficient, 31).empty(),
          "102 known bits cannot borrow confidence from zero-filled unknowns");
    const auto word = marker();
    Bytes ambiguous(word.size() + 1, byte_sync::unknown_bit);
    ambiguous.front() = word.front(); ambiguous.back() = word.back();
    for (std::size_t i = 1; i < word.size(); ++i)
        if (word[i] == word[i - 1]) ambiguous[i] = word[i];
    rejects([&] { collect(ambiguous, 7, 0, 0, true); },
            "individually credible different endpoints must reject marker acquisition");
    // The exact-marker shortcut is safe throughout every nearby endpoint.
    for (int shift = -94; shift <= 14; ++shift) if (shift) {
        unsigned conflicts = 0;
        for (int i = 160; i < 192; ++i)
            if (i + shift >= 0 && i + shift < 192)
                conflicts += word[static_cast<std::size_t>(i)] != word[static_cast<std::size_t>(i + shift)];
        check(conflicts > 0, "an exact marker must exclude each different partial endpoint anchor");
    }
}
void late_join_and_bounded_drains() {
    const auto original = random_bits(4 * byte_sync::interval_bits);
    const auto wire = byte_sync::insert(original);
    constexpr std::size_t cut = 1500;
    const Bytes fragment(wire.begin() + cut, wire.end());
    const auto joined = collect(fragment, 29, cut);
    check(joined.size() == 2 && joined.front().first_stream_symbol == 2 * (byte_sync::interval_bits + byte_sync::marker_bits) + byte_sync::marker_bits,
          "late acquisition must find a later marker without assuming a source-origin identity");
    check(unpack(joined.front()) == Bytes(original.begin() + 2 * byte_sync::interval_bits,
                                         original.begin() + 3 * byte_sync::interval_bits),
          "late acquisition must retain the actual later coded interval");
    const auto one = byte_sync::insert(random_bits(byte_sync::interval_bits));
    byte_sync::Collector collector;
    std::size_t delivered = 0;
    const auto sink = [&](const byte_sync::Interval& interval) {
        check(interval.first_stream_symbol == delivered * (byte_sync::interval_bits + byte_sync::marker_bits) + byte_sync::marker_bits,
              "draining must not restart canonical stream positions");
        ++delivered;
    };
    for (std::size_t i = 0; i < 512; ++i) {
        collector.push(one, sink);
        check(delivered == i + 1 && collector.buffered_bits() == 0,
              "continuous valid input must drain without growing retained storage");
    }
    collector.finish(false, sink);
    check(delivered == 512, "draining output must not imply physical stream completion");
}
void input_and_resource_limits() {
    rejects([&] { byte_sync::encoded_size(8); }, "transmit size must require complete fixed intervals");
    rejects([&] { byte_sync::insert(Bytes(byte_sync::interval_bits - 1)); }, "transmit must reject partial coded intervals");
    auto unknown = Bytes(byte_sync::interval_bits); unknown[0] = byte_sync::unknown_bit;
    rejects([&] { byte_sync::insert(unknown); }, "transmit must reject unknown source bits");
    const auto wire = byte_sync::insert(Bytes(byte_sync::interval_bits));
    rejects([&] { byte_sync::insert(Bytes(byte_sync::interval_bits), wire.size() - 1); }, "expanded transmit storage must be checked");
    rejects([&] { byte_sync::recover(wire, wire.size() - 1); }, "bounded recovery must enforce its input limit");
    rejects([&] { byte_sync::recover(Bytes{0, 3}); }, "receive must reject invalid slot values");
    rejects([&] { byte_sync::Collector invalid(0, 1); }, "known missing positions must already exist in the input address");
    rejects([&] { byte_sync::Collector invalid(81, 81); }, "known marker loss must obey its fixed bound");
    const auto huge = std::numeric_limits<std::size_t>::max() / byte_sync::interval_bits * byte_sync::interval_bits;
    rejects([&] { byte_sync::encoded_size(huge); }, "expanded size overflow must fail before allocation");
    auto one_slot = marker(); one_slot.push_back(0);
    rejects([&] { byte_sync::recover(one_slot, one_slot.size(), true); },
            "true-completion filling must still obey the output storage limit");
}
}
int main() {
    try {
        fixed_wire_geometry(); marker_damage_and_missing_positions(); erasures_and_true_completion();
        confidence_and_ambiguous_endpoints(); late_join_and_bounded_drains(); input_and_resource_limits();
        std::cout << "fixed-interval boundary synchronization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fixed-interval boundary synchronization failed: " << error.what() << '\n';
        return 1;
    }
}
