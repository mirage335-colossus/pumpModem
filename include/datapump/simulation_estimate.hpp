#pragma once
#include "datapump/transfer.hpp"
#include "datapump/pattern_search.hpp"
#include <vector>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <memory>

namespace datapump::live { struct Settings; }
namespace datapump::modem {
class PatternAcquisitionClockPrior;
struct PatternAcquisitionClockResult;
struct PatternAcquisitionState;
struct PatternAcquisitionWorkBound;
}

namespace datapump::simulation {
inline constexpr std::string_view reference_cpu = "Ryzen 5 PRO 5650U (local acquisition calibration)";
inline constexpr std::string_view reference_gpu = "RTX 4090 Laptop GPU";

// Ordinary unknown-peer Live reception is modeled in both execution media.
// transfer::simulate's deliberate known-source fixed-rate comparator is a
// separate receiver experiment and may retain its narrower oscillator bank.
// A sampled simulation has its randomized startup/settling epoch coverage.
// Hardware keeps the configured peer's oscillator/correction coverage even
// when local capture timing falls back to the full arrival window.
// hardware_timing_model is an explicit planning assumption, not device evidence.
enum class ReceiverWorkMode { sampled_simulation, hardware_fallback, hardware_timing_model,
    // Shared UTC hop calendar with declared bounds before dated capture exists.
    // This is a planning assumption, never evidence of hardware qualification.
    hardware_calendar_model };
struct ReceiverTimingModel {
    // Omitted fields use the explicitly selected planning assumptions. A
    // caller with capture metadata must supply its larger uncertainty rather
    // than replacing it with the user's smaller Audio error selection.
    std::optional<double> capture_error_seconds;
    std::optional<double> capture_rate_uncertainty_fraction;
    std::optional<double> capture_seconds_per_frame;
    bool operator==(const ReceiverTimingModel&) const = default;
};

// Source-derived geometry/work diagnostics. These are planning references,
// never execution counters, observed latency or calibrated sensitivity.
struct ReceiverSearchDiagnostics {
    std::string receiver_strategy_requested,receiver_strategy_selected,receiver_strategy_reason;
    bool receiver_strategy_experimental=false;
    double receiver_cpu_budget=0,existing_processing_ratio=0,frequency_axis_processing_ratio=0,amplitude_processing_ratio=0;
    // Receiver-only residual carrier expansion; oscillator clock bounds stay fixed.
    double frequency_search_multiplier=1;
    bool scheduled=false,scheduled_cost_calibrated=false,scheduled_timing_qualified=false;
    unsigned scheduled_frame_hops=1;
    std::uint64_t scheduled_active_hops=1;
    double scheduled_effective_multiplier=1,scheduled_active_seconds=0,scheduled_frame_seconds=0;
    double scheduled_minimum_quiet_seconds=0,scheduled_flat_duty=1;
    double scheduled_recording_union_upper=1,scheduled_fft_seconds=0;
    double scheduled_fft_union_upper=1,scheduled_interval_width_seconds=0;
    // Upper raw PCM demand for one frame's admitted interval union, before
    // concurrent decoding/reuse. Not a measured peak or a hard allocation.
    double scheduled_capture_frame_bytes_upper=0;
    std::size_t scheduled_canonical_phases=0;
    double scheduled_timing_positions=0,scheduled_full_timing_positions=0;
    double scheduled_mean_processing_ratio=0,scheduled_deadline_processing_ratio=0;
    double scheduled_guaranteed_recovery_seconds=0;
    std::string scheduled_limit;
    std::uint32_t sample_rate=0;
    unsigned dsss_factor=1;
    modem::OuterDsssVersion outer_dsss_version=modem::OuterDsssVersion::legacy_v1;
    std::size_t workspace_bytes=0;
    double per_bank_workspace_bytes=0;
    std::uint64_t fine_chip_samples=0,inner_chip_samples=0,symbol_samples=0;
    double fine_chip_seconds=0,inner_chip_seconds=0,symbol_seconds=0;
    double timing_grid_seconds=0,canonical_phase_step_seconds=0;
    double first_fft_seconds=0,fft_hop_seconds=0;
    // Previous all-selected union readiness and earliest guarded component.
    double first_qualified_window_seconds=0,first_component_window_seconds=0,qualified_ready_batch_slots=0;
    // Representative execution geometry, not all-cohort operation allowances.
    double input_fft_transforms=0,timing_component_dispatches=0,component_fallback_hops=0;
    double paired_direct_template_jobs=0,partitioned_template_jobs=0,paired_fallback_components=0;
    double partitioned_input_transforms=0,partitioned_template_transforms=0,partitioned_inverse_transforms=0;
    std::size_t partitioned_tile_min=0,partitioned_tile_max=0;
    std::size_t canonical_phases=0;
    double combined_arrival_half_width_seconds=0;
    bool arrival_window_available=false,restricted_fft_modeled=false;
    // Counts over the modeled observation for ONE representative epoch/key.
    // Cohort CPU allowances separately cover all initial/rolling epochs.
    // Retained template jobs are dispatches: one original job can execute in
    // several components. Full jobs/positions charge each original hop once.
    double full_template_jobs=0,retained_template_jobs=0,direct_template_jobs=0;
    double full_start_positions=0,retained_start_positions=0;
    std::string backend,scope,fallback_reason;
    // Source-derived dispatch description; observed execution counters remain
    // on PatternReceiver. Never infer a hardware measurement from this model.
    modem::SearchArithmetic search_arithmetic=modem::SearchArithmetic::default_mode;
    std::string arithmetic_operands,arithmetic_accumulation,arithmetic_backend,arithmetic_limit;
    std::string template_reuse;
    bool arithmetic_experimental=false;
};

// A planning assumption for one settled Burst reception after ordinary acquisition.
// Capture exclusion is conditional on the Live union proof; these are neither
// observed execution counters nor a promise that other active streams fit.
struct SymbolBurstWorkDiagnostics {
    bool enabled=false,modeled=false,timing_qualified=false;
    unsigned wait_symbols=0,effective_random_limit=0,recommended_wait_symbols=0;
    std::size_t initial_profiles=0,initial_key_families=0;
    bool configured_bank_modeled=false;
    double support_seconds=0,minimum_spacing_seconds=0,mean_spacing_seconds=0,maximum_spacing_seconds=0;
    double guard_before_seconds=0,guard_after_seconds=0,capture_seconds_per_slot=0;
    double useful_quiet_seconds=0,capture_fraction=1;
    double seeking_realtime_ratio=0,acquisition_admission_seconds=0,acquisition_observation_seconds=0,acquisition_cpu_seconds=0;
    double acquisition_cpu_upper_seconds=0;
    double acquisition_new_epoch_admissions=0;
    double tracked_cpu_seconds_per_slot=0,tracked_realtime_ratio=0,quiet_drain_ratio=0;
    double tracked_cpu_upper_seconds_per_slot=0,tracked_realtime_upper_ratio=0;
    // Minimum-spacing service denominator; CPU retains the longest initial
    // capture debt. It can be shorter than acquisition_observation_seconds.
    double finite_observation_seconds=0,finite_receiver_cpu_seconds=0,finite_realtime_ratio=0;
    double finite_realtime_upper_ratio=0; // Same separate rebuild fallback as the ordinary estimate.
    // True only for the engineering service allowance, never measured rescue.
    bool recurring_service_fits=false;
    std::string scope,limit;
};

struct Estimate {
    bool gpu_work_supported=true;
    // Engineering estimates, not measurements or calibrated probabilities.
    // Idealized success conditional on completing receiver computation: the
    // supplied draft survives reception/correction. This is not a deadline or
    // an empirical success rate for the implementation.
    // success_probability is meaningful only when confidence_available or
    // reference_probability_available is true, with their distinct scopes.
    double success_probability = 0;
    // First raw bit in the same geometry, independent of draft length/FEC.
    // Can remain supported when complete-draft tracking is outside coverage.
    double one_bit_success_probability = 0;
    bool one_bit_confidence_available = false;
    // Monte Carlo sampling uncertainty, conditional on the channel/model.
    // These intervals do not include model error or physical-link uncertainty.
    std::size_t probability_trials = 0;
    double one_bit_probability_low = 0, one_bit_probability_high = 1;
    double success_probability_low = 0, success_probability_high = 1;
    bool probability_interval_available = false;
    bool probability_search_approximation = false;
    std::size_t probability_carrier_candidates = 0;
    std::string probability_model_limit;
    // Conditional matched-template references remain useful outside the
    // validated receiver model. They do not make confidence_available true:
    // device timing, outer-code rejection and adaptive acquisition can remain
    // unmodeled. The numeric probability fields above carry a reference only
    // when the corresponding reference_available flag is set.
    bool probability_reference_only = false;
    bool reference_probability_available = false;
    bool one_bit_reference_available = false;
    double cpu_seconds = 0;
    // Receive projection, search, tracking and nominal payload processing;
    // excludes synthetic channel generation and exceptional recovery searches.
    // Divide by simulated_seconds for a rough real-time workload,
    // not a measured CPU utilization or a guarantee about per-bit latency.
    double receiver_cpu_seconds = 0;
    double logical_acquisition_threshold = 0,logical_search_trials = 0;
    std::uint64_t execution_projection_bin_samples=0;
    double execution_minimum_rate=0,execution_maximum_rate=0;
    bool execution_compact=false,execution_pulse_projected=false,execution_drift_supported=false,execution_differential_supported=false;
    SymbolBurstWorkDiagnostics symbol_burst;
    // Separate continuous hardware workload: one complete fresh UTC cohort
    // per admission cadence, through its acquisition/retirement lifetime.
    // A short draft can end before the first FFT and must not imply an idle
    // listening cost of zero. These allowances do not change finite simulation
    // times/probabilities, and are not measured utilization or peak deadlines.
    bool hardware_sustained_modeled = false;
    double hardware_sustained_realtime_ratio = 0;
    double hardware_sustained_tracking_realtime_ratio = 0;
    double fallback_hardware_sustained_realtime_ratio = 0;
    double hardware_cohort_lifetime_seconds = 0;
    double hardware_sustained_receiver_instances = 0;
    // False when additional implemented work lacks a defensible engineering
    // allowance. Geometry/resource diagnostics remain useful in that case.
    bool receiver_work_supported = true;
    // Components included in receiver_cpu_seconds. Frontend work follows the
    // original real sample rate; projected private search follows chip cadence
    // and runs serially in the current backend (also in the GPU projection).
    // Kernel work estimates reuse across nearby clock candidates. Its separate
    // upper allowance charges rebuilding every fractional-clock cell and is
    // excluded from the central totals. Neither is a host measurement.
    double receiver_frontend_seconds = 0;
    double receiver_search_seconds = 0;
    double receiver_kernel_rebuild_seconds = 0;
    double receiver_kernel_rebuild_upper_seconds = 0;
    bool pulse_projection_modeled = false;
    bool pulse_segment_projection_modeled = false;
    bool kernel_rebuild_upper_bound = false;
    // Ordinary receive bookkeeping, payload codecs and permitted text view.
    // Included once in CPU, receiver-only and hypothetical GPU totals. This
    // rounded reference allowance includes the mitigation subset below.
    double payload_processing_seconds = 0;
    // Estimated incremental CPU-mitigation cost within payload processing,
    // not an additional charge or a multiplier on unchanged modem DSP.
    double mitigation_seconds = 0;
    double gpu_seconds = 0;
    // Included in both totals: incremental serial whole-symbol continuation for
    // one established signal stream per matching FFT profile, through observed
    // absence. Competing/noise tracks and reacquisition are not upper-bounded.
    // Unrelated key/epoch banks add acquisition work, not established streams.
    double tracking_seconds = 0;
    // Logical continuation/absence windows also count compact profiles, whose
    // continuation CPU is already included in receiver_search_seconds.
    double tracking_symbol_windows = 0;
    double simulated_seconds = 0;
    double modeled_symbol_snr_db = 0;
    // Nonnegative loss from random phase drift during one complete coherent
    // symbol. Pattern reversals do not reset the carrier's phase history.
    double phase_coherence_loss_db = 0;
    // Eligible geometry can additionally combine four section fits. These
    // fields describe that policy, not a live receiver's allocation: compact
    // banks retain the coherent-only path if the added state cannot fit.
    // True only when eligible geometry uses the limited coherent reference
    // instead of the combined statistical model. That reference retains the
    // detector-choice penalty even on compact fallback; it is not a lower bound.
    bool coherent_reference_only = false;
    // Fixed matched-statistic trials modeled eligible branches, correlated noise,
    // phase paths and competing bits. This does not run the adaptive receiver.
    bool drift_model_available = false;
    // Same modeled scenario using only the original coherent score, without
    // the extra detector-choice penalty. Requires drift_model_available and
    // confidence_available; it is a comparison, not a measured receiver run.
    double coherent_success_probability = 0;
    std::size_t drift_sections = 1;
    double drift_section_seconds = 0;
    // Phase loss within the longest implemented section, diagnostic only.
    // The combined model samples phase paths rather than substituting this
    // scalar loss into the original coherent probability formula.
    double section_phase_coherence_loss_db = 0;
    // Receiver-local differential geometry. Zero means ineligible. Counts
    // describe eligible geometry, not an actual live allocation; the model
    // also checks the workspace allowance before crediting this detector.
    std::uint64_t differential_windows = 0;
    double differential_window_seconds = 0;
    bool differential_model_available = false;
    double differential_added_detection_probability = 0;
    double carrier_offset_hz = 0;
    // Finite coverage of the configured bank; oscillator_search_limited distinguishes
    // it from the full model/margin request below. An unaffordable legacy bank
    // may fall back locally at runtime; no probability models that fallback.
    double carrier_search_half_width_hz = 0;
    double requested_carrier_search_half_width_hz = 0;
    double clock_search_half_width_ppm = 0;
    double requested_clock_search_half_width_ppm = 0;
    std::size_t frequency_rate_hypotheses = 0;
    std::size_t epoch_hypotheses = 0;
    // Timing origins summed over the paired frequency/rate bank, per epoch/key.
    // phase_groups is the same maximum simultaneous canonical phase fit count
    // used by the compact work allowance, not additional timing origins.
    double timing_hypotheses = 0;
    std::size_t timing_phase_groups = 1;
    ReceiverSearchDiagnostics receiver_geometry;
    ReceiverWorkMode receiver_work_mode = ReceiverWorkMode::sampled_simulation;
    bool timing_window_modeled = false;
    // Per epoch in the INITIAL cohort, before/after the conservative arrival
    // envelope. These are work counts, never threshold hypothesis counts.
    double fft_acquisition_batches = 0, fft_retained_acquisition_batches = 0;
    // Automatic hardware FFT acquisition additionally admits fresh epochs.
    // Counts are conservative totals per key/profile over the observation,
    // separate from epoch_hypotheses (the initial cohort). Fixed timestamps,
    // sampled simulation and compact references have no added FFT cohort here.
    double new_epoch_admissions = 0;
    double new_epoch_full_fft_batches = 0, new_epoch_retained_fft_batches = 0;
    double new_epoch_qualified_ready_batch_slots = 0;
    // Included in frontend/total cost. Constructor allowance covers zeroing and
    // any initial template transforms; fresh batch work includes all later FFTs.
    // These are engineering allowances, not measured execution or RAM bounds.
    double initial_epoch_setup_seconds = 0;
    double new_epoch_frontend_seconds = 0, new_epoch_setup_seconds = 0;
    std::string receiver_work_assumptions;
    // Same configured peer oscillator bank with the complete arrival window.
    // This is a separate engineering fallback, never omitted search coverage.
    double fallback_receiver_cpu_seconds = 0;
    double fallback_timing_hypotheses = 0;
    bool oscillator_search_limited = false;
    // Includes membership in the paired region, not just independent extents.
    bool clock_in_search = true;
    std::size_t receiver_profiles = 0;
    bool profile_matches = false;
    // A matching profile must have enough modeled workspace for the expanded
    // FFT or compact search. This is a conservative per-bank allowance, not an allocation
    // guarantee; reference compute times still describe the requested work.
    bool receiver_workspace_supported = false;
    bool confidence_available = false;
    // The probability approximation requires the simulated carrier to lie
    // within the default receiver's finite frequency-search span. A signal
    // outside it is not proven impossible to receive; its probability is
    // unsupported by this model regardless of the signal strength.
    bool carrier_in_search = false;
    // The production receiver currently has no GPU execution backend.
    bool gpu_hypothetical = true;
};

// Uses an already encoded draft estimate: it never changes framing, encodes
// another message, samples PCM, measures elapsed time or inspects host hardware.
// options.modem is the actual selected transmit geometry. An empty profile
// span means that same profile; otherwise supply the independently configured
// receive bank. receive_key_count scales bank cost, not success probability.
// raw_bits covers both exact binary drafts and the fixed short dictionary.
// compute_probability=false returns search/workspace and compute estimates
// without probability trials; confidence_available then remains false.
// differential_window_seconds must match the receiver-local PatternSearch
// setting. Zero disables that detector; the default matches production use.
// probability_trials controls only Monte Carlo precision, not receiver policy.
// Details, assumed reference throughput and limitations:
// docs/simulation-estimates.md.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options, bool raw_bits,
                  const modem::ChannelConfig& channel,
                  std::span<const modem::Config> receive_profiles = {},
                  std::size_t receive_key_count = 1,
                  bool compute_probability = true,
                  double differential_window_seconds = 100,
                  std::size_t probability_trials = 4096,
                  ReceiverWorkMode work_mode = ReceiverWorkMode::sampled_simulation,
                  ReceiverTimingModel timing_model = {},
                  std::stop_token stop = {});
