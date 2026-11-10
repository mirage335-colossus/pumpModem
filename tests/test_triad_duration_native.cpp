#include "datapump/triad_duration.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump::triad::v3;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    check(std::abs(actual - expected) <= tolerance * std::max(1., std::abs(expected)), message);
}
template<class F> bool rejects(F&& action) {
    try { action(); return false; } catch (const datapump::Error&) { return true; }
}

void covariance_and_tail_reference() {
    // Bounded independent Python/NumPy64-node quadrature and SciPy reference
    // values from tools/triad_duration_model.py; no receiver workload here.
    near(duration_effective_modes(1e8, 0, 0, 2), 2, 1e-14, "static covariance rank changed");
    near(duration_effective_modes(.001, .5, 0), 1.0000004112334828, 2e-13,
        "short covariance window lost edge correction");
    near(duration_effective_modes(100, .5, .5, 2), 194.12420084219858, 2e-12,
        "dispersive covariance modes differ from fixed quadrature");
    near(duration_effective_modes(100, 0, .01), 1.0033361092577198, 2e-13,
        "analytic zero-width diffusion differs from reference");
    near(duration_effective_modes(11.6, .5, .00877, 2), 21.218001583109444, 2e-12,
        "two-ray fixture covariance modes changed");
    struct Fixture { double energy, modes, mean, gaussian, deterministic; };
    for (const auto row : std::array<Fixture, 3>{{
        {20, 1, 16.955477562276577, 0, 0},
        {100, 2.5, 90.71606983323923, -1.3024264514084254, -21.97507376332794},
        {300, 40, 214.39735346014916, -15.844314542687279, -61.198973496864596}}}) {
        for (const bool gaussian : {false, true}) {
            const auto prediction = duration_prediction(row.energy, row.modes, 20, gaussian);
            near(prediction.mean_true_atom_log_lr, row.mean, 2e-13, "likelihood mean changed");
            near(prediction.log_miss_bound, gaussian ? row.gaussian : row.deterministic,
                2e-12, "Chernoff tail differs from independent reference");
            check(prediction.detection_lower_bound >= 0 && prediction.detection_lower_bound <= 1,
                "detection lower bound left probability domain");
        }
    }
    const auto weak = duration_prediction(1e-5, 10, 2e-12, true);
    near(weak.log_miss_bound / -4.4999909000102635e-13, 1, 2e-12,
        "weak energy cancellation changed inverse-square planning bound");
    check(rejects([] { (void)duration_effective_modes(0, 1, 0); }), "zero span accepted");
    check(rejects([] { (void)duration_effective_modes(1, -1, 0); }), "negative covariance accepted");
    check(rejects([] { (void)duration_prediction(1, 0, 1, false); }), "zero rank accepted");
}

void native_tile_selection() {
    Format format;
    format.sample_rate = 6000;
    format.bit_seconds = .001;
    const auto base = plan_shaping(format);
    ReceiverConfig receiver;
    receiver.sample_rate_hz = 6000;
    receiver.frequency_bound_hz = 30.174 * 3;
    receiver.clock_bound_ppm = 100.005 * 3;
    receiver.spread_delay_seconds = .021;
    receiver.spread_doppler_hz = 90;
    receiver.random_doppler_width_hz = .5;
    receiver.relative_phase_diffusion_rad_sqrt_s = .00877;
    DurationOptions options;
    options.target_cn0_db_hz = 8;
    options.arrival_lower_seconds = -7.035;
    options.arrival_upper_seconds = 7.035;
    options.helper_lead_seconds = 38;
    options.stochastic_channel = true;
    auto result = plan_duration(base, receiver, options);
    check(result.status == DurationStatus::provisional_plan && result.selected && result.shaping,
        "valid target produced no legal native duration");
    check(result.selected->meets_detection_allocation &&
        (!result.previous_legal || !result.previous_legal->meets_detection_allocation),
        "selected native tile is not the smallest bracketed model pass");
    check(result.shaping->geometry.tile_count == result.selected->tiles &&
        result.shaping->format.bit_seconds == result.selected->active_seconds,
        "accepted duration and native source geometry disagree");
    check(!result.correct_probability_qualified && !result.physical_null_qualified && !result.model_qualified,
        "provisional duration advertised receiver qualification");
    const auto original = result.selected->tiles;
    options.target_cn0_db_hz = 5;
    auto weaker = plan_duration(base, receiver, options);
    check(weaker.selected && weaker.selected->tiles >= original, "weaker target shortened duration");
    options.target_cn0_db_hz = 8;
    options.retained_fraction = .5;
    const auto lost = plan_duration(base, receiver, options);
    check(lost.selected && lost.selected->tiles >= original, "assumed energy loss shortened duration");
    options.retained_fraction = 1;
    options.residual_state_count = 6;
    const auto residual = plan_duration(base, receiver, options);
    check(residual.selected && residual.selected->tiles >= original, "extra prior states shortened duration");
    options.residual_state_count = 1;
    options.target_cn0_db_hz = -60;
    result = plan_duration(base, receiver, options);
    check(result.status == DurationStatus::duration_limit && !result.selected && !result.shaping,
        "unachievable target fabricated a duration");
    options.target_cn0_db_hz = -6;
    options.maximum_evaluations = 4;
    result = plan_duration(base, receiver, options);
    check(result.status == DurationStatus::evaluation_limit && !result.selected && !result.shaping,
        "exhausted planner claimed a selected duration");
    options.maximum_evaluations = 128;
    options.manual_active_seconds = 1.5 * base.geometry.tile_seconds;
    result = plan_duration(base, receiver, options);
    check(result.status == DurationStatus::manual_override && result.selected && result.shaping &&
        result.selected->tiles == 2, "manual duration lost legal tile rounding");
    auto burst_format = base.format;
    burst_format.burst_wait_tiles = 3;
    const auto burst = plan_duration(plan_shaping(burst_format), receiver, options);
    check(burst.selected && burst.shaping &&
        burst.selected->elapsed_seconds > burst.selected->active_seconds,
        "within-bit waiting disappeared from the covariance span");
    near(burst.selected->energy_n0, std::pow(10., options.target_cn0_db_hz / 10) *
        burst.selected->active_seconds, 2e-13, "waiting was charged as useful DATA energy");
    options.manual_active_seconds.reset();
    auto oversized = receiver;
    oversized.frequency_bound_hz = 1e18;
    const auto limited = plan_duration(base, oversized, options);
    check(limited.status == DurationStatus::geometry_limit && !limited.selected && !limited.shaping &&
        !limited.reason.empty(), "unrepresentable full grid did not return an explicit limit");
    options.correct_probability = 1;
    check(rejects([&] { (void)plan_duration(base, receiver, options); }), "invalid certainty accepted");
    options.correct_probability = .99;
    options.frequency_step_hz = .1;
    check(rejects([&] { (void)plan_duration(base, receiver, options); }), "weakened public grid accepted");
}
} // namespace

int main() {
    try {
        covariance_and_tail_reference();
        native_tile_selection();
        std::cout << "native provisional duration model and tile checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
