#include "datapump/background_noise.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace datapump::background_noise {
namespace {
constexpr std::size_t kKeyBytes = 32;
constexpr std::size_t kIvBytes = 16;
constexpr std::size_t kBufferBytes = 4096;
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

void check(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

void put_be64(std::uint8_t* p, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        p[i] = static_cast<std::uint8_t>(value >> (56U - i * 8U));
}

std::array<std::uint8_t, kIvBytes> clock_iv() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    if (ticks < 0) throw std::runtime_error("system clock cannot form a positive CTR IV");
    std::array<std::uint8_t, kIvBytes> iv{};
    put_be64(iv.data(), static_cast<std::uint64_t>(ticks));
    // Low64 is the per-transmission block counter, initialized to zero.
    return iv;
}

} // namespace

Level resolve(const Config& c) {
    check(std::isfinite(c.value) && std::isfinite(c.full_scale) && c.full_scale > 0,
          "background-noise value and full scale must be finite; full scale must be positive");
    Level out{};
    if (c.mode == Mode::Off) return out;
    out.enabled = true;
    switch (c.mode) {
    case Mode::Off: break;
    case Mode::EffectiveBits: {
        check(c.value >= 0 && c.value <= 16 && std::floor(c.value) == c.value,
              "effective DAC bits must be an integer from 0 through 16");
        const double step = std::ldexp(2 * c.full_scale, -static_cast<int>(c.value));
        out.variance = step * step / 12;
        break;
    }
    case Mode::ObscuredBits: {
        check(c.value >= 0 && c.value <= 16 && std::floor(c.value) == c.value,
              "obscured DAC bits must be an integer from 0 through 16");
        const auto bits = static_cast<int>(16 - c.value);
        const double step = std::ldexp(2 * c.full_scale, -bits);
        out.variance = step * step / 12;
        break;
    }
    case Mode::SuppressionDb:
        check(c.value >= 0 && c.value <= 300,
              "noise suppression must be between 0 and 300 dB below a full-scale sine");
        out.variance = .5 * c.full_scale * c.full_scale * std::pow(10., -c.value / 10.);
        break;
    case Mode::RelativeWatts:
        check(c.value >= 0 && std::isfinite(c.reference_mean_square) &&
                  c.reference_mean_square > 0 && std::isfinite(c.reference_watts) &&
                  c.reference_watts > 0,
              "relative-noise watts requires positive finite signal RMS power and reference watts");
        out.variance = c.reference_mean_square * c.value / c.reference_watts;
        break;
    }
    check(std::isfinite(out.variance) && out.variance >= 0,
          "resolved background-noise variance is invalid");
    out.peak = std::sqrt(3 * out.variance);
    check(std::isfinite(out.peak), "resolved background-noise peak is invalid");
    return out;
}

struct Generator::Impl {
    Config config;
    Level level;
    std::array<std::uint8_t, kKeyBytes> key{};
    std::array<std::uint8_t, kIvBytes> iv{};
    std::array<std::uint8_t, kBufferBytes> buffer{};
    CipherContext context{nullptr, EVP_CIPHER_CTX_free};
    std::size_t cursor = kBufferBytes;
    std::uint64_t blocks = 0;

    explicit Impl(const Config& c, bool private_random,
                  std::span<const std::uint8_t> test_key = {},
                  std::span<const std::uint8_t> test_iv = {}) : config(c), level(resolve(c)) {
        if (!level.enabled || level.variance == 0) return;
        if (config.full_scale != 1.) {
            // Arbitrary full scale is supported for floating samples, but a
            // fixed S16 conversion has a single canonical [-1,1] convention.
            // Keep the selected scale for real()/complex(); S16 is rejected
            // below when the requested full-scale units do not match.
        }
        if (private_random) {
            if (RAND_priv_bytes(key.data(), static_cast<int>(key.size())) != 1) {
                OPENSSL_cleanse(key.data(), key.size());
                throw std::runtime_error("OS-backed private cryptographic key generation failed");
            }
            try { iv = clock_iv(); }
            catch (...) {
                OPENSSL_cleanse(key.data(), key.size());
                throw;
            }
        } else {
            if (test_key.size() != key.size() || test_iv.size() != iv.size())
                throw std::invalid_argument("test AES key or IV has the wrong size");
            std::copy(test_key.begin(), test_key.end(), key.begin());
            std::copy(test_iv.begin(), test_iv.end(), iv.begin());
        }
        context.reset(EVP_CIPHER_CTX_new());
        if (!context || EVP_EncryptInit_ex(context.get(), EVP_aes_256_ctr(), nullptr,
                                           key.data(), iv.data()) != 1) {
            OPENSSL_cleanse(key.data(), key.size());
            OPENSSL_cleanse(iv.data(), iv.size());
            throw std::runtime_error("OpenSSL AES-256-CTR initialization failed");
        }
    }

