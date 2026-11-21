#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace datapump::triad::v3::receiver_detail {

struct OpenClNvfp4Workspace {
    std::size_t host_bytes = 0;
    std::size_t device_bytes = 0;
};

enum class OpenClFineMethod { staged, resident_fp4, resident_fft };

// Arithmetic of resident dense tile rendering and template mapping. FP32
// keeps FP64 tiles, time base and support decisions, and reduces each row's
// coordinate and phase exactly in fixed point; FP64 is the matched control
// and the default until the FP32 kernels are device-qualified.
enum class OpenClFineArithmetic { fp32, fp64 };

// Accumulation of the coarse rescoring products: FP32 partial sums of 64
// rows added in FP64, or every product in FP64, the matched control and the
// default until the FP32 partial sums are device-qualified.
enum class OpenClRescoreArithmetic { fp32_partial, fp64 };

struct FineResidentBlock;

struct OpenClResidentWork {
    std::uint64_t upload_bytes = 0, download_bytes = 0;
    std::uint64_t uploaded_tiles = 0, tile_hits = 0;
    std::uint64_t mapped_columns = 0, direct_columns = 0, dispatches = 0;
    // Writes the host waits for, and writes from staging that stays
    // unchanged until the batch's blocking read.
    std::uint64_t blocking_writes = 0, async_writes = 0, mapping_dispatches = 0;
    // Waits for the whole queue before an arena is reused within a batch.
    std::uint64_t arena_synchronizations = 0;
    std::uint64_t setup_upload_bytes = 0, observation_upload_bytes = 0, setup_dispatches = 0;
    // Dense tiles rendered on the device and the lanes keyed for them.
    std::uint64_t rendered_tiles = 0, keyed_lanes = 0;
    // A profiled session only. Host spans are elapsed on the calling thread:
    // the mapping sources without their sink, the sink's staging with any
    // transfer it makes, the final uploads and enqueues, and the wait for the
    // statistics. Device values sum command intervals on the device clock,
    // and the span runs from the first command's start to the last one's end.
    double host_source_seconds = 0, host_staging_seconds = 0, host_transfer_seconds = 0, host_wait_seconds = 0;
    double device_transfer_seconds = 0, device_render_seconds = 0, device_map_seconds = 0;
    double device_pack_seconds = 0, device_product_seconds = 0, device_statistics_seconds = 0;
    double device_span_seconds = 0;
};

// Resident session geometry beyond the projection shape.
struct OpenClResidentOptions {
    // The most public blocks of one call. Lanes are blocks times candidates
    // times the four columns of a candidate.
    std::size_t blocks = 2;
    // Keyed cells and lanes of a dense source whose tiles the session
    // renders. Zero maps uploaded native tiles only.
    std::size_t tile_cells = 0, tile_lanes = 0;
    // Record host spans and device command intervals in the work.
    bool profile = false;
    // With device-rendered tiles, submit the mappings of every this many
    // blocks while staging the following ones. Zero stages a whole batch
    // before the device starts.
    std::size_t stream_blocks = 4;
    // Tile cache slots the session uses, 16 to 64. Tests choose fewer to
    // flush under cache pressure often; the request arena holds four times
    // this many entries.
    std::size_t cache_slots = 64;
    // The kernels that render tiles and map columns.
    OpenClFineArithmetic arithmetic = OpenClFineArithmetic::fp64;
};

class OpenClNvfp4Device;
class OpenClNvfp4Coarse;
struct OpenClCoarseShape;

class OpenClNvfp4Projection {
public:
    ~OpenClNvfp4Projection();
    OpenClNvfp4Projection(const OpenClNvfp4Projection&) = delete;
    OpenClNvfp4Projection& operator=(const OpenClNvfp4Projection&) = delete;

    // The caller accounts separately for the mapped and projected host spans.
    static OpenClNvfp4Workspace workspace(std::size_t rows, std::size_t modes,
                                          std::size_t batch_capacity,
                                          OpenClFineMethod method = OpenClFineMethod::staged,
                                          const OpenClResidentOptions& = {});

    // A reusable host slab for direct template mapping. Size counts complete
    // complex records including trailers. Empty means pageable input is used.
    std::span<std::complex<float>> mapped_staging(std::size_t samples) noexcept;

    // Both spans are lane-major. Each mapped lane contains rows samples then
    // their maximum absolute real or imaginary component in the real part of
    // one extra sample. Its imaginary part is zero. A nonfinite input header
    // is rejected. Output lanes contain the orthonormal DFT modes in bin order.
    // A return of false publishes no results.
    bool project(std::span<const std::complex<float>> mapped,
                 std::span<std::complex<float>> projected, std::stop_token stop = {});

    // Up to the session's block count of compatible blocks share mapping
    // cache, projection and one result download. Blocks that name a tile
    // source receive tile addresses, and the session renders those tiles.
    // A return of false indicates incompatible inputs before visiting sources.
    // An execution error leaves both the supplied statistics and receipt untouched.
    bool project_resident(std::span<const FineResidentBlock> blocks, std::size_t candidates,
                          OpenClResidentWork& work, std::stop_token stop = {});

    // Validation access to a rendered dense tile that is still resident.
    bool read_source_tile(std::uint32_t lane, std::uint64_t index, std::span<std::complex<double>> samples);

private:
    struct Impl;
    explicit OpenClNvfp4Projection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class OpenClNvfp4Device;
};

class OpenClNvfp4Device {
public:
    ~OpenClNvfp4Device();
    OpenClNvfp4Device(const OpenClNvfp4Device&) = delete;
    OpenClNvfp4Device& operator=(const OpenClNvfp4Device&) = delete;

    // A nonzero UUID must match the selected Vulkan GPU. With no UUID, a
    // single eligible device is required. PTX is fixed at build time.
    static std::shared_ptr<OpenClNvfp4Device> open(
        std::span<const std::byte, 16> device_uuid, std::span<const std::byte> ptx,
        std::string* unavailable = nullptr);

    std::unique_ptr<OpenClNvfp4Projection> make_projection(
        std::size_t rows, std::span<const std::size_t> bins, std::size_t batch_capacity,
        std::size_t workspace_limit, OpenClFineMethod method = OpenClFineMethod::staged,
        const OpenClResidentOptions& = {}) const;

    // Empty when the device cannot hold the session within the limit. A
    // profiled session records device command intervals in its work.
    std::unique_ptr<OpenClNvfp4Coarse> make_coarse(const OpenClCoarseShape&, std::size_t workspace_limit,
        bool profile = false, OpenClRescoreArithmetic = OpenClRescoreArithmetic::fp64) const;

private:
    struct Impl;
    explicit OpenClNvfp4Device(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
    friend class OpenClNvfp4Projection;
    friend class OpenClNvfp4Coarse;
};

} // namespace datapump::triad::v3::receiver_detail