// Same model, using the actual frontend's immutable settings for pure Live
// workspace reservations plus the explicitly declared candidate profile/key
// span. It does not predict a complete post-edit cohort or construct Session/
// capture or grant runtime eligibility. Scheduled profiles share the original
// input sample rate; application-selected-rate comparisons are separate.
// Profile/key multiplicities follow estimate's existing convention. Unknown
// complete execution costs are NaN with receiver_work_supported=false; an
// unavailable cost/probability does not imply receiver_workspace_supported=false.
Estimate estimate_with_receiver_settings(const transfer::Estimate& transmission,
                  const transfer::Options& options,bool raw_bits,
                  const modem::ChannelConfig& channel,const live::Settings& receiver_settings,
                  std::span<const modem::Config> receive_profiles = {},
                  std::size_t receive_key_count = 1,bool compute_probability = true,
                  double differential_window_seconds = 100,std::size_t probability_trials = 4096,
                  ReceiverWorkMode work_mode = ReceiverWorkMode::sampled_simulation,
                  ReceiverTimingModel timing_model = {},std::stop_token stop = {});
// Explicit conditional frequency work. These inputs never qualify a tone or
// manufacture a forecast from enabled helper power. They describe a reference
// already qualified by the actual receiver, or a separately validated forecast.
enum class HelperScenarioOrigin { unavailable, measured_reference, requirements_forecast, validated_forecast };
namespace detail {struct HelperJointClockProof;}
// The joint owner consumes an opaque diagnostic copy supplied by the trusted
// joined runtime producer after ACTUAL core execution. Its public result fields
// are consistency-checked metadata, not an unforgeable execution receipt.
enum class HelperExecutionOwner { original_masked_owner, conditional_catalogue_owner, executed_joint_masked_owner };
struct HelperFrequencyCell {
    modem::PatternFrequencyRateHypothesis hypothesis;
    double frequency_half_width_hz=0,clock_lower_ppm=0,clock_upper_ppm=0;
};
struct HelperFrequencyStrip {
    double clock_lower_ppm=0,clock_upper_ppm=0;
    // Receiver-input residual frequency = intercept + slope * physical ppm.
    double frequency_intercept_hz=0,frequency_slope_hz_per_ppm=0,frequency_half_width_hz=0;
};
// Provenance from the actual joined producer, not inferred from oscillator
// presets, enabled power, marker presence or the numeric selected-pair count.
enum class HelperJointClockSourceRelation {unknown,physical_audio_unsteered,common_corrected_robust_source};
const char* helper_joint_clock_source_domain(HelperJointClockSourceRelation) noexcept;
struct HelperFrequencyInputMap {
    std::uint32_t physical_sample_rate=0,logical_sample_rate=0;
    std::uint64_t input_view_id=0,input_origin_samples=0;
    double physical_passband_lower_hz=0,physical_passband_upper_hz=0;
    double frequency_correction_hz=0;
    bool conjugate_image=false;
    // A filter/NCO is not automatically covered by the ordinary real-noise
    // model. A caller needs actual projection/covariance validation evidence.
    std::string ordinary_noise_model_evidence;
    // Authoritative planned fixed notch from this scenario's power/profile
    // union. A finite reference must not replan it from stale GUI settings.
    std::optional<double> payload_filter_hz;
    // Used only by executed joint copies. Unknown cannot grant joined cost
    // provenance; ordinary frequency-only scenarios keep this field unknown.
    HelperJointClockSourceRelation joint_source_relation=HelperJointClockSourceRelation::unknown;
};
// Conditional source requirements, not a probability that a tone qualifies.
// The six-column finite WLS geometry is evaluated on a unit straight tone;
// white-noise scale comes from the entered physical C/N0. Runtime residual,
// curvature, canonical punctuation and cycle-consistency gates still apply.
struct HelperStrongRequirements {
    bool frequency_requirements_available=false,chord_requirements_available=false;
    std::uint32_t sample_rate=0;
    std::size_t fit_samples=0;
    double expected_cn0_db_hz=0,fit_seconds=0,expected_explained_fraction=0;
    double frequency_radius_hz=0,phase_radius_radians=0,process_frequency_radius_hz=0;
    double chord_mean_radius_hz=0,chord_aperture_seconds=0,minimum_observation_seconds=0;
    std::string source_requirements_id,conditions,reason;
};
HelperStrongRequirements helper_strong_requirements(std::uint32_t physical_sample_rate,
    double observed_helper_hz,double search_half_width_hz,double expected_cn0_db_hz,
    double phase_noise_radians_per_sqrt_second,std::uint64_t acquisition_frontier_sample);
