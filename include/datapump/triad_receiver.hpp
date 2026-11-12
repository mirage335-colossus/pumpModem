#pragma once

#include "datapump/helper_carrier.hpp"
#include "datapump/triad_modem_v3.hpp"

#include <array>
#include <optional>
#include <stop_token>
#include <string>

namespace datapump::triad::v3 {

namespace receiver_detail { class ReceiverBulkBackend; }

// Automatic may choose Q15 products on a supported native AVX2 geometry.
// FP32 permits higher precision fallback. Explicit Q15 requires support.
enum class NativeCoarsePrecision { automatic, fp64, q15, fp32 };

inline constexpr std::array<std::int64_t, 13> receiver_epoch_offsets{
    -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6};
inline constexpr std::size_t receiver_coherence_count = 3;

// Public operational attempt/grid penalty shared by admission and local
// transmit duration planning. This is a conditional model bound.
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
    // Public noise normalization and absolute basis gain. Neither is inferred
    // from a decoded bit, a private sample realization, or the episode length.
    double noise_variance = 0, data_basis_gain = 1;
    NativeCoarsePrecision native_coarse_precision = NativeCoarsePrecision::automatic;
    helper::Plan helper;
    bool helper_enabled = false, physical_rf_retuning = false;
    // Optional compiled bulk arithmetic. The shared receiver still owns all
    // tracking state, original priors, physical decisions and worker permits.
    std::shared_ptr<receiver_detail::ReceiverBulkBackend> bulk_backend;
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
    std::array<double, 2> bit_evidence{}, adaptive_bit_evidence{}, combined_bit_evidence{};
    double quiet_energy = 0, quiet_threshold = 0;
    double quiet_noise_upper_tail_probability = .001;
    double operational_log_e = 0, operational_threshold = 0;
    double selected_prior_mass = 0;
    std::size_t quiet_modes = 0, workspace_peak_bytes = 0, fine_candidates = 0;
    std::size_t coarse_workspaces = 0, coarse_frequency_points = 0, coarse_arrival_points = 0;
    std::string coarse_fft_precision, coarse_precision_reason;
    std::size_t affine_blocks = 0, adaptive_blocks = 0, adaptive_peak_states = 0;
    std::uint64_t adaptive_transition_edges = 0, basis_rows = 0;
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
