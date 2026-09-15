#pragma once
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/packet.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/tuning.hpp"
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
    std::size_t content_limit = default_memory_limit;
    std::size_t dsp_workspace_bytes = runtime::default_dsp_workspace_bytes();
    FecMode fec = FecMode::rs20;
    bool compression = true;
    std::optional<Crypto> key;
    std::uint64_t timestamp = 0;
    // Optional wall-clock origin of sample zero in a hardware recording.
    // Without it, offline buffers retain their deterministic prefix origin.
    std::optional<double> capture_epoch;
    unsigned search_seconds = 6;
    // Manual callers retain their explicit modem profile. Automatic GUI/CLI
    // receive searches resolve only these targets at the selected band/mode.
    bool automatic_receive_profiles = false;
    std::vector<double> receive_targets_db_hz{tuning::default_receive_target_db_hz};
    tuning::PatternMode receive_pattern_mode = tuning::PatternMode::auto_pattern;
    RepeatPolicy repeat_policy;
};
struct Estimate {
    std::size_t packet_bytes = 0;
    std::size_t content_bytes = 0;
    double packet_seconds = 0;
    double content_seconds = 0;
    double total_seconds = 0;
    bool memory_supported = false;
    bool batch_memory_supported = false;
    bool repeatable_allowed = false;
    std::size_t waveform_samples = 0;
};
// Exact framing/compression/FEC size and actual quantized modem symbol timing;
// allocates a packet, never a waveform. Does not enforce repeat_policy.
Estimate estimate(const Message& message, const Options& options);
// Optional numeric layout comes from that same encoded packet, avoiding a
// second full content encoding when presenting a transmission inspection.
Estimate estimate(const Message& message, const Options& options, PacketLayout* layout);
// Raw binary has no framing, compression, FEC or authentication. Pattern mode
// can add a hardware-settling prefix that carries no meaningful bits.
// Input elements are individual 0/1 bits. Byte estimates are ceil(bits/8)
// storage equivalents; payload time excludes that prefix, total time includes it.
Estimate estimate_binary(std::span<const std::uint8_t> bits, const Options& options);
// Uses the one-bit pattern modem. A selected non-tone key masks
// MSB-first bits with the symbol-start epoch/local Data position; no tag is added.
std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits, const Options& options);
// Pattern transport: short text uses the fixed dictionary's exact bits;
// larger messages/files use packet bytes as a downstream content grammar.
Bytes message_bits(const Message&, const Options&);
// Intentional pattern transmission: insert leading and periodic byte-boundary recovery
// bits for compact messages/files, then Data-mask the entire bitstream.
// Raw binary and short dictionary text carry no recovery markers.
Bytes message_wire_bits(const Message&, const Options&);
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message&, const Options&);
// Symmetric Data masking by symbol-start second and local symbol ordinal,
// including fragments starting at an arbitrary global bit offset.
// Without a key the bits are unchanged. Does not authenticate decoded guesses.
void xor_binary_bits(std::span<std::uint8_t> bits, const Options& options,
                     std::size_t bit_offset = 0);
struct Received {
    DecodedPacket packet;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp = 0;
    // Observed Data-unmasked decisions, with plaintext zero placeholders in
    // timed interior gaps. missing_symbols counts those unobserved slots.
    Bytes raw_bits{};
    std::size_t missing_symbols = 0;
    bool packet_validated = true;
};
Received interpret_pattern(modem::PatternBurst, const Options&, std::uint64_t timestamp,
                           modem::Diagnostics = {});
// Bounded header prefilter, followed by complete packet integrity validation
// only at a plausible full extent. Used to avoid joining finished packets
// across a later gap; it never chooses signal timing or accepts a partial frame.
bool pattern_packet_complete(const modem::PatternBurst&, std::size_t confirmed_bits,
                             const modem::Config&, std::size_t content_limit);
using Progress = std::function<void(std::uint64_t)>;

// Separate codec scratch from the amount of application content admitted.
// Covers the largest supported RS overhead, temporary copies and metadata.
std::size_t packet_workspace_limit(std::size_t content_limit);
// Capacity for unpacked 0/1 elements, including leading and periodic recovery words.
// Checked separately from packed codec bytes; actual TX/RX allocations must
// still fit their independently configured DSP workspace.
std::size_t pattern_bit_limit(std::size_t content_limit);

// Callbacks own their key material and remain valid after Options is destroyed.
PacketOptions packet_options(const Options& options, std::uint64_t timestamp);
modem::Config seeded_config(const Options& options, std::uint64_t timestamp);
Bytes pack(const Message& message, const Options& options);
Bytes transmission_wire(const Message& message, const Options& options);
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
