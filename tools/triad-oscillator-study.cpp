#include "triad_fixture_key.hpp"
#include "datapump/triad_modem_v3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr double tau = 2 * std::numbers::pi;

struct Options {
    double seconds = 7200;
    double rf_hz = 30e6;
    double strength = -1;
    std::uint64_t seed = 0x32564f5343494c4cULL;
    std::string_view model = "all";
    bool check = true;
    bool strength_set = false;
};

double number(const char* s, const char* name) {
    std::size_t n = 0;
    const auto v = std::stod(s, &n);
    if (s[n] || !std::isfinite(v)) throw std::invalid_argument(std::string("invalid ") + name);
    return v;
}

std::uint64_t integer(const char* s, const char* name) {
    if (!s[0] || s[0] == '-' || s[0] == '+')
        throw std::invalid_argument(std::string("invalid ") + name);
    std::size_t n = 0;
    const auto v = std::stoull(s, &n);
    if (s[n]) throw std::invalid_argument(std::string("invalid ") + name);
    return v;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "triad-oscillator-study [--seconds 7200] [--rf-hz 30000000] "
                         "[--model all|white-fm|random-walk-fm|white-pm|flicker-fm] "
                         "[--strength VALUE] [--seed UINT64] [--no-check]\n"
                         "Streams the actual v3 shaped waveform through each oscillator model.\n"
                         "Strength defaults: white-FM 0.002 Hz*sqrt(s), random-walk-FM "
                         "2e-5 Hz/sqrt(s), white-PM 0.02 rad, flicker-FM 1e-5 Hz RMS.\n";
            std::exit(0);
        }
        if (arg == "--no-check") {
            o.check = false;
            continue;
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing option value");
        const char* value = argv[++i];
        if (arg == "--seconds") o.seconds = number(value, "duration");
        else if (arg == "--rf-hz") o.rf_hz = number(value, "RF carrier");
        else if (arg == "--strength") {
            o.strength = number(value, "strength");
            o.strength_set = true;
        }
        else if (arg == "--seed") o.seed = integer(value, "seed");
        else if (arg == "--model") o.model = value;
        else throw std::invalid_argument(std::string("unknown option: ") + std::string(arg));
    }
    if (!(o.seconds >= 600 && o.seconds <= 86400) || !(o.rf_hz > 0) ||
        !std::isfinite(o.seconds) || !std::isfinite(o.rf_hz))
        throw std::invalid_argument("duration must be 600..86400 s and RF must be positive");
    if (o.strength_set && o.strength < 0)
        throw std::invalid_argument("strength must be nonnegative");
    if (o.model != "all" && o.model != "white-fm" && o.model != "random-walk-fm" &&
        o.model != "white-pm" && o.model != "flicker-fm")
        throw std::invalid_argument("unknown oscillator model");
    return o;
}

Crypto fixture_key() {
    Bytes bytes(32);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(37 + 19 * i);
    return datapump::triad::fixture::key(bytes);
}

struct Model {
    const char* name;
    double strength;
    OscillatorSpectrum spectrum;
    bool flicker = false;
};

std::vector<Model> models(const Options& o) {
    const std::vector<std::string_view> requested = o.model == "all"
        ? std::vector<std::string_view>{"white-fm", "random-walk-fm", "white-pm", "flicker-fm"}
        : std::vector<std::string_view>{o.model};
    std::vector<Model> out;
    for (const auto name : requested) {
        Model m{};
        if (name == "white-fm") {
            m.name = "white_fm";
            m.strength = o.strength >= 0 ? o.strength : .002;
            m.spectrum.white_fm_hz_sqrt_seconds = m.strength;
        } else if (name == "random-walk-fm") {
            m.name = "random_walk_fm";
            m.strength = o.strength >= 0 ? o.strength : 2e-5;
            m.spectrum.random_walk_fm_hz_per_sqrt_second = m.strength;
        } else if (name == "white-pm") {
            m.name = "white_pm";
            m.strength = o.strength >= 0 ? o.strength : .02;
            m.spectrum.white_pm_rms_radians = m.strength;
        } else {
            m.name = "finite_band_ou_flicker_fm";
            m.strength = o.strength >= 0 ? o.strength : 1e-5;
            m.spectrum.flicker_fm_rms_hz = m.strength;
            m.spectrum.flicker_min_tau_seconds = .1;
            m.spectrum.flicker_max_tau_seconds = 3600;
            m.spectrum.flicker_bands = 12;
            m.flicker = true;
        }
        out.push_back(m);
    }
    return out;
}

