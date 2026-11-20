#include "triad_opencl_nvfp4_coarse.hpp"

#include "triad_opencl_nvfp4_device.hpp"
#include "triad_receiver_bulk.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
using namespace opencl_nvfp4;
namespace {

// These mirror the kernel constants in triad_opencl_nvfp4_kernels.cu.
constexpr std::size_t tile_inner = 64, variants = 8, groups = 8, block_arrivals = 512;
// The product kernel turns its operand ring once per five slices and loads
// four quads past the last one a stream needs.
constexpr std::size_t ring_tiles = 5, stream_slack = 4;
constexpr std::size_t tile_products = 16 * 8 * tile_inner, block_warps = local_size / 32;
constexpr std::size_t pulse_samples = 8 * 256 + 1, source_parameters = 16, column_parameters = 5;
// One epoch of the canonical bank has two lanes and their clock and ray
// columns. Larger staging only lets more of them share a dispatch.
constexpr std::size_t maximum_raw_batch_bytes = std::size_t{128} << 20, maximum_raster_lanes = 2;
// The FIR stage of one tile of output rows, in complex pairs.
constexpr std::size_t fir_stage_pairs = 6016;
constexpr double normalized_max = 6. * 448.;
constexpr auto no_peak = std::numeric_limits<std::uint64_t>::max();
static_assert(sizeof(std::complex<float>) == 2 * sizeof(float));
static_assert(sizeof(std::complex<double>) == 2 * sizeof(double));
static_assert(sizeof(CoarseBulkPeaks) == 40);

enum Buffer { bank, bank_peaks, packed_bank, observation, packed_observation, observation_scales,
              pack_parameters, score_parameters, coefficients, inverse, scores, frontier, rotated,
              chips, pulse, source, columns, raster, raw, taps, gram, buffer_count };
enum Kernel { column_peaks, pack_bank, pack_observation, fp4_scores, select_peaks, rotate_observation,
              rescore_peaks, render_lanes, warp_columns, fir_tiles, fir_decimate, warp_fir, bank_gram, kernel_count };

// The taps that one stage window of a tile holds beside the raw span of its
// rows, as fir_window in the kernels computes it. Zero when none fits.
std::size_t fir_window(std::size_t decimation, std::size_t taps) {
    const auto spread = multiply(local_size - 1, decimation);
    if (spread >= fir_stage_pairs) return 0;
    return std::min(2 * (fir_stage_pairs - spread) / 3, taps);
}

bool finite(std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

struct Geometry {
    OpenClCoarseShape shape;
    std::size_t columns = 0, k = 0, tiles = 0, packed_rows = 0, arrival_blocks = 0, pairs = 0;
    std::size_t samples = 0, variant_samples = 0, stream_quads = 0, raw_batch = 0, raster_lanes = 0;
    // Output row tiles per column, and the taps of one stage window of a
    // tile. When they are all of its taps, the FIR warps the raw rows that a
    // tile reads as it stages them. Otherwise raw columns are stored first,
    // and filtered in windows or, when no window fits, in place.
    std::size_t row_tiles = 0, window = 0;
    bool fused = false;
    std::array<std::size_t, buffer_count> sizes{};

    explicit Geometry(const OpenClCoarseShape& public_shape) : shape(public_shape) {
        if (!OpenClNvfp4Coarse::supports(shape)) throw Error("Invalid Robust OpenCL NVFP4 coarse geometry");
        columns = multiply(shape.workspaces, shape.taps);
        k = align(multiply(shape.kernel_rows, 2), tile_inner * ring_tiles);
        tiles = k / tile_inner;
        pairs = (shape.workspaces + 1) / 2;
        packed_rows = multiply(pairs, 8);
        arrival_blocks = (shape.arrivals - 1) / block_arrivals + 1;
        samples = add(shape.kernel_rows, shape.arrivals - 1);
        // The last warp of the last block reads its whole inner extent.
        variant_samples = add(multiply(arrival_blocks, block_arrivals), k / 2);
        // Each thread slot has its own quads of operand words, and each
        // scale parity as many pairs of ready scale vectors.
        stream_quads = variant_samples / 32 + stream_slack;
        const auto frequencies = shape.tile_frequencies;
        const auto streams = multiply(frequencies, variants);
        const auto rows = multiply(frequencies, shape.workspaces);
        // Kernel indices and extents are 32-bit.
        (void)kernel_extent(multiply(packed_rows, k));
        (void)kernel_extent(multiply(streams, multiply(variant_samples, 2)));
        (void)kernel_extent(stream_quads);
        (void)kernel_extent(multiply(multiply(frequencies, pairs), arrival_blocks));
        (void)kernel_extent(multiply(rows, 2));
        (void)kernel_extent(samples);
        (void)kernel_extent(multiply(frequencies, samples));
        sizes[bank] = multiply(multiply(columns, shape.kernel_rows), sizeof(std::complex<float>));
        sizes[bank_peaks] = multiply(columns, sizeof(float));
        // One sixteen-byte record per slice and thread slot.
        sizes[packed_bank] = multiply(packed_rows, k);
        sizes[observation] = multiply(samples, sizeof(std::complex<float>));
        sizes[packed_observation] = multiply(multiply(streams, 4), multiply(stream_quads, 16));
        sizes[observation_scales] = multiply(multiply(streams, 2), multiply(stream_quads, 8));
        sizes[pack_parameters] = sizes[score_parameters] = multiply(frequencies + 2, sizeof(double));
        sizes[coefficients] = sizes[inverse] = multiply(shape.workspaces, 4 * sizeof(double));
        sizes[scores] = multiply(multiply(rows, shape.arrivals), sizeof(float));
        sizes[frontier] = multiply(rows, sizeof(CoarseBulkPeaks));
        sizes[rotated] = multiply(multiply(frequencies, samples), sizeof(std::complex<float>));
        if (shape.synthesis_raw_rows) {
            const auto column_bytes = multiply(shape.synthesis_raw_rows, sizeof(std::complex<float>));
            window = fir_window(shape.synthesis_decimation, shape.synthesis_taps);
            fused = window == shape.synthesis_taps;
            raw_batch = fused ? columns : std::clamp<std::size_t>(maximum_raw_batch_bytes / column_bytes, 1, columns);
            raster_lanes = std::min(maximum_raster_lanes, shape.synthesis_lanes);
            row_tiles = (shape.kernel_rows - 1) / local_size + 1;
            (void)kernel_extent(shape.synthesis_raw_rows);
            (void)kernel_extent(shape.synthesis_source_rows);
            (void)kernel_extent(shape.synthesis_decimation);
            if (window) (void)kernel_extent(multiply(multiply(columns, row_tiles), local_size));
            sizes[raster] = multiply(multiply(raster_lanes, shape.synthesis_source_rows), sizeof(std::complex<float>));
            sizes[chips] = multiply(multiply(shape.synthesis_lanes, shape.synthesis_cells), 3 * sizeof(std::complex<float>));
            sizes[pulse] = pulse_samples * sizeof(float);
            sizes[source] = source_parameters * sizeof(double);
            sizes[Buffer::columns] = multiply(columns, column_parameters * sizeof(double));
            sizes[raw] = fused ? 0 : multiply(raw_batch, column_bytes);
            sizes[taps] = multiply(shape.synthesis_taps, sizeof(float));
            sizes[gram] = multiply(shape.workspaces, 4 * sizeof(double));
        }
    }

    std::size_t device_bytes() const {
        std::size_t total = 0;
        for (const auto bytes : sizes) total = add(total, align(bytes, allocation_alignment));
        return total;
    }

    std::size_t staging_samples() const { return std::max(samples, shape.kernel_rows); }

    std::size_t host_bytes() const {
        const auto rows = shape.tile_frequencies * shape.workspaces;
        return add(multiply(staging_samples(), sizeof(std::complex<float>)),
                   add(multiply(rows, sizeof(CoarseBulkPeaks)),
                       multiply(columns, sizeof(float) + 4 * sizeof(double) + column_parameters * sizeof(double))));
    }
};

} // namespace

struct OpenClNvfp4Coarse::Impl {
    // A command's interval, charged to its stage and to one of its parts.
    struct Charge {
        double OpenClCoarseWork::* stage;
        double OpenClCoarseWork::* part = nullptr;
    };
    struct Timed {
        cl_event event;
        Charge charge;
    };

