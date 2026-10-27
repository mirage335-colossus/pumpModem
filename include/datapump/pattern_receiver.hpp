#pragma once
#include <array>
#include <bit>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <limits>
#include <optional>
#include "datapump/modem.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include <memory>

namespace datapump::helper {class JointClockCellCertificate;class ContinuationFrequencyCertificate;}
namespace datapump::modem {
namespace detail::scheduled_capture {class SharedScheduledCapture;}
namespace detail { class NativePeakLease; }
namespace detail {class NativePointLease;}
namespace detail {struct NativeDiagnosticHistoryAccess;}
namespace detail {struct ConditionedReferenceDescriptor;}
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
    // A phase-view fit reports its frozen affine helper-reference coordinate,
    // not an instantaneous raw-carrier estimate. Trace labels the view ID.
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
    // Same reference-coordinate convention as PatternEvidence::frequency_hz.
    double frequency_hz = 0, score = 0;
    // True only after six seconds of observed search absence. EOF and a
    // pending gap never mark completion. A terminal event may contain no bits.
    bool complete = false;
    // Legacy continuous-helper overlap/source-identity policy tag. Keep its
    // arbitration semantics separate from observation-only birth provenance.
    bool helper_acquired = false;
    // True only when this stream was born in an executed conditional helper
    // acquisition pass. This durable observation never controls arbitration,
    // payload progress, framing or physical completion.
    bool helper_conditioned_acquisition = false;
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
// Conservative ordinary private-stream alignment, without payload contents.
// The clock cone includes native continuation's bounded timing refinements.
struct PatternSourceIdentity {
    std::uint64_t epoch=0,first_symbol=0;
    double clock_half_width_ppm=0;
    std::uint32_t phase_lower=0,phase_upper=0,sample_rate=0;
};
// True only when the entire physical overlap has disjoint source addresses.
// Unsupported/ambiguous geometry retains the existing overlap arbitration.
bool pattern_source_identities_disjoint(const PatternSourceIdentity&,std::uint64_t first_a,
    std::uint64_t end_a,std::uint64_t symbol_a,const PatternSourceIdentity&,std::uint64_t first_b,
    std::uint64_t end_b,std::uint64_t symbol_b)noexcept;
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
// Independently qualified birth-only clock domain. This does not rewrite the
// ordinary baseline oscillator/coverage report. Explicit original pairs retain
// their actual nonzero clock ratios and must cover this entire interval.
struct PatternClockInterval {double lower_ppm=0,upper_ppm=0,cell_half_width_ppm=0;};
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
    std::optional<PatternClockInterval> qualified_clock_interval;
    // Radius represented by each original paired timing lane. Thin independent
    // strips can have a larger cell than half the adjacent rate-center gap.
    // This provenance comes from the same builder as authoritative hypotheses.
    std::optional<double> admitted_clock_cell_half_width_ppm;
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
// Conservative closed-cell validation for an explicitly supplied conditional
// domain. It creates no clock hypotheses, and missing provenance stays absent.
inline bool pattern_clock_interval_valid(const PatternSearch& search) noexcept {
    if(!search.qualified_clock_interval)return true;
    const auto& interval=*search.qualified_clock_interval;
    if(search.hypotheses.empty() || search.hypotheses.size()>maximum_pattern_frequency_rate_hypotheses ||
       !std::isfinite(interval.lower_ppm) || !std::isfinite(interval.upper_ppm) ||
       !std::isfinite(interval.cell_half_width_ppm) || interval.lower_ppm>interval.upper_ppm ||
       interval.lower_ppm < -10000 || interval.upper_ppm>10000 || interval.cell_half_width_ppm<0 || interval.cell_half_width_ppm>10000)return false;
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || FLT_EVAL_METHOD!=0
    return false;
#else
    if(!std::numeric_limits<double>::is_iec559 || sizeof(double)!=sizeof(std::uint64_t) || std::fegetround()!=FE_TONEAREST)return false;
    volatile double tiny=std::numeric_limits<double>::denorm_min(),one=1;
    volatile double normal=std::numeric_limits<double>::min(),half=.5;
    const double input=tiny*one,output=normal*half;
    if(std::bit_cast<std::uint64_t>(input)!=1 || std::bit_cast<std::uint64_t>(output)!=0x0008000000000000ULL)return false;
#endif
    std::array<double,maximum_pattern_frequency_rate_hypotheses> rates{};
    for(std::size_t i=0;i<search.hypotheses.size();++i) {
        const auto rate=search.hypotheses[i].clock_error_ppm;
        if(!std::isfinite(rate) || std::abs(rate)>10000)return false;
        rates[i]=rate;
    }
    const auto count=search.hypotheses.size();std::sort(rates.begin(),rates.begin()+static_cast<std::ptrdiff_t>(count));
    if(interval.cell_half_width_ppm==0)
        return interval.lower_ppm==interval.upper_ppm && std::binary_search(rates.begin(),rates.begin()+static_cast<std::ptrdiff_t>(count),interval.lower_ppm);
    // Error-free two-sum preserves exactly touching representable cells.
    // Only actual inexact rounding contracts an inner endpoint; no tolerance
    // is allowed to bridge a real or unproved coverage gap.
    const auto sum=[](double a,double b) {
        const auto value=a+b,bv=value-a,error=(a-(value-bv))+(b-bv);
        const auto infinity=std::numeric_limits<double>::infinity();
        return std::array<double,2>{error<0?std::nextafter(value,-infinity):value,
            error>0?std::nextafter(value,infinity):value};
    };
    auto covered=interval.lower_ppm;
    for(std::size_t i=0;i<count;++i) {
        const auto lower=sum(rates[i],-interval.cell_half_width_ppm)[1];
        const auto upper=sum(rates[i],interval.cell_half_width_ppm)[0];
        if(!std::isfinite(lower) || !std::isfinite(upper))return false;
        if(upper<covered)continue;
        if(lower>covered)return false;
        covered=upper;if(covered>=interval.upper_ppm)return true;
    }
    return false;
}
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
    std::uint64_t conditional_template_jobs=0,original_audit_template_jobs=0,deferred_original_batches=0;
    std::uint64_t shifted_reference_trials=0;
    // Measured optional parallel preparation, separated from executed jobs.
    // Conditional preparation must not initialize the full original matrix.
    std::size_t conditional_prepared_jobs_peak=0,original_prepared_jobs_peak=0;
    std::size_t conditional_scoring_peak_bytes=0,original_scoring_peak_bytes=0;
    std::uint64_t phase_view_template_jobs=0,phase_view_tracking_fits=0,phase_corrected_bins=0;
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
// Receiver-local unwrapped residual phase, in original LOCAL input samples.
// It excludes the immutable affine helper frequency used by the optional
// frequency bank; constant Tx/propagation phase remains an unknown fitted gain.
struct PatternPhaseKnot {std::uint64_t sample=0;double radians=0;};
struct PatternPhaseSegment {
    std::uint64_t first_sample=0;
    double radians=0,radians_per_sample=0;
};
// One optional bank-owned, bounded residual-phase view. Coordinates are
// absolute original input samples. B is the frozen helper CFO, excluding its
// nominal audio frequency. The caller independently qualifies canonical tone
// phase AND punctuation timing; metadata never qualifies this model.
// All updates occur between joined receiver calls. Existing segments are never
// rewritten or reclaimed. A full curve retains its last affine continuation.
class PatternPhaseCurve {
public:
    static constexpr std::size_t segment_capacity=24;
    bool configure(std::uint64_t view_id,std::uint32_t sample_rate,double baseline_residual_hz,
                   std::uint64_t input_guard_samples,std::size_t maximum_bin_samples) noexcept;
    // At most two continuous future segments, committed atomically. The guard
    // includes any FIR lookback; max-bin separation also protects partial bins.
    bool append(std::span<const PatternPhaseSegment>,std::uint64_t evidence_sample,
                std::uint64_t qualified_until) noexcept;
    // Only independent evidence agreeing with the unchanged prediction may
    // refresh this expiry. This operation does not change any observation.
    bool requalify(std::uint64_t evidence_sample,std::uint64_t qualified_until) noexcept;
    void close() noexcept {qualified_until_=0;}
    double value_at(long double absolute_sample) const noexcept;
    double slope_at(long double absolute_sample) const noexcept;
    std::optional<std::uint64_t> next_segment_after(std::uint64_t absolute_sample) const noexcept;
    std::uint64_t first_sample() const noexcept {return size_?segments_[0].first_sample:UINT64_MAX;}
    double baseline() const noexcept {return baseline_;}
    std::uint64_t latest_evidence() const noexcept {return latest_evidence_;}
    std::uint64_t qualified_until() const noexcept {return qualified_until_;}
    std::size_t available_segments() const noexcept {return segment_capacity-size_;}
    std::uint64_t view_id() const noexcept {return view_id_;}
    std::uint32_t sample_rate() const noexcept {return sample_rate_;}
    std::size_t maximum_bin_samples() const noexcept {return maximum_bin_samples_;}
    // Decode-worker input fence, including raw input before phase activation.
    // The max-bin append guard protects an unfinished partial bin as well.
    void mark_observed(std::uint64_t end) const noexcept {observed_until_=std::max(observed_until_,end);}
private:
    std::array<PatternPhaseSegment,segment_capacity> segments_{};
    std::size_t size_=0;
    std::uint64_t view_id_=0,latest_evidence_=0,qualified_until_=0,input_guard_=0;
    mutable std::uint64_t observed_until_=0;
    double baseline_=0;
    std::uint32_t sample_rate_=0,maximum_bin_samples_=0;
};
static_assert(sizeof(PatternPhaseCurve)<=640);
// Joined, native-only storage bound before the first acquisition decision.
// This is a decoder/configured-bit floor, NOT an acquisition worker, cache or
// throughput grant. Query after optional trim when the smallest bound is wanted.
// Argument bytes use this receiver's constructor/setter convention; actual
// bytes also include any outer wrapper object owned outside that argument.
struct PatternColdWorkspaceFloor {
    std::size_t workspace_argument_bytes=0,actual_workspace_bytes=0,configured_bit_limit=0;
};
// Pristine native observation storage through the first original scoring fence.
// This certificate may fund observe_acquisition ONLY; it is not a setter grant
// or permission to score. Configured/effective bit ceilings and the original
// execution argument remain unchanged. Before any scoring the caller must fund
// that full ordinary execution argument, or discard this optional cold owner.
struct PatternColdObservationStorage {
    std::size_t actual_workspace_bytes=0,execution_workspace_argument_bytes=0;
    std::size_t configured_bit_limit=0,effective_bit_limit=0;
};
// Conservative mandatory storage through a finite retained-input replay.
// This grants no source interpretation or physical completion authority.
struct HelperReplayBudget {
    std::size_t maximum_stream_bits=0,workspace_bytes=0;
    // Nonzero only for the native, default-shaped ordinary reset recipe.
    // This is a CONSTRUCTOR ARGUMENT, not an actual-storage measurement.
    // Keep it immutable: constructing directly with workspace_bytes could
    // choose a larger resident template layout than the original receiver.
    std::size_t cold_constructor_bytes=0,reset_overlap_bytes=0,steady_workspace_bytes=0;
};
// Original native acquisition scheduling, in receiver-local input samples.
// Observation is not an acquisition decision or physical absence. A caller
// must finish ready work before further observation unless fresh dispatch is
// paused; an already-scored pending receipt must always resolve first.
// A caller-owned source-support lease controls NEW helper-conditioned work.
// Pausing leaves pending scored receipts, accepted/provisional tracks and their
// continuous phase/input coordinates intact. A native ring can eventually lose
// an unscored cold aperture; retired then forbids replay on this owner, without
// asserting that its original acquisition coverage was scored.
// A joined-call selector for a continuously measured helper reference. The
// original RF/LF catalogue and input/phase basis never move. The affine clock
// coefficient describes that catalogue (H-D for the realtime corrected path).
// Each original LF retains every RF Voronoi cell intersecting this interval;
// equal-distance boundary cells are both kept. No new RF coverage is created.
// An independently qualified interval in the ORIGINAL DATA/source clock's
// ppm coordinate. The caller includes every unresolved transmit-rate factor;
// a physical audio-only estimate cannot silently stand in for this interval.
struct PatternHelperClockInterval {
    double lower_ppm=0,upper_ppm=0;
    bool valid()const noexcept {
        return std::isfinite(lower_ppm) && std::isfinite(upper_ppm) && lower_ppm<=upper_ppm;
    }
    bool operator==(const PatternHelperClockInterval&)const=default;
};
struct PatternHelperFrequencyWindow {
    double center_hz=0,half_width_hz=0,clock_coefficient_hz=0,clock_cell_half_width_ppm=0;
    std::optional<PatternHelperClockInterval> clock_interval{};
    bool valid() const noexcept {
        return std::isfinite(center_hz) && std::isfinite(half_width_hz) && half_width_hz>=0 &&
            std::isfinite(clock_coefficient_hz) && std::isfinite(clock_cell_half_width_ppm) && clock_cell_half_width_ppm>=0 &&
            (!clock_interval || clock_interval->valid());
    }
    bool retains(std::span<const PatternFrequencyRateHypothesis> pairs,std::size_t index,
                 double frequency_origin_hz=0) const noexcept {
        if(index>=pairs.size() || !valid())return false;
        const auto& pair=pairs[index];
        auto clock_lower=std::nextafter(static_cast<long double>(pair.clock_error_ppm)-clock_cell_half_width_ppm,-INFINITY);
        auto clock_upper=std::nextafter(static_cast<long double>(pair.clock_error_ppm)+clock_cell_half_width_ppm,INFINITY);
        if(clock_interval) {
            clock_lower=std::max(clock_lower,static_cast<long double>(clock_interval->lower_ppm));
            clock_upper=std::min(clock_upper,static_cast<long double>(clock_interval->upper_ppm));
            if(clock_lower>clock_upper)return false;
        }
        long double lower=-INFINITY,upper=INFINITY;
        for(const auto& other:pairs)if(other.clock_error_ppm==pair.clock_error_ppm) {
            const auto mid=(static_cast<long double>(other.frequency_offset_hz)+pair.frequency_offset_hz)/2;
            if(other.frequency_offset_hz<pair.frequency_offset_hz)lower=std::max(lower,mid);
            if(other.frequency_offset_hz>pair.frequency_offset_hz)upper=std::min(upper,mid);
        }
        const auto a=static_cast<long double>(center_hz)-frequency_origin_hz-
            static_cast<long double>(clock_coefficient_hz)*clock_lower*1e-6L;
        const auto b=static_cast<long double>(center_hz)-frequency_origin_hz-
            static_cast<long double>(clock_coefficient_hz)*clock_upper*1e-6L;
        const auto pad=64*std::numeric_limits<long double>::epsilon()*std::max({1.L,std::abs(a),std::abs(b),
            std::abs(static_cast<long double>(center_hz)),std::abs(static_cast<long double>(frequency_origin_hz)),
            std::abs(static_cast<long double>(clock_coefficient_hz)*clock_lower*1e-6L),
            std::abs(static_cast<long double>(clock_coefficient_hz)*clock_upper*1e-6L),static_cast<long double>(half_width_hz)});
        return std::max(a,b)+half_width_hz+pad>=lower && std::min(a,b)-half_width_hz-pad<=upper;
    }
};
struct PatternHelperFrequencyWindowState {
    bool supported=false,active=false,clock_qualified=false;
    std::optional<PatternHelperClockInterval> clock_interval{};
    std::size_t original_pairs=0,selected_pairs=0;
    double unresolved_clock_cell_half_width_ppm=0;
    // Actual native continuation fits and omitted numerical fits. Compact
    // execution exposes its existing projected/accumulated/skipped samples.
    std::uint64_t continuation_fits=0,skipped_continuation_fits=0;
};
namespace detail {
// Optional helper-only geometry. Build RF cells once; changed measurements
// update one byte per ORIGINAL pair. Numeric hot paths use constant-time flags.
class PatternHelperFrequencySelector {
public:
    struct Cell {double lower=0,upper=0,clock_ppm=0;bool selected=true;};
    PatternHelperFrequencyWindow window;
    double domain_lower_ppm=0,domain_upper_ppm=0;
    std::vector<Cell> cells;
    std::size_t selected=0;
    std::uint64_t continuation_fits=0,skipped_continuation_fits=0;
    static std::size_t maximum_bytes(std::size_t n) noexcept {return sizeof(PatternHelperFrequencySelector)+n*sizeof(Cell);}
    std::size_t working_bytes()const noexcept {return sizeof(*this)+cells.capacity()*sizeof(Cell);}
    bool initialize(const PatternSearch& search,const PatternHelperFrequencyWindow& requested) {
        if(search.hypotheses.empty() || search.hypotheses.size()>8194 || !requested.valid() ||
           !search.admitted_clock_half_width_ppm || !std::isfinite(*search.admitted_clock_half_width_ppm) ||
           *search.admitted_clock_half_width_ppm<0)return false;
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || FLT_EVAL_METHOD!=0
        return false;
#else
        if(!std::numeric_limits<double>::is_iec559 || std::fegetround()!=FE_TONEAREST)return false;
#endif
        if(search.qualified_clock_interval && requested.clock_cell_half_width_ppm<search.qualified_clock_interval->cell_half_width_ppm)return false;
        // Verify the complete original requested clock domain using closed
        // source cells. This selector never substitutes a smaller LF domain.
        const auto lower=search.qualified_clock_interval?search.qualified_clock_interval->lower_ppm:-*search.admitted_clock_half_width_ppm;
        const auto upper=search.qualified_clock_interval?search.qualified_clock_interval->upper_ppm:*search.admitted_clock_half_width_ppm;
        if(!std::isfinite(lower) || !std::isfinite(upper) || lower>upper)return false;
        domain_lower_ppm=lower;domain_upper_ppm=upper;
        if(!accepts(requested))return false;
        const auto sum=[](double a,double b) {
            const auto value=a+b,bv=value-a,error=(a-(value-bv))+(b-bv);
            return std::array<double,2>{error<0?std::nextafter(value,-INFINITY):value,
                error>0?std::nextafter(value,INFINITY):value};
        };
        auto covered=lower;
        for(std::size_t pass=0;pass<=search.hypotheses.size();++pass) {
            auto next=covered;bool bridged=false;
            for(const auto& pair:search.hypotheses) {
                if(!std::isfinite(pair.frequency_offset_hz) || !std::isfinite(pair.clock_error_ppm))return false;
                const auto lo=sum(pair.clock_error_ppm,-requested.clock_cell_half_width_ppm)[1];
                const auto hi=sum(pair.clock_error_ppm,requested.clock_cell_half_width_ppm)[0];
                if(lo<=covered && hi>=covered){next=std::max(next,hi);bridged=true;}
            }
            if(!bridged)return false;
            if(next>=upper)break;
            if(next==covered || pass==search.hypotheses.size())return false;
            covered=next;
        }
        cells.resize(search.hypotheses.size());
        for(std::size_t i=0;i<cells.size();++i) {
            const auto& pair=search.hypotheses[i];auto& cell=cells[i];cell.clock_ppm=pair.clock_error_ppm;
            long double lo=-INFINITY,hi=INFINITY;
            for(const auto& other:search.hypotheses)if(other.clock_error_ppm==pair.clock_error_ppm) {
                const auto mid=(static_cast<long double>(other.frequency_offset_hz)+pair.frequency_offset_hz)/2;
                if(other.frequency_offset_hz<pair.frequency_offset_hz)lo=std::max(lo,mid);
                if(other.frequency_offset_hz>pair.frequency_offset_hz)hi=std::min(hi,mid);
            }
            cell.lower=static_cast<double>(lo);cell.upper=static_cast<double>(hi);
            if(static_cast<long double>(cell.lower)>lo)cell.lower=std::nextafter(cell.lower,-INFINITY);
            if(static_cast<long double>(cell.upper)<hi)cell.upper=std::nextafter(cell.upper,INFINITY);
        }
        update(requested);return true;
    }
    bool keep(std::size_t i,double origin=0)const noexcept {
        if(i>=cells.size())return false;
        return origin==0?cells[i].selected:intersects(cells[i],origin);
    }
    bool accepts(const PatternHelperFrequencyWindow& requested)const noexcept {
        return requested.valid() && (!requested.clock_interval ||
            (requested.clock_interval->upper_ppm>=domain_lower_ppm && requested.clock_interval->lower_ppm<=domain_upper_ppm));
    }
    bool update(const PatternHelperFrequencyWindow& requested)noexcept {
        if(!accepts(requested))return false;
        if(window.center_hz==requested.center_hz && window.half_width_hz==requested.half_width_hz &&
           window.clock_coefficient_hz==requested.clock_coefficient_hz &&
           window.clock_cell_half_width_ppm==requested.clock_cell_half_width_ppm &&
           window.clock_interval==requested.clock_interval && selected)return false;
        window=requested;selected=0;bool widens=false;
        for(auto& cell:cells) {const bool keep=intersects(cell,0);widens|=keep&&!cell.selected;cell.selected=keep;selected+=keep;}
        return widens;
    }
private:
    bool intersects(const Cell& cell,double origin)const noexcept {
        if(window.clock_interval) {
            // Intersect WHOLE original LF coverage cells with the measured
            // closed interval; retain both cells at an exact shared boundary.
            const auto lo=std::max({std::nextafter(static_cast<long double>(cell.clock_ppm)-window.clock_cell_half_width_ppm,-INFINITY),
                static_cast<long double>(domain_lower_ppm),static_cast<long double>(window.clock_interval->lower_ppm)});
            const auto hi=std::min({std::nextafter(static_cast<long double>(cell.clock_ppm)+window.clock_cell_half_width_ppm,INFINITY),
                static_cast<long double>(domain_upper_ppm),static_cast<long double>(window.clock_interval->upper_ppm)});
            if(lo>hi)return false;
            const auto a=static_cast<long double>(window.center_hz)-origin-
                static_cast<long double>(window.clock_coefficient_hz)*lo*1e-6L;
            const auto b=static_cast<long double>(window.center_hz)-origin-
                static_cast<long double>(window.clock_coefficient_hz)*hi*1e-6L;
            const auto pad=64*std::numeric_limits<long double>::epsilon()*
                std::max({1.L,std::abs(a),std::abs(b),std::abs(static_cast<long double>(window.center_hz)),
                    std::abs(static_cast<long double>(origin)),
                    std::abs(static_cast<long double>(window.clock_coefficient_hz)*lo*1e-6L),
                    std::abs(static_cast<long double>(window.clock_coefficient_hz)*hi*1e-6L),static_cast<long double>(window.half_width_hz)});
            return std::max(a,b)+window.half_width_hz+pad>=cell.lower &&
                std::min(a,b)-window.half_width_hz-pad<=cell.upper;
        }
        // The frequency-only default retains its preceding full-LF arithmetic.
        const auto center=static_cast<long double>(window.center_hz)-origin-
            static_cast<long double>(window.clock_coefficient_hz)*cell.clock_ppm*1e-6L;
        const auto radius=static_cast<long double>(window.half_width_hz)+
            std::abs(static_cast<long double>(window.clock_coefficient_hz))*window.clock_cell_half_width_ppm*1e-6L;
        const auto pad=64*std::numeric_limits<long double>::epsilon()*std::max({1.L,std::abs(center),radius});
        return center+radius+pad>=cell.lower && center-radius-pad<=cell.upper;
    }
};
}
struct PatternAcquisitionDispatchState {
    bool supported=false,permitted=true,retired=false;
};
struct PatternAcquisitionState {
    bool supported=false,ready=false,prior_active=false,prior_pending=false,prior_accepted=false;
    std::uint64_t observed_samples=0,processing_frontier=0,audit_start_sample=0;
    std::size_t input_room_samples=0,original_pairs=0,selected_pairs=0,bin_samples=0;
    double frequency_origin_hz=0;
    std::optional<double> admitted_clock_half_width_ppm;
    std::optional<double> admitted_clock_cell_half_width_ppm;
    std::optional<PatternClockInterval> qualified_clock_interval;
};
// Cold-acquisition descriptor upper bound, not CPU time. Exact original
// phase/index groups precede optional timing/overlap skips and score reuse.
// A numerical retry can perform more transforms inside one FFT pair job.
struct PatternAcquisitionWorkBound {
    std::uint64_t original_pair_descriptors=0,maximum_fft_pair_jobs=0,original_phase_groups=0;
    std::size_t initial_stream_symbols=0,transform_length=0;
    bool ready=false;
};
// Full desired cold-acquisition support in receiver-local input samples.
// The two-bin refinement cover is not clipped to consumed/captured PCM. The
// caller must expand through its actual FIR/resampler mapping and decline if
// any desired sample is unavailable. consumed_end_sample is exclusive.
// Pure physical RF cell of the native coherent-pattern score operator.
// Generated oscillator lattice spacing can be smaller and a UTC-steering
// catalogue can contain several original grids. Neither changes this radius.
// This query certifies no backend, capture, presence or future scored support;
// its caller must independently prove native execution eligibility.
double native_pattern_frequency_cell_radius(const Config&);
struct PatternAcquisitionClockDomain {
    // Immutable original catalogue and CURRENT legacy all-LF mask. The latter
    // is a prerequisite, never silently intersected after certificate creation.
    std::span<const PatternFrequencyRateHypothesis> original_pairs;
    std::span<const std::uint8_t> legacy_frequency_mask;
    std::uint64_t legacy_mask_id=0,input_sample_origin=0,input_phase_view_id=0,symbol_samples=0;
    std::uint32_t sample_rate=0;
    double frequency_origin_hz=0,frequency_cell_radius_hz=0;
    double data_clock_half_width_ppm=0,clock_cell_half_width_ppm=0;
};
struct PatternAcquisitionScoredSupport {
    std::uint64_t first_sample=0,last_sample=0,consumed_end_sample=0;
    PatternAcquisitionClockDomain domain;
};
struct PatternAcquisitionClockInterval {double lower_ppm=0,upper_ppm=0;};
// A trusted internal proof-producing builder issues this immutable, borrowed
// certificate for ONE exact ordinary batch. It contains a complete original
// RF x DATA-LF cell union, with every unresolved UTC transmit-steering factor.
// Core validates its catalogue/domain binding, support, subset and DATA-clock
// coverage. The builder, not core, proves the physical helper mapping.
// The builder AND receiver must outlive the synchronous joined call. Rebuilding
// the builder invalidates earlier views. No pointer is retained in Impl/Track.
class PatternAcquisitionClockPrior {
public:
    const PatternAcquisitionScoredSupport& scored_support()const noexcept{return support_;}
    std::span<const PatternAcquisitionClockInterval> intervals()const noexcept{return intervals_;}
    std::span<const std::uint8_t> pair_mask()const noexcept{return mask_;}
    std::uint64_t captured_input_end_sample()const noexcept{return input_end_;}
    std::uint64_t reference_evidence_sample()const noexcept{return evidence_;}
    std::uint64_t reference_captured_end_sample()const noexcept{return reference_end_;}
private:
    friend class datapump::helper::JointClockCellCertificate;
    PatternAcquisitionClockPrior(PatternAcquisitionScoredSupport support,
            std::span<const PatternAcquisitionClockInterval> intervals,std::span<const std::uint8_t> mask,
            std::uint64_t input_end,std::uint64_t evidence,std::uint64_t reference_end)noexcept:
        support_(support),intervals_(intervals),mask_(mask),input_end_(input_end),
        evidence_(evidence),reference_end_(reference_end){}
    PatternAcquisitionScoredSupport support_;
    std::span<const PatternAcquisitionClockInterval> intervals_;
    std::span<const std::uint8_t> mask_;
    std::uint64_t input_end_=0,evidence_=0,reference_end_=0;
};
// All current native PCM backing, in ORIGINAL receiver input coordinates.
// It conservatively encloses every same-call warm timing/phase/LF fit, including
// newly admitted owners. The core never clips this to a reference's support.
struct PatternContinuationScoredSupport {
    PatternAcquisitionScoredSupport input;
    std::uint64_t ring_bins=0,ready_established_tracks=0;
};
class PatternContinuationFrequencyPrior {
public:
    const PatternContinuationScoredSupport& support()const noexcept{return support_;}
    std::span<const std::uint8_t> pair_mask()const noexcept{return mask_;}
    std::uint64_t input_captured_end_sample()const noexcept{return input_end_;}
    std::uint64_t raw_last_sample()const noexcept{return raw_last_;}
    std::uint64_t reference_observed_end_sample()const noexcept{return reference_end_;}
private:
    friend class datapump::helper::ContinuationFrequencyCertificate;
    PatternContinuationFrequencyPrior(PatternContinuationScoredSupport support,std::span<const std::uint8_t> mask,
        std::uint64_t input_end,std::uint64_t raw_last,std::uint64_t reference_end)noexcept:
        support_(support),mask_(mask),input_end_(input_end),raw_last_(raw_last),reference_end_(reference_end){}
    PatternContinuationScoredSupport support_;
    std::span<const std::uint8_t> mask_;
    std::uint64_t input_end_=0,raw_last_=0,reference_end_=0;
};
enum class PatternContinuationFrequencyStatus {
    not_requested,active,unsupported_geometry,invalid_reference,domain_changed,invalid_pair_subset
};
struct PatternContinuationFrequencyResult {
    PatternContinuationFrequencyStatus status=PatternContinuationFrequencyStatus::not_requested;
    // Numeric fits and logical trials are DIFFERENT units. No skipped fit is
    // confidence/absence evidence. Anchor/timing fits always execute as usual.
    std::uint64_t windows=0,narrowed_windows=0,anchor_timing_fits=0;
    std::uint64_t original_competing_fits=0,executed_competing_fits=0,skipped_competing_fits=0;
    std::uint64_t first_failure_broad_retries=0,periodic_broad_windows=0,logical_trials=0;
};
enum class PatternAcquisitionClockStatus {
    not_requested,active,unsupported_geometry,invalid_reference,unsupported_clock_domain,
    uncovered_scored_support,uncovered_clock_interval,domain_changed,invalid_pair_subset,no_clock_reduction
};
struct PatternAcquisitionClockResult {
    PatternAcquisitionClockStatus status=PatternAcquisitionClockStatus::not_requested;
    bool completed=false,narrowed=false;
    std::size_t original_clock_rates=0,selected_clock_rates=0,original_pairs=0,selected_pairs=0;
    std::uint64_t executed_template_jobs=0;
};
// Compact assistance controls the existing streaming correlator, independently
// of the native FFT split-input seam. Conditional cold RF lanes are advanced
// without observations; fallback starts at the current frontier, never replay.
// Periodic physical punctuation, independently qualified without decoding
// metadata. All aliases survive. Source phase belongs to the local candidate
// epoch; observed_sample is a receiver-local INPUT coordinate, not a Tx date.
struct PatternPeriodicTimingPrior {
    long double observed_sample=0,source_phase_samples=0,period_samples=0,half_width_samples=0;
};
enum class PatternAcquisitionTimingStatus {
    not_requested,active,unsupported_geometry,invalid_reference,unsupported_clock_domain,
    bounded_scoring_declined,numeric_selection_declined,no_start_reduction
};
struct PatternAcquisitionTimingResult {
    PatternAcquisitionTimingStatus status=PatternAcquisitionTimingStatus::not_requested;
    bool completed=false,narrowed=false;
    // Original group/start cells, before multiplying by retained RF/LF pairs.
    // qualified cells are diagnostic only when bounded execution declines.
    std::uint64_t original_group_start_cells=0,qualified_group_start_cells=0;
    std::uint64_t direct_jobs=0,partitioned_jobs=0,partitioned_transform_points=0,skipped_template_jobs=0;
};
// Borrowed synchronous arguments only; no selector/model pointer enters a
// receiver, pending batch or Track. Existing overloads keep null defaults.
struct PatternAcquisitionAssistance {
    const PatternAcquisitionClockPrior* clock=nullptr;
    PatternAcquisitionClockResult* clock_result=nullptr;
    const PatternPeriodicTimingPrior* timing=nullptr;
    std::uint64_t timing_evidence_sample=0;
    PatternAcquisitionTimingResult* timing_result=nullptr;
    const PatternContinuationFrequencyPrior* continuation=nullptr;
    PatternContinuationFrequencyResult* continuation_result=nullptr;
};
struct PatternCompactAcquisitionState {
    bool supported=false,prior_active=false,audit_active=false,audit_complete=false;
    bool timing_prior_active=false,timing_narrowing_active=false,workspace_yielded=false;
    std::uint64_t observed_samples=0,audit_start_sample=0;
    std::size_t original_pairs=0,selected_pairs=0,active_pairs=0,protected_pairs=0;
    std::size_t original_arrival_lanes=0,active_arrival_lanes=0,projected_carriers=0;
    std::size_t audit_scored_lanes=0,workspace_bytes=0,timing_selected_arrival_lanes=0;
    std::uint64_t projected_carrier_samples=0,accumulated_lane_samples=0;
    std::uint64_t skipped_carrier_samples=0,skipped_lane_samples=0;
    std::uint64_t broad_audit_projected_carrier_samples=0,broad_audit_accumulated_lane_samples=0;
    std::uint64_t broad_audit_full_symbols=0,skipped_logical_trials=0;
    std::uint64_t phase_projected_carrier_samples=0,phase_carrier_moment_updates=0,phase_view_id=0;
    std::optional<double> admitted_clock_half_width_ppm,admitted_clock_cell_half_width_ppm;
    std::optional<PatternClockInterval> qualified_clock_interval;
};
class PatternReceiver {
public:
    PatternReceiver(Config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                    PatternSearch search = {});
    ~PatternReceiver();
    PatternReceiver(PatternReceiver&&) noexcept;
    PatternReceiver& operator=(PatternReceiver&&) noexcept;
    void push(std::span<const float>, std::stop_token = {});
    // Streaming compact control is separate from the native FFT seam below.
    PatternCompactAcquisitionState compact_acquisition_state() const noexcept;
    std::span<const PatternFrequencyRateHypothesis> compact_acquisition_hypotheses() const noexcept;
    // Only origin zero is supported. The original-pair mask must retain every
    // unresolved clock rate; all arrival/private phase alternatives survive.
    // Optional control storage yields before payload/detector allocations.
    // Reclaim permanently declines optional assistance for this owner, so a
    // later reinstall cannot starve its required broad fallback/audit window.
    bool set_compact_acquisition_prior(double frequency_origin_hz,std::span<const std::uint8_t> mask);
    bool clear_compact_acquisition_prior() noexcept;
    // The original bank stays broad until EVERY original arrival lane scores
    // a fresh complete symbol. Reenabled crossing partial symbols are ineligible;
    // the first broad decision can therefore require almost two symbol times.
    bool start_compact_acquisition_audit() noexcept;
    // Raw/unaccepted acquisition sufficient statistics may be cold. Any hard
    // bit/provisional chain, timing/emission dependency or output forbids this.
    bool cold_acquisition_only() const noexcept;
    PatternAcquisitionDispatchState helper_acquisition_dispatch() const noexcept;
    bool set_helper_acquisition_dispatch(bool permitted) noexcept;
    // Update only between joined calls, from the current qualified reference.
    // The first set builds bounded helper-only geometry paid from this loan;
    // subsequent measurement updates allocate nothing. No basis translation,
    // clock pruning or automatic broad audit. Original LF-cell uncertainty
    // is added by the engine, never the caller.
    // Established anchors/dependencies and ordinary physical END remain active.
    bool set_helper_frequency_window(double center_hz,double half_width_hz,double clock_coefficient_hz=0) noexcept;
    // Independently measured ORIGINAL DATA LF interval. Unknown/stale clock
    // uses the three-argument setter to restore all original LF immediately.
    bool set_helper_frequency_window(double center_hz,double half_width_hz,double clock_coefficient_hz,
                                    std::optional<PatternHelperClockInterval> clock_interval) noexcept;
    void clear_helper_frequency_window() noexcept;
    PatternHelperFrequencyWindowState helper_frequency_window() const noexcept;

