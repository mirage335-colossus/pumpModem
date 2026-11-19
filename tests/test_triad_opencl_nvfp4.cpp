#include "datapump/host/sandbox.hpp"
#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
