#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_helper.hpp"

#if DATAPUMP_TRIAD_TENSOR_CUDA
#include "triad_tensor_mapping.cuh"
#include "triad_tensor_mapping.hpp"
#include <c10/cuda/CUDAException.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <numeric>
#include <random>

namespace {
namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;
using v::Complex;
constexpr std::size_t mib = 1024 * 1024;
constexpr double tau = 2 * std::numbers::pi;

void check(bool condition, const char* reason) {
    if (!condition) throw datapump::Error(reason);
}

void near(Complex a, Complex b, double tolerance, const char* reason) {
    check(std::abs(a - b) <= tolerance * std::max(1., std::abs(b)), reason);
}

Complex rotation(double cycles) {
    return std::polar(1., tau * cycles);
}

#if DATAPUMP_TRIAD_TENSOR_CUDA
void cuda_mapping_pointwise() {
    const auto stream = c10::cuda::getStreamFromPool(false, 0);
    const c10::cuda::CUDAStreamGuard guard(stream);
    const auto device = at::Device(at::kCUDA, 0);
    constexpr auto tile_samples = r::cuda_mapping_tile_samples;
    constexpr auto table_width = 3 + r::cuda_mapping_maximum_pins;
    constexpr std::int64_t rows = 139, lanes = 17, tile_slots = 3, output_columns = lanes + 2;
    const auto host = at::TensorOptions().device(at::kCPU);
    const auto gpu = at::TensorOptions().device(device);
    const auto complex = gpu.dtype(at::kComplexFloat);
    auto x = at::arange(tile_slots * tile_samples, gpu.dtype(at::kFloat));
    const auto tiles = at::complex((x * .007f).cos() * .7f, (x * .013f).sin() * .4f)
        .reshape({tile_slots, tile_samples});
    std::array<double, rows> positions{};
    positions[1] = std::nextafter(1., 0.);
    positions[2] = 1.;
    positions[3] = std::nextafter(1., 2.);
    for (std::int64_t i = 4; i < rows; ++i) positions[i] = i - 2.25;
    std::array<double, lanes * 4> coefficients{
        .25, 1., -.03, .065,
        .875, 1., .99, -.073,
        std::nextafter(1., 0.), std::nextafter(1., 2.), -0x1p28 + .25, .125,
        .125, 1., 0x1p28 + .5, .013,
        .5, .75, -.5, -.125};
    std::array<std::int64_t, lanes * table_width> descriptors{};
    const auto map = [&](std::int64_t lane, std::int64_t origin, std::int64_t extent,
                         std::int64_t output, std::int64_t first_slot, std::int64_t second_slot) {
        auto* item = descriptors.data() + lane * table_width;
        item[0] = origin;
        item[1] = extent;
        item[2] = output;
        item[3] = first_slot;
        item[4] = second_slot;
    };
    map(0, -3, tile_samples, 5, 1, 0);
    map(1, tile_samples - 3, 2 * tile_samples, 0, 2, 0);
    map(2, tile_samples - 2, tile_samples, 3, 0, 0);
    map(3, INT64_C(1) << 40, tile_samples, 2, 1, 0);
    map(4, -(INT64_C(1) << 42), tile_samples, 6, 2, 0);
    std::mt19937_64 generator(4027);
    const auto unit = [&] { return static_cast<double>(generator() >> 11) * 0x1p-53; };
    for (std::int64_t lane = 5; lane < lanes; ++lane) {
        auto* item = coefficients.data() + lane * 4;
        item[0] = unit();
        item[1] = .25 + 2 * unit();
        item[2] = (2 * unit() - 1) * 0x1p20;
        item[3] = (2 * unit() - 1) * 2;
        map(lane, static_cast<std::int64_t>(generator() % (tile_samples + 200)) - 100,
            2 * tile_samples, lane + 2, lane % tile_slots, (lane + 1) % tile_slots);
    }
    const auto maps = at::from_blob(coefficients.data(), {lanes, 4}, host.dtype(at::kDouble)).to(gpu.dtype(at::kDouble));
    const auto table = at::from_blob(descriptors.data(), {lanes, table_width}, host.dtype(at::kLong)).to(gpu.dtype(at::kLong));
    const auto offsets = at::from_blob(positions.data(), {rows}, host.dtype(at::kDouble)).to(gpu.dtype(at::kDouble));
    const auto differences = [&](const at::Tensor& coefficients) {
        auto expected_storage = at::full({output_columns, 2 * rows + 7}, 1.25, complex);
        auto actual_storage = expected_storage.clone();
        auto expected = expected_storage.slice(1, 3, 3 + 2 * rows, 2);
        auto actual = actual_storage.slice(1, 3, 3 + 2 * rows, 2);
        r::render_aten_mapping(expected, tiles, coefficients, table, offsets, at::kFloat,
                               tile_samples, r::cuda_mapping_maximum_pins);
        C10_CUDA_CHECK(r::launch_cuda_mapped_c64(coefficients.const_data_ptr<double>(),
            table.const_data_ptr<std::int64_t>(), offsets.const_data_ptr<double>(),
            tiles.const_data_ptr<c10::complex<float>>(), actual.mutable_data_ptr<c10::complex<float>>(),
            {rows, lanes, tile_slots, output_columns, actual.stride(0), actual.stride(1)}, stream.stream()));
        check(actual.select(0, 2).eq(0).all().item<bool>() && actual.select(0, 6).eq(0).all().item<bool>() &&
              actual.select(0, 4).eq(1.25).all().item<bool>() &&
              actual_storage.select(1, 0).eq(1.25).all().item<bool>(),
              "fused CUDA mapping changed zero extension or wrote outside its selected output view");
        const auto actual_bits = at::view_as_real(actual_storage).view(at::kInt);
        const auto expected_bits = at::view_as_real(expected_storage).view(at::kInt);
        return actual_bits.ne(expected_bits).sum().item<std::int64_t>();
    };
    auto no_rotation = maps.clone();
    no_rotation.narrow(1, 2, 2).zero_();
    const auto interpolation_differences = differences(no_rotation);
    const auto rotated_differences = differences(maps);
    if (interpolation_differences || rotated_differences)
        throw datapump::Error("fused CUDA mapping differs from ATen in " +
            std::to_string(interpolation_differences) + " unrotated and " +
            std::to_string(rotated_differences) + " rotated float components");
}
#endif

void fir(r::ReceiverBulkBackend& backend) {
    auto device = backend.make_coarse(8, .0001);
    const std::array<double, 5> taps{.125, .25, .5, .25, .125};
    std::array<Complex, 17> input{};
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = Complex(.01 * i * i, -.1 * i);
    r::CoarseBulkWork work;
    const auto filter = device->prepare_filter(taps, 16, work, {});
    const r::CoarseFirCrop crop{input.size(), 7, 1, 3, 1, 10};
    const auto values = device->project_fir(*filter, crop,
        [&](std::size_t first, std::span<Complex> output) {
            std::copy_n(input.begin() + first, output.size(), output.begin());
        }, work, {});
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto position = crop.output_origin + (i + crop.first_output) * crop.output_stride;
        Complex expected{};
        for (std::size_t j = 0; j < taps.size(); ++j)
            if (position >= j && position - j < input.size()) expected += input[position - j] * taps[j];
        near(values[i], expected, 2e-12, "compiled FIR lost a finite-convolution tail or crop phase");
    }
    const auto receipt = device->receipt();
    check(receipt.precision == "fp64" && receipt.executed && work.forward_ffts == 4 &&
          work.inverse_ffts == 3 && work.fir_input_rows == input.size(),
          "compiled FIR did not report tight-clock work");
}

