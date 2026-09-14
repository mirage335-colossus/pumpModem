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
// Raw binary has no training, framing, compression, FEC or authentication.
// Input elements are individual 0/1 bits. Byte estimates are ceil(bits/8)
// storage equivalents; only the supplied meaningful bits are modulated.
Estimate estimate_binary(std::span<const std::uint8_t> bits, const Options& options);
// Uses the selected APSK modem and seeded spreading. A selected key masks
// MSB-first bits with its data stream at options.timestamp; no tag is added.
std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits, const Options& options);
// Pattern transport: short text uses the fixed dictionary's exact bits;
// larger messages/files use packet bytes as a downstream content grammar.
Bytes message_bits(const Message&, const Options&);
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message&, const Options&);
// Symmetric raw data-stream masking, including fragments starting mid-byte.
// Without a key the bits are unchanged. Does not authenticate decoded guesses.
void xor_binary_bits(std::span<std::uint8_t> bits, const Options& options,
                     std::size_t bit_offset = 0);
struct Received {
    DecodedPacket packet;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp = 0;
    Bytes raw_bits{};
    bool packet_validated = true;
};
Received interpret_pattern(modem::PatternBurst, const Options&, std::uint64_t timestamp,
                           modem::Diagnostics = {});
using Progress = std::function<void(std::uint64_t)>;

// Separate codec scratch from the amount of application content admitted.
// Covers the largest supported RS overhead, temporary copies and metadata.
std::size_t packet_workspace_limit(std::size_t content_limit);

// Callbacks own their key material and remain valid after Options is destroyed.
PacketOptions packet_options(const Options& options, std::uint64_t timestamp);
modem::Config seeded_config(const Options& options, std::uint64_t timestamp);
// Public, reversible audio-frame whitening; this is not encryption. Offsets
// include the fixed 32-byte training prefix, which is left untouched. Apply
// after encryption on TX and before decryption on RX, with no added bytes.
void xor_audio_whitening(std::span<std::uint8_t> bytes, std::uint64_t wire_offset = 0);
// Combined public whitening/private data-stream mask for bootstrap and the
// start of whole-frame checks. Cache once per receiver; larger frames unmask
// their remainder with bounded scratch. This cache adds no frame bytes.
inline constexpr std::size_t audio_validation_limit = 2048;
inline constexpr std::size_t audio_validation_workspace = audio_validation_limit + 4096;
Bytes audio_bootstrap_mask(const Options& options, std::uint64_t timestamp);
struct AudioValidators {
    modem::BootstrapValidator bootstrap;
    modem::PacketValidator packet;
};
// Both callbacks own their key/mask and inspect bytes following training.
// Bootstrap returns the declared whole-frame extent; packet requires full
// integrity/authentication and checks the admitted original-content limit.
AudioValidators audio_validators(const Options& options, std::uint64_t timestamp);
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
