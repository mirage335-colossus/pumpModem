#pragma once

#include "datapump/background_noise.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace datapump::triad::cli {

// Shared, default-off command-line controls for isolated sampled transmitters.
// The caller supplies its fixed signal reference after resolving DATA/Helper
// power; this parser never infers signal power from a particular waveform row.
class BackgroundNoiseOptions {
public:
    bool consume(std::string_view option, std::string_view value) {
        using Mode = background_noise::Mode;
        Mode mode{};
        if (option == "--background-noise-bits") mode = Mode::EffectiveBits;
        else if (option == "--background-noise-obscured-bits") mode = Mode::ObscuredBits;
        else if (option == "--background-noise-db") mode = Mode::SuppressionDb;
        else if (option == "--background-noise-watts") mode = Mode::RelativeWatts;
        else if (option == "--background-noise-reference-watts") {
            reference_watts = parse(value, "background noise reference watts");
            return true;
        } else if (option == "--background-noise-reference-rms") {
            reference_rms = parse(value, "background noise reference RMS");
            if (*reference_rms <= 0)
                throw std::invalid_argument("background noise reference RMS must be positive");
            return true;
        } else return false;
        if (config.mode != Mode::Off)
            throw std::invalid_argument("select only one background-noise level option");
        config.mode = mode;
        config.value = parse(value, "background-noise level");
        return true;
    }

    background_noise::Config resolved(double full_scale = 1,
                                      double automatic_reference_mean_square = 0,
                                      double automatic_reference_watts = 0) const {
        auto result = config;
        result.full_scale = full_scale;
        if (result.mode == background_noise::Mode::RelativeWatts) {
            result.reference_mean_square = reference_rms ? *reference_rms * *reference_rms :
                automatic_reference_mean_square;
            result.reference_watts = reference_watts ? *reference_watts : automatic_reference_watts;
        }
        return result;
    }

    bool enabled() const { return config.mode != background_noise::Mode::Off; }
    double value() const { return config.value; }
    std::string_view mode_name() const {
        using Mode = background_noise::Mode;
        switch (config.mode) {
        case Mode::Off: return "off";
        case Mode::EffectiveBits: return "effective-bits";
        case Mode::ObscuredBits: return "obscured-bits";
        case Mode::SuppressionDb: return "suppression-db-below-full-scale-sine";
        case Mode::RelativeWatts: return "watts-relative-to-strongest-signal";
        }
        return "unknown";
    }

private:
    static double parse(std::string_view text, const char* label) {
        std::size_t used = 0;
        const auto result = std::stod(std::string(text), &used);
        if (used != text.size() || !std::isfinite(result))
            throw std::invalid_argument(std::string("invalid ") + label);
        return result;
    }

    background_noise::Config config{};
    std::optional<double> reference_watts, reference_rms;
};

} // namespace datapump::triad::cli
