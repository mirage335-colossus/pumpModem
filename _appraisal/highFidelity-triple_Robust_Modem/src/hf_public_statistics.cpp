#include "appraisal/hf_public_statistics.hpp"
#include "appraisal/hf_phase_bank.hpp"
#include "search_fft.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace appraisal {
namespace {
using Complex = datapump::triad::v3::Complex;
using Clock = std::chrono::steady_clock;
constexpr long double pi = std::numbers::pi_v<long double>;
constexpr std::size_t overhead_bytes = 4096;
constexpr long double slope_phase_tolerance = .25L;
constexpr long double minus_infinity = -std::numeric_limits<long double>::infinity();

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF public statistics cancelled");
}
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    require(!a || b <= UINT64_MAX / a, "HF public statistics work count overflow");
    return a * b;
}
std::size_t count(long double value, const char* message) {
    require(std::isfinite(value) && value >= 0 && value <= static_cast<long double>(SIZE_MAX / 2), message);
    return static_cast<std::size_t>(value);
}
double finite_double(long double value) {
    require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<double>::max(),
            "HF public statistics result exceeds finite report range");
    return static_cast<double>(value);
}
long double log_add(long double a, long double b) {
    if (a == minus_infinity) return b;
    if (b == minus_infinity) return a;
    const auto high = std::max(a, b), low = std::min(a, b);
    return high + std::log1p(std::exp(low - high));
}
unsigned family_index(HfPublicPhaseFamily family) {
    const auto index = static_cast<unsigned>(family);
    require(index < 3, "unknown HF public phase family");
    return index;
}
bool same_plan(const HfPublicStatisticsPlan& a, const HfPublicStatisticsPlan& b) {
    return a.fft_rows == b.fft_rows && a.frequency_radius == b.frequency_radius &&
        a.frequency_count == b.frequency_count && a.affine_slope_radius == b.affine_slope_radius &&
        a.frequency_step_hz == b.frequency_step_hz &&
        a.actual_frequency_bound_hz == b.actual_frequency_bound_hz &&
        a.affine_slope_step_hz_per_second == b.affine_slope_step_hz_per_second &&
        a.affine_nearest_grid_phase_error_bound_radians == b.affine_nearest_grid_phase_error_bound_radians &&
        a.family_model_counts == b.family_model_counts &&
        a.family_fft_row_stages == b.family_fft_row_stages &&
        a.actual_fft_count == b.actual_fft_count && a.actual_fft_row_stages == b.actual_fft_row_stages &&
        a.phase_complex_multiplications == b.phase_complex_multiplications &&
        a.likelihood_cells == b.likelihood_cells && a.unique_phase_models == b.unique_phase_models &&
        a.prepared_phase_values == b.prepared_phase_values &&
        a.shared_workspace_bytes == b.shared_workspace_bytes && a.lane_workspace_bytes == b.lane_workspace_bytes &&
        a.workspace_bytes == b.workspace_bytes;
}

struct FrequencySummary {
    long double log_sum = minus_infinity, maximum = minus_infinity;
    double maximum_frequency = 0;
};
struct FamilySummary {
    long double log_sum = minus_infinity, maximum = minus_infinity;
    double maximum_frequency = 0;
    std::size_t maximum_model = 0;
    void append(const FrequencySummary& value, std::size_t model) {
        log_sum = log_add(log_sum, value.log_sum);
        if (value.maximum > maximum) {
            maximum = value.maximum;
            maximum_frequency = value.maximum_frequency;
            maximum_model = model;
        }
    }
};
} // namespace