struct HelperFrequencyGeometry {
    std::uint64_t projection_bin_samples=0;
    double minimum_rate=0,maximum_rate=0;
    bool compact=false,pulse_projected=false,pulse_segmented=false;
    bool drift_supported=false,differential_supported=false;
};
struct HelperFrequencyProfile {
    std::size_t profile_index=0;
    HelperExecutionOwner owner=HelperExecutionOwner::original_masked_owner;
    // Include every actually active/protected pair, not only a desired RF mask.
    std::vector<HelperFrequencyCell> cells;
    HelperFrequencyStrip physical_domain;
    HelperFrequencyInputMap input;
    HelperFrequencyGeometry actual_geometry;
    // Original masks preserve their full original logical trials. A newly
    // born catalogue requires the core's separate original-statistics seam;
    // diagnostic original-pair counts alone cannot set this evidence.
    std::string original_trial_policy_evidence;
    std::shared_ptr<const detail::HelperJointClockProof> joint_clock_proof;
};
enum class HelperForecastUtilityScope { unavailable, validated_strong_only, validated_global };
struct HelperFrequencyForecastCoverage {
    HelperForecastUtilityScope utility_scope=HelperForecastUtilityScope::unavailable;
    std::string source_revision,estimator_requirements_id,validation_evidence;
    std::uint32_t physical_sample_rate=0;
    double minimum_cn0_db_hz=0,maximum_cn0_db_hz=0;
    double maximum_clock_magnitude_ppm=0,maximum_phase_noise_degrees_per_sqrt_second=0;
    double maximum_discovery_seconds=0;
};
// Actual masked-tone analysis geometry, separate from the short polynomial
// fit. Support/aperture is not a predicted CI, qualification probability or
// reserved memory. Coarse FFT width remains an engineering seed envelope.
struct HelperMaskedFrequencyRequirements {
    bool geometry_supported=false,coarse_required=false,continuous_phase_assumed=false;
    std::uint32_t physical_sample_rate=0;
    std::size_t required_bytes=0,coarse_bytes=0;
    double profile_half_width_hz=0,aperture_seconds=0,startup_support_seconds=0,cold_deadline_seconds=0;
    std::string reason;
};
HelperMaskedFrequencyRequirements helper_masked_frequency_requirements(std::uint32_t physical_sample_rate,
    double expected_helper_hz,double constructor_half_width_hz,double phase_noise_radians_per_sqrt_second,
    bool continuous_phase_assumed);
