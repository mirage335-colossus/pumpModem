#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <span>

namespace datapump::background_noise {

enum class Mode { Off, EffectiveBits, ObscuredBits, SuppressionDb, RelativeWatts };

struct Config {
    Mode mode = Mode::Off;
    double value = 0;
    double full_scale = 1;
    double reference_mean_square = 0;
    double reference_watts = 0;
};

struct Level {
    bool enabled = false;
    double variance = 0; // E[n^2], normalized to real DAC full scale.
    double peak = 0;     // Continuous-sample bound; quantized output can add one code.
};

Level resolve(const Config& config);

// One instance is one transmission. Enabled instances obtain a new private
// AES-256-CTR key from OpenSSL's OS-seeded private RNG. The key never leaves
// this object. Calls consume a continuous stream; keep the object across
// blocks, bursts, and callbacks belonging to the same transmission.
class Generator {
public:
    explicit Generator(const Config& config);
    ~Generator();
    Generator(Generator&&) noexcept;
    Generator& operator=(Generator&&) noexcept;
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    double real();
    // Energy-preserving analytic equivalent: E[|n|^2] equals the real-DAC
    // variance; real and imaginary components each carry half that variance.
    std::complex<double> complex();
    // Quantization-aware S16_LE additive noise. Its integer distribution is a
    // mixture of neighboring symmetric uniforms with exactly the requested
    // second moment, including floors smaller than one output code.
    std::int32_t s16_noise();
    // Integer-code dither with a requested physical step and variance. Useful
    // after a simulated converter has rounded away sub-LSB floating noise.
    // A fresh cryptographic draw is consumed each call.
    std::int64_t quantized_noise(double step, double variance);
    std::int16_t mix_s16(std::int16_t clean);
    double variance() const noexcept;
    double peak() const noexcept; // Continuous bound; not an integer-code bound.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef DATAPUMP_BACKGROUND_NOISE_TESTING
    explicit Generator(const Config&, std::span<const std::uint8_t> key,
                       std::span<const std::uint8_t> iv);
    friend Generator testing_generator(const Config&, std::span<const std::uint8_t>,
                                       std::span<const std::uint8_t>);
#endif
};

#ifdef DATAPUMP_BACKGROUND_NOISE_TESTING
// Deterministic injection exists only as an explicitly named test seam; the
// production constructor and CLI have no key, seed, or IV input.
Generator testing_generator(const Config&, std::span<const std::uint8_t> key,
                            std::span<const std::uint8_t> iv);
void testing_aes_ctr_bytes(std::span<const std::uint8_t> key,
                           std::span<const std::uint8_t> iv,
                           std::span<std::uint8_t> output);
#endif

} // namespace datapump::background_noise