    ~Impl() {
        if (context) EVP_CIPHER_CTX_reset(context.get());
        OPENSSL_cleanse(key.data(), key.size());
        OPENSSL_cleanse(iv.data(), iv.size());
        OPENSSL_cleanse(buffer.data(), buffer.size());
    }

    void refill() {
        if (!context) return;
        if (blocks > std::numeric_limits<std::uint64_t>::max() - kBufferBytes / 16)
            throw std::runtime_error("AES-CTR per-transmission counter exhausted");
        std::array<std::uint8_t, kBufferBytes> zeros{};
        int produced = 0;
        if (EVP_EncryptUpdate(context.get(), buffer.data(), &produced, zeros.data(),
                              static_cast<int>(zeros.size())) != 1 ||
            produced != static_cast<int>(buffer.size())) {
            OPENSSL_cleanse(buffer.data(), buffer.size());
            throw std::runtime_error("OpenSSL AES-256-CTR generation failed");
        }
        blocks += kBufferBytes / 16;
        cursor = 0;
    }

    std::uint8_t byte() {
        if (cursor == buffer.size()) refill();
        return buffer[cursor++];
    }
    std::uint64_t word64() {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value = (value << 8) | byte();
        return value;
    }
    double uniform01() { return static_cast<double>(word64() >> 11) * 0x1.0p-53; }
    std::uint32_t bounded(std::uint32_t bound) {
        if (bound == 0) throw std::runtime_error("AES bounded draw has empty range");
        const auto threshold = static_cast<std::uint32_t>(-bound) % bound;
        for (;;) {
            std::uint32_t x = static_cast<std::uint32_t>(byte()) << 24;
            x |= static_cast<std::uint32_t>(byte()) << 16;
            x |= static_cast<std::uint32_t>(byte()) << 8;
            x |= byte();
            if (x >= threshold) return x % bound;
        }
    }
    std::int64_t discrete_uniform(double code_variance) {
        if (!std::isfinite(code_variance) || code_variance < 0 || code_variance > 0x1.0p62)
            throw std::invalid_argument("quantized noise variance is outside supported range");
        // A 53-bit Bernoulli draw must resolve the mixture probability. This
        // floor keeps its probability rounding error below one part per million.
        if (code_variance > 0 && code_variance < 0x1.0p-32)
            throw std::invalid_argument("quantized noise variance is below supported probability precision");
        auto m = static_cast<std::int64_t>(std::sqrt(3 * code_variance + .25) - .5);
        m = std::max<std::int64_t>(0, m);
        const double lower = static_cast<double>(m) * static_cast<double>(m + 1) / 3;
        const double upper = static_cast<double>(m + 1) * static_cast<double>(m + 2) / 3;
        const double mix = upper == lower ? 0 : std::clamp((code_variance - lower) / (upper - lower), 0., 1.);
        if (uniform01() < mix) ++m;
        if (m == 0) return 0;
        // General range mapping is unbiased. Guard the 32-bit rejection draw
        // because it keeps the hot path compact and the range exactly bounded.
        if (m > (static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) - 1) / 2)
            throw std::invalid_argument("quantized noise peak exceeds supported integer range");
        const auto draw = static_cast<std::int64_t>(bounded(static_cast<std::uint32_t>(2 * m + 1)));
        return draw - m;
    }
};

Generator::Generator(const Config& config) : impl_(std::make_unique<Impl>(config, true)) {}
#ifdef DATAPUMP_BACKGROUND_NOISE_TESTING
Generator::Generator(const Config& config, std::span<const std::uint8_t> key,
                     std::span<const std::uint8_t> iv)
    : impl_(std::make_unique<Impl>(config, false, key, iv)) {}
#endif
Generator::~Generator() = default;
Generator::Generator(Generator&&) noexcept = default;
Generator& Generator::operator=(Generator&&) noexcept = default;

