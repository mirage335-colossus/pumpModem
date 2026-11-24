#include "datapump/robust_audio_frontend.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using datapump::live::detail::AudioInput;
using datapump::live::detail::calibrate_noise_dbfs_hz;
using datapump::live::detail::noise_calibration_samples;
using datapump::live::detail::noise_calibration_workspace_bytes;
using datapump::live::detail::real_pcm;
using datapump::triad::v3::Complex;

constexpr std::uint32_t rate = 6000;
constexpr double center = 1725.125, frequency = 700.0625, amplitude = .17;
constexpr std::uint64_t high_origin = (std::uint64_t{1} << 54) + 37;

void check(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

Complex phase(std::uint64_t index, double hz) {
    return std::polar(1., 2 * std::numbers::pi * static_cast<double>(index) * hz / rate);
}

std::vector<float> waveform() {
    std::vector<float> result(10017);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = real_pcm(amplitude * phase(i, frequency - center), i, center, rate);
    return result;
}

void continuity_and_passband() {
    const auto pcm = waveform();
    AudioInput whole(rate, center, 500, 2900), chunked(rate, center, 500, 2900, high_origin);
    std::vector<Complex> expected, part, observed;
    const auto first = whole.project(pcm, expected);
    check(first == whole.first_valid_sample() && expected.size() == pcm.size() - 2 * whole.halo_samples(),
          "Robust PCM projection changed its centered FIR coordinates");
    for (std::size_t at = 0; at < pcm.size(); at += 273) {
        chunked.project({}, part);
        check(part.empty(), "An empty chunk generated a synthetic audio sample");
        const auto begin = chunked.project(
            std::span(pcm).subspan(at, std::min<std::size_t>(273, pcm.size() - at)), part);
        if (!part.empty())
            check(begin == high_origin + first + observed.size(),
                  "Robust projection lost the absolute sample position across a chunk");
        observed.insert(observed.end(), part.begin(), part.end());
    }
    check(observed == expected, "Robust audio projection depends on chunk size or large absolute origin");
    double peak_error = 0;
    Complex conjugate_image{};
    for (std::size_t i = 0; i < observed.size(); ++i) {
        peak_error = std::max(peak_error, std::abs(observed[i] - amplitude * phase(first + i, frequency - center)));
        conjugate_image += observed[i] * phase(first + i, frequency + center);
    }
    check(peak_error < .001 && std::abs(conjugate_image) / static_cast<double>(observed.size()) < .0007,
          "Robust analytic projection did not preserve the wanted tone or suppress its image");
}

void erasure_and_eof() {
    auto pcm = waveform();
    constexpr std::size_t gap = 1703;
    pcm[gap] = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::uint8_t> valid(pcm.size(), 1), mask, chunk_mask, observed_mask;
    valid[gap] = 0;
    AudioInput whole(rate, center, 500, 2900, high_origin), chunked(rate, center, 500, 2900, high_origin);
    std::vector<Complex> expected, part, observed;
    const auto first = whole.project(pcm, expected, valid, &mask);
    check(mask.size() == expected.size(), "Robust projection omitted its physical validity mask");
    const auto halo = whole.halo_samples();
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const auto frame = first - high_origin + i;
        const bool affected = frame >= gap - halo && frame <= gap + halo;
        check(mask[i] == !affected && std::isfinite(expected[i].real()) && std::isfinite(expected[i].imag()),
              "A masked nonfinite audio sample did not invalidate the full FIR support");
    }
    for (std::size_t at = 0; at < pcm.size(); at += 301) {
        const auto count = std::min<std::size_t>(301, pcm.size() - at);
        chunked.project(std::span(pcm).subspan(at, count), part, std::span(valid).subspan(at, count), &chunk_mask);
        observed.insert(observed.end(), part.begin(), part.end());
        observed_mask.insert(observed_mask.end(), chunk_mask.begin(), chunk_mask.end());
    }
    check(observed == expected && observed_mask == mask, "Robust masked projection changed at a chunk edge");
    chunked.project({}, part, {}, &chunk_mask);
    check(part.empty() && chunk_mask.empty(), "Audio EOF invented observed filter-tail samples");
    AudioInput unmasked(rate, center, 500, 2900);
    try {
        unmasked.project(pcm, part);
        throw std::runtime_error("Unmasked nonfinite PCM entered Robust projection");
    } catch (const datapump::Error&) {}
}

