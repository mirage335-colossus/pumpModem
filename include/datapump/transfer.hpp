#pragma once
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/stream_codec.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/tuning.hpp"
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

namespace datapump::transfer {
struct RepeatPolicy {
    // Local compose policy only. Tiny distress messages remain repeatable
    // even when their raw symbols exceed the normal time limit.
    double maximum_seconds = 2;
    std::size_t minimum_payload_bytes = 1;
};
struct Options {
    modem::Config modem;
    std::size_t content_limit = default_memory_limit;
    std::size_t dsp_workspace_bytes = runtime::default_dsp_workspace_bytes();
    FecMode fec = FecMode::rs60;
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
    std::size_t coded_bytes = 0;
    std::size_t wire_bits = 0;
    std::size_t content_bytes = 0;
    double coded_seconds = 0;
    double content_seconds = 0;
    double total_seconds = 0;
    bool memory_supported = false;
    bool batch_memory_supported = false;
    bool repeatable_allowed = false;
    std::size_t waveform_samples = 0;
};
// Exact framing/compression/FEC size and actual quantized modem symbol timing;
// allocates a stream, never a waveform. Does not enforce repeat_policy.
Estimate estimate(const Message& message, const Options& options);
// Optional numeric layout comes from that same encoded stream, avoiding a
// second full content encoding when presenting a transmission inspection.
Estimate estimate(const Message& message, const Options& options, StreamLayout* layout);
// Raw binary has no framing, compression, FEC or authentication. Pattern mode
// can add a hardware-settling prefix that carries no meaningful bits.
// Input elements are individual 0/1 bits. Byte estimates are ceil(bits/8)
// storage equivalents; payload time excludes that prefix, total time includes it.
Estimate estimate_binary(std::span<const std::uint8_t> bits, const Options& options);
// Uses the one-bit pattern modem. A selected non-tone key masks
// MSB-first bits with the symbol-start epoch/local Data position; no tag is added.
std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits, const Options& options);
inline constexpr std::size_t short_message_bytes = 16;
inline constexpr std::size_t short_message_bits = 13 * short_message_bytes;
// Nonempty text up to and including 16 source bytes uses the fixed dictionary's
// exact bits, without alignment markers, padding, parity or authentication.
// Files/screenshots and longer text use fixed 128-byte coded intervals.
bool uses_raw_message(const Message&) noexcept;
Bytes message_bits(const Message&, const Options&);
// Insert one alignment marker before every coded interval, then Data-mask
// the entire bitstream at its original symbol positions. Raw bits have no markers.
Bytes message_wire_bits(const Message&, const Options&);
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message&, const Options&);
// Symmetric Data masking by symbol-start second and local symbol ordinal,
// including fragments starting at an arbitrary global bit offset.
// Without a key the bits are unchanged. Does not authenticate decoded guesses.
void xor_binary_bits(std::span<std::uint8_t> bits, const Options& options,
                     std::size_t bit_offset = 0);
struct Received {
    StreamContent content;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp = 0;
    Bytes raw_bits; // Bounded diagnostic prefix, not the retained stream.
    std::size_t observed_bits = 0, missing_symbols = 0;
    bool stream_complete = false, content_validated = false;
    // Complete fixed-dictionary interpretation of an unmarked short stream.
    // This is neither interval validation nor authentication; raw bits remain.
    bool short_text_decoded = false;
    std::string error;
};
// Consumes immutable bounded physical chunks. Only a physical complete event
// can seal storage and invoke the application source decoder.
struct ReceiveStorageQuota {
    std::size_t limit=0, used=0; // Owned by one serialized receiver bank.
};
class StreamReceiver {
public:
    StreamReceiver(Options, std::uint64_t timestamp,
                   std::shared_ptr<ReceiveStorageQuota> = {});
    ~StreamReceiver();
    StreamReceiver(StreamReceiver&&) noexcept;
    StreamReceiver& operator=(StreamReceiver&&) noexcept;
    Received push(modem::PatternBurst, modem::Diagnostics = {});
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Received interpret_pattern(modem::PatternBurst, const Options&, std::uint64_t timestamp,
                           modem::Diagnostics = {});
using Progress = std::function<void(std::uint64_t)>;

// Separate codec scratch from the amount of application content admitted.
// Bounds the encoded source spool independently of the decoded output quota.
std::size_t source_storage_limit(std::size_t content_limit);
// Capacity for unpacked 0/1 elements, including leading and periodic recovery words.
// Checked separately from packed codec bytes; actual TX/RX allocations must
// still fit their independently configured DSP workspace.
std::size_t pattern_bit_limit(std::size_t content_limit);

modem::Config seeded_config(const Options& options, std::uint64_t timestamp);
// Packed convenience form; rejects partial bytes instead of adding padding.
// Use message_wire_bits/message_transmitter for exact short dictionary streams.
Bytes transmission_wire(const Message& message, const Options& options);
std::vector<float> transmit(const Message& message, const Options& options,
                            std::stop_token stop = {});
// Progress runs synchronously on the caller's thread, before each candidate.
// Cancellation is checked between candidates and throughout modulation, FFT,
// and acquisition loops. Individual bounded stream correction/library calls
// and allocations run to completion. Cancelled work raises datapump::Error.
Received receive(std::span<const float> samples, const Options& options,
                 Progress progress = {}, std::stop_token stop = {});
Received simulate(const Message& message, const Options& options,
                  const modem::ChannelConfig& channel, Progress progress = {},
                  std::stop_token stop = {});
}
