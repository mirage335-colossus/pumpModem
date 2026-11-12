#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_helper.hpp"
#include "triad_probe_json.hpp"
#include "search_fft.hpp"

#if DATAPUMP_TRIAD_TENSOR_CUDA
#include "triad_tensor_mapping.cuh"
#include "triad_tensor_mapping.hpp"
#include "triad_tensor_peaks.hpp"
#include <c10/cuda/CUDAException.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#endif

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <thread>

namespace {
namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;
using v::Complex;
constexpr std::size_t mib = 1024 * 1024;
constexpr double tau = 2 * std::numbers::pi;

void check(bool condition, const char* reason) {
    if (!condition) throw datapump::Error(reason);
}

void emit_component_identity(const r::BulkReceipt& receipt) {
    using datapump::triad::probe::object;
    using datapump::triad::probe::write_json;
    check(!receipt.executed, "component identity must precede numerical device work");
    write_json(std::cout, object({
        {"schema", "datapump.triad.native-component-identity.v1"},
        {"source_manifest_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
        {"build_options_sha256", DATAPUMP_TRIAD_PROBE_BUILD_SHA},
        {"fixture_source_sha256", DATAPUMP_TRIAD_COMPONENT_SOURCE_SHA},
        {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER},
        {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG},
#if DATAPUMP_TRIAD_TENSOR_CUDA
        {"mapping_fp32_arithmetic_contract", r::mapping_c64_arithmetic_contract},
#endif
        {"device", receipt.device}, {"device_name", receipt.device_name},
        {"cuda", receipt.cuda}, {"library_version", receipt.library_version},
        {"cuda_compute_major", receipt.cuda_compute_major},
        {"cuda_compute_minor", receipt.cuda_compute_minor},
        {"cuda_runtime_version", receipt.cuda_runtime_version},
        {"cuda_driver_version", receipt.cuda_driver_version},
        {"cuda_blas_version", receipt.cuda_blas_version},
        {"total_device_bytes", receipt.total_device_bytes},
        {"initial_free_device_bytes", receipt.initial_free_device_bytes},
        {"workspace_limit_bytes", receipt.workspace_limit_bytes},
        {"actual_device_work", false}}));
    std::cout << '\n' << std::flush;
}

void near(Complex a, Complex b, double tolerance, const char* reason) {
    check(std::abs(a - b) <= tolerance * std::max(1., std::abs(b)), reason);
}

Complex rotation(double cycles) {
    return std::polar(1., tau * cycles);
}

bool same_coarse_work(const r::CoarseBulkWork& a, const r::CoarseBulkWork& b) {
    return a.forward_ffts == b.forward_ffts && a.inverse_ffts == b.inverse_ffts &&
        a.fft_rows == b.fft_rows && a.fft_row_stages == b.fft_row_stages &&
        a.fir_input_rows == b.fir_input_rows && a.fir_output_contributions == b.fir_output_contributions &&
        a.score_rows == b.score_rows && a.workspace_frequency_pairs == b.workspace_frequency_pairs &&
        a.singleton_dot_terms == b.singleton_dot_terms;
}

#if DATAPUMP_TRIAD_TENSOR_CUDA
void cuda_arrival_peak_oracle() {
    const auto stream = c10::cuda::getStreamFromPool(false, 0);
    const c10::cuda::CUDAStreamGuard guard(stream);
    const auto host_double = at::TensorOptions().device(at::kCPU).dtype(at::kDouble);
    const auto device = at::Device(at::kCUDA, 0);
    const auto run = [&](const std::vector<std::vector<double>>& rows, std::size_t separation) {
        check(!rows.empty() && !rows.front().empty(), "empty arrival-peak oracle fixture");
        const auto arrivals = rows.front().size();
        std::vector<double> values;
        for (const auto& row : rows) {
            check(row.size() == arrivals, "arrival-peak oracle fixture has mixed row lengths");
            values.insert(values.end(), row.begin(), row.end());
        }
        const auto input = at::from_blob(values.data(), {static_cast<std::int64_t>(values.size())}, host_double)
            .to(device, at::kDouble);
        auto bytes = at::empty({static_cast<std::int64_t>(rows.size() * sizeof(r::CoarseBulkPeaks))},
                              at::TensorOptions().device(device).dtype(at::kByte));
        C10_CUDA_CHECK(r::cuda_select_coarse_peaks(input.const_data_ptr<double>(),
            reinterpret_cast<r::CoarseBulkPeaks*>(bytes.mutable_data_ptr<std::uint8_t>()),
            rows.size(), arrivals, separation, stream.stream()));
        const auto copied = bytes.to(at::kCPU).contiguous();
        std::vector<r::CoarseBulkPeaks> actual(rows.size());
        std::memcpy(actual.data(), copied.const_data_ptr<std::uint8_t>(), actual.size() * sizeof(actual.front()));
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (std::any_of(rows[row].begin(), rows[row].end(), [](double value) { return !std::isfinite(value); })) {
                check(actual[row].count == UINT64_MAX,
                      "CUDA arrival selector did not reject a nonfinite unselected tail");
                continue;
            }
            const auto expected = r::select_coarse_arrivals(rows[row], separation);
            check(actual[row].count == expected.count, "CUDA arrival selector changed the canonical frontier count");
            for (std::size_t i = 0; i < expected.count; ++i) {
                check(actual[row].lags[i] == expected.indices[i] &&
                      std::bit_cast<std::uint64_t>(actual[row].scores[i]) ==
                          std::bit_cast<std::uint64_t>(rows[row][expected.indices[i]]),
                      "CUDA arrival selector changed exact score bits, tie order or selected lags");
            }
        }
    };
    run({{-3.}, {0.}, {-0.}}, 1);
    for (const auto separation : {std::size_t{1}, std::size_t{2}})
        run({{-2., -1.}, {-1., -2.}, {-0., 0.}, {4., 4.}}, separation);
    for (const auto separation : {std::size_t{1}, std::size_t{2}, std::size_t{3}})
        run({{0., 3., 0., 2., 0.}}, separation);
    const std::vector<std::vector<double>> short_rows{
        {-8, -3, -7, -2, -9, -4, -8, -1, -8, -2, -7, -3, -8, -2, -6, -4, -7},
        {-3, 1, 1, 1, 0, 2, 2, 0, 1, 1, 0, -1, -2, -3, -4, -5, -6},
        {0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0},
        {9, 8, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 8, 9},
        {0, 8, 0, 0, 7, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        std::vector<double>(17, 0.), std::vector<double>(17, -4.)};
    for (const auto separation : {std::size_t{1}, std::size_t{4}, std::size_t{16}}) run(short_rows, separation);
    std::vector<std::vector<double>> long_rows(4, std::vector<double>(257, -3.));
    for (std::size_t i = 0; i < 257; ++i) {
        long_rows[0][i] = static_cast<double>(i % 17) - 8.;
        long_rows[1][i] = i >= 31 && i <= 96 ? 7. : -2.;
        long_rows[2][i] = (i == 0 || i == 256) ? -1. : -3.;
        long_rows[3][i] = static_cast<double>((i * 37 + 11) % 53) - 26.;
    }
    for (const auto separation : {std::size_t{1}, std::size_t{17}, std::size_t{256}}) run(long_rows, separation);
    auto nonfinite = short_rows;
    nonfinite[0].back() = std::numeric_limits<double>::quiet_NaN();
    nonfinite[1].back() = std::numeric_limits<double>::infinity();
    nonfinite[2].back() = -std::numeric_limits<double>::infinity();
    run(nonfinite, 4);
    check(r::cuda_select_coarse_peaks(nullptr, nullptr, 0, 1, 1, stream.stream()) == cudaErrorInvalidValue,
          "CUDA arrival selector accepted invalid zero-row/null geometry");
}

void cuda_mapping_rounding_holdouts() {
    const auto stream = c10::cuda::getStreamFromPool(false, 0);
    const c10::cuda::CUDAStreamGuard guard(stream);
    const auto host = at::TensorOptions().device(at::kCPU);
    const auto gpu = at::TensorOptions().device(at::kCUDA, 0);
    // Independent IEEE binary32 goldens, not values obtained from either
    // mapper. In the first case both real products round to 1 + 2^-22;
    // their subtraction is +0, while the imaginary sum is 2 + 2^-21.
    // The second case has exact a*c = 1 + 2^-24 - 2^-47, which rounds to 1;
    // fusing the subtraction would instead leave a nonzero residual.
    constexpr std::array<std::array<std::uint32_t, 6>, 6> cases{{
        {0x3f800001, 0x3f800000, 0x3f800001, 0x3f800002, 0x00000000, 0x40000002},
        {0x3f800001, 0x3f800000, 0x3f7fffff, 0x3f800000, 0x00000000, 0x40000000},
        {0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x00000000, 0x40000000},
        {0x00000000, 0x00000000, 0xbf800000, 0x00000000, 0x80000000, 0x00000000},
        {0x80000000, 0x80000000, 0x3f800000, 0x3f800000, 0x00000000, 0x80000000},
        {0x3f800000, 0x3f800000, 0x3f800000, 0xbf800000, 0x40000000, 0x00000000}}};
    std::array<c10::complex<float>, cases.size()> values{};
    std::array<float, cases.size()> cosine{}, sine{};
    for (std::size_t i = 0; i < cases.size(); ++i) {
        values[i] = {std::bit_cast<float>(cases[i][0]), std::bit_cast<float>(cases[i][1])};
        cosine[i] = std::bit_cast<float>(cases[i][2]);
        sine[i] = std::bit_cast<float>(cases[i][3]);
    }
    for (const auto target : {host, gpu}) {
        auto input = at::from_blob(values.data(), {static_cast<std::int64_t>(cases.size())},
            host.dtype(at::kComplexFloat)).to(target.dtype(at::kComplexFloat)).clone();
        const auto c = at::from_blob(cosine.data(), {static_cast<std::int64_t>(cases.size())},
            host.dtype(at::kFloat)).to(target.dtype(at::kFloat));
        const auto s = at::from_blob(sine.data(), {static_cast<std::int64_t>(cases.size())},
            host.dtype(at::kFloat)).to(target.dtype(at::kFloat));
        r::rotate_mapping_c64(input, c, s);
        const auto actual = at::view_as_real(input).contiguous().cpu();
        const auto* components = actual.const_data_ptr<float>();
        for (std::size_t i = 0; i < cases.size(); ++i)
            for (std::size_t part = 0; part < 2; ++part)
                check(std::bit_cast<std::uint32_t>(components[2 * i + part]) == cases[i][4 + part],
                      "FP32 mapping rotation violated independent rounding/cancellation/signed-zero golden");
    }

    // Exact identity rotation exercises the actual fused kernel independently
    // of the ATen renderer. Offsets produce cycles 0, -1 and -2, all wrapping
    // to angle +0: cos(+0)=1 and sin(+0)=+0 preserve both components. Do not
    // assume correctly rounded transcendental results at nonzero angles.
    constexpr std::int64_t rows = 3;
    const float c = std::bit_cast<float>(std::uint32_t{0x3f3504f3});
    const auto component = at::full({1, r::cuda_mapping_tile_samples}, c, gpu.dtype(at::kFloat));
    const auto tiles = at::complex(component, component);
    std::array<double, 4> coefficients{0., 1., 0., -1.};
    std::array<double, rows> positions{0., 1., 2.};
    std::array<std::int64_t, 3 + r::cuda_mapping_maximum_pins> descriptors{};
    descriptors[0] = 2;
    descriptors[1] = 8;
    const auto maps = at::from_blob(coefficients.data(), {1, 4}, host.dtype(at::kDouble)).to(gpu.dtype(at::kDouble));
    const auto offsets = at::from_blob(positions.data(), {rows}, host.dtype(at::kDouble)).to(gpu.dtype(at::kDouble));
    const auto table = at::from_blob(descriptors.data(), {1, static_cast<std::int64_t>(descriptors.size())},
        host.dtype(at::kLong)).to(gpu.dtype(at::kLong));
    for (bool fused : {false, true}) {
        auto result = at::empty({1, rows}, gpu.dtype(at::kComplexFloat));
        if (fused)
            C10_CUDA_CHECK(r::launch_cuda_mapped_c64(maps.const_data_ptr<double>(), table.const_data_ptr<std::int64_t>(),
                offsets.const_data_ptr<double>(), tiles.const_data_ptr<c10::complex<float>>(),
                result.mutable_data_ptr<c10::complex<float>>(), {rows, 1, 1, 1, rows, 1}, stream.stream()));
        else
            r::render_aten_mapping(result, tiles, maps, table, offsets, at::kFloat,
                r::cuda_mapping_tile_samples, r::cuda_mapping_maximum_pins);
        const auto actual = at::view_as_real(result).contiguous().cpu();
        const auto* components = actual.const_data_ptr<float>();
        for (std::size_t i = 0; i < rows; ++i)
            check(std::bit_cast<std::uint32_t>(components[2 * i]) == 0x3f3504f3 &&
                  std::bit_cast<std::uint32_t>(components[2 * i + 1]) == 0x3f3504f3,
                  "FP32 mapping violated independent unit-rotation/negative-phase golden");
    }
}

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

void staged_fir(r::ReceiverBulkBackend& backend) {
    auto device = backend.make_coarse(8, .0001);
    const std::array<double, 5> taps{.125, .25, .5, .25, .125};
    const r::CoarseFirCrop crop{80, 7, 1, 3, 6, 25};
    constexpr std::size_t first_chunk = 14;
    std::array<std::array<Complex, 80>, 2> input{};
    for (std::size_t lane = 0; lane < input.size(); ++lane)
        for (std::size_t i = 0; i < input[lane].size(); ++i)
            input[lane][i] = Complex(.001 * (lane + 1) * i * i, -.05 * (lane + 2) * i);
    r::CoarseBulkWork setup;
    const auto filter = device->prepare_filter(taps, 16, setup, {});
    const auto before = device->receipt();

    const auto check_output = [&](std::span<const Complex> source, std::span<const Complex> result) {
        check(result.size() == crop.output_rows, "staged compiled FIR returned an incomplete crop");
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto position = crop.output_origin + (i + crop.first_output) * crop.output_stride;
            Complex expected{};
            for (std::size_t j = 0; j < taps.size(); ++j)
                if (position >= j && position - j < source.size()) expected += source[position - j] * taps[j];
            near(result[i], expected, 2e-12, "staged compiled FIR lost order, tail or crop phase");
        }
    };

    // Two independent crops can fill their leading chunks on the same
    // session before either begins its serialized device transaction.
    std::mutex mutex;
    std::condition_variable ready;
    std::size_t leading_chunks = 0;
    std::array<std::exception_ptr, 2> failures;
    const auto run = [&](std::size_t lane) {
        try {
            auto next = first_chunk;
            r::CoarseBulkWork work;
            const auto result = device->project_fir(*filter, crop, [&](std::size_t first, std::span<Complex> values) {
                check(first == next, "compiled FIR filled a chunk out of order");
                if (first == first_chunk) {
                    std::unique_lock lock(mutex);
                    ++leading_chunks;
                    ready.notify_all();
                    check(ready.wait_for(lock, std::chrono::seconds(3), [&] { return leading_chunks == 2; }),
                          "compiled FIR serialized independent leading chunks");
                }
                next += values.size();
                std::copy_n(input[lane].begin() + first, values.size(), values.begin());
            }, work, {});
            check_output(input[lane], result);
            check(next == crop.input_rows && work.forward_ffts == 10 && work.inverse_ffts == 10 &&
                  work.fir_input_rows == crop.input_rows - first_chunk,
                  "staged compiled FIR omitted contributing chunks");
        } catch (...) {
            failures[lane] = std::current_exception();
        }
    };
    {
        std::jthread first(run, 0), second(run, 1);
    }
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    const auto after = device->receipt();
    if (after.cuda) {
        // Batches of 4, 4 and 2 include one short row. Each crop uses three
        // blocking uploads, two inter-batch waits and one blocking readback.
        check(after.upload_bytes - before.upload_bytes == 2 * (crop.input_rows - first_chunk) * sizeof(Complex) &&
              after.synchronizations - before.synchronizations == 12,
              "compiled FIR did not use the expected packed transfers and inter-batch waits");
    }

    // The second batch's fill can fail or cancel while the first owns device buffers.
    for (const bool cancel : {false, true}) {
        std::stop_source stop;
        r::CoarseBulkWork partial;
        bool rejected = false;
        try {
            (void)device->project_fir(*filter, crop, [&](std::size_t first, std::span<Complex> values) {
                if (first == first_chunk + 4 * crop.chunk_rows) {
                    if (!cancel) throw datapump::Error("staged FIR fill failure");
                    stop.request_stop();
                }
                std::copy_n(input[0].begin() + first, values.size(), values.begin());
            }, partial, stop.get_token());
        } catch (const datapump::Error& error) {
            rejected = std::string_view(error.what()) ==
                (cancel ? "Robust tensor operation cancelled" : "staged FIR fill failure");
        }
        check(rejected && partial.forward_ffts == 4 && partial.inverse_ffts == 4 &&
              device->receipt().reserved_bytes == before.reserved_bytes,
              "failed FIR lookahead did not drain its submitted batch and release its workspace");
    }
    if (after.cuda)
        check(device->receipt().synchronizations - after.synchronizations == 4,
              "failed FIR lookahead did not drain the stream after each first upload");
    r::CoarseBulkWork retried;
    const auto result = device->project_fir(*filter, crop, [&](std::size_t first, std::span<Complex> values) {
        std::copy_n(input[0].begin() + first, values.size(), values.begin());
    }, retried, {});
    check_output(input[0], result);
    check(retried.forward_ffts == 10 && device->receipt().reserved_bytes == before.reserved_bytes,
          "compiled FIR could not run a complete crop after a failed fill");
}

void tight_staged_fir() {
    // 2048-point FP64 FIR scratch needs 512 KiB per chunk. Once the filter
    // and output are resident, this workspace can admit only one chunk.
    auto backend = v::make_tensor_receiver_backend({v::TensorDevice::cpu, 0, mib, v::TensorPrecision::fp64});
    auto device = backend->make_coarse(100, 0);
    const std::array<double, 2> taps{.75, .25};
    r::CoarseBulkWork work;
    const auto filter = device->prepare_filter(taps, 2048, work, {});
    const r::CoarseFirCrop crop{5123, 1024, 0, 1, 0, 5124};
    const auto input = [&](std::size_t i) {
        return i < crop.input_rows ? Complex(.001 * i, -.01 * (i % 31)) : Complex{};
    };
    const auto result = device->project_fir(*filter, crop, [&](std::size_t first, std::span<Complex> values) {
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = input(first + i);
    }, work, {});
    check(result.size() == crop.output_rows, "tight staged FIR returned an incomplete crop");
    for (std::size_t i = 0; i < result.size(); ++i)
        near(result[i], taps[0] * input(i) + (i ? taps[1] * input(i - 1) : Complex{}), 2e-12,
             "tight staged FIR lost a chunk at width one");
    const auto receipt = device->receipt();
    check(work.fir_input_rows == crop.input_rows && work.forward_ffts == 7 && work.inverse_ffts == 6 &&
          receipt.fft_calls == 13 && receipt.peak_reserved_bytes <= mib,
          "tight staged FIR changed its complete work or exceeded the workspace");
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
        std::vector<double> published_scores;
        const auto sink = [&](std::size_t first, std::size_t count, std::span<const double> values) {
            check(first == seen && values.size() == count * frequencies.size() * arrivals,
                  "compiled coarse batch skipped part of the public grid");
            published_scores.insert(published_scores.end(), values.begin(), values.end());
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
        else {
            for (const auto separation : {std::size_t{1}, arrivals - 1}) {
                const auto before = device->receipt();
                r::CoarseBulkWork peak_work;
                std::size_t next = 0;
                bool published = false;
                const auto handled = device->frequency_peaks(frequencies, separation,
                    [&](std::size_t first, std::size_t count, std::span<const r::CoarseBulkPeaks> peaks) {
                        published = true;
                        check(first == next && count && peaks.size() == count * frequencies.size(),
                              "compressed coarse frontier skipped or reordered public rows");
                        for (std::size_t i = 0; i < peaks.size(); ++i) {
                            const auto scores = std::span(published_scores).subspan(
                                (first * frequencies.size() + i) * arrivals, arrivals);
                            const auto expected = r::select_coarse_arrivals(scores, separation);
                            check(peaks[i].count == expected.count, "compressed coarse frontier count changed");
                            for (std::size_t j = 0; j < expected.count; ++j)
                                check(peaks[i].lags[j] == expected.indices[j] &&
                                      std::bit_cast<std::uint64_t>(peaks[i].scores[j]) ==
                                          std::bit_cast<std::uint64_t>(scores[expected.indices[j]]),
                                      "compressed coarse frontier changed exact score bits or canonical order");
                        }
                        next += count;
                    }, peak_work, {});
                const auto after = device->receipt();
                if (before.cuda) {
                    check(handled && published && next == workspaces.size() && same_coarse_work(peak_work, work) &&
                          after.score_rows - before.score_rows == work.score_rows &&
                          after.cuda_arrival_peak_rows - before.cuda_arrival_peak_rows == work.workspace_frequency_pairs &&
                          after.cuda_arrival_peak_values - before.cuda_arrival_peak_values == work.score_rows &&
                          after.download_bytes - before.download_bytes == work.workspace_frequency_pairs * sizeof(r::CoarseBulkPeaks),
                          "compressed coarse publication lost full work or exact transfer/selector accounting");
                    std::stop_source stop;
                    stop.request_stop();
                    bool rejected = false, cancelled_publication = false;
                    r::CoarseBulkWork cancelled_work;
                    try {
                        (void)device->frequency_peaks(frequencies, separation,
                            [&](auto, auto, auto) { cancelled_publication = true; }, cancelled_work, stop.get_token());
                    } catch (const datapump::Error&) { rejected = true; }
                    check(rejected && !cancelled_publication && same_coarse_work(cancelled_work, {}) &&
                          device->receipt().score_rows == after.score_rows &&
                          device->receipt().cuda_arrival_peak_rows == after.cuda_arrival_peak_rows,
                          "cancelled compressed coarse work published or credited an unfinished frontier");
                } else {
                    check(!handled && !published && same_coarse_work(peak_work, {}) &&
                          after.upload_bytes == before.upload_bytes && after.download_bytes == before.download_bytes &&
                          after.synchronizations == before.synchronizations && after.fft_calls == before.fft_calls &&
                          after.score_rows == before.score_rows && after.executed == before.executed,
                          "unsupported CPU compressed publication performed work before declining");
                }
            }
        }
    }
}

// These deterministic component strata qualify a bounded numerical contract,
// not production receiver sensitivity or sustained throughput. The budgets are
// declared before device measurements: 0.2% FP16 and 1% INT8 maximum score error
// relative to the largest double-oracle score (with an absolute floor of one).
// Primitive threshold decisions have a separate, fixed two-percent margin.
struct MatrixCoarseFixture {
    static constexpr double fs = 80., variance = .7;
    std::size_t kernel, frequencies, arrivals, taps;
    std::vector<double> frequency;
    std::vector<std::vector<Complex>> columns;
    std::vector<r::CoarseBulkWorkspace> workspaces;
    std::vector<Complex> phase, convolution, exponentials, active, noise, zero;
    std::vector<double> active_scores, noise_scores, zero_scores;
    r::CoarseBulkGeometry geometry;
    r::CoarseZoomGroup group;

