#pragma once

#include "datapump/triad_modem_v3.hpp"
#include "triad_bulk_peaks.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace datapump::triad::v3::receiver_detail {

enum class CoarseRoute { frequency_tiles, lag_groups };

inline constexpr std::array<std::string_view, 8> bulk_direct_fallback_reasons{
    "unsupported-or-abrupt-pulse", "insufficient-cache-oversampling", "native-origin-precision",
    "nonfinite-relative-map", "large-phase", "pinned-tile-limit", "relative-map-range", "other"
};

inline constexpr std::array<std::string_view, 6> bulk_matrix_fallback_reasons{
    "cpu-device", "fp64-stage", "frequency-tile-route", "unfavorable-shape",
    "workspace-limit", "device-capability"
};

inline constexpr std::array<std::string_view, 11> bulk_half_fft_fallback_reasons{
    "cpu-device", "fp64-stage", "lag-group-route", "unsupported-plan", "workspace-limit",
    "fp32-minimum", "unqualified-hardware", "unqualified-shape", "ill-conditioned-template",
    "plan-allocation", "allocator-limit"
};

// Receiver conditions that withhold template synthesis from a frequency-tile
// session: its route may become lag groups, a mapped profile or a varying
// passband reads the native columns, or the keyed description exceeds its
// cell bound or the native bank storage that it borrows.
enum class CoarseSynthesisWithheld : std::uint8_t {
    lag_group_route, mapped_profile, midpoint_gain, varying_passband, fine_cell_limit, description_storage
};

// Why a dense OpenCL coarse session handed its search to the session it
// wraps, one count per session. The withheld conditions come first, in
// their enum order; the rest are the dense session's own admission, and
// "unoffered" a session prepared with native columns and no stated reason.
// tools/triad_native.py validates receipts against these names.
inline constexpr std::array<std::string_view, 17> opencl_coarse_fallback_reasons{
    "lag-group-route", "mapped-profile", "midpoint-gain", "varying-passband", "fine-cell-limit",
    "description-storage", "device-templates-off", "few-arrivals", "one-ray-unqualified", "dense-cost",
    "keyed-chip-limit", "kernel-extent", "workspace-lease", "host-allocation", "device-allocation",
    "ill-conditioned-template", "unoffered"
};

// Fine-stage device families: the signed-lag gain direct rank of the coarse
// search, drift proposals and varying statistics of fine scoring. Their
// session interfaces follow CoarseBulk below.
enum class StageFamily : std::uint8_t { gain_rank, drift, varying };
inline constexpr std::array<StageFamily, 3> stage_families{
    StageFamily::gain_rank, StageFamily::drift, StageFamily::varying};
// Receipt names: vulkan_<receipt name>_<field>, reasons vulkan-<reason name>-<suffix>.
inline constexpr std::array<std::string_view, 3> stage_family_receipt_names{"gain_rank", "drift", "varying"};
inline constexpr std::array<std::string_view, 3> stage_family_reason_names{"gain-rank", "drift", "varying"};

// Why a family stage stayed native, in StageReason order. Policy declines
// come in the order the pure selection policy tests them, then the
// fallbacks of a call selected for the device, then the receiver's own
// reasons of one family. tools/triad_native.py validates receipts against
// these names; a backend prefixes them with its name and the family's.
inline constexpr std::array<std::string_view, 7> stage_policy_reasons{
    "native-requested", "fp64-requested", "host-scoring-forced", "shader-fp64-unavailable",
    "unsupported-shape", "unqualified-device", "unimplemented"};
inline constexpr std::array<std::string_view, 5> stage_fallback_reasons{
    "workspace", "unified-memory-envelope", "unsupported-transform", "nonfinite-input", "nonfinite-device"};
inline constexpr std::array<std::string_view, 9> gain_rank_stage_reasons{
    "verify-mode", "no-direct-rank", "no-unsampled-blocks", "audit-unavailable", "shift-runs", "shift-range",
    "band-violated", "frontier-overflow", "near-tie"};
inline constexpr std::array<std::string_view, 4> varying_stage_reasons{
    "diagnostic", "line", "coherent-gain", "projected-tracker"};
enum class StageReason : std::uint8_t {
    native_requested, fp64_requested, host_scoring_forced, shader_fp64_unavailable, unsupported_shape,
    unqualified_device, unimplemented,
    workspace, unified_memory_envelope, unsupported_transform, nonfinite_input, nonfinite_device,
    verify_mode, no_direct_rank, no_unsampled_blocks, audit_unavailable, shift_runs, shift_range,
    band_violated, frontier_overflow, near_tie,
    diagnostic, line, coherent_gain, projected_tracker
};
inline constexpr std::size_t stage_reason_count = stage_policy_reasons.size() + stage_fallback_reasons.size() +
    gain_rank_stage_reasons.size() + varying_stage_reasons.size();
static_assert(static_cast<std::size_t>(StageReason::workspace) == stage_policy_reasons.size() &&
              static_cast<std::size_t>(StageReason::verify_mode) ==
                  stage_policy_reasons.size() + stage_fallback_reasons.size() &&
              static_cast<std::size_t>(StageReason::diagnostic) ==
                  static_cast<std::size_t>(StageReason::verify_mode) + gain_rank_stage_reasons.size() &&
              static_cast<std::size_t>(StageReason::projected_tracker) + 1 == stage_reason_count);

constexpr std::string_view stage_reason_suffix(StageReason reason) noexcept {
    auto index = static_cast<std::size_t>(reason);
    if (index < stage_policy_reasons.size()) return stage_policy_reasons[index];
    index -= stage_policy_reasons.size();
    if (index < stage_fallback_reasons.size()) return stage_fallback_reasons[index];
    index -= stage_fallback_reasons.size();
    if (index < gain_rank_stage_reasons.size()) return gain_rank_stage_reasons[index];
    return varying_stage_reasons[index - gain_rank_stage_reasons.size()];
}
// The receiver's own reasons belong to one family.
constexpr bool stage_reason_applies(StageFamily family, StageReason reason) noexcept {
    if (reason >= StageReason::diagnostic) return family == StageFamily::varying;
    if (reason >= StageReason::verify_mode) return family == StageFamily::gain_rank;
    return true;
}
// Fallbacks follow a call selected for the device; the rest are declines.
constexpr bool stage_reason_is_fallback(StageReason reason) noexcept {
    return reason >= StageReason::workspace && reason <= StageReason::nonfinite_device;
}

// How a stage that ran natively relates to the backend's sessions, for
// ReceiverBulkBackend::record_stage_native.
enum class StageNative : std::uint8_t {
    // Kept native before any factory call: a planned and declined call.
    decline,
    // Selected for the device but kept native before any factory call, such
    // as session host bytes outside the preserved fine envelope: a planned
    // call and a native fallback.
    fallback,
    // A call of an admitted session that ran natively, such as a failed host
    // check of device results: a native fallback of the planned call that
    // its factory already counted.
    session_fallback
};

// One family's device stage in a backend receipt. A planned call is a stage
// offered to the backend: a factory call, or a stage the receiver kept
// native before calling one, whether declined or as a fallback. A declined
// call is one the selection policy or the receiver kept native. A native
// fallback is a call selected for the device that ran natively instead: a
// refused lease, an unplannable transform, nonfinite values or a failed
// host check of device results. Every planned call is a decline, a session
// or a fallback before any session, so declines plus sessions never exceed
// planned calls, and planned calls never exceed declines, sessions and
// fallbacks together. Sessions and device work are credited after
// validated readback. The first reason of each kind is kept.
struct StageDeviceReceipt {
    std::uint64_t sessions = 0, forward_ffts = 0, inverse_ffts = 0, fft_row_stages = 0, score_rows = 0;
    std::uint64_t planned_calls = 0, declined_calls = 0, native_fallbacks = 0, upload_bytes = 0;
    std::string decline_reason, fallback_reason;
    // The device class on which automatic selection admits this family.
    bool qualified_device = false;
};