struct Allan {
    double overlapping = 0, nonoverlapping = 0;
    std::uint64_t overlapping_pairs = 0, nonoverlapping_pairs = 0;
};

Allan allan(const std::vector<double>& phase, const std::vector<std::uint8_t>& valid,
            std::uint64_t m, double rf) {
    long double over_sum = 0, non_sum = 0;
    Allan result;
    auto add = [&](std::uint64_t i, long double& sum, std::uint64_t& count) {
        if (!valid[static_cast<std::size_t>(i)] ||
            !valid[static_cast<std::size_t>(i + m)] ||
            !valid[static_cast<std::size_t>(i + 2 * m)]) return;
        const auto p0 = phase[static_cast<std::size_t>(i)];
        const auto p1 = phase[static_cast<std::size_t>(i + m)];
        const auto p2 = phase[static_cast<std::size_t>(i + 2 * m)];
        const auto second_difference = p2 - 2 * p1 + p0;
        const long double ydiff = static_cast<long double>(second_difference) /
            (tau * rf * static_cast<long double>(m));
        sum += .5L * ydiff * ydiff;
        ++count;
    };
    for (std::uint64_t i = 0; i + 2 * m < phase.size(); ++i)
        add(i, over_sum, result.overlapping_pairs);
    for (std::uint64_t i = 0; i + 2 * m < phase.size(); i += 2 * m)
        add(i, non_sum, result.nonoverlapping_pairs);
    if (result.overlapping_pairs)
        result.overlapping = std::sqrt(static_cast<double>(over_sum / result.overlapping_pairs));
    if (result.nonoverlapping_pairs)
        result.nonoverlapping = std::sqrt(static_cast<double>(non_sum / result.nonoverlapping_pairs));
    return result;
}

struct RunResult {
    std::vector<double> phase;
    std::vector<std::uint8_t> valid;
    std::uint64_t source_samples = 0, total_samples = 0, usable = 0;
    std::size_t channel_workspace = 0, source_workspace = 0;
};

