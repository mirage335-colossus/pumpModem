#pragma once
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
};
enum class PatternCorrelationBackend { raw, pulse, pulse_moments, pulse_segments, amplitude };
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
    double admitted_frequency_half_width_hz=0,admitted_clock_half_width_ppm=0;
};
PatternScheduledComplexGeometry pattern_scheduled_complex_geometry(const Config&,const PatternSearch&);
struct PatternCorrelatorWork {
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
    std::size_t scheduled_template_cache_peak_bytes=0;
    std::uint64_t amplitude_publication_polls=0,amplitude_unchanged_polls=0;
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
    void finish(std::stop_token = {});
    // Publish accepted decisions at each drain; chunk capacity never
    // requires a longer message before a pending symbol becomes visible.
    std::vector<PatternBurst> take_bursts();
    PatternBurst provisional() const;
    std::vector<PatternEvidence> candidates() const;
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // Every original clock hypothesis has scored a fully observed symbol.
    // A leading partial symbol and capture EOF do not satisfy this coverage.
    bool initial_search_complete() const;
    // Actual detector availability after reserving the complete search bank.
    bool drift_tolerant() const;
    std::size_t working_bytes() const;
    // Includes promised optional detector state not yet materialized by a push.
    std::size_t reserved_workspace_bytes() const;
    // Additional allocation bound for one finite PCM push and its next drain.
    // SIZE_MAX disables optional shared preprocessing for unsupported geometry.
    std::size_t projection_cache_headroom(std::size_t input_samples) const;
    void set_workspace_bytes(std::size_t);
    // Release exact, reconstructible worker scratch without touching evidence
    // or promised detectors. Required rolling admissions take priority over it.
    void trim_optional_workspace();
    Diagnostics diagnostics() const;
    PatternCorrelatorWork work() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