struct BulkReceipt {
    std::string device, device_name, library_version, precision, requested_coarse_math;
    std::optional<std::array<std::uint8_t,16>> device_uuid;
    std::string selection_reason;
    std::string native_fine_fallback_reason;
    std::string workspace_scope = "backend_shared";
    int cuda_runtime_version = 0, cuda_driver_version = 0;
    int cuda_blas_version = 0, cuda_compute_major = 0, cuda_compute_minor = 0;
    int cuda_fft_version = 0;
    std::uint32_t vulkan_api_version = 0, vulkan_driver_version = 0;
    std::uint32_t vulkan_vendor_id = 0, vulkan_device_id = 0, vulkan_subgroup_size = 0;
    bool vulkan = false, unified_memory = false, vulkan_float16 = false, vulkan_int8 = false;
    bool vulkan_integer_dot8_accelerated = false;
    unsigned numerical_threads = 1;
    std::size_t workspace_limit_bytes = 0, reserved_bytes = 0, peak_reserved_bytes = 0;
    std::size_t allocator_live_bytes = 0, allocator_reserved_bytes = 0;
    std::size_t allocator_peak_live_bytes = 0, allocator_peak_reserved_bytes = 0;
    std::size_t total_device_bytes = 0, initial_free_device_bytes = 0;
    std::size_t runtime_headroom_bytes = 0;
    std::size_t device_admission_waiters = 0;
    std::size_t host_cache_limit_bytes = 0, host_cache_bytes = 0, host_cache_entries = 0;
    std::size_t fft_plan_cache_count = 0, fft_plan_cache_limit = 0;
    std::uint64_t upload_bytes = 0, download_bytes = 0, synchronizations = 0;
    std::uint64_t fft_calls = 0, fft_rows = 0, fir_input_rows = 0, fir_output_contributions = 0;
    std::uint64_t score_rows = 0, statistics_blocks = 0, projected_values = 0, dc_reduction_values = 0;
    // Exact CUDA arrival selection, credited after validated frontier readback.
    std::uint64_t cuda_arrival_peak_rows = 0, cuda_arrival_peak_values = 0;
    // Changing-channel projections have a separate public grid from the
    // coherent coarse stages accounted in score_rows.
    std::uint64_t cuda_segmented_sessions = 0, cuda_segmented_forward_ffts = 0;
    std::uint64_t cuda_segmented_inverse_ffts = 0, cuda_segmented_score_rows = 0;
    std::uint64_t cuda_segmented_native_fallbacks = 0;
    // The same grid on Vulkan: VkFFT FP32 transforms with FP32 twiddle
    // tables, FP64 ranks and FP32 tracking states. Sessions count workspace
    // calls that submitted device work, credited after validated readback.
    // Planned shapes are those offered to the sessions. A declined call is a
    // shape the selection policy left native (requested, unqualified device
    // or shape, missing FP64 shaders); a native fallback is a call the device
    // was selected for but could not score (workspace, unsupported transform
    // configuration, nonfinite block energy). The first reason of each is kept.
    std::uint64_t vulkan_segmented_sessions = 0, vulkan_segmented_forward_ffts = 0;
    std::uint64_t vulkan_segmented_inverse_ffts = 0, vulkan_segmented_score_rows = 0;
    std::uint64_t vulkan_segmented_planned_shapes = 0, vulkan_segmented_declined_calls = 0;
    std::uint64_t vulkan_segmented_native_fallbacks = 0;
    std::string vulkan_segmented_decline_reason, vulkan_segmented_fallback_reason;
    // The device class on which automatic selection admits qualified shapes.
    bool vulkan_segmented_qualified_device = false;
    // Fine-stage device families on Vulkan, vulkan_<family>_<field> in a
    // receipt. Family extras: the signed-lag frontier and FP64-rescored lags
    // with the host rescoring time (sums over certifications), the largest
    // rescored error over its lag band (the maximum over certifications) and
    // the band source ("audit" or "fp32-floor"). The source is the band term
    // of the first recorded certification's bit with the larger band; it
    // does not identify the lag band behind the ratio. Then drift ridge
    // cells and dechirped slope transforms; varying blocks and batches.
    StageDeviceReceipt vulkan_gain_rank, vulkan_drift, vulkan_varying;
    std::uint64_t vulkan_gain_rank_frontier_lags = 0, vulkan_gain_rank_rescored_lags = 0;
    double vulkan_gain_rank_rescore_seconds = 0, vulkan_gain_rank_band_ratio = 0;
    std::string vulkan_gain_rank_band_source;
    std::uint64_t vulkan_drift_ridge_cells = 0, vulkan_drift_slope_ffts = 0;
    std::uint64_t vulkan_varying_blocks = 0, vulkan_varying_batches = 0;
    // Physical cuFFT transforms include padded rows. Score rows count only
    // validated results on the public grid, with no padded hypotheses.
    std::uint64_t cuda_half_fft_forward_ffts = 0, cuda_half_fft_inverse_ffts = 0;
    std::uint64_t cuda_half_fft_padded_ffts = 0, cuda_half_fft_score_rows = 0;
    std::size_t cuda_half_fft_peak_bank_bytes = 0, cuda_half_fft_peak_workspace_bytes = 0;
    std::size_t cuda_half_fft_peak_phase_bytes = 0;
    double cuda_half_fft_plan_seconds = 0;
    std::array<std::uint64_t, bulk_half_fft_fallback_reasons.size()> half_fft_fallback_counts{};
    std::uint64_t vulkan_dispatches = 0, vulkan_arrival_peak_rows = 0, vulkan_arrival_peak_values = 0;
    // Sum of completed command-buffer intervals on the device clock.
    // Intervals may overlap across owners and do not measure GPU utilization.
    std::uint64_t vulkan_timed_submissions = 0;
    double vulkan_device_command_seconds = 0;
    std::uint64_t vulkan_host_scored_rows = 0;
    std::uint64_t vulkan_fp32_score_rows = 0, vulkan_fp16_storage_score_rows = 0;
    std::uint64_t vulkan_direct_dot_terms = 0;
    std::uint64_t vulkan_direct_fp32_score_rows = 0, vulkan_direct_fp16_score_rows = 0;
    std::uint64_t vulkan_direct_fp16_storage_score_rows = 0;
    double vulkan_direct_dot_seconds = 0;
    std::uint64_t vulkan_peak_owner_bytes = 0, vulkan_active_sessions = 0;
    std::size_t vulkan_shared_workspace_limit_bytes = 0, vulkan_leased_bytes = 0;
    std::size_t vulkan_peak_leased_bytes = 0, vulkan_peak_owner_lease_bytes = 0;
    std::uint64_t vulkan_waiting_sessions = 0, native_coarse_workspace_sessions = 0;
    std::uint64_t vulkan_fft_cache_hits = 0, vulkan_fft_cache_misses = 0;
    std::uint64_t vulkan_shader_spirv_compiles = 0, vulkan_shader_spirv_cache_hits = 0;
    double vulkan_fft_plan_seconds = 0, vulkan_shader_compile_seconds = 0;
    std::uint64_t native_coarse_lag_sessions = 0, native_coarse_precision_sessions = 0, native_fine_sessions = 0;
    std::uint64_t mapped_columns = 0, mapped_values = 0, uploaded_tiles = 0, tile_hits = 0;
    // CUDA mapped work credited after the batch's blocking statistics readback.
    std::uint64_t cuda_fused_mapped_launches = 0, cuda_fused_mapped_values = 0;
    // Fine template modes consumed by the receiver after synchronized NVFP4 work.
    std::uint64_t cuda_nvfp4_fine_batches = 0, cuda_nvfp4_fine_modes = 0;
    std::uint64_t cuda_nvfp4_fine_workspace_fallbacks = 0, cuda_nvfp4_fine_algorithm_fallbacks = 0;
    std::uint64_t cuda_cufft_fine_modes = 0;
    std::uint64_t opencl_nvfp4_fine_batches = 0, opencl_nvfp4_fine_modes = 0;
    std::uint64_t opencl_nvfp4_pinned_batches = 0, opencl_nvfp4_native_blocks = 0;
    std::uint64_t opencl_resident_fine_batches = 0, opencl_resident_fine_pairs = 0;
    std::uint64_t opencl_resident_nvfp4_modes = 0, opencl_resident_fft_modes = 0;
    std::uint64_t opencl_resident_upload_bytes = 0, opencl_resident_download_bytes = 0;
    std::uint64_t opencl_resident_direct_columns = 0;
    std::uint64_t opencl_resident_dispatches = 0, opencl_resident_blocking_writes = 0;
    // Writes from staging that stays unchanged until the batch's statistics
    // read, which the host does not wait for.
    std::uint64_t opencl_resident_async_writes = 0;
    std::uint64_t opencl_resident_mapping_dispatches = 0;
    // Public blocks of all resident batches, dense source tiles rendered on
    // the device from keyed coefficients, and the keyed lanes uploaded.
    std::uint64_t opencl_resident_fine_blocks = 0, opencl_resident_rendered_tiles = 0;
    std::uint64_t opencl_resident_keyed_lanes = 0;
    // The most public blocks one resident batch held, and the resident calls
    // that ran in smaller batches than their session admitted because the
    // device lease for the whole window was refused.
    std::uint64_t opencl_resident_window_blocks = 0, opencl_resident_reduced_windows = 0;
    // Resident columns mapped and dense tiles rendered by the FP32 kernels,
    // with fixed-point coordinates and phase and FP32 interpolation, sums and
    // rotation, and by the FP64 control kernels.
    std::uint64_t opencl_resident_fp32_mapped_columns = 0, opencl_resident_fp64_mapped_columns = 0;
    std::uint64_t opencl_resident_fp32_rendered_tiles = 0, opencl_resident_fp64_rendered_tiles = 0;
    // Present for a profiled resident session. Host spans are elapsed on the
    // owning thread: the mapping source without its sink, the sink's staging,
    // transfers and enqueues, and the wait for the statistics. Device values
    // sum command intervals on the device clock. None is utilization.
    double opencl_resident_host_source_seconds = 0, opencl_resident_host_staging_seconds = 0;
    double opencl_resident_host_transfer_seconds = 0, opencl_resident_host_wait_seconds = 0;
    double opencl_resident_device_transfer_seconds = 0, opencl_resident_device_render_seconds = 0;
    double opencl_resident_device_map_seconds = 0, opencl_resident_device_pack_seconds = 0;
    double opencl_resident_device_product_seconds = 0, opencl_resident_device_statistics_seconds = 0;
    // First command start to last command end of each batch, with idle gaps.
    double opencl_resident_device_span_seconds = 0;
    // Dense FP4 coarse scores, credited after the frontier readback. Issued
    // products include padding. Seconds are summed device command intervals
    // of a profiled session, not wall time or utilization.
    std::uint64_t opencl_coarse_score_rows = 0, opencl_coarse_rescored_peaks = 0;
    std::uint64_t opencl_coarse_issued_products = 0, opencl_coarse_useful_products = 0;
    std::uint64_t opencl_coarse_synthesized_columns = 0, opencl_coarse_uploaded_columns = 0;
    std::uint64_t opencl_coarse_upload_bytes = 0, opencl_coarse_download_bytes = 0;
    std::uint64_t opencl_coarse_dispatches = 0, opencl_coarse_blocking_transfers = 0;
    // Fallbacks are the sessions handed to the wrapped session, in total and
    // by opencl_coarse_fallback_reasons. The counts sum to the total.
    std::uint64_t opencl_coarse_sessions = 0, opencl_coarse_frequency_fallbacks = 0;
    std::array<std::uint64_t, opencl_coarse_fallback_reasons.size()> opencl_coarse_fallback_counts{};
    // Retained arrivals rescored with FP32 partial sums of 64 rows added in
    // FP64, and with every product accumulated in FP64, the matched control.
    std::uint64_t opencl_coarse_rescore_fp32_partial_peaks = 0, opencl_coarse_rescore_fp64_peaks = 0;
    double opencl_coarse_product_seconds = 0, opencl_coarse_packing_seconds = 0;
    double opencl_coarse_selection_seconds = 0, opencl_coarse_synthesis_seconds = 0;
    // Parts of the synthesis and selection intervals: lane rendering, raw
    // warping with the decimating FIR, the bank Gram, and the observation
    // rotation with the rescoring of retained arrivals.
    double opencl_coarse_render_seconds = 0, opencl_coarse_filter_seconds = 0;
    double opencl_coarse_gram_seconds = 0, opencl_coarse_rescore_seconds = 0;
    // CUDA Gram elements credited after the same readback.
    std::uint64_t cuda_direct_gram_values = 0, cuda_bmm_gram_values = 0;
    // Post-channelization matrix work, credited only after synchronization.
    std::uint64_t cuda_matrix_fp16_calls = 0, cuda_matrix_int8_calls = 0;
    // cuBLASLt algorithm implementation declarations. Legacy GemmEx provides
    // no selected-algorithm witness. None of these are hardware counters.
    std::uint64_t cuda_matrix_lt_hmma_calls = 0, cuda_matrix_lt_imma_calls = 0, cuda_matrix_unwitnessed_calls = 0;
    std::uint64_t cuda_matrix_inner_chunks = 0, cuda_matrix_padded_real_macs = 0;
    std::uint64_t cuda_matrix_packed_values = 0, cuda_matrix_output_values = 0;
    // Cumulative prepared right operands (including padding), and successful
    // synchronized group contractions using them. These are not peak bytes.
    std::uint64_t cuda_matrix_prepared_b_groups = 0, cuda_matrix_prepared_b_bytes = 0;
    std::uint64_t cuda_matrix_prepared_b_values = 0, cuda_matrix_prepared_b_uses = 0;
    std::uint64_t cuda_matrix_maximum_rows = 0, cuda_matrix_maximum_columns = 0, cuda_matrix_maximum_inner = 0;
    std::uint64_t cuda_matrix_nonfinite_values = 0, cuda_matrix_saturated_values = 0;
    double cuda_matrix_maximum_a_scale = 0, cuda_matrix_maximum_b_scale = 0;
    std::array<std::uint64_t, bulk_matrix_fallback_reasons.size()> matrix_fft_fallback_counts{};
    std::uint64_t projection_index_cache_hits = 0, projection_index_cache_uploads = 0;
    std::uint64_t mapping_offset_cache_hits = 0, mapping_offset_cache_uploads = 0;
    std::uint64_t direct_columns = 0, direct_values = 0;
    std::array<std::uint64_t, bulk_direct_fallback_reasons.size()> direct_fallback_counts{};
    double wall_seconds = 0;
    bool cuda = false, executed = false;
};