void coarse_helper_dynamic_range(r::ReceiverBulkBackend& backend) {
    // Exercise the real coarse backend's raw-I/Q -> automatic complex64 -> FIR
    // boundary with a weak in-band component beside a strong out-of-band tone.
    // Compare against an independent double-precision finite convolution; this
    // is arithmetic parity evidence, not a claim that this test filter or its
    // stop-band is the production channel specification.
    constexpr std::size_t taps_count = 129, input_count = 1024, fft_rows = 2048;
    constexpr double fs = 80., cutoff = .12, data_frequency = .035;
    std::array<double, taps_count> taps{};
    const auto midpoint = static_cast<double>((taps_count - 1) / 2);
    for (std::size_t i = 0; i < taps.size(); ++i) {
        const auto offset = static_cast<double>(i) - midpoint;
        const auto ideal = offset == 0 ? 2 * cutoff :
            std::sin(tau * cutoff * offset) / (std::numbers::pi * offset);
        // A bounded Hann-windowed sinc is sufficient to exercise real FIR
        // overlap-add and stop-band input without adding a filter-design API.
        const auto window = .5 - .5 * std::cos(tau * i / (taps_count - 1));
        taps[i] = ideal * window;
    }
    const auto tap_sum = std::accumulate(taps.begin(), taps.end(), 0.);
    for (auto& tap : taps) tap /= tap_sum;

    auto device = backend.make_coarse(fs, .25);
    check(device->receipt().precision == "fp32_bulk_fp64_coordinates_and_reductions",
          "strong-Helper fixture did not select the automatic lower-precision coarse path");
    r::CoarseBulkWork work;
    const auto filter = device->prepare_filter(taps, fft_rows, work, {});
    constexpr std::size_t first_output = 64, output_count = 384, stride = 2;
    const r::CoarseFirCrop crop{input_count, 512, 0, stride, first_output, output_count};
    std::vector<Complex> input(input_count);
    std::mt19937_64 generator(811);
    const auto uniform = [&] {
        return (static_cast<double>(generator() >> 11) + .5) / 9007199254740992.;
    };
    // The listed levels are Helper-to-DATA power ratios. A 90 dB ratio is
    // about 31,623:1 in amplitude, within FP32's exponent range; it is useful
    // here to quantify round-off at the pre-FIR cast rather than presume that
    // FP32 must fail solely because the level ratio is large.
    struct Case { double helper_db, helper_frequency; bool data; };
    constexpr std::array cases{
        Case{0, .25, true}, Case{30, .25, true}, Case{60, .25, true},
        Case{90, .25, true}, Case{90, .115, true}, Case{0, .25, false}
    };
    for (const auto& test : cases) {
        const auto helper_amplitude = std::pow(10., test.helper_db / 20.);
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto n = static_cast<double>(i);
            const auto data = test.data ? rotation(data_frequency * n) : Complex{};
            const auto helper = helper_amplitude * rotation(test.helper_frequency * n + .173);
            const auto noise = .015 * Complex(uniform() - .5, uniform() - .5);
            input[i] = data + helper + noise;
        }
        const auto actual_values = device->project_fir(*filter, crop,
            [&](std::size_t first, std::span<Complex> output) {
                std::copy_n(input.begin() + first, output.size(), output.begin());
            }, work, {});
        check(actual_values.size() == output_count,
              "strong-Helper coarse projection returned incomplete finite-FIR output");
        double largest_error = 0;
        for (std::size_t i = 0; i < output_count; ++i) {
            const auto position = (first_output + i) * stride;
            Complex expected{};
            for (std::size_t j = 0; j < taps.size(); ++j)
                expected += input[position - j] * taps[j];
            largest_error = std::max(largest_error, std::abs(actual_values[i] - expected));
        }
        // Compare with DATA amplitude, independent of residual Helper power.
        // The looser absolute bound accommodates accumulated FP32 FIR error
        // while catching loss of the weak in-band component at this boundary.
        check(largest_error <= .05,
              "automatic FP32 coarse FIR exceeded the predeclared 5% DATA-amplitude error bound beside a strong Helper tone");
    }
    check(work.fir_input_rows > 0 && device->receipt().executed,
          "strong-Helper fixture did not execute the tensor coarse FIR");
}

