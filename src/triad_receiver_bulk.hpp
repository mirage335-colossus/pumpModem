#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <array>
#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>

namespace datapump::triad::v3::receiver_detail {

inline constexpr std::array<std::string_view, 8> bulk_direct_fallback_reasons{
    "unsupported-or-abrupt-pulse", "insufficient-cache-oversampling", "native-origin-precision",
    "nonfinite-relative-map", "large-phase", "pinned-tile-limit", "relative-map-range", "other"
};

struct BulkReceipt {
    std::string device, device_name, library_version, precision;
    int cuda_runtime_version = 0, cuda_driver_version = 0;
    unsigned numerical_threads = 1;
    std::size_t workspace_limit_bytes = 0, reserved_bytes = 0, peak_reserved_bytes = 0;
    std::size_t allocator_live_bytes = 0, allocator_reserved_bytes = 0;
    std::size_t allocator_peak_live_bytes = 0, allocator_peak_reserved_bytes = 0;
    std::size_t total_device_bytes = 0, initial_free_device_bytes = 0;
    std::size_t runtime_headroom_bytes = 0;
    std::size_t fft_plan_cache_count = 0, fft_plan_cache_limit = 0;
    std::uint64_t upload_bytes = 0, download_bytes = 0, synchronizations = 0;
    std::uint64_t fft_calls = 0, fft_rows = 0, fir_input_rows = 0, fir_output_contributions = 0;
    std::uint64_t score_rows = 0, statistics_blocks = 0, projected_values = 0, dc_reduction_values = 0;
    std::uint64_t mapped_columns = 0, mapped_values = 0, uploaded_tiles = 0, tile_hits = 0;
    std::uint64_t direct_columns = 0, direct_values = 0;
    std::array<std::uint64_t, bulk_direct_fallback_reasons.size()> direct_fallback_counts{};
    double wall_seconds = 0;
    bool cuda = false, executed = false;
};

struct BulkMappedTile {
    std::shared_ptr<const void> owner;
    std::span<const Complex> samples;
};

struct BulkMappedColumn {
    double fraction = 0, scale = 1, phase = 0, phase_step = 0;
    std::int64_t origin = 0;
    std::size_t output_column = 0;
    std::vector<BulkMappedTile> tiles;
};

struct BulkDirectColumn {
    std::size_t output_column = 0;
    std::span<const Complex> samples;
    std::string_view reason;
};

using BulkMappedSink = std::function<void(std::span<const double> offsets,
    std::span<const BulkMappedColumn>, std::span<const BulkDirectColumn>)>;

// The callback supplies native immutable tiles with the original mapping.
// It may call the sink repeatedly with disjoint columns. Each tile lease and
// direct column remains valid until the synchronous sink returns.
using FineMappingSource = std::function<void(std::size_t candidate, std::size_t first_row,
    std::size_t row_count, const BulkMappedSink&)>;

struct FineBulkBlock {
    std::size_t rows = 0, taps = 0;
    std::span<const std::size_t> projection_indices;
    std::span<const Complex> projected_y;
    double variance = 0;
};

struct FineBulkStatistics {
    std::array<Complex, 6> correlations{};
    std::array<Complex, 12> gram{};
    bool nonzero_basis = false;
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
};

struct CoarseBulkWork {
    std::uint64_t forward_ffts = 0, inverse_ffts = 0, fft_rows = 0, fft_row_stages = 0;
    std::uint64_t fir_input_rows = 0, fir_output_contributions = 0, score_rows = 0;
    std::uint64_t workspace_frequency_pairs = 0, singleton_dot_terms = 0;
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
};

// A score view contains [workspace][frequency][arrival] in canonical order.
// The callback consumes it before the backend reuses host staging.
using CoarseScoreSink = std::function<void(std::size_t first_workspace, std::size_t workspace_count,
    std::span<const double>)>;

class CoarseBulk {
public:
    virtual ~CoarseBulk() = default;
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
    virtual void lag_tile(std::size_t first_workspace, std::size_t workspace_count,
        const CoarseScoreSink&, CoarseBulkWork&, std::stop_token) = 0;
    virtual BulkReceipt receipt() const = 0;
    virtual CoarseBulkCompletedWork completed_work() const {
        const auto result = receipt();
        return {result.score_rows, result.executed};
    }
};

class ReceiverBulkBackend {
public:
    virtual ~ReceiverBulkBackend() = default;
    virtual std::unique_ptr<FineBulk> make_fine(double projected_rate_hz, double clock_span_ppm) = 0;
    virtual std::unique_ptr<CoarseBulk> make_coarse(double projected_rate_hz, double clock_span_ppm) = 0;
    // Additional host reservation to include in public DSP preflight.
    virtual std::size_t fine_host_bytes(std::size_t block_rows, std::size_t taps) const = 0;
    virtual std::size_t coarse_host_bytes(std::size_t maximum_fft_rows,
        std::size_t maximum_tile_frequencies, std::size_t arrivals) const = 0;
    virtual BulkReceipt receipt() const = 0;
};

} // namespace datapump::triad::v3::receiver_detail