struct HelperFrequencyForecastRequest {
    std::uint32_t physical_sample_rate=0;
    double physical_passband_lower_hz=0,physical_passband_upper_hz=0;
    double helper_frequency_hz=0,expected_usable_helper_cn0_db_hz=0;
    // A dated capture frontier determines the actual finite opportunity weight;
    // zero forecasts a new capture generation, not an indefinitely valid fit.
    std::uint64_t acquisition_frontier_sample=0;
    bool assume_independently_qualified_punctuation=false;
    bool assume_continuous_helper_phase=false;
    // Explicit fixed nominal resampling retains frequency in Hz and the full
    // original DATA-clock domain. Steered/unknown maps need separate evidence.
    bool fixed_nominal_rate_conversion=false;
    std::optional<HelperFrequencyForecastCoverage> validated_coverage;
};
struct HelperFrequencyCosts {
    std::string validation_evidence,cpu_reference_id;
    // Explicit measured/validated stage costs, not pair-count discounts.
    std::optional<double> discovery_cpu_seconds,prequalification_broad_cpu_seconds;
    std::optional<double> additional_frontend_cpu_per_second,optional_probe_cpu_per_second;
    // Omission retains the ordinary backend's modeled tracking component.
    std::optional<double> ordinary_tracking_cpu_per_second;
};
struct QualifiedHelperFrequencyScenario {
    HelperScenarioOrigin origin=HelperScenarioOrigin::unavailable;
    std::string reference_evidence;
    std::vector<HelperFrequencyProfile> profiles;
    HelperFrequencyCosts costs;
    std::optional<HelperFrequencyForecastCoverage> forecast_coverage;
    std::optional<double> expected_usable_helper_cn0_db_hz;
    double qualification_aperture_seconds=0;
};
struct HelperJointClockMetadata {
    std::uint64_t first_input_sample=0,last_input_sample=0,captured_input_end_sample=0;
    std::uint64_t reference_evidence_sample=0,reference_captured_end_sample=0,executed_template_jobs=0;
    std::size_t original_pairs=0,frequency_only_pairs=0,joint_pairs=0,original_clock_rates=0,joint_clock_rates=0;
    std::size_t owned_bytes=0,projection_bin_samples=0,original_phase_groups=0;
    HelperJointClockSourceRelation source_relation=HelperJointClockSourceRelation::unknown;
};
class HelperJointClockDiagnosticPool;
struct HelperJointClockDiagnosticStatus {
    std::size_t slots=2,occupied_slots=0,slot_bytes=0;
    // Actual allocator requests, including pool/lease shared-control blocks.
    // Account object/its control block and allocator overhead are separate.
    std::size_t allocator_requested_bytes=0,account_object_bytes=0;
};
// A separately funded GUI diagnostic arena; never borrowed from DSP/codec or
// ordinary receiver promises. Exactly TWO fixed sizeof-derived slots, each
// bounded to4096 original pairs and64 DATA intervals. No fallback vector heap.
std::size_t helper_joint_clock_diagnostic_gui_grant_bytes();
std::shared_ptr<HelperJointClockDiagnosticPool> make_helper_joint_clock_diagnostic_pool(std::size_t gui_grant_bytes);
HelperJointClockDiagnosticStatus helper_joint_clock_diagnostic_status(const std::shared_ptr<HelperJointClockDiagnosticPool>&);
struct HelperJointClockCopy {
    std::shared_ptr<const detail::HelperJointClockProof> proof;
    HelperJointClockMetadata metadata;
    std::string reason;
    // Pooled decoder path uses static text only, avoiding untracked temporary
    // string allocations. Component/background calls may use reason above.
    const char* runtime_reason=nullptr;
};
// Trusted-producer API: the caller must supply its actual synchronous joined
// core result, not a constructed PatternAcquisitionClockResult. The function
// checks matching geometry/count/work metadata but does not authenticate that
// execution occurred. Call while the prior's borrowed spans still exist, AFTER
// joined execution and BEFORE resolve/mask replacement. The grant owns retained
// metadata only; allocator overhead is separate. No PCM or payload is copied.
HelperJointClockCopy copy_executed_helper_joint_clock(const modem::Config&,
    const modem::PatternAcquisitionClockPrior&,const modem::PatternAcquisitionClockResult&,
    const modem::PatternAcquisitionState& before,const modem::PatternAcquisitionWorkBound& before_work,
    const HelperFrequencyInputMap&,std::size_t optional_grant_bytes);