    std::shared_ptr<OpenClNvfp4Device::Impl> owner;
    Geometry geometry;
    cl_command_queue queue = nullptr;
    std::array<cl_mem, buffer_count> buffers{};
    std::array<cl_kernel, kernel_count> kernels{};
    std::vector<Timed> events;
    std::vector<std::complex<float>> staging;
    std::vector<float> peaks;
    std::vector<CoarseBulkPeaks> downloaded;
    double rate = 0, variance = 0, reciprocal_observation = 0;
    bool profile = false, pending = false, bank_ready = false, prepared = false;

    Impl(std::shared_ptr<OpenClNvfp4Device::Impl> device, const Geometry& shape, bool timed)
        : owner(std::move(device)), geometry(shape), profile(timed) {}

    ~Impl() {
        drain();
        for (const auto kernel : kernels)
            if (kernel) (void)owner->api.ReleaseKernel(kernel);
        for (const auto buffer : buffers)
            if (buffer) (void)owner->api.ReleaseMemObject(buffer);
        if (queue) (void)owner->api.ReleaseCommandQueue(queue);
    }

    void drain() noexcept {
        if (pending) (void)owner->api.Finish(queue);
        pending = false;
        for (const auto& timed : events) (void)owner->api.ReleaseEvent(timed.event);
        events.clear();
    }