inline StageDeviceReceipt& vulkan_stage_receipt(BulkReceipt& receipt, StageFamily family) noexcept {
    return family == StageFamily::gain_rank ? receipt.vulkan_gain_rank
         : family == StageFamily::drift ? receipt.vulkan_drift : receipt.vulkan_varying;
}
inline const StageDeviceReceipt& vulkan_stage_receipt(const BulkReceipt& receipt, StageFamily family) noexcept {
    return vulkan_stage_receipt(const_cast<BulkReceipt&>(receipt), family);
}
// A session's own fine-stage work over a copy of the backend receipt,
// keeping the backend's qualified-device flags. An empty source clears
// the work from a session receipt that did none.
inline void copy_stage_work(BulkReceipt& to, const BulkReceipt& from) {
    for (const auto family : stage_families) {
        auto& stage = vulkan_stage_receipt(to, family);
        const bool qualified = stage.qualified_device;
        stage = vulkan_stage_receipt(from, family);
        stage.qualified_device = qualified;
    }
    to.vulkan_gain_rank_frontier_lags = from.vulkan_gain_rank_frontier_lags;
    to.vulkan_gain_rank_rescored_lags = from.vulkan_gain_rank_rescored_lags;
    to.vulkan_gain_rank_rescore_seconds = from.vulkan_gain_rank_rescore_seconds;
    to.vulkan_gain_rank_band_ratio = from.vulkan_gain_rank_band_ratio;
    to.vulkan_gain_rank_band_source = from.vulkan_gain_rank_band_source;
    to.vulkan_drift_ridge_cells = from.vulkan_drift_ridge_cells;
    to.vulkan_drift_slope_ffts = from.vulkan_drift_slope_ffts;
    to.vulkan_varying_blocks = from.vulkan_varying_blocks;
    to.vulkan_varying_batches = from.vulkan_varying_batches;
}