    MatrixCoarseFixture(std::size_t k, std::size_t f, std::size_t l, std::size_t rays,
                        std::size_t count = 2, bool full_oracle = true)
        : kernel(k), frequencies(f), arrivals(l), taps(rays), frequency(f),
          columns(count * rays, std::vector<Complex>(k)), workspaces(count), exponentials(k * f),
          active(k + l - 1), noise(active.size()), zero(active.size()),
          geometry{rays, k, std::bit_ceil(active.size()), l, f, fs, variance} {
        std::mt19937_64 generator(0x527814 + k + 7 * f);
        const auto unit = [&] { return static_cast<double>(generator() >> 11) * 0x1p-53; };
        for (auto& column : columns)
            for (auto& value : column) value = (.4 + .6 * unit()) * rotation(unit());
        for (std::size_t w = 0; w < workspaces.size(); ++w) {
            workspaces[w] = {{columns[w * taps], taps == 2 ? std::span<const Complex>(columns[w * taps + 1]) :
                std::span<const Complex>{}}, 1. / kernel, .7 / kernel, {.03 / kernel, -.02 / kernel}};
        }
        const auto step = 1. / kernel;
        const auto first = -static_cast<double>(frequencies / 2) * step;
        for (std::size_t f_index = 0; f_index < frequencies; ++f_index) {
            frequency[f_index] = (first + f_index * step) * fs;
            for (std::size_t j = 0; j < kernel; ++j)
                exponentials[f_index * kernel + j] = rotation(-(first + f_index * step) * j);
        }
        group = zoom_group(0, frequencies, phase, convolution);
        for (std::size_t j = 0; j < active.size(); ++j) {
            noise[j] = .2 * Complex(2 * unit() - 1, 2 * unit() - 1);
            active[j] = .05 * noise[j];
        }
        constexpr std::size_t planted_arrival = 7;
        const auto planted_frequency = frequency[frequencies / 2 + 3];
        for (std::size_t j = 0; j < kernel; ++j) {
            const auto value = .8 * columns[0][j] + (taps == 2 ? .3 * columns[1][j] : Complex{});
            active[j + planted_arrival] += value * rotation(planted_frequency * (j + planted_arrival) / fs);
        }
        if (full_oracle) {
            active_scores = oracle(active);
            noise_scores = oracle(noise);
            zero_scores.resize(workspaces.size() * frequencies * arrivals);
        }
    }

    r::CoarseZoomGroup zoom_group(std::size_t first, std::size_t count,
                                  std::vector<Complex>& input_phase, std::vector<Complex>& impulse) const {
        input_phase.resize(kernel);
        impulse.assign(std::bit_ceil(kernel + count - 1), {});
        const auto step = 1. / kernel;
        const auto start = (static_cast<double>(first) - static_cast<double>(frequencies / 2)) * step;
        for (std::size_t j = 0; j < kernel; ++j)
            input_phase[j] = rotation(-start * j - .5 * step * j * j);
        for (std::size_t j = 0; j < count; ++j)
            impulse[j] = rotation(.5 * step * j * j);
        for (std::size_t j = 1; j < kernel; ++j)
            impulse[impulse.size() - j] = rotation(.5 * step * j * j);
        return {first, count, input_phase, impulse};
    }

    double oracle_point(std::span<const Complex> input, std::size_t w, std::size_t f, std::size_t arrival) const {
        const auto& workspace = workspaces[w];
        std::array<Complex, 2> correlation{};
        for (std::size_t tap = 0; tap < taps; ++tap)
            for (std::size_t j = 0; j < kernel; ++j)
                correlation[tap] += input[arrival + j] * std::conj(workspace.columns[tap][j]) *
                                    exponentials[f * kernel + j];
        return (workspace.inverse_a * std::norm(correlation[0]) +
                workspace.inverse_d * std::norm(correlation[1]) +
                2 * std::real(std::conj(correlation[0]) * workspace.inverse_b * correlation[1])) / variance;
    }

