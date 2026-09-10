#pragma once
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/packet.hpp"
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

namespace datapump::transfer {
struct RepeatPolicy {
    // Incremental encoded content only: same metadata with an empty payload
    // establishes the fixed-overhead baseline. Tiny distress messages remain
    // repeatable even when their single byte exceeds the normal time limit.
    double maximum_seconds = 2;
    std::size_t minimum_payload_bytes = 1;
};
struct Options {
    modem::Config modem;
    FecMode fec = FecMode::rs20;
    bool compression = true;
    std::optional<Crypto> key;
    std::uint64_t timestamp = 0;
    unsigned search_seconds = 6;
    RepeatPolicy repeat_policy;
};
struct Estimate {
    std::size_t packet_bytes = 0;
    std::size_t content_bytes = 0;
    double packet_seconds = 0;
    double content_seconds = 0;
    double total_seconds = 0;
    bool memory_supported = false;
    bool repeatable_allowed = false;
    std::size_t waveform_samples = 0;
};
// Exact framing/compression/FEC size and actual quantized modem symbol timing;
// allocates a packet, never a waveform. Does not enforce repeat_policy.
Estimate estimate(const Message& message, const Options& options);
struct Received {
    DecodedPacket packet;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp = 0;
};
using Progress = std::function<void(std::uint64_t)>;

// Callbacks own their key material and remain valid after Options is destroyed.
PacketOptions packet_options(const Options& options, std::uint64_t timestamp);
modem::Config seeded_config(const Options& options, std::uint64_t timestamp);
Bytes pack(const Message& message, const Options& options);
DecodedPacket unpack(const Bytes& wire, const Options& options);
std::vector<float> transmit(const Message& message, const Options& options,
                            std::stop_token stop = {});
// Progress runs synchronously on the caller's thread, before each candidate.
// Cancellation is checked between candidates and throughout modulation, FFT,
// and acquisition loops. Individual bounded packet correction/library calls
// and allocations run to completion. Cancelled work raises datapump::Error.
Received receive(std::span<const float> samples, const Options& options,
                 Progress progress = {}, std::stop_token stop = {});
Received simulate(const Message& message, const Options& options,
                  const modem::ChannelConfig& channel, Progress progress = {},
                  std::stop_token stop = {});
}