struct BulkMappedTile {
    std::shared_ptr<const void> owner;
    std::span<const Complex> samples;
};

// Consecutive dense source tiles of one keyed lane. A lane is the receiver
// epoch index times two plus the source bit. The address is public: it
// follows from the candidate map and the calendar alone.
struct BulkTileAddress {
    std::uint64_t first = 0;
    std::uint32_t lane = 0, count = 0;
};

struct BulkMappedColumn {
    double fraction = 0, scale = 1, phase = 0, phase_step = 0;
    std::int64_t origin = 0;
    std::size_t output_column = 0;
    // Native immutable leases. Empty for a consumer that renders the
    // addressed tiles itself from the slot's FineTileSource.
    std::vector<BulkMappedTile> tiles;
    BulkTileAddress address;
};

// The public description of a slot's dense keyed DATA source, for a backend
// that renders its tiles. Dense sample d of a lane is the keyed waveform at
// source sample d * sample_step, scaled by gain, and exactly zero from
// dense_samples on. The waveform fields follow CoarseTemplateSource. Keyed
// coefficients are the only keyed input: no rendered sample crosses the
// host boundary.
struct FineTileSource {
    double sample_step = 1, gain = 1;
    double calendar_origin_sample = 0, source_samples = 0, sample_rate_hz = 0;
    double fine_chip_rate_hz = 0, center_offset = 0, carrier_spacing_hz = 0;
    double bit_seconds = 0, envelope_edge_seconds = 0;
    // A zero period or dwell disables the burst or hop gate.
    double burst_period_tiles = 0, tile_seconds = 0, tile_count = 0;
    double dwell_seconds = 0, quiet_seconds = 0, ramp_seconds = 0;
    std::uint64_t dense_samples = 0;
    std::size_t tile_samples = 0, cells = 0, lanes = 0;
    std::span<const double> pulse; // shaping table, 256 per chip over [0, 8]
    // Fills one lane's [cell][carrier] coefficients on the calling thread.
    // The owner keeps the source alive for the session that renders from it.
    std::function<void(std::size_t lane, std::span<Complex> chips, std::stop_token)> key_lane;
};

struct BulkDirectColumn {
    std::size_t output_column = 0;
    std::span<const Complex> samples;
    std::string_view reason;
};

using BulkMappedSink = std::function<void(std::span<const double> offsets,
    std::span<const BulkMappedColumn>, std::span<const BulkDirectColumn>)>;

// The callback supplies native immutable tiles with the original mapping, or
// only their public addresses when its block names a FineTileSource. It may
// call the sink repeatedly with disjoint columns. Each tile lease and direct
// column remains valid until the synchronous sink returns.
using FineMappingSource = std::function<void(std::size_t candidate, std::size_t first_row,
    std::size_t row_count, const BulkMappedSink&)>;

struct FineBulkBlock {
    std::size_t rows = 0, taps = 0;
    std::span<const std::size_t> projection_indices;
    std::span<const Complex> projected_y;
    double variance = 0;
    // Automatic NVFP4 is qualified only after DATA isolation in this setup.
    bool isolated_varying_data = false;
};

struct FineBulkGeometry {
    std::size_t maximum_block_rows = 0, maximum_projection_modes = 0, taps = 0;
    unsigned workers = 0;
    bool native_avx2 = false;
    bool isolated_varying_data = false;
};

struct FineBulkStatistics {
    std::array<Complex, 6> correlations{};
    std::array<Complex, 12> gram{};
    bool nonzero_basis = false;
};

struct FineResidentBlock {
    FineBulkBlock shape;
    FineMappingSource source;
    std::span<FineBulkStatistics> output;
    // Set when the source addresses tiles of this description instead of
    // leasing native samples. Every block of one call names the same one.
    const FineTileSource* tiles = nullptr;
};

struct FineBulkWork {
    std::uint64_t statistics_blocks = 0, projected_values = 0;
    bool executed = false;
};

