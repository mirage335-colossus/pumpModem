#include "datapump/host/sandbox.hpp"
#include "datapump/triad_shaping.hpp"
#include "triad_fixture_key.hpp"
#include "triad_opencl_nvfp4_coarse.hpp"
#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"
#include "triad_receiver_basis.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_coarse.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
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

    // The FP4 method maps with the FP64 and the FP32 kernels, the FFT method
    // with the FP32 kernels, whose FFT result the FP64 mapper repeated to the
    // digit. The FP64 mapper agrees with the host mapping within 1e-12 of a
    // lane's peak, so FP4 rounds the same elements. The FP32 mapper is
    // within FP32 rounding of it, below 1e-6 of a lane's peak, which can move
    // an element across an E2M1 boundary: one such element moves a statistic
    // by less than 2e-3, the bound of the tile checks below.
    for (const auto [method, arithmetic] : {std::pair{OpenClFineMethod::resident_fp4, OpenClFineArithmetic::fp64},
                                            std::pair{OpenClFineMethod::resident_fp4, OpenClFineArithmetic::fp32},
                                            std::pair{OpenClFineMethod::resident_fft, OpenClFineArithmetic::fp32}}) {
        const bool fp4 = method == OpenClFineMethod::resident_fp4, fp32 = arithmetic == OpenClFineArithmetic::fp32;
        const auto& reference = fp4 ? fp4_reference : fft_reference;
        const double tolerance = fp4 ? (fp32 ? 2e-3 : 3e-5) : 8e-6;
        double worst = 0;
        const auto check_block = [&](std::size_t b) {
            const auto near = [&](Reference actual, Reference expected) {
                if (expected == Reference{}) {
                    require(actual == Reference{}, "Resident projection changed an exactly zero statistic");
                    return;
                }
                const auto difference = std::abs(actual - expected) / std::max(1., std::abs(expected));
                worst = std::max(worst, difference);
                require(difference <= tolerance,
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
        OpenClResidentOptions options;
        options.arithmetic = arithmetic;
        const auto limit = OpenClNvfp4Projection::workspace(rows, modes, lanes, method, options).device_bytes;
        require(limit > 0 && !device.make_projection(rows, bins, lanes, limit - 1, method, options),
                "Resident projection exceeded its quoted workspace");
        const auto resident = device.make_projection(rows, bins, lanes, limit, method, options);
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
        std::cout << "OpenCL resident " << (fp4 ? "NVFP4" : "FFT") << (fp32 ? " FP32" : " FP64")
                  << " mapping: paired and single fine statistics verified, worst difference " << worst << '\n';
    }
}
// A resident session that renders its dense source tiles from the keyed
// coefficients must hold the native tiles, and give the statistics of the
// session that uploads them.
void check_resident_tiles(const OpenClNvfp4Device& device, const datapump::triad::v3::Format& format,
                          double receive_rate, bool compare_statistics, const char* label) {
    using namespace datapump::triad::v3;
    constexpr std::size_t rows = 1500, modes = 47, taps = 2, candidates = 2, tile = 8192;
    constexpr std::size_t lanes_per_block = 2 * taps * candidates;
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i != seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(41 * i + 9);
    const auto key = datapump::triad::fixture::key(seed);
    ReceiverConfig config;
    config.sample_rate_hz = receive_rate;
    config.data_if_hz = 1725;
    config.data_basis_gain = .83;
    ReceiverSlotPlan slot;
    slot.format = format;
    MappedBasis basis(config, slot, key, rows, 1);
    const auto* source = basis.tile_source();
    require(source && source->tile_samples == tile && source->cells == geometry(format).fine_cells &&
            source->dense_samples > 3 * tile, "Mapped basis did not describe its dense tile source");
    const ConsecutiveBasisOffsets offsets(rows);
    const std::array<Tap, taps> paths{{{0, 0}, {.0113, 7.5}}};
    // Two keyed epochs, with opposite clock offsets.
    const std::array<MappedBasisRow, candidates> maps{{{0, .004, .35, 12.5}, {2, -.011, -.2, -31.25}}};
    // The start of the dense source, a tile boundary, its interior, its
    // partial last tile, and a block wholly past its support.
    const auto dense = source->dense_samples;
    const std::array<std::uint64_t, 6> firsts{0, tile - 700, dense / 3, dense / 2 + 311, dense - 900, dense + 5 * tile};
    const auto blocks = firsts.size();
    std::array<std::size_t, modes> bins;
    for (std::size_t m = 0; m != modes; ++m) bins[m] = (rows - 23 + m) % rows;
    std::mt19937 random(7719);
    std::normal_distribution<double> noise(0, .1);
    std::vector<std::array<Reference, modes>> observed(blocks);
    for (auto& block : observed)
        for (auto& value : block) value = {noise(random), noise(random)};

    using Statistics = std::vector<std::array<FineBulkStatistics, candidates>>;
    std::map<std::pair<std::uint32_t, std::uint64_t>, std::vector<Reference>> native_tiles;
    const auto requests = [&](bool rendered, Statistics& output) {
        std::vector<FineResidentBlock> list;
        for (std::size_t b = 0; b != blocks; ++b) {
            list.push_back({{rows, taps, bins, observed[b], .7 + .1 * b, true},
                [&, b, rendered](std::size_t candidate, std::size_t first, std::size_t count, const BulkMappedSink& sink) {
                    require(candidate < candidates && first == 0 && count == rows,
                            "Resident tile check changed the candidate row extent");
                    if (rendered) {
                        basis.address_pair(maps[candidate], paths, firsts[b], offsets, rows, sink);
                        return;
                    }
                    basis.visit_pair(maps[candidate], paths, firsts[b], offsets, rows,
                        [&](std::span<const double> coordinates, std::span<const BulkMappedColumn> mapped,
                            std::span<const BulkDirectColumn> direct) {
                            for (const auto& column : mapped) {
                                require(column.tiles.size() == column.address.count, "Native column lost its tile address");
                                for (std::size_t i = 0; i != column.tiles.size(); ++i) {
                                    const auto samples = column.tiles[i].samples;
                                    native_tiles.try_emplace({column.address.lane, column.address.first + i},
                                                             samples.begin(), samples.end());
                                }
                            }
                            sink(coordinates, mapped, direct);
                        });
                }, output[b], rendered ? source : nullptr});
        }
        return list;
    };

    // The FP64 renderer keeps the native renderer's operations, within
    // 1e-10 of a tile's peak, and its statistics match the session that
    // uploads native tiles. The FP32 renderer sums at most 17 chips of three
    // carriers in FP32 from FP32 pulse and chip values, so each sample carries
    // a few tens of FP32 roundings relative to its terms: a declared bound of
    // 4e-6 of the tile peak. Mapped values then move by at most the sum of
    // the absolute cubic weights, 1.25, times that, and statistics, bilinear
    // in the templates, by at most twice their relative change: within 1e-5
    // of the statistic scale for the FFT route. FP4 rounds the mapped values
    // next, and one changed E2M1 element of a 3,000-element lane moves a
    // statistic by less than 2e-3. Each FP32 session is compared with the
    // native tiles and the FP64 session just before it.
    std::map<OpenClFineArithmetic, double> worst_tiles, worst_statistics;
    double fp32_fp4 = 0, fp32_fft = 0;
    Statistics control(blocks);
    OpenClResidentWork control_work;
    for (const auto [method, arithmetic] : {std::pair{OpenClFineMethod::resident_fp4, OpenClFineArithmetic::fp64},
                                            std::pair{OpenClFineMethod::resident_fp4, OpenClFineArithmetic::fp32},
                                            std::pair{OpenClFineMethod::resident_fft, OpenClFineArithmetic::fp64},
                                            std::pair{OpenClFineMethod::resident_fft, OpenClFineArithmetic::fp32}}) {
        const bool fp4 = method == OpenClFineMethod::resident_fp4, fp32 = arithmetic == OpenClFineArithmetic::fp32;
        if (!fp4 && !compare_statistics) continue;
        auto& worst_tile = worst_tiles[arithmetic];
        OpenClResidentOptions rendering{blocks, source->cells, source->lanes};
        rendering.arithmetic = arithmetic;
        const auto limit = OpenClNvfp4Projection::workspace(rows, modes, blocks * lanes_per_block, method, rendering).device_bytes;
        require(!device.make_projection(rows, bins, blocks * lanes_per_block, limit - 1, method, rendering),
                "Tile-rendering session exceeded its quoted workspace");
        const auto session = device.make_projection(rows, bins, blocks * lanes_per_block, limit, method, rendering);
        require(static_cast<bool>(session), "Could not make the tile-rendering resident session");
        Statistics rendered(blocks), uploaded(blocks), repeated(blocks);
        OpenClResidentWork work;
        {
            const auto list = requests(true, rendered);
            require(session->project_resident(list, candidates, work), "Tile-rendering resident batch failed");
        }
        require(work.rendered_tiles > 0 && work.uploaded_tiles == 0 && work.keyed_lanes == 4 &&
                work.direct_columns == 0 && work.mapped_columns == (blocks - 1) * lanes_per_block,
                "Tile-rendering batch uploaded samples or lost a mapped column");

        if (!fp32) {
            OpenClResidentOptions uploading{blocks};
            uploading.arithmetic = arithmetic;
            const auto reference = device.make_projection(rows, bins, blocks * lanes_per_block,
                OpenClNvfp4Projection::workspace(rows, modes, blocks * lanes_per_block, method, uploading).device_bytes,
                method, uploading);
            OpenClResidentWork native_work;
            native_tiles.clear();
            {
                const auto list = requests(false, uploaded);
                require(reference && reference->project_resident(list, candidates, native_work),
                        "Uploaded-tile resident batch failed");
            }
            // The rendering session uploads keyed coefficients, the pulse, the
            // public parameters and one request per tile in place of the tiles.
            const auto keyed_bytes = work.keyed_lanes * source->cells * 3 * sizeof(Reference) +
                source->pulse.size() * sizeof(double) + 18 * sizeof(double) + work.rendered_tiles * 16;
            require(native_work.uploaded_tiles == work.rendered_tiles && native_work.rendered_tiles == 0 &&
                    native_work.mapped_columns == work.mapped_columns && native_work.tile_hits == work.tile_hits &&
                    native_work.upload_bytes - native_work.uploaded_tiles * tile * sizeof(Reference) ==
                        work.upload_bytes - keyed_bytes &&
                    native_work.download_bytes == work.download_bytes,
                    "Tile rendering did not replace exactly the uploaded tiles");
            if (fp4) std::cout << "OpenCL resident tiles " << label << ": " << work.rendered_tiles << " rendered, "
                               << work.upload_bytes << " bytes uploaded against " << native_work.upload_bytes << '\n';
        } else {
            // The FP32 renderer reads the keyed chips and the pulse rounded
            // to FP32, half the bytes of the FP64 session before it.
            const auto halved = work.keyed_lanes * source->cells * 3 * sizeof(std::complex<float>) +
                source->pulse.size() * sizeof(float);
            require(work.keyed_lanes == control_work.keyed_lanes && work.rendered_tiles == control_work.rendered_tiles &&
                    work.upload_bytes + halved == control_work.upload_bytes,
                    "FP32 tile rendering did not upload FP32 keyed chips and pulse");
        }

        // Every tile the batch rendered is still resident and equals the
        // native rendering of the same address.
        std::vector<Reference> resident(tile);
        std::size_t compared = 0;
        for (const auto& [address, native] : native_tiles) {
            if (!session->read_source_tile(address.first, address.second, resident)) continue;
            double peak = 0, error = 0;
            for (std::size_t i = 0; i != tile; ++i) {
                peak = std::max(peak, std::abs(native[i]));
                error = std::max(error, std::abs(resident[i] - native[i]));
            }
            if (peak == 0) require(error == 0, "A rendered tile outside the source support is not zero");
            else worst_tile = std::max(worst_tile, error / peak);
            ++compared;
        }
        require(compared == work.rendered_tiles && compared == native_tiles.size(),
                "A rendered tile left the cache before it could be compared");
        require(worst_tile < (fp32 ? 4e-6 : 1e-10), "Device-rendered dense tiles differ from the native tiles");

        // FP64 against the uploaded-tile session, FP32 against FP64.
        const auto& expected = fp32 ? control : uploaded;
        double scale = 0, statistic = 0;
        std::size_t present = 0;
        for (std::size_t b = 0; b != blocks; ++b) {
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& a = rendered[b][c];
                const auto& e = expected[b][c];
                // The source opens with a zero pulse halo and the last block lies past it.
                require(a.nonzero_basis == e.nonzero_basis && (b + 1 != blocks || !a.nonzero_basis),
                        "Tile rendering changed the basis presence witness");
                present += a.nonzero_basis;
                for (std::size_t i = 0; i != a.correlations.size(); ++i) {
                    scale = std::max(scale, std::abs(e.correlations[i]));
                    statistic = std::max(statistic, std::abs(a.correlations[i] - e.correlations[i]));
                }
                for (std::size_t i = 0; i != a.gram.size(); ++i) {
                    scale = std::max(scale, std::abs(e.gram[i]));
                    statistic = std::max(statistic, std::abs(a.gram[i] - e.gram[i]));
                }
            }
        }
        require(scale > 0 && present >= 2 * candidates, "Resident tile check produced no statistics");
        statistic /= scale;
        // The FFT route has no FP4 quantization.
        require(statistic < (fp4 ? 2e-3 : fp32 ? 1e-5 : 1e-6), fp32
                ? "FP32 tile rendering and mapping left their FP64 control"
                : "Device-rendered tiles changed the resident statistics");
        if (fp32) (fp4 ? fp32_fp4 : fp32_fft) = statistic;
        else worst_statistics[arithmetic] = std::max(worst_statistics[arithmetic], statistic);
        control = rendered;
        control_work = work;
        // The cache is arithmetic-independent: a second batch of the FP64
        // session finds every tile resident and repeats its results.
        if (!compare_statistics || fp32) continue;
        OpenClResidentWork warm;
        {
            const auto list = requests(true, repeated);
            require(session->project_resident(list, candidates, warm), "Warm tile-rendering batch failed");
        }
        require(warm.rendered_tiles == 0 && warm.keyed_lanes == 0 && warm.upload_bytes < work.upload_bytes,
                "A warm batch rendered or keyed its dense source again");
        for (std::size_t b = 0; b != blocks; ++b)
            for (std::size_t c = 0; c != candidates; ++c)
                require(repeated[b][c].correlations == rendered[b][c].correlations &&
                        repeated[b][c].gram == rendered[b][c].gram, "Cached rendered tiles changed the statistics");
    }
    native_tiles.clear();
    std::cout << "OpenCL resident tiles " << label << ": worst tile error, FP64 "
              << worst_tiles[OpenClFineArithmetic::fp64] << ", FP32 " << worst_tiles[OpenClFineArithmetic::fp32]
              << "; FP64 statistics against native tiles " << worst_statistics[OpenClFineArithmetic::fp64]
              << "; FP32 against FP64 kernels, FP4 " << fp32_fp4;
    if (compare_statistics) std::cout << ", FFT " << fp32_fft;
    std::cout << '\n';
}

// One batch whose blocks address more dense tiles than the session caches.
// The owner flushes under cache pressure, submits the mappings of every four
// blocks, and waits for the queue when its render requests outgrow their
// arena. The rendered route must still give the uploaded-tile statistics.
void check_resident_pressure(const OpenClNvfp4Device& device) {
    using namespace datapump::triad::v3;
    constexpr std::size_t rows = 1500, modes = 47, taps = 2, candidates = 16, blocks = 16, tile = 8192;
    constexpr std::size_t lanes_per_block = 2 * taps * candidates;
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i != seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(29 * i + 3);
    const auto key = datapump::triad::fixture::key(seed);
    ReceiverConfig config;
    config.sample_rate_hz = 6000;
    config.data_if_hz = 1725;
    config.data_basis_gain = .83;
    ReceiverSlotPlan slot;
    slot.format.inner_rate_hz = 3.6;
    slot.format.added_symbols = 1;
    slot.format.added_interleave = false;
    slot.format.sample_rate = 6000;
    slot.format.bit_seconds = 160;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    MappedBasis basis(config, slot, key, rows, 1);
    const auto* source = basis.tile_source();
    require(source && source->dense_samples > 100 * tile, "Pressure check needs a long dense source");
    const ConsecutiveBasisOffsets offsets(rows);
    const std::array<Tap, taps> paths{{{0, 0}, {.0113, 7.5}}};
    // Candidates of two keyed epochs, about nine tiles apart, so four blocks
    // need more tiles than the cache holds.
    std::array<MappedBasisRow, candidates> maps;
    for (std::size_t c = 0; c != candidates; ++c)
        maps[c] = {c % 2 ? 2 : 0, (c % 2 ? -.011 : .004) + 12. * static_cast<double>(c), c % 2 ? -.2 : .35,
                   c % 2 ? -31.25 : 12.5};
    // Blocks visit the source out of order, so each one needs new tiles.
    std::array<std::uint64_t, blocks> firsts;
    for (std::size_t b = 0; b != blocks; ++b) firsts[b] = (b * 37 % blocks) * (source->dense_samples / blocks) + 911;
    std::array<std::size_t, modes> bins;
    for (std::size_t m = 0; m != modes; ++m) bins[m] = (5 * m + 3) % rows;
    std::mt19937 random(4417);
    std::normal_distribution<double> noise(0, .1);
    std::vector<std::array<Reference, modes>> observed(blocks);
    for (auto& block : observed)
        for (auto& value : block) value = {noise(random), noise(random)};
    using Statistics = std::vector<std::array<FineBulkStatistics, candidates>>;
    const auto requests = [&](bool rendered, Statistics& output) {
        std::vector<FineResidentBlock> list;
        for (std::size_t b = 0; b != blocks; ++b)
            list.push_back({{rows, taps, bins, observed[b], .9, true},
                [&, b, rendered](std::size_t candidate, std::size_t, std::size_t, const BulkMappedSink& sink) {
                    if (rendered) basis.address_pair(maps[candidate], paths, firsts[b], offsets, rows, sink);
                    else basis.visit_pair(maps[candidate], paths, firsts[b], offsets, rows, sink);
                }, output[b], rendered ? source : nullptr});
        return list;
    };
    const auto method = OpenClFineMethod::resident_fft;
    const auto run = [&](bool rendered, Statistics& output, OpenClResidentWork& work, std::size_t slots = 64) {
        auto options = rendered ? OpenClResidentOptions{blocks, source->cells, source->lanes}
                                : OpenClResidentOptions{blocks};
        options.cache_slots = slots;
        // The FP32 kernels, whose FP32 keyed uploads share the flushes.
        options.arithmetic = OpenClFineArithmetic::fp32;
        const auto session = device.make_projection(rows, bins, blocks * lanes_per_block,
            OpenClNvfp4Projection::workspace(rows, modes, blocks * lanes_per_block, method, options).device_bytes,
            method, options);
        const auto list = requests(rendered, output);
        require(session && session->project_resident(list, candidates, work), "Pressure batch failed");
    };
    Statistics rendered(blocks), uploaded(blocks), crowded(blocks);
    OpenClResidentWork work, native, small;
    run(true, rendered, work);
    run(false, uploaded, native);
    // A quarter of the cache flushes so often that a flush falls between the
    // two bit columns of a tap, whose descriptors would otherwise merge.
    run(true, crowded, small, 16);
    require(small.mapping_dispatches > 2 * work.mapping_dispatches &&
            small.arena_synchronizations > work.arena_synchronizations,
            "A smaller tile cache did not flush and synchronize more often");
    // Streamed flushes alone give blocks / 4 mapping dispatches.
    require(work.arena_synchronizations > 0 && work.mapping_dispatches > blocks / 4 + work.arena_synchronizations &&
            work.rendered_tiles > 4 * 64 && work.rendered_tiles == native.uploaded_tiles &&
            work.mapped_columns == native.mapped_columns,
            "Pressure batch did not flush under pressure and synchronize its arenas");
    // Flushes and arena waits change no arithmetic: the crowded batch repeats
    // the rendered one exactly. Against native tiles the FP32 kernels keep
    // the FFT route's FP32 bound of the tile checks.
    double scale = 0, worst = 0;
    for (std::size_t b = 0; b != blocks; ++b)
        for (std::size_t c = 0; c != candidates; ++c)
            require(crowded[b][c].correlations == rendered[b][c].correlations &&
                    crowded[b][c].gram == rendered[b][c].gram &&
                    crowded[b][c].nonzero_basis == rendered[b][c].nonzero_basis,
                    "Pressure flushes changed the resident statistics");
    for (const auto* route : {&rendered, &crowded})
        for (std::size_t b = 0; b != blocks; ++b)
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& a = (*route)[b][c];
                const auto& e = uploaded[b][c];
                require(a.nonzero_basis == e.nonzero_basis, "Pressure batch changed the basis presence witness");
                for (std::size_t i = 0; i != a.correlations.size(); ++i) {
                    scale = std::max(scale, std::abs(e.correlations[i]));
                    worst = std::max(worst, std::abs(a.correlations[i] - e.correlations[i]));
                }
                for (std::size_t i = 0; i != a.gram.size(); ++i) {
                    scale = std::max(scale, std::abs(e.gram[i]));
                    worst = std::max(worst, std::abs(a.gram[i] - e.gram[i]));
                }
            }
    require(scale > 0 && worst / scale < 1e-5, "Rendered pressure batch left the uploaded-tile statistics");
    std::cout << "OpenCL resident pressure: " << work.rendered_tiles << " rendered, " << work.mapping_dispatches
              << " mapping dispatches, " << work.arena_synchronizations << " arena waits; with 16 slots "
              << small.rendered_tiles << ", " << small.mapping_dispatches << " and " << small.arena_synchronizations
              << ". Worst statistic difference " << worst / scale << '\n';
}