    void initialize() {
        cl_int status = 0;
        queue = owner->api.CreateCommandQueue(owner->context, owner->device,
                                              profile ? CL_QUEUE_PROFILING_ENABLE : 0, &status);
        check(status, "create coarse queue");
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            if (!geometry.sizes[i]) continue;
            buffers[i] = owner->api.CreateBuffer(owner->context, CL_MEM_READ_WRITE, geometry.sizes[i], nullptr, &status);
            check(status, "allocate coarse buffer");
        }
        constexpr std::array<const char*, kernel_count> names{
            "coarse_column_peaks", "coarse_pack_bank", "coarse_pack_observation", "coarse_fp4_scores",
            "coarse_select_peaks", "coarse_rotate_observation", "coarse_rescore_peaks", "coarse_render_lanes",
            "coarse_warp_columns", "coarse_fir_tiles", "coarse_fir_decimate", "coarse_warp_fir", "coarse_bank_gram"};
        // Each synthesis route creates the kernels that it dispatches.
        const auto used = [&](std::size_t kernel) {
            if (kernel < render_lanes) return true;
            if (!geometry.shape.synthesis_raw_rows) return false;
            switch (kernel) {
            case warp_fir: return geometry.fused;
            case warp_columns: return !geometry.fused;
            case fir_tiles: return !geometry.fused && geometry.window;
            case fir_decimate: return !geometry.window;
            default: return true;
            }
        };
        for (std::size_t i = 0; i < kernels.size(); ++i) {
            if (!used(i)) continue;
            kernels[i] = owner->api.CreateKernel(owner->program, names[i], &status);
            check(status, "create coarse kernel");
        }
        staging.resize(geometry.staging_samples());
        peaks.resize(geometry.columns);
        downloaded.resize(geometry.shape.tile_frequencies * geometry.shape.workspaces);
    }

    template<class... Args>
    void dispatch(Kernel kernel, std::size_t global, Charge charge, OpenClCoarseWork& work, const Args&... args) {
        cl_uint index = 0;
        (check(owner->api.SetKernelArg(kernels[kernel], index++, sizeof(args), &args), "set coarse kernel argument"), ...);
        const auto threads = align(global, local_size);
        cl_event event = nullptr;
        check(owner->api.EnqueueNDRangeKernel(queue, kernels[kernel], 1, nullptr, &threads, &local_size,
                                              0, nullptr, profile ? &event : nullptr), "enqueue coarse kernel");
        pending = true;
        if (event) events.push_back({event, charge});
        ++work.dispatches;
    }

    template<class T>
    void upload(Buffer buffer, std::size_t offset, std::span<const T> input, OpenClCoarseWork& work) {
        check(owner->api.EnqueueWriteBuffer(queue, buffers[buffer], CL_TRUE, offset, input.size_bytes(), input.data(),
                                            0, nullptr, nullptr), "upload coarse data");
        work.upload_bytes += input.size_bytes();
        ++work.blocking_transfers;
    }