RunResult simulate(const Options& o, const Model& model, std::uint64_t trial) {
    Format f;
    f.inner_rate_hz = 3.6;
    f.added_symbols = 10;
    f.sample_rate = 64;
    f.bit_seconds = o.seconds;
    f.minimum_inner_coefficients = 64;
    f.encrypt = true;
    f.epoch = 1793404800;
    f.symbol_ordinal = trial;
    const auto g = geometry(f);
    const auto key = fixture_key();
    PatternSource tx(f, key, 1), reference(f, key, 1);

    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    cfg.effects.rf_hz = o.rf_hz;
    cfg.effects.seed = o.seed + trial * 0x9e3779b97f4a7c15ULL;
    cfg.effects.signal_enabled = true;
    cfg.effects.noise_enabled = false;
    cfg.tx_spectrum = model.spectrum;
    cfg.rx_spectrum = model.spectrum;
    cfg.complex_noise_variance = 0;
    cfg.maximum_chunk_samples = 4096;
    StreamingChannel channel(tx, cfg);

    const auto seconds = static_cast<std::size_t>(std::ceil(
        static_cast<long double>(g.source_samples) / f.sample_rate));
    RunResult result;
    result.phase.assign(seconds + 1, 0);
    result.valid.assign(seconds + 1, 0);
    result.source_samples = g.source_samples;
    result.total_samples = channel.total_samples();
    result.channel_workspace = channel.report().working_bytes;
    result.source_workspace = tx.working_bytes() + reference.working_bytes();
    std::vector<Complex> block(cfg.maximum_chunk_samples);
    std::vector<std::uint8_t> validity(block.size());
    std::uint64_t index = 0;
    constexpr double minimum_source_magnitude = 1e-8;
    while (index < std::min<std::uint64_t>(channel.total_samples(), g.source_samples)) {
        const auto requested = static_cast<std::size_t>(std::min<std::uint64_t>(
            block.size(), std::min<std::uint64_t>(channel.total_samples(), g.source_samples) - index));
        const auto got = channel.read(std::span(block).first(requested),
                                      std::span(validity).first(requested));
        if (got != requested) throw std::runtime_error("channel ended before source support");
        for (std::size_t j = 0; j < got; ++j) {
            const auto n = index + j;
            if (n % f.sample_rate != 0) continue;
            const auto second = static_cast<std::size_t>(n / f.sample_rate);
            if (second >= result.phase.size() || !validity[j]) continue;
            const auto source = reference.sample_at(static_cast<long double>(n));
            if (std::abs(source) <= minimum_source_magnitude) continue;
            const auto ratio = block[j] / source;
            if (!std::isfinite(ratio.real()) || !std::isfinite(ratio.imag())) continue;
            result.phase[second] = std::arg(ratio);
            result.valid[second] = 1;
            ++result.usable;
        }
        index += got;
    }

    // Remove the known finite source ramps and halo. Unwrap only actual valid
    // observations; missing low-envelope samples remain explicit erasures.
    // Phase is sampled once per second, so discard three seconds at each
    // endpoint (the shaped source's finite ramp/halo region).
    const auto edge = std::size_t{3};
    double previous = 0;
    bool have_previous = false;
    for (std::size_t i = edge; i + edge < result.phase.size(); ++i) {
        if (!result.valid[i]) continue;
        if (have_previous) result.phase[i] += tau * std::round((previous - result.phase[i]) / tau);
        previous = result.phase[i];
        have_previous = true;
    }
    for (std::size_t i = 0; i < edge && i < result.valid.size(); ++i) result.valid[i] = 0;
    for (std::size_t i = result.valid.size() > edge ? result.valid.size() - edge : 0;
         i < result.valid.size(); ++i) result.valid[i] = 0;
    result.usable = static_cast<std::uint64_t>(std::count(result.valid.begin(), result.valid.end(), 1));
    return result;
}

double expected_adev(const Model& model, double tau_seconds, double rf, double endpoints = 2) {
    const double relative = std::sqrt(endpoints);
    if (model.name == std::string_view("white_fm"))
        return relative * model.strength / (rf * std::sqrt(tau_seconds));
    if (model.name == std::string_view("random_walk_fm"))
        return relative * model.strength * std::sqrt(tau_seconds / 3) / rf;
    if (model.name == std::string_view("white_pm"))
        return std::sqrt(3.0) * relative * model.strength / (tau * rf * tau_seconds);
    return std::numeric_limits<double>::quiet_NaN();
}