void check_resident_tiles(const OpenClNvfp4Device& device) {
    using namespace datapump::triad::v3;
    Format slow;
    slow.inner_rate_hz = 3.6;
    slow.added_symbols = 1;
    slow.added_interleave = false;
    slow.sample_rate = 6000;
    slow.bit_seconds = 16;
    slow.pulse_shape = PulseShape::TaperedRrc;
    check_resident_tiles(device, slow, 6000, true, "continuous");
    // A source rate below the receive rate puts dense samples between its integers.
    auto resampled = slow;
    resampled.inner_rate_hz = 36;
    resampled.added_symbols = 4;
    resampled.sample_rate = 2400;
    check_resident_tiles(device, resampled, 6000, false, "resampled");
    auto hopped = slow;
    hopped.inner_rate_hz = 720;
    hopped.sample_rate = 4800;
    hopped.fhss = true;
    check_resident_tiles(device, hopped, 4800, false, "gated");
    auto burst = resampled;
    burst.burst_wait_tiles = 1;
    burst.bit_seconds = 2 * geometry(resampled).tile_seconds;
    check_resident_tiles(device, burst, 6000, false, "burst");
}
} // namespace

Reference turn(double cycles) {
    return std::polar(1., 2 * std::numbers::pi * (cycles - std::floor(cycles)));
}