HfPublicStatisticsPlan hf_public_statistics_plan(const HfPublicStatisticsConfig& c) {
    require(c.block_count > 0, "HF public statistics needs at least one block");
    require(std::isfinite(c.block_seconds) && c.block_seconds > 0 &&
            std::isfinite(c.aperture_seconds) && c.aperture_seconds > 0 &&
            std::isfinite(c.first_block_center_seconds) && std::isfinite(c.aperture_midpoint_seconds),
            "HF public statistics needs finite positive aperture geometry");
    const auto block_span = static_cast<long double>(c.block_count) * c.block_seconds;
    const auto previous_span = static_cast<long double>(c.block_count - 1) * c.block_seconds;
    const auto tolerance = 32 * std::numeric_limits<double>::epsilon() * std::max(1.L, block_span);
    require(c.aperture_seconds > previous_span - tolerance && c.aperture_seconds <= block_span + tolerance,
            "HF public statistics aperture must fill every block except possibly the last");
    require(std::isfinite(c.white_noise_variance) && c.white_noise_variance > 0 &&
            std::isfinite(c.frequency_bound_hz) && c.frequency_bound_hz >= 0 &&
            std::isfinite(c.affine_slope_bound_hz_per_second) && c.affine_slope_bound_hz_per_second >= 0,
            "HF public statistics needs known positive noise and finite nonnegative search bounds");
    require(c.maximum_fft_rows >= 2 && c.maximum_fft_row_stages > 0 && c.maximum_workspace_bytes > 0,
            "HF public statistics needs positive finite resource limits");
    const auto maximum_step = std::min(.01L, 1 / (4.L * c.aperture_seconds));
    const auto minimum_fft = std::max(static_cast<long double>(c.block_count),
        std::ceil(1 / (static_cast<long double>(c.block_seconds) * maximum_step)));
    require(std::isfinite(minimum_fft) && minimum_fft <= c.maximum_fft_rows,
            "HF public statistics FFT row limit exceeded");
    HfPublicStatisticsPlan p;
    p.config = c;
    p.fft_rows = 2;
    while (static_cast<long double>(p.fft_rows) < minimum_fft) {
        require(p.fft_rows <= c.maximum_fft_rows / 2 && p.fft_rows <= SIZE_MAX / 2,
                "HF public statistics FFT row limit exceeded");
        p.fft_rows *= 2;
    }
    const auto spacing = 1 / (static_cast<long double>(p.fft_rows) * c.block_seconds);
    p.frequency_step_hz = finite_double(spacing);
    p.frequency_radius = count(std::ceil(static_cast<long double>(c.frequency_bound_hz) / spacing),
                               "HF public statistics frequency radius overflow");
    // +/-Nyquist are the same effective template. Never count either twice.
    require(p.frequency_radius < p.fft_rows / 2,
            "HF public statistics CFO support reaches the block Nyquist frequency");
    p.frequency_count = 2 * p.frequency_radius + 1;
    p.actual_frequency_bound_hz = finite_double(p.frequency_radius * spacing);
    p.family_model_counts[0] = 1;
    p.family_model_counts[2] = hf_phase_bank_models().size();
    if (c.include_affine) {
        const long double first = c.first_block_center_seconds;
        const long double last = first + static_cast<long double>(c.block_count - 1) * c.block_seconds;
        require(std::isfinite(last), "HF public statistics last block clock overflow");
        const auto extent = std::max({.5L * c.aperture_seconds,
            std::abs(first - c.aperture_midpoint_seconds), std::abs(last - c.aperture_midpoint_seconds)});
        if (c.affine_slope_bound_hz_per_second > 0) {
            const auto maximum_slope_step = 2 * slope_phase_tolerance / (pi * extent * extent);
            p.affine_slope_radius = count(std::ceil(c.affine_slope_bound_hz_per_second / maximum_slope_step),
                                          "HF public statistics affine grid overflow");
            require(p.affine_slope_radius > 0, "HF public statistics affine step underflow");
            p.affine_slope_step_hz_per_second = finite_double(
                static_cast<long double>(c.affine_slope_bound_hz_per_second) / p.affine_slope_radius);
            p.affine_nearest_grid_phase_error_bound_radians = finite_double(
                pi * .5L * p.affine_slope_step_hz_per_second * extent * extent);
        }
        p.family_model_counts[1] = 2 * p.affine_slope_radius + 1;
    }
    const auto row_stages = multiply(p.fft_rows, std::countr_zero(p.fft_rows));
    for (std::size_t i = 0; i < p.family_model_counts.size(); ++i)
        p.family_fft_row_stages[i] = multiply(multiply(2, p.family_model_counts[i]), row_stages);
    // The zero phase/CFO model is evaluated only once, and shared by families.
    const auto unique_models = static_cast<std::uint64_t>(p.family_model_counts[2]) +
        (c.include_affine ? static_cast<std::uint64_t>(p.family_model_counts[1] - 1) : 0);
    require(unique_models <= SIZE_MAX, "HF public statistics phase model count overflow");
    p.unique_phase_models = static_cast<std::size_t>(unique_models);
    p.prepared_phase_values = multiply(unique_models, c.block_count);
    p.actual_fft_count = multiply(2, unique_models);
    p.actual_fft_row_stages = multiply(p.actual_fft_count, row_stages);
    p.phase_complex_multiplications = multiply(p.actual_fft_count, c.block_count);
    p.likelihood_cells = multiply(p.actual_fft_count, p.frequency_count);
    require(p.actual_fft_row_stages <= c.maximum_fft_row_stages,
            "HF public statistics FFT work limit exceeded");
    require(p.fft_rows <= (SIZE_MAX - overhead_bytes) / sizeof(Complex) &&
            p.prepared_phase_values <= (SIZE_MAX - overhead_bytes) / sizeof(Complex),
            "HF public statistics workspace overflow");
    const auto table_bytes = static_cast<std::size_t>(p.prepared_phase_values) * sizeof(Complex);
    const auto twiddle_bytes = (p.fft_rows - 1) * sizeof(Complex);
    require(table_bytes <= SIZE_MAX - overhead_bytes - twiddle_bytes,
            "HF public statistics shared workspace overflow");
    p.shared_workspace_bytes = table_bytes + twiddle_bytes + overhead_bytes;
    p.lane_workspace_bytes = p.fft_rows * sizeof(Complex) + overhead_bytes;
    require(p.shared_workspace_bytes <= SIZE_MAX - p.lane_workspace_bytes,
            "HF public statistics aggregate workspace overflow");
    p.workspace_bytes = p.shared_workspace_bytes + p.lane_workspace_bytes;
    require(p.workspace_bytes <= c.maximum_workspace_bytes,
            "HF public statistics workspace limit exceeded");
    return p;
}

