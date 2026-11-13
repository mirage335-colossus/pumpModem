#include "fast/ssb_frontend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace datapump::fast::detail;
using Complex = std::complex<double>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void invalid(Function action) {
    try { action(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Fast SSB frontend accepted invalid geometry or PCM");
}

Complex phase(double sample, double frequency, double rate) {
    return std::polar(1., 2 * std::numbers::pi * sample * frequency / rate);
}

double dc_sum(const SsbFrontendGeometry& g) {
    // Neumaier summation keeps the assertion independent of whether this
    // compiler implements an extended long double type.
    double sum = 0, correction = 0;
    for (const auto tap : g.taps) {
        const auto next = sum + tap;
        correction += std::abs(sum) >= std::abs(tap) ? (sum - next) + tap : (tap - next) + sum;
        sum = next;
    }
    return sum + correction;
}

double response(const SsbFrontendGeometry& g, double frequency) {
    double result = g.taps[g.radius()];
    for (std::size_t i = 1; i <= g.radius(); ++i)
        result += 2 * g.taps[g.radius() + i] *
            std::cos(2 * std::numbers::pi * frequency * double(i) / g.input_rate);
    return result;
}

std::vector<Complex> collect(const SsbFrontendGeometry& g, std::span<const float> input,
    std::span<const std::size_t> chunks, std::size_t output_chunk = 256) {
    std::vector<Complex> result;
    SsbFrontend* running = nullptr;
    SsbFrontend frontend(g, [&](const SsbFrontendOutput& block) {
        check(!block.samples.empty() && block.samples.size() <= output_chunk,
            "Fast SSB output exceeded its bounded callback storage");
        check(block.decimation == g.decimation &&
            block.first_center_input_sample == g.radius() + result.size() * g.decimation,
            "Fast SSB output lost its absolute FIR center or decimation phase");
        const auto last = block.first_center_input_sample + (block.samples.size() - 1) * block.decimation;
        check(last + g.radius() < running->input_samples(),
            "Fast SSB output arrived before its full observed input support");
        result.insert(result.end(), block.samples.begin(), block.samples.end());
    }, output_chunk);
    running = &frontend;
    const auto bytes = frontend.workspace_bytes();
    std::size_t at = 0, chunk = 0;
    while (at < input.size()) {
        check(frontend.push({}) == 0, "Empty Fast SSB push consumed input");
        const auto count = std::min(chunks[chunk++ % chunks.size()], input.size() - at);
        check(frontend.push(input.subspan(at, count)) == count, "Fast SSB lost input");
        at += count;
    }
    const auto before_finish = result.size();
    frontend.finish();
    frontend.finish();
    check(frontend.finished() && result.size() == before_finish,
        "Fast SSB EOF fabricated filter support or tail output");
    const auto count = input.size() > 2 * g.radius() ?
        (input.size() - 1 - 2 * g.radius()) / g.decimation + 1 : 0;
    check(result.size() == count && frontend.output_samples() == count &&
        frontend.input_samples() == input.size(), "Fast SSB full-support sample count changed");
    check(frontend.workspace_bytes() == bytes, "Fast SSB workspace grew with stream length");
    try { frontend.push({}); }
    catch (const std::logic_error&) { return result; }
    throw std::runtime_error("Finished Fast SSB frontend accepted more input");
}

void geometry_and_continuity() {
    constexpr std::array<std::size_t, 1> whole{100000};
    constexpr std::array<std::size_t, 8> chunks{1, 2, 17, 257, 4093, 11, 701, 3};
    for (const auto rate : {44100u, 47999u, 48000u, 96000u, 192000u}) {
        const auto g = make_ssb_frontend_geometry(rate);
        check(g.decimation == rate / 12000 &&
            g.radius() == static_cast<std::size_t>(std::lround(64. * rate / 12000.)),
            "Fast SSB rate did not select the declared FIR and decimation");
        check(std::abs(g.output_rate() - double(rate) / g.decimation) < 1e-12 &&
            std::abs(dc_sum(g) - 1) < 2e-15,
            "Fast SSB geometry lost output rate or normalized DC gain");
        for (std::size_t i = 0; i < g.taps.size(); ++i)
            check(g.taps[i] == g.taps[g.taps.size() - 1 - i], "Fast SSB FIR is not symmetric");
        std::vector<float> input(2 * g.radius() + 10423);
        for (std::size_t n = 0; n < input.size(); ++n)
            input[n] = float(.7 * phase(n, 1731.375, rate).real() + .2 * phase(n, 11713.25, rate).real());
        check(collect(g, input, whole) == collect(g, input, chunks, 7),
            "Fast SSB values or continuous oscillator phase changed at chunk boundaries");
        for (const auto size : {std::size_t{0}, std::size_t{1}, 2 * g.radius(),
            2 * g.radius() + 1, 2 * g.radius() + g.decimation, 2 * g.radius() + g.decimation + 1})
            collect(g, std::span(input).first(size), chunks);
    }
    check(make_ssb_frontend_geometry(44100).output_rate() == 14700 &&
        make_ssb_frontend_geometry(48000).output_rate() == 12000 &&
        make_ssb_frontend_geometry(96000).output_rate() == 12000,
        "Fast SSB treated 44.1 kHz as an integer path to 12 kHz");
    const auto narrow=make_ssb_frontend_geometry(48000,1800,832,9);
    check(narrow.decimation==9 && narrow.radius()==256 && response(narrow,612.)>.99 &&
        std::abs(response(narrow,2600.))<.001,
        "Compact Fast frontend lost translated-band coverage or anti-alias rejection");
    std::vector<float> tone(6091);
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(phase(i,2350.,48000.).real());
    check(collect(narrow,tone,whole)==collect(narrow,tone,chunks,7),
        "Compact Fast frontend changed its exact center sequence across chunks");
    for (const auto rate : {0u, 44099u, 192001u}) invalid([&] { make_ssb_frontend_geometry(rate); });
    invalid([] { make_ssb_frontend_geometry(48000, 0); });
    invalid([] { make_ssb_frontend_geometry(48000, 24000); });
    invalid([] { make_ssb_frontend_geometry(48000, 1500, 6000); });
    invalid([] { make_ssb_frontend_geometry(48000, 1800, 832, 30); });
    invalid([] { make_ssb_frontend_geometry(48000, std::numeric_limits<double>::quiet_NaN()); });
    invalid([] { SsbFrontend f(make_ssb_frontend_geometry(48000), {}); });
    invalid([] { SsbFrontend f(make_ssb_frontend_geometry(48000), [](const auto&) {}, 4097); });
    auto bad = make_ssb_frontend_geometry(48000);
    bad.taps.front() = 1;
    invalid([&] { SsbFrontend f(bad, [](const auto&) {}); });
    SsbFrontend finite(make_ssb_frontend_geometry(48000), [](const auto&) {});
    const std::array<float, 1> nonfinite{std::numeric_limits<float>::infinity()};
    invalid([&] { finite.push(nonfinite); });
    check(finite.input_samples() == 0, "Nonfinite PCM advanced the Fast SSB oscillator");
}

void impulse_and_finite_tail() {
    constexpr std::array<std::size_t, 3> chunks{71, 3, 907};
    for (const auto rate : {44100u, 48000u, 96000u}) {
        for (const auto cutoff : {1200., 1520.}) {
            const auto g = make_ssb_frontend_geometry(rate, 1500, cutoff);
            const auto impulse = 3 * g.radius() + 11;
            std::vector<float> input(6 * g.radius() + 37);
            input[impulse] = 1;
            const auto output = collect(g, input, chunks);
            const auto mixed = 2. * phase(impulse, -g.carrier_hz, rate);
            bool seen = false;
            for (std::size_t i = 0; i < output.size(); ++i) {
                const auto center = g.radius() + i * g.decimation;
                const auto offset = static_cast<std::ptrdiff_t>(impulse) - static_cast<std::ptrdiff_t>(center);
                const auto tap = offset + static_cast<std::ptrdiff_t>(g.radius());
                const auto expected = tap >= 0 && tap < static_cast<std::ptrdiff_t>(g.taps.size()) ?
                    mixed * g.taps[static_cast<std::size_t>(tap)] : Complex{};
                check(std::abs(output[i] - expected) < 2e-13,
                    "Fast SSB impulse did not match the exposed full-rate FIR and carrier phase");
                if (tap < 0 || tap >= static_cast<std::ptrdiff_t>(g.taps.size()))
                    check(output[i] == Complex{}, "Fast SSB FIR retained a nonphysical infinite tail");
                seen = seen || std::abs(output[i]) > .01;
            }
            check(seen && g.taps.front() == 0 && g.taps.back() == 0,
                "Fast SSB finite impulse fixture missed its passband response");
        }
    }
}

void passband_and_alias_rejection() {
    constexpr std::array<std::size_t, 3> chunks{101, 4099, 7};
    for (const auto rate : {44100u, 48000u, 96000u}) {
        const auto g = make_ssb_frontend_geometry(rate);
        for (const auto offset : {-700., 0., 713.25}) {
            const auto rf = g.carrier_hz + offset;
            constexpr double amplitude = .37, angle = .3125;
            std::vector<float> input(2 * g.radius() + 6001);
            for (std::size_t n = 0; n < input.size(); ++n)
                input[n] = float(amplitude * (phase(n, rf, rate) * std::polar(1., angle)).real());
            const auto output = collect(g, input, chunks);
            const auto wanted_gain = response(g, offset);
            const auto image_gain = response(g, -rf - g.carrier_hz);
            check(std::abs(wanted_gain - 1) < 3e-4 && std::abs(image_gain) < 2e-5,
                "Fast SSB FIR lost the wanted band or negative-image suppression");
            for (std::size_t i = 0; i < output.size(); ++i) {
                const auto center = g.radius() + i * g.decimation;
                const auto wanted = amplitude * wanted_gain * phase(center, offset, rate) * std::polar(1., angle);
                const auto image = amplitude * image_gain * phase(center, -rf - g.carrier_hz, rate) * std::polar(1., -angle);
                check(std::abs(output[i] - wanted - image) < 3e-8,
                    "Fast SSB changed complex passband gain, sign or FIR center phase");
            }
        }
        // Decimating this RF tone before filtering places it at +431 Hz in the
        // desired baseband. Correct full-rate filtering removes it first.
        constexpr double alias_offset = 431;
        const auto rf = g.carrier_hz + g.output_rate() + alias_offset;
        std::vector<float> input(2 * g.radius() + 8001);
        for (std::size_t n = 0; n < input.size(); ++n) input[n] = float(phase(n, rf, rate).real());
        const auto output = collect(g, input, chunks);
        double peak = 0;
        for (const auto sample : output) peak = std::max(peak, std::abs(sample));
        check(peak < 2e-6 && std::abs(response(g, alias_offset)) > .999,
            "Fast SSB decimation admitted the high-frequency alias into the passband");
    }
}

double crossing(const SsbFrontendGeometry& g, double db) {
    const auto threshold = std::pow(10., db / 20.);
    double lower = 0, upper = g.cutoff_hz + 1000;
    for (int i = 0; i < 45; ++i) {
        const auto middle = (lower + upper) / 2;
        if (std::abs(response(g, middle)) > threshold) lower = middle;
        else upper = middle;
    }
    return (lower + upper) / 2;
}

void response_and_noise() {
    for (const auto cutoff : {1200., 1520.}) {
        const auto g = make_ssb_frontend_geometry(48000, 1500, cutoff);
        const auto sum = std::accumulate(g.taps.begin(), g.taps.end(), 0.);
        const auto squared = std::inner_product(g.taps.begin(), g.taps.end(), g.taps.begin(), 0.);
        const auto width6 = 2 * crossing(g, -6), width40 = 2 * crossing(g, -40);
        const auto enbw = g.input_rate * squared / (sum * sum);
        check(std::abs(width6 - 2 * cutoff) < 2 && width40 > width6 &&
            width40 < 2 * cutoff + 600 && enbw > 2 * cutoff - 200 && enbw < 2 * cutoff,
            "Fast SSB measured Blackman response differs from its declared width and ENBW");

        // Apply each real input basis vector to the actual frontend. For unit
        // independent real noise these column inner products give the exact
        // output covariance, without a random confidence interval or Python DSP.
        constexpr std::size_t lag = 17;
        std::vector<float> basis(g.taps.size() + lag * g.decimation);
        double variance = 0;
        Complex covariance{};
        for (std::size_t n = 0; n < basis.size(); ++n) {
            basis[n] = 1;
            Complex first{}, last{};
            std::size_t position = 0;
            SsbFrontend frontend(g, [&](const SsbFrontendOutput& block) {
                for (const auto sample : block.samples) {
                    if (position == 0) first = sample;
                    if (position == lag) last = sample;
                    ++position;
                }
            });
            frontend.push(basis);
            check(position == lag + 1, "Fast SSB noise basis omitted a covariance row");
            variance += std::norm(first);
            covariance += first * std::conj(last);
            basis[n] = 0;
        }
        double expected_covariance = 0;
        const auto shift = lag * g.decimation;
        for (std::size_t i = shift; i < g.taps.size(); ++i)
            expected_covariance += 4 * g.taps[i] * g.taps[i - shift];
        check(std::abs(variance - 4 * squared) < 2e-13 &&
            std::abs(covariance - expected_covariance) < 2e-13,
            "Fast SSB actual noise power/correlation disagrees with the FIR spectral density");
        std::cout << std::fixed << std::setprecision(6) << "SSB FIR Fs=" << g.input_rate
            << " cutoff=" << cutoff << " -6dB_width=" << width6 << "Hz -40dB_width=" << width40
            << "Hz two_sided_ENBW=" << enbw << "Hz radius=" << g.radius()
            << " tail_seconds=" << double(g.radius()) / g.input_rate
            << " real_unit_noise_complex_variance=" << variance << '\n';
    }
}

void cancellation() {
    const auto g = make_ssb_frontend_geometry(48000);
    constexpr std::array<std::size_t, 1> chunks{10000};
    std::vector<float> input(6017);
    for (std::size_t n = 0; n < input.size(); ++n) input[n] = float(phase(n, 1731.25, g.input_rate).real());
    std::vector<Complex> output;
    std::stop_source stop;
    SsbFrontend frontend(g, [&](const SsbFrontendOutput& block) {
        output.insert(output.end(), block.samples.begin(), block.samples.end());
        stop.request_stop();
    }, 1);
    std::stop_source stopped;
    stopped.request_stop();
    check(frontend.push(input, stopped.get_token()) == 0 && frontend.input_samples() == 0,
        "Fast SSB pre-cancelled push consumed input");
    const auto consumed = frontend.push(input, stop.get_token());
    check(consumed >= g.taps.size() && consumed < g.taps.size() + 256 && consumed < input.size(),
        "Fast SSB cancellation did not stop at its bounded checkpoint");
    check(frontend.push(std::span(input).subspan(consumed)) == input.size() - consumed,
        "Fast SSB could not resume its unconsumed input after cancellation");
    frontend.finish();
    check(output == collect(g, input, chunks), "Fast SSB cancellation duplicated or lost FIR/oscillator state");
}
} // namespace

int main() {
    try {
        geometry_and_continuity();
        impulse_and_finite_tail();
        passband_and_alias_rejection();
        response_and_noise();
        cancellation();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
