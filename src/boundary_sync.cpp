#include "datapump/boundary_sync.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace datapump::boundary_sync {
namespace {
void validate_bits(std::span<const std::uint8_t> bits, std::size_t limit) {
    if (bits.size() > limit || bits.size() > Bytes{}.max_size())
        throw Error("Byte-boundary bit storage exceeds memory limit");
    if (std::any_of(bits.begin(), bits.end(), [](auto bit) { return bit > 1; }))
        throw Error("Byte-boundary input elements must be zero or one");
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
}

std::size_t encoded_size(std::size_t data_bits) {
    if (data_bits % 8 != 0) throw Error("Byte-boundary input must be byte aligned");
    const auto intervals = data_bits / interval_bits;
    if (intervals > (std::numeric_limits<std::size_t>::max() - data_bits) / marker_bits)
        throw Error("Byte-boundary encoded size overflow");
    return data_bits + intervals * marker_bits;
}

Bytes insert(std::span<const std::uint8_t> bits, std::size_t limit) {
    validate_bits(bits, limit);
    const auto size = encoded_size(bits.size());
    if (size > limit || size > Bytes{}.max_size())
        throw Error("Byte-boundary encoded storage exceeds memory limit");
    Bytes output;
    output.reserve(size);
    std::size_t position = 0;
    while (bits.size() - position >= interval_bits) {
        append(output, bits.subspan(position, interval_bits));
        append(output, marker());
        position += interval_bits;
    }
    append(output, bits.subspan(position));
    return output;
}

Bytes recover(std::span<const std::uint8_t> wire_bits, std::size_t limit) {
    validate_bits(wire_bits, limit);
    Bytes output;
    // Every recognized or damaged full slot removes more bits than the
    // maximum zero fill can add, so output never exceeds the input bound.
    output.reserve(wire_bits.size());
    std::size_t position = 0;
    constexpr auto first_candidate = interval_bits - maximum_slip_bits;
    while (wire_bits.size() - position >= first_candidate + marker_bits) {
        const auto remaining = wire_bits.subspan(position);
        const auto last_candidate = std::min(interval_bits + maximum_slip_bits,
                                             remaining.size() - marker_bits);
        bool found = false;
        std::size_t marker_offset = 0;
        const auto& expected = marker();
        for (auto offset = first_candidate; offset <= last_candidate; ++offset) {
            const auto candidate = remaining.subspan(offset, marker_bits);
            if (!std::equal(expected.begin(), expected.end(), candidate.begin())) continue;
            if (found) throw Error("Ambiguous byte-boundary marker");
            found = true;
            marker_offset = offset;
        }
        if (found) {
            const auto kept = std::min(interval_bits, marker_offset);
            append(output, remaining.first(kept));
            output.insert(output.end(), interval_bits - kept, 0);
            position += marker_offset + marker_bits;
        } else {
            if (remaining.size() < interval_bits + marker_bits) break;
            append(output, remaining.first(interval_bits));
            position += interval_bits + marker_bits;
        }
    }
    append(output, wire_bits.subspan(position));
    return output;
}
}