    std::vector<double> oracle(std::span<const Complex> input) const {
        std::vector<double> scores(workspaces.size() * frequencies * arrivals);
        for (std::size_t w = 0; w < workspaces.size(); ++w)
            for (std::size_t f = 0; f < frequencies; ++f)
                for (std::size_t arrival = 0; arrival < arrivals; ++arrival)
                    scores[(w * frequencies + f) * arrivals + arrival] = oracle_point(input, w, f, arrival);
        return scores;
    }
};

std::size_t matrix_fallback_index(std::string_view reason) {
    const auto found = std::find(r::bulk_matrix_fallback_reasons.begin(), r::bulk_matrix_fallback_reasons.end(), reason);
    check(found != r::bulk_matrix_fallback_reasons.end(), "unknown matrix fallback test reason");
    return found - r::bulk_matrix_fallback_reasons.begin();
}

const char* matrix_math_name(v::TensorCoarseMath math) {
    if (math == v::TensorCoarseMath::fft) return "fft";
    if (math == v::TensorCoarseMath::fp16) return "fp16";
    return "int8";
}

std::uint64_t matrix_calls(const r::BulkReceipt& receipt, v::TensorCoarseMath math) {
    if (math == v::TensorCoarseMath::fft) return 0;
    if (math == v::TensorCoarseMath::fp16) return receipt.cuda_matrix_fp16_calls;
    return receipt.cuda_matrix_int8_calls;
}

void matrix_coarse(r::ReceiverBulkBackend& backend, v::TensorCoarseMath math,
                   const MatrixCoarseFixture& fixture, bool expect_matrix) {
    const auto label = std::string(matrix_math_name(math)) + " K=" + std::to_string(fixture.kernel) +
        " F=" + std::to_string(fixture.frequencies) + " L=" + std::to_string(fixture.arrivals) +
        " W=" + std::to_string(fixture.workspaces.size());
    const auto validate = [&](std::span<const double> actual, std::span<const double> expected,
                              std::size_t stratum, double tolerance) {
        const bool zero = stratum == 1;
        check(actual.size() == expected.size(), "matrix coarse score extent changed");
        const auto expected_winner = std::max_element(expected.begin(), expected.end()) - expected.begin();
        const auto maximum = expected[expected_winner];
        double error = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            check(std::isfinite(actual[i]) && actual[i] >= -1e-12 * std::max(1., maximum),
                  "matrix coarse score became nonfinite or negative");
            error = std::max(error, std::abs(actual[i] - expected[i]));
            if (zero) check(actual[i] == 0., "matrix coarse zero input retained old correlation state");
        }
        const auto normalized_error = error / std::max(1., maximum);
        if (normalized_error > tolerance)
            throw datapump::Error(label + " stratum=" + std::to_string(stratum) +
                " normalized score error=" + std::to_string(normalized_error) +
                " exceeds declared budget=" + std::to_string(tolerance));
        if (!zero) {
            const auto actual_winner = std::max_element(actual.begin(), actual.end()) - actual.begin();
            if (std::count(expected.begin(), expected.end(), maximum) != 1 || actual_winner != expected_winner) {
                std::size_t runner = expected_winner == 0 ? 1 : 0;
                for (std::size_t i = 0; i < expected.size(); ++i)
                    if (i != static_cast<std::size_t>(expected_winner) && expected[i] > expected[runner]) runner = i;
                std::ostringstream diagnostic;
                diagnostic << std::setprecision(17) << label << " stratum=" << stratum
                    << " winner=" << actual_winner << " differs from unique oracle=" << expected_winner
                    << " oracle_peak=" << maximum << " oracle_runner=" << runner
                    << " oracle_runner_score=" << expected[runner]
                    << " oracle_relative_runner_gap=" << (maximum - expected[runner]) / maximum
                    << " oracle_at_candidate_winner=" << expected[actual_winner]
                    << " candidate_at_oracle_winner=" << actual[expected_winner]
                    << " candidate_at_candidate_winner=" << actual[actual_winner]
                    << " normalized_max_error=" << normalized_error
                    << " candidate_winner_error=" << actual[actual_winner] - expected[actual_winner]
                    << " oracle_winner_error=" << actual[expected_winner] - maximum;
                throw datapump::Error(diagnostic.str());
            }
            if (!(actual[expected_winner] > maximum * .98 && actual[expected_winner] < maximum * 1.02))
                throw datapump::Error(label + " stratum=" + std::to_string(stratum) +
                    " peak ratio=" + std::to_string(actual[expected_winner] / maximum) +
                    " crossed declared 0.98/1.02 primitive thresholds");
        }
    };
    std::vector<double> first_active;
    // prepare() binds immutable samples to an owner. A/zero/A creates new owners
    // of the SAME backend; replay below exercises the existing owner separately.
    const std::array<std::span<const Complex>, 4> inputs{
        fixture.active, fixture.zero, fixture.active, fixture.noise};
    const std::array<std::span<const double>, 4> references{
        fixture.active_scores, fixture.zero_scores, fixture.active_scores, fixture.noise_scores};
    for (std::size_t stratum = 0; stratum < inputs.size(); ++stratum) {
        const auto expected = references[stratum];
        auto device = backend.make_coarse(MatrixCoarseFixture::fs, .25);
        r::CoarseBulkWork preparation, work;
        device->prepare(fixture.geometry, inputs[stratum], fixture.workspaces,
                        std::span(&fixture.group, 1), preparation, {});
        const auto run = [&] {
            std::vector<double> actual;
            std::size_t next = 0;
            device->lag_tile(0, fixture.workspaces.size(),
                [&](std::size_t first, std::size_t count, std::span<const double> scores) {
                    check(first == next && count && count <= 8 &&
                          scores.size() == count * fixture.frequencies * fixture.arrivals,
                          "matrix coarse skipped, reordered or truncated the public grid");
                    actual.insert(actual.end(), scores.begin(), scores.end());
                    next += count;
                }, work, {});
            check(next == fixture.workspaces.size(), "matrix coarse did not publish every workspace");
            const auto receipt = device->receipt();
            const auto tolerance = matrix_calls(receipt, math) ?
                (math == v::TensorCoarseMath::fp16 ? .002 : .01) : 3e-5;
            validate(actual, expected, stratum, tolerance);
            return actual;
        };
        const auto actual = run();
        if (stratum == 0) {
            first_active = actual;
            const auto repeated = run();
            check(repeated == actual, "matrix coarse repeated immutable input changed its result");
        } else if (stratum == 2) {
            check(actual == first_active, "matrix coarse A/zero/A backend lifecycle changed the repeated input");
        }
        const auto receipt = device->receipt();
        const auto repetitions = stratum == 0 ? 2u : 1u;
        check(receipt.cuda && receipt.executed &&
              work.score_rows == repetitions * expected.size() &&
              work.workspace_frequency_pairs == repetitions * fixture.workspaces.size() * fixture.frequencies &&
              device->completed_work().score_rows == work.score_rows,
              "matrix coarse receipt did not cover the complete requested grid");
        const auto calls = matrix_calls(receipt, math);
        const auto all_calls = receipt.cuda_matrix_fp16_calls + receipt.cuda_matrix_int8_calls;
        check(receipt.cuda_matrix_lt_hmma_calls <= receipt.cuda_matrix_fp16_calls &&
              receipt.cuda_matrix_lt_imma_calls <= receipt.cuda_matrix_int8_calls &&
              receipt.cuda_matrix_lt_hmma_calls + receipt.cuda_matrix_lt_imma_calls +
                  receipt.cuda_matrix_unwitnessed_calls == all_calls,
              "matrix coarse receipt did not attribute all completed GEMMs to their implementation");
        if (expect_matrix) {
            const auto chunks = (fixture.kernel + 511) / 512;
            check(calls > 0 && all_calls == calls &&
                  calls == receipt.cuda_matrix_inner_chunks &&
                  receipt.cuda_matrix_output_values == repetitions * fixture.taps * expected.size() &&
                  receipt.cuda_matrix_inner_chunks >= repetitions * chunks &&
                  receipt.cuda_matrix_maximum_rows == std::min<std::size_t>(32, fixture.workspaces.size()) *
                      std::min<std::size_t>(64, fixture.arrivals) * fixture.taps &&
                  receipt.cuda_matrix_maximum_columns == fixture.frequencies &&
                  receipt.cuda_matrix_maximum_inner == fixture.kernel &&
                  receipt.cuda_matrix_padded_real_macs > 0 && receipt.cuda_matrix_packed_values > 0 &&
                  receipt.cuda_matrix_nonfinite_values == 0 && receipt.cuda_matrix_saturated_values == 0 &&
                  std::all_of(receipt.matrix_fft_fallback_counts.begin(), receipt.matrix_fft_fallback_counts.end(),
                              [](auto value) { return value == 0; }),
                  "matrix coarse did not execute the requested bounded low-precision products");
            const auto padded = [](std::size_t size) { return (size + 15) / 16 * 16; };
            const auto packed_right_values = 4 * padded(fixture.frequencies) *
                padded(std::min<std::size_t>(512, fixture.kernel)) * chunks;
            check(receipt.cuda_matrix_prepared_b_groups == 1 &&
                  receipt.cuda_matrix_prepared_b_values == packed_right_values &&
                  receipt.cuda_matrix_prepared_b_bytes >= packed_right_values *
                      (math == v::TensorCoarseMath::fp16 ? 2 : 1) &&
                  receipt.cuda_matrix_prepared_b_uses * chunks == calls &&
                  receipt.cuda_matrix_prepared_b_uses >= repetitions &&
                  receipt.cuda_matrix_packed_values > packed_right_values,
                  "matrix coarse did not reuse the prepared right operand across complete rectangles");
        } else {
            check(all_calls == 0 && !receipt.cuda_matrix_prepared_b_groups && !receipt.cuda_matrix_prepared_b_uses &&
                  receipt.matrix_fft_fallback_counts[matrix_fallback_index("unfavorable-shape")] > 0 &&
                  work.inverse_ffts > 0,
                  "matrix coarse silently dropped the full FFT fallback for an unfavorable shape");
        }
        std::stop_source stop;
        stop.request_stop();
        bool rejected = false, published = false;
        const auto prior_scores = work.score_rows;
        try {
            device->lag_tile(0, fixture.workspaces.size(),
                            [&](auto, auto, auto) { published = true; }, work, stop.get_token());
        } catch (const datapump::Error&) { rejected = true; }
        check(rejected && !published && work.score_rows == prior_scores,
              "cancelled matrix coarse work published scores or counted unfinished work");
    }
    check(backend.receipt().reserved_bytes == 0,
          "matrix coarse owner destruction retained a charged workspace");
}

void cuda_matrix_grouped(r::ReceiverBulkBackend& backend, v::TensorCoarseMath math, bool tight) {
    // A shared endpoint belongs to both groups, but only one score is
    // published for that public frequency. The small last group uses FFT.
    // The 4*113-bin case is below the old dense 16 MiB gate but its four
    // padded FP16 operands exceed it. Complete FFT service still fits.
    const MatrixCoarseFixture fixture(tight ? 4097 : 31, tight ? 449 : 17, 16, 1, 1, !tight);
    const std::size_t groups_count = tight ? 4 : 2;
    std::array<std::vector<Complex>, 4> phase, convolution;
    std::array<r::CoarseZoomGroup, 4> groups;
    for (std::size_t i = 0; i < groups_count; ++i) {
        const auto first = tight ? 112 * i : 12 * i;
        const auto count = tight ? 113 : (i ? 5 : 13);
        groups[i] = fixture.zoom_group(first, count, phase[i], convolution[i]);
    }
    auto device = backend.make_coarse(MatrixCoarseFixture::fs, .25);
    r::CoarseBulkWork preparation, work;
    device->prepare(fixture.geometry, fixture.active, fixture.workspaces,
                    std::span(groups).first(groups_count), preparation, {});
    const double peak = tight ? 1. : std::max(1., *std::max_element(fixture.active_scores.begin(), fixture.active_scores.end()));
    std::size_t seen = 0;
    device->lag_tile(0, fixture.workspaces.size(),
        [&](std::size_t first, std::size_t count, std::span<const double> scores) {
            check(first == seen && count == 1 && scores.size() == fixture.frequencies * fixture.arrivals,
                  "grouped CUDA matrix path did not publish one complete public grid");
            for (std::size_t f = 0; f < fixture.frequencies; ++f) {
                if (tight && f != 0 && f != fixture.frequencies / 2 && f != fixture.frequencies - 1) continue;
                for (std::size_t a = 0; a < fixture.arrivals; ++a) {
                    const auto i = f * fixture.arrivals + a;
                    const auto expected = tight ? fixture.oracle_point(fixture.active, first, f, a)
                                                : fixture.active_scores[i];
                    if (tight || f >= 12) {
                        near(scores[i], expected, 3e-5, "grouped CUDA FFT result disagrees with direct scores");
                    } else {
                        const auto tolerance = math == v::TensorCoarseMath::fp16 ? .002 : .01;
                        check(std::abs(scores[i] - expected) <= tolerance * peak,
                              "grouped CUDA matrix result disagrees with direct scores");
                    }
                }
            }
            seen += count;
        }, work, {});
    const auto receipt = device->receipt();
    check(seen == 1 && receipt.cuda && receipt.executed && work.inverse_ffts &&
          work.score_rows == fixture.frequencies * fixture.arrivals,
          "grouped CUDA matrix or FFT fallback lost completed work");
    if (tight) {
        check(!receipt.cuda_matrix_prepared_b_groups && !matrix_calls(receipt, math) &&
              receipt.matrix_fft_fallback_counts[matrix_fallback_index("workspace-limit")] == groups_count,
              "matrix pre-admission did not refuse padded right operands over the configured cap");
    } else {
        check(receipt.cuda_matrix_prepared_b_groups == 1 && receipt.cuda_matrix_prepared_b_uses == 1 &&
              matrix_calls(receipt, math) == 1 &&
              receipt.matrix_fft_fallback_counts[matrix_fallback_index("unfavorable-shape")] == 1,
              "mixed CUDA rectangle did not execute both prepared matrix and FFT groups");
    }
}

