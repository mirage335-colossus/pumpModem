#include "datapump/audio_output.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::audio {

struct PlaybackOutput::Impl {
    std::uint32_t rate;
    background_noise::Config config;
    background_noise::Level level;
    double gain;
    std::unique_ptr<background_noise::Generator> generator;
    std::uint64_t active_samples = 0;
    long double noise_sum = 0, noise_square_sum = 0;
    double peak = 0;
    AudioOutputSampleFormat format = AudioOutputSampleFormat::S16;

    Impl(std::uint32_t final_rate, const background_noise::Config& value, double output_gain)
        : rate(final_rate), config(value), level(background_noise::resolve(value)), gain(output_gain) {
        if (!rate || rate > 120'000'000)
            throw Error("final audio output rate must be 1..120000000 Hz");
        if (!std::isfinite(gain) || gain < 0.0001 || gain > 1.75)
            throw Error("final audio transmit gain must be between 0.01% and 175%");
        if (config.full_scale != 1.)
            throw Error("full-rate audio background noise requires normalized DAC full scale 1");
        if (level.enabled && config.mode == background_noise::Mode::RelativeWatts &&
            config.reference_mean_square > 1.)
            throw Error("background-noise reference mean-square power exceeds normalized full scale");
    }

    void begin_active() {
        if (!generator && level.enabled && level.variance > 0)
            generator = std::make_unique<background_noise::Generator>(config);
    }
    void accumulate(double perturbation) {
        if (active_samples == std::numeric_limits<std::uint64_t>::max())
            throw Error("background-noise sample count overflow");
        ++active_samples;
        noise_sum += perturbation;
        noise_square_sum += static_cast<long double>(perturbation) * perturbation;
        peak = std::max(peak, std::abs(perturbation));
    }
    BackgroundNoiseReport report() const noexcept {
        BackgroundNoiseReport result;
        result.enabled = level.enabled;
        result.sample_rate_hz = rate;
        result.sample_format = format;
        result.active_samples = active_samples;
        result.expected_variance = level.variance;
        result.peak = peak;
        if (active_samples) {
            const long double count = static_cast<long double>(active_samples);
            const long double mean = noise_sum / count;
            result.measured_mean = static_cast<double>(mean);
            result.measured_variance = static_cast<double>(std::max(0.L, noise_square_sum/count - mean*mean));
        }
        return result;
    }
};

PlaybackOutput::PlaybackOutput(std::uint32_t rate, const background_noise::Config& noise,
                               double transmit_gain)
    : impl_(std::make_unique<Impl>(rate, noise, transmit_gain)) {}
PlaybackOutput::~PlaybackOutput() = default;
PlaybackOutput::PlaybackOutput(PlaybackOutput&&) noexcept = default;
PlaybackOutput& PlaybackOutput::operator=(PlaybackOutput&&) noexcept = default;

bool PlaybackOutput::enabled() const noexcept { return impl_ && impl_->level.enabled; }
double PlaybackOutput::provider_gain() const noexcept {
    return enabled() ? 1.0 : (impl_ ? impl_->gain : 1.0);
}

