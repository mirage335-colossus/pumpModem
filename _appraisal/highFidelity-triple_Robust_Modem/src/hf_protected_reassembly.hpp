#pragma once

#include "datapump/triad_message_receiver.hpp"

#include <array>
#include <string_view>

namespace appraisal {

enum class ProtectedReassemblyBoundary { session_changed, native_recovery, input_closed };

struct ProtectedReassemblyCandidate {
    std::uint64_t first_public_slot = 0, stop_public_slot = 0; // Stop includes the final scored quiet pair.
    std::size_t bit_positions = 0, erased_positions = 0, fragments = 0;
    datapump::StreamContent content;
};

struct ProtectedReassemblyReport {
    std::uint64_t events = 0, seeds = 0, appends = 0, decode_attempts = 0, candidates = 0;
    std::uint64_t malformed_events = 0, expirations = 0, resets = 0;
    std::size_t pending_positions = 0, pending_erased_positions = 0, pending_fragments = 0;
    std::size_t last_attempt_positions = 0, last_attempt_erasures = 0, last_gap_positions = 0;
    std::optional<std::uint64_t> pending_first_public_slot;
    std::string_view last_action = "empty";
    double decode_seconds = 0, service_seconds = 0;
};

// Local experimental consumer of immutable canonical completion events. It
// has one fixed short RS20 hypothesis. It never replays receiver work, creates
// a completion, changes a native message, or infers content from an EOF.
class HfProtectedReassembly {
public:
    static constexpr std::size_t maximum_positions = 304;

    explicit HfProtectedReassembly(datapump::FecMode, std::uint64_t first_public_slot = 0);

    // Deliver all newly drained completion events in public order before
    // advancing past their endpoints. Reset as soon as a native recovery or
    // session boundary is observed. A FEC mode change requires a new owner.
    std::optional<ProtectedReassemblyCandidate> consume(const datapump::triad::v3::CompletedMessage&);
    void advance(std::uint64_t next_committed_slot);
    void reset(std::uint64_t next_committed_slot, ProtectedReassemblyBoundary);

    const ProtectedReassemblyReport& report() const noexcept { return report_; }

private:
    const datapump::FecMode fec_;
    std::uint64_t earliest_event_first_, next_committed_;
    bool closed_ = false;
    std::array<datapump::triad::v3::MessageBit, maximum_positions> positions_{};
    ProtectedReassemblyReport report_;

    void clear_pending(std::string_view action);
};

} // namespace appraisal
