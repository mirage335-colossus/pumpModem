#include "datapump/boundary_sync.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <string_view>

namespace datapump::boundary_sync {
namespace {
constexpr std::uint8_t unknown_bit = 2;

void validate_bits(std::span<const std::uint8_t> bits, std::size_t limit,
                   bool allow_unknown = false) {
    if (bits.size() > limit || bits.size() > Bytes{}.max_size())
        throw Error("Byte-boundary bit storage exceeds memory limit");
    const auto maximum = allow_unknown ? unknown_bit : std::uint8_t{1};
    if (std::any_of(bits.begin(), bits.end(), [maximum](auto bit) { return bit > maximum; }))
        throw Error(allow_unknown ? "Byte-boundary input elements must be zero, one, or unknown (2)" :
                                    "Byte-boundary input elements must be zero or one");
}

const std::array<std::uint8_t, marker_bits>& marker() {
    // Only the public derivation domain is stored in source or the executable.
    // EVP computes the word at runtime so optimization cannot embed the wire
    // marker when the source or executable itself is transferred as data.
    static const auto value = [] {
        constexpr std::string_view domain = "DataPump/byte-boundary/v1";
        std::array<std::uint8_t, 32> digest{};
        unsigned length = 0;
        if (EVP_Digest(domain.data(), domain.size(), digest.data(), &length,
                       EVP_sha256(), nullptr) != 1 || length != digest.size())
            throw Error("Byte-boundary marker derivation failed");
        std::array<std::uint8_t, marker_bits> bits{};
        for (std::size_t i = 0; i < bits.size(); ++i) {
            const auto word_bit = i % (marker_bits / 2);
            bits[i] = static_cast<std::uint8_t>((digest[word_bit / 8] >>
                (7 - word_bit % 8)) & 1U);
        }
        return bits;
    }();
    return value;
}

void append(Bytes& output, std::span<const std::uint8_t> bits) {
    output.insert(output.end(), bits.begin(), bits.end());
}

constexpr auto minimum_marker_bits = marker_bits - maximum_marker_loss_bits;
constexpr std::size_t hypothesis_count() {
    std::size_t paths = 1; // Complete marker, including substitutions.
    for (std::size_t lost = 1; lost <= maximum_marker_loss_bits; ++lost)
        paths += marker_bits - lost - marker_tail_bits + 1;
    return (2 * maximum_slip_bits + 1) * paths;
}
static_assert(hypothesis_count() == 144615);

unsigned ceil_log2(std::uint64_t value) {
    return static_cast<unsigned>(std::bit_width(value - 1));
}

// For n observed iid fair bits, at most e mismatches occupy
// V(n,e) = sum(i=0..e) C(n,i) of the 2^n possible strings. Exact integer
// arithmetic covers n<=192 and e<=8, including intermediate products.
using Volumes = std::array<std::array<unsigned, maximum_marker_errors + 1>, marker_bits + 1>;
const Volumes& mismatch_costs() {
    static const auto costs = [] {
        Volumes result{};
        for (std::size_t n = 0; n <= marker_bits; ++n) {
            std::uint64_t choose = 1, volume = 1;
            for (std::size_t e = 1; e <= std::min(n, maximum_marker_errors); ++e) {
                choose = choose * (n - e + 1) / e;
                volume += choose;
                result[n][e] = ceil_log2(volume);
            }
        }
        return result;
    }();
    return costs;
}

struct Acceptance {
    std::array<int, marker_bits + 1> errors{};
    explicit Acceptance(std::size_t input_bits) {
        errors.fill(-1);
        // A recovered periodic slot advances at least this many input bits.
        // The first false match under the iid null must occur on the nominal
        // cadence (no previous accepted match); this also bounds that cadence.
        const auto slots = 1 + input_bits / (interval_bits - maximum_slip_bits + minimum_marker_bits);
        const auto penalty = false_match_bits + ceil_log2(hypothesis_count()) + ceil_log2(slots);
        const auto& costs = mismatch_costs();
        // Marker geometry still occupies at least minimum_marker_bits slots,
        // but erased slots contribute no independent bit observations.
        for (std::size_t n = 0; n <= marker_bits; ++n)
            for (std::size_t e = 0; e <= std::min(n, maximum_marker_errors); ++e)
                if (penalty + costs[n][e] <= n) errors[n] = static_cast<int>(e);
    }
};

struct Match {
    std::size_t offset, length;
    unsigned evidence_bits;
};

void consider(std::optional<Match>& best, const Match& candidate) {
    if (best && best->offset + best->length != candidate.offset + candidate.length)
        throw Error("Ambiguous byte-boundary marker endpoint");
    // Equivalent deletion paths need not locate the missing bits uniquely.
    // Preserve the strongest evidence for normalizing the preceding interval.
    if (!best || candidate.evidence_bits > best->evidence_bits ||
        (candidate.evidence_bits == best->evidence_bits && candidate.length > best->length))
        best = candidate;
}

std::optional<Match> match_marker(std::span<const std::uint8_t> bits,
                                  std::size_t first, std::size_t last,
                                  const Acceptance& acceptance) {
    if (bits.size() < minimum_marker_bits || first > bits.size() - minimum_marker_bits)
        return {};
    last = std::min(last, bits.size() - minimum_marker_bits);
    const auto& expected = marker();
    std::optional<Match> best;
    // Keep the common pristine path inexpensive. The fixed marker's endpoint
    // geometry (pinned by tests) excludes relaxed competing endpoints when a
    // full exact marker exists. Still reject two exact ends defensively.
    for (auto offset = first; offset <= last; ++offset) {
        if (bits.size() - offset >= marker_bits && acceptance.errors[marker_bits] >= 0 &&
            std::equal(expected.begin(), expected.end(), bits.begin() + static_cast<std::ptrdiff_t>(offset)))
            consider(best, {offset, marker_bits, static_cast<unsigned>(marker_bits)});
    }
    if (best) return best;

    const auto& costs = mismatch_costs();
    for (auto offset = first; offset <= last; ++offset) {
        const auto available = std::min(marker_bits, bits.size() - offset);
        const auto observed = bits.subspan(offset, available);
        std::array<unsigned, marker_bits + 1> prefix{}, suffix{}, known{};
        for (std::size_t i = 0; i < available; ++i) {
            const auto present = observed[i] != unknown_bit;
            known[i + 1] = known[i] + present;
            prefix[i + 1] = prefix[i] + (present && observed[i] != expected[i]);
        }
        const auto complete_observations = known[available];
        if (available == marker_bits && acceptance.errors[complete_observations] >= 0 &&
            prefix[marker_bits] <= static_cast<unsigned>(acceptance.errors[complete_observations]))
            consider(best, {offset, marker_bits,
                complete_observations - costs[complete_observations][prefix[marker_bits]]});
        for (std::size_t lost = 1; lost <= maximum_marker_loss_bits; ++lost) {
            const auto length = marker_bits - lost;
            if (available < length) continue;
            const auto n = known[length];
            if (acceptance.errors[n] < 0) continue;
            // Without an intact end anchor, a shortened marker prefix could
            // consume arbitrary payload bits. Do not infer a missing trailer.
            // Unknown timed slots cannot satisfy this exact observed anchor.
            if (!std::equal(expected.end() - static_cast<std::ptrdiff_t>(marker_tail_bits), expected.end(),
                            observed.begin() + static_cast<std::ptrdiff_t>(length - marker_tail_bits))) continue;
            suffix[length] = 0;
            for (auto i = length; i > 0; --i)
                suffix[i - 1] = suffix[i] +
                    (observed[i - 1] != unknown_bit && observed[i - 1] != expected[i - 1 + lost]);
            for (std::size_t gap = 0; gap <= length - marker_tail_bits; ++gap) {
                const auto errors = prefix[gap] + suffix[gap];
                if (errors <= static_cast<unsigned>(acceptance.errors[n]))
                    consider(best, {offset, length, n - costs[n][errors]});
            }
        }
    }
    return best;
}

std::optional<Match> match_known_suffix(std::span<const std::uint8_t> bits,
                                       std::size_t missing, const Acceptance& acceptance) {
    const auto length = marker_bits - missing;
    if (bits.size() < length) return {};
    const auto& expected = marker();
    unsigned errors = 0, n = 0;
    for (std::size_t i = 0; i < length; ++i) if (bits[i] != unknown_bit) {
        ++n;
        errors += bits[i] != expected[i + missing];
    }
    if (acceptance.errors[n] < 0 || errors > static_cast<unsigned>(acceptance.errors[n])) return {};
    return Match{0, length, n - mismatch_costs()[n][errors]};
}
}

std::size_t encoded_size(std::size_t data_bits) {
    if (data_bits % 8 != 0) throw Error("Byte-boundary input must be byte aligned");
    const auto markers = 1 + data_bits / interval_bits;
    if (markers > (std::numeric_limits<std::size_t>::max() - data_bits) / marker_bits)
        throw Error("Byte-boundary encoded size overflow");
    return data_bits + markers * marker_bits;
}

Bytes insert(std::span<const std::uint8_t> bits, std::size_t limit) {
    validate_bits(bits, limit);
    const auto size = encoded_size(bits.size());
    if (size > limit || size > Bytes{}.max_size())
        throw Error("Byte-boundary encoded storage exceeds memory limit");
    Bytes output;
    output.reserve(size);
    append(output, marker());
    std::size_t position = 0;
    while (bits.size() - position >= interval_bits) {
        append(output, bits.subspan(position, interval_bits));
        append(output, marker());
        position += interval_bits;
    }
    append(output, bits.subspan(position));
    return output;
}

Recovery recover_packet(std::span<const std::uint8_t> wire_bits, std::size_t limit,
                        std::size_t leading_missing_bits) {
    validate_bits(wire_bits, limit, true);
    if (leading_missing_bits > maximum_marker_loss_bits)
        throw Error("Missing leading marker exceeds recovery limit");
    const Acceptance acceptance(wire_bits.size());
    const auto initial = leading_missing_bits ? match_known_suffix(wire_bits, leading_missing_bits, acceptance) :
        match_marker(wire_bits, 0, maximum_slip_bits, acceptance);
    Recovery result;
    result.leading_marker_recognized = initial.has_value();
    auto& output = result.bits;
    // Every recognized or damaged full slot removes more bits than the
    // maximum zero fill can add, so output never exceeds the input bound.
    output.reserve(wire_bits.size());
    const auto initial_length = marker_bits - leading_missing_bits;
    if (!initial && wire_bits.size() < initial_length) {
        append(output, wire_bits);
        std::replace(output.begin(), output.end(), unknown_bit, std::uint8_t{0});
        return result;
    }
    std::size_t position = initial ? initial->offset + initial->length : initial_length;
    constexpr auto first_candidate = interval_bits - maximum_slip_bits;
    while (wire_bits.size() - position >= first_candidate + minimum_marker_bits) {
        const auto remaining = wire_bits.subspan(position);
        const auto matched = match_marker(remaining, first_candidate, interval_bits + maximum_slip_bits, acceptance);
        if (matched) {
            const auto kept = std::min(interval_bits, matched->offset);
            append(output, remaining.first(kept));
            output.insert(output.end(), interval_bits - kept, 0);
            position += matched->offset + matched->length;
        } else {
            if (remaining.size() < interval_bits + marker_bits) break;
            append(output, remaining.first(interval_bits));
            position += interval_bits + marker_bits;
        }
    }
    append(output, wire_bits.subspan(position));
    std::replace(output.begin(), output.end(), unknown_bit, std::uint8_t{0});
    return result;
}

Bytes recover(std::span<const std::uint8_t> wire_bits, std::size_t limit) {
    return recover_packet(wire_bits, limit).bits;
}
}