// Same trusted-producer premise and metadata validation, using one fixed slot.
// The shared lease returns it only after the LAST Snapshot/Planner borrower.
HelperJointClockCopy copy_executed_helper_joint_clock(const std::shared_ptr<HelperJointClockDiagnosticPool>&,
    const modem::Config&,const modem::PatternAcquisitionClockPrior&,const modem::PatternAcquisitionClockResult&,
    const modem::PatternAcquisitionState& before,const modem::PatternAcquisitionWorkBound& before_work,
    const HelperFrequencyInputMap&);
bool same_helper_joint_clock_geometry(const std::shared_ptr<const detail::HelperJointClockProof>&,
    const modem::Config&,const modem::PatternAcquisitionClockPrior&,const HelperFrequencyInputMap&);
struct HelperJointClockScenario {
    std::optional<QualifiedHelperFrequencyScenario> scenario;
    HelperJointClockMetadata metadata;
    std::string reason;
};
// Cost-only current-geometry illustration from observed joint cells. No
// enabled/CN0/punctuation forecast and no future-support or RX guarantee.
HelperJointClockScenario helper_executed_joint_scenario(const transfer::Estimate&,
    const transfer::Options&,const modem::ChannelConfig&,
    const std::shared_ptr<const detail::HelperJointClockProof>&,
    const HelperFrequencyCosts& costs={},ReceiverWorkMode=ReceiverWorkMode::hardware_fallback,
    ReceiverTimingModel={},const live::Settings* receiver_settings=nullptr,std::stop_token={});