    template<class T>
    void download(Buffer buffer, std::span<T> output, OpenClCoarseWork& work) {
        check(owner->api.EnqueueReadBuffer(queue, buffers[buffer], CL_TRUE, 0, output.size_bytes(), output.data(),
                                           0, nullptr, nullptr), "read coarse results");
        pending = false;
        work.download_bytes += output.size_bytes();
        ++work.blocking_transfers;
        // The blocking read completed every earlier command of this queue.
        for (const auto& timed : events) {
            cl_ulong began = 0, ended = 0;
            if (owner->api.GetEventProfilingInfo(timed.event, CL_PROFILING_COMMAND_START, sizeof(began), &began, nullptr) == CL_SUCCESS &&
                owner->api.GetEventProfilingInfo(timed.event, CL_PROFILING_COMMAND_END, sizeof(ended), &ended, nullptr) == CL_SUCCESS &&
                ended >= began) {
                const auto seconds = static_cast<double>(ended - began) * 1e-9;
                work.*timed.charge.stage += seconds;
                if (timed.charge.part) work.*timed.charge.part += seconds;
            }
            (void)owner->api.ReleaseEvent(timed.event);
        }
        events.clear();
    }

    std::span<const std::complex<float>> narrowed(std::span<const std::complex<double>> input) {
        constexpr double largest = std::numeric_limits<float>::max();
        const auto output = std::span(staging).first(input.size());
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto value = input[i];
            if (!finite(value) || std::abs(value.real()) > largest || std::abs(value.imag()) > largest)
                throw Error("Robust OpenCL NVFP4 coarse input is nonfinite or outside FP32");
            output[i] = std::complex<float>(value);
        }
        return output;
    }

    // Peak scaling uses the resident FP32 columns, however they were made.
    void pack_bank_columns(OpenClCoarseWork& work) {
        const auto& shape = geometry.shape;
        const auto rows = kernel_extent(shape.kernel_rows), column_count = kernel_extent(geometry.columns);
        dispatch(column_peaks, geometry.columns * local_size, {&OpenClCoarseWork::packing_seconds}, work,
                 buffers[bank], buffers[bank_peaks], rows, column_count);
        download(bank_peaks, std::span(peaks), work);
        if (!std::ranges::all_of(peaks, [](float peak) { return std::isfinite(peak) && peak >= 0; }))
            throw Error("Robust OpenCL NVFP4 coarse template bank is nonfinite");
        dispatch(pack_bank, geometry.packed_rows * geometry.k, {&OpenClCoarseWork::packing_seconds}, work,
                 buffers[bank], buffers[bank_peaks], buffers[packed_bank],
                 rows, kernel_extent(geometry.k), column_count, kernel_extent(geometry.packed_rows));
        bank_ready = true;
        prepared = false;
    }

    void load(std::span<const std::span<const std::complex<double>>> input, OpenClCoarseWork& work, std::stop_token stop) {
        const auto rows = geometry.shape.kernel_rows;
        if (input.size() != geometry.columns ||
            !std::ranges::all_of(input, [&](const auto& column) { return column.size() == rows; }))
            throw Error("Robust OpenCL NVFP4 coarse bank extent differs from its geometry");
        for (std::size_t column = 0; column < input.size(); ++column) {
            cancelled(stop);
            upload(bank, column * rows * sizeof(std::complex<float>), narrowed(input[column]), work);
        }
        pack_bank_columns(work);
    }

    void synthesize(const CoarseTemplateSource& input, std::span<CoarseTemplateGram> grams, OpenClCoarseWork& work,
                    std::stop_token stop) {
        const auto& shape = geometry.shape;
        const auto raw_rows = shape.synthesis_raw_rows;
        const auto valid = [](double value) { return std::isfinite(value); };
        if (!raw_rows || grams.size() != shape.workspaces || input.columns.size() != geometry.columns ||
            input.raw_rows != raw_rows || input.source_rows != shape.synthesis_source_rows ||
            input.lanes != shape.synthesis_lanes || input.cells != shape.synthesis_cells ||
            input.chips.size() != shape.synthesis_lanes * shape.synthesis_cells * 3 ||
            input.pulse.size() != pulse_samples || input.taps.size() != shape.synthesis_taps ||
            input.decimation != shape.synthesis_decimation ||
            // Every output row needs a raw sample under its newest tap.
            (shape.kernel_rows - 1) * input.decimation >= raw_rows + shape.synthesis_taps - 1 ||
            !(input.rate_ratio > 0 && input.sample_rate_hz > 0 && input.fine_chip_rate_hz > 0 &&
              input.bit_seconds > 0 && input.envelope_edge_seconds > 0 && input.source_samples > 0) ||
            !valid(input.calendar_origin_sample) || !valid(input.center_offset) || !valid(input.carrier_spacing_hz) ||
            !(input.burst_period_tiles >= 0 && input.dwell_seconds >= 0) ||
            (input.burst_period_tiles > 0 && !(input.tile_seconds > 0 && input.tile_count > 0)) ||
            (input.dwell_seconds > 0 && !(input.ramp_seconds > 0 && valid(input.quiet_seconds))))
            throw Error("Robust OpenCL NVFP4 coarse synthesis differs from its geometry");
        std::vector<double> descriptors(geometry.columns * column_parameters);
        for (std::size_t column = 0; column < geometry.columns; ++column) {
            const auto& map = input.columns[column];
            if (!valid(map.first_position) || !(map.scale > 0) || !valid(map.first_cycles) || !valid(map.cycle_step) ||
                map.lane >= shape.synthesis_lanes)
                throw Error("Robust OpenCL NVFP4 coarse synthesis column is invalid");
            const std::array values{map.first_position, map.scale, map.first_cycles, map.cycle_step,
                                    static_cast<double>(map.lane)};
            std::ranges::copy(values, descriptors.begin() + column * column_parameters);
        }
        const std::array<double, source_parameters> parameters{
            input.rate_ratio, input.calendar_origin_sample, input.source_samples, input.sample_rate_hz,
            input.fine_chip_rate_hz, input.center_offset, static_cast<double>(shape.synthesis_cells),
            input.carrier_spacing_hz, input.bit_seconds, input.envelope_edge_seconds, input.burst_period_tiles,
            input.tile_seconds, input.tile_count, input.dwell_seconds, input.quiet_seconds, input.ramp_seconds};
        std::vector<float> filter(input.taps.size());
        std::ranges::transform(input.taps, filter.begin(), [](double tap) { return static_cast<float>(tap); });
        upload(chips, 0, input.chips, work);
        upload(pulse, 0, input.pulse, work);
        upload(source, 0, std::span<const double>(parameters), work);
        upload(Buffer::columns, 0, std::span<const double>(descriptors), work);
        upload(taps, 0, std::span<const float>(filter), work);
        const auto rows = shape.kernel_rows, source_rows = shape.synthesis_source_rows;
        for (std::size_t first = 0; first < geometry.columns;) {
            cancelled(stop);
            // Consecutive columns share a dispatch while their lanes fit
            // the resident renderings. A lane can be rendered again later.
            auto low = input.columns[first].lane, high = low;
            std::size_t count = 1;
            for (; first + count < geometry.columns && count < geometry.raw_batch; ++count) {
                const auto lane = input.columns[first + count].lane;
                if (std::max(high, lane) - std::min(low, lane) >= geometry.raster_lanes) break;
                low = std::min(low, lane);
                high = std::max(high, lane);
            }
            const auto lanes = high - low + 1;
            dispatch(render_lanes, lanes * source_rows, {&OpenClCoarseWork::synthesis_seconds,
                     &OpenClCoarseWork::render_seconds}, work,
                     buffers[chips], buffers[pulse], buffers[source], buffers[raster], kernel_extent(source_rows),
                     kernel_extent(lanes), kernel_extent(shape.synthesis_cells), kernel_extent(low));
            // A tiled FIR block filters one tile of output rows of one column.
            const auto tile_threads = count * geometry.row_tiles * local_size;
            const Charge filtering{&OpenClCoarseWork::synthesis_seconds, &OpenClCoarseWork::filter_seconds};
            if (geometry.fused) {
                dispatch(warp_fir, tile_threads, filtering, work, buffers[raster], buffers[Buffer::columns], buffers[taps],
                         buffers[bank], kernel_extent(source_rows), kernel_extent(raw_rows),
                         kernel_extent(shape.synthesis_taps), kernel_extent(input.decimation), kernel_extent(rows),
                         kernel_extent(count), kernel_extent(first), kernel_extent(low));
            } else {
                dispatch(warp_columns, count * raw_rows, filtering, work,
                         buffers[raster], buffers[Buffer::columns], buffers[raw], kernel_extent(source_rows),
                         kernel_extent(raw_rows), kernel_extent(count), kernel_extent(first), kernel_extent(low));
                dispatch(geometry.window ? fir_tiles : fir_decimate, geometry.window ? tile_threads : count * rows,
                         filtering, work, buffers[raw], buffers[taps], buffers[bank], kernel_extent(raw_rows),
                         kernel_extent(shape.synthesis_taps), kernel_extent(input.decimation), kernel_extent(rows),
                         kernel_extent(count), kernel_extent(first));
            }
            work.rendered_samples += lanes * source_rows;
            first += count;
        }
        dispatch(bank_gram, shape.workspaces * local_size, {&OpenClCoarseWork::synthesis_seconds,
                 &OpenClCoarseWork::gram_seconds}, work,
                 buffers[bank], buffers[gram], kernel_extent(rows), kernel_extent(shape.workspaces),
                 kernel_extent(shape.taps));
        std::vector<double> values(shape.workspaces * 4);
        download(gram, std::span(values), work);
        cancelled(stop);
        for (std::size_t workspace = 0; workspace < shape.workspaces; ++workspace) {
            const auto* entry = values.data() + workspace * 4;
            if (!std::all_of(entry, entry + 4, valid))
                throw Error("Robust OpenCL NVFP4 coarse synthesis produced a nonfinite template");
            grams[workspace] = {entry[0], entry[1], {entry[2], entry[3]}};
        }
        work.warped_samples += geometry.columns * raw_rows;
        work.filtered_rows += geometry.columns * rows;
        pack_bank_columns(work);
    }

    void prepare(std::span<const std::complex<double>> input, std::span<const std::array<double, 4>> matrix,
                 double projected_rate, double noise_variance, OpenClCoarseWork& work, std::stop_token stop) {
        const auto& shape = geometry.shape;
        if (!bank_ready || input.size() != geometry.samples || matrix.size() != shape.workspaces ||
            !(std::isfinite(projected_rate) && projected_rate > 0 && std::isfinite(noise_variance) && noise_variance > 0))
            throw Error("Robust OpenCL NVFP4 coarse preparation differs from its geometry");
        cancelled(stop);
        const auto samples = narrowed(input);
        float peak = 0;
        for (const auto value : samples) peak = std::max(peak, std::abs(value));
        if (!std::isfinite(peak)) throw Error("Robust OpenCL NVFP4 coarse observation exceeds FP32");
        upload(observation, 0, samples, work);
        rate = projected_rate;
        variance = noise_variance;
        reciprocal_observation = peak > 0 ? 1. / peak : 0.;
        std::vector<double> scaled(shape.workspaces * 4), exact(shape.workspaces * 4);
        for (std::size_t workspace = 0; workspace < shape.workspaces; ++workspace) {
            const auto& entry = matrix[workspace];
            if (!std::ranges::all_of(entry, [](double value) { return std::isfinite(value); }))
                throw Error("Robust OpenCL NVFP4 coarse inverse coefficients are nonfinite");
            // The packed operands carry their peaks and the full E2M1 and
            // block-scale range. Fold those and the variance into the form.
            const auto unit = static_cast<double>(peak) / (normalized_max * normalized_max);
            const auto first = peaks[workspace * shape.taps] * unit, second = peaks[workspace * shape.taps + 1] * unit;
            const std::array values{entry[0] * first * first / variance, entry[1] * second * second / variance,
                                    entry[2] * first * second / variance, entry[3] * first * second / variance};
            if (!std::ranges::all_of(values, [](double value) { return std::isfinite(value); }))
                throw Error("Robust OpenCL NVFP4 coarse score scale is outside FP64");
            std::ranges::copy(values, scaled.begin() + workspace * 4);
            std::ranges::copy(entry, exact.begin() + workspace * 4);
        }
        upload(coefficients, 0, std::span<const double>(scaled), work);
        upload(inverse, 0, std::span<const double>(exact), work);
        prepared = true;
    }

    void search(std::span<const double> frequencies, std::size_t separation, std::span<CoarseBulkPeaks> output,
                OpenClCoarseWork& work, std::stop_token stop) {
        const auto& shape = geometry.shape;
        const auto count = frequencies.size();
        const auto rows = count * shape.workspaces;
        if (!prepared || !count || count > shape.tile_frequencies || output.size() != rows || !separation ||
            !std::ranges::all_of(frequencies, [](double value) { return std::isfinite(value); }))
            throw Error("Robust OpenCL NVFP4 coarse frequency tile differs from its geometry");
        cancelled(stop);
        std::vector<double> parameters(count + 2);
        std::ranges::copy(frequencies, parameters.begin());
        parameters[count] = rate;
        parameters[count + 1] = reciprocal_observation;
        upload(pack_parameters, 0, std::span<const double>(parameters), work);
        parameters[count + 1] = variance;
        upload(score_parameters, 0, std::span<const double>(parameters), work);
        const auto frequency_count = kernel_extent(count), workspaces = kernel_extent(shape.workspaces);
        const auto arrivals = kernel_extent(shape.arrivals), samples = kernel_extent(geometry.samples);
        const auto kernel_rows = kernel_extent(shape.kernel_rows);
        dispatch(pack_observation, count * variants * geometry.variant_samples * 2, {&OpenClCoarseWork::packing_seconds},
                 work, buffers[observation], buffers[pack_parameters], buffers[packed_observation],
                 buffers[observation_scales], samples, kernel_extent(geometry.variant_samples), frequency_count);
        const auto blocks = count * geometry.pairs * geometry.arrival_blocks;
        dispatch(fp4_scores, blocks * local_size, {&OpenClCoarseWork::product_seconds}, work,
                 buffers[packed_observation], buffers[observation_scales], buffers[packed_bank],
                 buffers[coefficients], buffers[scores], kernel_extent(geometry.k),
                 kernel_extent(geometry.stream_quads), arrivals, workspaces, frequency_count);
        dispatch(select_peaks, rows * local_size, {&OpenClCoarseWork::selection_seconds}, work,
                 buffers[scores], buffers[frontier], arrivals, kernel_extent(separation));
        // Retained arrivals share one rotation of the observation per frequency.
        const Charge rescoring{&OpenClCoarseWork::selection_seconds, &OpenClCoarseWork::rescore_seconds};
        dispatch(rotate_observation, count * geometry.samples, rescoring, work, buffers[observation],
                 buffers[score_parameters], buffers[rotated], samples, frequency_count);
        dispatch(rescore_peaks, rows * 2 * local_size, rescoring, work,
                 buffers[bank], buffers[rotated], buffers[score_parameters], buffers[inverse], buffers[frontier],
                 kernel_rows, samples, workspaces, frequency_count, kernel_extent(shape.taps));
        const auto results = std::span(downloaded).first(rows);
        download(frontier, results, work);
        cancelled(stop);
        std::uint64_t retained = 0;
        for (std::size_t frequency = 0; frequency < count; ++frequency) {
            for (std::size_t workspace = 0; workspace < shape.workspaces; ++workspace) {
                const auto& row = results[frequency * shape.workspaces + workspace];
                if (row.count == no_peak) throw Error("Robust OpenCL NVFP4 coarse score is nonfinite");
                if (!row.count || row.count > 2 || (row.count == 2 && row.lags[0] == row.lags[1]))
                    throw Error("Robust OpenCL NVFP4 coarse frontier is invalid");
                for (std::size_t i = 0; i < row.count; ++i)
                    if (row.lags[i] >= shape.arrivals || !std::isfinite(row.scores[i]))
                        throw Error("Robust OpenCL NVFP4 coarse frontier arrival or score is invalid");
                retained += row.count;
                output[workspace * count + frequency] = row;
            }
        }
        work.issued_products += blocks * block_warps * groups * geometry.tiles * tile_products;
        work.useful_products += rows * shape.arrivals * 8 * shape.kernel_rows;
        work.score_rows += rows * shape.arrivals;
        work.rescored_peaks += retained;
    }
};

