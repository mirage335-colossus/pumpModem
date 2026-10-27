#pragma once
#include "fhss_profile.hpp"
#include "datapump/transfer.hpp"
#include "datapump/live.hpp"
#include "datapump/helper_reference_precision.hpp"
#include "datapump/lpi_estimate.hpp"
#include "datapump/simulation_estimate.hpp"
#include <memory>
#include <limits>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace datapump::gui::planner {
struct QualifiedHelperCurveScenario {
    double target_db_hz=0;
    simulation::QualifiedHelperFrequencyScenario scenario;
};
struct Inputs {
    transfer::Options options;
    tuning::PatternMode mode=tuning::PatternMode::auto_pattern;
    modem::ChannelConfig channel;
    std::optional<lpi::Hopping> observer_hopping;
    fhss::Profile fake_hopping;
    double target_db_hz=-8;
    double tx_dbm=3;
    double path_loss_db=120;
    double noise_density_dbm_hz=-164;
    std::size_t wire_bits=1;
    bool empty_draft=false; // One-bit preview; the composer remains empty.
    // Presentation only; the actual byte allowance above drives every check.
    unsigned dsp_workspace_percent=0;
    simulation::ReceiverWorkMode receiver_work_mode=simulation::ReceiverWorkMode::sampled_simulation;
    simulation::ReceiverTimingModel receiver_timing_model;
    // Current immutable frontend/cohort reservations plus candidate-profile
    // necessary fit; no full post-Use-target allocation or runtime guarantee.
    std::optional<live::Settings> receiver_settings; // Never a Session/capture owner.
    // Explicit measured or validated-forecast scenarios for exact targets.
    // Enabled helper power alone never creates these entries.
    std::vector<QualifiedHelperCurveScenario> helper_frequency_scenarios;
    // An immutable, funded copy of an actually executed current native joint
    // RF/DATA-clock batch. Only the selected current target may illustrate its
    // backend cost. It grants no forecast/future-support or RX probability.
    std::shared_ptr<const simulation::detail::HelperJointClockProof> helper_executed_joint_clock;
    std::string helper_joint_diagnostic_reason;
    std::size_t helper_joint_diagnostic_allocator_bytes=0,helper_joint_diagnostic_account_bytes=0;
    std::size_t helper_joint_diagnostic_slot_bytes=0,helper_joint_diagnostic_occupied_slots=0;
    // Default planner forecasts are conditional source requirements. Validation
    // coverage and actual component costs are supplied independently; absence
    // leaves corresponding RX/sustained-CPU graph gaps with concrete reasons.
    simulation::HelperFrequencyCosts helper_forecast_costs;
    std::optional<simulation::HelperFrequencyForecastCoverage> helper_forecast_validation;
    bool helper_forecast_assume_qualified_punctuation=false;
    // Actual or explicitly planned PHYSICAL capture format, independent of
    // the modem's logical Fs. Hardware/provider negotiation can be unknown.
    std::optional<std::uint32_t> helper_forecast_capture_sample_rate;
    std::optional<double> helper_forecast_capture_passband_hz;
    bool helper_forecast_fixed_nominal_rate_conversion=false;
    std::optional<std::uint32_t> helper_forecast_capture_logical_sample_rate;
    bool helper_forecast_plan_sampled_capture=false;
    std::string helper_forecast_capture_description;
};
struct Point {
    double target_db_hz=0;
    double bit_seconds=0;
    double observer_ratio=0;
    bool observer_available=false;
};
struct ReceivePoint {
    double target_db_hz=0;
    double success_probability=0;
    bool confidence_available=false;
    bool reference_available=false;
    bool probability_reference_only=false;
    bool clock_supported=false;
    bool workspace_supported=false;
    std::size_t probability_trials=0;
};
struct CpuPoint {
    double target_db_hz=0;
    double realtime_ratio=0;
    bool available=false;
};
// Prices inherited from the ordinary receiver estimator, not measured total
// application CPU. The finite components are disjoint; epoch setup diagnostics
// below are already included in frontend/search and must not be added again.
struct HelperReceiverCostLedger {
    bool finite_available=false,sustained_available=false,workspace_supported=false;
    double finite_cpu_seconds=0,finite_audio_seconds=0,finite_realtime_ratio=0;
    double frontend_seconds=0,search_seconds=0,kernel_seconds=0;
    double continuation_seconds=0,payload_seconds=0,startup_other_seconds=0;
    double sustained_realtime_ratio=0,sustained_continuation_ratio=0;
    double initial_epoch_setup_seconds=0,new_epoch_setup_seconds=0,new_epoch_frontend_seconds=0;
    double initial_epochs=0,new_epoch_admissions=0,cohort_lifetime_seconds=0,resident_instances=0;
    simulation::ReceiverSearchDiagnostics geometry;
    // Separate additive terms. Missing calibration stays missing, never zero.
    // New manual-reference mode has no parallel broad fallback or audit bank.
    std::optional<double> helper_startup_cpu_seconds,helper_sustained_realtime_ratio;
    std::optional<double> post_lock_lead_receiver_cpu_seconds;
};
// Unsupported component pricing cannot be promoted to a complete finite or
// sustained total. Workspace eligibility stays a separate model diagnostic.
HelperReceiverCostLedger helper_receiver_cost(const simulation::Estimate&);
// Noise-only Cramer--Rao reference for an identified uninterrupted constant
// tone in white noise. It is neither an operational confidence interval nor a
// substitute for the waveform/model-aware forecast or measured qualification.
std::optional<double> helper_ideal_tone_half_width(double cn0_db_hz,double observation_seconds) noexcept;
struct HelperPrecisionReference {
    double cn0_db_hz=0,phase_noise_radians_per_sqrt_second=0;
    // An acceptance guard, not a physical oscillator-drift prior.
    double maximum_tracking_slew_hz_per_second=0,maximum_source_capture_ratio=1;
    std::optional<double> ideal_one_second_half_width_hz,ideal_lead_half_width_hz;
    double ideal_lead_observation_seconds=0;
};
// One ordinary receiver work query at an explicitly conditional residual U.
// This is receiver-model pricing, not measured total helper/application CPU.
struct RealtimeHelperReceiverWork {
    double realtime_ratio=0,uncertainty_hz=0,rf_half_width_hz=0,clock_half_width_ppm=0;
    std::size_t frequency_rate_hypotheses=0,retained_clock_rates=0;
    std::optional<modem::PatternHelperClockInterval> shared_frequency_clock;
    std::size_t shared_clock_selected_pairs=0,shared_clock_selected_rates=0;
    bool available=false;
    HelperReceiverCostLedger receiver_cost;
    std::string reason;
};
struct RealtimeHelperCpuPoint : RealtimeHelperReceiverWork {
    double target_db_hz=0,observation_seconds=0,minimum_seed_seconds=0;
    std::uint32_t physical_sample_rate=0;
    helper::TonePrecisionPrediction precision;
    HelperPrecisionReference precision_reference;
    double planned_lead_seconds=0,planned_tail_seconds=0;
    // Continued reference reception can improve U after payload starts. This
    // separate conditional query never changes the selected helper-only lead.
    std::optional<RealtimeHelperReceiverWork> sustained;
    double sustained_at_seconds=0; // Source time since helper start, not extra TX delay.
};
struct HelperCpuPoint {
    double target_db_hz=0,discovery_cpu_seconds=0,prequalification_broad_cpu_seconds=0;
    double qualified_finite_receiver_cpu_seconds=0,qualified_sustained_realtime_ratio=0;
    double ordinary_tracking_realtime_ratio=0,additional_frontend_realtime_ratio=0,optional_probe_realtime_ratio=0;
    bool scenario_available=false,discovery_available=false,sustained_available=false,forecast_available=false;
    bool backend_sustained_available=false;
    double backend_sustained_realtime_ratio=0;
    simulation::HelperStrongRequirements requirements;
    simulation::HelperMaskedFrequencyRequirements masked_requirements;
    std::optional<simulation::HelperJointClockMetadata> executed_joint_geometry;
    std::string reason;
};
// A scheduling/compute overlay carrying the UNCHANGED ordinary PHY point.
// It is not a helper sensitivity/probability estimate. Backend fit is only
// necessary until reference, frontend and bounded-probe costs are calibrated.
struct HelperBackendFeasibilityPoint {
    double target_db_hz=0,ordinary_success_probability=0;
    double backend_realtime_ratio=0,cpu_budget=0;
    bool ordinary_phy_available=false,backend_feasibility_known=false,backend_fits_budget=false;
};
HelperBackendFeasibilityPoint helper_backend_feasibility(const ReceivePoint&,const HelperCpuPoint&,double cpu_budget);
struct HelperWorkPoint {
    double target_db_hz=0;
    simulation::HelperNativeWork native;
    // Requirement forecast never means synchronization was observed.
    bool measured_reference=false,native_probe_attempted=false;
    double expected_helper_cn0_db_hz=std::numeric_limits<double>::quiet_NaN(),minimum_observation_seconds=0;
};
struct HelperReferencePlan {
    bool enabled=false;
    bool budget_available=false;
    // Nominal placement only. The audio device/radio passband is not known to
    // the planner, and a received tone still needs measured qualification.
    bool geometry_available=false;
    bool tone_level_sufficient=false;
    double effective_power_w=0;
    double received_dbm=0;
    double cn0_db_hz=0;
    double tone_snr_db=0; // The presence tracker's 10 Hz resolution cell.
    // Sampled simulation retains the source waveform's existing amplitude
    // backoff against its nominal-power noise model. Hardware watts above
    // instead describe the operator's calibrated transmit-power budget.
    bool simulation_level_modeled=false;
    double simulation_level_db=0;
    double simulation_data_cn0_db_hz=0;
    double simulation_cn0_db_hz=0;
    double simulation_tone_snr_db=0;
    double frequency_hz=0;
    RealtimeHelperCpuPoint conditional;
    // Original-grid geometry under an explicit hypothetical qualified helper
    // measurement. This does not certify runtime receiver/profile/resources.
    bool conditional_geometry_available=false;
    std::size_t ordinary_pairs=0,conditional_pairs=0,retained_clock_rates=0;
    // Unique RF catalogue offsets. Only resolved coherent pattern compact
    // geometry shares one physical projection per offset across LF lanes.
    std::size_t ordinary_carriers=0,conditional_carriers=0;
    bool carrier_projection_counts_available=false;
    bool compact_geometry=false,baseline_search_limited=false;
    double ordinary_rf_half_width_hz=0,requested_rf_half_width_hz=0;
    // Signed pre-conversion support. Real mono capture may fold its negative
    // image; all surviving signed branches must remain eligible at runtime.
    double helper_search_lower_hz=0,helper_search_upper_hz=0;
    double payload_search_lower_hz=0,payload_search_upper_hz=0;
    bool coarse_conversion_required=false;
    // Earliest optional-probe eligibility, not an audit-completion deadline.
    // One cold global slot rotates current key/profile/epoch ranks. Its
    // original RF/LF work is partial future coverage of the cohort.
    double broad_probe_eligibility_seconds=0,physical_symbol_seconds=0;
    double punctuation_period_seconds=.4,arrival_cell_seconds=0;
    std::string timing_status;
    double assumed_frequency_uncertainty_hz=.01;
    double assumed_helper_offset_hz=0;
    double original_clock_cell_half_width_ppm=0;
    std::string status;
};
struct Model {
    Inputs inputs;
    std::vector<Point> points;
    // One-bit reception at the unchanged link budget. Missing confidence is a
    // graph gap, never a prediction of zero; intermediate values interpolate
    // bounded statistical samples, with extra samples near the transition.
    std::vector<ReceivePoint> receive_points;
    // Receiver CPU time per second of one-bit simulated audio. Uses the same
    // checked target sweep; unsupported Clock/RAM geometry leaves graph gaps.
    std::vector<CpuPoint> cpu_points;
    // Ordinary receiver primitive-cost model after a conditional synchronous
    // helper frequency correction. Includes the full residual constructor/LF
    // catalogue, excludes separately uncalibrated reference/front-end CPU.
    std::vector<RealtimeHelperCpuPoint> helper_reference_cpu_points;
    // Separate conditional curves. Unsupported residual/backend/forecast
    // coverage stays a gap; ordinary curves above are never overwritten.
    std::vector<ReceivePoint> helper_receive_points;
    std::vector<HelperCpuPoint> helper_cpu_points;
    std::vector<HelperBackendFeasibilityPoint> helper_backend_feasibility_points;
    std::vector<HelperWorkPoint> helper_work_points;
    bool available=false;
    // A valid background request; a previous completed view may remain visible.
    bool calculating=false;
    bool curves_calculating=false; // Selected values complete; full target sweep pending.
    bool automatic_mode=true;
    bool observer_available=false;
    bool observer_hypothetical=true;
    bool shaped_band=false;
    bool clock_search_supported=false;
    bool receiver_workspace_supported=false;
    bool confidence_available=false;
    bool one_bit_confidence_available=false;
    bool probability_reference_only=false;
    bool reference_probability_available=false;
    bool one_bit_reference_available=false;
    double bit_seconds=0;
    double send_seconds=0;
    bool scheduled_duration_representative=false;
    // Earliest modeled finish: whole burst plus fully scored absent symbols.
    double finish_seconds=0;
    double observer_ratio=0;
    double received_dbm=0;
    double actual_cn0_db_hz=0;
    double margin_db=0;
    // Rough, conditional raw wire-bit preview at the actual link budget;
    // Requires confidence_available or reference_probability_available, with
    // their distinct scopes, and completed receiver computation.
    // No interval FEC/source context is available; the full draft is separate.
    double success_probability=0;
    double one_bit_success_probability=0;
    std::size_t probability_trials=0;
    double success_probability_low=0,success_probability_high=1;
    double one_bit_probability_low=0,one_bit_probability_high=1;
    bool probability_interval_available=false;
    bool probability_search_approximation=false;
    std::size_t probability_carrier_candidates=0;
    std::string probability_model_limit;
    double phase_coherence_loss_db=0;
    bool coherent_reference_only=false;
    bool drift_model_available=false;
    double coherent_success_probability=0;
    double section_phase_coherence_loss_db=0;
    std::uint64_t differential_windows=0;
    double differential_window_seconds=0;
    bool differential_model_available=false;
    double differential_added_detection_probability=0;
    // One exact bit plus its full waveform and absence processing, regardless
    // of draft length. The receiver ratio excludes synthetic channel creation
    // and divides receiver work by all received audio, including absence.
    bool one_bit_cpu_available=false;
    double one_bit_cpu_seconds=0;
    double receiver_cpu_seconds=0;
    bool kernel_rebuild_upper_bound=false;
    double cpu_realtime_ratio=0;
    double cpu_realtime_upper_ratio=0;
    double finite_cpu_realtime_ratio=0;
    simulation::SymbolBurstWorkDiagnostics symbol_burst;
    bool hardware_sustained_modeled=false;
    double hardware_sustained_realtime_ratio=0,hardware_cohort_lifetime_seconds=0;
    double hardware_sustained_receiver_instances=0;
    double cpu_per_bit_ratio=0;
    simulation::ReceiverSearchDiagnostics receiver_geometry;
    std::size_t frequency_rate_hypotheses=0,epoch_hypotheses=0;
    std::size_t timing_phase_groups=1;
    double timing_hypotheses=0;
    bool timing_window_modeled=false;
    double initial_fft_acquisition_batches=0,initial_fft_retained_acquisition_batches=0;
    double new_epoch_admissions=0,new_epoch_full_fft_batches=0,new_epoch_retained_fft_batches=0;
    double new_epoch_qualified_ready_batch_slots=0;
    std::string receiver_work_assumptions;
    // Separate auxiliary link budget. It changes neither the ordinary payload
    // target/margin nor the unassisted probability and CPU curves.
    HelperReferencePlan helper_reference;
    double fallback_cpu_realtime_ratio=0,fallback_timing_hypotheses=0;
    double occupied_bandwidth_hz=0;
    // Additional full nominal engineering rolloff allowance for smooth gates;
    // finite burst envelopes have tails beyond this scale.
    double gate_bandwidth_allowance_hz=0;
    double low_audio_hz=0;
    double high_audio_hz=0;
    std::string receiver_status;
    std::string clock_limit_reason;
    std::string error;
    std::optional<double> fast_target; // One bit per second, if reachable.
    std::optional<double> day_target; // One day per bit, if reachable.
    // A checked usable edge from the long-duration side: clock search AND RAM.
    // Sample rounding and short-profile policy can create other coverage gaps.
    std::optional<double> clock_target;
    // Checked useful steps, normally about 1 dB, skipping clock/RAM gaps.
    // A remaining end point may be nearer than 1 dB. Never a promise that all
    // intervening sample-quantized profiles fit or that every island is known.
    std::optional<double> stronger_fit_target;
    std::optional<double> weaker_fit_target;
};
struct TargetSteps {
    std::optional<double> stronger,weaker;
};
// Bounded estimator state survives short-lived GUI worker threads. Access is
// serialized internally; separate controllers retain independent caches.
class Cache {
public:
    struct Statistics {
        std::size_t entries=0,probability_evaluations=0,probability_reuses=0;
        std::size_t support_entries=0,support_evaluations=0,support_reuses=0;
    };
    Cache();
    ~Cache();
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend Model build(const Inputs&,Cache&,std::stop_token,const std::function<void(const Model&)>&,
                       std::optional<TargetSteps>);
};
// Bounded analytical planning only; no sampled audio or transmission occurs.
Model build(const Inputs& inputs,std::stop_token stop = {});
// The optional callback receives completed selected values before curve work.
// Called under the cache lock; it must not reenter this cache.
// Supplied steps must be the preview for this exact immutable input snapshot;
// a present pair with both directions empty is still a completed preview.
Model build(const Inputs& inputs,Cache& cache,std::stop_token stop = {},
            const std::function<void(const Model&)>& selected = {},
            std::optional<TargetSteps> supplied_steps = std::nullopt);
// Two exact +/-1 dB receiver-support checks, without probability calculations
// or the full gap search. Missing directions remain for the background planner
// to resolve. The caller retains this result for its immutable input snapshot.
TargetSteps preview_steps(const Inputs& inputs);
struct ReceiveBanks {
    bool plaintext=false;
    // Distinct key material, including the selected transmit key if present.
    std::size_t private_keys=0;
};
// Keep a usable request; otherwise prefer the nearest checked weaker target,
// falling back to a stronger fit. Companion targets share the receiver budget
// and are not changed. Omitted banks means one matching waveform family.
// An empty result means no fitting candidate was found.
std::optional<double> nearest_fit_target(const Inputs& inputs,
    std::span<const double> companion_targets={},std::optional<ReceiveBanks> banks=std::nullopt);
std::string duration(double seconds);
}
