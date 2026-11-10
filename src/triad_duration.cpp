#include "datapump/triad_duration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace datapump::triad::v3 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
void positive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0) throw Error(name);
}
void nonnegative(double value, const char* name) {
    if (!std::isfinite(value) || value < 0) throw Error(name);
}
struct Quadrature {
    std::array<long double, 64> nodes{}, weights{};
    Quadrature() {
        // A fixed 64-node rule matches the canonical provisional model. Its
        // construction is bounded and independent of samples or bit duration.
        constexpr unsigned n = 64;
        for (unsigned i = 0; i < n / 2; ++i) {
            long double root = std::cos(std::numbers::pi_v<long double> *
                (static_cast<long double>(i) + .75L) / (n + .5L));
            long double derivative = 0;
            bool converged = false;
            for (unsigned step = 0; step < 32; ++step) {
                long double previous = 0, value = 1;
                for (unsigned degree = 1; degree <= n; ++degree) {
                    const auto next = ((2 * degree - 1) * root * value -
                        (degree - 1) * previous) / degree;
                    previous = value; value = next;
                }
                derivative = n * (root * value - previous) / (root * root - 1);
                const auto correction = value / derivative;
                root -= correction;
                if (std::abs(correction) <= 4 * std::numeric_limits<long double>::epsilon()) {
                    converged = true; break;
                }
            }
            require(converged, "duration quadrature failed to converge");
            const auto weight = 2 / ((1 - root * root) * derivative * derivative);
            nodes[i] = -root; nodes[n - 1 - i] = root;
            weights[i] = weights[n - 1 - i] = weight;
        }
    }
};
double x_minus_log1p(double value) {
    if (value < 1e-4)
        return value * value * (.5 - value / 3 + value * value / 4 - value * value * value / 5);
    return value - std::log1p(value);
}
} // namespace

double duration_effective_modes(double seconds, double width_hz, double diffusion, unsigned rays) {
    positive(seconds, "duration observation span must be finite and positive");
    nonnegative(width_hz, "duration covariance width must be finite and nonnegative");
    nonnegative(diffusion, "duration phase diffusion must be finite and nonnegative");
    require(rays == 1 || rays == 2, "duration model requires one or two rays");
    const long double time = seconds;
    const long double a = std::pow(std::numbers::pi_v<long double> * width_hz, 2);
    const long double b = static_cast<long double>(diffusion) * diffusion;
    long double modes;
    if (a == 0) {
        const auto z = b * time;
        if (z == 0) return rays;
        // Integral in closed form, with its cancellation-free static limit.
        const auto normalized = z < 1e-3L ?
            .5L - z / 6 + z * z / 24 - z * z * z / 120 + std::pow(z, 4) / 720 :
            (z + std::expm1(-z)) / (z * z);
        modes = rays / (2 * normalized);
    } else {
        static const Quadrature quadrature;
        const auto rate = std::max({std::sqrt(a), b, 1 / time});
        const auto end = std::min(time * rate, 40.L);
        long double integral = 0;
        for (unsigned i = 0; i < quadrature.nodes.size(); ++i) {
            const auto x = (quadrature.nodes[i] + 1) * end / 2;
            integral += quadrature.weights[i] * (1 - x / (rate * time)) *
                std::exp(-(a / rate / rate) * x * x - (b / rate) * x);
        }
        integral *= end / 2;
        require(std::isfinite(integral) && integral > 0, "duration covariance integral is invalid");
        modes = rays * time * rate / (2 * integral);
    }
    require(std::isfinite(modes) && modes <= std::numeric_limits<double>::max(),
        "duration covariance mode count overflow");
    return std::max(static_cast<double>(rays), static_cast<double>(modes));
}

DurationPrediction duration_prediction(double energy, double modes, double threshold, bool stochastic) {
    positive(energy, "duration energy must be finite and positive");
    positive(modes, "duration mode count must be finite and positive");
    require(std::isfinite(threshold), "duration likelihood threshold must be finite");
    DurationPrediction prediction;
    prediction.energy_n0 = energy;
    prediction.effective_modes = modes;
    prediction.true_atom_log_lr_threshold = threshold;
    const auto lambda = energy / modes;
    require(std::isfinite(lambda), "duration energy per mode overflow");
    if (stochastic) {
        prediction.mean_true_atom_log_lr = modes * x_minus_log1p(lambda);
        const auto delta = (prediction.mean_true_atom_log_lr - threshold) / energy;
        if (delta <= 0) prediction.log_miss_bound = 0;
        else if (delta >= 1) prediction.log_miss_bound = -std::numeric_limits<double>::infinity();
        else {
            const auto loss = delta < 1e-4 ? -delta * delta *
                (.5 + delta / 3 + delta * delta / 4 + delta * delta * delta / 5) :
                delta + std::log1p(-delta);
            prediction.log_miss_bound = modes * loss;
        }
    } else {
        const auto scale = lambda / (1 + lambda);
        const auto x = (threshold + modes * std::log1p(lambda)) / scale;
        prediction.mean_true_atom_log_lr = scale * (modes + energy) - modes * std::log1p(lambda);
        if (x >= modes + energy) prediction.log_miss_bound = 0;
        else if (x <= 0) prediction.log_miss_bound = -std::numeric_limits<double>::infinity();
        else {
            // hypot avoids overflowing the discriminant of the positive root.
            const auto root = std::hypot(modes, 2 * std::sqrt(energy) * std::sqrt(x));
            const auto v = 2 * x / (modes + root);
            prediction.log_miss_bound = std::min(0., (1 / v - 1) * x +
                modes * std::log(v) - energy * (1 - v));
        }
    }
    require(!std::isnan(prediction.log_miss_bound) && prediction.log_miss_bound <= 0,
        "duration tail bound is invalid");
    prediction.detection_lower_bound = -std::expm1(prediction.log_miss_bound);
    return prediction;
}