void coarse(r::ReceiverBulkBackend& backend) {
    constexpr std::size_t kernel = 7, arrivals = 5;
    constexpr double fs = 80, variance = .7;
    const std::array<double, 5> frequencies{-1., -.5, 0., .5, 1.};
    std::array<Complex, kernel + arrivals - 1> observation{};
    std::array<std::array<Complex, kernel>, 4> columns{};
    for (std::size_t i = 0; i < observation.size(); ++i)
        observation[i] = rotation(.01 * i * i) * (.5 + .1 * i);
    for (std::size_t j = 0; j < columns.size(); ++j)
        for (std::size_t i = 0; i < kernel; ++i) columns[j][i] = rotation(.03 * (i + j) * (i + j)) * (.2 + .1 * j);
    const std::array<r::CoarseBulkWorkspace, 2> workspaces{{
        {{columns[0], columns[1]}, .4, .8, {.1, -.05}},
        {{columns[2], columns[3]}, .3, .7, {-.06, .02}}
    }};
    constexpr std::size_t fft_rows = 16;
    const r::CoarseBulkGeometry geometry{2, kernel, fft_rows, arrivals, frequencies.size(), fs, variance};
    std::array<std::vector<Complex>, 3> phases, convolution;
    std::array<r::CoarseZoomGroup, 3> groups{};
    const std::array<std::size_t, 3> starts{0, 2, 2}, sizes{3, 1, 3};
    for (std::size_t group = 0; group < groups.size(); ++group) {
        const auto step = sizes[group] == 1 ? 0. : .5 / fs;
        const auto first = frequencies[starts[group]] / fs;
        phases[group].resize(kernel);
        for (std::size_t i = 0; i < kernel; ++i) phases[group][i] = rotation(-first * i - .5 * step * i * i);
        if (sizes[group] > 1) {
            convolution[group].resize(fft_rows);
            for (std::size_t i = 0; i < sizes[group]; ++i)
                convolution[group][i] = rotation(.5 * step * i * i);
            for (std::size_t i = 1; i < kernel; ++i)
                convolution[group][fft_rows - i] = rotation(.5 * step * i * i);
        }
        groups[group] = {starts[group], sizes[group], phases[group], convolution[group]};
    }

    for (const bool lag : {false, true}) {
        auto device = backend.make_coarse(fs, .25);
        r::CoarseBulkWork preparation, work;
        device->prepare(geometry, observation, workspaces,
                        lag ? std::span<const r::CoarseZoomGroup>(groups) : std::span<const r::CoarseZoomGroup>{},
                        preparation, {});
        std::size_t seen = 0;
        const auto sink = [&](std::size_t first, std::size_t count, std::span<const double> values) {
            check(first == seen && values.size() == count * frequencies.size() * arrivals,
                  "compiled coarse batch skipped part of the public grid");
            for (std::size_t w = 0; w < count; ++w) {
                const auto& workspace = workspaces[first + w];
                for (std::size_t f = 0; f < frequencies.size(); ++f) {
                    for (std::size_t i = 0; i < arrivals; ++i) {
                        std::array<Complex, 2> correlation{};
                        for (std::size_t tap = 0; tap < correlation.size(); ++tap)
                            for (std::size_t j = 0; j < kernel; ++j)
                                correlation[tap] += observation[i + j] * std::conj(workspace.columns[tap][j]) *
                                                    rotation(-frequencies[f] * (i + j) / fs);
                        const auto expected = (workspace.inverse_a * std::norm(correlation[0]) +
                            workspace.inverse_d * std::norm(correlation[1]) +
                            2 * std::real(std::conj(correlation[0]) * workspace.inverse_b * correlation[1])) / variance;
                        near(values[(w * frequencies.size() + f) * arrivals + i], expected, 3e-6,
                             "compiled frequency/lag scores differ from direct two-ray correlations");
                    }
                }
            }
            seen += count;
        };
        if (lag) device->lag_tile(0, workspaces.size(), sink, work, {});
        else device->frequency_tile(frequencies, sink, work, {});
        check(seen == workspaces.size() && work.workspace_frequency_pairs == workspaces.size() * frequencies.size() &&
              work.score_rows == work.workspace_frequency_pairs * arrivals && work.inverse_ffts > 0 &&
              device->receipt().precision == "fp32_bulk_fp64_coordinates_and_reductions",
              "compiled coarse scoring did not finish the bounded full grid");
        if (lag) check(work.singleton_dot_terms == workspaces.size() * 2 * arrivals * kernel,
                       "compiled lag scoring skipped the touching singleton");
    }
}

