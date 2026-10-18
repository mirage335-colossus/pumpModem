#pragma once
#include <optional>
#include "datapump/modem.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include <memory>

namespace datapump::modem {
namespace detail::scheduled_capture {class SharedScheduledCapture;}
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
    // Local search identity distinguishes equal-carrier clock alternatives.
    // Not a transmitted address or a source interpretation.
    // The bounded frequency bank has at most 4097 entries. Keeping this local
    // diagnostic identifier compact also preserves small receiver budgets.
    std::uint32_t frequency_hypothesis = 0;
    bool outer_presence_active = false;
    // Independent outer-code evidence when a structured in-band observation
    // activates the additional presence guard. Diagnostic, not authentication.
    double outer_presence_score = 0;
    // Receiver-local diagnostic event, created at original completed-window
    // order, never at presentation query time. Zero denotes legacy history.
    std::uint64_t completed_event = 0;
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
// A qualified or explicitly modeled UTC map, in original input coordinates. A
// canonical private stream phase p may arrive at B + scale*p +/- half_width.
// The scale is an absolute UTC-to-input map, not a relative oscillator lane.
// The caller supplies the physical timing assumptions for the initial arrival
// interval. A callback timestamp alone does not supply that contract. A model
// using uncalibrated hardware must remain labeled estimated at the application.
struct PatternStartWindow {
    long double epoch_origin_samples=0,phase_scale=1,half_width_samples=0;
};
// Immutable affine envelope of every retained scheduled arrival lane at one
// physical input coordinate. A Live generation/future-admission fence belongs
// to the caller; the receiver cannot certify not-yet-admitted banks.
struct ScheduledCaptureEnvelope {
    std::uint64_t reference_sample=0;
    SchedHopCoordinate lower,upper;
    long double minimum_rate=1,maximum_rate=1;
};
struct PatternSearch {
    // Live supplies the entire simultaneous key/profile/epoch cohort for the
    // shared CPU-budget policy. These are scheduling inputs, never wire data.
    std::size_t receiver_bank_count=1;
    // Ordinary amplitude acquisition has finite arrival windows even for a
    // public pattern. Live can price/reserve its rolling cohort separately
    // from a public continuous FFT bank. Zero inherits the original inputs.
    std::size_t amplitude_receiver_bank_count=0;
    std::size_t amplitude_strategy_workspace_bytes=0;
    // Independent schedule keys, not rolling epochs sharing those keys.
    // Used only for conservative scheduled capture-union cost accounting.
    std::size_t receiver_schedule_key_count=1;
    // Per-bank share reserved for a complete rolling cohort. Zero lets an
    // isolated/offline receiver use its supplied workspace. No coverage change.
    std::size_t receiver_strategy_workspace_bytes=0;
    // Internal resolved coarse-channel cap. Zero leaves geometry selection to
    // the amplitude constructor; reducing the cap widens cells, not coverage.
    std::size_t amplitude_channel_limit=0;
    // Absolute capture coordinate for exact shared input statistics. Separate
    // bank-local origins must not make different PCM blocks alias in a cache.
    std::uint64_t input_sample_origin=0;
    // Borrowed cohort owner, charged once by the caller before bank subdivision.
    // An absent owner retains the complete per-bank original-capture fallback.
    std::shared_ptr<detail::scheduled_capture::SharedScheduledCapture> scheduled_capture;