std::array<double, 4> inverse_gram(std::span<const Reference> first, std::span<const Reference> second) {
    double a = 0, d = 0;
    Reference b{};
    for (std::size_t i = 0; i != first.size(); ++i) {
        a += std::norm(first[i]);
        d += std::norm(second[i]);
        b += std::conj(first[i]) * second[i];
    }
    const auto determinant = a * d - std::norm(b);
    require(determinant > 1e-9 * a * d, "Coarse test bank is rank deficient");
    return {d / determinant, a / determinant, -b.real() / determinant, -b.imag() / determinant};
}

// The frequency-tile score of the native route in FP64: conj(template)
// against the arrival window of the rotated observation.
double coarse_score(std::span<const Reference> first, std::span<const Reference> second,
                    std::span<const Reference> observation, const std::array<double, 4>& inverse,
                    double frequency, double rate, double variance, std::size_t arrival) {
    Reference a{}, b{};
    for (std::size_t i = 0; i != first.size(); ++i) {
        const auto sample = arrival + i;
        const auto value = observation[sample] * turn(-frequency * (static_cast<double>(sample) / rate));
        a += std::conj(first[i]) * value;
        b += std::conj(second[i]) * value;
    }
    const Reference cross{inverse[2], inverse[3]};
    return (inverse[0] * std::norm(a) + inverse[1] * std::norm(b) + 2 * std::real(std::conj(a) * cross * b)) / variance;
}

