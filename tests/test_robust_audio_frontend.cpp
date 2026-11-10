#include "datapump/robust_audio_frontend.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using datapump::live::detail::AudioInput;
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
}

int main() {
    try {
        continuity_and_passband();
        erasure_and_eof();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