void cuda_matrix_coarse(std::optional<v::TensorCoarseMath> selected = {}) {
    const MatrixCoarseFixture narrow(31, 17, 19, 1), wide(769, 129, 19, 2), fallback(769, 257, 19, 2);
    // Cross both B=32 workspace and A=64 arrival tiles, including their tails;
    // the full tile contains 4096 complex rows. Publication stays <=8 workspaces.
    const MatrixCoarseFixture batch_boundary(31, 17, 65, 2, 33);
    for (const auto math : {v::TensorCoarseMath::fp16, v::TensorCoarseMath::int8}) {
        if (selected && math != *selected) continue;
        auto backend = v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, 128 * mib,
                                                       v::TensorPrecision::automatic, math});
        matrix_coarse(*backend, math, narrow, true);
        matrix_coarse(*backend, math, wide, true);
        matrix_coarse(*backend, math, batch_boundary, true);
        matrix_coarse(*backend, math, fallback, false);
        cuda_matrix_grouped(*backend, math, false);
        if (math == v::TensorCoarseMath::fp16) cuda_matrix_grouped(*backend, math, true);
        const auto receipt = backend->receipt();
        std::cout << "Matrix " << matrix_math_name(math) << " actual GEMMs " << matrix_calls(receipt, math)
                  << "; cuBLASLt declared HMMA/IMMA " << receipt.cuda_matrix_lt_hmma_calls << '/'
                  << receipt.cuda_matrix_lt_imma_calls << "; legacy unwitnessed " << receipt.cuda_matrix_unwitnessed_calls
                  << "; capability fallbacks "
                  << receipt.matrix_fft_fallback_counts[matrix_fallback_index("device-capability")] << '\n';
    }
    auto bounded = v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, 128 * mib,
        v::TensorPrecision::automatic, selected.value_or(v::TensorCoarseMath::int8)});
    auto device = bounded->make_coarse(MatrixCoarseFixture::fs, .25);
    r::CoarseBulkWork work;
    const std::array<double, 1> filter{1.};
    bool rejected = false;
    try {
        // Reject the full reservation before allocating/processing a huge FFT.
        // Reuse the established arena size: a new tiny CUDA arena could reject
        // unrelated allocator caching before this operation reaches preflight.
        (void)device->prepare_filter(filter, std::size_t{1} << 24, work, {});
    } catch (const v::ReceiverWorkspaceError&) { rejected = true; }
    device.reset();
    check(rejected && work.forward_ffts == 0 && work.score_rows == 0 && bounded->receipt().reserved_bytes == 0,
          "matrix-enabled backend bypassed FFT reservation preflight or retained a failed owner");

    auto invalid = narrow.active;
    invalid[0] = {std::numeric_limits<double>::quiet_NaN(), 0.};
    auto invalid_owner = bounded->make_coarse(MatrixCoarseFixture::fs, .25);
    r::CoarseBulkWork invalid_work;
    bool invalid_rejected = false, published = false;
    try {
        invalid_owner->prepare(narrow.geometry, invalid, narrow.workspaces, std::span(&narrow.group, 1), invalid_work, {});
        invalid_owner->lag_tile(0, narrow.workspaces.size(),
            [&](auto, auto, auto) { published = true; }, invalid_work, {});
    } catch (const datapump::Error&) { invalid_rejected = true; }
    check(invalid_rejected && !published && invalid_work.score_rows == 0 &&
          invalid_owner->receipt().cuda_matrix_nonfinite_values > 0,
          "nonfinite matrix inputs were sanitized into publishable scores instead of rejected with diagnostics");
    invalid_owner.reset();
    check(bounded->receipt().reserved_bytes == 0, "failed nonfinite matrix owner retained workspace");
}

void matrix_policy_fallback(bool cuda) {
    const MatrixCoarseFixture fixture(31, 17, 19, 1);
    const auto& expected = fixture.active_scores;
    for (const auto math : {v::TensorCoarseMath::fp16, v::TensorCoarseMath::int8}) {
        auto backend = v::make_tensor_receiver_backend({cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu,
            0, 128 * mib, cuda ? v::TensorPrecision::fp64 : v::TensorPrecision::automatic, math});
        auto device = backend->make_coarse(MatrixCoarseFixture::fs, .25);
        r::CoarseBulkWork preparation, work;
        device->prepare(fixture.geometry, fixture.active, fixture.workspaces,
                        std::span(&fixture.group, 1), preparation, {});
        std::size_t seen = 0;
        device->lag_tile(0, fixture.workspaces.size(),
            [&](std::size_t first, std::size_t count, std::span<const double> scores) {
                check(first == seen && scores.size() == count * fixture.frequencies * fixture.arrivals,
                      "matrix policy fallback lost public grid coverage");
                const auto offset = first * fixture.frequencies * fixture.arrivals;
                for (std::size_t i = 0; i < scores.size(); ++i)
                    near(scores[i], expected[offset + i], cuda ? 2e-12 : 3e-5,
                         "matrix policy fallback changed the FFT oracle scores");
                seen += count;
            }, work, {});
        const auto receipt = device->receipt();
        check(seen == fixture.workspaces.size() && receipt.cuda == cuda && receipt.executed &&
              receipt.cuda_matrix_fp16_calls == 0 && receipt.cuda_matrix_int8_calls == 0 &&
              receipt.matrix_fft_fallback_counts[matrix_fallback_index(cuda ? "fp64-stage" : "cpu-device")] > 0 &&
              work.inverse_ffts > 0,
              "matrix request did not report its actual policy FFT fallback");
    }
}

void tensor_policy_receipts(bool cuda) {
    constexpr std::array policies{v::TensorCoarseMath::fft, v::TensorCoarseMath::fp16, v::TensorCoarseMath::int8};
    std::array<std::shared_ptr<r::ReceiverBulkBackend>, policies.size()> backends;
    std::array<std::unique_ptr<r::CoarseBulk>, policies.size()> coarse;
    std::array<std::unique_ptr<r::FineBulk>, policies.size()> fine;
    // Keep all peers alive concurrently. CUDA peers share their device arena;
    // a stage must report its owning backend's policy, not the arena creator's.
    for (std::size_t i = 0; i < policies.size(); ++i) {
        backends[i] = v::make_tensor_receiver_backend({cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu,
            0, 128 * mib, v::TensorPrecision::automatic, policies[i]});
        coarse[i] = backends[i]->make_coarse(80, .25);
        fine[i] = backends[i]->make_fine(80, .25);
    }
    const auto check_labels = [&] {
        for (std::size_t i = 0; i < policies.size(); ++i) {
            const auto expected = matrix_math_name(policies[i]);
            const auto fine_receipt = fine[i]->receipt();
            check(backends[i]->receipt().requested_coarse_math == expected &&
                  coarse[i]->receipt().requested_coarse_math == expected &&
                  fine_receipt.requested_coarse_math == expected,
                  "backend/coarse/fine receipts lost their owning backend's requested coarse policy");
            check(fine_receipt.precision == "fp32_bulk_fp64_coordinates_and_reductions" &&
                  !fine_receipt.cuda_matrix_fp16_calls && !fine_receipt.cuda_matrix_int8_calls,
                  "coarse policy metadata changed or misreported fine-stage arithmetic");
        }
    };
    check_labels();
    const std::array<std::size_t, 1> bins{0};
    const std::array<Complex, 1> observation{};
    const r::FineBulkBlock block{4, 1, bins, observation, .8};
    for (auto& owner : fine) {
        std::array<r::FineBulkStatistics, 1> result{};
        owner->statistics(block, 1, [](std::size_t, std::size_t, std::size_t, const r::BulkMappedSink&) {}, result, {});
        check(owner->completed_work().statistics_blocks == 1 && !result[0].nonzero_basis &&
              std::all_of(result[0].correlations.begin(), result[0].correlations.end(), [](auto value) { return value == Complex{}; }) &&
              std::all_of(result[0].gram.begin(), result[0].gram.end(), [](auto value) { return value == Complex{}; }),
              "policy receipt fixture did not execute a fresh zero fine block");
        check_labels();
    }
    for (auto& owner : fine) owner.reset();
    for (auto& owner : coarse) owner.reset();
    for (const auto& backend : backends)
        check(backend->receipt().reserved_bytes == 0, "policy receipt peers retained expired stage workspace");
}

std::vector<double> matrix_benchmark_scores(r::CoarseBulk& owner, const MatrixCoarseFixture& fixture,
                                           r::CoarseBulkWork& work) {
    std::vector<double> result;
    result.reserve(fixture.workspaces.size() * fixture.frequencies * fixture.arrivals);
    std::size_t next = 0;
    owner.lag_tile(0, fixture.workspaces.size(),
        [&](std::size_t first, std::size_t count, std::span<const double> scores) {
            check(first == next && count && count <= 8 &&
                  scores.size() == count * fixture.frequencies * fixture.arrivals,
                  "matrix benchmark lost ordered full-grid publication");
            result.insert(result.end(), scores.begin(), scores.end());
            next += count;
        }, work, {});
    check(next == fixture.workspaces.size() && work.score_rows == result.size() &&
          work.workspace_frequency_pairs == fixture.workspaces.size() * fixture.frequencies,
          "matrix benchmark omitted required coarse work");
    return result;
}

double matrix_benchmark_compare(std::span<const double> actual, std::span<const double> expected,
                                v::TensorCoarseMath math) {
    check(actual.size() == expected.size(), "matrix benchmark changed its reference score extent");
    const auto winner = std::max_element(expected.begin(), expected.end()) - expected.begin();
    const auto actual_winner = std::max_element(actual.begin(), actual.end()) - actual.begin();
    const auto maximum = expected[winner];
    double error = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        check(std::isfinite(actual[i]) && actual[i] >= -1e-12 * std::max(1., maximum),
              "matrix benchmark produced nonfinite or negative scores");
        error = std::max(error, std::abs(actual[i] - expected[i]));
    }
    const auto normalized_error = error / std::max(1., maximum);
    const auto tolerance = math == v::TensorCoarseMath::fft ? 3e-5 :
        (math == v::TensorCoarseMath::fp16 ? .002 : .01);
    if (normalized_error > tolerance || actual_winner != winner ||
        !(actual[winner] > .98 * maximum && actual[winner] < 1.02 * maximum))
        throw datapump::Error(std::string("matrix benchmark ") + matrix_math_name(math) +
            " normalized error=" + std::to_string(normalized_error) + " budget=" + std::to_string(tolerance) +
            " winner=" + std::to_string(actual_winner) + " reference=" + std::to_string(winner) +
            " peak ratio=" + std::to_string(actual[winner] / maximum));
    return normalized_error;
}

void matrix_benchmark_work(const r::BulkReceipt& receipt, const MatrixCoarseFixture& fixture,
                           v::TensorCoarseMath math, unsigned repetitions) {
    const auto diagnostic = [&] {
        std::ostringstream out;
        out << " math=" << matrix_math_name(math) << " requested_M=4096 requested_N=" << fixture.frequencies
            << " requested_K=" << fixture.kernel << " actual_M=" << receipt.cuda_matrix_maximum_rows
            << " actual_N=" << receipt.cuda_matrix_maximum_columns << " actual_K=" << receipt.cuda_matrix_maximum_inner
            << " calls=" << matrix_calls(receipt, math) << " chunks=" << receipt.cuda_matrix_inner_chunks
            << " output_values=" << receipt.cuda_matrix_output_values << " reserved=" << receipt.reserved_bytes
            << " peak_reserved=" << receipt.peak_reserved_bytes << " allocator_peak_reserved="
            << receipt.allocator_peak_reserved_bytes << " workspace_limit=" << receipt.workspace_limit_bytes
            << " fallbacks={";
        for (std::size_t i = 0; i < receipt.matrix_fft_fallback_counts.size(); ++i)
            out << (i ? "," : "") << r::bulk_matrix_fallback_reasons[i] << ':' << receipt.matrix_fft_fallback_counts[i];
        out << '}';
        return out.str();
    };
    if (!(receipt.cuda && receipt.executed && receipt.peak_reserved_bytes <= receipt.workspace_limit_bytes &&
          receipt.allocator_peak_reserved_bytes > 0 && receipt.allocator_peak_reserved_bytes <= receipt.workspace_limit_bytes))
        throw datapump::Error("matrix benchmark did not execute within the requested CUDA workspace" + diagnostic());
    if (math == v::TensorCoarseMath::fft) {
        check(receipt.cuda_matrix_fp16_calls == 0 && receipt.cuda_matrix_int8_calls == 0 && receipt.fft_calls > 0 &&
              !receipt.cuda_matrix_prepared_b_groups && !receipt.cuda_matrix_prepared_b_uses &&
              !receipt.cuda_matrix_lt_hmma_calls && !receipt.cuda_matrix_lt_imma_calls &&
              !receipt.cuda_matrix_unwitnessed_calls,
              "matrix benchmark FFT reference changed its arithmetic");
    } else {
        const auto calls = matrix_calls(receipt, math);
        const auto all_calls = receipt.cuda_matrix_fp16_calls + receipt.cuda_matrix_int8_calls;
        const bool qualified = calls > 0 && all_calls == calls &&
              calls == receipt.cuda_matrix_inner_chunks &&
              receipt.cuda_matrix_lt_hmma_calls <= receipt.cuda_matrix_fp16_calls &&
              receipt.cuda_matrix_lt_imma_calls <= receipt.cuda_matrix_int8_calls &&
              receipt.cuda_matrix_lt_hmma_calls + receipt.cuda_matrix_lt_imma_calls +
                  receipt.cuda_matrix_unwitnessed_calls == calls &&
              receipt.cuda_matrix_maximum_rows == 4096 &&
              receipt.cuda_matrix_maximum_columns == fixture.frequencies &&
              receipt.cuda_matrix_maximum_inner == fixture.kernel &&
              receipt.cuda_matrix_prepared_b_groups == 1 && receipt.cuda_matrix_prepared_b_values > 0 &&
              receipt.cuda_matrix_prepared_b_uses * ((fixture.kernel + 511) / 512) == calls &&
              receipt.cuda_matrix_prepared_b_uses >= repetitions &&
              receipt.cuda_matrix_output_values == repetitions * fixture.taps * fixture.workspaces.size() *
                  fixture.frequencies * fixture.arrivals &&
              !receipt.cuda_matrix_nonfinite_values && !receipt.cuda_matrix_saturated_values &&
              std::all_of(receipt.matrix_fft_fallback_counts.begin(), receipt.matrix_fft_fallback_counts.end(),
                          [](auto count) { return count == 0; });
        if (!qualified)
            throw datapump::Error("matrix benchmark requested arithmetic/4096-row batch was unavailable or fell back" + diagnostic());
    }
}

struct MatrixBenchmarkMeasurement {
    double owner_seconds = 0, prepare_seconds = 0, first_lag_seconds = 0;
    double fresh_owner_seconds = 0, resident_lag_seconds = 0, normalized_error = 0;
    r::BulkReceipt receipt;
};