HfPublicPhaseModel hf_public_phase_model(const HfPublicStatisticsPlan& p, HfPublicPhaseFamily family,
                                        std::size_t index) {
    const auto fi = family_index(family);
    require(index < p.family_model_counts[fi], "HF public phase model index outside fixed family");
    HfPublicPhaseModel model;
    model.family = family;
    if (family == HfPublicPhaseFamily::affine) {
        const auto signed_index = static_cast<long double>(index) - p.affine_slope_radius;
        model.slope_hz_per_second = finite_double(signed_index * p.affine_slope_step_hz_per_second);
    } else if (family == HfPublicPhaseFamily::sinusoid) {
        const auto source = hf_phase_bank_models()[index];
        model.amplitude_radians = source.amplitude_radians;
        model.period_seconds = source.period_seconds;
        model.phase_radians = source.phase_radians;
    }
    return model;
}

long double hf_public_phase_radians(const HfPublicStatisticsPlan& p, const HfPublicPhaseModel& model,
                                    long double seconds) {
    require(std::isfinite(seconds), "HF public phase clock must be finite");
    family_index(model.family);
    if (model.family == HfPublicPhaseFamily::affine) {
        const auto relative = seconds - p.config.aperture_midpoint_seconds;
        const auto phase = pi * model.slope_hz_per_second * relative * relative;
        require(std::isfinite(phase), "HF public affine phase overflow");
        return phase;
    }
    if (model.family == HfPublicPhaseFamily::sinusoid && model.amplitude_radians != 0) {
        require(std::isfinite(model.period_seconds) && model.period_seconds > 0 &&
                std::isfinite(model.amplitude_radians) && std::isfinite(model.phase_radians),
                "invalid HF public sinusoidal model");
        const auto cycles = std::remainder(seconds / model.period_seconds, 1.L);
        return model.amplitude_radians * std::sin(2 * pi * cycles + model.phase_radians);
    }
    return 0;
}

struct HfPreparedPublicStatistics::Impl {
    HfPublicStatisticsPlan plan;
    datapump::modem::detail::search_fft::DoublePlan fft;
    std::vector<Complex> rotations;
    std::size_t actual_shared_bytes = 0;
    double preparation_seconds = 0;