void startup_noise_calibration() {
    constexpr std::uint32_t sample_rate = 6000;
    const auto count = noise_calibration_samples(sample_rate);
    check(count == 16 * 1024 && noise_calibration_workspace_bytes(sample_rate) > count * sizeof(float),
          "Robust calibration capture or reported workspace changed unexpectedly");
    std::mt19937 generator(0x51A7U);
    std::normal_distribution<float> white(0.f, .025f);
    std::vector<float> pcm(count);
    for (auto& sample : pcm) sample = white(generator);
    const auto measured = calibrate_noise_dbfs_hz(pcm, sample_rate);
    const auto expected = 10 * std::log10(2. * .025 * .025 / sample_rate);
    check(std::isfinite(measured.noise_dbfs_hz) && measured.noise_dbfs_hz >= expected + 1. &&
              measured.noise_dbfs_hz <= expected + 8.,
          "Robust white-noise density estimate is not normalized or conservatively bounded");

    constexpr float added_dc = 2e-5f;
    std::vector<float> quiet(count);
    for (std::size_t i = 0; i < count; ++i) {
        quiet[i] = pcm[i] * 1e-5f;
        pcm[i] = quiet[i] + added_dc;
    }
    const auto quiet_baseline = calibrate_noise_dbfs_hz(quiet, sample_rate);
    const auto with_dc = calibrate_noise_dbfs_hz(pcm, sample_rate);
    check(std::abs(with_dc.dc_offset - quiet_baseline.dc_offset - added_dc) < 1e-12 &&
              std::abs(with_dc.noise_dbfs_hz - quiet_baseline.noise_dbfs_hz) < .001,
          "Quiet input with a small DC offset changed the calibrated noise density");

    AudioInput original(rate, center, 500, 2900), corrected(rate, center, 500, 2900);
    original.set_dc_offset(quiet_baseline.dc_offset);
    corrected.set_dc_offset(with_dc.dc_offset);
    std::vector<Complex> original_iq, corrected_iq;
    const auto first_original = original.project(quiet, original_iq);
    const auto first_corrected = corrected.project(pcm, corrected_iq);
    check(first_original == first_corrected && !original_iq.empty() &&
              std::equal(original_iq.begin(), original_iq.end(), corrected_iq.begin(), corrected_iq.end(),
                         [](Complex a, Complex b) { return std::abs(a - b) < 2e-12; }),
          "Robust live projection did not use the calibrated DC correction");

    constexpr double tone_frequency = 937.5;
    constexpr double tone_amplitude = 1.8e-6;
    for (std::size_t i = 0; i < count; ++i)
        pcm[i] = quiet[i] + static_cast<float>(
            tone_amplitude * std::sin(2 * std::numbers::pi * tone_frequency * i / sample_rate));
    const auto with_tone = calibrate_noise_dbfs_hz(pcm, sample_rate);
    check(with_tone.noise_dbfs_hz > quiet_baseline.noise_dbfs_hz + 20. &&
              with_tone.noise_dbfs_hz < -120.,
          "Robust calibration did not account for a stable, low-level tone in the estimated density");

    auto expect_error = [](const std::vector<float>& input, const char* fragment) {
        try {
            (void)calibrate_noise_dbfs_hz(input, sample_rate);
        } catch (const std::exception& error) {
            if (std::string_view(error.what()).find(fragment) == std::string_view::npos)
                throw std::runtime_error(std::string("Unexpected Robust calibration diagnostic: ") + error.what());
            return;
        }
        throw std::runtime_error("Robust calibration accepted unusable startup audio");
    };
    std::fill(pcm.begin(), pcm.end(), 0.f);
    expect_error(pcm, "silent");

    std::fill(pcm.begin(), pcm.end(), added_dc);
    expect_error(pcm, "silent");

    for (std::size_t i = 0; i < count; ++i) pcm[i] = white(generator);
    for (std::size_t i = count - 1024; i < count; ++i) pcm[i] *= 5.f;
    expect_error(pcm, "unstable");

    std::fill(pcm.begin(), pcm.end(), .1f);
    pcm[count / 2] = 1.f;
    expect_error(pcm, "clipping");

    pcm[count / 2] = std::numeric_limits<float>::quiet_NaN();
    expect_error(pcm, "nonfinite");
}
}

int main() {
    try {
        continuity_and_passband();
        erasure_and_eof();
        startup_noise_calibration();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