    bool set_compact_timing_prior(const PatternPeriodicTimingPrior&) noexcept;
    bool clear_compact_timing_prior() noexcept;
    // Attach once before the first observed sample. The borrowed curve must
    // outlive this receiver; guarded future segments preserve history and an
    // expired/closed reference keeps its last affine phase convention.
    bool compact_phase_assistance_supported() const noexcept;
    bool set_compact_phase_view(const PatternPhaseCurve*) noexcept;
    bool compact_phase_view_active() const noexcept;
    // Opt-in split input/ordinary-search seam. Unsupported engines consume
    // zero. Input stops at the exact original native scheduling fence; no
    // hypothesis, trial, candidate, bit or END rule is omitted or changed.
    PatternAcquisitionState acquisition_state() const noexcept;
    std::optional<PatternAcquisitionWorkBound> acquisition_work_bound() const noexcept;
    std::optional<PatternAcquisitionScoredSupport> acquisition_scored_support() const noexcept;
    std::optional<PatternContinuationScoredSupport> continuation_scored_support(std::size_t incoming_samples=0) const noexcept;
    std::span<const PatternFrequencyRateHypothesis> acquisition_hypotheses() const noexcept;
    // A conditional common-link prior, never a replacement source decoder.
    // The mask names the immutable original pairs and must retain every clock
    // rate. Only native continuous execution is supported. Shifted references
    // spend additional lifetime trials; each track freezes its own origin.
    // Selecting every pair is allowed only for a nonzero RF translation;
    // this aligns a small original catalogue without promising less work.
    // Refresh is atomic; only compatible, unconsumed ordinary RF/clock
    // evidence survives. Clear/loss consumes that evidence.
    bool set_acquisition_prior(double frequency_origin_hz,std::span<const std::uint8_t> mask);
    bool clear_acquisition_prior();
    // A fresh causal helper reference may protect current/future native tracks
    // from abandonment by an independent start during a pending gap. Loss
    // disarms future births, never accepted owners' ordinary absence scoring.
    // Allocation-free; unsupported compact engines remain untouched.
    bool set_helper_gap_protection(bool qualified) noexcept;
    std::size_t observe_acquisition(std::span<const float>,std::stop_token = {});
    std::size_t observe_acquisition(std::span<const float>,const PatternContinuationFrequencyPrior&,
        PatternContinuationFrequencyResult&,std::stop_token = {});
    // One conditional pass holds its original input/cursor until resolution.
    // True means an ordinary Robust bit was accepted, not helper metadata.
    bool process_prior_acquisition(std::stop_token = {});
    bool process_assisted_acquisition(const PatternAcquisitionAssistance&,std::stop_token = {});
    // Independently qualified periodic punctuation is borrowed for this joined
    // acquisition batch only. observed_sample is the row-local physical grid
    // anchor; evidence_sample separately dates the actual raw publication.
    // Unsupported/crossover cases retain the ordinary selection and frequency
    // prior. No timing pointer, epoch choice or changed basis reaches a track.
    bool process_prior_acquisition(const PatternPeriodicTimingPrior&,std::uint64_t evidence_sample,
                                   PatternAcquisitionTimingResult&,std::stop_token = {});
    // Native deferred-FHSS cold acquisition only. The legacy all-LF prior
    // setter remains unchanged; established tracking and END never use this
    // selector. Unsupported certificates keep the frequency/timing path.
    bool process_prior_acquisition(const PatternAcquisitionClockPrior&,PatternAcquisitionClockResult&,
                                   std::stop_token = {});
    bool process_prior_acquisition(const PatternAcquisitionClockPrior&,PatternAcquisitionClockResult&,
                                   const PatternPeriodicTimingPrior&,std::uint64_t timing_evidence_sample,
                                   PatternAcquisitionTimingResult&,std::stop_token = {});
    // A cohort may retire the original batch under the explicitly configured
    // common-link assumption. False instead audits that same batch at origin0.
    void resolve_acquisition(bool conditional_link,std::stop_token = {});
    void resolve_acquisition(bool conditional_link,const PatternContinuationFrequencyPrior&,
        PatternContinuationFrequencyResult&,std::stop_token = {});
    void process_acquisition(std::stop_token = {});
    // Borrowed bank-owned curve: it must outlive this receiver and all its
    // tracks. No per-row allocation or permanent phase history is added.
    bool set_acquisition_phase_view(const PatternPhaseCurve*) noexcept;
    bool acquisition_phase_view_active() const noexcept;
    // Optional separate assisted receiver only. Original real PCM is first
    // projected exactly as usual; each complete complex bin is then rotated
    // at its center. Unit rotations preserve its noise covariance eigenvalues
    // and the existing conservative scalar condition bound for predetermined
    // rotations. Helper-derived/adaptive rotations need empirical calibration;
    // this is not a certified false-alarm probability. Model observations must
    // predate the FIRST sample of every affected bin, including partial bins.
    // Sample/Gram,
    // differential, singular and correlator paths are unsupported.
    bool phase_assistance_supported() const noexcept;
    std::size_t phase_assistance_bin_samples() const noexcept;
    // Knots are immutable during this call and not retained. Every completed
    // bin center (including a partial bin from a previous push) must be covered;
    // no interpolation gap may exceed maximum_gap_samples. Residual phases
    // must be finite with magnitude <=1e12 radians. False leaves all
    // state untouched; caller must feed the original samples with push().
    bool push_phase_assisted(std::span<const float>,std::span<const PatternPhaseKnot>,
                             std::uint64_t maximum_gap_samples,std::stop_token = {});
    // Optional FFT helper lane only: stop first-start acquisition beyond the
    // already processed start frontier, while admitted tracks continue scoring
    // every observed symbol/absence. False leaves unsupported state untouched.
    bool suspend_helper_acquisition();
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
    std::optional<PatternSourceIdentity> source_identity(const PatternBurst&)const;
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
    // Conservative physical input span for the first ordinary native FFT
    // batch (zero for a non-FFT backend). No samples or work are inserted.
    std::uint64_t initial_fft_input_samples() const noexcept;
    // PRIVATE finite-reference export. Pure metadata snapshot of a pristine
    // ordinary native owner; no samples, trials, tracks or policy are changed.
    // Output allocation and complete physical observation support are capped.
    bool export_conditioned_reference(detail::ConditionedReferenceDescriptor&,
        std::size_t maximum_jobs,std::size_t maximum_source_samples) const;
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
    // Pure/allocation-free. Declines after any acquisition decision, provisional
    // or published owner, held acquisition/view, or unsupported backend. It neither
    // changes the configured quota nor promises execution workers. A caller must
    // fund a genuine ordinary execution loan before scoring a ready batch, and
    // promote a new dependency before any further input under a cold policy.
    std::optional<PatternColdWorkspaceFloor> cold_workspace_floor() const noexcept;
    // Same pristine lifetime gate; no allocations, state or quota changes.
    // Only the split observe_acquisition path is covered, up to its ready fence.
    std::optional<PatternColdObservationStorage> cold_observation_storage() const noexcept;
    // No state changes. Unsupported calendars/backends return nullopt.
    // Includes all already observed input, future PCM and current capacities.
    std::optional<HelperReplayBudget> helper_replay_budget(std::size_t future_samples,
        const PatternSearch* reset_search=nullptr) const;
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
    friend class detail::NativePeakLease;
    friend class detail::NativePointLease;
    friend struct detail::NativeDiagnosticHistoryAccess;
    bool process_prior_acquisition_bounded(const PatternPeriodicTimingPrior*,std::uint64_t,
        PatternAcquisitionTimingResult*,const PatternAcquisitionClockPrior*,PatternAcquisitionClockResult*,std::stop_token,
        const PatternContinuationFrequencyPrior* = nullptr,PatternContinuationFrequencyResult* = nullptr);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