void fine(r::ReceiverBulkBackend& backend) {
    constexpr std::size_t rows = 17, taps = 2, states = 3, tile_size = 8192;
    using Tile = std::array<Complex, tile_size>;
    const auto first_tile = std::make_shared<Tile>();
    const auto second_tile = std::make_shared<Tile>();
    for (std::size_t i = 0; i < tile_size; ++i) {
        (*first_tile)[i] = rotation(.021 * i) * .1;
        (*second_tile)[i] = rotation(.021 * (tile_size + i)) * .1;
    }
    const std::array<std::size_t, 3> indices{0, 3, 8};
    struct alignas(16) Input {
        std::uint64_t padding;
        std::array<Complex, 3> values;
    };
    // The caller can provide only the native Complex alignment, even when
    // ATen's complex scalar requires a wider one.
    const Input input{0, {{{.4, -.1}, {.2, -.3}, {-.3, .6}}}};
    const auto& y = input.values;
    const r::FineBulkBlock block{rows, taps, indices, y, .8};
    auto device = backend.make_fine(5, .0005);
    std::array<double, rows> offsets{};
    std::array<Complex, rows> direct{};
    for (std::size_t i = 0; i < rows; ++i) {
        offsets[i] = i;
        direct[i] = Complex(.03 * i, -.02 * i);
    }
    const r::FineMappingSource source = [&](std::size_t candidate, std::size_t first, std::size_t count,
                                             const r::BulkMappedSink& sink) {
        check(first == 0 && count == rows, "fine tiny fixture was incorrectly split");
        if (candidate == 2) return;
        const std::array<r::BulkMappedColumn, 2> mappings{{
            {.17, 1.04, .03 * candidate, -.02, -2, 0, {{first_tile, *first_tile}}},
            {.51, .97, -.04 * candidate, .013, tile_size - 4, 1,
                {{first_tile, *first_tile}, {second_tile, *second_tile}}}
        }};
        const std::array<r::BulkDirectColumn, 1> fallback{{{3, direct, "test-native-direct"}}};
        sink(offsets, mappings, fallback);
    };
    std::array<r::FineBulkStatistics, states> measured{};
    device->statistics(block, states, source, measured, {});
    const auto cubic = [&](double coordinate) {
        const auto center = static_cast<int>(std::floor(coordinate));
        const auto u = coordinate - center;
        Complex out{};
        for (int j = -1; j < 3; ++j) {
            double w = 1;
            for (int m = -1; m < 3; ++m) if (j != m) w *= (u - m) / (j - m);
            const auto index = center + j;
            if (index < 0 || index >= 2 * static_cast<int>(tile_size)) continue;
            out += w * (index < static_cast<int>(tile_size) ? (*first_tile)[index] : (*second_tile)[index - tile_size]);
        }
        return out;
    };
    for (std::size_t candidate = 0; candidate < states; ++candidate) {
        std::array<std::array<Complex, indices.size()>, 4> projected{};
        if (candidate != 2) {
            for (std::size_t i = 0; i < rows; ++i) {
                const std::array<Complex, 4> row{{
                    cubic(-2 + .17 + 1.04 * i) * rotation(.03 * candidate - .02 * i),
                    cubic(tile_size - 4 + .51 + .97 * i) * rotation(-.04 * candidate + .013 * i),
                    {}, direct[i]}};
                for (std::size_t column = 0; column < row.size(); ++column)
                    for (std::size_t mode = 0; mode < indices.size(); ++mode)
                        projected[column][mode] += row[column] *
                            rotation(-static_cast<double>(indices[mode] * i) / rows) / std::sqrt(rows);
            }
        }
        check(measured[candidate].nonzero_basis == (candidate != 2), "compiled fine basis energy witness changed");
        for (std::size_t h = 0; h < 3; ++h) {
            for (std::size_t i = 0; i < taps; ++i) {
                Complex correlation{};
                for (std::size_t mode = 0; mode < indices.size(); ++mode)
                    if (h) correlation += std::conj(projected[(h - 1) * taps + i][mode]) * y[mode];
                near(measured[candidate].correlations[h * taps + i], correlation / block.variance, 2e-12,
                     "compiled fine projection/correlation differs from independent DFT");
                for (std::size_t j = 0; j < taps; ++j) {
                    Complex gram{};
                    for (std::size_t mode = 0; mode < indices.size(); ++mode) {
                        if (h) gram += std::conj(projected[(h - 1) * taps + i][mode]) *
                                       projected[(h - 1) * taps + j][mode];
                    }
                    near(measured[candidate].gram[(h * taps + i) * taps + j], gram / block.variance, 2e-12,
                         "compiled fine Gram differs from independent Hermitian product");
                }
            }
        }
    }
    const auto before = device->receipt();
    device->statistics(block, states, source, measured, {});
    const auto after = device->receipt();
    check(before.precision == "fp64" && before.uploaded_tiles == 2 && after.uploaded_tiles == 2 &&
          !after.cuda_fused_mapped_launches && !after.cuda_fused_mapped_values &&
          !after.cuda_direct_gram_values &&
          after.cuda_bmm_gram_values == (after.cuda ? 2 * states * 2 * taps * taps : 0) &&
          before.mapped_columns == 4 && before.direct_columns == 2 && before.statistics_blocks == states &&
          before.direct_fallback_counts.back() == 2 &&
          after.tile_hits > before.tile_hits && after.statistics_blocks == 2 * states &&
          after.fft_rows == 2 * states * 2 * taps * rows,
          "compiled fine work or resident tile reuse was not recorded");
    const r::FineBulkBlock dc{rows, taps, std::span(indices).first(1), std::span(y).first(1), .8};
    device->statistics(dc, states, source, measured, {});
    const auto dc_work = device->receipt();
    check(dc_work.fft_calls == after.fft_calls && dc_work.dc_reduction_values == states * 2 * taps * rows &&
          dc_work.statistics_blocks == after.statistics_blocks + states &&
          device->completed_work().projected_values == dc_work.projected_values,
          "compiled DC projection did not report its actual reduction without a fabricated FFT");
    Complex direct_dc{};
    for (const auto value : direct) direct_dc += value / std::sqrt(rows);
    near(measured[0].correlations[5], std::conj(direct_dc) * y[0] / block.variance, 2e-12,
         "compiled DC reduction changed unitary normalization");
}