void check_coarse(const OpenClNvfp4Device& device) {
    // Two arrival blocks, an unpaired last workspace and a padded inner
    // extent exercise every partial tile of the product kernel.
    constexpr std::size_t rows = 150, arrivals = 700, workspaces = 5, taps = 2, frequencies = 3;
    constexpr std::size_t separation = 9, planted_workspace = 2, planted_arrival = 137;
    constexpr double rate = 240, variance = .7;
    const std::array<double, frequencies> grid{-.31, .17, .44};
    std::mt19937 random(90210);
    std::normal_distribution<double> noise(0, 1);
    std::vector<std::vector<Reference>> bank(workspaces * taps, std::vector<Reference>(rows));
    std::vector<double> gains(bank.size());
    for (std::size_t column = 0; column != bank.size(); ++column) {
        // Column gains span the FP32 range that peak scaling must absorb.
        gains[column] = std::ldexp(1., 9 * (static_cast<int>(column % 7) - 3));
        for (std::size_t i = 0; i != rows; ++i) {
            const auto envelope = .15 + std::pow(std::sin(std::numbers::pi * (i + .5) / rows), 2);
            bank[column][i] = gains[column] * envelope * Reference(noise(random), noise(random));
        }
    }
    const auto samples = rows + arrivals - 1;
    std::vector<Reference> observation(samples);
    for (auto& value : observation) value = .5 * Reference(noise(random), noise(random));
    for (std::size_t i = 0; i != rows; ++i) {
        const auto sample = planted_arrival + i;
        const auto first = bank[planted_workspace * taps][i] / gains[planted_workspace * taps];
        const auto second = bank[planted_workspace * taps + 1][i] / gains[planted_workspace * taps + 1];
        observation[sample] += (Reference(.8, .2) * first + Reference(.3, -.6) * second) *
                               turn(grid[1] * (static_cast<double>(sample) / rate));
    }
    std::vector<std::array<double, 4>> inverse(workspaces);
    for (std::size_t workspace = 0; workspace != workspaces; ++workspace)
        inverse[workspace] = inverse_gram(bank[workspace * taps], bank[workspace * taps + 1]);
    const auto reference = [&](std::size_t frequency, std::size_t workspace, std::size_t arrival) {
        return coarse_score(bank[workspace * taps], bank[workspace * taps + 1], observation, inverse[workspace],
                            grid[frequency], rate, variance, arrival);
    };

    const OpenClCoarseShape shape{taps, rows, arrivals, workspaces, frequencies};
    require(OpenClNvfp4Coarse::supports(shape) && OpenClNvfp4Coarse::supports({1, rows, arrivals, workspaces, frequencies}) &&
            !OpenClNvfp4Coarse::supports({3, rows, arrivals, workspaces, frequencies}) &&
            !OpenClNvfp4Coarse::supports({0, rows, arrivals, workspaces, frequencies}),
            "Coarse FP4 geometry support changed");
    const auto limit = OpenClNvfp4Coarse::workspace(shape).device_bytes;
    require(!device.make_coarse(shape, limit - 1), "Coarse FP4 session exceeded its workspace limit");
    const auto coarse = device.make_coarse(shape, limit, true, OpenClRescoreArithmetic::fp32_partial);
    require(static_cast<bool>(coarse), "Could not make the coarse FP4 session");
    const std::vector<std::span<const Reference>> columns(bank.begin(), bank.end());
    OpenClCoarseWork work;
    coarse->load_bank(columns, work);
    coarse->prepare(observation, inverse, rate, variance, work);

    const auto check_tile = [&](std::span<const double> tile, std::size_t first_frequency) {
        const auto count = tile.size();
        std::vector<CoarseBulkPeaks> frontier(workspaces * count);
        const auto before = work;
        coarse->peaks(tile, separation, frontier, work);
        require(work.score_rows - before.score_rows == count * workspaces * arrivals &&
                work.useful_products - before.useful_products == count * workspaces * arrivals * 8 * rows &&
                work.issued_products - before.issued_products >= work.useful_products - before.useful_products,
                "Coarse FP4 work receipt does not cover the public tile");
        std::vector<float> scores(count * workspaces * arrivals);
        coarse->read_scores(scores);
        double product = 0, fp4_energy = 0, reference_energy = 0;
        std::vector<double> row(arrivals), exact_row(arrivals);
        for (std::size_t f = 0; f != count; ++f) {
            for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
                double best = 0;
                for (std::size_t arrival = 0; arrival != arrivals; ++arrival) {
                    row[arrival] = scores[(f * workspaces + workspace) * arrivals + arrival];
                    const auto expected = reference(first_frequency + f, workspace, arrival);
                    exact_row[arrival] = expected;
                    product += row[arrival] * expected;
                    fp4_energy += row[arrival] * row[arrival];
                    reference_energy += expected * expected;
                    best = std::max(best, expected);
                }
                // The widened FP4 shortlist is finalized with exact rescored
                // values, so compare the public frontier to the native exact
                // score selector across every public arrival.
                const auto expected = select_coarse_arrivals(exact_row, separation);
                const auto& actual = frontier[workspace * count + f];
                require(actual.count == expected.count, "Coarse FP4 frontier count differs from native selection");
                for (std::size_t i = 0; i != expected.count; ++i) {
                    require(actual.lags[i] == expected.indices[i], "Coarse FP4 frontier arrival differs from native selection");
                    const auto exact = reference(first_frequency + f, workspace, actual.lags[i]);
                    require(std::abs(actual.scores[i] - exact) <= 2e-5 * std::max(best, std::abs(exact)),
                            "Coarse rescored peak differs from the FP64 reference");
                }
                if (first_frequency + f == 1 && workspace == planted_workspace)
                    require(actual.lags[0] == planted_arrival || (actual.count == 2 && actual.lags[1] == planted_arrival),
                            "Coarse FP4 frontier lost the planted arrival");
            }
        }
        return product / std::sqrt(fp4_energy * reference_energy);
    };
    const auto agreement = check_tile(grid, 0);
    require(agreement > .97, "Coarse FP4 scores are too far from the FP64 reference");

    auto compact_shape = shape;
    compact_shape.frontier = OpenClCoarseFrontier::compact;
    const auto compact_quote = OpenClNvfp4Coarse::workspace(compact_shape);
    require(compact_quote.device_bytes <= limit && compact_quote.host_bytes <= OpenClNvfp4Coarse::workspace(shape).host_bytes,
            "Compact coarse frontier increased its workspace quote");
    const auto compact = device.make_coarse(compact_shape, compact_quote.device_bytes, true,
                                             OpenClRescoreArithmetic::fp64);
    require(static_cast<bool>(compact), "Could not make the compact coarse FP4 session");
    OpenClCoarseWork compact_work;
    compact->load_bank(columns, compact_work);
    compact->prepare(observation, inverse, rate, variance, compact_work);
    std::vector<CoarseBulkPeaks> compact_frontier(workspaces * frequencies);
    compact->peaks(grid, separation, compact_frontier, compact_work);
    require(compact_work.rescored_peaks > 0 && compact_work.rescored_peaks <= workspaces * frequencies * 8,
            "Compact coarse frontier exceeded its eight-score-per-row rescore bound");
    for (std::size_t f = 0; f < frequencies; ++f)
        for (std::size_t workspace = 0; workspace < workspaces; ++workspace) {
            const auto& row = compact_frontier[workspace * frequencies + f];
            require(row.count > 0 && row.count <= 2, "Compact coarse frontier published an invalid count");
            for (std::size_t i = 0; i < row.count; ++i) {
                require(row.lags[i] < arrivals, "Compact coarse frontier published an invalid lag");
                const auto exact = reference(f, workspace, row.lags[i]);
                require(std::abs(row.scores[i] - exact) <= 2e-5 * std::max(exact, 1e-12),
                        "Compact coarse frontier score is not the exact rescore");
            }
        }

    // This session rescores with FP32 partial sums of 64 rows added in FP64;
    // the control accumulates every product in FP64. Both routes use the same
    // wider FP4 proposals and apply the final exact-score selection policy.
    // A partial sum of 64 single-rounding multiply-adds is within 64 * 2^-24
    // of the sum of its absolute products, so a correlation moves by at most
    // 3.8e-6 of the product of its operand norms, and a score whose
    // correlation coefficient is r by about 7.6e-6 / r of itself; random
    // rounding gives far less. Declared bound: 1e-5 of the row's best exact
    // score.
    std::vector<CoarseBulkPeaks> partial_frontier(workspaces * frequencies), control_frontier(workspaces * frequencies);
    double rescore_difference = 0;
    {
        const auto control = device.make_coarse(shape, limit, true, OpenClRescoreArithmetic::fp64);
        require(static_cast<bool>(control), "Could not make the FP64 rescoring control session");
        OpenClCoarseWork control_work, partial_work;
        control->load_bank(columns, control_work);
        control->prepare(observation, inverse, rate, variance, control_work);
        control->peaks(grid, separation, control_frontier, control_work);
        coarse->peaks(grid, separation, partial_frontier, partial_work);
        require(control_work.rescored_peaks == partial_work.rescored_peaks,
                "FP64 rescoring control retained different arrivals");
        for (std::size_t row = 0; row != partial_frontier.size(); ++row) {
            const auto& a = partial_frontier[row];
            const auto& e = control_frontier[row];
            require(a.count == e.count && a.lags[0] == e.lags[0] && a.lags[1] == e.lags[1],
                    "The rescoring sessions retained different FP4 frontiers");
            const auto workspace = row / frequencies, frequency = row % frequencies;
            double best = 0;
            for (std::size_t arrival = 0; arrival != arrivals; ++arrival)
                best = std::max(best, reference(frequency, workspace, arrival));
            for (std::size_t i = 0; i != a.count; ++i)
                rescore_difference = std::max(rescore_difference,
                    std::abs(a.scores[i] - e.scores[i]) / std::max(best, std::abs(e.scores[i])));
        }
    }
    require(rescore_difference < 1e-5, "FP32 partial rescoring left its FP64-accumulating control");
    // A narrower tile reuses the resident bank and prepared observation.
    (void)check_tile(std::span(grid).subspan(2, 1), 2);
    require(work.rescored_peaks > 0 && work.dispatches > 0 && work.upload_bytes > 0 && work.download_bytes > 0,
            "Coarse FP4 work receipt is incomplete");
    require(work.rescore_seconds > 0 && work.rescore_seconds <= work.selection_seconds && !work.synthesis_seconds,
            "Coarse FP4 rescoring interval is not part of selection");

    std::stop_source cancelled;
    cancelled.request_stop();
    std::vector<CoarseBulkPeaks> frontier(workspaces * frequencies);
    const auto before = work;
    bool rejected = false;
    try { coarse->peaks(grid, separation, frontier, work, cancelled.get_token()); } catch (const datapump::Error&) { rejected = true; }
    require(rejected && work.score_rows == before.score_rows && work.dispatches == before.dispatches,
            "A cancelled coarse FP4 tile published work");
    auto invalid = observation;
    invalid[17] = {std::numeric_limits<double>::quiet_NaN(), 0};
    rejected = false;
    try { coarse->prepare(invalid, inverse, rate, variance, work); } catch (const datapump::Error&) { rejected = true; }
    require(rejected, "Coarse FP4 accepted a nonfinite observation");
    rejected = false;
    try { coarse->peaks(grid, separation, frontier, work); } catch (const datapump::Error&) { rejected = true; }
    require(rejected, "Coarse FP4 searched after a rejected observation");

    // Physical quiet has no arrival structure. Its flat scores still yield
    // the native endpoint frontier.
    std::vector<Reference> quiet(samples);
    coarse->prepare(quiet, inverse, rate, variance, work);
    coarse->peaks(grid, separation, frontier, work);
    for (const auto& peaks : frontier)
        require(peaks.count == 2 && peaks.lags[0] == 0 && peaks.lags[1] == arrivals - 1 &&
                peaks.scores[0] == 0 && peaks.scores[1] == 0, "Coarse FP4 quiet frontier changed");
    std::cout << "OpenCL NVFP4 coarse " << workspaces << " workspaces, " << frequencies << " frequencies, "
              << arrivals << " arrivals. Score agreement " << agreement
              << ", FP32 partial against FP64 rescoring " << rescore_difference << '\n';
}

