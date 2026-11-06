#include "datapump/background_noise.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace datapump::background_noise;
namespace {
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
template<class F> void rejects(F f, const char* text) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error(text);
}

void units() {
    Config c{Mode::EffectiveBits, 10};
    const auto ten = resolve(c);
    check(std::abs(ten.variance - (4. / (1024. * 1024. * 12.))) < 1e-15,
          "effective-bit variance uses ideal quantizer step");
    c = {Mode::ObscuredBits, 6};
    check(std::abs(resolve(c).variance - ten.variance) < 1e-15,
          "obscured bits complement effective bits");
    c = {Mode::SuppressionDb, 90};
    check(std::abs(resolve(c).variance - .5e-9) < 1e-20,
          "dB reference is full-scale sine");
    c = {Mode::RelativeWatts, .001, 1, 1, 40};
    check(std::abs(resolve(c).variance - .000025) < 1e-15,
          "relative watts use the declared signal RMS reference");
    c.reference_watts = 4;
    check(std::abs(resolve(c).variance - .00025) < 1e-15,
          "relative watts scale inversely with reference output power");
    rejects([] { resolve({Mode::EffectiveBits, 16.1}); }, "out-of-range effective bits accepted");
    rejects([] { resolve({Mode::RelativeWatts, 1}); }, "unreferenced watts accepted");
    rejects([] { Generator g({Mode::SuppressionDb,250}); (void)g.s16_noise(); },
            "unresolvable integer mixture probability accepted");
}

void deterministic_aes() {
    // NIST SP800-38A AES-256 CTR vector, first 16-byte block. The generator
    // exposes no key input except this named testing constructor.
    constexpr std::array<std::uint8_t, 32> key{
        0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
        0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4};
    constexpr std::array<std::uint8_t, 16> iv{
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
    constexpr std::array<std::uint8_t, 16> expected{
        0x0b,0xdf,0x7d,0xf1,0x59,0x17,0x16,0x33,0x5e,0x9a,0x8b,0x15,0xc8,0x60,0xc5,0x02};
    std::array<std::uint8_t,16> actual{};
    testing_aes_ctr_bytes(key, iv, actual);
    check(actual == expected, "AES-256-CTR stream differs from the NIST test vector");
}

void statistics_and_lifecycle() {
    Config c{Mode::SuppressionDb, 30};
    Generator g(c), other(c);
    constexpr std::size_t n = 250000;
    long double sum = 0, sum2 = 0, re2 = 0, im2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = g.real(); sum += x; sum2 += x*x;
        const auto z = g.complex(); re2 += z.real()*z.real(); im2 += z.imag()*z.imag();
    }
    const double mean = static_cast<double>(sum/n);
    const double variance = static_cast<double>(sum2/n) - mean*mean;
    check(std::abs(mean) < std::sqrt(g.variance()/n)*6., "cryptographic real sequence mean is biased");
    check(std::abs(variance/g.variance()-1) < .02, "real noise variance differs by more than 2 percent");
    const double component = static_cast<double>((re2+im2)/n);
    check(std::abs(component/g.variance()-1) < .02, "complex analytic total power is misnormalized");
    Generator first_fresh(c), second_fresh(c);
    bool differs = false;
    for (unsigned i = 0; i < 64; ++i) differs |= first_fresh.real() != second_fresh.real();
    check(differs, "separate transmissions reused an identical temporary key and stream");

    for (const auto& [mode, value] : {std::pair{Mode::EffectiveBits,0.},
                                     std::pair{Mode::EffectiveBits,15.},
                                     std::pair{Mode::EffectiveBits,16.},
                                     std::pair{Mode::EffectiveBits,10.},
                                     std::pair{Mode::SuppressionDb,90.}}) {
        Generator quantized({mode,value});
        constexpr std::size_t m = 500000;
        long double squares = 0;
        for (std::size_t i = 0; i < m; ++i) {
            const auto x = quantized.s16_noise();
            squares += static_cast<long double>(x)*x;
        }
        const double measured = static_cast<double>(squares/m)/(32767.*32767.);
        const double target = quantized.variance();
        check(std::abs(measured/target-1) < .035, "sub-LSB S16 noise variance is outside statistical tolerance");
    }
    Generator quant_off({});
    check(quant_off.quantized_noise(1., 2.) == 0,
          "quantized-noise Off bypass consumed nonexistent AES state");
    Generator clipping({Mode::EffectiveBits, 10});
    bool clip_seen = false;
    for (unsigned i = 0; i < 1000 && !clip_seen; ++i) {
        try { (void)clipping.mix_s16(32767); }
        catch (const std::invalid_argument&) { clip_seen = true; }
    }
    check(clip_seen, "S16 mixing failed to detect a sampled clipping event");
}

void chunk_identity_and_off() {
    Config off{};
    Generator g(off);
    check(g.real() == 0 && g.complex() == std::complex<double>{} && g.s16_noise() == 0 &&
          g.mix_s16(-32768) == -32768, "off mode is not an exact bypass");
    const std::array<std::uint8_t, 32> key{};
    const std::array<std::uint8_t, 16> iv{};
    Config c{Mode::EffectiveBits, 12};
    auto a = testing_generator(c, key, iv), b = testing_generator(c, key, iv);
    std::vector<double> values(10000);
    for (auto& v : values) v = a.real();
    for (const auto v : values) check(v == b.real(), "stream depends on arbitrary consumer chunking");
}
} // namespace

int main() {
    try { units(); deterministic_aes(); statistics_and_lifecycle(); chunk_identity_and_off(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "background noise checks passed\n";
}
