#pragma once
#include "datapump/modem.hpp"
#include <algorithm>
#include <memory>

namespace datapump::modem {
// Cross-profile presentation weight for one admitted symbol. Native scores
// have different conservative bounds on different DSP paths; cap each symbol
// separately so a strong prefix cannot lend its surplus to a weak later fit.
// This weight changes neither symbol admission nor physical completion.
inline double pattern_symbol_support(double known_samples,double log_evidence,std::uint64_t chip_samples) {
    return std::min(known_samples,std::max(0.,log_evidence)*static_cast<double>(chip_samples));
}
// Scores use natural-log evidence against an ideal circular-Gaussian reference.
// Real-PCM covariance and adaptive searches require empirical calibration;
// neither a lifetime false-alarm guarantee nor authentication is implied.
struct PatternEvidence {
    std::uint64_t first_sample = 0, end_sample = 0, stream_symbol = 0;
    double frequency_hz = 0, score = 0, alternative_score = 0;
    unsigned bit = 0;
    std::uint64_t stream_phase_samples = 0;
    // Single-symbol admission reference when retained, in the same log-evidence
    // units. Diagnostic only: alternative margins and chain evidence also gate
    // admission, and later search trials can raise this reference.
    double admission_threshold = 0;
};
// Internal unknown-slot value; transfer interpretation replaces it with a
// plaintext zero after advancing the Data mask through the same symbol slot.
inline constexpr std::uint8_t missing_pattern_bit = 2;
struct PatternBurst {
    // 0/1 decisions, plus missing_pattern_bit when gap preservation is enabled.
    Bytes bits;
    std::uint64_t first_sample = 0, end_sample = 0;
    std::uint64_t first_stream_symbol = 0;
    double frequency_hz = 0, score = 0;
    // True only after six seconds of observed search absence. EOF and a
    // pending gap never mark completion. A terminal event may contain no bits.
    bool complete = false;
    std::uint64_t stream_phase_samples = 0;
    // Stable local identity across drainable chunks. The ordinary first_*
    // fields above describe this chunk's original sample/symbol coordinates.
    std::uint64_t stream_first_sample = 0, stream_first_symbol = 0;
    // Compact all-unknown run, mutually exclusive with bits. It is emitted
    // only after a later found symbol confirms the run's observed extent.
    std::size_t missing_slots = 0;
    // Cumulative per-symbol supported media samples for confirmed decisions.
    // Missing positions add zero; draining chunks never resets this total.
    double support_samples = 0;
};
// Completed failed-symbol observations covering this much received-media
// time end the stream. One failed symbol suffices when it lasts >=6s.
inline constexpr std::uint64_t pattern_absence_seconds = 6;
struct PatternSearch {
    // A finite, explicit frequency bank. Empty selects five offsets separated
    // by 1/(4 symbol duration). No carrier/constellation lock precedes scoring.
    std::vector<double> frequency_offsets_hz;
    // Nominal per-search significance, before finite-search trial penalties.
    // Correlated interference still needs independent pattern evidence.
    double false_alarm_probability = 1e-10;
    double retain_score = 5;
    // Unknown interior slots retain their original symbol positions. Consumers
    // must preserve missing_pattern_bit through byte packing/erasure handling.
    // Maximum decisions per event, not a minimum before presentation.
    // Confirmed decisions drain without ending the stream; actual chunk
    // capacity is also capped by the available decision-storage budget.
    std::size_t chunk_bits = 1024;
    std::size_t candidate_limit = 2048, track_limit = 16, bit_limit = 1024 * 1024;
    // Independently try these first stream positions (0 through count-1).
    // This bounded local keystream search adds no transmitted metadata.
    std::size_t initial_stream_symbols = 4;
    // An independently admitted whole-second epoch can begin between symbol
    // boundaries. Search the finite phase lattice inherited from an original
    // whole-second start; equivalent addresses share one template/track.
    bool search_stream_phases = false;
    // Prefer the clock-window correlator with compact diagnostic histories
    // and projection scratch. Retains every requested correlation hypothesis;
    // requires start_offset_seconds and does not change confidence gates.
    bool compact_clock_search = false;
    // Optional system-clock prediction of symbol zero relative to the first
    // supplied sample. The long-symbol correlator searches this entire window
    // at half-chip resolution; it rejects unaffordable coverage explicitly.
    std::optional<double> start_offset_seconds;
    double start_uncertainty_seconds = 0;
    std::vector<double> clock_errors_ppm{0};
};
// Whole-symbol sampled absence needed by the fixed six-second policy.
// Callers add their finite acquisition/lookahead margin when generating tails.
std::uint64_t pattern_absence_samples(const Config&);
class PatternReceiver {
public:
    PatternReceiver(Config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                    PatternSearch search = {});
    ~PatternReceiver();
    PatternReceiver(PatternReceiver&&) noexcept;
    PatternReceiver& operator=(PatternReceiver&&) noexcept;
    void push(std::span<const float>, std::stop_token = {});
    // Shared per-sample carrier projection for a bank with one carrier/clock.
    // projected[i] is raw[i] * exp(-j*carrier_phase[i]); an arbitrary fixed
    // projection phase is fitted by the pattern score. Both spans must match.
    // Sample-resolution Gram fits reconstruct the carrier from raw samples
    // to keep their real basis matrix independent of that arbitrary rotation.
    void push(std::span<const float>,std::span<const std::complex<double>> projected,std::stop_token = {});
    // Flush available decisions as incomplete; capture EOF is not stream end.
    void finish(std::stop_token = {});
    // Drain accepted decisions, including a final short chunk, as pending.
    // Only a physical absence event marks the stream complete.
    std::vector<PatternBurst> take_bursts();
    PatternBurst provisional() const;
    std::vector<PatternEvidence> candidates() const;
    // Latest retained hypotheses in append order, without copying older entries.
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // True when acquisition covers an explicit finite system-clock window.
    bool clock_windowed() const;
    std::size_t working_bytes() const;
    // Reject a reduction that cannot retain current state. Future candidate
    // growth is capped within the new ceiling; excess payload throws Error.
    void set_workspace_bytes(std::size_t);
    Diagnostics diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