    // Original declared domain, before a finite complex-bank cap. Applications
    // include qualified UTC steering here; a phase-insensitive alternative must
    // cover it in full rather than inherit the clipped coherent endpoints.
    std::optional<double> admitted_frequency_half_width_hz,admitted_clock_half_width_ppm;
    // Coverage provenance from the same policy that produced authoritative
    // pairs (including UTC steering). Endpoints alone cannot certify holes or
    // joint clock/frequency coverage; absent certificates stay conservative.
    std::optional<bool> complex_frequency_coverage,complex_clock_coverage;
    // Long pattern symbols also fit four fixed sections with independent
    // complex gains. Full-symbol evidence includes the extra noise degrees
    // and detector-choice penalty; the waveform and completion rule stay fixed.
    // False retains the coherent reference detector for comparisons.
    bool drift_tolerant = true;
    // Long patterns additionally compare independent pairs of local matched
    // windows. Their duration is independent of the full symbol, rounded up
    // to whole chips and at least sixteen chips; 256 complete windows are
    // required. Zero retains the historical coherent/four-section comparison.
    // This is receiver-local configuration and never changes the waveform.
    double differential_window_seconds = 100;
    // A finite, explicit frequency bank. Empty preserves the five-bin local
    // search unless expand_clock_search is enabled by the application.
    std::vector<double> frequency_offsets_hz;
    // Authoritative finite frequency/rate pairs. A nonempty bank replaces
    // frequency_offsets_hz, clock_errors_ppm and carrier-coupling expansion;
    // no Cartesian alternatives are added. An attached Config oscillator
    // policy supplies this bank when no explicit frequency/rate override is
    // given. Without a policy, empty retains the legacy API.
    std::vector<PatternFrequencyRateHypothesis> hypotheses;
    // Authoritative pairs describe alternatives for one uncertain carrier.
    // Compare overlapping fits across the whole bank before publishing bits.
    // False preserves independent-carrier arbitration for legacy rate banks.
    bool frequency_rate_competition = true;
    // Search long pattern symbols across bounded +/-200 ppm carrier error,
    // with nominal and coupled sample-clock alternatives. See pattern_search.hpp.
    // The low-level local-search default remains usable in tiny DSP budgets.
    bool expand_clock_search = false;
    // Application policy: retain all expanded hypotheses while generating
    // their transformed rows in bounded scratch, leaving RAM for peer banks.
    bool prefer_streamed_templates = false;
    // Internal A/B diagnostic: original deferred jobs and trial charges stay
    // present; only exact canonical template/score reuse is disabled.
    bool reuse_deferred_templates = true;
    // Application-only opt-in for an automatic expanded request. If its FFT
    // core cannot fit, retain the original five nominal-clock hypotheses and
    // the full configured start window. Explicit banks remain strict.
    bool allow_local_clock_fallback = false;
    // Each carrier hypothesis also scales pattern time by 1 + offset/carrier.
    // Automatically enabled for an expanded default pattern bank with the
    // default {0} clock-rate list. Explicit frequency banks remain independent.
    // Requires FFT competition across carrier alternatives; a compact-path
    // preference cannot replace this evidence comparison. Insufficient FFT
    // workspace rejects the search rather than switching to per-lane admission.
    bool couple_clock_to_carrier = false;
    // Internal partition of an automatic bank: nominal-clock alternatives
    // precede coupled alternatives. Zero couples every explicit hypothesis.
    std::size_t uncoupled_frequency_count = 0;
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
    // Compact correlators retain the complete admitted start/phase lattice.
    // Supported short FFT banks restrict acquisition work to intersecting
    // original start cells; accepted tracks continue outside this acquisition
    // window. Unsupported geometries keep legacy work.
    std::optional<PatternStartWindow> qualified_start_window;
    // Dated public/long arrival constraint enforced only by amplitude. Auto's
    // continuous complex/FFT candidates retain their original timing domain.
    std::optional<PatternStartWindow> amplitude_start_window;
    // Public continuous acquisition partitions arrival coverage into wider
    // finite windows. Apply only after selecting amplitude; complex search
    // retains its original arrival domain. Cadence is in captured seconds.
    std::optional<double> amplitude_start_offset_seconds;
    std::optional<double> amplitude_start_uncertainty_seconds;
    std::size_t amplitude_epoch_stride_seconds = 1;
    // Extra public epoch cohorts prepared before a dated arrival opens.
    std::size_t amplitude_lookahead_epochs = 0;
    // Internal paired-measurement opt-out. Supported coherent FFT searches
    // score only the original start cells intersecting the qualified prior.
    // False preserves the preceding mixed-batch numerical work. Trial counts
    // retain their original logical alternatives. Set early_qualified_fft
    // false separately to isolate work selection from scheduling.
    bool restricted_timing_search = true;
    // Diagnostic opt-out for direct arithmetic; no coverage change. Disable
    // partitioned_timing_search too for the original full-FFT arithmetic.
    bool restricted_direct_search = true;
    // Diagnostic opt-out for paired, restricted partition/direct execution.
    // Coverage and original logical trial accounting are unchanged.
    bool partitioned_timing_search = true;
    // A qualified batch may run as soon as every retained start has a full
    // symbol. False keeps the former full-hop cadence for diagnostics.
    bool early_qualified_fft = true;
    // Independently close disjoint qualified timing components inside the
    // original logical hop. False retains the max-retained-end diagnostic.
    bool component_qualified_search = true;
    // Multi-epoch Live banks borrow their execution allowance sequentially.
    // Idle FFT receivers need observation rings, not private FFT scratch.
    // False retains the preceding allocation for diagnostic comparisons.
    bool release_idle_fft_scratch = true;
    // Independent timing alternatives for each legacy frequency hypothesis.
    // Rate is 1 + ppm*1e-6; observed symbol duration is nominal/rate.
    std::vector<double> clock_errors_ppm{0};
    // Scoring workers: zero uses all but one CPU available to this process
    // (at least one). One selects serial scoring. Search coverage and ordered
    // admission are unchanged; spare workspace bounds parallel scratch.
    std::size_t worker_threads = 0;
    // Internal paired-measurement control; never exported or persisted.
    // False retains the preceding detector for identical-PCM comparisons.
    bool outer_presence_guard = true;
};
// Whole-symbol sampled absence needed by the fixed six-second policy.
// Callers add their finite acquisition/lookahead margin when generating tails.
std::uint64_t pattern_absence_samples(const Config&);
// Conservative wall duration containing that eligible absence, including FHSS waits.
std::uint64_t pattern_absence_capture_samples(const Config&);
enum class PatternFftWindowStatus { not_requested, active, unsupported_geometry, invalid_numerics, correlator_fallback };
struct PatternFftWork {
    // Acquisition only: one input transform and one pair-of-bits job per
    // executed stream-index/phase/frequency alternative (a cancelled job may
    // have started). These are execution counters, not timing measurements.
    std::uint64_t input_transforms=0,template_jobs=0;
    std::uint64_t exact_reused_template_jobs=0;
    std::uint64_t skipped_batches=0,skipped_start_trials=0,threshold_trials=0;
    // Acquisition start cells only; threshold_trials additionally includes
    // unchanged tracked timing/frequency refinements. A successful dispatch
    // scores every retained cell exactly once, without charging bit choices.
    std::uint64_t retained_start_trials=0,scored_start_trials=0;
    std::uint64_t early_batches=0;
    std::uint64_t timing_components=0,component_fallbacks=0;
    std::uint64_t direct_jobs=0,fft_jobs=0,skipped_template_jobs=0;
    std::uint64_t partitioned_jobs=0,partitioned_input_transforms=0,partitioned_transform_points=0;
    std::uint64_t partitioned_fallbacks=0;
    // Executed scores include bounded unpublished EOF replay; threshold_trials
    // reflects the original finite-final charge rather than repeated arithmetic.
    std::uint64_t eof_replays=0;
    // INT8 is a certified screening stage. Possible retained candidates are
    // verified from original operands before entering the unchanged detector.
    std::uint64_t int8_jobs=0,int8_dots=0,certified_rejects=0,exact_refines=0,fp32_dots=0;
    // Branch-specific fallback events, not a fraction of template_jobs: a
    // partition context, an unsafe whole-FFT bit, or an input batch can each
    // trigger one event. Compact mode reports its FP64 template-job count.
    std::uint64_t arithmetic_fallback_jobs=0;
    std::uint64_t native_fp32_transforms=0,tabulated_fp64_transforms=0;
    // Batch counts distinguish acquisition/continuation; transform/refinement
    // totals and peak transient bytes include both stages.
    std::uint64_t frequency_axis_batches=0,frequency_axis_transforms=0,frequency_axis_refinements=0;
    std::uint64_t frequency_axis_tracking_batches=0,frequency_axis_tracking_fallbacks=0;
    std::size_t frequency_axis_peak_bytes=0;
    std::uint64_t frequency_axis_fallback_batches=0,frequency_axis_fallback_starts=0;
    std::uint64_t frequency_axis_crossover_fallbacks=0,frequency_axis_workspace_fallbacks=0;
    std::uint64_t fp32_retry_fp64_transforms=0;
    std::size_t native_scratch_peak_bytes=0;
    // Lane jobs offered the cache; a numerical retry can bypass its values.
    std::uint64_t private_template_builds=0,private_template_lane_jobs=0;
    std::size_t private_template_peak_bytes=0;
    bool restricted_timing_active=false;
    PatternFftWindowStatus timing_window=PatternFftWindowStatus::not_requested;
};
class PatternReceiver {
public:
    PatternReceiver(Config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                    PatternSearch search = {});
    ~PatternReceiver();
    PatternReceiver(PatternReceiver&&) noexcept;
    PatternReceiver& operator=(PatternReceiver&&) noexcept;
    void push(std::span<const float>, std::stop_token = {});
    // Scheduled nulls only; never fabricates received absence. A false return
    // leaves all state unchanged and requires the original input samples.
    bool try_skip_unobserved(std::uint64_t samples, std::stop_token = {});
    std::optional<std::uint64_t> unobserved_end_from(std::uint64_t local_sample) const;
    std::optional<ScheduledCaptureEnvelope> scheduled_capture_envelope(std::uint64_t local_sample) const;
    // Freeze fresh first-start searches, retaining every original acquisition
    // window and existing continuation. Used by the bank-wide Burst barrier.
    void suspend_fresh_acquisition();
    bool symbol_burst_tracking_active() const;
    // Shared per-sample carrier projection for a bank with one carrier/clock.
    // projected[i] is raw[i] * exp(-j*carrier_phase[i]); an arbitrary fixed
    // projection phase is fitted by the pattern score. Both spans must match.
    // Sample-resolution and local differential Gram fits reconstruct the carrier from raw samples
    // to keep their real basis matrix independent of that arbitrary rotation.
    void push(std::span<const float>,std::span<const std::complex<double>> projected,std::stop_token = {});
    // Internal bank-scoped reuse of exact carrier moments over immutable PCM.
    void push(std::span<const float>,std::span<const std::complex<double>> projected,
              std::stop_token,detail::CorrelationProjectionCache*);
    // Flush available decisions as incomplete; capture EOF is not stream end.
    void finish(std::stop_token = {});
    // Drain accepted decisions, including a final short chunk, as pending.
    // Only a physical absence event marks the stream complete.
    std::vector<PatternBurst> take_bursts();
    // Receiver-worker boundary only, after push/process_pending/take_bursts.
    // Retained tracks and queued accepted chunks can still publish this identity.
    // False permits releasing its local state, never declaring physical END.
    bool retains_stream(std::uint64_t first_sample, std::uint64_t first_symbol) const;
    PatternBurst provisional() const;
    // Numerical acquisition may defer diagnostic history; admission/progress never wait for it.
    // Receiver-worker only: score retained observations with cancellable bounded batching.
    // Publication polls never perform the deferred search or manufacture input.
    void process_pending(std::stop_token = {});
    bool deferred_pattern_history() const noexcept;
    std::uint64_t diagnostic_history_generation() const noexcept;
    std::uint64_t diagnostic_history_resolved_through() const noexcept;
    std::uint64_t oldest_unresolved_diagnostic_event() const noexcept;

