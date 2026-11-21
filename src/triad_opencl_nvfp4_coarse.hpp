#pragma once

#include "triad_opencl_nvfp4_projection.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

namespace datapump::triad::v3::receiver_detail {

struct CoarseBulkPeaks;
struct CoarseTemplateSource;
struct CoarseTemplateGram;

// Public frequency-tile geometry of one coarse session. Synthesis rows are
// the raw template rows before the canonical decimating FIR, or zero when
// the caller supplies channelized columns. The decimation and taps decide
// whether the FIR warps its raw rows as it filters or stores them first.
struct OpenClCoarseShape {
    std::size_t taps = 0, kernel_rows = 0, arrivals = 0, workspaces = 0, tile_frequencies = 0;
    std::size_t synthesis_raw_rows = 0, synthesis_source_rows = 0;
    std::size_t synthesis_lanes = 0, synthesis_cells = 0, synthesis_taps = 0, synthesis_decimation = 0;
    bool operator==(const OpenClCoarseShape&) const = default;
};

struct OpenClCoarseWork {
    std::uint64_t upload_bytes = 0, download_bytes = 0, dispatches = 0, blocking_transfers = 0;
    // Issued real multiply-adds include the padded inner extent, the
    // arrivals beyond the public window in the last block and the unused
    // columns of a last bank tile. Useful ones are the public score rows
    // times their four real products per ray and sample.
    std::uint64_t issued_products = 0, useful_products = 0;
    std::uint64_t score_rows = 0, rescored_peaks = 0;
    std::uint64_t rendered_samples = 0, warped_samples = 0, filtered_rows = 0;
    // Device command intervals, present when the session profiles its queue.
    double product_seconds = 0, packing_seconds = 0, selection_seconds = 0, synthesis_seconds = 0;
    // Parts of those intervals: lane rendering, warping with filtering and
    // the Gram of synthesis, and the rotation and rescoring of selection.
    double render_seconds = 0, filter_seconds = 0, gram_seconds = 0, rescore_seconds = 0;
};

// One device-resident frequency-tile search: FP4 Tensor Core scores for every
// public workspace, frequency and arrival, the exact arrival frontier of
// those scores, and FP32 rescoring of each retained arrival.
class OpenClNvfp4Coarse {
public:
    ~OpenClNvfp4Coarse();
    OpenClNvfp4Coarse(const OpenClNvfp4Coarse&) = delete;
    OpenClNvfp4Coarse& operator=(const OpenClNvfp4Coarse&) = delete;

    static bool supports(const OpenClCoarseShape&) noexcept;
    static OpenClNvfp4Workspace workspace(const OpenClCoarseShape&);

    // Channelized columns in [workspace][tap] order, each kernel_rows long.
    void load_bank(std::span<const std::span<const std::complex<double>>> columns, OpenClCoarseWork&,
                   std::stop_token = {});
    // Renders and filters the same columns on the device and returns the
    // FP64 Gram entries of each workspace.
    void synthesize_bank(const CoarseTemplateSource&, std::span<CoarseTemplateGram>, OpenClCoarseWork&,
                         std::stop_token = {});
    // Inverse holds the first, second and complex cross coefficients of each
    // workspace's quadratic form. A one-ray form has the first alone and the
    // others zero. Variance divides every score.
    void prepare(std::span<const std::complex<double>> observation, std::span<const std::array<double, 4>> inverse,
                 double projected_rate_hz, double variance, OpenClCoarseWork&, std::stop_token = {});
    // Output is [workspace][frequency]. Scores are the FP32 rescored values
    // of the arrivals that the FP4 frontier retained.
    void peaks(std::span<const double> frequencies, std::size_t separation, std::span<CoarseBulkPeaks> output,
               OpenClCoarseWork&, std::stop_token = {});
    // Validation access to the resident channelized bank and to the FP4
    // scores of the last tile, as [frequency][workspace][arrival].
    void read_bank(std::span<std::complex<float>> columns);
    void read_scores(std::span<float> scores);

private:
    struct Impl;
    explicit OpenClNvfp4Coarse(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class OpenClNvfp4Device;
};

} // namespace datapump::triad::v3::receiver_detail
