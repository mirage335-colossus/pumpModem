#include "hf_protected_reassembly.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace appraisal {
namespace {

using namespace datapump::triad::v3;
using Clock = std::chrono::steady_clock;
constexpr std::size_t required_quiet = 2;

struct Timer {
    double& seconds;
    Clock::time_point began = Clock::now();
    ~Timer() { seconds += std::chrono::duration<double>(Clock::now() - began).count(); }
};

bool physical_completion(const CompletedMessage& event) {
    const auto& message = event.message;
    return message.physical_complete && message.slots_consumed > required_quiet &&
        event.stop_public_slot > event.first_public_slot &&
        event.stop_public_slot - event.first_public_slot == message.slots_consumed &&
        message.trailing_quiet_slots.size() == required_quiet &&
        message.trailing_quiet_slots[0] == message.slots_consumed - required_quiet &&
        message.trailing_quiet_slots[1] == message.slots_consumed - 1;
}

bool valid_short_fragment(const MessageResult& message) {
    const auto bits = std::span(message.bit_slots);
    if (bits.empty() || bits.size() > HfProtectedReassembly::maximum_positions ||
        bits.size() > message.slots_consumed - required_quiet || !bits.front() ||
        message.observed_bits > bits.size() || message.missing_slots != bits.size() - message.observed_bits ||
        message.received_bits.size() != message.observed_bits || message.erasure_slot_indices.size() != message.missing_slots)
        return false;

    // ContinuousMessageReceiver rearms during a pair of leading quiet slots;
    // a qualifying reject can still contain one skipped leading quiet slot.
    // The event origin therefore need not be the address of bit_slots[0].
    const auto leading = message.slots_consumed - required_quiet - bits.size();
    const auto [ambiguous, data, quiet] = message.classification_counts;
    if (leading > 1 || data != message.observed_bits || ambiguous > message.missing_slots ||
        quiet != message.missing_slots - ambiguous + leading + required_quiet)
        return false;

    std::size_t received = 0, erased = 0;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (const auto bit = bits[i]) {
            if (received >= message.received_bits.size() || *bit > 1 || *bit != message.received_bits[received++])
                return false;
        } else if (erased >= message.erasure_slot_indices.size() || message.erasure_slot_indices[erased++] != leading + i) {
            return false;
        }
    }
    return received == message.observed_bits && erased == message.missing_slots;
}

} // namespace

HfProtectedReassembly::HfProtectedReassembly(datapump::FecMode fec, std::uint64_t first_public_slot)
    : fec_(fec), earliest_event_first_(first_public_slot), next_committed_(first_public_slot) {}

void HfProtectedReassembly::clear_pending(std::string_view action) {
    report_.last_action = action;
    report_.pending_first_public_slot.reset();
    report_.pending_positions = 0;
    report_.pending_erased_positions = 0;
    report_.pending_fragments = 0;
}

std::optional<ProtectedReassemblyCandidate> HfProtectedReassembly::consume(const CompletedMessage& event) {
    const Timer service{report_.service_seconds};
    ++report_.events;
    if (closed_ || fec_ != datapump::FecMode::rs20) {
        clear_pending(closed_ ? "input_closed" : "unsupported_fec");
        return {};
    }
    if (event.first_public_slot < earliest_event_first_ || event.stop_public_slot <= next_committed_ ||
        !physical_completion(event)) {
        ++report_.malformed_events;
        clear_pending("unordered_or_unqualified_completion");
        return {};
    }
    earliest_event_first_ = next_committed_ = event.stop_public_slot;

    const auto& message = event.message;
    if (message.state == MessageState::complete && message.decoded_content && message.message_ready_for_decode) {
        clear_pending("native_message_accepted");
        return {};
    }
    if (message.state != MessageState::invalid_message || message.reason != "codec_rejected_message" ||
        message.long_form || message.decoded_content || message.message_ready_for_decode) {
        clear_pending("unsupported_completion");
        return {};
    }
    if (!valid_short_fragment(message)) {
        ++report_.malformed_events;
        clear_pending("inconsistent_native_fragment");
        return {};
    }

    const auto stop = event.stop_public_slot - required_quiet;
    const auto first = stop - message.bit_slots.size();
    if (!report_.pending_first_public_slot) {
        if (message.bit_slots.size() > maximum_positions - required_quiet - 1) {
            clear_pending("fragment_too_long_to_extend");
            return {};
        }
        std::ranges::copy(message.bit_slots, positions_.begin());
        ++report_.seeds;
        report_.pending_first_public_slot = first;
        report_.pending_positions = message.bit_slots.size();
        report_.pending_erased_positions = message.missing_slots;
        report_.pending_fragments = 1;
        report_.last_action = "seeded_rejected_short_fragment";
        return {};
    }

    const auto start = *report_.pending_first_public_slot;
    const auto previous_stop = start + report_.pending_positions;
    if (first < previous_stop || first - previous_stop < required_quiet || stop - start > maximum_positions) {
        clear_pending("fragment_overlap_or_position_limit");
        return {};
    }
    const auto gap = static_cast<std::size_t>(first - previous_stop);
    const auto offset = static_cast<std::size_t>(first - start);
    auto output = std::span(positions_);
    std::ranges::fill(output.subspan(report_.pending_positions, gap), MessageBit{});
    std::ranges::copy(message.bit_slots, output.begin() + offset);
    ++report_.appends;
    ++report_.pending_fragments;
    report_.pending_positions = static_cast<std::size_t>(stop - start);
    report_.pending_erased_positions += gap + message.missing_slots;
    report_.last_attempt_positions = report_.pending_positions;
    report_.last_attempt_erasures = report_.pending_erased_positions;
    report_.last_gap_positions = gap;

    ++report_.decode_attempts;
    const Timer decode{report_.decode_seconds};
    try {
        auto decoded = decode_message_bits(output.first(report_.pending_positions), fec_);
        ProtectedReassemblyCandidate candidate{start, event.stop_public_slot, report_.pending_positions,
            report_.pending_erased_positions, report_.pending_fragments, std::move(decoded)};
        ++report_.candidates;
        clear_pending("later_physical_completion_recovered_candidate");
        return candidate;
    } catch (const datapump::Error&) {
        report_.last_action = "combined_short_frame_rejected";
        return {};
    }
}

void HfProtectedReassembly::advance(std::uint64_t next_committed_slot) {
    if (next_committed_slot < next_committed_) {
        clear_pending("committed_cursor_regressed");
        return;
    }
    next_committed_ = next_committed_slot;
    // Since the caller drains before advancing, at this boundary every
    // completion that could possibly fit this hypothesis has been seen.
    if (report_.pending_first_public_slot &&
        next_committed_ - *report_.pending_first_public_slot >= maximum_positions + required_quiet) {
        ++report_.expirations;
        clear_pending("public_position_window_expired");
    }
}

void HfProtectedReassembly::reset(std::uint64_t next_committed_slot, ProtectedReassemblyBoundary boundary) {
    if (boundary != ProtectedReassemblyBoundary::session_changed)
        next_committed_slot = std::max(next_committed_slot, next_committed_);
    earliest_event_first_ = next_committed_ = next_committed_slot;
    ++report_.resets;
    if (boundary == ProtectedReassemblyBoundary::session_changed) closed_ = false;
    if (boundary == ProtectedReassemblyBoundary::input_closed) closed_ = true;
    clear_pending(boundary == ProtectedReassemblyBoundary::session_changed ? "session_changed" :
        boundary == ProtectedReassemblyBoundary::native_recovery ? "native_recovery" : "input_closed");
}

} // namespace appraisal