OpenClNvfp4Coarse::OpenClNvfp4Coarse(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OpenClNvfp4Coarse::~OpenClNvfp4Coarse() = default;

bool OpenClNvfp4Coarse::supports(const OpenClCoarseShape& shape) noexcept {
    // Two rays fill the eight result columns of one tile with two workspaces.
    const bool synthesis = shape.synthesis_raw_rows || shape.synthesis_source_rows || shape.synthesis_lanes ||
                           shape.synthesis_cells || shape.synthesis_taps || shape.synthesis_decimation;
    return shape.taps == 2 && shape.kernel_rows && shape.arrivals && shape.workspaces && shape.tile_frequencies &&
           shape.kernel_rows <= std::numeric_limits<std::uint32_t>::max() / 4 &&
           shape.arrivals <= std::numeric_limits<std::uint32_t>::max() / 4 &&
           (!synthesis || (shape.synthesis_raw_rows && shape.synthesis_source_rows && shape.synthesis_lanes &&
                           shape.synthesis_cells && shape.synthesis_taps && shape.synthesis_decimation));
}

OpenClNvfp4Workspace OpenClNvfp4Coarse::workspace(const OpenClCoarseShape& shape) {
    const Geometry geometry(shape);
    return {add(sizeof(Impl) + sizeof(OpenClNvfp4Coarse), geometry.host_bytes()), geometry.device_bytes()};
}

void OpenClNvfp4Coarse::load_bank(std::span<const std::span<const std::complex<double>>> columns,
                                  OpenClCoarseWork& work, std::stop_token stop) {
    auto completed = work;
    // A partly replaced bank is neither resident nor searchable.
    try { impl_->load(columns, completed, stop); }
    catch (...) { impl_->drain(); impl_->bank_ready = impl_->prepared = false; throw; }
    work = completed;
}

void OpenClNvfp4Coarse::synthesize_bank(const CoarseTemplateSource& synthesis, std::span<CoarseTemplateGram> grams,
                                        OpenClCoarseWork& work, std::stop_token stop) {
    auto completed = work;
    try { impl_->synthesize(synthesis, grams, completed, stop); }
    catch (...) { impl_->drain(); impl_->bank_ready = impl_->prepared = false; throw; }
    work = completed;
}

void OpenClNvfp4Coarse::prepare(std::span<const std::complex<double>> observation,
                                std::span<const std::array<double, 4>> inverse, double projected_rate_hz,
                                double variance, OpenClCoarseWork& work, std::stop_token stop) {
    auto completed = work;
    try { impl_->prepare(observation, inverse, projected_rate_hz, variance, completed, stop); }
    catch (...) { impl_->drain(); impl_->prepared = false; throw; }
    work = completed;
}

void OpenClNvfp4Coarse::peaks(std::span<const double> frequencies, std::size_t separation,
                              std::span<CoarseBulkPeaks> output, OpenClCoarseWork& work, std::stop_token stop) {
    // A failed tile publishes neither frontiers nor work.
    auto completed = work;
    try { impl_->search(frequencies, separation, output, completed, stop); } catch (...) { impl_->drain(); throw; }
    work = completed;
}

void OpenClNvfp4Coarse::read_bank(std::span<std::complex<float>> columns) {
    if (!impl_->bank_ready || columns.size() != impl_->geometry.columns * impl_->geometry.shape.kernel_rows)
        throw Error("Robust OpenCL NVFP4 coarse bank is not resident");
    OpenClCoarseWork ignored;
    try { impl_->download(bank, columns, ignored); } catch (...) { impl_->drain(); throw; }
}

void OpenClNvfp4Coarse::read_scores(std::span<float> values) {
    if (!impl_->prepared || values.size() > impl_->geometry.sizes[scores] / sizeof(float))
        throw Error("Robust OpenCL NVFP4 coarse scores are not resident");
    OpenClCoarseWork ignored;
    try { impl_->download(scores, values, ignored); } catch (...) { impl_->drain(); throw; }
}

std::unique_ptr<OpenClNvfp4Coarse> OpenClNvfp4Device::make_coarse(const OpenClCoarseShape& shape,
        std::size_t workspace_limit, bool profile) const {
    if (!OpenClNvfp4Coarse::supports(shape)) return {};
    const Geometry geometry(shape);
    const auto bytes = geometry.device_bytes();
    if (bytes > workspace_limit || bytes > impl_->global_memory ||
        *std::ranges::max_element(geometry.sizes) > impl_->maximum_allocation) return {};
    try {
        auto coarse = std::make_unique<OpenClNvfp4Coarse::Impl>(impl_, geometry, profile);
        coarse->initialize();
        return std::unique_ptr<OpenClNvfp4Coarse>(new OpenClNvfp4Coarse(std::move(coarse)));
    } catch (const OpenClError& error) {
        if (allocation_failure(error.code)) return {};
        throw;
    }
}

} // namespace datapump::triad::v3::receiver_detail