MatrixBenchmarkMeasurement matrix_benchmark_measure(r::ReceiverBulkBackend& backend,
    const MatrixCoarseFixture& fixture, std::span<const double> reference, v::TensorCoarseMath math) {
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    auto owner = backend.make_coarse(MatrixCoarseFixture::fs, .25);
    const auto created = Clock::now();
    r::CoarseBulkWork preparation, first_work, resident_work;
    owner->prepare(fixture.geometry, fixture.active, fixture.workspaces, std::span(&fixture.group, 1), preparation, {});
    const auto prepared = Clock::now();
    const auto first_scores = matrix_benchmark_scores(*owner, fixture, first_work);
    const auto first_done = Clock::now();
    // Validation is outside both timed windows. Resident replay retains the
    // same immutable observation/bank; it does not model a future physical slot.
    const auto first_error = matrix_benchmark_compare(first_scores, reference, math);
    const auto resident_begin = Clock::now();
    const auto resident_scores = matrix_benchmark_scores(*owner, fixture, resident_work);
    const auto resident_done = Clock::now();
    const auto resident_error = matrix_benchmark_compare(resident_scores, reference, math);
    const auto receipt = owner->receipt();
    matrix_benchmark_work(receipt, fixture, math, 2);
    const auto seconds = [](auto finish, auto start) { return std::chrono::duration<double>(finish - start).count(); };
    MatrixBenchmarkMeasurement result{seconds(created, begin), seconds(prepared, created),
        seconds(first_done, prepared), seconds(first_done, begin), seconds(resident_done, resident_begin),
        std::max(first_error, resident_error), receipt};
    owner.reset();
    check(backend.receipt().reserved_bytes == 0, "matrix benchmark retained an expired owner reservation");
    return result;
}

std::uint64_t peak_benchmark_input_hash(const MatrixCoarseFixture& fixture);

std::uint64_t matrix_benchmark_input_hash(const MatrixCoarseFixture& fixture) {
    // Extend the existing observation/template fingerprint with every actual
    // lag-plan input. Encode words little-endian instead of hashing padding,
    // pointers or platform-native object representations. This is a stable
    // comparison fingerprint, not a cryptographic digest.
    auto hash = peak_benchmark_input_hash(fixture);
    const auto word = [&](std::uint64_t value) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (8 * byte)) & 255;
            hash *= UINT64_C(1099511628211);
        }
    };
    const auto real = [&](double value) { word(std::bit_cast<std::uint64_t>(value)); };
    const auto samples = [&](std::span<const Complex> values) {
        word(values.size());
        for (const auto value : values) {
            real(value.real());
            real(value.imag());
        }
    };
    word(1); // Fingerprint encoding version.
    for (const auto value : {fixture.geometry.taps, fixture.geometry.kernel_rows,
                            fixture.geometry.correlation_fft_rows, fixture.geometry.lag_count,
                            fixture.geometry.frequency_count}) word(value);
    real(fixture.geometry.projected_sample_rate_hz);
    real(fixture.geometry.variance);
    word(fixture.group.first_frequency);
    word(fixture.group.frequencies);
    samples(fixture.group.input_phase);
    samples(fixture.group.convolution);
    samples(fixture.active);
    word(fixture.workspaces.size());
    for (const auto& workspace : fixture.workspaces) {
        for (const auto column : workspace.columns) samples(column);
        real(workspace.inverse_a);
        real(workspace.inverse_d);
        real(workspace.inverse_b.real());
        real(workspace.inverse_b.imag());
    }
    return hash;
}

void emit_matrix_benchmark(const MatrixBenchmarkMeasurement& result, const MatrixCoarseFixture& fixture,
                           std::uint64_t input_hash,
                           v::TensorCoarseMath math, v::TensorCoarseMath comparison_math, unsigned look,
                           bool candidate_first, unsigned benchmark_case) {
    const auto& receipt = result.receipt;
    // Machine-readable evidence for this production CoarseBulk API only.
    // The preceding identity record binds compiled source/runtime. The
    // invoking harness separately binds the executable bytes and total cost.
    std::cout << std::setprecision(17)
        << "{\"schema\":\"native_coarse_bulk_benchmark_v1\",\"scope\":\"post_channelization_CoarseBulk\""
        << ",\"initialized_device\":true,\"fresh_owner_excludes_teardown\":true"
        << ",\"resident_reuses_immutable_input\":true,\"independent_dft_checks\":19,\"math\":\"" << matrix_math_name(math)
        << "\",\"comparison_math\":\"" << matrix_math_name(comparison_math)
        << "\",\"candidate_first\":" << (candidate_first ? "true" : "false")
        << ",\"look\":" << look << ",\"benchmark_case\":" << benchmark_case << ",\"kernel\":" << fixture.kernel
        << ",\"input_fnv1a64\":\"" << input_hash << "\""
        << ",\"input_hash_encoding\":\"fnv1a64_le_geometry_observation_templates_and_lag_plan_v1\""
        << ",\"frequencies\":" << fixture.frequencies << ",\"arrivals\":" << fixture.arrivals
        << ",\"workspaces\":" << fixture.workspaces.size() << ",\"taps\":" << fixture.taps
        << ",\"correlation_fft_rows\":" << fixture.geometry.correlation_fft_rows
        << ",\"owner_seconds\":" << result.owner_seconds << ",\"prepare_seconds\":" << result.prepare_seconds
        << ",\"first_lag_seconds\":" << result.first_lag_seconds
        << ",\"fresh_owner_seconds\":" << result.fresh_owner_seconds
        << ",\"resident_lag_seconds\":" << result.resident_lag_seconds
        << ",\"normalized_score_error\":" << result.normalized_error
        << ",\"workspace_limit_bytes\":" << receipt.workspace_limit_bytes
        << ",\"peak_reserved_bytes\":" << receipt.peak_reserved_bytes
        << ",\"allocator_peak_reserved_bytes\":" << receipt.allocator_peak_reserved_bytes
        << ",\"synchronizations\":" << receipt.synchronizations
        << ",\"upload_bytes\":" << receipt.upload_bytes << ",\"download_bytes\":" << receipt.download_bytes
        << ",\"score_rows\":" << receipt.score_rows << ",\"fft_calls\":" << receipt.fft_calls
        << ",\"matrix_calls\":" << matrix_calls(receipt, math)
        << ",\"matrix_fp16_calls\":" << receipt.cuda_matrix_fp16_calls
        << ",\"matrix_int8_calls\":" << receipt.cuda_matrix_int8_calls
        << ",\"matrix_lt_hmma_calls\":" << receipt.cuda_matrix_lt_hmma_calls
        << ",\"matrix_lt_imma_calls\":" << receipt.cuda_matrix_lt_imma_calls
        << ",\"matrix_unwitnessed_calls\":" << receipt.cuda_matrix_unwitnessed_calls
        << ",\"matrix_nonfinite_values\":" << receipt.cuda_matrix_nonfinite_values
        << ",\"matrix_saturated_values\":" << receipt.cuda_matrix_saturated_values
        << ",\"matrix_output_values\":" << receipt.cuda_matrix_output_values
        << ",\"matrix_inner_chunks\":" << receipt.cuda_matrix_inner_chunks
        << ",\"matrix_padded_real_macs\":" << receipt.cuda_matrix_padded_real_macs
        << ",\"matrix_packed_values\":" << receipt.cuda_matrix_packed_values
        << ",\"matrix_prepared_b_groups\":" << receipt.cuda_matrix_prepared_b_groups
        << ",\"matrix_prepared_b_bytes\":" << receipt.cuda_matrix_prepared_b_bytes
        << ",\"matrix_prepared_b_values\":" << receipt.cuda_matrix_prepared_b_values
        << ",\"matrix_prepared_b_uses\":" << receipt.cuda_matrix_prepared_b_uses
        << ",\"matrix_maximum_rows\":" << receipt.cuda_matrix_maximum_rows
        << ",\"matrix_maximum_columns\":" << receipt.cuda_matrix_maximum_columns
        << ",\"matrix_maximum_inner\":" << receipt.cuda_matrix_maximum_inner
        << ",\"cuda_runtime_version\":" << receipt.cuda_runtime_version
        << ",\"cuda_driver_version\":" << receipt.cuda_driver_version
        << ",\"cuda_blas_version\":" << receipt.cuda_blas_version
        << ",\"cuda_compute_major\":" << receipt.cuda_compute_major
        << ",\"cuda_compute_minor\":" << receipt.cuda_compute_minor
        << ",\"matrix_fft_fallback_counts\":[";
    for (std::size_t i = 0; i < receipt.matrix_fft_fallback_counts.size(); ++i)
        std::cout << (i ? "," : "") << receipt.matrix_fft_fallback_counts[i];
    std::cout << "]}\n";
}

void cuda_matrix_benchmark(std::optional<v::TensorCoarseMath> selected,
                           std::optional<unsigned> selected_case, std::size_t workspace_mib) {
    const auto workspace = workspace_mib * mib;
    const auto backend = [&](v::TensorCoarseMath math) {
        return v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, workspace,
                                                v::TensorPrecision::automatic, math});
    };
    // Two declared native bulk geometries, optionally scoped to one, with
    // exactly three alternating-order matched pairs per selected geometry.
    // No timing-based enlargement/repetition. Inputs and the first 16 DFT points
    // are fixed before observing outputs; planted/winner/runner-up checks follow
    // a predeclared selection rule. All scores are paired with the FFT reference.
    constexpr std::array shapes{std::array<std::size_t, 4>{769, 129, 65, 33},
                               std::array<std::size_t, 4>{2048, 65, 64, 32}};
    bool emitted_identity = false;
    for (unsigned benchmark_case = 0; benchmark_case < shapes.size(); ++benchmark_case) {
        if (selected_case && benchmark_case != *selected_case) continue;
        const auto& shape = shapes[benchmark_case];
        const MatrixCoarseFixture fixture(shape[0], shape[1], shape[2], 2, shape[3], false);
        const auto input_hash = matrix_benchmark_input_hash(fixture);
        auto fft = backend(v::TensorCoarseMath::fft);
        if (!emitted_identity) {
            emit_component_identity(fft->receipt());
            emitted_identity = true;
        }
        auto reference_owner = fft->make_coarse(MatrixCoarseFixture::fs, .25);
        r::CoarseBulkWork preparation, reference_work;
        reference_owner->prepare(fixture.geometry, fixture.active, fixture.workspaces,
                                 std::span(&fixture.group, 1), preparation, {});
        const auto reference = matrix_benchmark_scores(*reference_owner, fixture, reference_work);
        reference_owner.reset();
        std::vector<std::size_t> points;
        for (std::size_t i = 0; i < 16; ++i)
            points.push_back(((7 * i % fixture.workspaces.size()) * fixture.frequencies +
                              11 * i % fixture.frequencies) * fixture.arrivals + 13 * i % fixture.arrivals);
        points.push_back((fixture.frequencies / 2 + 3) * fixture.arrivals + 7);
        const auto winner = std::max_element(reference.begin(), reference.end()) - reference.begin();
        points.push_back(winner);
        std::size_t runner = winner == 0 ? 1 : 0;
        for (std::size_t i = 0; i < reference.size(); ++i)
            if (i != static_cast<std::size_t>(winner) && reference[i] > reference[runner]) runner = i;
        points.push_back(runner);
        check(reference[winner] > reference[runner], "matrix benchmark reference lacks a unique winner");
        for (const auto point : points) {
            const auto arrival = point % fixture.arrivals;
            const auto frequency = point / fixture.arrivals % fixture.frequencies;
            const auto w = point / fixture.arrivals / fixture.frequencies;
            near(reference[point], fixture.oracle_point(fixture.active, w, frequency, arrival), 3e-5,
                 "matrix benchmark FFT reference failed the declared independent DFT samples");
        }
        for (const auto math : {v::TensorCoarseMath::fp16, v::TensorCoarseMath::int8}) {
            if (selected && math != *selected) continue;
            auto candidate = backend(math);
            auto preflight = candidate->make_coarse(MatrixCoarseFixture::fs, .25);
            r::CoarseBulkWork candidate_preparation, candidate_work;
            preflight->prepare(fixture.geometry, fixture.active, fixture.workspaces,
                               std::span(&fixture.group, 1), candidate_preparation, {});
            const auto preliminary = matrix_benchmark_scores(*preflight, fixture, candidate_work);
            (void)matrix_benchmark_compare(preliminary, reference, math);
            matrix_benchmark_work(preflight->receipt(), fixture, math, 1);
            preflight.reset();
            for (unsigned look = 1; look <= 3; ++look) {
                MatrixBenchmarkMeasurement baseline_result, candidate_result;
                if (look % 2) {
                    baseline_result = matrix_benchmark_measure(*fft, fixture, reference, v::TensorCoarseMath::fft);
                    candidate_result = matrix_benchmark_measure(*candidate, fixture, reference, math);
                } else {
                    candidate_result = matrix_benchmark_measure(*candidate, fixture, reference, math);
                    baseline_result = matrix_benchmark_measure(*fft, fixture, reference, v::TensorCoarseMath::fft);
                }
                emit_matrix_benchmark(baseline_result, fixture, input_hash, v::TensorCoarseMath::fft, math, look, !(look % 2), benchmark_case);
                emit_matrix_benchmark(candidate_result, fixture, input_hash, math, math, look, !(look % 2), benchmark_case);
            }
        }
    }
}

struct PeakBenchmarkResult {
    std::vector<r::CoarseBulkPeaks> peaks;
    std::array<double, 16> oracle_samples{};
    r::CoarseBulkWork preparation, search;
    r::BulkReceipt receipt;
    double owner_seconds = 0, prepare_seconds = 0, search_seconds = 0, fresh_owner_seconds = 0;
};

std::array<std::size_t, 16> peak_benchmark_points(const MatrixCoarseFixture& fixture) {
    std::array<std::size_t, 16> points{};
    points[0] = (fixture.frequencies / 2 + 3) * fixture.arrivals + 7;
    for (std::size_t i = 1; i < points.size(); ++i)
        points[i] = ((7 * i % fixture.workspaces.size()) * fixture.frequencies +
                     11 * i % fixture.frequencies) * fixture.arrivals + 613 * i % fixture.arrivals;
    return points;
}