class FineBulk {
public:
    virtual ~FineBulk() = default;
    virtual void statistics(const FineBulkBlock&, std::size_t candidate_count,
                            const FineMappingSource&, std::span<FineBulkStatistics>,
                            std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
    virtual FineBulkWork completed_work() const {
        const auto result = receipt();
        return {result.statistics_blocks, result.projected_values, result.executed};
    }
    virtual bool supports_resident_statistics(const FineBulkBlock&) const noexcept { return false; }
    // The most public blocks one resident call accepts. More than the two
    // of one dispatch lets a caller whose maps do not depend on earlier
    // statistics submit a window, and still apply the results in order.
    virtual std::size_t resident_block_capacity() const noexcept { return 2; }
    // The most blocks the session will submit in one resident call, at most
    // resident_block_capacity(), stated before its first resident call. A
    // backend sizes its device workspace by it.
    virtual void admit_resident_blocks(std::size_t) noexcept {}
    // True when resident calls render the dense tiles of this public source
    // themselves, from addressed columns. It must not start device work.
    virtual bool renders_source_tiles(const FineTileSource&) const noexcept { return false; }
    // Up to resident_block_capacity() already prepared public blocks, in
    // order. Each source supplies a complete block per candidate, requested
    // once in ascending candidate order on the owning thread. Decline before
    // consuming sources or changing outputs/receipts; after starting, finish
    // every block or throw.
    virtual bool resident_statistics(std::span<const FineResidentBlock>, std::size_t,
                                     std::stop_token) { return false; }
    // A projection-only device can use the same native mapper and scheduler.
    // Input/output are [template][row/bin], with both bits and all taps.
    virtual std::size_t host_projection_capacity() const noexcept { return 0; }
    // The backend may append one exact C64 magnitude scale to each lane.
    virtual bool host_projection_stages_peaks() const noexcept { return false; }
    virtual bool begin_host_projection(const FineBulkBlock&, std::stop_token) { return false; }
    // Optional owner-held host storage. The urgent mapping leaves finish
    // before submission, and projection consumes the span before it returns.
    virtual std::span<std::complex<float>> host_projection_staging(std::size_t) noexcept { return {}; }
    virtual void project_host_templates(std::span<const std::complex<float>>,
        std::span<std::complex<float>>, std::stop_token) { throw Error("No host-mapped projection for this backend"); }
    virtual void complete_host_statistics(std::size_t, std::size_t) {
        throw Error("No host-mapped statistics for this backend");
    }
};

struct CoarseBulkWork {
    std::uint64_t forward_ffts = 0, inverse_ffts = 0, fft_rows = 0, fft_row_stages = 0;
    std::uint64_t fir_input_rows = 0, fir_output_contributions = 0, score_rows = 0;
    std::uint64_t workspace_frequency_pairs = 0, singleton_dot_terms = 0;
    bool operator==(const CoarseBulkWork&) const = default;
};

struct CoarseBulkCompletedWork {
    std::uint64_t score_rows = 0;
    bool executed = false;
};

class CoarseBulkFilter {
public:
    virtual ~CoarseBulkFilter() = default;
};

struct CoarseFirCrop {
    std::size_t input_rows = 0, chunk_rows = 0;
    // Output row i samples convolution at output_origin +
    // (first_output + i) * output_stride.
    std::size_t output_origin = 0, output_stride = 0, first_output = 0, output_rows = 0;
};

inline constexpr std::size_t coarse_fir_chunk_rows = 16'384;

// Calls for one crop supply contributing chunks in input order. Separate
// crops can fill concurrently, so their callbacks own private or immutable data.
using CoarseFirSource = std::function<void(std::size_t first_row, std::span<Complex>)>;

struct CoarseBulkWorkspace {
    std::array<std::span<const Complex>, 2> columns;
    double inverse_a = 0, inverse_d = 0;
    Complex inverse_b{};
};

struct CoarseZoomGroup {
    std::size_t first_frequency = 0, frequencies = 0;
    std::span<const Complex> input_phase;
    // The native chirp convolution kernel, before FFT. Empty for singletons.
    std::span<const Complex> convolution;
};

struct CoarseBulkGeometry {
    std::size_t taps = 0, kernel_rows = 0, correlation_fft_rows = 0, lag_count = 0, frequency_count = 0;
    double projected_sample_rate_hz = 0, variance = 0;
    std::size_t frequency_tile_width = 0;
    // Optional backend-specific allocation shape chosen for the full prior.
    std::size_t workspace_batch = 0, device_workspace_bytes = 0;
    // Canonical FP64 template-bank conditioning after projection.
    double minimum_eigenvalue_ratio = 1;
    std::size_t lag_workspace_tile_width = 0;
};

struct CoarseFrequencyPlan {
    std::size_t tile_width = 0, workspace_batch = 0, device_workspace_bytes = 0, host_workspace_bytes = 0;
    std::string_view native_fallback_reason;
};

// Public changing-channel frequency/lag geometry. The observation spectra
// have shape [block][phase][fft row], shared by both bit alternatives.
struct CoarseSegmentedShape {
    std::size_t kernel_rows = 0, arrivals = 0, blocks = 0, fft_rows = 0;
    std::size_t radius = 0, phases = 0, bin_stride = 0, tracking_hop = 0;
    bool operator==(const CoarseSegmentedShape&) const = default;
};

// A score view contains [workspace][frequency][arrival] in canonical order.
// The callback consumes it before the backend reuses host staging.
using CoarseScoreSink = std::function<void(std::size_t first_workspace, std::size_t workspace_count,
    std::span<const double>)>;

// Exact frontiers contain [workspace][frequency] in canonical order. All
// arrival scores still undergo the backend's finite-value validation and count
// toward completed work. The callback consumes this view before staging reuse.
using CoarsePeakSink = std::function<void(std::size_t first_workspace, std::size_t workspace_count,
    std::span<const CoarseBulkPeaks>)>;

// One template column of a keyed lane. Raw row i samples the lane's source
// at first_position + i * scale and rotates by first_cycles + i * cycle_step
// turns, before the canonical decimating FIR.
struct CoarseTemplateColumn {
    double first_position = 0, scale = 1, first_cycles = 0, cycle_step = 0;
    std::size_t lane = 0;
};

// The public description of the keyed DATA waveform and of the exact
// template route, for a backend that renders the bank itself. Each lane is
// rendered on source_rows integer receive samples and interpolated from
// there. Coefficients are the only keyed input: no rendered sample crosses
// the host boundary.
struct CoarseTemplateSource {
    double rate_ratio = 1, calendar_origin_sample = 0, source_samples = 0, sample_rate_hz = 0;
    double fine_chip_rate_hz = 0, center_offset = 0, carrier_spacing_hz = 0;
    double bit_seconds = 0, envelope_edge_seconds = 0;
    // A zero period or dwell disables the burst or hop gate.
    double burst_period_tiles = 0, tile_seconds = 0, tile_count = 0;
    double dwell_seconds = 0, quiet_seconds = 0, ramp_seconds = 0;
    std::size_t lanes = 0, cells = 0, source_rows = 0, raw_rows = 0, decimation = 0;
    std::span<const std::complex<float>> chips; // [lane][cell][carrier]
    std::span<const float> pulse;               // shaping table, 256 per chip over [0, 8]
    std::span<const double> taps;
    std::span<const CoarseTemplateColumn> columns; // [workspace][tap]
};

struct CoarseTemplateGram {
    double a = 0, d = 0;
    Complex b{};
};

class CoarseBulk {
public:
    virtual ~CoarseBulk() = default;
    virtual std::string_view precision_reason() const noexcept { return {}; }
    // True when this session can render, filter and retain the whole bank of
    // this frequency-tile geometry. It must not start device work.
    virtual bool supports_template_synthesis(const CoarseBulkGeometry&, std::size_t /*workspaces*/,
        const CoarseTemplateSource&) const noexcept { return false; }
    // Retains every template column and returns the FP64 Gram entries of
    // each workspace. The following prepare() then receives empty column
    // spans. False declines before any device state or work is retained,
    // and the caller prepares its native columns instead.
    virtual bool synthesize_templates(const CoarseBulkGeometry&, const CoarseTemplateSource&,
        std::span<CoarseTemplateGram>, CoarseBulkWork&, std::stop_token) { return false; }
    // The receiver condition that withholds template synthesis from this
    // session, stated before it prepares native columns. Reporting only.
    virtual void withhold_template_synthesis(CoarseSynthesisWithheld) noexcept {}
    // False keeps the canonical native FIR/channelizer before device upload.
    // It does not permit a fallback after any device operation has started.
    virtual bool supports_fir() const noexcept { return true; }
    // Supply the possible simultaneous FIR/segmented shapes before device
    // admission. A declined shape keeps the native projected scorer. The
    // receiver offers them to every session, with or without device FIR.
    virtual void plan_segmented(std::span<const CoarseSegmentedShape>) {}
    // Admits the device work of the planned shapes on the owning thread,
    // before the first segmented call and before any other stage of the
    // session holds device memory. A shape it cannot admit stays native.
    virtual void begin_segmented(std::stop_token) {}
    // Follows the last segmented call and releases what begin admitted.
    virtual void end_segmented() noexcept {}
    // False must leave outputs and operation counts untouched. A device
    // operation that has started either publishes all ranks or throws.
    // Workspaces call concurrently. Calls of one session that pass the same
    // observation span pass the same immutable values.
    virtual bool segmented_scores(const CoarseSegmentedShape&,
        std::span<const std::complex<float>>, std::span<const Complex>, double,
        std::span<double>, std::span<double>, CoarseBulkWork&, std::stop_token) { return false; }
    // Automatic backends can admit an entire FIR phase before it retains any
    // device buffers, avoiding waits while holding another phase's memory.
    virtual void begin_fir_projection(std::size_t, std::size_t, std::size_t, std::stop_token) {}
    virtual std::shared_ptr<const CoarseBulkFilter> prepare_filter(
        std::span<const double> taps, std::size_t fft_rows, CoarseBulkWork&, std::stop_token) = 0;
    virtual std::vector<Complex> project_fir(const CoarseBulkFilter&, const CoarseFirCrop&,
        const CoarseFirSource&, CoarseBulkWork&, std::stop_token) = 0;
    // Nonempty groups choose the lag/zoom route. Bank and observation are
    // copied to the device before this call returns.
    virtual void prepare(const CoarseBulkGeometry&, std::span<const Complex> observation,
        std::span<const CoarseBulkWorkspace>, std::span<const CoarseZoomGroup>,
        CoarseBulkWork&, std::stop_token) = 0;
    virtual void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink&,
        CoarseBulkWork&, std::stop_token) = 0;
    // Optional exact on-device arrival selection. False means unsupported and
    // must not change work, completion receipts or invoke the sink. A backend
    // that starts work must finish with true or throw, never request fallback
    // after partial execution/publication. True retains full score-row counts.
    virtual bool frequency_peaks(std::span<const double>, std::size_t,
        const CoarsePeakSink&, CoarseBulkWork&, std::stop_token) { return false; }
    virtual void lag_tile(std::size_t first_workspace, std::size_t workspace_count,
        const CoarseScoreSink&, CoarseBulkWork&, std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
    virtual CoarseBulkCompletedWork completed_work() const {
        const auto result = receipt();
        return {result.score_rows, result.executed};
    }
};

// ---- Fine-stage device sessions ----------------------------------------
// The shared part of this block (the session contract, StageDeviceWork,
// StageShape and the ReceiverBulkBackend factories) changes only together
// with every family. Each family's block is append-only for that family.
//
// A factory is the session's admission. The slot owner calls it on its own
// thread when the stage starts. It runs the pure selection policy
// (stage_decline), a lease that never waits, and creates every buffer,
// pipeline and transform plan. If any of these fails it returns null and
// records the decline or native fallback in the backend receipt; the
// caller then runs the unchanged native stage, whose host workspace stays
// quoted. An explicit device route throws instead only where the segmented
// route does: a transform the device library cannot plan has no substitute
// on a requested device route. A policy decline of an explicit device route
// is recorded, and the probe's --require-accelerator rule reports it.
// The session ends with the stage and releases its lease on destruction.
// The stage's lanes may call it concurrently; it serializes its own device
// context. A call either returns false, before device work (such as a
// nonfinite input) or after it (such as nonfinite device results), with its
// outputs and the work counters untouched and its native fallback recorded,
// or publishes all of its outputs, or throws. After false the receiver runs
// that call's work natively.
// A stage the receiver keeps native before calling a factory is recorded
// with record_stage_native, so that its planned call is counted: a decline
// for the receiver's own reason, or a fallback when the device was selected
// but its lease cannot be held, such as host memory (a unified-memory
// lease or compilation) that stage_host_bytes quotes outside the preserved
// fine envelope even at the narrowest batch.

// Device work of one call, credited only after validated readback.
struct StageDeviceWork {
    std::uint64_t forward_ffts = 0, inverse_ffts = 0, fft_rows = 0, fft_row_stages = 0;
    std::uint64_t score_rows = 0, upload_bytes = 0, download_bytes = 0;
    bool operator==(const StageDeviceWork&) const = default;
};

// --- signed-lag gain direct rank -------------------------------------------
// The public shape, from the coarse midpoint gain plan. The device always
// tiles each block's direct statistic into tile_fft_rows transforms. Shift
// runs and audit blocks are table capacities: the run limit (at most the
// blocks) and the sampled blocks. Maximum outputs bounds every block's
// outputs (its run's last minus first shift, plus one) from both ends of
// the aperture, where each lag's monotone shift takes its extremes. All of
// it is public geometry, so the DSP preflight builds the same shape as the
// rank before any device work.
struct GainRankShape {
    std::size_t observation_rows = 0, kernel_rows = 0, block_rows = 0, blocks = 0, lags = 0;
    std::size_t direct_frequencies = 0, direct_radius = 0, frequency_stride = 0;
    std::size_t tile_fft_rows = 0;
    std::size_t shift_runs = 0, audit_blocks = 0;
    std::size_t maximum_outputs = 0;
    bool operator==(const GainRankShape&) const = default;
};
// One block's host-prepared native geometry: rows, outputs, its shift run
// and first shift, the earliest observation row, and the FP64 template
// scales and log-determinants of both bits.
struct GainRankBlock {
    std::uint32_t block = 0, first = 0, rows = 0, outputs = 0, run = 0;
    std::int64_t first_shift = 0, earliest = 0;
    std::array<double, 2> scale{}, logdet{};
};
struct GainRankRequest {
    std::span<const Complex> observation;                   // FP64; a session packs the spans it scores
    std::array<std::span<const Complex>, 2> direct_columns; // each bit's direct template column
    std::span<const GainRankBlock> blocks;                  // unsampled blocks in block order, then audit blocks
    std::span<const std::int32_t> run_shifts;               // [run][lag]: shift minus the run's first shift
    double correlation_scale = 0;
};
// The receiver's certification of a session's published call, for the
// backend receipt's family extras. The band ratio is the largest
// |T - R| / lag band over the rescored lags; above 1 the band failed. The
// band source is the band term of the bit with the larger band.
struct GainRankCertificationRecord {
    std::uint64_t frontier_lags = 0, rescored_lags = 0;
    double rescore_seconds = 0, band_ratio = 0;
    std::string_view band_source;
};
class GainRankBulk {
public:
    virtual ~GainRankBulk() = default;
    // direct_totals: [2][lags] FP64 sums over the unsampled blocks in block
    // order. audit: [audits][2][lags] FP32 per-block log-means of the
    // request's last `audits` blocks (at most the shape's audit_blocks),
    // which set the certificate band against the native values. Every
    // other block is unsampled. A published total may be nonfinite; the
    // receiver's certification then runs the native rank.
    virtual bool direct_ranks(const GainRankRequest&, std::span<double> direct_totals,
                              std::span<float> audit, StageDeviceWork&, std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
    // Ends the session's device work after its last call: its buffers,
    // plans and lease return before the receiver's FP64 certification,
    // which can outlast the call. receipt() and record_certification()
    // stay valid; no call may follow.
    virtual void release() {}
    // Records the receiver's certification of a published call, whether it
    // published or fell back.
    virtual void record_certification(const GainRankCertificationRecord&) {}
};

// --- drift proposals ---------------------------------------------------------
// The public shape, from the drift plan and its slope proposals. A ridge
// with no segments is an exhaustive slope bank or an absent slow plan.
// slope_batch bounds the dechirped slopes one device transform batch holds;
// the receiver halves it until the session's host bytes fit its envelope.
struct DriftBulkShape {
    std::size_t rows = 0, taps = 0, nfft = 0, frequency_radius = 0;
    std::size_t segments = 0, segment_fft = 0, slow_segments = 0, slow_segment_fft = 0;
    std::size_t ridge_slopes = 0, ridge_centers = 0, slope_budget = 0, lanes = 0;
    std::size_t slope_batch = 0;
    bool operator==(const DriftBulkShape&) const = default;
};
// One ridge search: S+1 segment row bounds, slopes in the native visit
// order, [slope][segment] host-rounded offsets, and whether the slow
// refinement reads the power table back.
struct DriftRidgeGeometry {
    std::size_t segments = 0, segment_fft = 0;
    std::span<const std::size_t> segment_bounds;
    std::span<const std::int64_t> slopes;
    std::span<const std::int64_t> offsets;
    std::int64_t bin_radius = 0, bin_stride = 0;
    bool return_power = false;
};
// Per slope and bit: the first maximum, by ascending center, of the FP64
// incoherent sum over segments in segment order. A center whose shifted bin
// leaves the open half band (-fft_rows/2, fft_rows/2) skips that segment.
// The native ridge (drift_ridge_records) and a device ridge share this
// record.
struct DriftRidgeRecord {
    std::array<double, 2> score{-std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity()};
    std::array<std::int64_t, 2> center{};
};
// One entry of a slope's per-bit top-eight frontier.
inline constexpr std::size_t drift_frontier_ranks = 8;
struct DriftFrontierRow {
    double rank = 0;
    std::int64_t bin = 0;
};
struct DriftAtomRequest {
    std::size_t lane = 0;                          // selects this lane's staging slot
    std::array<std::array<double, 2>, 2> energy{}; // proposal normalization only
    double normalized = 0;                         // staged products are round32(P * normalized)
    DriftRidgeGeometry fast, slow;                 // slow.segments == 0: no slow refinement
    std::span<const std::int64_t> slopes;          // dechirp slopes; empty for the ridge alone
    double rate_step = 0, delta = 0, first_t = 0;
    bool nominate = false;                         // the rank pass nominates zero-slope bins
};
// A ridge call computes the records of each geometry with segments, and
// its FP64 power table [bit][segment][bin] where return_power is set. A
// geometry's segment bounds and offsets must stay valid and unchanged for
// the session's lifetime: a session may upload them once. A slopes call
// writes each requested slope's per-bit stable top eight (equal ranks by
// ascending signed bin, NaN and -inf excluded, padded with -inf ranks) in
// request order, and with nominate each bit's largest energy-normalized
// power over the public bins, -inf if none.
struct DriftAtomResult {
    std::span<DriftRidgeRecord> fast, slow;
    std::span<double> slow_power;
    std::span<DriftFrontierRow> frontiers;         // [slope][bit][8]
    std::array<double, 2> nomination{};
    std::span<double> fast_power;
};
// A ridge or slopes call that returns false has recorded its native
// fallback reason in the backend receipt and left its outputs and work
// untouched; the receiver then scores the atom natively.
class DriftBulk {
public:
    virtual ~DriftBulk() = default;
    // Host-visible slot of one lane, [2 * taps][rows], written outside the
    // session's mutex.
    virtual std::span<std::complex<float>> staging(std::size_t lane) noexcept = 0;
    virtual bool ridge(const DriftAtomRequest&, DriftAtomResult&, StageDeviceWork&, std::stop_token) = 0;
    virtual bool slopes(const DriftAtomRequest&, DriftAtomResult&, StageDeviceWork&, std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
};

// --- varying statistics --------------------------------------------------------
// The arithmetic of the template-side projections: FP64 transforms, or FP32
// templates and transforms with FP64 state.
enum class VaryingArithmetic : std::uint8_t { fp64, fp32_template };
// The public shape, from the varying fine plan and its tracking plans. A
// block's items are the affine items, 2 * taps template columns each (the
// fixed-affine states, then center_atoms central independent atoms, whose
// projected frequency lattice is empty), then the tracked independent atoms
// of four columns each. bluestein_rows is the transform length of the block
// and final projections, zero for a projection without a DFT plan; taps is
// the most taps of any item and affine_columns the affine items' template
// columns per block.
struct VaryingBulkShape {
    std::size_t block_rows = 0, final_rows = 0;
    std::array<std::size_t, 2> bluestein_rows{}, modes{};
    std::size_t independent_states = 0, affine_states = 0, independent_atoms = 0, taps = 0;
    std::size_t batch_blocks = 0;
    VaryingArithmetic arithmetic = VaryingArithmetic::fp64;
    std::size_t affine_columns = 0, center_atoms = 0;
    bool operator==(const VaryingBulkShape&) const = default;
};
// The native projection's chirp and kernel spectrum (empty for a power-of-two
// transform), retained indices and scale.
struct VaryingProjectionTables {
    std::span<const Complex> chirp, kernel_spectrum;
    std::span<const std::size_t> indices;
    double scale = 0;
};
// One template staging slot of a session admitted for `blocks` blocks, in
// bytes from the slot's start. FP64 templates [block][column][rows]: a
// block's columns are packed at its own rows, in a block stride of every
// item's columns at final_rows. Then the block observations projected by the
// host, [block][mode] at the larger mode count, and one nonzero-basis byte per
// [block][item] that only the host reads. Last, the stage's constants: the
// tracked states' signed half-bins [length][state] and each affine item's
// taps, which a call names by spans into either slot.
struct VaryingStagingLayout {
    std::size_t template_columns = 0, block_values = 0, mode_stride = 0, items = 0;
    std::size_t projected_offset = 0, flags_offset = 0, shifts_offset = 0, taps_offset = 0, bytes = 0;
};
inline VaryingStagingLayout varying_staging_layout(const VaryingBulkShape& shape, std::size_t blocks) {
    const auto multiply = [](std::size_t a, std::size_t b) {
        if (a && b > SIZE_MAX / a) throw Error("Robust varying staging extent overflow");
        return a * b;
    };
    const auto add = [](std::size_t a, std::size_t b) {
        if (b > SIZE_MAX - a) throw Error("Robust varying staging extent overflow");
        return a + b;
    };
    VaryingStagingLayout layout;
    layout.template_columns = add(shape.affine_columns, multiply(4, shape.independent_atoms));
    layout.block_values = multiply(layout.template_columns, shape.final_rows);
    layout.mode_stride = shape.modes[0] < shape.modes[1] ? shape.modes[1] : shape.modes[0];
    layout.items = add(shape.affine_states, shape.independent_atoms);
    layout.projected_offset = multiply(multiply(blocks, layout.block_values), sizeof(Complex));
    layout.flags_offset = add(layout.projected_offset,
        multiply(multiply(blocks, layout.mode_stride), sizeof(Complex)));
    // Each region starts on a complex-value boundary.
    const auto aligned = [&](std::size_t bytes) {
        return add(bytes, (sizeof(Complex) - bytes % sizeof(Complex)) % sizeof(Complex));
    };
    layout.shifts_offset = add(layout.flags_offset, aligned(multiply(blocks, layout.items)));
    layout.taps_offset = add(layout.shifts_offset,
        multiply(multiply(2, shape.independent_states), sizeof(std::int64_t)));
    layout.bytes = add(layout.taps_offset, aligned(multiply(shape.affine_states, sizeof(std::uint32_t))));
    return layout;
}
// One call: consecutive public blocks from first_block, the last of them the
// slot's final-length block when terminal_last. The host has written the
// staging slot's templates and projected observations for blocks
// [0, blocks); projected_y is that slot's projected region. affine_taps holds
// each affine item's taps. independent_shifts is [length][state]: each
// tracked state's signed half-bin for the block and final lengths, as the
// native projected tracker rounds it. Both constants lie in a staging slot.
// Correlations and Gram entries of the affine items are divided by the noise
// variance, as the native ones are.
struct VaryingBatch {
    std::size_t first_block = 0, blocks = 0;
    bool terminal_last = false;
    std::array<VaryingProjectionTables, 2> tables;     // block, final
    std::span<const Complex> projected_y;
    std::span<const std::int64_t> independent_shifts;
    std::size_t staging_slot = 0;                      // double-buffered template staging
    std::span<const std::uint32_t> affine_taps;
    double variance = 0;
};
// One tracked independent-gain statistic cell of a block: a (bit, state) of
// the projected tracker, in the order the native visitor produces them (the
// even half-bin states, then the odd ones, each in state order, both bits).
struct VaryingIndependentRow {
    std::array<Complex, 2> correlations{};
    std::array<double, 2> diagonal{};
    Complex cross{};
    std::int64_t half_bin = 0;
    std::uint32_t bit = 0, state = 0;
};
// Item-major outputs, [item][block_stride] from the call's first block: the
// fixed-affine states and central atoms, whose nonzero_basis flags the host
// sets, and [atom][block_stride][2 * independent_states] tracked cells.
struct VaryingBatchOutput {
    std::span<FineBulkStatistics> affine;
    std::span<VaryingIndependentRow> independent;
    std::span<FineBulkStatistics> center;
    std::size_t block_stride = 0;
};
class VaryingBulk {
public:
    virtual ~VaryingBulk() = default;
    // Host-visible template staging of one slot (varying_staging_layout).
    virtual std::span<std::byte> template_staging(std::size_t slot) noexcept = 0;
    virtual bool statistics(const VaryingBatch&, VaryingBatchOutput&, StageDeviceWork&, std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
    // The admitted blocks per call, at most the shape's batch_blocks.
    virtual std::size_t batch_blocks() const noexcept = 0;
};

// A family's public shape, or the family alone for the shape-independent
// part of its selection policy.
using StageShape = std::variant<StageFamily, GainRankShape, DriftBulkShape, VaryingBulkShape>;
static_assert(std::is_same_v<std::variant_alternative_t<0, StageShape>, StageFamily> &&
              std::is_same_v<std::variant_alternative_t<1, StageShape>, GainRankShape> &&
              std::is_same_v<std::variant_alternative_t<2, StageShape>, DriftBulkShape> &&
              std::is_same_v<std::variant_alternative_t<3, StageShape>, VaryingBulkShape> &&
              std::variant_size_v<StageShape> == 4, "stage_family names StageShape alternatives by index");
constexpr StageFamily stage_family(const StageShape& shape) noexcept {
    if (const auto* family = std::get_if<StageFamily>(&shape)) return *family;
    return shape.index() == 1 ? StageFamily::gain_rank : shape.index() == 2 ? StageFamily::drift : StageFamily::varying;
}

class ReceiverBulkBackend {
public:
    virtual ~ReceiverBulkBackend() = default;
    virtual bool supports_fine() const noexcept { return true; }
    // Uses the full public block calendar, independent of observed scores.
    // Empty means the backend must provide the requested fine stage.
    virtual std::string_view fine_fallback_reason(const FineBulkGeometry&) const noexcept {
        return supports_fine() ? std::string_view{} : "native-fine-backend";
    }
    virtual bool fine_maps_on_host() const noexcept { return false; }
    // The resident blocks one fine call of this public geometry may hold,
    // for the session's preflight. Two keeps the single-dispatch workspace.
    // A backend that wraps another forwards this and every other query.
    virtual std::size_t fine_resident_blocks(const FineBulkGeometry&) const noexcept { return 2; }
    virtual bool permits_native_exact_chirp_z() const noexcept { return false; }
    virtual bool supports_exact_device_chirp_z() const noexcept { return false; }
    // Bound a computational tile before preflight/construction; every public
    // frequency is still scored. Explicit/default CPU geometry is unchanged.
    virtual std::size_t maximum_frequency_tile_width(const CoarseBulkGeometry&,
        std::size_t, std::size_t requested_width, double) const { return requested_width; }
    // Geometry-specific automatic admission, from the complete public prior.
    // An absent result retains the existing backend's workspace contract.
    virtual std::optional<CoarseFrequencyPlan> frequency_plan(const CoarseBulkGeometry&,
        std::size_t, std::size_t) const { return std::nullopt; }
    // Pure public-geometry selection, shared by preflight and construction.
    // Empty means the backend must construct the requested coarse session.
    virtual std::string_view coarse_fallback_reason(double, double, CoarseRoute) const noexcept { return {}; }
    virtual void record_native_coarse(std::string_view) {}
    virtual void record_native_fine(std::string_view) {}
    virtual std::unique_ptr<FineBulk> make_fine(double projected_rate_hz, double clock_span_ppm) = 0;
    virtual std::unique_ptr<CoarseBulk> make_coarse(double projected_rate_hz, double clock_span_ppm) = 0;
    // Additional host reservation to include in public DSP preflight.
    virtual std::size_t persistent_host_workspace() const noexcept { return 0; }
    // Host RAM that one coarse session's device segmented work can hold for
    // these public shapes, where that device memory is host memory. It is
    // live only before the frequency search admits its own workspace. It
    // must not start device work.
    virtual std::size_t segmented_host_bytes(std::span<const CoarseSegmentedShape>) const { return 0; }
    virtual std::size_t fine_host_bytes(std::size_t block_rows, std::size_t taps) const = 0;
    virtual std::size_t coarse_host_bytes(std::size_t maximum_fft_rows,
        std::size_t maximum_tile_frequencies, std::size_t arrivals) const = 0;
    virtual BulkReceipt receipt() const = 0;
    // GUI polling must not query a driver or synchronize outstanding device work.
    virtual BulkReceipt status_receipt() const { return receipt(); }
    // Fine-stage device sessions, under the contract above the family
    // interfaces. A wrapping backend forwards every one of these.
    // The pure selection policy, shared by preflight and execution: empty
    // when the backend would construct a session for this shape, otherwise
    // why the stage stays native. fp64_forced states an explicit native FP64
    // coarse request, which every family declines. It starts no device work.
    virtual std::string_view stage_decline(const StageShape&, bool /*fp64_forced*/) const noexcept {
        return "native-stage-backend";
    }
    virtual std::unique_ptr<GainRankBulk> make_gain_rank(const GainRankShape&, bool /*fp64_forced*/, std::stop_token) {
        return {};
    }
    virtual std::unique_ptr<DriftBulk> make_drift(const DriftBulkShape&, bool /*fp64_forced*/, std::stop_token) {
        return {};
    }
    virtual std::unique_ptr<VaryingBulk> make_varying(const VaryingBulkShape&, bool /*fp64_forced*/, std::stop_token) {
        return {};
    }
    // Host RAM that one session of this shape can hold, for the native DSP
    // preflight: its plan and shader compilation and, where device memory is
    // host memory, its lease; zero when the policy declines it. It must not
    // start device work.
    virtual std::size_t stage_host_bytes(const StageShape&, bool /*fp64_forced*/) const { return 0; }
    // A stage the receiver ran natively without a factory's own record: a
    // decline or a fallback before any factory call, which counts its
    // planned call, or a fallback of an admitted session's call. A decline
    // takes a policy or family reason, a fallback a fallback or family
    // reason (StageNative).
    virtual void record_stage_native(StageFamily, StageReason, StageNative) {}
};

// The application's GPU selection wrapper (src/triad_gpu_backend.cpp). It
// forwards every query and factory of the backend it wraps and adds the
// selection reason to receipts. Exposed for its forwarding check.
std::shared_ptr<ReceiverBulkBackend> make_selected_gpu_backend(std::shared_ptr<ReceiverBulkBackend>, std::string reason);

} // namespace datapump::triad::v3::receiver_detail