const char* duration_status_name(DurationStatus status) {
    switch (status) {
    case DurationStatus::provisional_plan: return "provisional_model_plan";
    case DurationStatus::manual_override: return "manual_duration_override";
    case DurationStatus::duration_limit: return "duration_limit";
    case DurationStatus::evaluation_limit: return "evaluation_limit";
    case DurationStatus::geometry_limit: return "geometry_limit";
    }
    return "unknown";
}

DurationPlan plan_duration(const ShapingPlan& base, const ReceiverConfig& receiver,
                          const DurationOptions& options) {
    require(std::isfinite(options.target_cn0_db_hz) && options.target_cn0_db_hz >= -120 &&
        options.target_cn0_db_hz <= 200, "target C/N0 must be finite in [-120,200] dB-Hz");
    require(std::isfinite(options.correct_probability) && options.correct_probability > 0 &&
        options.correct_probability < 1, "duration reception probability must be in (0,1)");
    require(options.planning_bits > 0 && options.residual_state_count > 0,
        "duration planning bits and residual states must be positive");
    require(std::isfinite(options.arrival_lower_seconds) && std::isfinite(options.arrival_upper_seconds) &&
        options.arrival_lower_seconds <= options.arrival_upper_seconds, "invalid duration arrival prior");
    nonnegative(options.helper_lead_seconds, "invalid duration Helper lead");
    nonnegative(options.pulse_halo_seconds, "invalid duration pulse halo");
    nonnegative(receiver.clock_bound_ppm, "invalid duration clock bound");
    nonnegative(receiver.frequency_bound_hz, "invalid duration frequency bound");
    positive(receiver.sample_rate_hz, "duration input sample rate must be finite and positive");
    positive(options.frequency_step_hz, "duration frequency step must be finite and positive");
    require(options.frequency_step_hz <= .01, "duration frequency step cannot weaken the canonical grid");
    require(std::isfinite(options.outage_fraction) && options.outage_fraction >= 0 && options.outage_fraction < 1 &&
        std::isfinite(options.retained_fraction) && options.retained_fraction > 0 && options.retained_fraction <= 1,
        "duration outage or retained fraction outside supported range");
    positive(options.maximum_active_seconds, "duration maximum active span must be finite and positive");
    require(options.maximum_evaluations >= 4 && options.maximum_evaluations <= 4096,
        "duration planner requires 4..4096 bounded evaluations");
    const auto actual_base = geometry(base.format);
    require(actual_base.tile_seconds == base.geometry.tile_seconds &&
        actual_base.tile_count == base.geometry.tile_count &&
        actual_base.source_samples == base.geometry.source_samples,
        "duration base shaping and geometry disagree");
    const auto tile = actual_base.tile_seconds;
    positive(tile, "duration native tile span must be finite and positive");
    const auto max_tiles_real = std::floor(static_cast<long double>(options.maximum_active_seconds) / tile);
    require(max_tiles_real <= std::numeric_limits<std::uint64_t>::max(), "duration tile horizon overflow");
    const auto maximum_tiles = static_cast<std::uint64_t>(max_tiles_real);
    const auto rho = std::pow(10., options.target_cn0_db_hz / 10);
    DurationPlan result;
    result.per_bit_detection_miss_budget = (1 - options.correct_probability) /
        (2 * static_cast<double>(options.planning_bits));
    positive(result.per_bit_detection_miss_budget, "duration detection allocation underflow");
    const auto log_beta = std::log(result.per_bit_detection_miss_budget);
    std::map<std::uint64_t, DurationPrediction> trials;
    bool exhausted = false, geometry_failed = false;
    const auto evaluate = [&](std::uint64_t count) -> const DurationPrediction* {
        if (const auto prior = trials.find(count); prior != trials.end()) return &prior->second;
        if (trials.size() == options.maximum_evaluations) { exhausted = true; return nullptr; }
        const auto active = count * static_cast<long double>(tile);
        const auto elapsed = (count + static_cast<long double>(count - 1) * base.format.burst_wait_tiles) * tile;
        const auto origin = 30.L + (options.planning_bits - 1) *
            (elapsed + options.helper_lead_seconds + 30 + 2 * options.pulse_halo_seconds);
        require(std::isfinite(active) && std::isfinite(elapsed) && std::isfinite(origin) &&
            origin <= std::numeric_limits<double>::max(), "duration public calendar overflow");
        const auto drift = static_cast<double>(origin * receiver.clock_bound_ppm * 1e-6L);
        auto format = base.format;
        format.bit_seconds = static_cast<double>(active);
        double frequency_step, log_count;
        try {
            frequency_step = std::min(options.frequency_step_hz,
                receiver_coarse_frequency_step(format, receiver.sample_rate_hz));
            log_count = receiver_log_grid_count(options.arrival_lower_seconds - drift,
                options.arrival_upper_seconds + drift, receiver.frequency_bound_hz,
                receiver.clock_bound_ppm, frequency_step);
        } catch (const Error& error) {
            geometry_failed = true;
            result.reason = error.what();
            return nullptr;
        }
        const auto threshold = receiver_operational_threshold() + std::log(2.) + log_count +
            std::log(static_cast<double>(receiver_coherence_count)) +
            std::log(static_cast<double>(options.residual_state_count));
        const auto energy = rho * static_cast<double>(active) * (1 - options.outage_fraction) * options.retained_fraction;
        const unsigned rays = receiver.spread_delay_seconds != 0 || receiver.spread_doppler_hz != 0 ? 2 : 1;
        auto prediction = duration_prediction(energy, duration_effective_modes(static_cast<double>(elapsed),
            options.stochastic_channel ? receiver.random_doppler_width_hz : 0,
            receiver.relative_phase_diffusion_rad_sqrt_s, rays), threshold, options.stochastic_channel);
        prediction.tiles = count;
        prediction.active_seconds = static_cast<double>(active);
        prediction.elapsed_seconds = static_cast<double>(elapsed);
        prediction.log_grid_count = log_count;
        prediction.frequency_step_hz = frequency_step;
        prediction.origin_shift_allowance_seconds = static_cast<double>(origin);
        prediction.meets_detection_allocation = prediction.log_miss_bound <= log_beta;
        return &trials.emplace(count, prediction).first->second;
    };
    if (options.manual_active_seconds) {
        positive(*options.manual_active_seconds, "manual duration must be finite and positive");
        const auto requested = std::max(1.L, std::ceil(static_cast<long double>(*options.manual_active_seconds) / tile - 1e-12L));
        require(requested <= maximum_tiles, "manual duration exceeds configured active limit");
        if (const auto point = evaluate(static_cast<std::uint64_t>(requested))) {
            result.selected = *point;
            result.status = DurationStatus::manual_override;
        }
    } else if (maximum_tiles) {
        std::uint64_t lower = 0, upper = 1;
        const DurationPrediction* point = nullptr;
        while ((point = evaluate(upper)) && !point->meets_detection_allocation) {
            lower = upper;
            if (upper == maximum_tiles) { point = nullptr; break; }
            upper = upper > maximum_tiles / 2 ? maximum_tiles : 2 * upper;
        }
        if (point) {
            while (upper - lower > 1) {
                const auto middle = lower + (upper - lower) / 2;
                point = evaluate(middle);
                if (!point) break;
                if (point->meets_detection_allocation) upper = middle;
                else lower = middle;
            }
            if (!exhausted && !geometry_failed) {
                result.selected = *evaluate(upper);
                result.status = DurationStatus::provisional_plan;
                if (upper > 1) {
                    if (const auto previous = evaluate(upper - 1)) result.previous_legal = *previous;
                }
            }
        }
    }
    if (geometry_failed) {
        result.status = DurationStatus::geometry_limit;
        result.selected.reset(); result.previous_legal.reset();
    } else if (exhausted) {
        result.status = DurationStatus::evaluation_limit;
        result.selected.reset(); result.previous_legal.reset();
        result.reason = "bounded duration-model evaluation budget exhausted";
    } else if (result.selected) {
        try {
            auto format = base.format;
            format.bit_seconds = result.selected->active_seconds;
            auto shaped = plan_shaping(format, options.shaping_options);
            require(shaped.geometry.tile_seconds == tile &&
                shaped.geometry.tile_count == result.selected->tiles,
                "duration shaping changed the selected native tile lattice");
            result.shaping = std::move(shaped);
        } catch (const Error& error) {
            result.status = DurationStatus::geometry_limit;
            result.reason = error.what();
        }
    } else result.reason = "no duration meets the provisional model within the configured active horizon";
    result.evaluated.reserve(trials.size());
    for (const auto& [_, value] : trials) result.evaluated.push_back(value);
    return result;
}

} // namespace datapump::triad::v3
