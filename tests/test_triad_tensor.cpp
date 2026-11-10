#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
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
} // namespace

int main(int argc, char** argv) {
    try {
        const bool cuda = argc == 2 && std::string_view(argv[1]) == "--cuda";
        check(argc == 1 || cuda, "usage: test_triad_tensor [--cuda]");
        auto backend = v::make_tensor_receiver_backend({
            cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu, 0, 128 * mib, v::TensorPrecision::automatic});
        fir(*backend);
        coarse(*backend);
        fine(*backend);
        full_slot(backend);
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
