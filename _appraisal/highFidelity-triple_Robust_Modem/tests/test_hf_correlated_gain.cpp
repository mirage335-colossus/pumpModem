#include "appraisal/hf_correlated_gain.hpp"
#include "triad_receiver_executor.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

extern "C" int triad_gaussian_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* mean, double* covariance,
    double* scores, double* diagnostics) noexcept;

namespace {
using namespace appraisal;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using Plan = HfCorrelatedGainPlan;
constexpr auto count = 7U;
constexpr double noise = .7, power = .8;
constexpr std::array<Complex, count> x{{{1., .1}, {.2, -.7}, {}, {}, {-.5, .3}, {.1, .8}, {.6, -.2}}};
constexpr std::array<Complex, count> y{{{.3, 1.}, {.6, -.4}, {.2, .9}, {.8, .1}, {.7, -.6}, {.3, .4}, {1., -.5}}};

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void near(double a, double b, const char* reason) {
    check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 5e-12, reason);
}

double dense_ratio(const std::array<Complex, count>& observed, double rho) {
    auto lower = std::array<std::array<Complex, count>, count>{};
    auto whitened = std::array<Complex, count>{};
    double result = 0.;
    for (unsigned row = 0; row < count; ++row) {
        for (unsigned column = 0; column <= row; ++column) {
            const auto lag = row / 2 - column / 2;
            auto value = std::pow(rho, lag) * power * x[row] * std::conj(x[column]);
            if (row == column) value += noise;
            for (unsigned previous = 0; previous < column; ++previous)
                value -= lower[row][previous] * std::conj(lower[column][previous]);
            lower[row][column] = column == row ? std::sqrt(value.real()) : value / lower[column][column];
        }
        auto residual = observed[row];
        for (unsigned column = 0; column < row; ++column) residual -= lower[row][column] * whitened[column];
        whitened[row] = residual / lower[row][row];
        result += std::norm(observed[row]) / noise - std::norm(whitened[row]) - std::log(std::norm(lower[row][row]) / noise);
    }
    return result;
}

double forward(const std::array<Complex, count>& observed, double rho) {
    double covariance = power, value = 0.;
    Complex mean{};
    std::array<Complex, 3> native_mean{}, native_covariance{power, power, power};
    std::array<double, 3> native_scores{};
    std::array<double, 2> diagnostics{1., 0.};
    const std::array<double, 4> transition{rho * rho, 1 - rho * rho, rho, 0.};
    for (unsigned first = 0; first < count; first += 2) {
        double gram = 0.;
        Complex q{};
        for (unsigned row = first; row < count && row < first + 2; ++row) {
            gram += std::norm(x[row]) / noise;
            q += std::conj(x[row]) * observed[row] / noise;
        }
        const auto result = hf_gain_innovation(covariance, gram, power, rho).accumulate(mean, q);
        if (first == 2) check(result == 0., "zero-energy block contributes exact zero LR");
        value += result;
        const std::array<Complex, 3> r{Complex{}, q, Complex{}}, g{Complex{}, gram, Complex{}};
        const auto data = [](auto& array) { return reinterpret_cast<const double*>(array.data()); };
        const auto mutable_data = [](auto& array) { return reinterpret_cast<double*>(array.data()); };
        check(triad_gaussian_update_v1(1, 1, 1, 1, 0, 1, data(r), data(g), &power, transition.data(),
              mutable_data(native_mean), mutable_data(native_covariance), native_scores.data(), diagnostics.data()) == 0,
              "native B=1 Gaussian kernel must admit the witness");
        near(value, native_scores[1], "forward evidence must match the native B=1 kernel after each block");
        check(native_scores[0] == 0. && native_scores[2] == 0., "native null peer scores remain zero");
    }
    return value;
}

void covariance_witness() {
    auto rotated = y;
    for (auto& value : rotated) value *= std::polar(1., 1.234);
    for (const auto rho : {0., .55, std::nextafter(1., 0.), 1.}) {
        const auto expected = dense_ratio(y, rho);
        near(forward(y, rho), expected, "factored gain likelihood must match dense proper-complex covariance");
        near(forward(rotated, rho), expected, "circular gain likelihood must be invariant to global IQ phase");
    }
}

void frequency_projection_witness() {
    HfGainFrequencyProjection projection(4);
    std::array<Complex, Plan::fft_rows> scratch{};
    std::array<Complex, Plan::atoms_per_group> q{};
    for (std::size_t i = 0; i < q.size(); ++i) q[i] = {.03 * static_cast<double>(i), std::cos(.4 * i)};
    for (std::size_t group : {0U, 2U, 3U}) {
        const auto n = group == 3 ? 7 : q.size();
        const auto result = projection.project(group, std::span(q).first(n), scratch);
        for (std::size_t index : {0U, 160U, 320U}) {
            const auto frequency = -Plan::frequency_bound_hz + index * Plan::frequency_step_hz;
            Complex direct{};
            for (std::size_t i = 0; i < n; ++i) {
                const double center = group * Plan::group_rows + i * Plan::atom_rows + (Plan::atom_rows - 1) * .5;
                direct += q[i] * std::polar(1., -2 * std::numbers::pi * frequency * center / Plan::sample_rate);
            }
            near(std::abs(result[index] - direct), 0., "CFO projection must retain global phase and partial-group exposure");
        }
    }
}