void PlaybackOutput::pack_s16(std::span<const float> mono,
                              std::span<std::int16_t> interleaved,
                              unsigned channels, ChannelMode mode,
                              std::size_t active_begin) {
    if (!impl_) throw Error("audio output finalizer was moved from");
    if ((channels != 1 && channels != 2) ||
        mono.size() > std::numeric_limits<std::size_t>::max()/channels ||
        interleaved.size() < mono.size()*channels || active_begin > mono.size())
        throw Error("invalid final S16 audio buffer geometry");
    impl_->format = AudioOutputSampleFormat::S16;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const float original = mono[i];
        if (!std::isfinite(original)) throw Error("nonfinite transmit sample");
        std::int16_t sample;
        if (!impl_->level.enabled) {
            // Preserve the historical output expression and channel mapping
            // exactly while the optional feature is off.
            sample = impl_->gain == 1.0
                ? static_cast<std::int16_t>(std::clamp(original,-1.0f,1.0f)*32767)
                : static_cast<std::int16_t>(std::clamp(static_cast<double>(original)*impl_->gain,-1.0,1.0)*32767);
        } else {
            const double scaled = static_cast<double>(original) * impl_->gain;
            if (!std::isfinite(scaled) || std::abs(scaled) > 1.)
                throw Error("transmit sample exceeds normalized DAC range; lower fixed transmit gain");
            // Keep the exact historical quantizer for each gain mode. The
            // enabled noise stage must not perturb payload codes by switching
            // the unity-gain path to double or the non-unity path to float.
            const auto base = impl_->gain == 1.0
                ? static_cast<std::int16_t>(std::clamp(original,-1.0f,1.0f)*32767)
                : static_cast<std::int16_t>(std::clamp(
                    static_cast<double>(original)*impl_->gain,-1.0,1.0)*32767);
            if (i >= active_begin) {
                impl_->begin_active();
                sample = impl_->generator
                    ? impl_->generator->mix_s16(base) : base;
                impl_->accumulate((static_cast<double>(sample)-base)/32767.);
            } else sample = base;
        }
        interleaved[i*channels] = channels == 2 && mode == ChannelMode::right_mono ? 0 : sample;
        if (channels == 2) interleaved[i*channels+1] = mode == ChannelMode::left_mono ? 0 : sample;
    }
}

void PlaybackOutput::mix_s16(std::span<std::int16_t> mono, std::size_t active_begin) {
    if (!impl_) throw Error("audio output finalizer was moved from");
    if (active_begin > mono.size()) throw Error("invalid S16 audio active boundary");
    impl_->format = AudioOutputSampleFormat::S16;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const auto original = mono[i];
        if (!impl_->level.enabled && impl_->gain == 1.0) continue;

        // Mirror the established cached-PCM gain convention. Do not convert
        // through float: that can alter full-range codes even at unity gain.
        const double gained = static_cast<double>(original) * impl_->gain;
        if (impl_->level.enabled &&
            (gained < -32768. || gained > 32767.))
            throw Error("transmit S16 sample exceeds DAC range; lower fixed transmit gain");
        const auto base = impl_->gain == 1.0
            ? original
            : static_cast<std::int16_t>(std::clamp(std::llround(gained),-32768LL,32767LL));
        if (impl_->level.enabled && i >= active_begin) {
            impl_->begin_active();
            const auto sample = impl_->generator ? impl_->generator->mix_s16(base) : base;
            mono[i] = sample;
            impl_->accumulate((static_cast<double>(sample)-base)/32767.);
        } else {
            mono[i] = base;
        }
    }
}

void PlaybackOutput::mix_float(std::span<float> mono, std::size_t active_begin) {
    if (!impl_) throw Error("audio output finalizer was moved from");
    if (active_begin > mono.size()) throw Error("invalid float audio active boundary");
    impl_->format = AudioOutputSampleFormat::Float32;
    if (!impl_->level.enabled) return;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const double original = mono[i];
        if (!std::isfinite(original)) throw Error("nonfinite transmit sample");
        const double scaled = original * impl_->gain;
        const bool active = i >= active_begin;
        if (!std::isfinite(scaled) || std::abs(scaled) > 1. ||
            (active && std::abs(scaled) + impl_->level.peak > 1.))
            throw Error("transmit sample and background noise exceed normalized output headroom; lower fixed transmit gain");
        if (active) impl_->begin_active();
        const double added = active && impl_->generator ? impl_->generator->real() : 0.;
        const float emitted = static_cast<float>(scaled + added);
        mono[i] = emitted;
        if (active)
            impl_->accumulate(static_cast<double>(emitted)-static_cast<float>(scaled));
    }
}

BackgroundNoiseReport PlaybackOutput::report() const noexcept {
    return impl_ ? impl_->report() : BackgroundNoiseReport{};
}

} // namespace datapump::audio