void gram_mode_boundary(r::ReceiverBulkBackend& backend) {
    constexpr std::size_t rows = 160, maximum_modes = 129;
    constexpr double variance = .8;
    constexpr std::array<std::size_t, 4> tones{3, 31, 41, 128};
    std::array<std::array<Complex, tones.size()>, 2> amplitudes{{{.25, {.15, .2}, 0., .004}}};
    for (std::size_t k = 0; k < tones.size(); ++k)
        amplitudes[1][k] = Complex(.9, .2) * amplitudes[0][k];
    amplitudes[1][2] = {.0001, -.0002};
    std::array<double, rows> offsets{};
    std::array<std::array<Complex, rows>, 2> samples{};
    for (std::size_t row = 0; row < rows; ++row) {
        offsets[row] = row;
        for (std::size_t tap = 0; tap < samples.size(); ++tap)
            for (std::size_t k = 0; k < tones.size(); ++k)
                samples[tap][row] += amplitudes[tap][k] * rotation(static_cast<double>(tones[k] * row) / rows);
    }

    std::array<std::size_t, maximum_modes> bins{};
    std::iota(bins.begin(), bins.end(), 0);
    const std::array<Complex, maximum_modes> y{};
    const std::array<r::BulkDirectColumn, 2> direct{{
        {0, samples[0], "test-native-direct"}, {1, samples[1], "test-native-direct"}}};
    auto device = backend.make_fine(497, .25);
    for (std::size_t taps : {1, 2}) {
        for (std::size_t modes : {1, 128, 129}) {
            const r::FineBulkBlock block{rows, taps, std::span(bins).first(modes), std::span(y).first(modes), variance};
            const auto before = device->receipt();
            std::array<r::FineBulkStatistics, 1> output{};
            device->statistics(block, 1, [&](std::size_t candidate, std::size_t first, std::size_t count,
                                             const r::BulkMappedSink& sink) {
                check(candidate == 0 && first == 0 && count == rows, "small Gram fixture split unexpectedly");
                sink(offsets, {}, std::span(direct).first(taps));
            }, output, {});
            const auto after = device->receipt();
            const auto direct_values = after.cuda_direct_gram_values - before.cuda_direct_gram_values;
            const auto bmm_values = after.cuda_bmm_gram_values - before.cuda_bmm_gram_values;
            check(after.precision == "fp32_bulk_fp64_coordinates_and_reductions" && output.front().nonzero_basis &&
                  direct_values == (after.cuda && modes == 128 ? 2 * taps * taps : 0) &&
                  bmm_values == (after.cuda && modes != 128 ? 2 * taps * taps : 0),
                  "ordinary Gram selected the wrong device or mode-range algorithm");

            const auto gram = std::span(output.front().gram);
            for (std::size_t i = 0; i < taps; ++i) {
                for (std::size_t j = 0; j < taps; ++j) {
                    const auto entry = i * taps + j;
                    Complex expected{};
                    for (std::size_t k = 0; k < tones.size(); ++k)
                        if (tones[k] < modes) expected += std::conj(amplitudes[i][k]) * amplitudes[j][k];
                    near(gram[taps * taps + entry], expected * (rows / variance), 2e-6,
                         "ordinary Gram changed the analytic tone projection");
                    check(gram[entry] == Complex{} && gram[2 * taps * taps + entry] == Complex{} &&
                          gram[taps * taps + entry] == std::conj(gram[taps * taps + j * taps + i]),
                          "ordinary Gram changed exact zero or Hermitian entries");
                }
            }
            if (taps == 2) {
                const double diagonal_product = gram[4].real() * gram[7].real();
                check(diagonal_product - std::norm(gram[5]) >= -1e-12 * diagonal_product,
                      "ordinary near-rank-one Gram lost positive semidefiniteness");
            }
        }
    }
}

