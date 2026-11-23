#pragma once

#include "datapump/helper_carrier.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/wrong_key_admission.hpp"

#include <array>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace datapump::triad::v3 {

namespace receiver_detail { class ReceiverBulkBackend; }

// Automatic may choose Q15 products on a supported native AVX2 geometry.
// FP32 permits higher precision fallback. Explicit Q15 requires support.
enum class NativeCoarsePrecision { automatic, fp64, q15, fp32 };

inline constexpr std::array<std::int64_t, 13> receiver_epoch_offsets{
    -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6};
inline constexpr std::size_t receiver_coherence_count = 3;
inline constexpr std::size_t receiver_independent_gain_power_count = 4;
inline constexpr std::size_t receiver_signed_lag_gain_max_candidates = 4;

// Reference conditional bound for unbound slot scoring and duration planning.
inline constexpr std::uint32_t receiver_reference_opportunities_per_day = 216'001;
double receiver_operational_threshold();
double receiver_coarse_frequency_step(const Format&, double input_sample_rate_hz);
double receiver_log_grid_count(double arrival_lower_seconds, double arrival_upper_seconds,
                               double frequency_bound_hz, double clock_bound_ppm,
                               double frequency_step_hz = .01);

struct ReceiverConfig {
    double sample_rate_hz = 0;
    double data_if_hz = 0;
    // Total relative bounds, with the configured margin already applied.
    double frequency_bound_hz = 0, clock_bound_ppm = 0;
    double endpoint_clock_bound_ppm = 0;
    double spread_delay_seconds = 0, spread_doppler_hz = 0;
    double random_doppler_width_hz = 0, delay_motion_doppler_bound_hz = 0;
    double delay_excursion_seconds = 0, capture_tail_bound_seconds = 0;
    double relative_phase_diffusion_rad_sqrt_s = 0;
    double selective_modulation_rate_hz = 0, selective_slow_depth = 0, selective_fast_depth = 0;
    double source_spectral_skirt_allowance_hz = 0;
    // Add bounded proposals and a shorter-coherence receive model. Both
    // fields are public assumptions, never a realization from simulation.
    bool time_varying_channel = false;
    double time_varying_common_doppler_hz = 0;
    // Additional atmospheric offset outside the legacy static frequency
    // prior. Supplemental proposals use their sum without enlarging the
    // legacy exhaustive coherent grid.
    double time_varying_atmospheric_hz = 0;
    // Varying reception can erase a slot with insufficient separation of
    // the two bit scores even when the presence bound passes.
    double time_varying_minimum_bit_log_separation = 5.293304824724492; // log(199)
    // Public noise normalization and absolute basis gain. Neither is inferred
    // from a decoded bit, a private sample realization, or the episode length.
    double noise_variance = 0, data_basis_gain = 1;
    NativeCoarsePrecision native_coarse_precision = NativeCoarsePrecision::automatic;
    WrongKeyAdmission wrong_key_admission = WrongKeyAdmission::current;
    helper::Plan helper;
    bool helper_enabled = false, physical_rf_retuning = false;
    // Optional compiled bulk arithmetic. The shared receiver still owns all
    // tracking state, original priors, physical decisions and worker permits.
    std::shared_ptr<receiver_detail::ReceiverBulkBackend> bulk_backend;
};

class PublicProfile;
struct ReceiverSlotPlan;

class CalendarAdmissionContext {
public:
    // Zero means the slot has no locally configured calendar bound.
    std::uint32_t opportunities_per_day() const { return opportunities_per_day_; }
private:
    friend ReceiverSlotPlan make_receiver_slot_plan(const PublicProfile&, const ReceiverConfig&, std::uint64_t);
    std::uint32_t opportunities_per_day_ = 0;
};

struct ReceiverSlotPlan {
    std::uint64_t index = 0, first_sample = 0, stop_sample = 0;
    std::uint64_t quiet_first_sample = 0, quiet_stop_sample = 0;
    // The configured format is addressed at this public pattern epoch/ordinal.
    Format format;
    // The physical arrival lag prior relative to the nominal source origin.
    double arrival_lower_seconds = 0, arrival_upper_seconds = 0;
    // Source times are elapsed since the shared UTC calendar origin. Window
    // lead is the global capture lead less first_sample / sample_rate_hz.
    long double window_capture_lead_seconds = 0, source_start_seconds = 0;
    long double helper_begin_seconds = 0, helper_end_seconds = 0;
    CalendarAdmissionContext calendar_admission;
    // Optional packed DATA support. Includes all observed samples that
    // cannot contain a neighboring configured slot; quiet scoring uses the
    // separate physical aperture above.
    std::uint64_t protected_data_first_sample = 0, protected_data_stop_sample = 0;
    // Scheduled gap fill may enter the full capture outside protected DATA.
    // All proposal families start from that same protected input support.
    bool scheduled_gap_fill = false;
    // Proposal-only Scheduled DATA interior. Both endpoints are zero when
    // the declared geometry has no eligible carrier-beat aperture.
    std::uint64_t carrier_beat_first_sample = 0, carrier_beat_stop_sample = 0;
    // Local scorer shape selected for an admitted owner under memory pressure.
    bool memory_bounded_coarse = false;
};

