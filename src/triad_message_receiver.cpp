#include "datapump/triad_message_receiver.hpp"
#include "datapump/compression.hpp"
#include "datapump/received_text.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include <lzma.h>

namespace datapump::triad::v3 {
namespace {
constexpr std::size_t quiet_slots = 2;
bool finite_nonnegative(double value) { return std::isfinite(value) && value >= 0; }
bool geometry_valid(const ReceiverSlotScore& row) {
    return std::isfinite(row.sample_rate_hz) && row.sample_rate_hz > 0 &&
        row.stop_sample > row.first_sample &&
        row.consumed_samples == row.stop_sample-row.first_sample;
}
bool terminal_state(MessageState state) {
    return state == MessageState::complete || state == MessageState::invalid_message ||
        state == MessageState::incomplete_message || state == MessageState::incomplete ||
        state == MessageState::resource_limit || state == MessageState::failed;
}
void validate(const MessageReceiverOptions& options) {
    // All extents come from the local profile. No received header or decoded
    // byte count can increase them. Defaults retain the canonical short profile.
    if (options.maximum_slots < quiet_slots || !options.maximum_data_bits ||
        !options.maximum_decoded_bytes || options.maximum_decoded_bytes > default_memory_limit ||
        options.maximum_data_bits > default_memory_limit ||
        options.maximum_slots > 2*options.maximum_data_bits+quiet_slots)
        throw Error("Invalid local Robust message bounds");
}
struct ServiceTimer {
    MessageResult& result;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    ~ServiceTimer() {
        result.message_service_seconds += std::chrono::duration<double>(
            std::chrono::steady_clock::now()-begin).count();
    }
};
std::size_t parity_bytes(std::size_t data_bytes, FecMode mode) {
    if (mode == FecMode::rs20) return (data_bytes+4)/5;
    if (mode == FecMode::rs60) return (3*data_bytes+4)/5;
    throw Error("Invalid Robust FEC mode");
}
void validate_mode(FecMode mode) {
    if (mode != FecMode::off && mode != FecMode::rs20 && mode != FecMode::rs60)
        throw Error("Invalid Robust FEC mode");
}
std::uint32_t message_crc(std::span<const std::uint8_t> body, FecMode mode) {
    const std::array<std::uint8_t,6> domain{'D','P','R','F',1,static_cast<std::uint8_t>(mode)};
    return lzma_crc32(body.data(), body.size(), lzma_crc32(domain.data(), domain.size(), 0));
}
void region_stats(FecRegionStats& stats, std::span<const MessageBit> supplied,
                  std::span<const std::uint8_t> corrected, std::size_t first, std::size_t stop) {
    for (std::size_t byte=first; byte<stop; ++byte) {
        bool changed = false, erased = false;
        for (unsigned bit=0; bit<8; ++bit) {
            const auto input = supplied[8*byte+bit];
            if (input) {
                ++stats.received_bits;
                if (*input != ((corrected[byte]>>(7-bit))&1)) {
                    ++stats.corrected_bits;
                    changed = true;
                }
            } else {
                ++stats.missing_bits;
                erased = true;
            }
        }
        stats.corrected_bytes += changed;
        stats.erased_bytes += erased;
        stats.repaired_bytes += changed || erased;
    }
}
}

StreamContent decode_short_message(std::span<const MessageBit> bits) {
    Bytes exact;
    if (bits.size() > 208) throw Error("Robust short message exceeds its local bit bound");
    exact.reserve(bits.size());
    for (const auto bit : bits) {
        if (!bit || *bit > 1) throw Error("Robust short message contains an erasure");
        exact.push_back(*bit);
    }
    StreamContent content;
    content.message.data = compression::decode_short_bits(exact, 16);
    content.consumed_bytes = exact.size(); // One byte stores each exact symbol bit.
    return content;
}

Bytes encode_message_bits(std::span<const std::uint8_t> text, FecMode mode) {
    validate_mode(mode);
    if (text.empty() || text.size()>16) throw Error("Robust text must contain one to sixteen bytes");
    auto bits = compression::encode_short_bits(text, 208);
    if (mode == FecMode::off) return bits;
    Bytes body(1+(bits.size()+7)/8, 0);
    body[0] = static_cast<std::uint8_t>(bits.size());
    for (std::size_t index=0; index<bits.size(); ++index)
        body[1+index/8] |= bits[index]<<(7-index%8);
    const auto checksum = message_crc(body, mode);
    for (unsigned shift : {24U,16U,8U,0U}) body.push_back(static_cast<std::uint8_t>(checksum>>shift));
    const auto word = fec::rs_encode(body, parity_bytes(body.size(), mode));
    Bytes result;
    result.reserve(8*word.size());
    for (const auto byte : word) for (unsigned bit=0; bit<8; ++bit)
        result.push_back((byte>>(7-bit))&1);
    return result;
}

StreamContent decode_message_bits(std::span<const MessageBit> bits, FecMode mode) {
    validate_mode(mode);
    if (mode == FecMode::off) return decode_short_message(bits);
    // Physical completion supplied the span. Enumerate the tiny public size
    // set before examining received bytes; no received count drives allocation.
    if (bits.empty() || bits.size()%8 || bits.size()>400)
        throw Error("Robust FEC span is outside its public frame profile");
    const auto bytes = bits.size()/8;
    std::size_t body_size = 0;
    for (std::size_t candidate=6; candidate<=31; ++candidate)
        if (candidate+parity_bytes(candidate, mode) == bytes) {
            if (body_size) throw Error("Ambiguous Robust FEC frame span");
            body_size = candidate;
        }
    if (!body_size) throw Error("Robust FEC span is outside its public frame profile");
    Bytes word(bytes, 0);
    std::vector<std::size_t> erasures;
    for (std::size_t byte=0; byte<bytes; ++byte) {
        bool erased = false;
        for (unsigned bit=0; bit<8; ++bit) {
            const auto value = bits[8*byte+bit];
            if (!value) erased = true;
            else {
                if (*value>1) throw Error("Invalid Robust FEC bit value");
                word[byte] |= *value<<(7-bit);
            }
        }
        if (erased) erasures.push_back(byte);
    }
    const auto changed = fec::rs_correct(word, parity_bytes(body_size, mode), erasures);
    const auto count = static_cast<std::size_t>(word[0]);
    if (!count || count>208 || 1+(count+7)/8+4 != body_size)
        throw Error("Invalid Robust FEC short bit extent");
    if (count%8 && (word[body_size-5] & ((1U<<(8-count%8))-1)))
        throw Error("Invalid Robust FEC padding");
    std::uint32_t checksum = 0;
    for (std::size_t index=body_size-4; index<body_size; ++index) checksum = (checksum<<8)|word[index];
    if (checksum != message_crc(std::span(word).first(body_size-4), mode))
        throw Error("Robust FEC checksum mismatch");
    Bytes short_bits(count);
    for (std::size_t index=0; index<count; ++index)
        short_bits[index] = (word[1+index/8]>>(7-index%8))&1;
    StreamContent content;
    content.message.data = compression::decode_short_bits(short_bits, 16);
    content.consumed_bytes = word.size();
    content.corrected_bytes = changed;
    // A public CRC is error detection, never sender authentication.
    content.authenticated = false;
    region_stats(content.fec_stats.data, bits, word, 0, body_size-4);
    region_stats(content.fec_stats.integrity, bits, word, body_size-4, body_size);
    region_stats(content.fec_stats.parity, bits, word, body_size, word.size());
    const auto& data = content.fec_stats.data;
    content.pre_fec_accuracy = StreamBitAccuracy{data.received_bits, data.corrected_bits, data.missing_bits};
    return content;
}

MessageDecoder message_decoder(FecMode mode) {
    validate_mode(mode);
    return [mode](std::span<const MessageBit> bits) { return decode_message_bits(bits, mode); };
}

MessageReceiverOptions message_receiver_options(FecMode mode) {
    validate_mode(mode);
    const std::size_t count = mode == FecMode::off ? 208 : mode == FecMode::rs20 ? 304 : 400;
    return {2*count+quiet_slots, count, 16, mode != FecMode::off};
}

QuietSlotMessageReceiver::QuietSlotMessageReceiver(MessageDecoder decoder, MessageReceiverOptions options)
    : decoder_(std::move(decoder)), options_(options) {
    if (!decoder_) throw Error("Robust message decoder is required");
    validate(options_);
}

bool QuietSlotMessageReceiver::terminal() const noexcept { return terminal_state(result_.state); }

void QuietSlotMessageReceiver::fail(MessageState state, const char* reason) {
    result_.state = state;
    result_.reason = reason;
}

SlotDecision QuietSlotMessageReceiver::classification(const ReceiverSlotScore& row) noexcept {
    if (!row.complete || !row.all_valid || !row.zero_h0_verified || !geometry_valid(row))
        return SlotDecision::ambiguous;
    if (row.decision == SlotDecision::data) {
        return row.bit && *row.bit <= 1 && row.operational_passes_conditional_bound &&
            std::isfinite(row.operational_log_e) && std::isfinite(row.operational_threshold) &&
            row.operational_log_e >= row.operational_threshold
            ? SlotDecision::data : SlotDecision::ambiguous;
    }
    if (row.decision != SlotDecision::quiet || !row.noise_statistics_valid || !row.quiet_modes ||
        !finite_nonnegative(row.quiet_energy) || !finite_nonnegative(row.quiet_threshold) ||
        !std::isfinite(row.quiet_noise_upper_tail_probability) ||
        row.quiet_noise_upper_tail_probability <= 0 || row.quiet_noise_upper_tail_probability > .001 ||
        row.quiet_energy > row.quiet_threshold || row.quiet_first_sample < row.first_sample ||
        row.quiet_stop_sample > row.stop_sample || row.quiet_stop_sample <= row.quiet_first_sample)
        return SlotDecision::ambiguous;
    return SlotDecision::quiet;
}

bool QuietSlotMessageReceiver::physical_geometry(const ReceiverSlotScore& row) {
    if (!geometry_valid(row) || (result_.sample_rate_hz && row.sample_rate_hz != result_.sample_rate_hz) ||
        (previous_first_ && (row.first_sample <= *previous_first_ || row.stop_sample <= *previous_stop_)))
        return false;
    const auto newly_covered = previous_stop_ && row.first_sample <= *previous_stop_
        ? row.stop_sample-*previous_stop_ : row.stop_sample-row.first_sample;
    if (newly_covered > std::numeric_limits<std::uint64_t>::max()-result_.represented_samples)
        return false;
    result_.represented_samples += newly_covered;
    result_.sample_rate_hz = row.sample_rate_hz;
    result_.represented_baseband_seconds = result_.represented_samples/row.sample_rate_hz;
    previous_first_ = row.first_sample;
    previous_stop_ = row.stop_sample;
    return true;
}

void QuietSlotMessageReceiver::commit_erasures(std::span<const std::uint64_t> slots) {
    result_.erasure_slot_indices.insert(result_.erasure_slot_indices.end(), slots.begin(), slots.end());
    result_.bit_slots.insert(result_.bit_slots.end(), slots.size(), std::nullopt);
}

void QuietSlotMessageReceiver::finish_decode() {
    // Only the final two eligible quiet slots are the physical suffix. Earlier
    // quiet/ambiguous slots remain erasures, including undecoded leading slots.
    const auto first_trailing = quiet_run_[quiet_run_.size()-quiet_slots];
    const auto final = std::lower_bound(pending_nondata_.begin(), pending_nondata_.end(), first_trailing);
    commit_erasures(std::span(pending_nondata_.begin(), final));
    result_.trailing_quiet_slots.assign(quiet_run_.end()-quiet_slots, quiet_run_.end());
    pending_nondata_.clear();
    result_.physical_complete = true;
    if (leading_ambiguity_) {
        // FEC corrects located interior losses, not an unknown message start.
        // Only the continuous owner's separate two-quiet rearm can discard an
        // unresolved prefix and establish the start of a new bounded episode.
        fail(MessageState::incomplete_message, "leading_message_prefix_unqualified");
        return;
    }
    if (!result_.erasure_slot_indices.empty() && !options_.decoder_accepts_erasures) {
        fail(MessageState::incomplete_message, "message_contains_erasures");
        return;
    }
    try {
        auto decoded = decoder_(result_.bit_slots);
        if (decoded.message.data.size() > options_.maximum_decoded_bytes ||
            decoded.message.filename.size() > options_.maximum_decoded_bytes ||
            decoded.message.callsign.size() > options_.maximum_decoded_bytes ||
            decoded.message.grid.size() > options_.maximum_decoded_bytes) {
            fail(MessageState::invalid_message, "invalid_native_decoder_result");
            return;
        }
        result_.filtered_text = received_text(decoded.message.data);
        result_.decoded_content = std::move(decoded);
        result_.message_ready_for_decode = true;
        result_.state = MessageState::complete;
        result_.reason.clear();
    } catch (...) {
        // Codec exception text can contain input bytes. Keep failure local and
        // bounded; it cannot turn physical completion into successful decoding.
        fail(MessageState::invalid_message, "codec_rejected_message");
    }
}

const MessageResult& QuietSlotMessageReceiver::consume_slot(std::uint64_t local_index,
                                                          const ReceiverSlotScore& row) {
    if (terminal()) throw Error("Robust message receiver is terminal");
    ServiceTimer timer{result_};
    result_.reacquisition_required = false;
    if (local_index != result_.slots_consumed) {
        fail(MessageState::failed, "slot_sequence_discontinuity");
        return result_;
    }
    if (result_.slots_consumed >= options_.maximum_slots) {
        fail(MessageState::resource_limit, "local_slot_limit_exceeded");
        return result_;
    }
    ++result_.slots_consumed;
    const auto category = physical_geometry(row) ? classification(row) : SlotDecision::ambiguous;
    ++result_.classification_counts[static_cast<std::size_t>(category)];
    if (category == SlotDecision::data) {
        result_.reacquisition_required = !pending_nondata_.empty();
        if (result_.reacquisition_required) {
            ++result_.reacquisition_count;
            commit_erasures(pending_nondata_);
            pending_nondata_.clear();
        }
        quiet_run_.clear();
        if (result_.received_bits.size() >= options_.maximum_data_bits) {
            fail(MessageState::resource_limit, "local_data_bit_limit_exceeded");
            return result_;
        }
        result_.received_bits.push_back(*row.bit);
        result_.bit_slots.push_back(*row.bit);
        result_.state = MessageState::receiving;
        result_.reason.clear();
    } else if (category == SlotDecision::ambiguous) {
        if (result_.received_bits.empty()) leading_ambiguity_ = true;
        pending_nondata_.push_back(local_index);
        quiet_run_.clear();
        result_.state = result_.received_bits.empty() ? MessageState::listening : MessageState::ambiguous;
        result_.reason = "unscored_or_ambiguous_slot";
    } else if (!result_.received_bits.empty()) {
        pending_nondata_.push_back(local_index);
        quiet_run_.push_back(local_index);
        if (quiet_run_.size() >= quiet_slots) finish_decode();
        else {
            result_.state = MessageState::awaiting_end_or_resume;
            result_.reason.clear();
        }
    } else {
        // Once a possible prefix exists, intervening quiet is still part of
        // that unresolved physical region. Preserve its exact slot position;
        // silently shortening it could give a FEC decoder a different frame.
        if (leading_ambiguity_) pending_nondata_.push_back(local_index);
        quiet_run_.clear();
        result_.state = MessageState::listening;
        result_.reason.clear();
    }
    return result_;
}

const MessageResult& QuietSlotMessageReceiver::finish_input() {
    if (!terminal()) fail(MessageState::incomplete, "transport_ended_before_physical_completion");
    return result_;
}
const MessageResult& QuietSlotMessageReceiver::cancel() {
    if (!terminal()) fail(MessageState::incomplete, "receive_cancelled_before_physical_completion");
    return result_;
}

ContinuousMessageReceiver::ContinuousMessageReceiver(MessageDecoder decoder, MessageReceiverOptions options,
    std::uint64_t first_public_slot, std::size_t maximum_pending_messages)
    : decoder_(std::move(decoder)), options_(options), episode_(decoder_, options_),
      next_slot_(first_public_slot), episode_first_(first_public_slot), maximum_pending_(maximum_pending_messages) {
    if (!maximum_pending_ || maximum_pending_ > 64) throw Error("Invalid Robust pending message bound");
}

bool ContinuousMessageReceiver::physical_geometry(const ReceiverSlotScore& row) {
    if (!geometry_valid(row) || (sample_rate_hz_ && sample_rate_hz_ != row.sample_rate_hz) ||
        (previous_first_ && (row.first_sample <= *previous_first_ || row.stop_sample <= *previous_stop_)))
        return false;
    sample_rate_hz_ = row.sample_rate_hz;
    previous_first_ = row.first_sample;
    previous_stop_ = row.stop_sample;
    return true;
}

void ContinuousMessageReceiver::rearm() {
    episode_ = QuietSlotMessageReceiver(decoder_, options_);
    episode_first_ = next_slot_;
    recovering_ = false;
    quiet_run_ = 0;
}

bool ContinuousMessageReceiver::consume(const ReceiverSlotScore& supplied) {
    if (stopped_) throw Error("Robust continuous message receiver is stopped");
    if (messages_.size() >= maximum_pending_) return false;
    if (supplied.index != next_slot_ || next_slot_ == std::numeric_limits<std::uint64_t>::max()) {
        rejected_ = episode_.result();
        rejected_.state = MessageState::failed;
        rejected_.reason = "public_slot_sequence_discontinuity";
        rejected_.physical_complete = rejected_.message_ready_for_decode = false;
        recovering_ = stopped_ = true;
        return true;
    }
    auto row = supplied;
    if (!physical_geometry(row)) row.complete = false;
    const auto category = QuietSlotMessageReceiver::classification(row);
    ++next_slot_;
    if (recovering_) {
        quiet_run_ = category == SlotDecision::quiet ? quiet_run_+1 : 0;
        if (quiet_run_ >= quiet_slots) rearm();
        return true;
    }
    const auto& message = episode_.consume_slot(next_slot_-1-episode_first_, row);
    if (message.physical_complete) {
        messages_.push_back({episode_first_, next_slot_, message});
        rearm();
    } else if (message.state == MessageState::resource_limit || message.state == MessageState::failed ||
               message.slots_consumed >= options_.maximum_slots) {
        rejected_ = message;
        if (rejected_.state != MessageState::resource_limit && rejected_.state != MessageState::failed) {
            rejected_.state = MessageState::resource_limit;
            rejected_.reason = "local_slot_limit_before_physical_completion";
        }
        recovering_ = true;
        quiet_run_ = category == SlotDecision::quiet ? 1 : 0;
    } else if (message.received_bits.empty()) {
        quiet_run_ = category == SlotDecision::quiet ? quiet_run_+1 : 0;
        if (quiet_run_ >= quiet_slots) rearm();
    } else quiet_run_ = 0;
    return true;
}

std::vector<CompletedMessage> ContinuousMessageReceiver::drain_messages() {
    std::vector<CompletedMessage> result;
    result.reserve(messages_.size());
    while (!messages_.empty()) {
        result.push_back(std::move(messages_.front()));
        messages_.pop_front();
    }
    return result;
}

const MessageResult& ContinuousMessageReceiver::result() const noexcept {
    return recovering_ ? rejected_ : episode_.result();
}

void ContinuousMessageReceiver::invalidate_episode(std::string reason) {
    if (stopped_) return;
    rejected_ = episode_.result();
    rejected_.state = MessageState::incomplete;
    rejected_.physical_complete = rejected_.message_ready_for_decode = false;
    // Caller supplies a local status, never received bytes. Bound even that
    // diagnostic before it can be retained by a long-lived receiver.
    rejected_.reason = reason.substr(0, 128);
    recovering_ = true;
    quiet_run_ = 0;
}

void ContinuousMessageReceiver::finish_input() {
    if (stopped_) return;
    if (!recovering_) episode_.finish_input();
    stopped_ = true;
}
void ContinuousMessageReceiver::cancel() {
    if (stopped_) return;
    if (!recovering_) episode_.cancel();
    stopped_ = true;
}

} // namespace datapump::triad::v3