// One ray: four workspaces fill a bank tile, and a score is the scalar
// inverse Gram times the correlation's energy. Seven workspaces leave a
// partial last tile and 700 arrivals a partial last block. Every workspace
// holds its own planted arrival at the second frequency, at an exact score
// near 60 in noise. The rescored FP4 frontier must keep the exact frontier's
// best arrival of each of those rows.
void check_coarse_one_ray(const OpenClNvfp4Device& device) {
    constexpr std::size_t rows = 150, arrivals = 700, workspaces = 7, frequencies = 3, separation = 9;
    constexpr double rate = 240, variance = .7;
    const std::array<double, frequencies> grid{-.31, .17, .44};
    std::mt19937 random(31337);
    std::normal_distribution<double> noise(0, 1);
    std::vector<std::vector<Reference>> bank(workspaces, std::vector<Reference>(rows));
    std::vector<double> gains(workspaces);
    std::vector<std::size_t> planted(workspaces);
    for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
        gains[workspace] = std::ldexp(1., 9 * (static_cast<int>(workspace % 7) - 3));
        planted[workspace] = 40 + 93 * workspace;
        for (std::size_t i = 0; i != rows; ++i) {
            const auto envelope = .15 + std::pow(std::sin(std::numbers::pi * (i + .5) / rows), 2);
            bank[workspace][i] = gains[workspace] * envelope * Reference(noise(random), noise(random));
        }
    }
    const auto samples = rows + arrivals - 1;
    std::vector<Reference> observation(samples);
    for (auto& value : observation) value = .5 * Reference(noise(random), noise(random));
    for (std::size_t workspace = 0; workspace != workspaces; ++workspace)
        for (std::size_t i = 0; i != rows; ++i) {
            const auto sample = planted[workspace] + i;
            observation[sample] += Reference(.45, .25) * (bank[workspace][i] / gains[workspace]) *
                                   turn(grid[1] * (static_cast<double>(sample) / rate));
        }
    // The receiver's one-ray inverse: 1 / a, and zero second-ray terms.
    std::vector<std::array<double, 4>> inverse(workspaces);
    for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
        double a = 0;
        for (const auto value : bank[workspace]) a += std::norm(value);
        inverse[workspace] = {1 / a, 0, 0, 0};
    }
    const auto reference = [&](std::size_t frequency, std::size_t workspace, std::size_t arrival) {
        Reference a{};
        for (std::size_t i = 0; i != rows; ++i) {
            const auto sample = arrival + i;
            a += std::conj(bank[workspace][i]) * observation[sample] *
                 turn(-grid[frequency] * (static_cast<double>(sample) / rate));
        }
        return inverse[workspace][0] * std::norm(a) / variance;
    };

    const OpenClCoarseShape shape{1, rows, arrivals, workspaces, frequencies};
    const auto limit = OpenClNvfp4Coarse::workspace(shape).device_bytes;
    require(limit < OpenClNvfp4Coarse::workspace({2, rows, arrivals, workspaces, frequencies}).device_bytes,
            "One-ray coarse FP4 session leased a two-ray bank");
    const std::vector<std::span<const Reference>> columns(bank.begin(), bank.end());
    std::array<std::vector<CoarseBulkPeaks>, 2> frontiers;
    std::vector<float> scores(frequencies * workspaces * arrivals);
    OpenClCoarseWork work;
    for (const auto arithmetic : {OpenClRescoreArithmetic::fp64, OpenClRescoreArithmetic::fp32_partial}) {
        const auto coarse = device.make_coarse(shape, limit, true, arithmetic);
        require(static_cast<bool>(coarse), "Could not make the one-ray coarse FP4 session");
        OpenClCoarseWork session;
        coarse->load_bank(columns, session);
        auto wrong = inverse;
        wrong[3][1] = inverse[3][0];
        bool rejected = false;
        try { coarse->prepare(observation, wrong, rate, variance, session); } catch (const datapump::Error&) { rejected = true; }
        require(rejected, "One-ray coarse FP4 session accepted second-ray coefficients");
        coarse->prepare(observation, inverse, rate, variance, session);
        auto& frontier = frontiers[arithmetic == OpenClRescoreArithmetic::fp32_partial];
        frontier.resize(workspaces * frequencies);
        const auto before = session;
        coarse->peaks(grid, separation, frontier, session);
        require(session.score_rows - before.score_rows == frequencies * workspaces * arrivals &&
                session.useful_products - before.useful_products == frequencies * workspaces * arrivals * 4 * rows &&
                session.issued_products - before.issued_products >= session.useful_products - before.useful_products,
                "One-ray coarse FP4 work receipt does not cover the public tile");
        if (arithmetic == OpenClRescoreArithmetic::fp64) {
            coarse->read_scores(scores);
            work = session;
        }
    }

    double product = 0, fp4_energy = 0, reference_energy = 0, rescore_difference = 0;
    std::size_t same_frontiers = 0;
    std::vector<double> row(arrivals), exact(arrivals);
    for (std::size_t f = 0; f != frequencies; ++f) {
        for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
            double best = 0;
            for (std::size_t arrival = 0; arrival != arrivals; ++arrival) {
                row[arrival] = scores[(f * workspaces + workspace) * arrivals + arrival];
                exact[arrival] = reference(f, workspace, arrival);
                product += row[arrival] * exact[arrival];
                fp4_energy += row[arrival] * row[arrival];
                reference_energy += exact[arrival] * exact[arrival];
                best = std::max(best, exact[arrival]);
            }
            // Both rescore modes must publish the exact-score selection.
            const auto selected = select_coarse_arrivals(exact, separation);
            const auto& actual = frontiers[0][workspace * frequencies + f];
            const auto& partial = frontiers[1][workspace * frequencies + f];
            require(actual.count == selected.count && partial.count == actual.count,
                    "One-ray coarse FP4 frontier count differs from native selection");
            for (std::size_t i = 0; i != selected.count; ++i) {
                require(actual.lags[i] == selected.indices[i] && partial.lags[i] == actual.lags[i],
                        "One-ray coarse FP4 frontier arrival differs from native selection");
                require(std::abs(actual.scores[i] - exact[actual.lags[i]]) <= 2e-5 * std::max(best, exact[actual.lags[i]]),
                        "One-ray rescored peak differs from the FP64 reference");
                rescore_difference = std::max(rescore_difference,
                    std::abs(partial.scores[i] - actual.scores[i]) / std::max(best, std::abs(actual.scores[i])));
            }
            // The exact frontier of the FP64 scores, which the native FFT
            // route would select.
            const auto native = select_coarse_arrivals(exact, separation);
            bool same = native.count == actual.count;
            for (std::size_t i = 0; same && i != native.count; ++i) same = native.indices[i] == actual.lags[i];
            same_frontiers += same;
            if (f == 1) {
                const auto strongest = static_cast<std::size_t>(std::max_element(exact.begin(), exact.end()) - exact.begin());
                require(strongest == planted[workspace] && best > 30,
                        "One-ray planted arrival is not the exact frontier's best");
                require(actual.lags[0] == strongest || (actual.count == 2 && actual.lags[1] == strongest),
                        "One-ray coarse FP4 frontier lost the planted arrival");
            }
        }
    }
    const auto agreement = product / std::sqrt(fp4_energy * reference_energy);
    require(agreement > .97, "One-ray coarse FP4 scores are too far from the FP64 reference");
    require(rescore_difference < 1e-5, "One-ray FP32 partial rescoring left its FP64-accumulating control");
    require(work.rescored_peaks > 0 && work.rescore_seconds > 0 && !work.synthesis_seconds,
            "One-ray coarse FP4 work receipt is incomplete");
    std::cout << "OpenCL NVFP4 coarse one ray: " << workspaces << " workspaces, " << frequencies << " frequencies, "
              << arrivals << " arrivals. Score agreement " << agreement << ", " << same_frontiers << " of "
              << workspaces * frequencies << " frontiers equal to the exact frontier, " << workspaces
              << " planted arrivals kept, FP32 partial against FP64 rescoring " << rescore_difference << '\n';
}