void geometry_witness() {
    ReceiverConfig config;
    config.sample_rate_hz = 1920;
    config.time_varying_atmospheric_hz = 5;
    config.frequency_bound_hz = 0;
    config.clock_bound_ppm = config.endpoint_clock_bound_ppm = 0;
    config.spread_delay_seconds = config.spread_doppler_hz = 0;
    config.noise_variance = 1920;
    config.helper_enabled = false;
    ReceiverSlotPlan slot;
    slot.first_sample = 345524;
    slot.stop_sample = 480445;
    slot.arrival_lower_seconds = 4. / 1920;
    slot.arrival_upper_seconds = 84. / 1920;
    slot.format.sample_rate = 1920;
    slot.format.bit_seconds = 128;
    const auto plan = hf_correlated_gain_plan(config, slot);
    check(plan.samples == 134921 && plan.groups == 141 && plan.timings == 559, "complete public lens geometry is admitted");
    check(plan.basis_products == 150841678 && plan.fft_row_stages == 3551268864 && plan.innovations == 607221576,
          "full public work is charged before looking at IQ");
    const auto short_workspace = hf_correlated_gain_workspace(config, slot, 24);
    slot.first_sample = 575924;
    slot.stop_sample = 844477;
    const auto long_plan = hf_correlated_gain_plan(config, slot);
    check(long_plan.samples == 268553 && long_plan.groups == 280 && long_plan.timings == 559,
          "full doubled-duration aperture keeps the same public timing lattice");
    check(long_plan.basis_products == 300242254 && long_plan.fft_row_stages == 7052165120ULL &&
          long_plan.innovations == 1205830080, "long work quote must retain its full 64-bit counts");
    check(hf_correlated_gain_workspace(config, slot, 24) > short_workspace,
          "long projection memory must be charged before scoring with the same worker budget");
    constexpr std::uint64_t limit = 286225;
    slot.stop_sample = slot.first_sample + limit;
    check(hf_correlated_gain_plan(config, slot).samples == limit, "last within-budget lens aperture is supported");
    const auto reject_basis = [&](std::uint64_t stop) {
        slot.stop_sample = stop;
        try { (void)hf_correlated_gain_plan(config, slot); }
        catch (const datapump::Error& error) {
            return std::string_view(error.what()).find("basis work cap") != std::string_view::npos;
        }
        return false;
    };
    check(reject_basis(slot.first_sample + limit + 1), "first over-budget lens aperture must fail at preflight");
    check(reject_basis(UINT64_MAX), "extreme public aperture must fail before work-count multiplication");
}

void shared_scorer_witness() {
    ReceiverConfig config;
    config.sample_rate_hz = 1920;
    config.data_if_hz = 240;
    config.frequency_bound_hz = 0;
    config.time_varying_atmospheric_hz = 5;
    config.noise_variance = .7;
    config.helper_enabled = false;
    config.clock_bound_ppm = config.endpoint_clock_bound_ppm = 0;
    config.spread_delay_seconds = config.spread_doppler_hz = 0;
    ReceiverSlotPlan slot;
    slot.index = 0;
    slot.first_sample = 20000;
    slot.window_capture_lead_seconds = .025;
    slot.source_start_seconds = .06;
    slot.format.sample_rate = 1920;
    slot.format.bit_seconds = .001;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.pulse_shape = PulseShape::Rrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.encrypt = true;
    // A canonical tile has a minimum dwell envelope. Use an explicitly
    // bounded component aperture inside that source, with a partial last atom.
    constexpr auto rows = 2 * Plan::group_rows + Plan::atom_rows - 1;
    check(geometry(slot.format).source_samples > rows, "component aperture must lie inside one native source tile");
    slot.stop_sample = slot.first_sample + rows;
    slot.arrival_lower_seconds = slot.arrival_upper_seconds = .002;
    const auto key = datapump::Crypto::amateur();
    std::vector<Complex> iq(rows);
    for (std::size_t i = 0; i < iq.size(); ++i) iq[i] = {std::cos(.11 * i), .26 * std::sin(.07 * i)};
    FinePreparation preparation{AdmissionGrid(.002, .002, 0, 0, .01), {{0, .002, 0, 0}}, {}, {}, {}};
    ReceiverExecutor one(1), several(3);
    const auto quote = hf_correlated_gain_workspace(config, slot, 1);
    const auto single = score_hf_correlated_gain(config, slot, key, iq, preparation, 1, quote, {}, one);
    check(std::abs(single.bit_log_e[0]) > 1e-6 || std::abs(single.bit_log_e[1]) > 1e-6,
          "lane fixture must exercise nonzero native template evidence");
    HfCorrelatedGainReceiver fixture(1);
    const auto hook = fixture.supplement();
    check(hook.fixed_weight == .25 && hook.supported(config, slot), "experimental hook must keep its fixed prior and support");
    const auto multi_quote = hook.workspace(config, slot, 3);
    const auto multi = hook.score(config, slot, key, iq, preparation, 3, multi_quote, {}, several);
    check(single.bit_log_e == multi && single.plan.timings == 13 && single.plan.innovations == single.work.innovations && fixture.scored(0),
          "shared scorer evidence and full work are invariant to admitted lane count");
    bool rejected = false;
    try { (void)hook.score(config, slot, key, iq, preparation, 3, multi_quote - 1, {}, several); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "oversubscribed slot must retain its prior and reject its undersized lease");
}
} // namespace

int main() {
    try {
        covariance_witness();
        frequency_projection_witness();
        geometry_witness();
        shared_scorer_witness();
        std::cout << "Correlated gain covariance, native-kernel, phase, CFO, public-work and lane witnesses passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
