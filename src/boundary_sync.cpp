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
    explicit Acceptance(std::uint64_t slot) {
        errors.fill(-1);
        if (!slot || slot == std::numeric_limits<std::uint64_t>::max())
            throw Error("Byte-boundary marker trial counter exhausted");
        // Allocate at most 2^-84 / (j*(j+1)) to marker attempt j. The budgets
        // telescope to 2^-84 over an indefinitely drained stream. Chunking
        // never adds marker trials, and no future input length is required.
        const auto penalty = false_match_bits + ceil_log2(hypothesis_count()) +
            ceil_log2(slot) + ceil_log2(slot + 1);
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
    if (data_bits % interval_bits != 0)
        throw Error("Byte-boundary input must contain complete coded intervals");
    const auto markers = data_bits / interval_bits;
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
    for (std::size_t position = 0; position < bits.size(); position += interval_bits) {
        append(output, marker());
        append(output, bits.subspan(position, interval_bits));
    }
    return output;
}

struct Collector::Impl {
    Bytes buffer;
    std::uint64_t position, attempts = 0;
    std::size_t leading_missing;
    bool need_marker = true, aligned = false, recognized = false;
    bool leading_recognized = false, finished = false, timed_slots;

    Impl(std::uint64_t first, std::size_t missing, bool timed)
        : position(first), leading_missing(missing), timed_slots(timed) {
        if (missing > maximum_marker_loss_bits || first < missing)
            throw Error("Invalid acquired leading marker offset");
        buffer.reserve(interval_bits);
    }

    void advance(std::uint64_t count) {
        if (count > std::numeric_limits<std::uint64_t>::max() - position)
            throw Error("Byte-boundary stream position overflow");
        position += count;
    }

    void consume(std::size_t count, std::size_t nominal) {
        advance(nominal);
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
    }

    bool acquire(bool final) {
        constexpr auto window = marker_bits + maximum_slip_bits;
        const auto known_length = marker_bits - leading_missing;
        const bool known = !aligned && leading_missing;
        // A complete exact marker excludes every different nearby endpoint,
        // including endpoints whose last few bits have not arrived yet.
        const bool exact = buffer.size() >= marker_bits &&
            std::equal(marker().begin(), marker().end(), buffer.begin());
        if (!final && buffer.size() < (known ? known_length : window) && !exact) return false;
        if (buffer.size() < minimum_marker_bits) return false;
        if (attempts == std::numeric_limits<std::uint64_t>::max() - 1)
            throw Error("Byte-boundary marker trial counter exhausted");
        const Acceptance acceptance(++attempts);
        const auto observed = std::span<const std::uint8_t>(buffer);
        const auto matched = known ? match_known_suffix(observed, leading_missing, acceptance) :
            match_marker(observed, 0, maximum_slip_bits, acceptance);
        if (matched) {
            // Timed input already carries every physical slot, including
            // unknowns. An acquired prefix is already absent from its start
            // coordinate. Untimed input instead restores inferred deletions.
            consume(matched->offset + matched->length, timed_slots ?
                    matched->offset + matched->length :
                    matched->offset + marker_bits - (known ? leading_missing : 0));
            leading_missing = 0;
            recognized = true;
            if (!aligned) leading_recognized = true;
            aligned = true;
            need_marker = false;
            return true;
        }
        if (known) {
            if (buffer.size() < known_length) return false;
            consume(known_length, known_length);
            leading_missing = 0;
            return true;
        }
        if (aligned) {
            // An established clock owns the fixed slot even if its marker
            // cannot be recognized. Never treat a partial trailer as data.
            if (buffer.size() < marker_bits) return false;
            consume(marker_bits, marker_bits);
            recognized = false;
            need_marker = false;
            return true;
        }
        // Late acquisition has no assumed source origin. Keep the overlap and
        // try the next disjoint group of eight starts; each group pays its own
        // trial budget. Arbitrarily long input never increases retained state.
        constexpr auto step = maximum_slip_bits + 1;
        consume(step, step);
        return true;
    }