// Weak planted arrivals near the noise maximum, for one and two rays. The
// wider FP4 proposal set is checked against exact-score peak selection, so a
// weak true arrival can be recovered when its FP4 rank falls below the old
// two-arrival frontier.
// Four workspaces of 150 rows each hold one planted arrival at the single
// trial frequency, in disjoint stretches of one observation, at a noiseless
// score of 24, 16, 12, 10 or 8 in unit noise; five noise realizations per
// level give twenty trials per level and ray count. The noise-only maximum
// of a 512-arrival row is about 6.8 for one ray and 8.9 for two, and of the
// matched search's 3.95 million score rows about 16 and 18.5.
// Predeclared rule: the FP4 frontier keeps every planted arrival that the
// exact FP64 frontier keeps at levels 24 and 16, and at least 90% of them
// over the sweep; the exact frontier keeps all of level 24. Component
// evidence only, separate from receiver sensitivity and capacity.
void check_coarse_weak_arrivals(const OpenClNvfp4Device& device, OpenClCoarseFrontier mode) {
    constexpr std::size_t rows = 150, arrivals = 512, workspaces = 4, separation = 9, realizations = 5;
    constexpr std::size_t spacing = 165;
    constexpr std::array<double, 5> levels{24, 16, 12, 10, 8};
    constexpr double rate = 240, variance = 1;
    const std::array<double, 1> grid{.17};
    std::ostringstream report;
    for (const std::size_t taps : {1, 2}) {
        std::mt19937 random(4242 + static_cast<unsigned>(taps));
        std::normal_distribution<double> noise(0, std::sqrt(.5));
        std::uniform_real_distribution<double> phase(0, 1);
        std::vector<std::vector<Reference>> bank(workspaces * taps, std::vector<Reference>(rows));
        for (auto& column : bank)
            for (std::size_t i = 0; i != rows; ++i) {
                const auto envelope = .15 + std::pow(std::sin(std::numbers::pi * (i + .5) / rows), 2);
                column[i] = envelope * Reference(noise(random), noise(random));
            }
        std::vector<std::array<double, 4>> inverse(workspaces);
        for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
            if (taps == 2) {
                inverse[workspace] = inverse_gram(bank[2 * workspace], bank[2 * workspace + 1]);
                continue;
            }
            double a = 0;
            for (const auto value : bank[workspace]) a += std::norm(value);
            inverse[workspace] = {1 / a, 0, 0, 0};
        }
        OpenClCoarseShape shape{taps, rows, arrivals, workspaces, grid.size()};
        shape.frontier = mode;
        const auto coarse = device.make_coarse(shape, OpenClNvfp4Coarse::workspace(shape).device_bytes);
        require(static_cast<bool>(coarse), "Could not make the weak-arrival coarse FP4 session");
        const std::vector<std::span<const Reference>> columns(bank.begin(), bank.end());
        OpenClCoarseWork work;
        coarse->load_bank(columns, work);
        std::array<std::size_t, levels.size()> exact_kept{}, fp4_kept{}, fp4_only{};
        std::vector<Reference> observation(rows + arrivals - 1);
        std::vector<double> exact(arrivals);
        std::vector<CoarseBulkPeaks> frontier(workspaces);
        for (std::size_t level = 0; level != levels.size(); ++level)
            for (std::size_t realization = 0; realization != realizations; ++realization) {
                for (auto& value : observation) value = Reference(noise(random), noise(random));
                for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
                    // A random combination of the workspace's rays, scaled to
                    // the level: a signal in the span scores its energy over
                    // the variance.
                    std::vector<Reference> signal(rows);
                    const auto first = turn(phase(random)), second = .7 * turn(phase(random));
                    double energy = 0;
                    for (std::size_t i = 0; i != rows; ++i) {
                        signal[i] = first * bank[workspace * taps][i] +
                                    (taps == 2 ? second * bank[workspace * taps + 1][i] : Reference{});
                        energy += std::norm(signal[i]);
                    }
                    const auto gain = std::sqrt(levels[level] * variance / energy);
                    for (std::size_t i = 0; i != rows; ++i) {
                        const auto sample = 5 + spacing * workspace + i;
                        observation[sample] += gain * signal[i] *
                                               turn(grid[0] * (static_cast<double>(sample) / rate));
                    }
                }
                coarse->prepare(observation, inverse, rate, variance, work);
                coarse->peaks(grid, separation, frontier, work);
                for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
                    for (std::size_t arrival = 0; arrival != arrivals; ++arrival) {
                        if (taps == 2) {
                            exact[arrival] = coarse_score(bank[2 * workspace], bank[2 * workspace + 1], observation,
                                                          inverse[workspace], grid[0], rate, variance, arrival);
                            continue;
                        }
                        Reference a{};
                        for (std::size_t i = 0; i != rows; ++i) {
                            const auto sample = arrival + i;
                            a += std::conj(bank[workspace][i]) * observation[sample] *
                                 turn(-grid[0] * (static_cast<double>(sample) / rate));
                        }
                        exact[arrival] = inverse[workspace][0] * std::norm(a) / variance;
                    }
                    const auto planted = 5 + spacing * workspace;
                    const auto native = select_coarse_arrivals(exact, separation);
                    const auto& actual = frontier[workspace];
                    const bool by_exact = std::find(native.indices.begin(), native.indices.begin() + native.count,
                                                    planted) != native.indices.begin() + native.count;
                    const bool by_fp4 = std::find(actual.lags, actual.lags + actual.count, planted) !=
                                        actual.lags + actual.count;
                    exact_kept[level] += by_exact;
                    fp4_kept[level] += by_exact && by_fp4;
                    fp4_only[level] += by_fp4 && !by_exact;
                }
            }
        const auto trials = realizations * workspaces;
        const auto exact_total = std::accumulate(exact_kept.begin(), exact_kept.end(), std::size_t{0});
        const auto fp4_total = std::accumulate(fp4_kept.begin(), fp4_kept.end(), std::size_t{0});
        report << (taps == 1 ? "one ray" : "; two rays") << ", level";
        for (std::size_t level = 0; level != levels.size(); ++level)
            report << (level ? ", " : " ") << levels[level] << ": exact " << exact_kept[level] << '/' << trials
                   << ", FP4 " << fp4_kept[level] << " of those (+" << fp4_only[level] << ')';
        require(exact_kept[0] == trials, "The exact frontier lost a strong planted arrival");
        require(fp4_kept[0] == exact_kept[0] && fp4_kept[1] == exact_kept[1],
                "The coarse FP4 frontier lost a planted arrival that the exact frontier kept at a strong level");
        require(10 * fp4_total >= 9 * exact_total,
                "The coarse FP4 frontier kept fewer than 90% of the planted arrivals the exact frontier kept");
    }
    std::cout << (mode == OpenClCoarseFrontier::expanded ? "OpenCL NVFP4 coarse weak arrivals: "
                                                         : "OpenCL NVFP4 compact coarse weak arrivals: ")
              << report.str() << '\n';
}

// Lagrange interpolation of the native exact route, over a zero-extended
// integer rendering.
Reference cubic_sample(std::span<const Reference> source, long double index) {
    if (index <= -2 || index >= static_cast<long double>(source.size()) + 1) return {};
    const auto center = static_cast<std::int64_t>(std::floor(index));
    const auto u = static_cast<double>(index - center);
    const std::array<double, 4> weights{-u * (u - 1) * (u - 2) / 6, (u + 1) * (u - 1) * (u - 2) / 2,
                                        -(u + 1) * u * (u - 2) / 2, (u + 1) * u * (u - 1) / 6};
    Reference value{};
    for (int i = 0; i != 4; ++i) {
        const auto sample = center + i - 1;
        if (sample >= 0 && static_cast<std::uint64_t>(sample) < source.size())
            value += weights[i] * source[static_cast<std::size_t>(sample)];
    }
    return value;
}