enum class SlotDecision { ambiguous, data, quiet };

class ReceiverWorkspaceError : public Error {
public:
    using Error::Error;
};

struct ReceiverSlotScore {
    std::uint64_t index = 0, first_sample = 0, stop_sample = 0;
    std::uint64_t quiet_first_sample = 0, quiet_stop_sample = 0;
    std::uint64_t consumed_samples = 0;
    double sample_rate_hz = 0;
    SlotDecision decision = SlotDecision::ambiguous;
    std::optional<std::uint8_t> bit;
    bool complete = false, all_valid = false, zero_h0_verified = false;
    bool noise_statistics_valid = false, operational_passes_conditional_bound = false;
    bool adaptive_diagnostics = false;
    std::array<double, 2> bit_evidence{}, adaptive_bit_evidence{}, combined_bit_evidence{};
    // Added fixed-affine gain priors, or zeros outside their public support.
    std::array<double, 2> slow_gain_coherence_seconds{};
    // Both added priors combine a stationary and an OU gain on one template.
    bool affine_stationary_fading_gain = false;
    // Prior-corrected evidence actually used for operational bit selection.
    std::array<double, 2> operational_bit_evidence{};
    double operational_bit_log_separation = 0, operational_minimum_bit_log_separation = 0;
    WrongKeyAdmission wrong_key_admission = WrongKeyAdmission::current;
    double wrong_key_required_log_separation = 0;
    bool wrong_key_rejected = false;
    double quiet_energy = 0, quiet_threshold = 0;
    double quiet_noise_upper_tail_probability = .001;
    double operational_log_e = 0, operational_threshold = 0;
    std::uint32_t calendar_opportunities_per_day = 0;
    double selected_prior_mass = 0;
    std::size_t quiet_modes = 0, workspace_peak_bytes = 0, fine_candidates = 0;
    std::size_t coarse_workspaces = 0, coarse_frequency_points = 0, coarse_arrival_points = 0;
    double coarse_projected_rate_hz = 0;
    std::string coarse_fft_precision, coarse_precision_reason;
    std::size_t affine_blocks = 0, adaptive_blocks = 0, adaptive_peak_states = 0;
    std::uint64_t adaptive_transition_edges = 0, basis_rows = 0;
    std::size_t varying_candidates = 0, varying_blocks = 0, varying_block_rows = 0;
    std::uint64_t varying_basis_rows = 0;
    double varying_coarse_seconds = 0, varying_fine_seconds = 0;
    // --- Varying fine batch receipts (append-only block) ---
    // Lane-seconds of the projection, statistics and recursion phases, and
    // the idle seconds of the lanes that ran a leaf, from each lane's first
    // leaf to the end of that phase. The quote, blocks per batch and batch
    // count are geometry receipts. A slot's receipts cover its ordinary,
    // coherent-gain and line-conditioned varying stages: seconds and
    // batches add, and the quote and blocks per batch are the largest
    // stage's.
    double varying_projection_seconds = 0, varying_statistics_seconds = 0;
    double varying_recursion_seconds = 0, varying_wait_seconds = 0;
    std::size_t varying_workspace_bytes = 0, varying_batch_blocks = 0, varying_batches = 0;
    // --- end varying fine batch receipts ---
    std::size_t varying_timing_blocks = 0, varying_timing_frequencies = 0;
    std::string varying_timing_status;
    // The coarse search's raw changing-channel timing proposals, in their
    // selection order before fine-grid snapping. A diagnostic receipt that
    // lets equivalent coarse scorers be compared; the public cap is eight.
    struct TimingProposal {
        std::int64_t epoch_offset_seconds = 0;
        double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
    };
    // Best fixed-affine coordinates for the individual bits. Ordered stream
    // commit can transfer the admitted bit's coordinate as a nomination.
    std::array<std::optional<TimingProposal>, 2> affine_tracking{};
    bool affine_continuation_attempted = false, affine_continuation_admitted = false;
    // Supplemental proposal work on the neighbor-free observation.
    bool protected_discovery_attempted = false, protected_discovery_admitted = false;
    std::size_t protected_discovery_candidates = 0;
    double protected_discovery_coarse_seconds = 0;
    struct ProtectedCarrierBeat {
        std::int64_t epoch_offset_seconds = 0;
        double arrival_seconds = 0, clock_ppm = 0;
        std::uint8_t ordinary_bit = 0, predicted_bit = 0;
        std::array<std::array<double, 2>, 2> explained_fraction{};
        std::array<std::array<std::array<double, 2>, 2>, 2> held_out_fraction{};
        struct AffineSupport {
            double arrival_seconds, frequency_hz, clock_ppm, log_e;
        };
        std::optional<AffineSupport> affine_support;
    };
    std::optional<ProtectedCarrierBeat> protected_carrier_beat;
    std::size_t protected_carrier_beat_search_coordinates = 0;
    bool protected_carrier_beat_corrected_polarity = false;
    std::size_t varying_timing_proposal_count = 0;
    std::array<TimingProposal, 8> varying_timing_proposals{};
    std::string midpoint_timing_status, midpoint_timing_precision;
    std::size_t midpoint_timing_lags = 0, midpoint_timing_candidates = 0, midpoint_timing_workspace_bytes = 0;
    std::uint64_t midpoint_timing_basis_rows = 0, midpoint_timing_widened_inverses = 0, midpoint_timing_scored_lags = 0;
    std::array<double, 2> varying_bit_evidence{};
    std::string varying_status;
    // The varying statistics route and its executed arithmetic, as for the
    // signed-lag route below.
    std::string varying_fine_route, varying_fine_arithmetic;
    std::size_t varying_frequency_states = 0;
    // Component receipts before the fixed short-gain / frequency mixture.
    std::array<double, 2> fixed_varying_bit_evidence{};
    std::array<double, 2> tracked_frequency_bit_evidence{};
    double tracked_frequency_log_prior_adjustment = 0;
    std::size_t nominal_independent_gain_candidates = 0, nominal_independent_gain_frequency_states = 0;
    std::uint64_t nominal_independent_gain_basis_rows = 0;
    std::uint64_t independent_gain_emission_cells = 0;
    std::size_t signed_lag_gain_candidates = 0;
    std::uint64_t signed_lag_gain_basis_rows = 0;
    std::array<double, 2> signed_lag_gain_bit_evidence{};
    std::string signed_lag_gain_proposal_status;
    std::size_t signed_lag_gain_proposal_maps = 0, signed_lag_gain_proposal_fft_rows = 0;
    std::size_t signed_lag_gain_proposal_fixed_maps = 0;
    std::array<std::int64_t, receiver_signed_lag_gain_max_candidates> signed_lag_gain_proposal_lags{};
    std::array<std::array<double, 2>, receiver_signed_lag_gain_max_candidates> signed_lag_gain_map_bit_evidence{};
    std::size_t signed_lag_gain_proposal_frequency_points = 0, signed_lag_gain_proposal_projected_rows = 0;
    std::size_t signed_lag_gain_proposal_fixed_frequency_points = 0;
    std::string signed_lag_gain_proposal_frequency_status, nominal_independent_gain_frequency_status;
    std::size_t signed_lag_gain_proposal_available_blocks = 0, signed_lag_gain_proposal_sampled_blocks = 0;
    std::size_t signed_lag_gain_proposal_scored_blocks = 0, signed_lag_gain_proposal_workspace_bytes = 0;
    std::size_t signed_lag_gain_proposal_direct_scored_blocks = 0;
    std::uint64_t signed_lag_gain_proposal_fft_stages = 0, signed_lag_gain_proposal_score_rows = 0;
    std::uint64_t signed_lag_gain_proposal_fft_stage_bound = 0, signed_lag_gain_proposal_score_row_bound = 0;
    double signed_lag_gain_proposal_seconds = 0;
    // Fine-stage device routes. A route names where the stage ran and its
    // arithmetic, or the native route; empty when the stage did not run or
    // no family route reports it. Device FFT row-stages and those of the
    // host's FP64 frontier rescoring (whenever it ran) are counted apart
    // from the native fft_stages above. The lags rescored in FP64 and the
    // gap between the best two rescored lags describe a certified rank.
    std::string signed_lag_gain_proposal_route;
    std::uint64_t signed_lag_gain_proposal_device_fft_stages = 0;
    std::uint64_t signed_lag_gain_proposal_rescore_fft_stages = 0;
    std::size_t signed_lag_gain_proposal_rescored_lags = 0;
    double signed_lag_gain_proposal_top2_gap = 0;
    std::array<double, 2> nominal_independent_gain_bit_evidence{};
    std::array<std::array<double, 2>, receiver_independent_gain_power_count> nominal_independent_gain_power_bit_evidence{};
    std::array<double, 2> nominal_independent_fixed_centroid_bit_evidence{}, nominal_independent_moving_centroid_bit_evidence{};
    double nominal_independent_gain_log_prior_adjustment = 0;
    std::size_t coherent_gain_candidates = 0, coherent_gain_blocks = 0, coherent_gain_frequency_states = 0;
    std::array<double, 2> coherent_gain_bit_evidence{};
    double coherent_gain_seconds = 0;
    std::string coherent_gain_status;
    std::size_t varying_quiet_modes = 0;
    double varying_quiet_energy = 0, varying_quiet_threshold = 0;
    // Elapsed time of the varying quiet-aperture re-projection on the slot's lanes.
    double varying_quiet_seconds = 0;
    std::string varying_quiet_status;
    double varying_line_frequency_hz = 0, varying_line_log_evidence = 0;
    std::size_t drift_timing_atoms = 0, drift_slopes = 0, drift_frequency_bins = 0;
    std::size_t drift_relative_doppler_models = 0;
    // The drift proposal route, as for the signed-lag route above, and the
    // timing atom evaluations for which a device call published ridge
    // records or slope frontiers, including those that then fell back. The
    // retained digest hashes each sign's set of survivor timing atoms, each
    // with its exactly scored (bit, slope, bin) set, so routes that select
    // differently differ in it. The line fields are the same three for the
    // line-conditioned drift stage of a retained coherent line.
    std::string drift_proposal_route;
    std::size_t drift_device_atoms = 0;
    std::uint64_t drift_retained_digest = 0;
    std::string drift_line_proposal_route;
    std::size_t drift_line_device_atoms = 0;
    std::uint64_t drift_line_retained_digest = 0;
    unsigned drift_stage_workers = 0, drift_line_stage_workers = 0;
    unsigned drift_planned_stage_workers = 0, drift_line_planned_stage_workers = 0;
    std::uint64_t drift_coherent_timing_fft_rows = 0, drift_coherent_timing_fft_stages = 0;
    std::size_t drift_observation_rows = 0, drift_proposal_stride = 0;
    double drift_seconds = 0;
    std::array<double, 2> drift_bit_evidence{};
    std::string drift_status, drift_line_status;
    // --- Drift section timers (append-only block) ---
    // Lane-summed seconds of the drift stages over both relative-Doppler
    // signs, including a line-conditioned drift stage when it runs:
    // template render and products, ridge segment FFTs and power tables,
    // ridge record search and slope selection, slope dechirp/FFT/scan, and
    // exact statistics with their likelihoods. Each lane's wall time
    // counts; lanes overlap and may share hardware threads, so the sum can
    // exceed drift_seconds, which times only the ordinary stage.
    double drift_render_seconds = 0, drift_ridge_seconds = 0, drift_ridge_search_seconds = 0;
    double drift_slope_seconds = 0, drift_exact_seconds = 0;
    // --- end drift section timers ---
    double helper_seconds = 0, coarse_seconds = 0, fine_seconds = 0;
    // Helper acquisition ran beside coarse preparation: helper_seconds then
    // lies inside coarse_seconds instead of preceding it.
    bool helper_overlapped = false;
    std::string helper_status, reason;
};

// The whole slot's input lease is included. Each call owns independent DSP
// state. The stream controller supplies its admitted worker share and commits
// these receipts in public order.
std::size_t receiver_slot_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
// The lease that also lets Helper acquisition run beside coarse preparation.
// It is never below receiver_slot_workspace(), and equals it when no stage
// can overlap or another stage already needs as much. A slot scored with at
// least this limit overlaps; its decisions and evidence are identical.
std::size_t receiver_slot_overlap_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
ReceiverSlotScore score_receiver_slot(const ReceiverConfig&, const ReceiverSlotPlan&,
                                     const Crypto&, std::span<const Complex> iq,
                                     std::span<const std::uint8_t> valid, unsigned workers,
                                     std::size_t memory_limit_bytes, std::stop_token = {});

} // namespace datapump::triad::v3