    Impl(const HfPublicStatisticsPlan& p, std::stop_token stop)
        : plan(p), fft(p.fft_rows, stop), rotations(static_cast<std::size_t>(p.prepared_phase_values)) {
        require(rotations.capacity() <= (SIZE_MAX - overhead_bytes) / sizeof(Complex),
                "HF public statistics phase table capacity overflow");
        const auto table_bytes = rotations.capacity() * sizeof(Complex);
        require(fft.working_bytes() <= SIZE_MAX - table_bytes - overhead_bytes,
                "HF public statistics actual shared workspace overflow");
        actual_shared_bytes = table_bytes + fft.working_bytes() + overhead_bytes;
        require(actual_shared_bytes <= p.shared_workspace_bytes,
                "HF public statistics preparation exceeds admitted shared workspace");
        const auto prepare = [&](HfPublicPhaseFamily family, std::size_t index, std::size_t unique) {
            cancelled(stop);
            const auto model = hf_public_phase_model(p, family, index);
            auto* values = rotations.data() + unique * p.config.block_count;
            for (std::size_t j = 0; j < p.config.block_count; ++j) {
                if ((j & 4095U) == 0) cancelled(stop);
                const auto t = static_cast<long double>(p.config.first_block_center_seconds) +
                    static_cast<long double>(j) * p.config.block_seconds;
                const auto phase = std::remainder(hf_public_phase_radians(p, model, t), 2 * pi);
                values[j] = std::polar(1., static_cast<double>(-phase));
            }
        };
        // The zero phase is prepared through the very same polar(-0) path as
        // the original CFO evaluator, preserving even signed-zero arithmetic.
        prepare(HfPublicPhaseFamily::cfo, 0, 0);
        for (std::size_t i = 1; i < p.family_model_counts[2]; ++i)
            prepare(HfPublicPhaseFamily::sinusoid, i, i);
        for (std::size_t i = 0; i < p.family_model_counts[1]; ++i)
            if (i != p.affine_slope_radius)
                prepare(HfPublicPhaseFamily::affine, i, unique_id(HfPublicPhaseFamily::affine, i));
    }

    std::size_t unique_id(HfPublicPhaseFamily family, std::size_t index) const {
        if (family == HfPublicPhaseFamily::cfo) return 0;
        if (family == HfPublicPhaseFamily::sinusoid) return index;
        if (index == plan.affine_slope_radius) return 0;
        return plan.family_model_counts[2] + index - (index > plan.affine_slope_radius ? 1 : 0);
    }
};

HfPreparedPublicStatistics::HfPreparedPublicStatistics(const HfPublicStatisticsPlan& p, std::stop_token stop) {
    const auto begin = Clock::now();
    cancelled(stop);
    require(same_plan(p, hf_public_statistics_plan(p.config)), "HF public statistics plan was modified");
    impl_ = std::make_unique<Impl>(p, stop);
    impl_->preparation_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
}
HfPreparedPublicStatistics::~HfPreparedPublicStatistics() = default;
HfPreparedPublicStatistics::HfPreparedPublicStatistics(HfPreparedPublicStatistics&&) noexcept = default;
HfPreparedPublicStatistics& HfPreparedPublicStatistics::operator=(HfPreparedPublicStatistics&&) noexcept = default;
const HfPublicStatisticsPlan& HfPreparedPublicStatistics::plan() const {
    require(bool(impl_), "HF public statistics preparation has been moved from");
    return impl_->plan;
}
std::size_t HfPreparedPublicStatistics::shared_workspace_bytes() const {
    require(bool(impl_), "HF public statistics preparation has been moved from");
    return impl_->actual_shared_bytes;
}
double HfPreparedPublicStatistics::preparation_wall_seconds() const {
    require(bool(impl_), "HF public statistics preparation has been moved from");
    return impl_->preparation_seconds;
}
std::uint64_t HfPreparedPublicStatistics::prepared_phase_values() const {
    return plan().prepared_phase_values;
}