// A decimation and tap count whose 128-row tiles fit one stage window warp
// raw rows as the FIR stages them. Other geometries store raw columns first,
// and their FIR stages several windows or, past the stage, reads in place.
// With `shared`, columns of different lanes share maps, as the canonical
// bank's bits and epochs share a clock and ray: sets of four, three, two and
// one column. A column must then filter to the same bits as when it is the
// only column of its map. With `one_ray`, a one-ray bank of the same
// workspaces' first rays must equal those columns bit for bit, and its
// scalar Grams their first entries.
void check_coarse_synthesis(const OpenClNvfp4Device& device, const datapump::triad::v3::Format& format,
                            double receive_rate, const char* label, std::size_t decimation = 4,
                            std::size_t tap_count = 97, bool fused = true, bool shared = false,
                            bool one_ray = false) {
    using namespace datapump::triad::v3;
    constexpr std::size_t taps = 2, lanes = 4;
    const std::size_t workspaces = shared ? 5 : 3;
    constexpr double delay_seconds = .021, doppler_hz = 37.25, origin_seconds = -.35;
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i != seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(29 * i + 5);
    const auto key = datapump::triad::fixture::key(seed);
    const auto public_geometry = geometry(format);
    const auto source_rate = effective_sample_rate(format);
    const auto ratio = source_rate / receive_rate;
    const auto source_rows = static_cast<std::size_t>(std::ceil(public_geometry.source_samples / ratio));
    const auto raw_rows = source_rows + 64;
    const auto kernel_rows = (raw_rows + tap_count - 2) / decimation + 1;
    const auto cells = static_cast<std::size_t>(public_geometry.fine_cells);

    std::vector<double> filter(tap_count);
    double sum = 0;
    for (std::size_t i = 0; i != tap_count; ++i) {
        const auto x = static_cast<double>(i) - (tap_count - 1) / 2.;
        const auto window = .54 + .46 * std::cos(2 * std::numbers::pi * x / (tap_count - 1));
        filter[i] = window * (x == 0 ? .22 : std::sin(.22 * std::numbers::pi * x) / (std::numbers::pi * x));
        sum += filter[i];
    }
    for (auto& tap : filter) tap /= sum;

    std::vector<PatternSource> sources;
    std::vector<std::complex<float>> chips(lanes * cells * 3);
    std::vector<std::vector<Reference>> integer(lanes, std::vector<Reference>(source_rows));
    for (std::size_t lane = 0; lane != lanes; ++lane) {
        auto keyed = format;
        keyed.epoch += lane / 2;
        sources.emplace_back(keyed, key, static_cast<std::uint8_t>(lane % 2));
        for (std::size_t cell = 0; cell != cells; ++cell) {
            const auto chip = sources.back().pattern_coefficient_at(cell);
            for (std::size_t carrier = 0; carrier != 3; ++carrier)
                chips[(lane * cells + cell) * 3 + carrier] = std::complex<float>(chip[carrier]);
        }
        for (std::size_t i = 0; i != source_rows; ++i)
            integer[lane][i] = sources.back().sample_at(static_cast<long double>(i) * ratio);
    }
    std::vector<float> pulse(8 * 256 + 1);
    for (std::size_t i = 0; i != pulse.size(); ++i)
        pulse[i] = static_cast<float>(shaping_pulse(format.pulse_shape, static_cast<double>(i) / 256));

    constexpr std::array<double, 3> clocks{-20, 0, 35};
    // Shared: ray 0 has clocks 0, 0, 0, 0, 35 and ray 1 -20, -20, 0, 0, 0.
    const auto clock = [&](std::size_t workspace, std::size_t ray) {
        if (!shared) return clocks[workspace];
        return ray ? (workspace < 2 ? clocks[0] : clocks[1]) : (workspace < 4 ? clocks[1] : clocks[2]);
    };
    std::vector<CoarseTemplateColumn> columns(workspaces * taps);
    std::vector<std::vector<Reference>> expected(columns.size(), std::vector<Reference>(kernel_rows));
    for (std::size_t column = 0; column != columns.size(); ++column) {
        const auto workspace = column / taps, ray = column % taps;
        const auto scale = 1 + clock(workspace, ray) * 1e-6;
        const auto delay = ray ? delay_seconds : 0., doppler = ray ? doppler_hz : 0.;
        auto& map = columns[column];
        map = {(origin_seconds - delay) * scale * receive_rate - 2.5, scale, origin_seconds * doppler,
               doppler / receive_rate, (workspace + ray) % lanes};
        std::vector<Reference> raw(raw_rows);
        for (std::size_t i = 0; i != raw_rows; ++i)
            raw[i] = cubic_sample(integer[map.lane], static_cast<long double>(map.first_position) + i * static_cast<long double>(map.scale)) *
                     (ray ? turn(map.first_cycles + i * map.cycle_step) : Reference{1});
        for (std::size_t row = 0; row != kernel_rows; ++row) {
            const auto newest = row * decimation;
            Reference value{};
            for (std::size_t tap = 0; tap != tap_count && tap <= newest; ++tap)
                if (newest - tap < raw_rows) value += filter[tap] * raw[newest - tap];
            expected[column][row] = value;
        }
    }

    CoarseTemplateSource synthesis;
    synthesis.rate_ratio = ratio;
    synthesis.calendar_origin_sample = static_cast<double>(public_geometry.calendar_origin_sample);
    synthesis.source_samples = static_cast<double>(public_geometry.source_samples);
    synthesis.sample_rate_hz = source_rate;
    synthesis.fine_chip_rate_hz = public_geometry.fine_chip_rate_hz;
    synthesis.center_offset = format.fhss_center_aligned ? 0 : .5;
    synthesis.carrier_spacing_hz = public_geometry.carrier_spacing_hz;
    synthesis.bit_seconds = public_geometry.bit_seconds;
    synthesis.envelope_edge_seconds = std::max(format.ramp_seconds, 8 / public_geometry.fine_chip_rate_hz);
    if (format.burst_wait_tiles) {
        synthesis.burst_period_tiles = format.burst_wait_tiles + 1.;
        synthesis.tile_seconds = public_geometry.tile_seconds;
        synthesis.tile_count = static_cast<double>(public_geometry.tile_count);
    }
    if (format.fhss) {
        synthesis.dwell_seconds = format.dwell_seconds;
        synthesis.quiet_seconds = format.quiet_seconds;
        synthesis.ramp_seconds = format.ramp_seconds;
    }
    synthesis.lanes = lanes;
    synthesis.cells = cells;
    synthesis.source_rows = source_rows;
    synthesis.raw_rows = raw_rows;
    synthesis.decimation = decimation;
    synthesis.chips = chips;
    synthesis.pulse = pulse;
    synthesis.taps = filter;
    synthesis.columns = columns;

    const OpenClCoarseShape shape{taps, kernel_rows, 40, workspaces, 2, raw_rows, source_rows, lanes, cells,
                                  tap_count, decimation};
    // A FIR that warps as it stages leases no raw columns. Large row strides
    // always store them.
    auto stored = shape;
    stored.synthesis_decimation = 64;
    const auto lease = OpenClNvfp4Coarse::workspace(shape).device_bytes;
    const auto stored_lease = OpenClNvfp4Coarse::workspace(stored).device_bytes;
    require(fused ? stored_lease >= lease + raw_rows * sizeof(std::complex<float>) : stored_lease == lease,
            "Coarse synthesis leased raw storage for the wrong FIR route");
    const auto coarse = device.make_coarse(shape, lease, true);
    require(static_cast<bool>(coarse), "Could not make the coarse synthesis session");
    OpenClCoarseWork work;
    std::vector<CoarseTemplateGram> grams(workspaces);
    coarse->synthesize_bank(synthesis, grams, work);
    std::vector<std::complex<float>> resident(columns.size() * kernel_rows);
    coarse->read_bank(resident);
    double worst = 0;
    for (std::size_t column = 0; column != columns.size(); ++column) {
        double peak = 0, error = 0;
        for (std::size_t row = 0; row != kernel_rows; ++row) {
            peak = std::max(peak, std::abs(expected[column][row]));
            error = std::max(error, std::abs(Reference(resident[column * kernel_rows + row]) - expected[column][row]));
        }
        require(peak > 0, "Coarse synthesis reference column is empty");
        worst = std::max(worst, error / peak);
    }
    require(worst < 2e-5, "Device template synthesis differs from the native exact route");
    for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
        const auto& first = expected[workspace * taps];
        const auto& second = expected[workspace * taps + 1];
        double a = 0, d = 0;
        Reference b{};
        for (std::size_t row = 0; row != kernel_rows; ++row) {
            a += std::norm(first[row]);
            d += std::norm(second[row]);
            b += std::conj(first[row]) * second[row];
        }
        const auto scale = std::sqrt(a * d);
        require(std::abs(grams[workspace].a - a) <= 1e-4 * a && std::abs(grams[workspace].d - d) <= 1e-4 * d &&
                std::abs(grams[workspace].b - b) <= 1e-4 * scale, "Device template Gram differs from the native columns");
    }
    require(work.rendered_samples >= lanes * source_rows && work.warped_samples == columns.size() * raw_rows &&
            work.filtered_rows == columns.size() * kernel_rows, "Device template synthesis receipt is incomplete");
    const auto parts = work.render_seconds + work.filter_seconds + work.gram_seconds;
    require(work.render_seconds > 0 && work.filter_seconds > 0 && work.gram_seconds > 0 &&
            std::abs(parts - work.synthesis_seconds) <= 1e-6 * work.synthesis_seconds,
            "Device template synthesis intervals do not cover their parts");

    std::size_t alone = 0;
    if (shared) {
        // The last column of each set keeps its map, and the others move by
        // one unit in the last place, so it becomes the only column of its
        // map. Its bank column must keep every bit, in this same build.
        const auto same = [](const CoarseTemplateColumn& a, const CoarseTemplateColumn& b) {
            return a.first_position == b.first_position && a.scale == b.scale &&
                   a.first_cycles == b.first_cycles && a.cycle_step == b.cycle_step;
        };
        std::vector<bool> kept(columns.size());
        std::size_t later_members = 0;
        for (std::size_t column = 0; column != columns.size(); ++column) {
            std::size_t members = 0, last = column;
            for (std::size_t other = 0; other != columns.size(); ++other)
                if (same(columns[other], columns[column])) {
                    ++members;
                    last = other;
                }
            kept[column] = last == column;
            later_members += kept[column] && members > 1;
        }
        require(later_members == 3, "Shared synthesis check lost its sets of several columns");
        auto moved = synthesis;
        auto moved_columns = columns;
        for (std::size_t column = 0; column != columns.size(); ++column)
            if (!kept[column]) moved_columns[column].first_position =
                std::nextafter(columns[column].first_position, std::numeric_limits<double>::infinity());
        moved.columns = moved_columns;
        const auto separate = device.make_coarse(shape, lease, true);
        require(static_cast<bool>(separate), "Could not make the separate-map synthesis session");
        OpenClCoarseWork separate_work;
        std::vector<CoarseTemplateGram> separate_grams(workspaces);
        separate->synthesize_bank(moved, separate_grams, separate_work);
        std::vector<std::complex<float>> separated(columns.size() * kernel_rows);
        separate->read_bank(separated);
        for (std::size_t column = 0; column != columns.size(); ++column) {
            if (!kept[column]) continue;
            ++alone;
            require(std::equal(resident.begin() + column * kernel_rows, resident.begin() + (column + 1) * kernel_rows,
                               separated.begin() + column * kernel_rows, [](auto a, auto b) {
                                   return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
                               }),
                    "A column shared with other lanes filtered differently from the same map alone");
        }
    }
    double one_ray_worst = 0;
    if (one_ray) {
        std::vector<CoarseTemplateColumn> first_rays(workspaces);
        for (std::size_t workspace = 0; workspace != workspaces; ++workspace)
            first_rays[workspace] = columns[workspace * taps];
        auto single = synthesis;
        single.columns = first_rays;
        auto single_shape = shape;
        single_shape.taps = 1;
        const auto single_lease = OpenClNvfp4Coarse::workspace(single_shape).device_bytes;
        require(single_lease < lease, "One-ray synthesis leased a two-ray bank");
        const auto single_coarse = device.make_coarse(single_shape, single_lease, true);
        require(static_cast<bool>(single_coarse), "Could not make the one-ray synthesis session");
        OpenClCoarseWork single_work;
        std::vector<CoarseTemplateGram> single_grams(workspaces);
        single_coarse->synthesize_bank(single, single_grams, single_work);
        std::vector<std::complex<float>> single_bank(workspaces * kernel_rows);
        single_coarse->read_bank(single_bank);
        const auto bits = [](auto a, auto b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
        for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
            const auto* own = single_bank.data() + workspace * kernel_rows;
            const auto* two = resident.data() + workspace * taps * kernel_rows;
            require(std::equal(own, own + kernel_rows, two, bits),
                    "A one-ray bank column differs from the first ray of the two-ray bank");
            const auto& gram = single_grams[workspace];
            require(std::bit_cast<std::uint64_t>(gram.a) == std::bit_cast<std::uint64_t>(grams[workspace].a) &&
                    gram.d == 0 && gram.b == Reference{}, "A one-ray Gram is not the first ray's scalar Gram");
            double peak = 0, error = 0;
            for (std::size_t row = 0; row != kernel_rows; ++row) {
                const auto& value = expected[workspace * taps][row];
                peak = std::max(peak, std::abs(value));
                error = std::max(error, std::abs(Reference(own[row]) - value));
            }
            one_ray_worst = std::max(one_ray_worst, error / peak);
        }
        require(single_work.warped_samples == workspaces * raw_rows && single_work.filtered_rows == workspaces * kernel_rows,
                "One-ray synthesis receipt is incomplete");
    }
    std::cout << "OpenCL coarse synthesis " << label << ": " << raw_rows << " raw rows, " << kernel_rows
              << " filtered rows, decimation " << decimation << ", " << tap_count << " taps"
              << (fused ? ", warped in the FIR" : ", stored raw columns") << ". Worst column error " << worst;
    if (shared) std::cout << "; " << alone << " columns identical when alone with their map";
    if (one_ray)
        std::cout << "; one-ray bank identical to the first rays, worst column error " << one_ray_worst;
    std::cout << '\n';
}

