#include "hf_admission_replay.hpp"
#include "triad_receiver_driver.hpp"

#include <chrono>
#include <tuple>

namespace appraisal {
namespace {
using namespace datapump;
using namespace datapump::triad::v3;

bool same_region(const FecRegionStats& a, const FecRegionStats& b) {
    return std::tie(a.received_bits, a.corrected_bits, a.missing_bits, a.corrected_bytes, a.erased_bytes, a.repaired_bytes) ==
        std::tie(b.received_bits, b.corrected_bits, b.missing_bits, b.corrected_bytes, b.erased_bytes, b.repaired_bytes);
}

bool same_content(const StreamContent& a, const StreamContent& b) {
    const auto& x = a.message;
    const auto& y = b.message;
    if (std::tie(x.kind, x.data, x.filename, x.callsign, x.grid, x.repeatable, x.local_id) !=
        std::tie(y.kind, y.data, y.filename, y.callsign, y.grid, y.repeatable, y.local_id) ||
        std::tie(a.corrected_bytes, a.consumed_bytes, a.authenticated) !=
        std::tie(b.corrected_bytes, b.consumed_bytes, b.authenticated) ||
        a.pre_fec_accuracy.has_value() != b.pre_fec_accuracy.has_value()) return false;
    if (a.pre_fec_accuracy) {
        const auto& p = *a.pre_fec_accuracy;
        const auto& q = *b.pre_fec_accuracy;
        if (std::tie(p.received_data_bits, p.corrected_data_bits, p.missing_data_bits) !=
            std::tie(q.received_data_bits, q.corrected_data_bits, q.missing_data_bits)) return false;
    }
    return same_region(a.fec_stats.data, b.fec_stats.data) &&
        same_region(a.fec_stats.integrity, b.fec_stats.integrity) && same_region(a.fec_stats.parity, b.fec_stats.parity);
}

bool same_message(const MessageResult& a, const MessageResult& b) {
    if (std::tie(a.state, a.reason, a.physical_complete, a.message_ready_for_decode, a.reacquisition_required,
        a.slots_consumed, a.reacquisition_count, a.observed_bits, a.missing_slots, a.recognized_markers,
        a.damaged_markers, a.long_form, a.received_bits, a.bit_slots, a.erasure_slot_indices, a.trailing_quiet_slots,
        a.filtered_text, a.classification_counts, a.represented_samples, a.sample_rate_hz, a.represented_baseband_seconds) !=
        std::tie(b.state, b.reason, b.physical_complete, b.message_ready_for_decode, b.reacquisition_required,
        b.slots_consumed, b.reacquisition_count, b.observed_bits, b.missing_slots, b.recognized_markers,
        b.damaged_markers, b.long_form, b.received_bits, b.bit_slots, b.erasure_slot_indices, b.trailing_quiet_slots,
        b.filtered_text, b.classification_counts, b.represented_samples, b.sample_rate_hz, b.represented_baseband_seconds)) return false;
    const auto& x = a.wrong_key_evidence;
    const auto& y = b.wrong_key_evidence;
    if (std::tie(x.available, x.data_bits, x.passing_bits) != std::tie(y.available, y.data_bits, y.passing_bits) ||
        a.decoded_content.has_value() != b.decoded_content.has_value()) return false;
    return !a.decoded_content || same_content(*a.decoded_content, *b.decoded_content);
}

bool same_consumer(const ProtectedReassemblyReport& a, const ProtectedReassemblyReport& b) {
    return std::tie(a.events, a.seeds, a.appends, a.decode_attempts, a.candidates, a.malformed_events,
        a.expirations, a.resets, a.pending_positions, a.pending_erased_positions, a.pending_fragments,
        a.last_attempt_positions, a.last_attempt_erasures, a.last_gap_positions, a.pending_first_public_slot, a.last_action) ==
        std::tie(b.events, b.seeds, b.appends, b.decode_attempts, b.candidates, b.malformed_events,
        b.expirations, b.resets, b.pending_positions, b.pending_erased_positions, b.pending_fragments,
        b.last_attempt_positions, b.last_attempt_erasures, b.last_gap_positions, b.pending_first_public_slot, b.last_action);
}
} // namespace

HfAdmissionReplay replay_hf_admission_policy(const PublicProfile& profile, NativeMessageProfile message,
    std::span<const StreamSlotReceipt> receipts, WrongKeyAdmission policy, std::stop_token stop) {
    const auto began = std::chrono::steady_clock::now();
    (void)wrong_key_admission_name(policy);
    if (receipts.empty() || receipts.size() > hf_admission_replay_maximum_slots)
        throw Error("admission replay requires 1..304 complete public observations");
    if (!message.content_limit || message.content_limit > 16)
        throw Error("admission replay is bounded to the configured 1..16-byte native message profile");
    const auto first = receipts.front().score.index;
    ContinuousMessageReceiver controller(message, [&profile](std::uint64_t index) {
        return message_address(profile, index);
    }, first);
    HfProtectedReassembly consumer(message.fec, first);
    HfAdmissionReplay result;
    result.policy = policy;
    result.slots.reserve(receipts.size());
    for (std::size_t i = 0; i < receipts.size(); ++i) {
        if (stop.stop_requested()) throw Error("admission replay cancelled");
        const auto& receipt = receipts[i];
        const auto& baseline = receipt.score;
        const auto address = message_address(profile, baseline.index);
        if (!baseline.complete || !baseline.all_valid || baseline.index != first + i ||
            baseline.wrong_key_admission != WrongKeyAdmission::current || baseline.wrong_key_rejected ||
            QuietSlotMessageReceiver::classification(baseline) != baseline.decision ||
            receipt.pattern_epoch != address.epoch || receipt.symbol_ordinal != address.ordinal)
            throw Error("admission replay requires unchanged ordered valid current-policy public receipts");
        auto score = baseline;
        receiver_detail::apply_wrong_key_admission(score, policy);
        // Mirror the stream owner's final classification; the admission rule
        // cannot promote non-DATA or turn rejected DATA into physical silence.
        score.decision = QuietSlotMessageReceiver::classification(score);
        if (score.decision != SlotDecision::data) score.bit.reset();
        if ((score.decision == SlotDecision::quiet) != (baseline.decision == SlotDecision::quiet) ||
            score.operational_bit_evidence != baseline.operational_bit_evidence ||
            score.operational_passes_conditional_bound != baseline.operational_passes_conditional_bound ||
            score.operational_log_e != baseline.operational_log_e)
            throw Error("admission replay changed presence evidence or physical quiet");
        if (!controller.consume(score) || controller.stopped() || controller.recovering())
            throw Error("admission replay native message state rejected the public stream");
        result.slots.push_back({score.index, score.decision, score.bit, score.wrong_key_rejected,
            score.wrong_key_required_log_separation});
        for (auto& event : controller.drain_messages()) {
            if (auto candidate = consumer.consume(event)) result.recovered.push_back(std::move(*candidate));
            result.messages.push_back(std::move(event));
        }
        consumer.advance(controller.next_slot_index());
    }
    result.consumer = consumer.report();
    result.pending_message = controller.result();
    result.next_public_slot = controller.next_slot_index();
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return result;
}

bool hf_admission_matches_baseline(const HfAdmissionReplay& replay, std::span<const CompletedMessage> messages,
    std::span<const ProtectedReassemblyCandidate> recovered, const ProtectedReassemblyReport& consumer) {
    if (replay.policy != WrongKeyAdmission::current || replay.messages.size() != messages.size() ||
        replay.recovered.size() != recovered.size() || !same_consumer(replay.consumer, consumer)) return false;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto& a = replay.messages[i];
        const auto& b = messages[i];
        if (a.first_public_slot != b.first_public_slot || a.stop_public_slot != b.stop_public_slot ||
            !same_message(a.message, b.message)) return false;
    }
    for (std::size_t i = 0; i < recovered.size(); ++i) {
        const auto& a = replay.recovered[i];
        const auto& b = recovered[i];
        if (std::tie(a.first_public_slot, a.stop_public_slot, a.bit_positions, a.erased_positions, a.fragments) !=
            std::tie(b.first_public_slot, b.stop_public_slot, b.bit_positions, b.erased_positions, b.fragments) ||
            !same_content(a.content, b.content)) return false;
    }
    return true;
}
} // namespace appraisal