bool run_model(const Options& o, const Model& model, std::uint64_t trial) {
    const auto run = simulate(o, model, trial);
    const auto seconds = run.phase.size();
    const auto maximum_tau = std::min<std::uint64_t>(512, (seconds - 1) / 4);
    std::vector<std::uint64_t> taus;
    for (std::uint64_t m = 1; m <= maximum_tau; m *= 2) taus.push_back(m);
    std::cout << std::setprecision(12);
    std::cout << "{\"type\":\"oscillator_model\",\"model\":\"" << model.name
              << "\",\"duration_seconds\":" << o.seconds << ",\"actual_samples\":"
              << run.total_samples << ",\"source_samples\":" << run.source_samples
              << ",\"usable_phase_observations\":" << run.usable << ",\"seed\":"
              << o.seed + trial * 0x9e3779b97f4a7c15ULL << ",\"tx_and_rx_independent_equal_strength\":true"
              << ",\"strength\":" << model.strength << ",\"strength_units\":\""
              << (model.name == std::string_view("white_pm") ? "radian_rms_per_sample" :
                  model.name == std::string_view("white_fm") ? "Hz_sqrt_seconds" :
                  model.name == std::string_view("random_walk_fm") ? "Hz_per_sqrt_second" :
                  "Hz_rms_over_finite_tau_band")
              << "\",\"sample_rate_hz\":64,\"rf_hz\":" << o.rf_hz
              << ",\"channel_workspace_bytes\":" << run.channel_workspace
              << ",\"both_pattern_source_workspaces_bytes\":" << run.source_workspace << "}\n";

    bool passed = true;
    long double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    std::uint64_t slope_points = 0;
    for (const auto m : taus) {
        const auto a = allan(run.phase, run.valid, m, o.rf_hz);
        const auto expected = expected_adev(model, static_cast<double>(m), o.rf_hz);
        const double ratio = expected > 0 ? a.overlapping / expected : 0;
        if (model.flicker && a.overlapping > 0 && a.overlapping_pairs >= 10) {
            const long double x = std::log(static_cast<long double>(m));
            const long double y = std::log(static_cast<long double>(a.overlapping));
            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_xy += x * y;
            ++slope_points;
        }
        if (o.check && !model.flicker && m <= 32 && a.overlapping_pairs > 100) {
            if (!(ratio >= .55 && ratio <= 1.45)) passed = false;
        }
        std::cout << "{\"type\":\"allan_deviation\",\"model\":\"" << model.name
                  << "\",\"tau_seconds\":" << m << ",\"adev_overlapping\":"
                  << a.overlapping << ",\"adev_nonoverlapping\":" << a.nonoverlapping
                  << ",\"expected_model_adev\":";
        if (std::isfinite(expected)) std::cout << expected;
        else std::cout << "null";
        std::cout << ",\"overlapping_ratio_to_model\":";
        if (std::isfinite(expected) && expected > 0) std::cout << ratio;
        else std::cout << "null";
        std::cout << ",\"overlapping_pairs\":" << a.overlapping_pairs
                  << ",\"nonoverlapping_pairs\":" << a.nonoverlapping_pairs
                  << ",\"approx_relative_standard_error\":null"
                  << ",\"uncertainty_note\":\"not computed; overlapping pairs are correlated\""
                  << ",\"finite_band_flicker_is_exact_power_law\":false}\n";
    }
    std::cout << "{\"type\":\"model_validation\",\"model\":\"" << model.name
              << "\",\"passed\":";
    if (model.flicker || !o.check) std::cout << "null";
    else std::cout << (passed ? "true" : "false");
    std::cout << ",\"validation_status\":\""
              << (model.flicker ? "diagnostic_only_no_exact_power_law" :
                  !o.check ? "unchecked" : "model_scaling_check")
              << "\",\"adev_loglog_slope\":";
    if (slope_points >= 2) {
        const auto n = static_cast<long double>(slope_points);
        const auto denom = n * sum_xx - sum_x * sum_x;
        const auto slope = denom > 0 ? (n * sum_xy - sum_x * sum_y) / denom : 0;
        std::cout << static_cast<double>(slope);
    } else std::cout << "null";
    std::cout << ",\"slope_points\":" << slope_points
              << ",\"criterion\":\"non-flicker overlapping ADEV at tau<=32s within 45% of the continuous-time small-step model\"}\n";
    return passed;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        bool pass = true;
        for (const auto& model : models(options))
            pass = run_model(options, model, 0) && pass;
        return pass ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "triad oscillator study: " << e.what() << '\n';
        return 2;
    }
}