void check_coarse_synthesis(const OpenClNvfp4Device& device) {
    using namespace datapump::triad::v3;
    Format plain;
    plain.inner_rate_hz = 36;
    plain.added_symbols = 1;
    plain.sample_rate = 960;
    plain.bit_seconds = 16;
    check_coarse_synthesis(device, plain, 960, "continuous", 4, 97, true, false, true);
    // A receive rate above the source rate samples it between its integers.
    check_coarse_synthesis(device, plain, 1920, "resampled");
    auto hopped = plain;
    hopped.inner_rate_hz = 720;
    hopped.sample_rate = 4800;
    hopped.fhss = true;
    hopped.pulse_shape = PulseShape::TaperedRrc;
    check_coarse_synthesis(device, hopped, 4800, "gated");
    auto burst = plain;
    burst.burst_wait_tiles = 1;
    burst.bit_seconds = 2 * geometry(plain).tile_seconds;
    check_coarse_synthesis(device, burst, 960, "burst");
    // The matched coarse FIR: 1705 taps at decimation 25 fit one window of a
    // tile. Columns span several tiles, and the first and last tiles hold
    // rows whose taps start late or stop early.
    check_coarse_synthesis(device, plain, 960, "matched", 25, 1705, true, false, true);
    // The widest fused FIR of the canonical plan: 887 pairs of taps and a
    // raw span of 5075 fill 5962 of the 6016 stage pairs.
    check_coarse_synthesis(device, plain, 960, "stage limit", 26, 1773);
    // Stored raw columns: two stage windows of taps, then rows read in place.
    check_coarse_synthesis(device, plain, 960, "windowed", 32, 2183, false, false, true);
    check_coarse_synthesis(device, plain, 960, "in place", 50, 3409, false);
    // Lanes that share maps, through the warp table and the stored columns.
    check_coarse_synthesis(device, plain, 960, "shared maps", 4, 97, true, true, true);
    check_coarse_synthesis(device, plain, 960, "shared stored maps", 32, 2183, false, true);
}

// The synthesis route of the matched dense session's geometry across the
// canonical plan's decimations. Its FIR has 68.14 D + 1 taps, rounded up to
// odd, for pass and stop edges at .4 and .49 of the projected rate. Through
// decimation 26 a 128-row tile stages every tap beside its raw span, so the
// FIR warps as it filters and the session leases no raw columns. Wider
// strides store a bounded raw batch, as decimation 64 always does.
void check_coarse_fir_routes() {
    // The matched slot projects 6 kHz to 240 Hz: 1,122,562 raw rows, 78
    // workspaces of two rays, 15 frequencies and 3,377 arrivals.
    constexpr std::size_t raw_rows = 1'122'562, source_rows = 1'122'407, lanes = 26, cells = 97;
    struct Route {
        std::size_t decimation, taps;
        bool fused;
    };
    for (const auto& route : {Route{25, 1705, true}, Route{26, 1773, true}, Route{27, 1841, false},
                              Route{47, 3205, false}, Route{48, 3273, false}}) {
        const auto kernel_rows = (raw_rows + route.taps - 2) / route.decimation + 1;
        const OpenClCoarseShape shape{2, kernel_rows, 3377, 78, 15, raw_rows, source_rows, lanes, cells,
                                      route.taps, route.decimation};
        auto stored = shape;
        stored.synthesis_decimation = 64;
        const auto lease = OpenClNvfp4Coarse::workspace(shape).device_bytes;
        const auto stored_lease = OpenClNvfp4Coarse::workspace(stored).device_bytes;
        require(route.fused ? stored_lease >= lease + raw_rows * sizeof(std::complex<float>) : stored_lease == lease,
                "Coarse synthesis chose the wrong FIR route for a canonical decimation");
        // One ray halves the bank and its packed tiles, and keeps the route.
        auto single = shape;
        single.taps = 1;
        auto single_stored = stored;
        single_stored.taps = 1;
        const auto single_lease = OpenClNvfp4Coarse::workspace(single).device_bytes;
        const auto single_stored_lease = OpenClNvfp4Coarse::workspace(single_stored).device_bytes;
        require(single_lease < lease && (route.fused ? single_stored_lease > single_lease : single_stored_lease == single_lease),
                "One-ray coarse synthesis chose another FIR route or a larger lease");
        if (route.decimation == 25)
            std::cout << "OpenCL coarse matched-size lease at decimation 25: two rays " << lease << " bytes, one ray "
                      << single_lease << " bytes\n";
    }
}

// "resident" runs only the resident checks, as the deferred-execution
// emulation does.
int main(int argc, char** argv) {
    try {
        const bool resident_only = argc > 1 && std::string_view(argv[1]) == "resident";
        const bool weak_only = argc > 1 && std::string_view(argv[1]) == "weak";
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
        if (weak_only) {
            check_coarse_weak_arrivals(*device, OpenClCoarseFrontier::expanded);
            check_coarse_weak_arrivals(*device, OpenClCoarseFrontier::compact);
            return 0;
        }
        if (!resident_only) {
            check_projection(*device, 1500, 47, 33);
            check_projection(*device, 223, 67, 129);
        }
        check_resident_projection(*device);
        check_resident_tiles(*device);
        check_resident_pressure(*device);
        if (resident_only) return 0;
        check_coarse_fir_routes();
        check_coarse(*device);
        check_coarse_one_ray(*device);
        check_coarse_weak_arrivals(*device, OpenClCoarseFrontier::expanded);
        check_coarse_weak_arrivals(*device, OpenClCoarseFrontier::compact);
        check_coarse_synthesis(*device);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
