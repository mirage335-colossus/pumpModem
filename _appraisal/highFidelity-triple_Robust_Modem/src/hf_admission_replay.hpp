#pragma once

#include "datapump/triad_stream_receiver.hpp"
#include "hf_protected_reassembly.hpp"

namespace appraisal {

// A bounded downstream diagnostic. These are separate native state/codec
// evaluations of ONE scored capture, not independent receiver observations.
inline constexpr std::size_t hf_admission_replay_maximum_slots = 304;
inline constexpr std::size_t hf_admission_replay_reserve_bytes = 16u * 1024u * 1024u;

struct HfAdmissionSlot {
    std::uint64_t index = 0;
    datapump::triad::v3::SlotDecision decision{};
    std::optional<std::uint8_t> bit;
    bool rejected = false;
    double required_log_separation = 0;
};

struct HfAdmissionReplay {
    datapump::triad::v3::WrongKeyAdmission policy{};
    std::vector<HfAdmissionSlot> slots;
    std::vector<datapump::triad::v3::CompletedMessage> messages;
    std::vector<ProtectedReassemblyCandidate> recovered;
    ProtectedReassemblyReport consumer;
    datapump::triad::v3::MessageResult pending_message;
    std::uint64_t next_public_slot = 0;
    double seconds = 0;
};

// Only immutable, complete, valid current-policy scores are accepted. Every
// public boundary is drained; the configured content limit must be 1..16 bytes.
// No EOF, expected length, payload or channel truth
// is passed to either native message state or protected consumer.
HfAdmissionReplay replay_hf_admission_policy(
    const datapump::triad::v3::PublicProfile&,
    datapump::triad::v3::NativeMessageProfile,
    std::span<const datapump::triad::v3::StreamSlotReceipt>,
    datapump::triad::v3::WrongKeyAdmission,
    std::stop_token = {});

// Compare actual baseline results and replay without comparing service clocks.
bool hf_admission_matches_baseline(const HfAdmissionReplay&,
    std::span<const datapump::triad::v3::CompletedMessage>,
    std::span<const ProtectedReassemblyCandidate>, const ProtectedReassemblyReport&);

} // namespace appraisal