void compare_peak_frontiers(std::span<const r::CoarseBulkPeaks> actual,
                            std::span<const r::CoarseBulkPeaks> expected) {
    check(actual.size() == expected.size(), "peak benchmark changed the complete frontier extent");
    for (std::size_t row = 0; row < actual.size(); ++row) {
        check(actual[row].count == expected[row].count, "peak benchmark changed a canonical frontier count");
        for (std::size_t j = 0; j < expected[row].count; ++j)
            if (actual[row].lags[j] != expected[row].lags[j] ||
                std::bit_cast<std::uint64_t>(actual[row].scores[j]) !=
                    std::bit_cast<std::uint64_t>(expected[row].scores[j]))
                throw datapump::Error("peak benchmark changed exact frontier row=" + std::to_string(row) +
                                     " entry=" + std::to_string(j));
    }
}

PeakBenchmarkResult peak_benchmark_run(r::ReceiverBulkBackend& backend, const MatrixCoarseFixture& fixture,
                                       std::size_t separation, bool compressed) {
    using Clock = std::chrono::steady_clock;
    PeakBenchmarkResult result;
    const auto sample_points = peak_benchmark_points(fixture);
    const auto begin = Clock::now();
    auto owner = backend.make_coarse(MatrixCoarseFixture::fs, .25);
    const auto created = Clock::now();
    owner->prepare(fixture.geometry, fixture.active, fixture.workspaces, {}, result.preparation, {});
    const auto prepared = Clock::now();
    result.peaks.reserve(fixture.workspaces.size() * fixture.frequencies);
    std::size_t next = 0;
    if (compressed) {
        const auto handled = owner->frequency_peaks(fixture.frequency, separation,
            [&](std::size_t first, std::size_t count, std::span<const r::CoarseBulkPeaks> peaks) {
                check(first == next && count && peaks.size() == count * fixture.frequencies,
                      "peak benchmark compressed publication skipped or reordered workspaces");
                for (const auto& peak : peaks) {
                    check(peak.count > 0 && peak.count <= 2, "peak benchmark published an invalid compressed count");
                    for (std::size_t i = 0; i < peak.count; ++i)
                        check(peak.lags[i] < fixture.arrivals && std::isfinite(peak.scores[i]),
                              "peak benchmark published a malformed compressed frontier");
                }
                result.peaks.insert(result.peaks.end(), peaks.begin(), peaks.end());
                next += count;
            }, result.search, {});
        check(handled, "peak benchmark requested CUDA compression was unavailable");
    } else {
        owner->frequency_tile(fixture.frequency,
            [&](std::size_t first, std::size_t count, std::span<const double> values) {
                check(first == next && count && values.size() == count * fixture.frequencies * fixture.arrivals,
                      "peak benchmark full-score publication skipped or reordered workspaces");
                // Match the production host consumer: check every received
                // value, then run canonical arrival suppression inside timing.
                check(std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); }),
                      "peak benchmark full-score consumer received a nonfinite value");
                for (std::size_t row = 0; row < count * fixture.frequencies; ++row) {
                    const auto scores = values.subspan(row * fixture.arrivals, fixture.arrivals);
                    const auto selected = r::select_coarse_arrivals(scores, separation);
                    r::CoarseBulkPeaks peak;
                    peak.count = selected.count;
                    for (std::size_t i = 0; i < selected.count; ++i) {
                        peak.lags[i] = selected.indices[i];
                        peak.scores[i] = scores[selected.indices[i]];
                    }
                    result.peaks.push_back(peak);
                }
                const auto first_value = first * fixture.frequencies * fixture.arrivals;
                for (std::size_t i = 0; i < sample_points.size(); ++i)
                    if (sample_points[i] >= first_value && sample_points[i] - first_value < values.size())
                        result.oracle_samples[i] = values[sample_points[i] - first_value];
                next += count;
            }, result.search, {});
    }
    const auto finished = Clock::now();
    // All correctness/accounting assertions below are outside the timed span.
    result.receipt = owner->receipt();
    const auto rows = fixture.workspaces.size() * fixture.frequencies;
    const auto values = rows * fixture.arrivals;
    check(next == fixture.workspaces.size() && result.peaks.size() == rows &&
          result.search.workspace_frequency_pairs == rows && result.search.score_rows == values &&
          owner->completed_work().score_rows == values && result.receipt.score_rows == values &&
          result.receipt.cuda && result.receipt.executed &&
          result.receipt.peak_reserved_bytes <= result.receipt.workspace_limit_bytes &&
          result.receipt.allocator_peak_reserved_bytes > 0 &&
          result.receipt.allocator_peak_reserved_bytes <= result.receipt.workspace_limit_bytes,
          "peak benchmark lost complete work or exceeded its device workspace");
    check(result.receipt.cuda_arrival_peak_rows == (compressed ? rows : 0) &&
          result.receipt.cuda_arrival_peak_values == (compressed ? values : 0) &&
          result.receipt.download_bytes == (compressed ? rows * sizeof(r::CoarseBulkPeaks) : values * sizeof(double)),
          "peak benchmark changed exact selector/transfer accounting");
    const auto seconds = [](auto end, auto start) { return std::chrono::duration<double>(end - start).count(); };
    result.owner_seconds = seconds(created, begin);
    result.prepare_seconds = seconds(prepared, created);
    result.search_seconds = seconds(finished, prepared);
    result.fresh_owner_seconds = seconds(finished, begin);
    owner.reset();
    check(backend.receipt().reserved_bytes == 0, "peak benchmark retained an expired owner reservation");
    return result;
}

std::uint64_t peak_benchmark_input_hash(const MatrixCoarseFixture& fixture) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&](std::uint64_t value) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (8 * byte)) & 255;
            hash *= UINT64_C(1099511628211);
        }
    };
    const auto complex = [&](Complex value) {
        add(std::bit_cast<std::uint64_t>(value.real()));
        add(std::bit_cast<std::uint64_t>(value.imag()));
    };
    for (const auto value : {fixture.kernel, fixture.frequencies, fixture.arrivals, fixture.taps, fixture.workspaces.size(),
                              fixture.geometry.correlation_fft_rows}) add(value);
    for (const auto value : fixture.frequency) add(std::bit_cast<std::uint64_t>(value));
    for (const auto value : fixture.active) complex(value);
    for (const auto& column : fixture.columns) for (const auto value : column) complex(value);
    for (const auto& workspace : fixture.workspaces) {
        add(std::bit_cast<std::uint64_t>(workspace.inverse_a));
        add(std::bit_cast<std::uint64_t>(workspace.inverse_d));
        complex(workspace.inverse_b);
    }
    return hash;
}

void emit_peak_benchmark(const PeakBenchmarkResult& result, const MatrixCoarseFixture& fixture,
                         std::uint64_t input_hash, std::size_t separation, unsigned look,
                         bool compressed, bool compressed_first) {
    const auto& receipt = result.receipt;
    std::cout << std::setprecision(17)
        << "{\"schema\":\"native_arrival_peak_benchmark_v1\",\"scope\":\"synthetic_native_bulk_geometry\""
        << ",\"mode\":\"" << (compressed ? "cuda_frontier" : "full_scores_host_nms")
        << "\",\"look\":" << look << ",\"compressed_first\":" << (compressed_first ? "true" : "false")
        << ",\"input_fnv1a64\":\"" << input_hash << "\",\"kernel\":" << fixture.kernel
        << ",\"frequencies\":" << fixture.frequencies << ",\"arrivals\":" << fixture.arrivals
        << ",\"workspaces\":" << fixture.workspaces.size() << ",\"taps\":" << fixture.taps
        << ",\"correlation_fft_rows\":" << fixture.geometry.correlation_fft_rows
        << ",\"separation\":" << separation << ",\"independent_dft_points\":16"
        << ",\"initialized_device\":true,\"fresh_owner_excludes_teardown\":true"
        << ",\"owner_seconds\":" << result.owner_seconds << ",\"prepare_seconds\":" << result.prepare_seconds
        << ",\"search_seconds\":" << result.search_seconds << ",\"fresh_owner_seconds\":" << result.fresh_owner_seconds
        << ",\"score_rows\":" << receipt.score_rows << ",\"forward_ffts\":" << result.search.forward_ffts
        << ",\"inverse_ffts\":" << result.search.inverse_ffts << ",\"fft_row_stages\":" << result.search.fft_row_stages
        << ",\"upload_bytes\":" << receipt.upload_bytes << ",\"download_bytes\":" << receipt.download_bytes
        << ",\"cuda_arrival_peak_rows\":" << receipt.cuda_arrival_peak_rows
        << ",\"cuda_arrival_peak_values\":" << receipt.cuda_arrival_peak_values
        << ",\"synchronizations\":" << receipt.synchronizations
        << ",\"workspace_limit_bytes\":" << receipt.workspace_limit_bytes
        << ",\"peak_reserved_bytes\":" << receipt.peak_reserved_bytes
        << ",\"allocator_peak_reserved_bytes\":" << receipt.allocator_peak_reserved_bytes << "}\n";
}

void cuda_peak_benchmark() {
    constexpr std::size_t separation = 17, workspace = 2048 * mib;
    const MatrixCoarseFixture fixture(8989, 32, 10553, 2, 78, false);
    check(fixture.geometry.correlation_fft_rows == 32768, "peak benchmark changed its declared FFT geometry");
    const auto input_hash = peak_benchmark_input_hash(fixture);
    auto backend = v::make_tensor_receiver_backend({v::TensorDevice::cuda, 0, workspace,
                                                   v::TensorPrecision::automatic, v::TensorCoarseMath::fft});
    emit_component_identity(backend->receipt());
    // Eight scorings total: two preflights, then three AB/BA/AB pairs.
    // This synthetic post-channelization tile matches a public work shape;
    // it is not an actual waveform, complete receiver or backlog-capacity test.
    const auto reference = peak_benchmark_run(*backend, fixture, separation, false);
    const auto points = peak_benchmark_points(fixture);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto arrival = points[i] % fixture.arrivals;
        const auto frequency = points[i] / fixture.arrivals % fixture.frequencies;
        const auto w = points[i] / fixture.arrivals / fixture.frequencies;
        near(reference.oracle_samples[i], fixture.oracle_point(fixture.active, w, frequency, arrival), 3e-5,
             "peak benchmark reference failed a predeclared independent double DFT point");
    }
    const auto preflight = peak_benchmark_run(*backend, fixture, separation, true);
    compare_peak_frontiers(preflight.peaks, reference.peaks);
    check(same_coarse_work(preflight.preparation, reference.preparation) &&
          same_coarse_work(preflight.search, reference.search), "peak compression changed reference preparation/search work");
    for (unsigned look = 1; look <= 3; ++look) {
        PeakBenchmarkResult full, compressed;
        if (look % 2) {
            full = peak_benchmark_run(*backend, fixture, separation, false);
            compressed = peak_benchmark_run(*backend, fixture, separation, true);
        } else {
            compressed = peak_benchmark_run(*backend, fixture, separation, true);
            full = peak_benchmark_run(*backend, fixture, separation, false);
        }
        compare_peak_frontiers(full.peaks, reference.peaks);
        compare_peak_frontiers(compressed.peaks, reference.peaks);
        check(same_coarse_work(full.preparation, reference.preparation) && same_coarse_work(compressed.preparation, reference.preparation) &&
              same_coarse_work(full.search, reference.search) && same_coarse_work(compressed.search, reference.search),
              "peak benchmark paired runs changed full numerical work");
        emit_peak_benchmark(full, fixture, input_hash, separation, look, false, !(look % 2));
        emit_peak_benchmark(compressed, fixture, input_hash, separation, look, true, !(look % 2));
    }
}

