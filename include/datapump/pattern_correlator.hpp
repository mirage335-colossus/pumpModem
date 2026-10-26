#pragma once
#include <optional>
#include <limits>
#include "datapump/pattern_receiver.hpp"
#include <string>

namespace datapump::modem {
namespace detail {class CorrelationProjectionCache;}
// Numerical diagnostics for paired validation. These are deliberately separate
// from saved modem/search settings; ordinary callers select the automatic path.
struct PatternCorrelatorOptions {
    bool raw_reference=false;
    bool measure_work=false;
    // Preserve the preceding affine-span arithmetic, independently of the
    // original raw-sample reference. Neither diagnostic is a saved setting.
    bool affine_coefficient_reference=false;
    // Fine timers perturb short spans and require worker_threads=1. Use
    // uninstrumented paired runs to measure an execution speedup.
    bool measure_affine_work=false;
    // Receiver-only experimental envelope detector. The production waveform
    // and stream addressing are unchanged. Unsupported geometry is rejected.
    // Appended to preserve diagnostic aggregate-initializer meaning.
    bool amplitude_only=false;
    // Exact mapped-template cache opt-out for complete-execution comparisons.
    // Never changes waveform, hypotheses, arithmetic or statistical guards.
    bool scheduled_template_cache_reference=false;
    // Unsafe raw-scoring diagnostic: conditional sensitivity comparisons only,
    // never selected by production entry points or persisted settings.
    bool scheduled_raw_score_reference=false;
    // Bounded pulse-span prototype. Complete Live crossover evidence did not
    // justify automatic selection at the tested small table spans. Diagnostic
    // callers may evaluate it; production Default retains direct execution.
    bool scheduled_affine_experiment=false;
    // Original direct mapped contraction for paired execution/statistic checks.
    // FP64 force also bypasses finite-table interval reuse automatically.
    bool scheduled_moment_reference=false;
    // Native FP32 curve storage/interpolation diagnostic. Complete paired
    // execution did not demonstrate a speed advantage on the measured host.
    bool scheduled_float_curve_experiment=false;
    // Exact-family fit ordering diagnostic; completion/trials/publication keep
    // original logical order. No waveform, arithmetic or coverage change.
    bool scheduled_family_experiment=true;
    // Original fine-bank arithmetic; share exact source geometry/private
    // construction across proven source-equivalent siblings. Each unsupported
    // piece retains original contraction; explicit false is the paired reference.
    bool scheduled_common_shape_experiment=true;
    // Exact same-bank affine pair reuse within one proven common piece.
    // False is a paired execution reference; no waveform/arithmetic/guard change.
    bool scheduled_common_affine_reuse=true;
    // Separate native FP32 signal moment/product diagnostic. Original FP64 real
    // Gram/energy and all guard logic remain; local bounds do not certify final
    // decisions. Never selected by production entry points or persisted settings.
    bool scheduled_float_moment_experiment=false;
    // Internal diagnostic:0/1 keep phase-specific families;2..16 bound distinct
    // phase roots per job. Frequency siblings still share up to16 total lanes.
    std::size_t scheduled_common_phase_limit=8;
    // Exact FP64 one-sample candidate-axis SIMD diagnostic. Requires the common
    // source engine, at least four distinct unfitted frequencies, available ISA
    // and optional per-worker scratch. Default-off until complete cost qualifies.
    bool scheduled_common_simd_experiment=false;
    // Optional exact per-job target-index proof; unavailable workspace disables
    // reuse only, preserving every original detector and hypothesis.
    bool scheduled_common_phase_cache=true;
    // Separate local-matrix-certified affine FP32 Gram/products diagnostic.
    // Original FP64 fallback and full guard logic remain; not an exact final
    // decision certificate and never selected by production entry points.
    bool scheduled_float_gram_experiment=false;
    // Exact original-order temporal raw-output batching; paired prototype.
    // Default-off until complete production cost and preservation qualify it.
    // FP64 force/original-template reference always retain direct construction.
    bool scheduled_temporal_template_experiment=false;
};
enum class PatternCorrelationBackend { raw, pulse, pulse_moments, pulse_segments, amplitude, scheduled_acquisition };
struct PatternAmplitudeGeometry {
    bool supported=false;
    std::string reason;
    std::size_t fft_samples=0,first_bin=0,bin_count=0,clock_hypotheses=0,channel_count=1;
    double admitted_frequency_half_width_hz=0,retained_low_hz=0,retained_high_hz=0;
    double block_seconds=0,clock_half_width_ppm=0,clock_step_ppm=0;
    double center_low_bins=0,center_high_bins=0,spectral_margin_bins=0,bin_width_hz=0;
    // Scheduled receiver-only subdivisions; original crypto addresses and
    // every original half-chip origin remain represented. Packed phases are
    // exact singleton slot starts. Ordinary reception retains its old grid.
    std::uint64_t receiver_phase_step_samples=1,receiver_timing_subdivisions=1;
};
struct PatternAmplitudeChannel {std::size_t first_bin=0,bin_count=0;double center_hz=0;};
PatternAmplitudeChannel pattern_amplitude_channel(const PatternAmplitudeGeometry&,std::size_t index,std::size_t count=0);
// Geometry and coverage only; no reception-probability qualification implied.
// The complete carrier domain is integrated into one orthogonal energy channel.
PatternAmplitudeGeometry pattern_amplitude_geometry(const Config&,const PatternSearch&);
std::uint64_t pattern_amplitude_phase_cell_upper(const Config&,const PatternAmplitudeGeometry&,std::uint64_t phase);
// Bounded metadata-only coverage summary for complex split-symbol reception.
// Required spacings use PHYSICAL aperture; pausing source time never pauses
// carrier phase or sample-clock uncertainty. Explicit diagnostic banks remain
// permitted, but callers must not advertise them as complete coverage unless
// the two grid-qualified flags hold. No probability qualification is implied.
struct PatternScheduledComplexGeometry {
    bool supported=false,frequency_grid_qualified=false,clock_grid_qualified=false;
    std::string reason;
    std::uint64_t physical_aperture_samples=0,receiver_phase_step_samples=1,receiver_timing_subdivisions=1;
    std::size_t clock_hypotheses=0,timing_positions=0,phase_positions=0,workspace_lower_bound_bytes=0;
    // Total lanes including frequency/rate pairs and their arrival/phase cells.
    // full_hypotheses precedes qualified-prior pruning; retained is exact.
    std::size_t retained_hypotheses=0,full_hypotheses=0;
    double frequency_step_hz=0,clock_step_ppm=0,clock_cell_radius_ppm=0;
    // Complete joint-bank coverage; distinct from conservative runtime-cell
    // retention/END uncertainty when exact coarse and refined pairs coexist.
    double clock_coverage_radius_ppm=0;
    double admitted_frequency_half_width_hz=0,admitted_clock_half_width_ppm=0;
};
// Optional constructor allowance bounds transient joint-cell metadata too.
PatternScheduledComplexGeometry pattern_scheduled_complex_geometry(const Config&,const PatternSearch&,
    std::size_t preparation_allowance=std::numeric_limits<std::size_t>::max());
// Experimental bounded-representation policy. Automatic retains dense state
// when its necessary allocation can fit. Memory scopes remain separate:
// original capture is local until an actual shared owner is supplied.
struct PatternScheduledAcquisitionPolicy {
    bool requested=false,supported=false,shared_capture=false;
    std::string reason;
    std::size_t per_bank_required_bytes=0,local_capture_bytes=0,shared_capture_bytes=0,optional_tile_bytes=0;
    std::size_t logical_lanes=0,batch_samples=0;
};
PatternScheduledAcquisitionPolicy pattern_scheduled_acquisition_policy(
    const Config&,const PatternSearch&,const PatternScheduledComplexGeometry&,std::size_t workspace_bytes);

// Capture-allocation-independent requirements; bounded catalogue/code metadata
// construction is included. Not a runtime owner/capture certificate.
// This type does not alter PatternSearch or the actual policy object's layout.
struct PatternScheduledAcquisitionRequirements {
    bool requested=false,geometry_supported=false;
    std::string reason;
    std::size_t required_history_samples=0,per_bank_required_bytes=0;
    std::size_t local_capture_bytes=0,capture_fence_bytes=0,optional_tile_bytes=0;
    std::size_t slot_unit_bytes=0,minimum_slot_admission_bytes=0;
    std::size_t diagnostic_journal_bytes=0,serial_execution_bytes=0;
    std::size_t logical_lanes=0,batch_samples=0;
};
PatternScheduledAcquisitionRequirements pattern_scheduled_acquisition_requirements(
    const Config&,const PatternSearch&,const PatternScheduledComplexGeometry&,std::size_t workspace_bytes);
struct PatternScheduledAcquisitionMemoryProjection {
    bool requested=false,geometry_supported=false,projected_fits=false;
    std::string reason;
    std::size_t required_history_samples=0,per_bank_required_bytes=0,capture_fence_bytes=0;
    std::size_t shared_capture_bytes=0,guaranteed_history_samples=0,optional_tile_bytes=0;
    std::size_t logical_lanes=0,batch_samples=0;
};
// PLAN ONLY. Bytes do not grant a runtime owner, sample availability,
// subscription, revision/generation validity or any reclamation authority.
PatternScheduledAcquisitionMemoryProjection pattern_scheduled_acquisition_memory_projection(
    const Config&,const PatternSearch&,const PatternScheduledComplexGeometry&,std::size_t per_bank_workspace,
    std::size_t cohort_workspace,std::size_t maximum_capture_consumers);

struct PatternCorrelatorWork {
    std::uint64_t implicit_screened_lanes=0,implicit_rejected_lanes=0,implicit_refined_lanes=0;
    std::uint64_t implicit_input_transforms=0,implicit_template_transforms=0,implicit_inverse_transforms=0;
    std::uint64_t implicit_eligible_ranges=0,implicit_fallback_ranges=0,implicit_invalid_certificates=0;
    std::uint64_t implicit_proxy_ranges=0,implicit_fp32_ranges=0,implicit_fp64_ranges=0;
    std::uint64_t implicit_exact_guard_queries=0; // completed original-owner cold guard evaluations
    std::uint64_t implicit_plan_hits=0,implicit_plan_misses=0;
    std::uint64_t implicit_replay_samples=0,implicit_processed_samples=0,implicit_queued_samples=0;
    std::size_t implicit_history_reserved_bytes=0,implicit_resident_slots=0,implicit_peak_prepared_bytes=0;
    PatternCorrelationBackend backend=PatternCorrelationBackend::raw;
    std::size_t hypotheses=0,lattices=0,phase_groups=0,peak_workspace_bytes=0;
    unsigned drift_sections=1;
    std::uint64_t differential_window_samples=0,samples=0,cells=0,segments=0;
    // Kernel preparation is included in frontend time and also shown separately.
    // Opt-in timers affect execution cost; use uninstrumented runs for speedup.
    double frontend_seconds=0,search_seconds=0,kernel_seconds=0;
    double frontend_cpu_seconds=0,search_cpu_seconds=0,kernel_cpu_seconds=0;
    // Validation is outside frontend. Pulse statistics are a frontend subset
    // which includes kernel time; the remainder is downconversion/prefix work.
    double validation_seconds=0,validation_cpu_seconds=0;
    double pulse_statistics_seconds=0,pulse_statistics_cpu_seconds=0;
    std::size_t affine_coefficient_cache_entries=0;
    std::uint64_t affine_preparations=0,affine_reuses=0,affine_interval_fast_paths=0;
    double affine_prepare_seconds=0,affine_fit_seconds=0;
    double affine_prepare_cpu_seconds=0,affine_fit_cpu_seconds=0;
    std::uint64_t amplitude_transforms=0,amplitude_observations=0,amplitude_cache_hits=0,amplitude_channel_updates=0;
    std::size_t amplitude_bins=0,amplitude_fft_samples=0,amplitude_channels=0;
    std::uint64_t amplitude_float_transforms=0,amplitude_precision_fallbacks=0;
    std::uint64_t amplitude_float_envelopes=0;
    std::uint64_t scheduled_null_samples=0,scheduled_omitted_fft_blocks=0;
    std::uint64_t scheduled_null_pushes=0;
    std::uint64_t scheduled_worker_preparations=0;
    // Samples retained for the next fresh source symbol before the current
    // conservative physical completion fence; included in ordinary search cost.
    std::uint64_t scheduled_carry_samples=0;
    std::uint64_t scheduled_colored_guard_activations=0,scheduled_quarter_guard_fallbacks=0;
    std::uint64_t scheduled_template_constructions=0,scheduled_template_cache_hits=0;
    std::uint64_t scheduled_float_moment_prepared_samples=0,scheduled_float_moment_preparations=0;
    std::uint64_t scheduled_float_moment_prepare_fallbacks=0;
    std::uint64_t scheduled_float_moment_attempts=0,scheduled_float_moment_pieces=0;
    std::uint64_t scheduled_float_moment_condition_fallbacks=0,scheduled_float_moment_error_fallbacks=0;
    std::uint64_t scheduled_float_moment_accepted_pairs=0,scheduled_float_moment_fp64_pairs=0,scheduled_float_moment_gram_fallbacks=0;
    std::size_t scheduled_template_cache_peak_bytes=0;
    std::size_t scheduled_temporal_peak_bytes=0;
    std::uint64_t scheduled_temporal_attempts=0,scheduled_temporal_ready=0,scheduled_temporal_fallbacks=0;
    std::uint64_t scheduled_temporal_samples=0,scheduled_temporal_plan_reuses=0,scheduled_temporal_values=0;
    std::uint64_t scheduled_temporal_shape_terms=0,scheduled_temporal_normalized_reuses=0;
    std::size_t scheduled_exact_families=0,scheduled_family_groups=0,scheduled_family_metadata_bytes=0;
    std::uint64_t scheduled_family_dispatches=0,scheduled_family_lanes=0;
    std::size_t scheduled_common_peak_bytes=0;
    std::uint64_t scheduled_common_families=0,scheduled_common_lanes=0,scheduled_common_pieces=0,scheduled_common_shapes=0;
    std::uint64_t scheduled_common_raw_samples=0,scheduled_common_raw_lane_samples=0;
    std::uint64_t scheduled_common_affine_preparations=0,scheduled_common_affine_lanes=0,scheduled_common_gram_fallbacks=0;
    // Pair counts are actual evaluations/failures; lanes are logical targets.
    // mapped_pairs + affine_reused_lanes == mapped_lanes, including rejected
    // groups. Native diagnostic per-hypothesis counters remain actual calls.
    std::uint64_t scheduled_common_mapped_pairs=0,scheduled_common_mapped_failed_pairs=0;
    std::uint64_t scheduled_common_mapped_lanes=0,scheduled_common_affine_reused_lanes=0;
    // Differing canonical phases are exact-reused only after full flat-block,
    // private-address/source-coordinate and original-mask preflight succeeds.
    std::uint64_t scheduled_common_phase_attempts=0,scheduled_common_phase_rejects=0;
    std::uint64_t scheduled_common_phase_families=0,scheduled_common_phase_lanes=0;
    // Raw fit pairs are distinct-frequency evaluations; reused lane samples
    // retain all original independent accumulators and noise observations.
    std::size_t scheduled_common_raw_peak_bytes=0;
    std::uint64_t scheduled_common_raw_fit_pairs=0,scheduled_common_raw_reused_lane_samples=0;
    std::uint64_t scheduled_common_raw_packed_values=0,scheduled_common_raw_workspace_samples=0;
    std::size_t scheduled_common_simd_peak_bytes=0;
    unsigned scheduled_common_simd_selected_isa=0; // 0 scalar,1 SSE2,2 AVX2; samples count proves actual use.
    std::uint64_t scheduled_common_simd_attempts=0,scheduled_common_simd_samples=0,scheduled_common_simd_lane_samples=0;
    std::uint64_t scheduled_common_simd_packed_values=0,scheduled_common_simd_guard_updates=0,scheduled_common_simd_outer_updates=0;
    std::uint64_t scheduled_common_simd_short_samples=0,scheduled_common_simd_workspace_samples=0,scheduled_common_simd_isa_samples=0;
    std::uint64_t scheduled_common_simd_scalar_lane_samples=0;
    std::uint64_t scheduled_common_phase_address_hits=0,scheduled_common_phase_address_misses=0;
    std::uint64_t scheduled_common_phase_address_queries=0;
    std::uint64_t amplitude_publication_polls=0,amplitude_unchanged_polls=0;
    std::uint64_t scheduled_float_gram_attempts=0,scheduled_float_gram_accepted_pairs=0;
    std::uint64_t scheduled_float_gram_range_fallbacks=0,scheduled_float_gram_rank_fallbacks=0;
    std::uint64_t scheduled_float_gram_error_fallbacks=0,scheduled_float_gram_helper_fallbacks=0;
    std::uint64_t scheduled_float_gram_signal_attempts=0;
    double scheduled_float_gram_maximum_relative_error=0;
};
// Finite clock/frequency/start-time hypotheses with streaming sufficient
// statistics. Storage depends on requested coverage and retained bits, never
// the duration of a symbol. A system-clock start window is mandatory.
class PatternCorrelator {
public:
    PatternCorrelator(Config, PatternSearch, std::size_t workspace_bytes,
                      PatternCorrelatorOptions = {});
    ~PatternCorrelator();
    PatternCorrelator(PatternCorrelator&&) noexcept;
    PatternCorrelator& operator=(PatternCorrelator&&) noexcept;
    void push(std::span<const float>, std::stop_token = {}, detail::CorrelationProjectionCache* = nullptr);
    // Advance physical time only when every retained scheduled lane proves the
    // complete interval is null. False requires feeding the original PCM.
    // Skipping supplies no signal, missing bit or observed absence.
    bool try_skip_unobserved(std::uint64_t samples, std::stop_token = {});
    std::optional<std::uint64_t> unobserved_end_from(std::uint64_t local_sample) const;
    std::optional<ScheduledCaptureEnvelope> scheduled_capture_envelope(std::uint64_t local_sample) const;
    void suspend_fresh_acquisition();
    // Optional helper lane only: retain established ordinary coherent streams
    // and physical absence scoring, but admit no unrelated new stream. False
    // rejects unsupported/no-admission states before mutation.
    bool suspend_helper_acquisition();
    PatternCompactAcquisitionState compact_acquisition_state() const noexcept;
    std::span<const PatternFrequencyRateHypothesis> compact_acquisition_hypotheses() const noexcept;
    bool set_compact_acquisition_prior(double frequency_origin_hz,std::span<const std::uint8_t> mask);
    bool clear_compact_acquisition_prior() noexcept;
    bool start_compact_acquisition_audit() noexcept;
    bool cold_acquisition_only() const noexcept;
    PatternAcquisitionDispatchState helper_acquisition_dispatch() const noexcept;
    bool set_helper_acquisition_dispatch(bool permitted) noexcept;
    bool set_compact_timing_prior(const PatternPeriodicTimingPrior&) noexcept;
    bool clear_compact_timing_prior() noexcept;
    bool compact_phase_assistance_supported() const noexcept;
    bool set_compact_phase_view(const PatternPhaseCurve*) noexcept;
    bool compact_phase_view_active() const noexcept;
    bool symbol_burst_tracking_active() const;
    void finish(std::stop_token = {});
    // Publish accepted decisions at each drain; chunk capacity never
    // requires a longer message before a pending symbol becomes visible.
    std::vector<PatternBurst> take_bursts();
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
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<PatternEvidence> candidates(std::size_t limit, std::stop_token) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // Every original clock hypothesis has scored a fully observed symbol.
    // A leading partial symbol and capture EOF do not satisfy this coverage.
    bool initial_search_complete() const;
    // Actual detector availability after reserving the complete search bank.
    bool drift_tolerant() const;
    std::size_t working_bytes() const;
    // Planner accounting for optional Burst control/lane state only. Existing
    // numerical banks and detector reservations are accounted independently.
    static std::size_t symbol_burst_control_bytes(std::size_t hypotheses,std::size_t pulse_lattices=0);
    // Includes promised optional detector state not yet materialized by a push.
    std::size_t reserved_workspace_bytes() const;
    std::optional<HelperReplayBudget> helper_replay_budget(std::size_t future_samples) const;
    // Additional allocation bound for one finite PCM push and its next drain.
    // SIZE_MAX disables optional shared preprocessing for unsupported geometry.
    std::size_t projection_cache_headroom(std::size_t input_samples) const;
    // No allocation: nullopt unsupported, zero eligible but cold/reselecting,
    // positive only after the original worker and Temporal choices are retained.
    std::optional<std::size_t> temporal_plan_workers() const;
    void set_workspace_bytes(std::size_t);
    // Release exact, reconstructible worker scratch without touching evidence
    // or promised detectors. Required rolling admissions take priority over it.
    void trim_optional_workspace();
    Diagnostics diagnostics() const;
    PatternCorrelatorWork work() const;
private:
    // Definition exists only in the source-inclusion internal regression.
    friend struct PatternCorrelatorTimingTestAccess;
    friend struct PatternCorrelatorPolicyAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
