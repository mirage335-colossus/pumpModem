#pragma once
#include "datapump/types.hpp"
#include <array>
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
namespace fec {
// Shortened systematic GF(256) RS, polynomial 0x11d, first root alpha^0.
// Erasures are unique byte positions; mixed capacity is 2*errors+erasures<=p.
// Failure leaves codeword unchanged. Return the number of changed byte values.
Bytes rs_encode(const Bytes& data,std::size_t parity_symbols);
std::size_t rs_correct(Bytes& codeword,std::size_t parity_symbols,
    std::span<const std::size_t> erasure_positions = {});
}
}
