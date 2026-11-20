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
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <random>
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

    double worst_tile = 0, worst_statistic = 0;
    for (const auto method : {OpenClFineMethod::resident_fp4, OpenClFineMethod::resident_fft}) {
        const bool fp4 = method == OpenClFineMethod::resident_fp4;
        if (!fp4 && !compare_statistics) continue;
        const OpenClResidentOptions rendering{blocks, source->cells, source->lanes};
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

        const OpenClResidentOptions uploading{blocks};
        const auto reference = device.make_projection(rows, bins, blocks * lanes_per_block,
            OpenClNvfp4Projection::workspace(rows, modes, blocks * lanes_per_block, method, uploading).device_bytes,
            method, uploading);
        OpenClResidentWork native_work;
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
        require(worst_tile < 1e-10, "Device-rendered dense tiles differ from the native tiles");

        double scale = 0;
        std::size_t present = 0;
        for (std::size_t b = 0; b != blocks; ++b) {
            for (std::size_t c = 0; c != candidates; ++c) {
                const auto& a = rendered[b][c];
                const auto& e = uploaded[b][c];
                // The source opens with a zero pulse halo and the last block lies past it.
                require(a.nonzero_basis == e.nonzero_basis && (b + 1 != blocks || !a.nonzero_basis),
                        "Tile rendering changed the basis presence witness");
                present += a.nonzero_basis;
                for (std::size_t i = 0; i != a.correlations.size(); ++i) {
                    scale = std::max(scale, std::abs(e.correlations[i]));
                    worst_statistic = std::max(worst_statistic, std::abs(a.correlations[i] - e.correlations[i]));
                }
                for (std::size_t i = 0; i != a.gram.size(); ++i) {
                    scale = std::max(scale, std::abs(e.gram[i]));
                    worst_statistic = std::max(worst_statistic, std::abs(a.gram[i] - e.gram[i]));
                }
            }
        }
        require(scale > 0 && present >= 2 * candidates, "Resident tile check produced no statistics");
        worst_statistic /= scale;
        // One changed E2M1 element of a 3,000-element lane moves a statistic
        // by less than this. The FFT route has no such quantization.
        require(worst_statistic < (fp4 ? 2e-3 : 1e-6), "Device-rendered tiles changed the resident statistics");

        if (fp4) std::cout << "OpenCL resident tiles " << label << ": " << work.rendered_tiles << " rendered, "
                           << work.upload_bytes << " bytes uploaded against " << native_work.upload_bytes
                           << ". Worst tile error " << worst_tile;
        native_tiles.clear();
        if (!compare_statistics) continue;
        // A second batch finds every tile resident and repeats its results.
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
    std::cout << ", worst statistic difference " << worst_statistic << '\n';
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
    require(OpenClNvfp4Coarse::supports(shape) && !OpenClNvfp4Coarse::supports({1, rows, arrivals, workspaces, frequencies}),
            "Coarse FP4 geometry support changed");
    const auto limit = OpenClNvfp4Coarse::workspace(shape).device_bytes;
    require(!device.make_coarse(shape, limit - 1), "Coarse FP4 session exceeded its workspace limit");
    const auto coarse = device.make_coarse(shape, limit, true);
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
        std::vector<double> row(arrivals);
        for (std::size_t f = 0; f != count; ++f) {
            for (std::size_t workspace = 0; workspace != workspaces; ++workspace) {
                double best = 0;
                for (std::size_t arrival = 0; arrival != arrivals; ++arrival) {
                    row[arrival] = scores[(f * workspaces + workspace) * arrivals + arrival];
                    const auto expected = reference(first_frequency + f, workspace, arrival);
                    product += row[arrival] * expected;
                    fp4_energy += row[arrival] * row[arrival];
                    reference_energy += expected * expected;
                    best = std::max(best, expected);
                }
                // The device frontier is the native selection of its own
                // FP4 scores, and every retained score is recomputed.
                const auto expected = select_coarse_arrivals(row, separation);
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
    // A narrower tile reuses the resident bank and prepared observation.
    (void)check_tile(std::span(grid).subspan(2, 1), 2);
    require(work.rescored_peaks > 0 && work.dispatches > 0 && work.upload_bytes > 0 && work.download_bytes > 0,
            "Coarse FP4 work receipt is incomplete");

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
              << arrivals << " arrivals. Score agreement " << agreement << '\n';
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

void check_coarse_synthesis(const OpenClNvfp4Device& device, const datapump::triad::v3::Format& format,
                            double receive_rate, const char* label) {
    using namespace datapump::triad::v3;
    constexpr std::size_t workspaces = 3, taps = 2, lanes = 4, decimation = 4, tap_count = 97;
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

    const std::array<double, workspaces> clocks{-20, 0, 35};
    std::vector<CoarseTemplateColumn> columns(workspaces * taps);
    std::vector<std::vector<Reference>> expected(columns.size(), std::vector<Reference>(kernel_rows));
    for (std::size_t column = 0; column != columns.size(); ++column) {
        const auto workspace = column / taps, ray = column % taps;
        const auto scale = 1 + clocks[workspace] * 1e-6;
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

    const OpenClCoarseShape shape{taps, kernel_rows, 40, workspaces, 2, raw_rows, source_rows, lanes, cells, tap_count};
    const auto coarse = device.make_coarse(shape, OpenClNvfp4Coarse::workspace(shape).device_bytes, true);
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
    std::cout << "OpenCL coarse synthesis " << label << ": " << raw_rows << " raw rows, " << kernel_rows
              << " filtered rows. Worst column error " << worst << '\n';
}

void check_coarse_synthesis(const OpenClNvfp4Device& device) {
    using namespace datapump::triad::v3;
    Format plain;
    plain.inner_rate_hz = 36;
    plain.added_symbols = 1;
    plain.sample_rate = 960;
    plain.bit_seconds = 16;
    check_coarse_synthesis(device, plain, 960, "continuous");
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
}

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
        check_resident_tiles(*device);
        check_coarse(*device);
        check_coarse_synthesis(*device);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