double Generator::real() {
    if (!impl_ || !impl_->level.enabled || impl_->level.variance == 0) return 0;
    return std::sqrt(12 * impl_->level.variance) * (impl_->uniform01() - .5);
}
std::complex<double> Generator::complex() {
    if (!impl_ || !impl_->level.enabled || impl_->level.variance == 0) return {};
    const double scale = std::sqrt(6 * impl_->level.variance);
    return {scale * (impl_->uniform01() - .5), scale * (impl_->uniform01() - .5)};
}
std::int32_t Generator::s16_noise() {
    if (!impl_ || !impl_->level.enabled || impl_->level.variance == 0) return 0;
    if (impl_->config.full_scale != 1.)
        throw std::invalid_argument("S16 noise requires full_scale=1");
    return static_cast<std::int32_t>(quantized_noise(1. / 32767., impl_->level.variance));
}
std::int64_t Generator::quantized_noise(double step, double variance) {
    if (!std::isfinite(step) || step <= 0 || !std::isfinite(variance) || variance < 0)
        throw std::invalid_argument("quantized noise step and variance must be finite; step positive and variance nonnegative");
    if (!impl_ || !impl_->level.enabled || !impl_->context || variance == 0) return 0;
    return impl_->discrete_uniform(variance / (step * step));
}
std::int16_t Generator::mix_s16(std::int16_t clean) {
    if (!impl_ || !impl_->level.enabled || impl_->level.variance == 0) return clean;
    const double code_variance = impl_->level.variance * 32767. * 32767.;
    if (!std::isfinite(code_variance) || code_variance > 0x1.0p62)
        throw std::invalid_argument("background-noise S16 support exceeds supported range");
    const auto floor_m = static_cast<std::int64_t>(std::sqrt(3 * code_variance + .25) - .5);
    const double lower = static_cast<double>(floor_m) * static_cast<double>(floor_m + 1) / 3;
    const double upper = static_cast<double>(floor_m + 1) * static_cast<double>(floor_m + 2) / 3;
    const double mixture = upper == lower ? 0 : (code_variance - lower) / (upper - lower);
    const auto support = floor_m + (mixture > 0 ? 1 : 0);
    if (static_cast<std::int64_t>(clean) - support < -32768 ||
        static_cast<std::int64_t>(clean) + support > 32767)
        throw std::invalid_argument("background noise could clip S16 output; provide fixed source headroom");
    const auto noise = s16_noise();
    const auto mixed = static_cast<std::int32_t>(clean) + noise;
    if (mixed < -32768 || mixed > 32767)
        throw std::invalid_argument("background noise would clip S16 output; provide fixed source headroom");
    return static_cast<std::int16_t>(mixed);
}
double Generator::variance() const noexcept { return impl_ ? impl_->level.variance : 0; }
double Generator::peak() const noexcept {
    if (!impl_) return 0;
    return impl_->level.peak;
}

#ifdef DATAPUMP_BACKGROUND_NOISE_TESTING
Generator testing_generator(const Config& config, std::span<const std::uint8_t> key,
                            std::span<const std::uint8_t> iv) {
    return Generator(config, key, iv);
}

void testing_aes_ctr_bytes(std::span<const std::uint8_t> key,
                           std::span<const std::uint8_t> iv,
                           std::span<std::uint8_t> output) {
    if (key.size() != kKeyBytes || iv.size() != kIvBytes)
        throw std::invalid_argument("test AES key or IV has the wrong size");
    if (output.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("test AES output is too large");
    std::array<std::uint8_t,kKeyBytes> local_key{};
    std::copy(key.begin(), key.end(), local_key.begin());
    CipherContext ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr,
                                  local_key.data(), iv.data()) != 1) {
        OPENSSL_cleanse(local_key.data(), local_key.size());
        throw std::runtime_error("test OpenSSL AES initialization failed");
    }
    std::vector<std::uint8_t> zeros(output.size());
    int produced = 0;
    const bool ok = EVP_EncryptUpdate(ctx.get(), output.data(), &produced,
                                     zeros.data(), static_cast<int>(zeros.size())) == 1 &&
                    produced == static_cast<int>(output.size());
    OPENSSL_cleanse(local_key.data(), local_key.size());
    OPENSSL_cleanse(zeros.data(), zeros.size());
    if (!ok) throw std::runtime_error("test OpenSSL AES generation failed");
}
#endif

} // namespace datapump::background_noise
