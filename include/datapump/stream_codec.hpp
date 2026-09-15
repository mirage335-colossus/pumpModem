#pragma once
#include "datapump/types.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace datapump {
// Application-local metadata. None of these fields is a modem wire header.
enum class MessageKind : std::uint8_t { text=0, file=1, screenshot=2 };
enum class FecMode : std::uint8_t { off=0, rs20=1, rs60=2 };
struct Message {
    MessageKind kind=MessageKind::text;
    Bytes data;
    std::string filename,callsign,grid;
    bool repeatable=false;
    std::array<std::uint8_t,16> local_id{};
};
struct StreamBitAccuracy {
    // Known observed data-area bits and the subset changed by correction.
    // Missing bits are coverage, never part of the accuracy denominator.
    std::uint64_t received_data_bits=0,corrected_data_bits=0;
    std::uint64_t missing_data_bits=0;
};
struct FecRegionStats {
    // received_bits are known observations; corrected_bits is the subset
    // changed to match the accepted codeword. Missing bits are separate.
    std::uint64_t received_bits=0,corrected_bits=0,missing_bits=0;
    // corrected_bytes counts changed values. erased_bytes counts recovered
    // unknown positions, even when their zero placeholder was already right.
    // repaired_bytes counts their union, without counting a byte twice.
    std::uint64_t corrected_bytes=0,erased_bytes=0,repaired_bytes=0;
};
struct StreamFecStats {
    // These are fixed encoded areas, not decompressed application bytes.
    // Comparisons use an accepted codeword, not a measured error probability.
    FecRegionStats data,integrity,parity;
};
struct StreamContent {
    Message message;
    std::size_t corrected_bytes=0,consumed_bytes=0;
    bool authenticated=false;
    std::optional<StreamBitAccuracy> pre_fec_accuracy;
    StreamFecStats fec_stats;
};
struct StreamLayout {
    FecMode fec=FecMode::off;
    bool compressed=false,authenticated=false;
    std::size_t source_bytes=0,encoded_source_bytes=0,wire_bytes=0,intervals=0;
    std::size_t data_bytes_per_interval=0,source_bytes_per_interval=0;
    std::size_t parity_bytes_per_interval=0,integrity_bytes_per_interval=0;
};
inline constexpr std::size_t stream_interval_bytes=128;
inline constexpr std::size_t stream_mac_bytes=32;

// The caller binds domain/version, canonical profile and symbol address into
// these independently keyed HMAC callbacks. No public digest is generated.
struct IntervalOptions {
    FecMode fec=FecMode::rs20;
    std::function<Bytes(const Bytes&)> authenticator;
    std::function<bool(const Bytes&,const Bytes&)> verifier;
};
struct DecodedInterval {
    Bytes data;
    std::size_t corrected_bytes=0,erased_bytes=0;
    bool authenticated=false;
    // Known bits remain measurable when other bits of an interval are missing.
    std::optional<StreamBitAccuracy> pre_fec_accuracy;
    StreamFecStats fec_stats;
};
std::size_t interval_parity_bytes(FecMode);
std::size_t interval_data_bytes(FecMode,bool keyed);
Bytes encode_interval(std::span<const std::uint8_t> data,const IntervalOptions& = {});
DecodedInterval decode_interval(std::span<const std::uint8_t> coded,
    const IntervalOptions& = {},std::span<const std::size_t> erasure_positions = {},
    // Optional one-mask-per-byte detail: 1 marks an unknown bit. Nonzero masks
    // must agree exactly with erasure_positions. Without masks, all eight bits
    // of each listed erased byte are conservatively excluded from accuracy.
    std::span<const std::uint8_t> erasure_bits = {});

// Returns full fixed data areas, before interval HMAC/RS. Compressed mode is
// exactly one raw LZMA2 stream plus <one area's zero padding. Uncompressed mode
// uses fixed nine-bit validity/byte cells, preserving arbitrary trailing zeros.
std::size_t source_bytes_per_interval(std::size_t data_area_bytes,bool compressed);
Bytes encode_source(std::span<const std::uint8_t> input,std::size_t data_area_bytes,
    bool compressed,std::size_t output_limit=default_memory_limit);
// POST-END APPLICATION API: caller must have observed the sole physical
// six-second stream-end event. Reject unresolved/missing intervals before call.
Bytes decode_source(std::span<const std::uint8_t> areas,std::size_t data_area_bytes,
    bool compressed,std::size_t output_limit=default_memory_limit);

namespace fec {
// Shortened systematic GF(256) RS, polynomial 0x11d, first root alpha^0.
// Erasures are unique byte positions; mixed capacity is 2*errors+erasures<=p.
// Failure leaves codeword unchanged. Return the number of changed byte values.
Bytes rs_encode(const Bytes& data,std::size_t parity_symbols);
std::size_t rs_correct(Bytes& codeword,std::size_t parity_symbols,
    std::span<const std::size_t> erasure_positions = {});
}
}