HfPublicStatisticsReport hf_public_statistics(const HfPreparedPublicStatistics& prepared,
    const std::array<std::span<const Complex>, 2>& q, const std::array<double, 2>& energy,
    std::stop_token stop) {
    const auto begin = Clock::now();
    cancelled(stop);
    const auto& p = prepared.plan();
    for (std::size_t bit = 0; bit < 2; ++bit) {
        require(q[bit].size() == p.config.block_count, "HF public statistics block count mismatch");
        require(std::isfinite(energy[bit]) && energy[bit] >= 0,
                "HF public statistics template energy must be finite and nonnegative");
        for (std::size_t j = 0; j < q[bit].size(); ++j) {
            if ((j & 4095U) == 0) cancelled(stop);
            require(std::isfinite(q[bit][j].real()) && std::isfinite(q[bit][j].imag()),
                    "HF public statistics correlations must be finite");
            require(energy[bit] != 0 || q[bit][j] == Complex{},
                    "HF public statistics zero-energy template has nonzero correlation");
        }
    }
    std::vector<Complex> spectrum(p.fft_rows);
    HfPublicStatisticsReport result;
    result.plan = p;
    require(spectrum.capacity() <= SIZE_MAX / sizeof(Complex), "HF public statistics buffer capacity overflow");
    const auto buffer_bytes = spectrum.capacity() * sizeof(Complex);
    require(buffer_bytes <= SIZE_MAX - overhead_bytes, "HF public statistics actual lane workspace overflow");
    result.actual_lane_workspace_bytes = buffer_bytes + overhead_bytes;
    result.actual_shared_workspace_bytes = prepared.shared_workspace_bytes();
    require(result.actual_lane_workspace_bytes <= p.lane_workspace_bytes &&
            result.actual_shared_workspace_bytes <= SIZE_MAX - result.actual_lane_workspace_bytes,
            "HF public statistics lane allocation exceeds admitted workspace");
    result.actual_workspace_bytes = result.actual_shared_workspace_bytes + result.actual_lane_workspace_bytes;
    require(result.actual_workspace_bytes <= p.workspace_bytes,
            "HF public statistics allocation exceeds admitted workspace");
    std::array<FrequencySummary, 2> zero;
    const auto evaluate = [&](HfPublicPhaseFamily family, std::size_t index, std::size_t bit) {
        cancelled(stop);
        const auto unique = prepared.impl_->unique_id(family, index);
        const auto* rotations = prepared.impl_->rotations.data() + unique * p.config.block_count;
        std::fill(spectrum.begin(), spectrum.end(), Complex{});
        for (std::size_t j = 0; j < q[bit].size(); ++j) {
            if ((j & 4095U) == 0) cancelled(stop);
            spectrum[j] = q[bit][j] * rotations[j];
        }
        datapump::modem::detail::search_fft::fft(std::span(spectrum), false, prepared.impl_->fft, stop);
        const long double e = energy[bit], v = p.config.white_noise_variance;
        const auto offset = -std::log1p(e / v);
        const auto scale = 1 / v / (v + e);
        FrequencySummary summary;
        for (std::size_t i = 0; i < p.frequency_count; ++i) {
            if ((i & 4095U) == 0) cancelled(stop);
            const bool negative = i < p.frequency_radius;
            const auto magnitude = negative ? p.frequency_radius - i : i - p.frequency_radius;
            const auto bin = negative ? p.fft_rows - magnitude : magnitude;
            const std::complex<long double> dot{spectrum[bin].real(), spectrum[bin].imag()};
            const auto score = offset + scale * std::norm(dot);
            finite_double(score);
            summary.log_sum = log_add(summary.log_sum, score);
            if (score > summary.maximum) {
                summary.maximum = score;
                summary.maximum_frequency = finite_double((negative ? -1.L : 1.L) * magnitude *
                    p.frequency_step_hz);
            }
        }
        return summary;
    };
    for (unsigned fi = 0; fi < 3; ++fi) {
        auto& family = result.families[fi];
        if (!p.family_model_counts[fi]) continue;
        const auto start = Clock::now();
        family.evaluated = true;
        std::array<FamilySummary, 2> sum;
        for (std::size_t model = 0; model < p.family_model_counts[fi]; ++model) {
            const bool zero_model = fi == 0 || (fi == 1 && model == p.affine_slope_radius) ||
                (fi == 2 && model == 0);
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const auto frequency = fi && zero_model ? zero[bit] :
                    evaluate(static_cast<HfPublicPhaseFamily>(fi), model, bit);
                if (fi == 0) zero[bit] = frequency;
                sum[bit].append(frequency, model);
            }
        }
        const auto normalization = std::log(static_cast<long double>(p.frequency_count)) +
            std::log(static_cast<long double>(p.family_model_counts[fi]));
        for (std::size_t bit = 0; bit < 2; ++bit) {
            auto& hypothesis = family.hypotheses[bit];
            hypothesis.log_likelihood_ratio = finite_double(sum[bit].log_sum - normalization);
            hypothesis.maximum_log_likelihood_ratio = finite_double(sum[bit].maximum);
            hypothesis.maximum_frequency_hz = sum[bit].maximum_frequency;
            hypothesis.maximum_model_index = sum[bit].maximum_model;
        }
        family.wall_seconds = std::chrono::duration<double>(Clock::now() - start).count();
    }
    result.evaluator_wall_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    return result;
}

HfPublicStatisticsReport hf_public_statistics(const HfPublicStatisticsPlan& p,
    const std::array<std::span<const Complex>, 2>& q, const std::array<double, 2>& energy,
    std::stop_token stop) {
    const auto begin = Clock::now();
    HfPreparedPublicStatistics prepared(p, stop);
    auto result = hf_public_statistics(prepared, q, energy, stop);
    result.preparation_wall_seconds = prepared.preparation_wall_seconds();
    result.phase_values_prepared = prepared.prepared_phase_values();
    result.evaluator_wall_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    return result;
}
} // namespace appraisal
