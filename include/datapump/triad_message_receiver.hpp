#pragma once

#include "datapump/stream_codec.hpp"
#include "datapump/triad_receiver.hpp"

#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace datapump::triad::v3 {

// Missing physical bit slots are erasures, never invented zero bits. The
// decoder runs only after the signal receiver has established physical end.
using MessageBit = std::optional<std::uint8_t>;
using MessageDecoder = std::function<StreamContent(std::span<const MessageBit>)>;
StreamContent decode_short_message(std::span<const MessageBit>);
// Locally selected bounded TEXT profile. Off is the exact short prefix code.
// RS modes are a new configured profile, decoded only at scored physical end:
// u8 bit-count | MSB-first packed short bits / zero pad | CRC32-BE | RS parity.
// CRC32 covers {'D','P','R','F',1,FecMode} followed by count/packed bytes.
Bytes encode_message_bits(std::span<const std::uint8_t> text, FecMode);
StreamContent decode_message_bits(std::span<const MessageBit>, FecMode);
MessageDecoder message_decoder(FecMode);

struct MessageReceiverOptions {
    std::size_t maximum_slots = 418;
    std::size_t maximum_data_bits = 208;
    std::size_t maximum_decoded_bytes = 16;
    bool decoder_accepts_erasures = false;
};
MessageReceiverOptions message_receiver_options(FecMode);

enum class MessageState {
    listening, receiving, ambiguous, awaiting_end_or_resume,
    complete, invalid_message, incomplete_message, incomplete, resource_limit, failed
};

struct MessageResult {
    MessageState state = MessageState::listening;
    std::string reason;
    bool physical_complete = false, message_ready_for_decode = false;
    bool reacquisition_required = false;
    std::size_t slots_consumed = 0, reacquisition_count = 0;
    Bytes received_bits;
    std::vector<MessageBit> bit_slots;
    std::vector<std::uint64_t> erasure_slot_indices, trailing_quiet_slots;
    std::optional<StreamContent> decoded_content;
    std::string filtered_text;
    std::array<std::size_t,3> classification_counts{}; // ambiguous, DATA, QUIET
    std::uint64_t represented_samples = 0;
    double sample_rate_hz = 0, represented_baseband_seconds = 0;
    // State/codec work only, not a complete receiver service/capacity metric.
    double message_service_seconds = 0;
};

// One bounded episode. Its local indices check chronology only; they never
// select a DSP shape, a received endpoint, or allocation sizes.
class QuietSlotMessageReceiver {
public:
    explicit QuietSlotMessageReceiver(MessageDecoder = decode_short_message,
                                      MessageReceiverOptions = {});
    const MessageResult& consume_slot(std::uint64_t local_index, const ReceiverSlotScore&);
    const MessageResult& finish_input();
    const MessageResult& cancel();
    const MessageResult& result() const noexcept { return result_; }
    bool terminal() const noexcept;
    static SlotDecision classification(const ReceiverSlotScore&) noexcept;
private:
    MessageDecoder decoder_;
    MessageReceiverOptions options_;
    MessageResult result_;
    std::vector<std::uint64_t> pending_nondata_, quiet_run_;
    std::optional<std::uint64_t> previous_first_, previous_stop_;
    bool leading_ambiguity_ = false;
    void fail(MessageState, const char*);
    bool physical_geometry(const ReceiverSlotScore&);
    void commit_erasures(std::span<const std::uint64_t>);
    void finish_decode();
};

struct CompletedMessage {
    std::uint64_t first_public_slot = 0, stop_public_slot = 0;
    MessageResult message;
};

// Ordered codec/rearm state only. A stream scheduler owns samples, DSP and
// concurrency. Resetting this codec never changes its global waveform cursor.
class ContinuousMessageReceiver {
public:
    explicit ContinuousMessageReceiver(MessageDecoder = decode_short_message,
                                       MessageReceiverOptions = {},
                                       std::uint64_t first_public_slot = 0,
                                       std::size_t maximum_pending_messages = 16);
    // False is backpressure: the supplied slot has not been consumed.
    bool consume(const ReceiverSlotScore&);
    std::vector<CompletedMessage> drain_messages();
    const MessageResult& result() const noexcept;
    std::uint64_t next_slot_index() const noexcept { return next_slot_; }
    std::size_t pending_messages() const noexcept { return messages_.size(); }
    std::size_t maximum_pending_messages() const noexcept { return maximum_pending_; }
    bool recovering() const noexcept { return recovering_; }
    bool stopped() const noexcept { return stopped_; }
    void invalidate_episode(std::string reason);
    void finish_input();
    void cancel();
private:
    MessageDecoder decoder_;
    MessageReceiverOptions options_;
    QuietSlotMessageReceiver episode_;
    std::uint64_t next_slot_ = 0, episode_first_ = 0;
    std::size_t maximum_pending_, quiet_run_ = 0;
    bool recovering_ = false, stopped_ = false;
    MessageResult rejected_;
    std::deque<CompletedMessage> messages_;
    std::optional<std::uint64_t> previous_first_, previous_stop_;
    double sample_rate_hz_ = 0;
    void rearm();
    bool physical_geometry(const ReceiverSlotScore&);
};

} // namespace datapump::triad::v3
