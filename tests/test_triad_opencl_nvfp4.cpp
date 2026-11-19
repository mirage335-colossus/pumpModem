#include "datapump/host/sandbox.hpp"
#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"
#include "triad_receiver_bulk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace datapump::triad::v3::receiver_detail;
using Sample = std::complex<float>;
using Reference = std::complex<double>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<Sample> samples(std::size_t rows, std::size_t lanes, std::size_t tone_bin, unsigned seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> amplitude(-1, 1);
    const auto stride = rows + 1;
    std::vector<Sample> values(lanes * stride);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const float gain = std::ldexp(1.0f, 8 * (static_cast<int>(lane % 16) - 7));
        float peak = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            const float phase = 2 * std::numbers::pi_v<float> * ((i * tone_bin) % rows) / rows + .7f;
            const auto value = gain * (lane == 0 ? std::polar(1.0f, phase) : Sample{amplitude(random), amplitude(random)});
            values[lane * stride + i] = value;
            peak = std::max({peak, std::abs(value.real()), std::abs(value.imag())});
        }
        values[lane * stride + rows] = {peak, 0};
    }
    return values;
}

double compare_projection(std::span<const Sample> input, std::span<const Sample> output,
                          std::span<const std::size_t> bins, std::size_t rows) {
    const auto normalization = 1 / std::sqrt(static_cast<double>(rows));
    const auto stride = rows + 1;
    const auto lanes = input.size() / stride;
    std::vector<double> errors(lanes), energy(lanes);
    std::vector<Reference> coefficients(rows);
    for (std::size_t mode = 0; mode < bins.size(); ++mode) {
        for (std::size_t i = 0; i < rows; ++i) {
            const double phase = -2 * std::numbers::pi * ((i * bins[mode]) % rows) / rows;
            coefficients[i] = std::polar(normalization, phase);
        }
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            Reference expected{};
            for (std::size_t i = 0; i < rows; ++i)
                expected += Reference(input[lane * stride + i]) * coefficients[i];
            errors[lane] += std::norm(expected - Reference(output[lane * bins.size() + mode]));
            energy[lane] += std::norm(expected);
        }
    }
    double worst = 0;
    for (std::size_t lane = 0; lane < lanes; ++lane) worst = std::max(worst, std::sqrt(errors[lane] / energy[lane]));
    return worst;
}

void check_projection(const OpenClNvfp4Device& device, std::size_t rows, std::size_t modes, std::size_t lanes) {
    std::vector<std::size_t> bins(modes);
    for (std::size_t mode = 0; mode < modes; ++mode) bins[mode] = (rows + mode * 17 - 3) % rows;
    const auto capacity = lanes + 7;
    const auto workspace = OpenClNvfp4Projection::workspace(rows, modes, capacity);
    require(workspace.host_bytes && workspace.device_bytes, "Projection workspace is empty");
    require(!device.make_projection(rows, bins, capacity, workspace.device_bytes - 1),
            "Projection exceeded its quoted workspace");
    auto projection = device.make_projection(rows, bins, capacity, workspace.device_bytes);
    require(static_cast<bool>(projection), "OpenCL NVFP4 projection allocation failed");
    require(!projection->project({}, {}), "An empty FP4 batch submitted work");

    const auto input = samples(rows, lanes, bins[1], 313);
    std::vector<Sample> output(lanes * modes);
    require(projection->project(input, output), "FP4 projection failed");
    const auto worst = compare_projection(input, output, bins, rows);
    require(worst < .26, "FP4 projection differs from the complex DFT reference");
    require(std::abs(static_cast<double>(std::arg(output[1])) - .7) < .12,
            "FP4 projection changed the complex tone phase");

    const auto first_result = output;
    require(projection->mapped_staging((rows + 1) * capacity + 1).empty(),
            "Pinned FP4 staging exceeded its capacity");
    auto mapped = projection->mapped_staging(input.size());
    if (!mapped.empty()) {
        std::ranges::copy(input, mapped.begin());
        require(projection->project(mapped, output) && output == first_result,
                "Pinned FP4 staging differs from pageable input");
        std::stop_source cancellation;
        cancellation.request_stop();
        bool cancelled = false;
        try {
            projection->project(mapped, output, cancellation.get_token());
        } catch (const std::runtime_error& error) {
            cancelled = std::string_view(error.what()).find("cancelled") != std::string_view::npos;
        }
        require(cancelled && output == first_result, "Canceled pinned FP4 work changed output");
        require(projection->mapped_staging(input.size()).data() == mapped.data(),
                "The pinned FP4 staging pointer changed after cancellation");
        require(projection->project(mapped, output) && output == first_result,
                "The pinned FP4 buffer cannot be reused after cancellation");
        std::cout << "OpenCL pinned staging verified\n";
    }
    auto bad_peak = input;
    bad_peak[rows] = {std::numeric_limits<float>::quiet_NaN(), 0};
    bool rejected = false;
    try {
        projection->project(bad_peak, output);
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).find("peak") != std::string_view::npos;
    }
    require(rejected && output == first_result, "Nonfinite FP4 peak did not fail before publishing results");

    const std::vector<Sample> zero((rows + 1) * (lanes - 1));
    const auto partial_output = std::span(output).first((lanes - 1) * modes);
    require(projection->project(zero, partial_output), "Zero FP4 projection failed");
    require(std::ranges::all_of(partial_output, [](Sample value) { return value == Sample{}; }),
            "FP4 projection reused stale nonzero values");
    require(projection->project(input, output) && output == first_result,
            "FP4 projection changed after reusing its buffers");

    const auto other = samples(rows, lanes - 1, bins[1], 211);
    require(projection->project(other, partial_output), "Changed partial FP4 batch failed");
    const auto partial_error = compare_projection(other, partial_output, bins, rows);
    require(partial_error < .26, "Partial FP4 batch differs from the complex DFT reference");
    std::cout << "OpenCL NVFP4 " << rows << " rows, " << modes << " bins, " << lanes << " lanes. Worst RMS "
              << std::max(worst, partial_error) << '\n';
}