void fine_descriptor_cache(r::ReceiverBulkBackend& backend) {
    constexpr std::size_t rows = 19, tile_rows = 8192;
    auto tile = std::make_shared<std::array<Complex, tile_rows>>();
    for (std::size_t i = 0; i < tile_rows; ++i)
        (*tile)[i] = rotation(.013 * i) * (.3 + .1 * std::sin(.17 * i));
    const std::array<std::size_t, 3> bins_a{0, 3, 8}, bins_b{1, 4, 9};
    const std::array<Complex, 3> y_a{{{.4, -.2}, {.1, .3}, {-.3, .2}}},
                                 y_b{{{-.1, .2}, {.4, -.3}, {.2, .1}}}, zero{};
    std::array<double, rows> offsets_a{}, offsets_b{};
    for (std::size_t i = 0; i < rows; ++i) {
        offsets_a[i] = i;
        offsets_b[i] = i + (i % 2 ? .375 : .125);
    }
    const auto cubic = [&](double coordinate) {
        const auto center = static_cast<int>(std::floor(coordinate));
        const auto fraction = coordinate - center;
        Complex value{};
        for (int j = -1; j < 3; ++j) {
            double weight = 1;
            for (int other = -1; other < 3; ++other)
                if (other != j) weight *= (fraction - other) / (j - other);
            value += weight * (*tile)[center + j];
        }
        return value;
    };
    auto device = backend.make_fine(5, .0005);
    const auto run = [&](std::span<const std::size_t> bins, std::span<const Complex> y,
                         std::span<const double> offsets, std::stop_token stop = {}) {
        const r::FineBulkBlock block{offsets.size(), 1, bins, y, .8};
        const r::FineMappingSource source = [&](std::size_t candidate, std::size_t first, std::size_t count,
                                                 const r::BulkMappedSink& sink) {
            check(candidate == 0 && first == 0 && count == offsets.size(),
                  "descriptor-cache fixture changed its public extent");
            const std::array<r::BulkMappedColumn, 1> mappings{{
                {.17, 1.04, .03, -.02, 10, 0, {{tile, *tile}}}
            }};
            sink(offsets, mappings, {});
        };
        std::array<r::FineBulkStatistics, 1> result{};
        device->statistics(block, 1, source, result, stop);
        std::vector<Complex> projected(bins.size());
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            const auto value = cubic(10 + .17 + 1.04 * offsets[i]) * rotation(.03 - .02 * offsets[i]);
            for (std::size_t mode = 0; mode < bins.size(); ++mode)
                projected[mode] += value * rotation(-static_cast<double>(bins[mode] * i) / offsets.size()) /
                                   std::sqrt(static_cast<double>(offsets.size()));
        }
        Complex correlation{};
        double gram = 0;
        for (std::size_t mode = 0; mode < bins.size(); ++mode) {
            correlation += std::conj(projected[mode]) * y[mode];
            gram += std::norm(projected[mode]);
        }
        check(result[0].nonzero_basis && result[0].correlations[0] == Complex{} &&
              result[0].correlations[2] == Complex{} && result[0].gram[0] == Complex{} &&
              result[0].gram[2] == Complex{},
              "descriptor-cache update changed zero-hypothesis state");
        near(result[0].correlations[1], correlation / block.variance, 2e-12,
             "descriptor-cache reused stale bins, offsets or observation");
        near(result[0].gram[1], gram / block.variance, 2e-12,
             "descriptor-cache changed the independent mapped DFT Gram");
        return result[0];
    };
    const auto a = run(bins_a, y_a, offsets_a);
    const auto first = device->receipt();
    const auto bins_copy = bins_a;
    const auto offsets_copy = offsets_a;
    (void)run(bins_copy, y_a, offsets_copy);
    const auto repeated = device->receipt();
    check(repeated.projection_index_cache_uploads == first.projection_index_cache_uploads &&
          repeated.mapping_offset_cache_uploads == first.mapping_offset_cache_uploads &&
          repeated.projection_index_cache_hits > first.projection_index_cache_hits &&
          repeated.mapping_offset_cache_hits > first.mapping_offset_cache_hits,
          "unchanged fine descriptors were uploaded instead of reused");
    (void)run(bins_b, y_b, offsets_b);
    const auto changed = device->receipt();
    check(changed.projection_index_cache_uploads > repeated.projection_index_cache_uploads &&
          changed.mapping_offset_cache_uploads > repeated.mapping_offset_cache_uploads,
          "same-length changed fine descriptors did not invalidate their cache");
    const auto quiet = run(bins_b, zero, offsets_b);
    check(quiet.correlations[1] == Complex{}, "fresh zero observation retained prior fine correlations");
    const auto before_return = device->receipt();
    const auto returned = run(bins_a, y_a, offsets_a);
    const auto after_return = device->receipt();
    near(returned.correlations[1], a.correlations[1], 0., "fine descriptor A/B/A replay changed the observation");
    near(returned.gram[1], a.gram[1], 0., "fine descriptor A/B/A replay changed the basis");
    check(after_return.mapping_offset_cache_uploads == before_return.mapping_offset_cache_uploads &&
          after_return.mapping_offset_cache_hits > before_return.mapping_offset_cache_hits,
          "two-entry offset cache failed its declared A/B/A reuse");
    (void)run(bins_a, y_a, std::span(offsets_a).first(rows - 2));
    const auto resized = device->receipt();
    check(resized.projection_index_cache_uploads > after_return.projection_index_cache_uploads &&
          resized.mapping_offset_cache_uploads > after_return.mapping_offset_cache_uploads,
          "changed block rows reused a descriptor with a different public extent");
    std::stop_source stop;
    stop.request_stop();
    bool rejected = false;
    try { (void)run(bins_a, y_a, offsets_a, stop.get_token()); }
    catch (const datapump::Error&) { rejected = true; }
    check(rejected && device->receipt().statistics_blocks == resized.statistics_blocks,
          "cancelled fine descriptor work received completed statistics credit");
    device.reset();
    check(backend.receipt().reserved_bytes == 0, "fine descriptor cache outlived its charged owner");
}