void full_slot(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    constexpr unsigned fs = 497;
    v::ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = 1725;
    config.noise_variance = .25;
    v::ReceiverSlotPlan slot;
    slot.index = 37;
    slot.first_sample = 20'000;
    slot.window_capture_lead_seconds = -17.7;
    slot.source_start_seconds = 18.1;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.bit_seconds = .001;
    slot.format.pulse_shape = v::PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.encrypt = true;
    const auto key = datapump::Crypto::amateur();
    v::PatternSource source(slot.format, key, 1);
    const auto count = source.geometry().source_samples + 403;
    check(count < 6000, "full tensor scorer fixture grew beyond the declared input");
    slot.stop_sample = slot.first_sample + count;
    slot.quiet_first_sample = slot.first_sample + 230;
    slot.quiet_stop_sample = slot.first_sample + count - 223;
    std::vector<Complex> iq(count);
    for (std::size_t i = 0; i < count; ++i)
        iq[i] = .7 * source.sample_at((static_cast<long double>(i) / fs -
            slot.window_capture_lead_seconds - slot.source_start_seconds) * fs);
    const auto paired = [&](std::span<const Complex> samples) {
        const auto native = v::score_receiver_slot(config, slot, key, samples, {}, 1,
                                                   v::receiver_slot_workspace(config, slot, 1));
        auto tensor_config = config;
        tensor_config.bulk_backend = backend;
        const auto before = backend->receipt();
        const auto tensor = v::score_receiver_slot(tensor_config, slot, key, samples, {}, 1,
                                                   v::receiver_slot_workspace(tensor_config, slot, 1));
        const auto after = backend->receipt();
        check(native.complete && tensor.complete && tensor.bit == native.bit &&
              native.decision == tensor.decision && native.fine_candidates == tensor.fine_candidates &&
              native.adaptive_peak_states == tensor.adaptive_peak_states &&
              native.coarse_frequency_points == tensor.coarse_frequency_points &&
              native.coarse_workspaces == tensor.coarse_workspaces &&
              native.zero_h0_verified && tensor.zero_h0_verified,
              "production native and tensor score changed bounded slot decision or state work");
        near(tensor.operational_log_e, native.operational_log_e, 5e-6,
             "production native and tensor slot scores differ");
        check(after.fft_calls > before.fft_calls && after.fir_input_rows > before.fir_input_rows &&
              after.score_rows > before.score_rows && after.mapped_values > before.mapped_values &&
              after.statistics_blocks > before.statistics_blocks && after.reserved_bytes == 0,
              "production slot did not actually execute and release both tensor stages");
        if (after.cuda) {
            check(after.cuda_fused_mapped_launches > before.cuda_fused_mapped_launches &&
                  after.cuda_fused_mapped_values - before.cuda_fused_mapped_values ==
                      after.mapped_values - before.mapped_values,
                  "ordinary CUDA slot did not complete its fused mapped work");
        } else {
            check(!after.cuda_fused_mapped_launches && !after.cuda_fused_mapped_values,
                  "CPU tensor scoring credited CUDA fused work");
        }
        return native;
    };
    check(paired(iq).bit == 1, "paired tensor DATA fixture did not decode the selected bit");
    for (const auto attenuation : {.0615, .0621}) {
        auto near_threshold = iq;
        for (auto& sample : near_threshold) sample *= attenuation;
        const auto score = paired(near_threshold);
        check(std::abs(score.operational_log_e - score.operational_threshold) < .7 &&
              score.operational_passes_conditional_bound == (attenuation > .062),
              "paired tensor fixture did not exercise the intended threshold side");
    }
    std::mt19937_64 generator(37);
    const auto uniform = [&] {
        return (static_cast<double>(generator() >> 11) + .5) / 9007199254740992.;
    };
    for (auto& sample : iq) sample = rotation(uniform()) * std::sqrt(-config.noise_variance * std::log(uniform()));
    check(paired(iq).decision == v::SlotDecision::quiet, "paired tensor noise-only fixture did not score physical quiet");
}

