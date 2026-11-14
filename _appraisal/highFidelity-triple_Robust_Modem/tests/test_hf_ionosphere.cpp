#include "appraisal/channel.hpp"
#include "appraisal/hf_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace appraisal;
constexpr double pi = std::numbers::pi, c = 299792458., fs = 1920, rf = 3.4e6;
unsigned assertions = 0;
void require(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
void near(Complex actual, Complex expected, double tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance)
        std::cerr << message << ": absolute error " << std::abs(actual - expected) << '\n';
    require(std::abs(actual - expected) <= tolerance, message);
}

template<class Function> void rejected(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

Source source(std::uint64_t samples, double frequency, std::uint64_t first_nonzero = 0) {
    Source s;
    s.format.sample_rate = fs;
    s.geometry.source_samples = samples;
    s.minimum_frequency_hz = s.maximum_frequency_hz = frequency;
    s.sample_at = [frequency, first_nonzero](auto i) {
        if (i < first_nonzero) return Complex{};
        return std::polar(1., 2 * pi * frequency * (static_cast<double>(i) / fs));
    };
    return s;
}

std::vector<Complex> capture(Source s, ChannelSpec spec, std::uint64_t samples, std::size_t chunk, std::uint64_t seed = 9011) {
    Channel channel(std::move(s), std::move(spec), seed, samples);
    std::vector<Complex> out(samples);
    std::size_t first = 0;
    while (first < out.size()) {
        const auto count = std::min(chunk, out.size() - first);
        if (channel.read(std::span(out).subspan(first, count)) != count)
            throw std::runtime_error("HF render chunk incomplete");
        first += count;
    }
    return out;
}

void coupled_motion() {
    const double frequency = 136.;
    auto spec = make_scenario("hf-vertical-eikonal-motion").channel;
    auto& path = spec.paths.front();
    path.hf->parabolic_layer.reset();
    path.hf->group_delay_slope_seconds_per_hz = 0;
    path.hf->group_delay_motion = {32e-6, 5, .25, true};
    path.delay_seconds = .003;
    constexpr std::uint64_t n = 15360;
    const auto x = capture(source(n, frequency), spec, n, 357);
    const auto split = capture(source(n, frequency), spec, n, 4096);
    require(x == split, "moving HF sample history changes with chunking");
    // Interior tones have the complete interpolation stencil. The separate
    // tail test handles finite source termination and zero extension.
    for (const std::uint64_t sample : {70, 540, 3754, 4096, 8129, 15330}) {
        const double t = sample / fs;
        const double motion = 32e-6 * (std::sin(2 * pi * t / 5 + .25) - std::sin(.25));
        const double cycles = frequency * (t - .003) - (spec.rf_hz + frequency) * motion;
        near(x[sample], std::polar(1., 2 * pi * cycles), 5e-5, "geometric HF delay and RF phase disagree");
    }
    path.hf->group_delay_motion.couple_carrier_phase = false;
    const auto uncoupled = capture(source(n, frequency), spec, n, 357);
    for (const std::uint64_t sample : {70, 3754, 4096, 8129, 15330}) {
        const double t = sample / fs;
        const double motion = 32e-6 * (std::sin(2 * pi * t / 5 + .25) - std::sin(.25));
        near(uncoupled[sample], std::polar(1., 2 * pi * frequency * (t - .003 - motion)), 5e-5,
             "separate HF group delay added an unrequested carrier phase");
    }
    require(std::abs(x[4096] - uncoupled[4096]) > .2, "motion discriminator lacked useful RF phase contrast");
}

std::pair<double, double> integrate_vertical(const HfParabolicLayer& layer, double frequency, double critical) {
    // Independent integration of sqrt(n^2) and its reciprocal. The
    // substitution u=sqrt(1-b^2) cosh(v) removes the ray-turn singularity;
    // P and G integrands are evaluated directly, not by the closed formula.
    const double b = frequency / critical, a = std::sqrt(1 - b * b);
    const double extent = std::acosh(1 / a);
    double phase = 0, group = 0;
    constexpr unsigned nodes = 32768;
    for (unsigned i = 0; i < nodes; ++i) {
        const double v = extent * (i + .5) / nodes;
        const double k = a * std::sinh(v);
        phase += k * k / b;
        group += b;
    }
    const double dz = 2 * layer.semi_thickness_m * extent / nodes;
    return {(2 * layer.bottom_height_m + dz * phase) / c,
            (2 * layer.bottom_height_m + dz * group) / c};
}

void eikonal_derivatives() {
    const auto moving = make_scenario("hf-vertical-eikonal-motion");
    const auto fixed = make_scenario("hf-vertical-eikonal-static");
    const auto& layer = *moving.channel.paths.front().hf->parabolic_layer;
    const auto& fixed_layer = *fixed.channel.paths.front().hf->parabolic_layer;
    const auto initial = hf_parabolic_eikonal(layer, rf, 0);
    near(fixed.channel.paths.front().delay_seconds, initial.group_delay_seconds, 0, "matched eikonal initial delay");
    near(hf_parabolic_eikonal(fixed_layer, rf, 30).phase_delay_seconds, initial.phase_delay_seconds, 0,
         "static eikonal has phase dynamics");
    constexpr double delta = 50;
    for (const double t : {0., 30., 67., 90., 120., 269.}) {
        const double critical = layer.critical_frequency_hz + layer.critical_amplitude_hz * std::sin(2 * pi * t / 120);
        const auto [phase, group] = integrate_vertical(layer, rf, critical);
        const auto e = hf_parabolic_eikonal(layer, rf, t);
        near(e.phase_delay_seconds, phase, 3e-12, "closed-form phase length vs numerical eikonal");
        near(e.group_delay_seconds, group, 3e-12, "closed-form group length vs numerical eikonal");
        const auto high = hf_parabolic_eikonal(layer, rf + delta, t);
        const auto low = hf_parabolic_eikonal(layer, rf - delta, t);
        near((high.phase_delay_seconds * (rf + delta) - low.phase_delay_seconds * (rf - delta)) / (2 * delta),
             e.group_delay_seconds, 1e-11, "group delay is derivative of phase, not phase delay itself");
        near((high.group_delay_seconds - low.group_delay_seconds) / (2 * delta),
             e.group_delay_slope_seconds_per_hz, 1e-17, "group-delay slope disagrees with frequency derivative");
        require(std::abs(group - initial.group_delay_seconds) <= moving.bounds.delay_excursion_seconds,
                "eikonal excursion exceeds public delay bound");
    }
    const double dt = .01;
    const auto after = hf_parabolic_eikonal(layer, rf, dt);
    const auto before = hf_parabolic_eikonal(layer, rf, -dt);
    const auto doppler = -rf * (after.phase_delay_seconds - before.phase_delay_seconds) / (2 * dt);
    const auto incorrect = -rf * (after.group_delay_seconds - before.group_delay_seconds) / (2 * dt);
    require(doppler > 0 && doppler < moving.bounds.atmospheric_hz && std::abs(doppler - incorrect) > .3,
            "phase/group delay contrast or public frequency bound");

    auto invalid = moving.channel;
    invalid.paths[0].hf->parabolic_layer->critical_frequency_hz = rf;
    rejected([&] { Channel channel(source(1000, 10), invalid, 0, 1000); }, "eikonal continued above cutoff");

    constexpr std::uint64_t n = 2400;
    for (const auto frequency : {-300., 50., 300.}) {
        const auto iq = capture(source(n, frequency), moving.channel, n, 143);
        for (const auto sample : {80, 800, 1563, 2370}) {
            const double t = sample / fs;
            const auto e = hf_parabolic_eikonal(layer, rf + frequency, t);
            const double cycles = frequency * t - ((rf + frequency) * e.phase_delay_seconds - rf * initial.phase_delay_seconds);
            near(iq[sample], std::polar(1., 2 * pi * cycles), 2e-4, "rendered eikonal disagrees with full-frequency phase");
        }
    }
}

void dispersion_and_tails() {
    auto s = make_scenario("hf-trough-static").channel;
    s.paths.resize(1);
    auto& p = s.paths.front();
    p.gaussian_width_2sigma_hz = 0;
    p.power = 1;
    p.hf->coherent_to_diffuse_power = 0;
    p.hf->group_delay_motion.amplitude_seconds = 0;
    p.delay_seconds = 7 / fs;
    p.hf->group_delay_slope_seconds_per_hz = 1e-9;
    near(channel_interpolation_tail_seconds(s, fs), 20 / fs, 1e-15, "HF tail excludes dispersion halo");
    near(channel_filter_tail_seconds(s, fs), 20 / fs, 1e-15, "HF filter halo wrong");
    constexpr std::uint64_t n = 1000;
    for (const double slope : {-1e-9, 1e-9}) {
        p.hf->group_delay_slope_seconds_per_hz = slope;
        for (const auto f : {-300., 100., 300.}) {
            const auto iq = capture(source(n, f), s, n + 40, 37);
            for (const auto sample : {80, 500, 977}) {
                const double phase = 2 * pi * f * (sample / fs - p.delay_seconds) - pi * slope * f * f;
                near(iq[sample], std::polar(1., phase), 9e-7,
                     "phase-curvature stencil differs from physical intramode transfer");
            }
            require(std::abs(iq[n + 10]) > 0 && iq[n + 11] == Complex{},
                    "dispersion FIR tail samples lost or extended");
        }
    }
    auto invalid = s;
    invalid.paths.front().delay_seconds = 3 / fs;
    rejected([&] { Channel channel(source(n, 300), invalid, 0, n); }, "dispersion preecho extends before causal delay");
    rejected([&] { Channel channel(source(n, 500), s, 0, n); }, "unsupported dispersion band silently admitted");
}

void polarization_and_gates() {
    const auto split = make_scenario("hf-ox-split"), cancellation = make_scenario("hf-ox-cancellation");
    const auto& a = *split.channel.paths[0].hf->polarization;
    const auto& b = *split.channel.paths[1].hf->polarization;
    near(hf_port_projection(a, 0), .5, 2e-16, "linear ports charge circular ordinary weight once");
    near(hf_port_projection(b, 0), .5, 2e-16, "linear ports charge circular extraordinary weight once");
    auto p = a;
    p.receive_angle_radians = .43;
    p.transmit_angle_radians = -.79;
    p.rotation_amplitude_radians = .4;
    auto q = p;
    q.extraordinary = true;
    for (const double time : {0., 18., 66.})
        near(hf_port_projection(p, time) + hf_port_projection(q, time), std::cos(.43 + .79), 1e-14,
             "orthogonal complex Jones projection violates lossless port closure");
    constexpr std::uint64_t n = 4 * 1920 + 1;
    const auto control = capture(source(n + 32, 0), split.channel, n, 361);
    const auto result = capture(source(n + 32, 0), cancellation.channel, n, 1024);
    near(control.back(), 1., 1e-13, "matched O/X transfer has spurious power loss");
    near(result.back(), 0., 1e-12, "half-cycle O/X polarization cancellation absent");

    auto no_gate = make_scenario("hf-mode-transition").channel;
    no_gate.paths.resize(1);
    auto& path = no_gate.paths.front();
    path.hf->voltage_transitions.clear();
    auto gated = no_gate;
    gated.paths.front().hf->voltage_transitions = {{.5, 1, 1, 0}, {1.5, 2, 0, 1}};
    const auto unbroken = capture(source(n, 0), no_gate, n, 999);
    const auto pause = capture(source(n, 0), gated, n, 73);
    near(pause[1440], .5 * unbroken[1440], 1e-13, "raised-cosine voltage midpoint");
    near(pause[2400], 0, 0, "mode during gated interval still contributes");
    near(pause[5100], unbroken[5100], 0, "revived mode resets its latent fade");
    gated.paths.front().hf->voltage_transitions.back().from = .1;
    rejected([&] { Channel channel(source(n, 0), gated, 0, n); }, "discontinuous mode-voltage schedule accepted");
}

void summary_modes() {
    const auto i1 = make_scenario("hf-watterson-i1"), i3 = make_scenario("hf-watterson-i3");
    require(i1.channel.paths.size() == 4 && i3.channel.paths.size() == 6, "literal component count changed");
    near(i1.channel.rf_hz, 9.259e6, 0, "I1 carrier");
    near(i3.channel.rf_hz, 5.864e6, 0, "I3 carrier");
    for (const auto* scenario : {&i1, &i3}) {
        const auto& paths = scenario->channel.paths;
        near(paths[0].delay_seconds, paths[1].delay_seconds, 0, "magnetoionic paired delay changed");
        near(std::accumulate(paths.begin(), paths.end(), 0., [](double p, const auto& x) { return p + x.power; }),
             1., 1e-14, "summary normalization is not absolute fixed reference");
    }
    near(i1.channel.paths[3].residual_doppler_hz, -.167, 0, "I1 weak negative-centroid component");
    near(i1.channel.paths[0].gaussian_width_2sigma_hz, .0073, 0, "I1 narrower magnetoionic width");
    near(i3.channel.paths[5].gaussian_width_2sigma_hz, .021, 0, "I3 tail width");
}

void measured_time_origin() {
    const auto early = make_scenario("hf-ding-measured");
    const auto late = make_scenario("hf-ding-measured-late");
    const auto clock = make_scenario("hf-ding-clock41");
    const auto& full = *early.channel.paths.front().hf->measured_envelope;
    const auto& shifted = *late.channel.paths.front().hf->measured_envelope;
    const auto& alternative = *clock.channel.paths.front().hf->measured_envelope;
    require(full.samples == shifted.samples && full.samples == alternative.samples,
            "matched replay time origin or clock changed measured samples");
    near(alternative.sample_rate_hz, 2048. / 41, 0, "alternate clock convention changed");
    constexpr std::uint64_t n = 2000;
    const auto early_iq = capture(source(n + 32, 0), early.channel, n, 137);
    const auto late_iq = capture(source(n + 32, 0), late.channel, n, 2000);
    const auto clock_iq = capture(source(n + 32, 0), clock.channel, n, 873);
    for (const auto sample : {90, 1003, 1999}) {
        const long double t = sample / static_cast<long double>(fs);
        near(early_iq[sample], hf_replay_gain(full, t), 1e-13, "early measured source indexing");
        near(late_iq[sample], hf_replay_gain(full, t + 20), 1e-13, "late measured source indexing");
        near(clock_iq[sample], hf_replay_gain(alternative, t), 1e-13, "alternate measured clock indexing");
    }
    require(std::abs(late_iq[1003] - early_iq[1003]) > .01 && std::abs(clock_iq[1003] - early_iq[1003]) > 1e-5,
            "measured timing variants had no effect");
    const auto too_long = static_cast<std::uint64_t>(std::ceil(late.numerical_horizon_seconds * fs)) + 1;
    rejected([&] { Channel channel(source(n, 0), late.channel, 0, too_long); },
             "late measured source extrapolated beyond last knot");
}
}

int main() {
    try {
        coupled_motion();
        eikonal_derivatives();
        dispersion_and_tails();
        polarization_and_gates();
        summary_modes();
        measured_time_origin();
        std::cout << "HF ionosphere assertions passed: " << assertions << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HF ionosphere failed after " << assertions << " assertions: " << e.what() << '\n';
        return 1;
    }
}