    // Receiver-local original completion-event commit frontier. This does not
    // certify that uncompleted-symbol replay arithmetic has already executed.
    std::optional<std::uint64_t> delayed_processing_frontier() const noexcept;
    std::vector<PatternEvidence> candidates() const;
    // Latest retained hypotheses in append order, without copying older entries.
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<PatternEvidence> candidates(std::size_t limit, std::stop_token) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // The original finite start window has received complete acquisition
    // scoring. Independent of admission, message completion and wall time;
    // false without a finite start hint. EOF cannot supply missing samples.
    bool initial_search_complete() const;
    // True when acquisition covers an explicit finite system-clock window.
    bool clock_windowed() const;
    // The application requested expansion but only the original local
    // five-frequency search fitted its workspace. This is reduced coverage.
    bool local_clock_fallback() const;
    // A peak/continuation was omitted at the configured candidate/track quota.
    // Latched until reconstruction; normal competing-score rejection and
    // diagnostic history eviction do not set this. Not proof of a lost bit.
    bool candidate_limited() const;
    // Whether the additional section detector fits this profile and budget.
    bool drift_tolerant() const;
    PatternFftWork fft_work() const;
    std::size_t working_bytes() const;
    static std::size_t symbol_burst_control_bytes();
    // Includes admitted detector state whose allocation is deferred until PCM.
    std::size_t reserved_workspace_bytes() const;
    // Additional allocation bound for one finite PCM push and its next drain.
    // SIZE_MAX disables optional shared preprocessing for unsupported geometry.
    std::size_t projection_cache_headroom(std::size_t input_samples) const;
    // No allocation: nullopt unsupported, zero eligible but cold/reselecting,
    // positive only after the original worker and Temporal choices are retained.
    std::optional<std::size_t> temporal_plan_workers() const;
    // Reject a reduction that cannot retain current state. Future candidate
    // growth is capped within the new ceiling; excess payload throws Error.
    void set_workspace_bytes(std::size_t);
    void trim_optional_workspace();
    Diagnostics diagnostics() const;
private:
    friend struct PatternReceiverStartupGuideTestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