void helper_composite_slot(const std::shared_ptr<r::ReceiverBulkBackend>& backend,
                           double requested_ratio_db) {
    // Use the same native PublicProfile -> Emission -> StreamingChannel path
    // as the no-Tensor Helper test, with its channel seed, timing error and
    // Doppler. The 6 kHz, 1725 Hz geometry cannot legally place a +60 dB
    // Helper under the public low-tone leakage guard. Keep +30 dB at the
    // original geometry; use the smallest bounded higher-rate fixture already
    // shown to satisfy the unmodified guard for +60 dB.
    const unsigned fs = requested_ratio_db >= 60 ? 9000 : 6000;
    constexpr std::size_t fixture_workspace_ceiling = 2 * 1024 * mib;
    v::PublicProfileConfig settings;
    settings.format.sample_rate = fs;
    settings.format.inner_rate_hz = 120;
    settings.format.added_symbols = 1;
    settings.format.bit_seconds = .001;
    settings.format.pulse_shape = v::PulseShape::TaperedRrc;
    settings.format.encrypt = true;
    settings.format.epoch = 1793850000;
    settings.format.symbol_ordinal = 15;
    settings.data_if_hz = fs == 6000 ? 1725 : 2450;
    settings.helper_enabled = true;
    settings.helper.data_power_w = 4;
    settings.helper.ratio_limit_db = requested_ratio_db;
    const auto requested_ratio = std::pow(10., requested_ratio_db / 10.);
    settings.helper.power_w = settings.helper.data_power_w * (1 + requested_ratio);
    settings.helper.callsign = "TRIAD";
    settings.capture_lead_seconds = 2;
    settings.capture_trail_seconds = .4;
    settings.arrival_low_seconds = -.4;
    settings.arrival_high_seconds = .4;
    settings.cfo_bound_hz = 1.5;
    const auto key = datapump::Crypto::amateur();
    const v::PublicProfile profile(settings, key);
    const auto slot = profile.slot(0);
    check(slot.observation.size() < (fs == 6000 ? 360'000 : 550'000) &&
          slot.observation.stop < (fs == 6000 ? 1'100'000 : 1'800'000),
          "strong-Helper complete slot exceeds its fixed sample budget");
    const auto& helper_plan = profile.helper_plan();
    const auto effective_ratio = helper_plan.effective_power_w / settings.helper.data_power_w;
    check(std::abs(10 * std::log10(effective_ratio) - requested_ratio_db) < 1e-9,
          "Helper profile did not reach the predeclared Helper/Data ratio cap");
    check(helper_plan.frequency_hz < fs / 2. - 250,
          "strong-Helper placement exceeded the public high-tone sample-rate guard");

    const std::array<std::uint8_t, 1> bits{1};
    v::EmissionOptions emission_options;
    emission_options.bits = bits;
    v::Emission emission(profile, key, emission_options);
    v::StreamChannelConfig channel_config;
    channel_config.effects.sample_rate = fs;
    channel_config.effects.occupied_bandwidth_hz = profile.geometry().occupied_bandwidth_hz;
    channel_config.effects.nominal_conversion_if_hz = settings.data_if_hz;
    channel_config.effects.capture_lead_seconds = settings.capture_lead_seconds;
    channel_config.effects.capture_trail_seconds = settings.capture_trail_seconds;
    channel_config.effects.timing_offset_seconds = .18;
    channel_config.effects.static_doppler_hz = .357;
    channel_config.effects.seed = 37;
    // Keep the generated white-noise density fixed across the 6/9 kHz sample
    // rates. Variance scales with sample rate so the +60 dB case does not gain
    // 10*log10(9000/6000) dB of C/N0 merely from denser sampling.
    channel_config.complex_noise_variance = .02 * static_cast<double>(fs) / 6000;
    channel_config.minimum_capture_samples = slot.observation.stop;
    v::StreamingChannel channel(emission.source(), channel_config);

    auto config = v::ReceiverConfig{};
    config.sample_rate_hz = fs;
    config.data_if_hz = settings.data_if_hz;
    config.frequency_bound_hz = settings.cfo_bound_hz;
    config.noise_variance = channel_config.complex_noise_variance;
    config.data_basis_gain = 1 / std::sqrt(v::data_power_reference(profile.config().format, key).mean_norm);
    config.helper_enabled = true;
    config.helper = helper_plan;

    const auto source_geometry = v::geometry(profile.basis_format(slot));
    const auto required_band = .5 * source_geometry.occupied_bandwidth_hz + config.frequency_bound_hz;
    std::size_t projected_rate = fs;
    for (std::size_t divisor = 1; divisor <= fs / divisor; ++divisor) {
        if (fs % divisor) continue;
        for (const auto rate : {divisor, fs / divisor})
            if (.4 * rate > required_band) projected_rate = std::min(projected_rate, rate);
    }
    check(projected_rate > 10 && backend->make_coarse(projected_rate, 2 * config.clock_bound_ppm)->receipt().precision ==
          "fp32_bulk_fp64_coordinates_and_reductions",
          "production composite geometry did not select the automatic coarse FP32 cast policy");

    const auto seconds = [&](v::HelperBurstCoordinate coordinate) {
        return profile.calendar().relative_seconds(profile.origin(), coordinate);
    };
    v::ReceiverSlotPlan plan;
    plan.index = slot.index;
    plan.first_sample = slot.observation.first;
    plan.stop_sample = slot.observation.stop;
    plan.quiet_first_sample = slot.quiet_aperture.first;
    plan.quiet_stop_sample = slot.quiet_aperture.stop;
    plan.format = profile.basis_format(slot);
    plan.arrival_lower_seconds = settings.arrival_low_seconds;
    plan.arrival_upper_seconds = settings.arrival_high_seconds;
    plan.window_capture_lead_seconds = settings.capture_lead_seconds -
        static_cast<long double>(plan.first_sample) / fs;
    plan.source_start_seconds = seconds(slot.tx.support_begin);
    plan.helper_begin_seconds = seconds(slot.tx.helper_begin);
    plan.helper_end_seconds = seconds(slot.tx.helper_end);

    std::array<Complex, 4096> discard{};
    while (channel.position() < plan.first_sample) {
        const auto take = std::min<std::uint64_t>(discard.size(), plan.first_sample - channel.position());
        check(channel.read(std::span(discard).first(take)) == take,
              "truncated strong-Helper capture before the public slot");
    }
    std::vector<Complex> iq(slot.observation.size());
    std::vector<std::uint8_t> valid(iq.size());
    for (std::size_t i = 0; i < iq.size();) {
        const auto take = std::min(discard.size(), iq.size() - i);
        check(channel.read(std::span(iq).subspan(i, take), std::span(valid).subspan(i, take)) == take,
              "truncated strong-Helper capture inside the public slot");
        i += take;
    }
    const auto emitted = emission.report();
    check(emitted.data_energy > 0 && emitted.helper_energy > 0 &&
          std::abs(10 * std::log10(emitted.reference_helper_ratio / requested_ratio)) < .1,
          "composite emission did not preserve the requested Helper/Data power ratio");

    // Validate the actual noisy Helper measurement, including its public
    // frequency span. The +0.357 Hz simulated shift is an evaluator-only
    // assertion; the receiver itself receives only these samples and bounds.
    const auto helper = r::score_helper(config, plan, iq, valid,
                                         r::helper_workspace(config, plan));
    check(helper.narrowed && helper.reason == "pilot_aware_onset_relative_frequency_proposal" &&
          !helper.intervals.empty(),
          "strong emitted Helper did not acquire a narrowed DATA-frequency proposal");
    double span = 0;
    bool covers_channel_shift = false;
    for (const auto& interval : helper.intervals) {
        span += interval.upper_hz - interval.lower_hz;
        covers_channel_shift |= interval.lower_hz <= .357 && .357 <= interval.upper_hz;
    }
    check(covers_channel_shift && span < 2 * config.frequency_bound_hz,
          "Helper frequency proposal omitted the simulated shift or failed to narrow the public prior");

    auto tensor_config = config;
    tensor_config.bulk_backend = backend;
    // Admit the exact receiver implementation workspaces for the same single
    // worker used below, retaining the original 512 MiB floor. The optional
    // tensor backend adds its canonical host staging quote. Bound this test
    // fixture explicitly at 2 GiB; it is not an operating-system default.
    const auto native_workspace = v::receiver_slot_workspace(config, plan, 1);
    const auto tensor_workspace = v::receiver_slot_workspace(tensor_config, plan, 1);
    const auto memory_limit = std::max({512 * mib, native_workspace, tensor_workspace});
    check(memory_limit <= fixture_workspace_ceiling,
          "strong-Helper exact receiver workspace quote exceeds its predeclared 2 GiB fixture ceiling");
    std::cout << "Composite receiver workspace native " << native_workspace << " bytes; tensor "
              << tensor_workspace << " bytes; admitted " << memory_limit << " bytes\n";
    const auto native = v::score_receiver_slot(config, plan, key, iq, valid, 1, memory_limit);
    const auto before = backend->receipt();
    const auto tensor = v::score_receiver_slot(tensor_config, plan, key, iq, valid, 1, memory_limit);
    const auto after = backend->receipt();
    check(native.complete && tensor.complete && native.decision == v::SlotDecision::data &&
          tensor.decision == v::SlotDecision::data && native.bit == 1 && tensor.bit == native.bit &&
          native.helper_status == "pilot_aware_onset_relative_frequency_proposal" &&
          tensor.helper_status == native.helper_status && native.fine_candidates == tensor.fine_candidates &&
          native.adaptive_peak_states == tensor.adaptive_peak_states &&
          native.coarse_frequency_points == tensor.coarse_frequency_points &&
          native.coarse_workspaces == tensor.coarse_workspaces && native.zero_h0_verified && tensor.zero_h0_verified,
          "strong-Helper native and automatic tensor receivers changed DATA acquisition or work");
    near(tensor.operational_log_e, native.operational_log_e, 5e-6,
         "strong-Helper native and automatic tensor DATA scores differ");
    check(after.executed && after.cuda == before.cuda && after.fft_calls > before.fft_calls &&
          after.fir_input_rows > before.fir_input_rows && after.score_rows > before.score_rows &&
          after.mapped_values > before.mapped_values && after.statistics_blocks > before.statistics_blocks &&
          after.reserved_bytes == 0 && after.precision == "automatic",
          "strong-Helper scoring did not execute bounded bulk work on the requested automatic backend");
    std::cout << "Composite Helper/Data " << requested_ratio_db << " dB; IF " << settings.data_if_hz
              << " Hz; high Helper " << helper_plan.frequency_hz << " Hz; noise variance "
              << channel_config.complex_noise_variance << "; measured "
              << 10 * std::log10(emitted.reference_helper_ratio) << " dB; Helper span " << span
              << " Hz; tensor " << after.device << (after.cuda ? " CUDA" : " CPU") << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool cuda = argc == 2 && std::string_view(argv[1]) == "--cuda";
        check(argc == 1 || cuda, "usage: test_triad_tensor [--cuda]");
        auto backend = v::make_tensor_receiver_backend({
            cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu, 0, 128 * mib, v::TensorPrecision::automatic});
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (cuda) cuda_mapping_pointwise();
#else
        check(!cuda, "CUDA tensor test requires the compiled mapping parity fixture");
#endif
        fir(*backend);
        coarse_helper_dynamic_range(*backend);
        coarse(*backend);
        fine(*backend);
        gram_mode_boundary(*backend);
        full_slot(backend);
        helper_composite_slot(backend, 30);
        helper_composite_slot(backend, 60);
        const auto receipt = backend->receipt();
        check(receipt.cuda == cuda && receipt.executed && receipt.workspace_limit_bytes == 128 * mib &&
              receipt.peak_reserved_bytes <= receipt.workspace_limit_bytes &&
              (!cuda || (receipt.upload_bytes && receipt.download_bytes && receipt.synchronizations &&
                         receipt.allocator_reserved_bytes <= receipt.workspace_limit_bytes)),
              "tensor backend did not provide the requested device execution or bounded workspace receipt");
        auto tiny = v::make_tensor_receiver_backend({v::TensorDevice::cpu, 0, mib, v::TensorPrecision::fp64});
        auto coarse_tiny = tiny->make_coarse(100, 0);
        r::CoarseBulkWork work;
        const std::array<double, 1> taps{1.};
        bool rejected = false;
        try { (void)coarse_tiny->prepare_filter(taps, 8192, work, {}); }
        catch (const v::ReceiverWorkspaceError&) { rejected = true; }
        check(rejected && tiny->receipt().reserved_bytes == 0 && work.forward_ffts == 0,
              "tensor FFT bypassed its shared workspace reservation");
        std::cout << "Robust " << receipt.device << " compiled tensor parity and shared receiver checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