struct HelperFrequencyForecast {
    HelperStrongRequirements requirements;
    HelperMaskedFrequencyRequirements masked_requirements;
    std::optional<QualifiedHelperFrequencyScenario> scenario;
    std::string reason;
};
// Builds a conditional original-grid frequency execution scenario from the
// same conservative selector used by Live. It does not invent qualification,
// change the oscillator model, supply missing noise-model validation, or price
// component CPU from pair counts. Unsupported wide/shared births decline.
HelperFrequencyForecast helper_frequency_forecast(const transfer::Estimate& transmission,
    const transfer::Options&,const modem::ChannelConfig&,const HelperFrequencyForecastRequest&,
    const HelperFrequencyCosts& costs={},ReceiverWorkMode work_mode=ReceiverWorkMode::hardware_fallback,
    ReceiverTimingModel timing_model={},const live::Settings* receiver_settings=nullptr,
    std::stop_token stop={});
// Explicit acquired-reference counterfactual. The first-bit observation has
// a known cropped onset; no arrival-search or whole-message probability is
// implied. Qualification/startup costs are independent. Source channel errors
// and payload SNR remain unchanged; unsupported phase/arithmetic declines.
struct HelperFirstBitConditions {
    modem::ChannelConfig channel;
    bool independent_reference_before_observation=false;
    std::size_t starts=1,trials=256;
};
struct HelperFirstBitReference {
    bool available=false;
    double correct=0,wrong=0,correct_low=0,correct_high=1;
    std::size_t trials=0,original_jobs=0,selected_jobs=0,retained_clock_rates=0;
    std::string reason;
    std::optional<double> payload_filter_hz;
};
// Exact metadata from a bounded pristine ordinary native receiver. Counts
// name logical job descriptors, not physical transforms, CPU time or benefit.
struct HelperNativeWork {
    bool available=false,streaming_probe=false;
    std::size_t original_jobs=0,conditional_jobs=0,original_pairs=0,conditional_pairs=0;
    std::size_t retained_clock_rates=0,original_starts=0,bin_samples=0;
    std::size_t original_descriptor_bytes=0,probe_workspace_bytes=0;
    std::uint64_t original_trial_charge=0;
    double original_batch_end_threshold=0;
    std::string reason;
    HelperFirstBitReference first_bit;
};
HelperNativeWork helper_native_work(const transfer::Options&,const QualifiedHelperFrequencyScenario&,
    ReceiverWorkMode,const live::Settings* receiver_settings=nullptr,std::stop_token={},
    const HelperFirstBitConditions* first_bit=nullptr);