void check_resident_projection(const OpenClNvfp4Device& device) {
    constexpr std::size_t rows = 1500, modes = 47, taps = 2, candidates = 3, blocks_count = 2;
    constexpr std::size_t columns = 2 * taps, lanes = blocks_count * candidates * columns;
    constexpr std::int64_t tile_size = 8192;
    using Tile = std::array<Reference, tile_size>;
    const auto first_tile = std::make_shared<Tile>();
    const auto second_tile = std::make_shared<Tile>();
    const auto other_first_tile = std::make_shared<Tile>();
    const auto other_second_tile = std::make_shared<Tile>();
    const auto phasor = [](double cycles) { return std::polar(1., 2 * std::numbers::pi * cycles); };
    for (std::int64_t i = 0; i != 2 * tile_size; ++i) {
        auto& tile = i < tile_size ? *first_tile : *second_tile;
        auto& other = i < tile_size ? *other_first_tile : *other_second_tile;
        tile[i % tile_size] = .17 * phasor(7. * i / rows) + .08 * phasor(-11. * i / rows);
        other[i % tile_size] = .13 * phasor(19. * i / rows + .08) + .06 * phasor(-5. * i / rows);
    }
    std::array<double, rows> offsets;
    std::iota(offsets.begin(), offsets.end(), 0.);
    std::array<std::size_t, modes> bins;
    for (std::size_t m = 0; m != modes; ++m) bins[m] = (rows - 23 + m) % rows;
    std::array<std::array<Reference, modes>, blocks_count> observations;
    std::array<std::array<FineBulkStatistics, candidates>, blocks_count> output;
    std::array<FineResidentBlock, blocks_count> blocks;
    std::vector<Sample> mapped(lanes * (rows + 1));
    bool separate_mappings = false;
    for (std::size_t b = 0; b != blocks_count; ++b) {
        for (std::size_t m = 0; m != modes; ++m)
            observations[b][m] = (.08 + .006 * (m % 7)) * phasor(.07 * m + .19 * b);
        blocks[b] = {{rows, taps, bins, observations[b], .7 + .8 * b, true},
            [&, b](std::size_t candidate, std::size_t first, std::size_t count, const BulkMappedSink& sink) {
                require(candidate < candidates && first == 0 && count == rows,
                        "Resident projection changed the candidate row extent");
                if (candidate == candidates - 1) return;
                // Columns 0 and 2 share the stencil across a source tile
                // boundary, with different source samples for the two bits.
                // Column 1 exercises a different map and the clipped start.
                const auto phase = .05 * candidate - .12 * b;
                std::array<BulkMappedColumn, 3> mappings{{
                    {.375, 1.002, .05 * candidate, .0007, -2, 1, {{first_tile, *first_tile}}},
                    {.53125, .997, phase, -.0009, tile_size - 613, 0,
                        {{first_tile, *first_tile}, {second_tile, *second_tile}}},
                    {.53125, .997, phase, -.0009, tile_size - 613, 2,
                        {{other_first_tile, *other_first_tile}, {other_second_tile, *other_second_tile}}}
                }};
                if (separate_mappings) std::swap(mappings[0], mappings[1]);
                std::array<Reference, rows> direct;
                for (std::size_t i = 0; i != rows; ++i)
                    direct[i] = .12 * phasor(-7. * i / rows + .04 * candidate + .23 * b);
                const std::array<BulkDirectColumn, 1> fallback{{{3, direct, "test-native-direct"}}};
                sink(offsets, mappings, fallback);
            }, output[b]};

        // Render the same source independently for the exact DFT and the
        // existing staged NVFP4 projection, with C64 samples at their inputs.
        for (std::size_t candidate = 0; candidate != candidates; ++candidate) {
            blocks[b].source(candidate, 0, rows,
                [&](std::span<const double> coordinates, std::span<const BulkMappedColumn> mappings,
                    std::span<const BulkDirectColumn> direct) {
                    const auto store = [&](std::size_t column, auto sample) {
                        const auto lane = (b * candidates + candidate) * columns + column;
                        auto dest = std::span(mapped).subspan(lane * (rows + 1), rows + 1);
                        float peak = 0;
                        for (std::size_t i = 0; i != rows; ++i) {
                            dest[i] = Sample(sample(i));
                            peak = std::max({peak, std::abs(dest[i].real()), std::abs(dest[i].imag())});
                        }
                        dest.back() = {peak, 0};
                    };
                    for (const auto& mapping : mappings) store(mapping.output_column, [&](std::size_t i) {
                        const auto relative = mapping.scale * coordinates[i] + mapping.fraction;
                        const auto integral = std::floor(relative);
                        const auto center = mapping.origin + static_cast<std::int64_t>(integral);
                        Reference value{};
                        for (int j = -1; j <= 2; ++j) {
                            const auto index = center + j;
                            if (index < 0 || index / tile_size >= static_cast<std::int64_t>(mapping.tiles.size()))
                                continue;
                            double weight = 1;
                            for (int k = -1; k <= 2; ++k)
                                if (k != j) weight *= (relative - integral - k) / (j - k);
                            value += weight * mapping.tiles[index / tile_size].samples[index % tile_size];
                        }
                        return value * phasor(mapping.phase + mapping.phase_step * coordinates[i]);
                    });
                    for (const auto& column : direct)
                        store(column.output_column, [&](std::size_t i) { return column.samples[i]; });
                });
        }
    }

    std::vector<Sample> fp4_reference(lanes * modes), fft_reference(lanes * modes);
    {
        const auto limit = OpenClNvfp4Projection::workspace(rows, modes, lanes).device_bytes;
        const auto staged = device.make_projection(rows, bins, lanes, limit);
        require(staged && staged->project(mapped, fp4_reference), "Could not make staged resident-test reference");
    }
    for (std::size_t m = 0; m != modes; ++m) {
        for (std::size_t lane = 0; lane != lanes; ++lane) {
            Reference sum{};
            for (std::size_t i = 0; i != rows; ++i)
                sum += Reference(mapped[lane * (rows + 1) + i]) * phasor(-double((bins[m] * i) % rows) / rows);
            fft_reference[lane * modes + m] = Sample(sum / std::sqrt(double(rows)));
        }
    }

    for (const auto method : {OpenClFineMethod::resident_fp4, OpenClFineMethod::resident_fft}) {
        const bool fp4 = method == OpenClFineMethod::resident_fp4;
        const auto& reference = fp4 ? fp4_reference : fft_reference;
        const double tolerance = fp4 ? 3e-5 : 8e-6;
        const auto check_block = [&](std::size_t b) {
            const auto near = [&](Reference actual, Reference expected) {
                if (expected == Reference{}) {
                    require(actual == Reference{}, "Resident projection changed an exactly zero statistic");
                    return;
                }
                require(std::abs(actual - expected) <= tolerance * std::max(1., std::abs(expected)),
                        fp4 ? "Resident FP4 statistics differ from staged NVFP4"
                            : "Resident FFT statistics differ from the independent complex DFT");
            };
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& result = output[b][c];
                require(result.nonzero_basis == (c != candidates - 1), "Resident basis presence witness changed");
                require(std::ranges::all_of(std::span(result.correlations).first(taps),
                                            [](Reference z) { return z == Reference{}; }) &&
                        std::ranges::all_of(std::span(result.gram).first(taps * taps),
                                            [](Reference z) { return z == Reference{}; }),
                        "Resident projection changed the exact null hypothesis");
                for (std::size_t h = 1; h <= 2; ++h) {
                    const auto first_lane = (b * candidates + c) * columns + (h - 1) * taps;
                    for (std::size_t x = 0; x != taps; ++x) {
                        Reference correlation{};
                        for (std::size_t m = 0; m != modes; ++m)
                            correlation += std::conj(Reference(reference[(first_lane + x) * modes + m])) *
                                           observations[b][m];
                        near(result.correlations[h * taps + x], correlation / blocks[b].shape.variance);
                        for (std::size_t y = 0; y != taps; ++y) {
                            Reference gram{};
                            for (std::size_t m = 0; m != modes; ++m)
                                gram += std::conj(Reference(reference[(first_lane + x) * modes + m])) *
                                        Reference(reference[(first_lane + y) * modes + m]);
                            near(result.gram[(h * taps + x) * taps + y], gram / blocks[b].shape.variance);
                        }
                    }
                }
            }
        };
        const auto limit = OpenClNvfp4Projection::workspace(rows, modes, lanes, method).device_bytes;
        require(limit > 0 && !device.make_projection(rows, bins, lanes, limit - 1, method),
                "Resident projection exceeded its quoted workspace");
        const auto resident = device.make_projection(rows, bins, lanes, limit, method);
        OpenClResidentWork paired;
        require(resident && resident->project_resident(blocks, candidates, paired), "Paired resident projection failed");
        require(paired.uploaded_tiles == 4 && paired.tile_hits > 0 && paired.mapped_columns == 12 &&
                    paired.direct_columns == 4 && paired.upload_bytes > 0 && paired.download_bytes > 0,
                "Paired resident projection did not use cached tiles and mapped/direct lanes");
        require(paired.setup_upload_bytes == modes * sizeof(std::int64_t) &&
                    paired.observation_upload_bytes == blocks_count * modes * sizeof(Reference),
                "Paired resident projection transfer receipt omitted basis setup or observation bytes");
        for (std::size_t b = 0; b != blocks_count; ++b) check_block(b);

        OpenClResidentWork shared, separate;
        require(resident->project_resident(blocks, candidates, shared), "Warm shared mapper failed");
        const auto shared_statistics = output;
        separate_mappings = true;
        require(resident->project_resident(blocks, candidates, separate), "Warm single-column mapper failed");
        separate_mappings = false;
        require(shared.uploaded_tiles == 0 && separate.uploaded_tiles == 0 &&
                    shared.mapped_columns == separate.mapped_columns &&
                    shared.upload_bytes < separate.upload_bytes && shared.download_bytes == separate.download_bytes,
                "Paired mapper did not reduce descriptor bytes while preserving work");
        for (std::size_t b = 0; b != blocks_count; ++b) {
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& expected = shared_statistics[b][c];
                const auto& actual = output[b][c];
                require(expected.correlations == actual.correlations && expected.gram == actual.gram &&
                            expected.nonzero_basis == actual.nonzero_basis,
                        "Sharing the mapped stencil changed the computed receiver statistics");
            }
        }

        const auto previous = observations[1];
        const auto variance = blocks[1].shape.variance;
        observations[1].fill({});
        blocks[1].shape.variance *= 2;
        OpenClResidentWork replay;
        require(resident->project_resident(std::span(blocks).last(1), candidates, replay),
                "Single resident block could not reuse its paired workspace");
        require(replay.uploaded_tiles == 0 && replay.tile_hits > 0 && replay.download_bytes < paired.download_bytes,
                "Resident replay leaked a block or reuploaded unchanged tiles");
        require(replay.setup_upload_bytes == 0 && replay.observation_upload_bytes == modes * sizeof(Reference),
                "Resident replay counted initial basis setup again");
        check_block(1);
        observations[1] = previous;
        blocks[1].shape.variance = variance;

        const auto before_cancel = output;
        std::stop_source cancellation;
        auto cancelling = blocks;
        cancelling[1].source = [&](std::size_t c, std::size_t first, std::size_t count, const BulkMappedSink& sink) {
            blocks[1].source(c, first, count, sink);
            cancellation.request_stop();
        };
        OpenClResidentWork cancelled_work;
        bool cancelled = false;
        try {
            resident->project_resident(cancelling, candidates, cancelled_work, cancellation.get_token());
        } catch (const std::runtime_error& error) {
            cancelled = std::string_view(error.what()).find("cancelled") != std::string_view::npos;
        }
        require(cancelled && cancelled_work.dispatches == 0 && cancelled_work.upload_bytes == 0,
                "Cancelled resident work published a receipt");
        for (std::size_t b = 0; b != blocks_count; ++b) {
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& before = before_cancel[b][c];
                const auto& after = output[b][c];
                require(after.correlations == before.correlations && after.gram == before.gram &&
                            after.nonzero_basis == before.nonzero_basis,
                        "Cancelled resident work published partial statistics");
            }
        }
        require(resident->project_resident(blocks, candidates, replay), "Resident projection failed after cancellation");
        for (std::size_t b = 0; b != blocks_count; ++b) check_block(b);

        const Reference tiny{1e-100, -1e-100};
        const auto tiny_tile = std::make_shared<Tile>();
        const auto zero_tile = std::make_shared<Tile>();
        tiny_tile->fill(tiny);
        std::array<Reference, rows> tiny_direct;
        tiny_direct.fill(tiny);
        auto underflow = blocks.front();
        underflow.source = [&](std::size_t c, std::size_t, std::size_t, const BulkMappedSink& sink) {
            if (c == 0) {
                const std::array<BulkMappedColumn, 2> columns{{
                    {0., 1., 0., 0., 2, 0, {{zero_tile, *zero_tile}}},
                    {0., 1., 0., 0., 2, 2, {{tiny_tile, *tiny_tile}}}
                }};
                sink(offsets, columns, {});
            } else if (c == 1) {
                const std::array<BulkDirectColumn, 1> column{{{0, tiny_direct, "test-native-direct"}}};
                sink(offsets, {}, column);
            }
        };
        require(resident->project_resident(std::span(&underflow, 1), candidates, replay),
                "Resident projection failed on finite samples below C64 range");
        for (std::size_t c = 0; c != candidates; ++c) {
            const auto& result = output[0][c];
            require(result.nonzero_basis == (c != candidates - 1) &&
                        std::ranges::all_of(result.correlations, [](Reference z) { return z == Reference{}; }) &&
                        std::ranges::all_of(result.gram, [](Reference z) { return z == Reference{}; }),
                    "Resident projection lost native basis presence below C64 range");
        }
        std::cout << "OpenCL resident " << (fp4 ? "NVFP4" : "FFT") << " paired and single fine statistics verified\n";
    }
}
} // namespace

int main() {
    try {
        constexpr std::array descriptors{0, 1, 2};
        datapump::host::restrict_descriptors(descriptors);
        datapump::host::install_no_socket_boundary();
        const auto ptx = std::as_bytes(std::span(opencl_nvfp4_ptx));
        std::array<std::byte, 16> other_uuid{};
        other_uuid.fill(std::byte{0xab});
        std::string reason;
        require(!OpenClNvfp4Device::open(other_uuid, ptx, &reason) && !reason.empty(),
                "OpenCL selected a GPU with a different UUID");
        const std::array<std::byte, 16> any_uuid{};
        const auto device = OpenClNvfp4Device::open(any_uuid, ptx, &reason);
        if (!device) throw std::runtime_error(reason);
        check_projection(*device, 1500, 47, 33);
        check_projection(*device, 223, 67, 129);
        check_resident_projection(*device);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