    bool emit(bool final, const Sink& sink) {
        if (buffer.empty() || (buffer.size() < interval_bits && !final)) return false;
        Interval interval;
        interval.first_stream_symbol = position;
        interval.marker_recognized = recognized;
        const auto count = std::min(buffer.size(), interval_bits);
        for (std::size_t i = 0; i < interval_bits; ++i) {
            const auto bit = i < count ? buffer[i] : unknown_bit;
            const auto mask = static_cast<std::uint8_t>(1U << (7 - i % 8));
            if (bit == unknown_bit) {
                interval.erasures[i / 8] = 1;
                interval.erasure_bits[i / 8] |= mask;
            } else if (bit) interval.bytes[i / 8] |= mask;
        }
        consume(count, interval_bits);
        need_marker = true;
        sink(interval);
        return true;
    }

    void drain(bool final, const Sink& sink) {
        while (need_marker ? acquire(final) : emit(final, sink)) {}
    }
};

Collector::Collector(std::uint64_t first_stream_symbol, std::size_t leading_missing_bits, bool timed_slots)
    : impl_(std::make_unique<Impl>(first_stream_symbol, leading_missing_bits, timed_slots)) {}
Collector::~Collector() = default;
Collector::Collector(Collector&&) noexcept = default;
Collector& Collector::operator=(Collector&&) noexcept = default;

void Collector::push(std::span<const std::uint8_t> bits, const Sink& sink) {
    if (!sink) throw Error("Byte-boundary collector requires an output consumer");
    if (impl_->finished) throw Error("Byte-boundary stream already completed");
    validate_bits(bits, bits.size(), true);
    while (!bits.empty()) {
        const auto target = impl_->need_marker ? marker_bits + maximum_slip_bits : interval_bits;
        const auto count = std::min(bits.size(), target - impl_->buffer.size());
        append(impl_->buffer, bits.first(count));
        bits = bits.subspan(count);
        impl_->drain(false, sink);
    }
}

void Collector::finish(bool stream_complete, const Sink& sink) {
    if (!sink) throw Error("Byte-boundary collector requires an output consumer");
    if (!stream_complete || impl_->finished) return;
    impl_->drain(true, sink);
    impl_->buffer.clear();
    impl_->finished = true;
}

std::size_t Collector::buffered_bits() const noexcept { return impl_->buffer.size(); }
std::size_t Collector::working_bytes() const noexcept { return sizeof(Collector) + sizeof(Impl) + impl_->buffer.capacity(); }
bool Collector::leading_marker_recognized() const noexcept { return impl_->leading_recognized; }

Recovery recover_stream(std::span<const std::uint8_t> wire_bits, std::size_t limit,
                        std::size_t leading_missing_bits, bool stream_complete) {
    validate_bits(wire_bits, limit, true);
    Collector collector(leading_missing_bits, leading_missing_bits);
    Recovery result;
    const auto emit = [&](const Interval& interval) {
        if (result.bits.size() > limit || interval_bits > limit - result.bits.size())
            throw Error("Byte-boundary recovered storage exceeds memory limit");
        for (std::size_t i = 0; i < interval_bits; ++i) {
            const auto mask = static_cast<std::uint8_t>(1U << (7 - i % 8));
            result.bits.push_back(interval.erasure_bits[i / 8] & mask ? unknown_bit :
                static_cast<std::uint8_t>((interval.bytes[i / 8] & mask) != 0));
        }
    };
    collector.push(wire_bits, emit);
    collector.finish(stream_complete, emit);
    result.leading_marker_recognized = collector.leading_marker_recognized();
    return result;
}

Bytes recover(std::span<const std::uint8_t> wire_bits, std::size_t limit, bool stream_complete) {
    return recover_stream(wire_bits, limit, 0, stream_complete).bits;
}
}