void tight_descriptor_workspace() {
    // This exact pre-cache admission includes one candidate, shared staging and
    // the minimum 32-tile bank. Optional descriptor retention must not turn an
    // otherwise admissible block into a failure; transient descriptors already
    // fit the existing per-candidate/shared bounds.
    constexpr std::size_t budget = 6'337'568, rows = 64, tile_rows = 8192;
    auto backend = v::make_tensor_receiver_backend({v::TensorDevice::cpu, 0, budget,
                                                   v::TensorPrecision::automatic});
    auto device = backend->make_fine(80, .25);
    auto tile = std::make_shared<std::array<Complex, tile_rows>>();
    for (std::size_t i = 0; i < tile_rows; ++i)
        (*tile)[i] = .2 * rotation(3. * (static_cast<double>(i) - 4.) / rows);
    std::array<double, rows> offsets_a{}, offsets_b{};
    std::iota(offsets_a.begin(), offsets_a.end(), 0.);
    std::iota(offsets_b.begin(), offsets_b.end(), 1.);
    const std::array<std::size_t, 4> bins_a{0, 3, 8, 17}, bins_b{0, 4, 9, 17};
    const std::array<Complex, 4> y{0., .3, 0., 0.};
    struct Case { std::span<const std::size_t> bins; bool shifted; };
    const std::array<Case, 4> cases{{{bins_a, false}, {bins_a, true}, {bins_b, true}, {bins_a, false}}};
    for (const auto& test : cases) {
        const auto& offsets = test.shifted ? offsets_b : offsets_a;
        const r::FineBulkBlock block{rows, 1, test.bins, y, .8};
        std::array<r::FineBulkStatistics, 1> result{};
        device->statistics(block, 1, [&](std::size_t candidate, std::size_t first, std::size_t count,
                                          const r::BulkMappedSink& sink) {
            check(candidate == 0 && first == 0 && count == rows, "tight descriptor fixture changed extent");
            const std::array<r::BulkMappedColumn, 1> mapping{{{0., 1., 0., 0., 4, 0, {{tile, *tile}}}}};
            sink(offsets, mapping, {});
        }, result, {});
        const bool contains_tone = test.bins[1] == 3;
        check(result[0].nonzero_basis, "tight descriptor fallback lost its exact basis witness");
        near(result[0].correlations[1], contains_tone ? .6 * rotation(test.shifted ? -3. / rows : 0.) : Complex{}, 2e-6,
             "tight descriptor fallback reused stale bins or changed its analytic correlation");
        near(result[0].gram[1], contains_tone ? 3.2 : 0., 2e-6,
             "tight descriptor fallback changed its analytic Gram");
        check(device->receipt().peak_reserved_bytes <= budget,
              "tight descriptor fallback exceeded its exact workspace admission");
    }
    device.reset();
    check(backend->receipt().reserved_bytes == 0, "tight descriptor fallback retained its workspace");
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

void gram_mode_boundary(r::ReceiverBulkBackend& backend, bool precise = false) {
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
    auto device = backend.make_fine(precise ? 5 : 497, precise ? .0005 : .25);
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
            const auto direct_route = modes == 1 || (!precise && modes <= 128);
            check(after.precision == (precise ? "fp64" : "fp32_bulk_fp64_coordinates_and_reductions") && output.front().nonzero_basis &&
                  direct_values == (after.cuda && direct_route ? 2 * taps * taps : 0) &&
                  bmm_values == (after.cuda && !direct_route ? 2 * taps * taps : 0),
                  "ordinary Gram selected the wrong device or mode-range algorithm");

            const auto gram = std::span(output.front().gram);
            for (std::size_t i = 0; i < taps; ++i) {
                for (std::size_t j = 0; j < taps; ++j) {
                    const auto entry = i * taps + j;
                    Complex expected{};
                    for (std::size_t k = 0; k < tones.size(); ++k)
                        if (tones[k] < modes) expected += std::conj(amplitudes[i][k]) * amplitudes[j][k];
                    near(gram[taps * taps + entry], expected * (rows / variance), precise ? 2e-12 : 2e-6,
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

void gram_workspace_lifetime(r::ReceiverBulkBackend& backend) {
    if (!backend.receipt().cuda) {
        gram_mode_boundary(backend, true);
        return;
    }
    std::size_t warmed_live_bytes = 0;
    // Four fresh owners exercise nonzero complex two-tap/129-mode C128 Grams.
    // The first owner permits one-time library initialization. Later released
    // owners must not retain additional per-stream tensor workspace.
    for (unsigned owner = 0; owner < 4; ++owner) {
        gram_mode_boundary(backend, true);
        const auto after = backend.receipt();
        check(after.reserved_bytes == 0 && after.allocator_reserved_bytes <= after.workspace_limit_bytes,
              "released Gram owner retained an arena reservation or exceeded the allocator bound");
        if (owner == 0) warmed_live_bytes = after.allocator_live_bytes;
        else if (after.allocator_live_bytes > warmed_live_bytes)
            throw datapump::Error("released Gram owner retained per-stream allocation: owner=" + std::to_string(owner) +
                " live=" + std::to_string(after.allocator_live_bytes) + " warmed=" + std::to_string(warmed_live_bytes));
    }
}

void dc_gram_oracle(r::ReceiverBulkBackend& backend) {
    constexpr std::size_t rows = 17;
    constexpr double variance = .8;
    const std::array<std::size_t, 1> bins{0};
    const std::array<Complex, 1> y{{{.2, -.1}}};
    std::array<double, rows> offsets{};
    std::iota(offsets.begin(), offsets.end(), 0.);
    std::array<std::array<Complex, rows>, 2> samples{};
    std::array<Complex, 2> projected{};
    for (std::size_t i = 0; i < rows; ++i) {
        samples[0][i] = Complex(.3, .2) + (i % 2 ? -.07 : .07);
        samples[1][i] = Complex(.1, -.4) + (i % 2 ? Complex(0., -.09) : Complex(0., .09));
        for (std::size_t tap = 0; tap < samples.size(); ++tap) projected[tap] += samples[tap][i] / std::sqrt(rows);
    }
    for (const bool precise : {false, true}) {
        auto owner = backend.make_fine(precise ? 5 : 80, precise ? .0005 : .25);
        for (const std::size_t taps : {1, 2}) {
            const r::FineBulkBlock block{rows, taps, bins, y, variance};
            std::array<r::FineBulkStatistics, 1> result{};
            const auto before = owner->receipt();
            owner->statistics(block, 1, [&](std::size_t candidate, std::size_t first, std::size_t count,
                                              const r::BulkMappedSink& sink) {
                check(candidate == 0 && first == 0 && count == rows, "DC Gram oracle changed its extent");
                const std::array<r::BulkDirectColumn, 2> direct{{
                    {0, samples[0], "test-native-direct"}, {1, samples[1], "test-native-direct"}}};
                sink(offsets, {}, std::span(direct).first(taps));
            }, result, {});
            const auto after = owner->receipt();
            check(result[0].nonzero_basis && after.fft_calls == before.fft_calls &&
                  after.dc_reduction_values - before.dc_reduction_values == 2 * taps * rows &&
                  after.cuda_direct_gram_values - before.cuda_direct_gram_values == (after.cuda ? 2 * taps * taps : 0) &&
                  after.cuda_bmm_gram_values == before.cuda_bmm_gram_values &&
                  after.precision == (precise ? "fp64" : "fp32_bulk_fp64_coordinates_and_reductions"),
                  "DC Gram changed arithmetic policy, normalization work or selected a CUDA BMM");
            for (std::size_t h = 0; h < 3; ++h) {
                for (std::size_t i = 0; i < taps; ++i) {
                    const auto correlation = h == 1 ? std::conj(projected[i]) * y[0] / variance : Complex{};
                    near(result[0].correlations[h * taps + i], correlation, precise ? 2e-12 : 2e-6,
                         "DC Gram oracle changed nonzero correlation or zero-hypothesis state");
                    for (std::size_t j = 0; j < taps; ++j) {
                        const auto gram = h == 1 ? std::conj(projected[i]) * projected[j] / variance : Complex{};
                        near(result[0].gram[(h * taps + i) * taps + j], gram, precise ? 2e-12 : 2e-6,
                             "DC Gram oracle changed the independent Hermitian product");
                    }
                }
            }
        }
    }
}

void full_slot(const std::shared_ptr<r::ReceiverBulkBackend>& backend,
               unsigned workers = 1, bool native_fine = false) {
    constexpr unsigned fs = 497;
    v::ReceiverConfig config;
    config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
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
        const auto native = v::score_receiver_slot(config, slot, key, samples, {}, workers,
                                                   v::receiver_slot_workspace(config, slot, workers));
        auto tensor_config = config;
        tensor_config.bulk_backend = backend;
        const auto before = backend->receipt();
        const auto tensor = v::score_receiver_slot(tensor_config, slot, key, samples, {}, workers,
                                                   v::receiver_slot_workspace(tensor_config, slot, workers));
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
              after.score_rows > before.score_rows && after.reserved_bytes == 0,
              "production slot did not actually execute and release its tensor coarse stage");
        if (native_fine) {
            check(after.native_fine_sessions == before.native_fine_sessions + 1 &&
                  after.native_fine_fallback_reason == "small-chronological-blocks" &&
                  after.mapped_values == before.mapped_values && after.statistics_blocks == before.statistics_blocks &&
                  after.cuda_fused_mapped_launches == before.cuda_fused_mapped_launches &&
                  after.cuda_fused_mapped_values == before.cuda_fused_mapped_values,
                  "small public blocks did not use and report native fine work");
        } else {
            check(after.native_fine_sessions == before.native_fine_sessions &&
                  after.mapped_values > before.mapped_values && after.statistics_blocks > before.statistics_blocks,
                  "production slot did not execute its requested tensor fine stage");
        }
        if (after.cuda && !native_fine) {
            check(after.cuda_fused_mapped_launches > before.cuda_fused_mapped_launches &&
                  after.cuda_fused_mapped_values - before.cuda_fused_mapped_values ==
                      after.mapped_values - before.mapped_values,
                  "ordinary CUDA slot did not complete its fused mapped work");
        } else if (!after.cuda) {
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
    config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
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
void automatic_cuda_matrix(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    const auto identity = backend->status_receipt();
    const auto capability = 10 * identity.cuda_compute_major + identity.cuda_compute_minor;
    const bool qualified = capability == 89 || capability == 120;
    const auto score = [&](r::ReceiverBulkBackend& selected, const MatrixCoarseFixture& fixture,
                           std::size_t tile_width, bool matrix, std::string_view reason) {
        const auto peak = fixture.oracle_point(fixture.active, 0, fixture.frequencies / 2 + 3, 7);
        auto owner = selected.make_coarse(MatrixCoarseFixture::fs, .25);
        auto geometry = fixture.geometry;
        geometry.lag_workspace_tile_width = tile_width;
        r::CoarseBulkWork prepare, search;
        owner->prepare(geometry, fixture.active, fixture.workspaces, std::span(&fixture.group, 1), prepare, {});
        check(owner->precision_reason() == reason, "automatic CUDA lag selection reason differs from its public geometry");
        std::size_t seen = 0;
        for (std::size_t first = 0; first < fixture.workspaces.size();) {
            const auto rows = std::min(tile_width, fixture.workspaces.size() - first);
            owner->lag_tile(first, rows, [&](std::size_t workspace, std::size_t count, std::span<const double> scores) {
                const auto grid = fixture.frequencies * fixture.arrivals;
                check(workspace == seen && scores.size() == count * grid,
                      "automatic CUDA matrix or FFT changed public workspace order");
                for (std::size_t row = 0; row < count; ++row) {
                    for (const auto f : {std::size_t{0}, fixture.frequencies / 2 + 3, fixture.frequencies - 1}) {
                        for (const auto a : {std::size_t{0}, std::size_t{7}, fixture.arrivals - 1}) {
                            const auto actual = scores[row * grid + f * fixture.arrivals + a];
                            const auto expected = fixture.oracle_point(fixture.active, workspace + row, f, a);
                            if (matrix) {
                                check(std::abs(actual - expected) <= .01 * std::max(1., peak),
                                      "automatic CUDA INT8 component differs from direct scores");
                            } else {
                                near(actual, expected, 3e-5, "automatic CUDA FFT component differs from direct scores");
                            }
                        }
                    }
                }
                seen += count;
            }, search, {});
            first += rows;
        }
        const auto receipt = owner->receipt();
        check(seen == fixture.workspaces.size() && receipt.executed &&
              search.score_rows == fixture.workspaces.size() * fixture.frequencies * fixture.arrivals &&
              receipt.score_rows == search.score_rows && receipt.requested_coarse_math == "fft",
              "automatic CUDA matrix or FFT skipped public scores or changed the requested policy");
        if (matrix) {
            check(receipt.cuda_matrix_lt_imma_calls && receipt.cuda_matrix_prepared_b_groups == 1 &&
                  receipt.cuda_matrix_output_values == search.score_rows && search.inverse_ffts == 0 &&
                  receipt.precision == "int8_matrix_fp64_coordinates_and_reductions",
                  "automatic CUDA INT8 selection lacked complete Tensor Core work or arithmetic identity");
        } else {
            check(!receipt.cuda_matrix_int8_calls && !receipt.cuda_matrix_prepared_b_groups && search.inverse_ffts > 0,
                  "automatic CUDA lag fallback executed matrix work or omitted FFT work");
            if (reason == "cuda-int8-matrix-workspace-limit")
                check(receipt.matrix_fft_fallback_counts[matrix_fallback_index("workspace-limit")] > 0,
                      "partial CUDA phase lease omitted its matrix workspace fallback receipt");
        }
    };
    const MatrixCoarseFixture fixture(769, 65, 64, 1, 32, false);
    score(*backend, fixture, 32, qualified, qualified ? "cuda-int8-matrix" : "cuda-int8-matrix-unqualified-hardware");
    auto fp32 = v::make_automatic_cuda_receiver_backend(v::TensorPrecision::fp32_minimum);
    score(*fp32, fixture, 32, false, "cuda-int8-matrix-fp32-minimum");
    if (!qualified) return;
    score(*backend, fixture, 25, false, "cuda-int8-matrix-unqualified-shape");
    // The full bank has 2496 rows, but each main dispatch has only 832.
    const MatrixCoarseFixture narrow_arrival(769, 65, 32, 1, 78, false);
    score(*backend, narrow_arrival, 26, false, "cuda-int8-matrix-unqualified-shape");

    // Hold only an accounting lease. The remaining 40 MiB covers the
    // canonical FFT minimum but cannot admit the full INT8 rectangle.
    const auto available = identity.workspace_limit_bytes - identity.reserved_bytes;
    constexpr std::size_t remaining = 40 * mib;
    auto held = backend->make_coarse(MatrixCoarseFixture::fs, .25);
    held->begin_fir_projection(1, 1, (available - remaining) / (3 * sizeof(std::complex<float>)), {});
    score(*backend, fixture, 32, false, "cuda-int8-matrix-workspace-limit");
}

void automatic_cuda_backend() {
    const auto backend = v::make_automatic_cuda_receiver_backend();
    for (const auto rows : {750u, 1024u})
        check(backend->fine_fallback_reason({rows, 40, 1, 26, true}) == "small-chronological-blocks",
              "bounded chronological block did not select native fine processing");
    check(backend->fine_fallback_reason({1025, 40, 1, 32, true}).empty() &&
          backend->fine_fallback_reason({750, 41, 1, 32, true}).empty() &&
          backend->fine_fallback_reason({750, 40, 2, 32, true}).empty() &&
          backend->fine_fallback_reason({750, 40, 1, 25, true}).empty() &&
          backend->fine_fallback_reason({750, 40, 1, 32, false}).empty(),
          "automatic native fine processing exceeded its public geometry or hardware bounds");
    const auto before = backend->status_receipt();
    check(before.cuda && !before.executed && !before.score_rows && !before.statistics_blocks &&
          before.workspace_limit_bytes >= 128 * mib && !before.allocator_reserved_bytes,
          "automatic CUDA initialization did not produce a fresh device and workspace receipt");

    // Quote, but do not allocate, two FIR phases that cannot fit together.
    // One owner has a complete lease while the second is cancelled.
    const auto output = before.workspace_limit_bytes / (2 * 3 * sizeof(std::complex<float>)) + 1;
    auto resident = backend->make_coarse(100, 0);
    resident->begin_fir_projection(64, 1, output, {});
    const auto held = backend->status_receipt().reserved_bytes;
    check(held > before.workspace_limit_bytes / 2 && held <= before.workspace_limit_bytes,
          "automatic CUDA did not reserve the complete phase");
    const auto peer = v::make_automatic_cuda_receiver_backend();
    check(peer->status_receipt().reserved_bytes == held, "automatic CUDA duplicated a process/device workspace");

    const auto wait_for_queued = [&] {
        using namespace std::chrono_literals;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!peer->status_receipt().device_admission_waiters) {
            check(std::chrono::steady_clock::now() < deadline, "CUDA owner did not wait for its phase lease");
            std::this_thread::sleep_for(1ms);
        }
    };
    std::exception_ptr cancelled;
    std::jthread waiting([&](std::stop_token stop) {
        try {
            auto second = peer->make_coarse(100, 0);
            second->begin_fir_projection(64, 1, output, stop);
        } catch (...) {
            cancelled = std::current_exception();
        }
    });
    wait_for_queued();
    waiting.request_stop();
    waiting.join();
    check(static_cast<bool>(cancelled), "cancelled CUDA admission unexpectedly obtained a lease");
    try {
        std::rethrow_exception(cancelled);
    } catch (const datapump::Error& error) {
        check(std::string_view(error.what()).find("cancelled") != std::string_view::npos,
              "CUDA admission did not report cancellation");
    }
    const auto after_cancel = peer->status_receipt();
    check(after_cancel.reserved_bytes == held && !after_cancel.device_admission_waiters,
          "cancelled CUDA admission leaked a waiter or changed another owner's lease");

    std::exception_ptr wake_failure;
    std::jthread waking([&](std::stop_token stop) {
        try {
            auto second = peer->make_coarse(100, 0);
            second->begin_fir_projection(64, 1, output, stop);
            check(peer->status_receipt().reserved_bytes == held, "queued CUDA owner did not receive its complete lease");
        } catch (...) {
            wake_failure = std::current_exception();
        }
    });
    wait_for_queued();
    resident.reset();
    waking.join();
    if (wake_failure) std::rethrow_exception(wake_failure);
    check(backend->status_receipt().reserved_bytes == before.reserved_bytes, "CUDA phase lease was not released");

    automatic_cuda_matrix(backend);
    coarse(*backend);
    full_slot(backend);
    if (std::string_view(datapump::modem::detail::search_fft::kernel_name()) == "avx2")
        full_slot(backend, 32, true);
    const auto after = backend->receipt();
    const auto status = backend->status_receipt();
    const auto fresh = peer->status_receipt();
    check(after.cuda && after.executed && after.score_rows && after.statistics_blocks &&
          after.reserved_bytes == before.reserved_bytes &&
          after.peak_reserved_bytes <= after.workspace_limit_bytes &&
          after.allocator_peak_reserved_bytes && after.allocator_peak_reserved_bytes <= after.workspace_limit_bytes &&
          status.score_rows == after.score_rows && status.statistics_blocks == after.statistics_blocks &&
          !status.allocator_reserved_bytes && !fresh.executed && !fresh.score_rows && !fresh.statistics_blocks,
          "CUDA status mixed another backend's work or lost its device workspace bound");
    std::cout << "Automatic CUDA phase admission and complete slot checks passed on " << after.device_name << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool cuda = false, matrix_only = false, matrix_benchmark = false;
        bool peaks_only = false, peak_benchmark = false, component_describe = false, automatic_cuda = false;
        std::optional<v::TensorCoarseMath> matrix_math;
        std::optional<unsigned> matrix_benchmark_case;
        std::optional<std::size_t> matrix_benchmark_workspace_mib;
        for (int i = 1; i < argc; ++i) {
            const std::string_view option(argv[i]);
            if (option == "--cuda") cuda = true;
            else if (option == "--cuda-automatic") automatic_cuda = true;
            else if (option == "--component-describe") component_describe = true;
            else if (option == "--matrix-only") matrix_only = true;
            else if (option == "--matrix-benchmark") matrix_benchmark = true;
            else if (option == "--peaks-only") peaks_only = true;
            else if (option == "--peak-benchmark") peak_benchmark = true;
            else if (option == "--matrix-benchmark-case") {
                check(++i < argc && !matrix_benchmark_case, "--matrix-benchmark-case requires one 0|1 value");
                const std::string_view value(argv[i]);
                check(value == "0" || value == "1", "--matrix-benchmark-case requires 0|1");
                matrix_benchmark_case = value == "0" ? 0 : 1;
            }
            else if (option == "--matrix-benchmark-workspace-mib") {
                check(++i < argc && !matrix_benchmark_workspace_mib,
                      "--matrix-benchmark-workspace-mib requires one positive integer");
                const std::string_view value(argv[i]);
                std::size_t amount = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), amount);
                check(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && amount && amount <= SIZE_MAX / mib,
                      "invalid --matrix-benchmark-workspace-mib value");
                matrix_benchmark_workspace_mib = amount;
            }
            else if (option == "--matrix-math") {
                check(++i < argc && !matrix_math, "--matrix-math requires one fp16|int8 value");
                const std::string_view name(argv[i]);
                if (name == "fp16") matrix_math = v::TensorCoarseMath::fp16;
                else if (name == "int8") matrix_math = v::TensorCoarseMath::int8;
                else check(false, "--matrix-math requires fp16|int8");
            }
            else check(false, "usage: test_triad_tensor [--cuda] [--cuda-automatic|--component-describe|--matrix-only|--matrix-benchmark|--peaks-only|--peak-benchmark] [--matrix-math fp16|int8] [--matrix-benchmark-case 0|1] [--matrix-benchmark-workspace-mib N]");
        }
        check(cuda || (!matrix_only && !matrix_benchmark && !peaks_only && !peak_benchmark && !automatic_cuda),
              "CUDA-specific checks require --cuda");
        check(static_cast<unsigned>(matrix_only) + matrix_benchmark + peaks_only + peak_benchmark + component_describe +
                  automatic_cuda <= 1,
              "select at most one focused check or benchmark");
        check(!(matrix_only && matrix_benchmark), "select either --matrix-only or --matrix-benchmark");
        check(!matrix_math || matrix_only || matrix_benchmark, "--matrix-math requires --matrix-only or --matrix-benchmark");
        check(matrix_benchmark || (!matrix_benchmark_case && !matrix_benchmark_workspace_mib),
              "benchmark case/workspace selectors require --matrix-benchmark");
        if (automatic_cuda) {
            automatic_cuda_backend();
            return 0;
        }
        if (component_describe) {
            // CUDA_VISIBLE_DEVICES selects which physical GPU is visible as
            // device zero. A description initializes its native context but
            // never invokes numerical fixtures or claims executed DSP work.
            const auto described = v::make_tensor_receiver_backend({
                cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu, 0, 128 * mib,
                v::TensorPrecision::automatic});
            emit_component_identity(described->receipt());
            return 0;
        }
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (peak_benchmark) {
            cuda_peak_benchmark();
            return 0;
        }
        if (matrix_benchmark) {
            cuda_matrix_benchmark(matrix_math, matrix_benchmark_case, matrix_benchmark_workspace_mib.value_or(1024));
            return 0;
        }
#endif
        auto backend = v::make_tensor_receiver_backend({
            cuda ? v::TensorDevice::cuda : v::TensorDevice::cpu, 0, 128 * mib, v::TensorPrecision::automatic});
        tensor_policy_receipts(cuda);
#if DATAPUMP_TRIAD_TENSOR_CUDA
        if (cuda) {
            if (peaks_only) {
                cuda_arrival_peak_oracle();
                coarse(*backend);
                std::cout << "Robust CUDA exact arrival frontier fixtures passed\n";
                return 0;
            }
            if (!matrix_only) {
                cuda_arrival_peak_oracle();
                cuda_mapping_rounding_holdouts();
                cuda_mapping_pointwise();
            }
            cuda_matrix_coarse(matrix_math);
            matrix_policy_fallback(true);
            if (matrix_only) {
                std::cout << "Robust CUDA bounded matrix fixtures passed\n";
                return 0;
            }
        }
#else
        check(!cuda, "CUDA tensor test requires the compiled mapping parity fixture");
#endif
        fir(*backend);
        staged_fir(*backend);
        coarse_helper_dynamic_range(*backend);
        coarse(*backend);
        if (!cuda) {
            matrix_policy_fallback(false);
            tight_staged_fir();
            tight_descriptor_workspace();
        }
        fine(*backend);
        fine_descriptor_cache(*backend);
        gram_mode_boundary(*backend);
        gram_workspace_lifetime(*backend);
        dc_gram_oracle(*backend);
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