struct HelperFrequencyEstimate {
    Estimate baseline,assisted;
    bool scenario_available=false,conditional_probability_available=false,conditional_one_bit_probability_available=false;
    bool discovery_cost_available=false,qualified_cost_available=false,forecast_available=false;
    // Kept separate from finite acquisition and payload work in assisted.
    double discovery_cpu_seconds=0,prequalification_broad_cpu_seconds=0;
    double additional_frontend_realtime_ratio=0,optional_probe_realtime_ratio=0;
    double qualified_sustained_realtime_ratio=0,ordinary_tracking_realtime_ratio=0;
    std::string reason;
};
HelperFrequencyEstimate estimate_helper_frequency(const transfer::Estimate& transmission,
    const transfer::Options&,bool raw_bits,const modem::ChannelConfig& uncorrected_channel,
    const QualifiedHelperFrequencyScenario&,std::span<const modem::Config> receive_profiles={},
    std::size_t receive_key_count=1,bool compute_probability=true,
    double differential_window_seconds=100,std::size_t probability_trials=4096,
    ReceiverWorkMode work_mode=ReceiverWorkMode::sampled_simulation,
    ReceiverTimingModel timing_model={},const live::Settings* receiver_settings=nullptr,
    std::stop_token stop={});

// Pure planner support query with the same configured bank, timing coverage,
// backend and workspace decisions. Skips Burst service/debt/recommendation work
// and probability trials. Only coverage, profile and workspace fields are a
// supported result; CPU/service/probability fields must not be displayed.
Estimate estimate_receiver_support(const transfer::Estimate& transmission,
                  const transfer::Options& options,const modem::ChannelConfig& channel,
                  std::span<const modem::Config> receive_profiles = {},std::size_t receive_key_count = 1,
                  ReceiverWorkMode work_mode = ReceiverWorkMode::sampled_simulation,
                  ReceiverTimingModel timing_model = {},const live::Settings* receiver_settings = nullptr,
                  std::stop_token stop = {});
}
