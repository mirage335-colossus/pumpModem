#include "triad_receiver_fine.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_basis.hpp"
#include "triad_gaussian_kernel.hpp"
#include "triad_nvfp4_staging.hpp"
#include "search_fft.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>

namespace datapump::triad::v3::receiver_detail {

void pack_direct_gain_statistics(std::span<Complex, 12> correlations, std::span<Complex, 24> gram,
                                  std::span<const unsigned, 2> ranks) noexcept {
    // Each destination precedes all later source entries. A rank-zero
    // block advances the additive posterior without observational evidence.
    for (std::size_t block = 0; block < ranks.size(); ++block) {
        for (std::size_t hypothesis = 0; hypothesis < 3; ++hypothesis) {
            const auto index = 3 * block + hypothesis;
            correlations[index] = ranks[block] ? correlations[2 * index] : Complex{};
            gram[index] = ranks[block] ? gram[4 * index] : Complex{};
        }
    }
}

namespace {
constexpr std::size_t maximum_states = 64, maximum_proposals = 256;
constexpr std::size_t maximum_segment_rows = 1'048'576, maximum_basis_rows = 16'384;
constexpr std::size_t maximum_tapered_modes = 9;
constexpr int maximum_rate_radius = 32;
constexpr double acceleration_ppm_per_second = 10.;
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr std::array<double, receiver_coherence_count> coherence_seconds{.5, 2., infinity};
constexpr std::size_t models = coherence_seconds.size(), components = 2 * models;
constexpr std::size_t slow_models = 2;
constexpr double slow_model_weight = .25;
constexpr double log_two = std::numbers::ln2;
constexpr double tau = 2 * std::numbers::pi;
using FineClock = std::chrono::steady_clock;

double seconds_since(FineClock::time_point began) {
    return std::chrono::duration<double>(FineClock::now() - began).count();
}

struct AffineGainPlan {
    std::array<double, slow_models> coherence{};
    bool stationary_fading = false;
};

AffineGainPlan affine_gain_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                               AffineGainEvaluation evaluation = AffineGainEvaluation::operational) {
    if (!config.time_varying_channel) return {};
    const auto waveform = geometry(slot.format);
    const double cells = 2 / waveform.fine_chip_rate_hz;
    if (cells < 1.) {
        // A stationary and a changing component share the direct template.
        // Keep this single-path alternative when the public delay/Doppler
        // bounds also admit a second tap in the original family.
        if (evaluation == AffineGainEvaluation::operational)
            return {{coherence_seconds[0], coherence_seconds[1]}, true};
        return {};
    }
    // The medium-duration likelihood cannot span two cells at this rate.
    // An upper variation bound also admits slower actual channels. Keep
    // the extra gain scales at least two fine cells long, even when that
    // bound would suggest a shorter coherence. This heuristic does not
    // identify a physical Doppler spectrum from an excursion bound.
    const auto frequency = std::max({config.time_varying_common_doppler_hz,
        config.time_varying_atmospheric_hz, std::abs(config.spread_doppler_hz),
        3 * config.random_doppler_width_hz + config.delay_motion_doppler_bound_hz,
        .1 * config.selective_modulation_rate_hz});
    const auto diffusion = config.relative_phase_diffusion_rad_sqrt_s;
    const auto rate = tau * frequency + .5 * diffusion * diffusion;
    if (!(rate > 0)) return {};
    const auto seconds = std::max(cells, 1 / rate);
    if (!std::isfinite(4 * seconds)) return {};
    return {{seconds, 4 * seconds}};
}

double log_add(double a, double b) {
    if (a == -infinity) return b;
    if (b == -infinity) return a;
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

double finite_energy(std::span<const Complex> values) {
    double energy = 0;
    for (const auto value : values) energy += std::norm(value);
    if (!std::isfinite(energy)) throw Error("Robust projected energy is nonfinite");
    return energy;
}

std::size_t round_rows(double seconds, double sample_rate) {
    const auto value = seconds * sample_rate;
    if (!std::isfinite(value) || value > static_cast<double>(maximum_segment_rows) * 240)
        throw Error("Robust receiver block size exceeds its public bound");
    const auto below = std::floor(value);
    const auto fraction = value - below;
    const auto integer = static_cast<std::size_t>(below);
    return std::max<std::size_t>(1, integer + (fraction > .5 || (fraction == .5 && integer % 2)));
}

struct BlockCalendar {
    std::size_t rows, block_rows, segment_rows;
    std::size_t final_first, final_rows;
    BlockCalendar(const ReceiverConfig& config, const ReceiverSlotPlan& plan)
        : BlockCalendar(plan, fine_block_geometry(config, plan)) {}
    BlockCalendar(const ReceiverSlotPlan& plan, FineBlockGeometry geometry)
        : rows(static_cast<std::size_t>(plan.stop_sample - plan.first_sample)),
          block_rows(geometry.block_rows), segment_rows(geometry.segment_rows) {
        if (!rows) throw Error("Robust receiver observation has no samples");
        final_rows = rows % segment_rows;
        if (!final_rows) final_rows = std::min(rows, segment_rows);
        else if (rows >= segment_rows && final_rows < block_rows) {
            // The complete public extent determines this merge, before any
            // samples are exposed. A tiny remainder cannot independently
            // resolve otherwise disjoint DATA and Helper bands. Absorb it
            // into the preceding segment without dropping or padding input.
            final_rows = checked_add(segment_rows, final_rows);
        }
        final_first = rows - final_rows;
        const auto largest = final_first ? std::max(segment_rows, final_rows) : final_rows;
        if (largest > maximum_segment_rows)
            throw Error("Robust receiver segment exceeds 1048576 samples");
    }

    std::size_t segment_length(std::size_t first) const {
        if (first > final_first || first % segment_rows)
            throw Error("Robust receiver segment is outside its public calendar");
        return first == final_first ? final_rows : segment_rows;
    }

    std::size_t block_count(std::size_t segment_length) const {
        return std::max<std::size_t>(1, segment_length / block_rows);
    }

    std::size_t block_length(std::size_t segment_length, std::size_t block) const {
        return block + 1 == block_count(segment_length) ? segment_length - block * block_rows : block_rows;
    }

    std::array<std::size_t, 4> lengths() const {
        std::array<std::size_t, 4> values{};
        std::size_t used = 0;
        const auto add = [&](std::size_t segment) {
            if (!segment) return;
            const auto count = block_count(segment);
            for (const auto i : {std::size_t{0}, count - 1}) {
                const auto length = block_length(segment, i);
                if (std::find(values.begin(), values.end(), length) == values.end())
                    values.at(used++) = length;
            }
        };
        if (final_first) add(segment_rows);
        add(final_rows);
        return values;
    }
};

// Orthogonal DFT bins on the complete block. These are the same DATA bands
// and excluded Helper/auxiliary bands used by core_projection_bands.
struct ProjectionBands {
    FrequencyInterval required;
    std::array<FrequencyInterval, 3> excluded{};
    bool use_exclusions;
    bool omit_overlapping_modes;
    bool taper_native;
    bool taper_cell;
    bool float_projection;
    double sample_rate;

    enum class Policy { legacy, separable_varying };

    static bool can_taper_helper(const ReceiverConfig& config) {
        return config.helper_enabled && !config.time_varying_channel && !config.helper.pilot_dwell_gating &&
            !config.physical_rf_retuning && (!config.bulk_backend || !config.bulk_backend->supports_fine());
    }

    ProjectionBands(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                    Policy policy = Policy::legacy, bool cell_geometry = false)
        : use_exclusions(config.helper_enabled), omit_overlapping_modes(policy == Policy::separable_varying),
          taper_native(policy == Policy::legacy && can_taper_helper(config)),
          taper_cell(cell_geometry),
          float_projection(policy == Policy::legacy && !config.helper_enabled && config.time_varying_channel &&
                           (config.spread_delay_seconds || config.spread_doppler_hz) &&
                           config.native_coarse_precision != NativeCoarsePrecision::fp64 &&
                           (!config.bulk_backend || !config.bulk_backend->supports_fine()) &&
                           config.sample_rate_hz == 6000 &&
                           std::string_view(modem::detail::search_fft::kernel_name()) == "avx2"),
          sample_rate(config.sample_rate_hz) {
        const bool varying = policy != Policy::legacy;
        const double excursion = std::abs(config.spread_doppler_hz) +
            3 * config.random_doppler_width_hz + config.delay_motion_doppler_bound_hz;
        const double clock = config.clock_bound_ppm * 1e-6;
        const double selective = varying ? config.selective_modulation_rate_hz : 0;
        const double common = excursion + config.frequency_bound_hz + selective +
            (varying ? config.time_varying_atmospheric_hz : 0);
        const double skirts = varying ? config.source_spectral_skirt_allowance_hz : 0;
        const double half = geometry(slot.format).occupied_bandwidth_hz * .5 * (1 + clock) + common + skirts;
        required = {-half, half};
        if (use_exclusions) {
            const double gating = config.helper.pilot_dwell_gating ? 200. : 0.;
            const auto band = [&](double center, double width) {
                center -= config.data_if_hz;
                const auto radius = width + common + std::abs(center) * clock;
                return FrequencyInterval{center - radius, center + radius};
            };
            excluded = {band(config.helper.low_frequency_hz, gating),
                        band(config.helper.frequency_hz, gating),
                        band(config.helper.auxiliary_frequency_hz, 200. * (1 + clock))};
        }
        const auto inside = [&](const FrequencyInterval& band) {
            return band.lower_hz > -sample_rate / 2 && band.upper_hz < sample_rate / 2;
        };
        if (!inside(required)) throw Error("Robust DATA projection lies outside receiver Nyquist");
        if (use_exclusions) {
            for (const auto& band : excluded) {
                if (!inside(band))
                    throw Error("Robust Helper projection exclusion lies outside receiver Nyquist");
                if (policy != Policy::separable_varying &&
                    std::max(required.lower_hz, band.lower_hz) <=
                    std::min(required.upper_hz, band.upper_hz))
                    throw Error("Robust DATA projection overlaps a public Helper exclusion");
            }
        }
    }

    bool taper(std::size_t rows, std::size_t modes) const {
        if (!taper_native || rows < 10 || modes > maximum_tapered_modes || modes % 2 == 0 ||
            (modes > 1 && !taper_cell)) return false;
        const auto edge_bandwidth = sample_rate / (rows / 10) + (modes / 2) * sample_rate / rows;
        return std::ranges::none_of(excluded, [&](const auto& band) {
            return band.lower_hz <= edge_bandwidth && band.upper_hz >= -edge_bandwidth;
        });
    }

    // This complete projection shape has paired wanted, quiet, foreign-key
    // and changing-channel measurements with matching admission decisions.
    bool use_float(std::size_t rows, std::size_t modes) const {
        return float_projection && rows == 1500 && modes == 47;
    }
};

// Use the identical discrete-bin predicate for admission and construction.
// A continuous gap between public bands is not enough when a short DFT bin
// intersects both. The admission visitor stores nothing and allocates nothing.
template<class Store>
std::size_t visit_projection_modes(std::size_t rows, const ProjectionBands& bands,
                                  std::stop_token stop, Store store) {
    const double spacing = bands.sample_rate / static_cast<double>(rows);
    const double half_bin = spacing / 2;
    std::size_t modes = 0;
    for (std::size_t index = 0; index < rows; ++index) {
        if (!(index % 4096)) check_cancelled(stop);
        const auto bin = index < (rows + 1) / 2 ? static_cast<std::int64_t>(index) :
            static_cast<std::int64_t>(index) - static_cast<std::int64_t>(rows);
        const auto hz = static_cast<double>(bin) * spacing;
        if (hz < bands.required.lower_hz - half_bin || hz > bands.required.upper_hz + half_bin)
            continue;
        bool excluded = false;
        if (bands.use_exclusions) {
            for (const auto& band : bands.excluded) {
                if (hz >= band.lower_hz - half_bin && hz <= band.upper_hz + half_bin) {
                    if (!bands.omit_overlapping_modes)
                        throw Error("Robust likelihood block cannot resolve DATA and Helper bands");
                    excluded = true;
                    break;
                }
            }
        }
        if (excluded) continue;
        store(index);
        ++modes;
    }
    if (!modes) throw Error("Robust likelihood block has no observed DATA modes");
    return modes;
}

using FineFloatComplex = std::complex<float>;

class FloatProjection {
public:
    static constexpr std::size_t rows = 1500, scratch_rows = 4096;
    explicit FloatProjection(std::stop_token stop) : fft_(scratch_rows, stop) {
        std::size_t phase = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            chirp_[i] = std::polar(1., -std::numbers::pi * static_cast<double>(phase) / rows);
            kernel_[i] = std::conj(chirp_[i]);
            if (i) kernel_[scratch_rows - i] = kernel_[i];
            phase = (phase + 2 * i + 1) % (2 * rows);
        }
        modem::detail::search_fft::fft(kernel_, false, fft_, stop);
    }

    static constexpr std::size_t workspace_bytes() {
        return sizeof(FloatProjection) + (scratch_rows - 1) * sizeof(FineFloatComplex);
    }

    bool apply(std::span<Complex> values, std::span<const std::size_t> indices, double normalization,
               std::span<FineFloatComplex> scratch, std::stop_token stop) const {
        if (values.size() != rows || scratch.size() < scratch_rows)
            throw Error("Robust FP32 projection differs from its admitted workspace");
        check_cancelled(stop);
        scratch = scratch.first(scratch_rows);
        // Bound all intermediate FFT products before narrowing. The full
        // precision plan is available for exceptional input amplitudes.
        constexpr double maximum = std::numeric_limits<float>::max() /
            (8. * scratch_rows * scratch_rows * scratch_rows);
        constexpr double minimum = 8. * scratch_rows * std::numeric_limits<float>::min();
        double peak = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            const auto magnitude = std::max(std::abs(values[i].real()), std::abs(values[i].imag()));
            if (magnitude > maximum) return false;
            peak = std::max(peak, magnitude);
            scratch[i] = static_cast<FineFloatComplex>(values[i]) * chirp_[i];
        }
        if (peak > 0 && peak < minimum) return false;
        std::ranges::fill(scratch.subspan(rows), FineFloatComplex{});
        using namespace modem::detail::search_fft;
        fft(scratch, false, fft_, stop);
        multiply_spectra(scratch, scratch, kernel_, stop);
        fft_prefix(scratch, true, fft_, rows, stop);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto index = indices[i];
            values[i] = static_cast<Complex>(scratch[index] * chirp_[index]) * normalization;
        }
        return true;
    }

private:
    modem::detail::search_fft::FloatPlan fft_;
    std::array<FineFloatComplex, rows> chirp_{};
    std::array<FineFloatComplex, scratch_rows> kernel_{};
};

class Projection {
public:
    Projection(std::size_t rows, const ProjectionBands& bands, std::stop_token stop)
        : rows_(rows), normalization_(1 / std::sqrt(static_cast<double>(rows))) {
        indices_.reserve(rows);
        visit_projection_modes(rows, bands, stop, [&](std::size_t index) { indices_.push_back(index); });
        if (bands.taper(rows, indices_.size())) {
            prepare_taper(stop);
        } else if (indices_.size() != 1 || indices_.front() != 0) {
            plan_.emplace(rows, stop);
            if (bands.use_float(rows, indices_.size())) float_plan_ = std::make_unique<FloatProjection>(stop);
        }
    }

    std::size_t rows() const { return rows_; }
    std::size_t modes() const { return indices_.size(); }
    std::size_t float_scratch_rows() const { return float_plan_ ? FloatProjection::scratch_rows : 0; }
    std::span<const std::size_t> indices() const { return indices_; }
    // For a device transform of this projection: its DFT plan, null for a
    // tapered or DC-only projection, and the unitary scale of its modes.
    const DftPlan* plan() const noexcept { return plan_ ? &*plan_ : nullptr; }
    double normalization() const noexcept { return normalization_; }

    // In the original DFT order with unitary normalization. A caller
    // applying a circular frequency-bin shift needs every template bin,
    // including those outside the final observed subspace.
    void transform_full(std::span<Complex> values, std::span<Complex> scratch,
                        std::stop_token stop) const {
        if (!plan_) throw Error("Robust full DFT requires a nontrivial DATA projection");
        plan_->transform(values, false, scratch, stop);
        for (auto& value : values) value *= normalization_;
    }

    // Destructive only on caller-private render scratch.
    void apply(std::span<Complex> values, std::span<Complex> scratch, std::stop_token stop,
               std::span<FineFloatComplex> float_scratch = {}) const {
        if (float_plan_ && float_plan_->apply(values, indices_, normalization_, float_scratch, stop)) return;
        if (!tapered_modes_.empty()) {
            const auto projected = apply_taper(values, stop);
            std::ranges::copy(std::span(projected).first(modes()), values.begin());
            return;
        }
        if (!plan_) {
            values[0] = sum_dc(values, stop) * normalization_;
            return;
        }
        plan_->transform(values, false, scratch, stop);
        // Sorted indices make the in-place compact gather safe.
        for (std::size_t i = 0; i < indices_.size(); ++i)
            values[i] = values[indices_[i]] * normalization_;
    }

    // Orthogonal projection in the original sample coordinates. The same
    // unitary subspace is used by the projected likelihood. This inverse is
    // for an inexpensive matched-product proposal, never an assumption of
    // independent time-domain noise.
    void retain_time(std::span<Complex> values, std::span<Complex> scratch, std::stop_token stop) const {
        if (!tapered_modes_.empty()) {
            const auto projected = apply_taper(values, stop);
            std::ranges::fill(values, Complex{});
            for (std::size_t mode = 0; mode < modes(); ++mode) {
                check_cancelled(stop);
                const auto weights = taper_row(mode);
                for (std::size_t i = 0; i < rows_; ++i) values[i] += projected[mode] * weights[i];
            }
            return;
        }
        if (!plan_) {
            const auto dc = sum_dc(values, stop);
            std::ranges::fill(values, dc / static_cast<double>(rows_));
            return;
        }
        plan_->transform(values, false, scratch, stop);
        std::size_t keep = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!(i % 4096)) check_cancelled(stop);
            if (keep < indices_.size() && i == indices_[keep]) ++keep;
            else values[i] = {};
        }
        plan_->transform(values, true, scratch, stop);
    }

private:
    static Complex sum_dc(std::span<const Complex> values, std::stop_token stop) {
        Complex dc{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!(i % 4096)) check_cancelled(stop);
            dc += values[i];
        }
        return dc;
    }

    std::span<const double> taper_row(std::size_t mode) const {
        return std::span(tapered_modes_).subspan(mode * rows_, rows_);
    }

    void prepare_taper(std::stop_token stop) {
        // Taper a real DC/cosine/sine basis for the same symmetric DFT bins.
        // Orthogonalize the windowed modes so projected white noise remains
        // independent with unit variance in each mode.
        tapered_modes_.resize(checked_multiply(rows_, modes()));
        const auto row = [&](std::size_t mode) {
            return std::span(tapered_modes_).subspan(mode * rows_, rows_);
        };
        const auto window = row(0);
        const auto edge_rows = rows_ / 10;
        for (std::size_t i = 0; i < rows_; ++i) {
            if (!(i % 4096)) check_cancelled(stop);
            const auto distance = std::min(i, rows_ - 1 - i);
            window[i] = distance >= edge_rows ? 1. :
                .5 - .5 * std::cos(std::numbers::pi * (distance + .5) / edge_rows);
        }
        for (std::size_t mode = 0; mode < modes(); ++mode) {
            check_cancelled(stop);
            auto weights = row(mode);
            if (mode) {
                const auto harmonic = (mode + 1) / 2;
                for (std::size_t i = 0; i < rows_; ++i) {
                    const auto angle = tau * harmonic * (i - .5 * (rows_ - 1)) / rows_;
                    weights[i] = window[i] * (mode % 2 ? std::cos(angle) : std::sin(angle));
                }
                for (std::size_t prior = 0; prior < mode; ++prior) {
                    const auto basis = row(prior);
                    const auto overlap = std::inner_product(weights.begin(), weights.end(), basis.begin(), 0.);
                    for (std::size_t i = 0; i < rows_; ++i) weights[i] -= overlap * basis[i];
                }
            }
            const auto norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.);
            if (!(norm > 0) || !std::isfinite(norm)) throw Error("Robust Helper taper cannot resolve DATA modes");
            const auto scale = 1 / std::sqrt(norm);
            for (auto& weight : weights) weight *= scale;
        }
    }

    std::array<Complex, maximum_tapered_modes> apply_taper(std::span<const Complex> values,
                                                          std::stop_token stop) const {
        std::array<Complex, maximum_tapered_modes> result{};
        for (std::size_t mode = 0; mode < modes(); ++mode) {
            check_cancelled(stop);
            const auto weights = taper_row(mode);
            for (std::size_t i = 0; i < rows_; ++i) result[mode] += values[i] * weights[i];
        }
        return result;
    }

    std::size_t rows_;
    double normalization_;
    std::vector<std::size_t> indices_;
    std::vector<double> tapered_modes_;
    std::optional<DftPlan> plan_;
    std::unique_ptr<FloatProjection> float_plan_;
};

struct ChannelPosterior {
    std::array<Complex, 2> mean{};
    // Two spatial taps store a persistent covariance root. One spatial
    // tap and the separate additive latent model store covariance.
    GaussianRoot2 uncertainty{};
};

double initial_gain_diagonal(std::size_t spatial_taps, double variance) {
    return spatial_taps == 2 ? std::sqrt(variance) : variance;
}

struct State {
    FineStateKey key;
    std::array<double, components> weights{};
    std::array<ChannelPosterior, components> posterior{};
    double prior_mass = 0;
    bool nonzero_basis = false;
};

struct SlowGainState {
    std::array<double, 2 * slow_models> weights{};
    std::array<ChannelPosterior, 2 * slow_models> posterior{};
};

using SlowGainTransitions = std::array<double, 4 * 2 * slow_models>;
constexpr SlowGainTransitions no_slow_transitions{};

double rank(const std::array<double, components>& weights) {
    return std::accumulate(weights.begin(), weights.end(), -infinity, log_add);
}

struct RankedIndex {
    double rank;
    std::uint32_t index;
};

// Keeps the `count` best-ranked elements, ordered by key. Each rank is
// evaluated once: it is a log-sum over every channel component.
template<typename T>
void retain(std::vector<T>& states, std::size_t count, std::vector<RankedIndex>& ranking) {
    if (states.size() <= count) return;
    ranking.resize(states.size());
    for (std::size_t i = 0; i < states.size(); ++i)
        ranking[i] = {rank(states[i].weights), static_cast<std::uint32_t>(i)};
    std::nth_element(ranking.begin(), ranking.begin() + static_cast<std::ptrdiff_t>(count), ranking.end(),
        [&](const RankedIndex& a, const RankedIndex& b) {
            return a.rank != b.rank ? a.rank > b.rank : states[a.index].key < states[b.index].key;
        });
    // Survivors move down in their original order, which is already by key
    // for every caller. No destination has a source left to read.
    std::sort(ranking.begin(), ranking.begin() + static_cast<std::ptrdiff_t>(count),
        [](const RankedIndex& a, const RankedIndex& b) { return a.index < b.index; });
    for (std::size_t i = 0; i < count; ++i)
        if (ranking[i].index != i) states[i] = std::move(states[ranking[i].index]);
    states.resize(count);
    const auto by_key = [](const T& a, const T& b) { return a.key < b.key; };
    if (!std::is_sorted(states.begin(), states.end(), by_key)) std::sort(states.begin(), states.end(), by_key);
}

struct TimingGrid {
    double delay_step_seconds, rate_step_ppm;
    int radius;

    TimingGrid(double projected_rate, double coarse_gap)
        : delay_step_seconds(1 / (2 * projected_rate)), rate_step_ppm(5), radius(0) {
        const double extent = coarse_gap / 4;
        if (!std::isfinite(delay_step_seconds) || delay_step_seconds <= 0 ||
            !std::isfinite(extent) || extent < 0 || extent > 5 * maximum_rate_radius)
            throw Error("Robust residual timing grid exceeds its bounded radius");
        if (extent) {
            radius = std::max(1, static_cast<int>(std::ceil(extent / rate_step_ppm)));
            rate_step_ppm = extent / radius;
        }
    }

    std::vector<State> initial(std::size_t coordinates, std::size_t taps, bool cold_maximum) const {
        const auto count = coordinates * 3 * (radius ? 3 : 1);
        if (!count || count > maximum_proposals)
            throw Error("Robust initial timing bank exceeds the proposal cap");
        std::vector<State> states;
        states.reserve(cold_maximum ? maximum_proposals : count);
        const auto append = [&](FineStateKey key) {
            State state;
            state.key = key;
            states.push_back(state);
        };
        for (unsigned group = 0; group < coordinates; ++group)
            for (int delay = -1; delay <= 1; ++delay) {
                if (radius) append({group, delay, -radius});
                append({group, delay, 0});
                if (radius) append({group, delay, radius});
            }
        if (cold_maximum) {
            // Fill the same legal rate/delay lattice as the normal lane. The
            // extra states exist solely to exercise the cold DSP ceiling.
            for (int extent = 1; states.size() < maximum_proposals; ++extent) {
                for (unsigned group = 0; group < coordinates && states.size() < maximum_proposals; ++group) {
                    const auto delays = extent == 1 ? std::array{-1, 0, 1} : std::array{-extent, extent, extent};
                    for (const auto delay : std::span(delays).first(extent == 1 ? 3 : 2)) {
                        for (int rate = -radius; rate <= radius && states.size() < maximum_proposals; ++rate) {
                            const FineStateKey key{group, delay, rate};
                            if (extent != 1 || std::none_of(states.begin(), states.end(),
                                    [&](const State& state) { return state.key == key; })) append(key);
                        }
                    }
                }
            }
            std::sort(states.begin(), states.end(), [](const State& a, const State& b) { return a.key < b.key; });
        }
        const auto log_prior = -std::log(static_cast<double>(states.size())) - std::log(static_cast<double>(models));
        for (auto& state : states) {
            state.prior_mass = 1. / static_cast<double>(states.size());
            state.weights.fill(log_prior);
            for (auto& channel : state.posterior)
                for (std::size_t tap = 0; tap < taps; ++tap)
                    channel.uncertainty[tap * taps + tap] = initial_gain_diagonal(taps, 1. / static_cast<double>(taps));
        }
        return states;
    }
};

struct TransitionEdge {
    FineStateKey target;
    std::size_t source;
    double log_probability;
    Complex phase;
};

struct TransitionTarget {
    FineStateKey key;
    std::array<double, components> weights;
    std::size_t first_edge, stop_edge;
    double prior_mass = 0;
};

void append_edges(std::vector<TransitionEdge>& edges, const State& source, std::size_t index,
                  const TimingGrid& grid, double elapsed, double boundary, double center) {
    const auto rate_span = static_cast<int>(std::min<double>(2 * grid.radius, std::floor(
        std::nextafter(acceleration_ppm_per_second * elapsed / grid.rate_step_ppm, infinity))));
    const auto first_rate = std::max(-grid.radius, source.key.rate - rate_span);
    const auto last_rate = std::min(grid.radius, source.key.rate + rate_span);
    const auto probability = 1. / (last_rate - first_rate + 1);
    for (int next_rate = first_rate; next_rate <= last_rate; ++next_rate) {
        const double travel = .5 * (source.key.rate + next_rate) * grid.rate_step_ppm * 1e-6 *
            elapsed / grid.delay_step_seconds;
        const double integral = std::floor(travel);
        if (!std::isfinite(integral) || std::abs(integral) >= std::ldexp(1., 62))
            throw Error("Robust adaptive timing displacement overflows");
        const auto shift = static_cast<std::int64_t>(integral);
        if ((shift > 0 && source.key.delay >= INT64_MAX - shift) ||
            (shift < 0 && source.key.delay < INT64_MIN - shift))
            throw Error("Robust adaptive timing offset overflows");
        const auto lower = source.key.delay + shift;
        const auto fraction = travel - integral;
        const auto add = [&](std::int64_t target, double p) {
            if (!p) return;
            const auto delta = (source.key.delay - target) * grid.delay_step_seconds +
                (source.key.rate * boundary + next_rate * (elapsed - boundary)) * grid.rate_step_ppm * 1e-6;
            const auto phase = std::polar(1., tau * std::remainder(center * delta, 1.));
            edges.push_back({{source.key.group, target, next_rate}, index, std::log(p), phase});
        };
        if (fraction < 1) add(lower, probability * (1 - fraction));
        if (fraction > 0) {
            if (lower == INT64_MAX) throw Error("Robust adaptive timing offset overflows");
            add(lower + 1, probability * fraction);
        }
    }
}

std::vector<State> predict(const std::vector<State>& previous, const TimingGrid& grid,
                           double elapsed, double boundary, double center, std::size_t taps,
                           std::uint64_t& edges_scored, FineLaneWork* work, std::vector<RankedIndex>& ranking,
                           std::stop_token stop) {
    std::vector<TransitionEdge> edges;
    edges.reserve(2 * previous.size() * (2 * grid.radius + 1));
    for (std::size_t i = 0; i < previous.size(); ++i) {
        check_cancelled(stop);
        append_edges(edges, previous[i], i, grid, elapsed, boundary, center);
    }
    if (edges.empty()) throw Error("Robust adaptive timing prior lost every state");
    if (edges.size() > UINT64_MAX - edges_scored) throw Error("Robust adaptive transition count overflows");
    edges_scored += edges.size();
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return a.target != b.target ? a.target < b.target : a.source < b.source;
    });

    std::vector<TransitionTarget> targets;
    targets.reserve(edges.size());
    for (std::size_t i = 0; i < edges.size();) {
        if (!(i % 256)) check_cancelled(stop);
        TransitionTarget target{edges[i].target, {}, i, i};
        target.weights.fill(-infinity);
        do {
            const auto& edge = edges[i++];
            for (std::size_t c = 0; c < components; ++c) {
                const auto weight = previous[edge.source].weights[c] + edge.log_probability;
                target.weights[c] = log_add(target.weights[c], weight);
            }
            if (work) target.prior_mass += previous[edge.source].prior_mass * std::exp(edge.log_probability);
        } while (i < edges.size() && edges[i].target == target.key);
        target.stop_edge = i;
        targets.push_back(target);
    }
    if (work) {
        work->peak_transition_targets = std::max(work->peak_transition_targets, targets.size());
        if (targets.size() > maximum_proposals) {
            ++work->pruning_events;
            work->states_dropped += targets.size() - maximum_proposals;
        }
    }
    retain(targets, maximum_proposals, ranking);

    std::vector<State> result;
    result.reserve(targets.size());
    for (const auto& target : targets) {
        check_cancelled(stop);
        State state;
        state.key = target.key;
        state.weights = target.weights;
        if (work) {
            state.prior_mass = target.prior_mass;
            work->merged_channel_components += target.stop_edge - target.first_edge - 1;
        }
        for (std::size_t c = 0; c < components; ++c) {
            auto& out = state.posterior[c];
            const auto weight = [&](const TransitionEdge& edge) {
                return std::exp(previous[edge.source].weights[c] + edge.log_probability - target.weights[c]);
            };
            for (auto i = target.first_edge; i < target.stop_edge; ++i) {
                const auto& edge = edges[i];
                const auto& in = previous[edge.source].posterior[c];
                const auto p = weight(edge);
                for (std::size_t k = 0; k < taps; ++k) out.mean[k] += p * in.mean[k] * edge.phase;
            }
            if (taps == 2 && target.stop_edge == target.first_edge + 1) {
                out.uncertainty = previous[edges[target.first_edge].source].posterior[c].uncertainty;
                continue;
            }
            for (auto i = target.first_edge; i < target.stop_edge; ++i) {
                const auto& edge = edges[i];
                const auto& in = previous[edge.source].posterior[c];
                const auto p = weight(edge);
                std::array<Complex, 2> difference{};
                for (std::size_t k = 0; k < taps; ++k)
                    difference[k] = in.mean[k] * edge.phase - out.mean[k];
                if (taps == 2) {
                    const auto root_weight = std::sqrt(p);
                    append_gaussian_root_column(out.uncertainty,
                        root_weight * in.uncertainty[0], root_weight * in.uncertainty[2]);
                    append_gaussian_root_column(out.uncertainty,
                        root_weight * in.uncertainty[1], root_weight * in.uncertainty[3]);
                    append_gaussian_root_column(out.uncertainty,
                        root_weight * difference[0], root_weight * difference[1]);
                } else {
                    out.uncertainty[0] += p *
                        (in.uncertainty[0] + difference[0] * std::conj(difference[0]));
                }
            }
            if (taps == 1) out.uncertainty[0] = out.uncertainty[0].real();
        }
        result.push_back(state);
    }
    return result;
}

struct ObservationBlock {
    const Projection* projection = nullptr;
    std::vector<Complex> projected;
    std::span<const Complex> line;
    std::size_t first = 0;
    double elapsed = 0;
    bool observed = true;
};

unsigned highest_observation_rank(const ObservationBlock& block) {
    return block.observed ? static_cast<unsigned>(std::min<std::size_t>(2,
        block.projected.size() - !block.line.empty())) : 0;
}

void remove_line(std::span<Complex> values, std::span<const Complex> unit_line) {
    Complex component{};
    for (std::size_t i = 0; i < unit_line.size(); ++i) component += std::conj(unit_line[i]) * values[i];
    for (std::size_t i = 0; i < unit_line.size(); ++i) values[i] -= unit_line[i] * component;
}

std::span<const Complex> project_line(const LineAtom& line, const Projection& projection,
        std::size_t first, double fs, std::span<Complex> values, std::span<Complex> scratch,
        std::stop_token stop) {
    const auto begin = std::max(first, line.first);
    const auto end = std::min(first + projection.rows(), line.stop);
    if (begin >= end) return {};
    std::ranges::fill(values, Complex{});
    const auto increment = std::polar(1., tau * line.frequency_hz / fs);
    Complex phase = 1.;
    for (auto row = begin; row < end; ++row) {
        if (!((row - begin) % 4096)) check_cancelled(stop);
        values[row - first] = phase;
        phase *= increment;
    }
    projection.apply(values, scratch, stop);
    auto mode_values = values.first(projection.modes());
    const auto norm = std::sqrt(finite_energy(mode_values));
    if (!(norm > 1e-12)) return {};
    for (auto& value : mode_values) value /= norm;
    return mode_values;
}

struct WorkerScratch {
    std::vector<Complex> raw, chunk, fft;
    std::vector<double> offsets;
    double basis_statistics_seconds = 0, gaussian_seconds = 0;

    WorkerScratch(std::size_t block_rows, std::size_t fft_rows)
        : raw(4 * block_rows), chunk(block_rows > maximum_basis_rows ? 4 * maximum_basis_rows : 0),
          fft(fft_rows), offsets(std::min(maximum_basis_rows, block_rows)) {
        std::iota(offsets.begin(), offsets.end(), 0.);
    }
};

// Render the full block into column-major storage through bounded basis requests.
void render_templates_into(MappedBasis& basis, const MappedBasisRow& row, std::span<const Tap> taps,
                           std::size_t block_first, std::size_t rows, std::span<Complex> out, WorkerScratch& scratch,
                           std::stop_token stop) {
    const auto columns = 2 * taps.size();
    for (std::size_t first = 0; first < rows; first += maximum_basis_rows) {
        const auto count = std::min(maximum_basis_rows, rows - first);
        auto rendered = count == rows ? out.first(rows * columns) : std::span(scratch.chunk).first(count * columns);
        basis.render_pair(row, taps, block_first + first, std::span(scratch.offsets).first(count), rendered, stop);
        if (count != rows) {
            for (std::size_t column = 0; column < columns; ++column)
                std::copy_n(rendered.begin() + static_cast<std::ptrdiff_t>(column * count), count,
                            out.begin() + static_cast<std::ptrdiff_t>(column * rows + first));
        }
    }
}

std::span<Complex> render_templates(MappedBasis& basis, const MappedBasisRow& row, std::span<const Tap> taps,
                                   std::size_t first, std::size_t rows, WorkerScratch& scratch, std::stop_token stop) {
    auto raw = std::span(scratch.raw).first(2 * taps.size() * rows);
    render_templates_into(basis, row, taps, first, rows, raw, scratch, stop);
    return raw;
}

std::span<Complex> render_templates(MappedBasis& basis, const MappedBasisRow& row, std::span<const Tap> taps,
                                   const ObservationBlock& block, WorkerScratch& scratch, std::stop_token stop) {
    return render_templates(basis, row, taps, block.first, block.projection->rows(), scratch, stop);
}

template<typename Precision>
void projected_statistics(std::span<const std::complex<Precision>> raw, std::size_t stride, std::size_t tap_count,
                          const ObservationBlock& block, double variance,
                          std::span<Complex> correlations, std::span<Complex> gram) {
    for (std::size_t hypothesis = 1; hypothesis <= 2; ++hypothesis) {
        const auto offset = (hypothesis - 1) * tap_count;
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            auto a = raw.subspan((offset + tap) * stride, block.projected.size());
            Complex correlation{};
            for (std::size_t i = 0; i < a.size(); ++i) correlation += std::conj(Complex(a[i])) * block.projected[i];
            correlations[hypothesis * tap_count + tap] = correlation / variance;
            const auto start = hypothesis * tap_count * tap_count;
            for (std::size_t other = tap; other < tap_count; ++other) {
                auto b = raw.subspan((offset + other) * stride, block.projected.size());
                Complex product{};
                for (std::size_t i = 0; i < a.size(); ++i) product += std::conj(Complex(a[i])) * Complex(b[i]);
                product /= variance;
                gram[start + tap * tap_count + other] = product;
                gram[start + other * tap_count + tap] = std::conj(product);
            }
        }
    }
}

void sufficient_statistics(MappedBasis& basis, const MappedBasisRow& row, std::span<const Tap> taps,
                           const ObservationBlock& block, double variance, WorkerScratch& scratch,
                           std::span<Complex> correlations, std::span<Complex> gram,
                           bool& nonzero_basis, std::stop_token stop,
                           std::span<FineFloatComplex> float_scratch = {}) {
    const auto rows = block.projection->rows();
    const auto raw = render_templates(basis, row, taps, block, scratch, stop);
    for (std::size_t column = 0; column < 2 * taps.size(); ++column) {
        auto values = raw.subspan(column * rows, rows);
        // Required for a complete receipt even on slots that contain only noise.
        if (!nonzero_basis) nonzero_basis = finite_energy(values) > 0;
        block.projection->apply(values, scratch.fft, stop, float_scratch);
        remove_line(values, block.line);
    }
    projected_statistics(std::span<const Complex>(raw), rows, taps.size(), block, variance, correlations, gram);
}

MappedBasisRow mapped_row(const ReceiverConfig& config, const ReceiverSlotPlan& plan,
                          const TimingGrid& grid, const FineCoordinate& coordinate, const FineStateKey& key,
                          long double anchor, bool fixed) {
    const double base = 1 + coordinate.clock_ppm * 1e-6;
    const double velocity = key.rate * grid.rate_step_ppm * 1e-6;
    const double scale = base + velocity;
    if (scale <= 0 || !std::isfinite(scale)) throw Error("Robust fine source rate is invalid");
    const long double displacement = key.delay * grid.delay_step_seconds;
    const auto arrival = (base * (coordinate.arrival_seconds + plan.window_capture_lead_seconds) +
                          plan.source_start_seconds - displacement + velocity * anchor) / scale;
    return {coordinate.epoch_offset_seconds, static_cast<double>(arrival),
            coordinate.frequency_hz - config.data_if_hz * ((fixed ? scale : base) - 1),
            (scale - 1) * 1e6};
}

struct StateBlockWork {
    std::uint64_t positive_basis_blocks = 0, gaussian_null_skips = 0;
    long double template_energy = 0;
};

// Returns the common-null block updates that were provably inert and skipped.
template <std::size_t Count, class GainState>
std::size_t advance_gain(GainState& state, std::size_t tap_count, std::size_t block_count,
                         const std::array<Complex, 2 * 3 * 2>& correlations,
                         const std::array<Complex, 2 * 3 * 4>& grams, const std::array<unsigned, 2>& ranks,
                         const std::array<double, 2>& powers,
                         const std::array<double, 4 * 2 * Count>& transitions, const std::stop_token& stop,
                         bool stationary_fading = false) {
    const auto dimensions = stationary_fading ? 2 : tap_count;
    const auto state_square = dimensions * dimensions;
    std::array<Complex, Count * 3 * 2> means{};
    std::array<Complex, Count * 3 * 4> uncertainty{};
    std::array<double, Count * 3> scores{};
    std::array<double, 2 * Count> diagnostics{};
    for (std::size_t model = 0; model < Count; ++model) {
        const auto null = model * 3 * state_square;
        for (std::size_t tap = 0; tap < dimensions; ++tap)
            uncertainty[null + tap * dimensions + tap] = initial_gain_diagonal(tap_count, powers[tap]);
        if (stationary_fading) {
            uncertainty[null] += powers[1];
            uncertainty[null + 1] = uncertainty[null + 2] = powers[1];
        }
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto h = model * 3 + bit + 1;
            const auto& channel = state.posterior[model * 2 + bit];
            std::copy_n(channel.mean.begin(), dimensions, means.data() + h * dimensions);
            std::copy_n(channel.uncertainty.begin(), state_square, uncertainty.data() + h * state_square);
        }
    }

    check_cancelled(stop);
    // The common null's statistics, mean and evidence are zero, and only
    // its evidence is read back. The one-tap and factor entries skip it when
    // that provably holds through every block; the additive one runs it.
    const auto correlation_data = reinterpret_cast<const double*>(correlations.data());
    const auto gram_data = reinterpret_cast<const double*>(grams.data());
    const auto mean_data = reinterpret_cast<double*>(means.data());
    const auto uncertainty_data = reinterpret_cast<double*>(uncertainty.data());
    bool null_skipped = false;
    const auto status = tap_count == 2 ?
        gaussian_signal_factor2_update(1, Count, block_count, 0, 1, correlation_data, gram_data,
            ranks.data(), powers.data(), transitions.data(), mean_data, uncertainty_data, scores.data(), diagnostics.data(),
            null_skipped) :
        stationary_fading ?
        triad_gaussian_additive_update_v1(tap_count, 1, Count, block_count, 0, 1, correlation_data, gram_data,
            powers.data(), transitions.data(), mean_data, uncertainty_data, scores.data(), diagnostics.data()) :
        gaussian_signal_update(tap_count, 1, Count, block_count, 0, 1, correlation_data, gram_data,
            powers.data(), transitions.data(), mean_data, uncertainty_data, scores.data(), diagnostics.data(),
            null_skipped);
    if (status) throw Error("Robust fine Gaussian update failed (" + std::to_string(status) + ")");

    for (std::size_t model = 0; model < Count; ++model) {
        if (scores[model * 3] != 0) throw Error("Robust Gaussian common null acquired nonzero evidence");
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto h = model * 3 + bit + 1;
            const auto component = model * 2 + bit;
            auto& channel = state.posterior[component];
            std::copy_n(means.data() + h * dimensions, dimensions, channel.mean.begin());
            std::copy_n(uncertainty.data() + h * state_square, state_square, channel.uncertainty.begin());
            state.weights[component] += scores[h];
            if (!std::isfinite(state.weights[component]))
                throw Error("Robust Gaussian likelihood evidence is nonfinite");
        }
    }
    return null_skipped ? block_count * Count : 0;
}

void advance_state(State& state, const MappedBasisRow& row, MappedBasis& basis,
                   std::span<const Tap> taps, std::span<const ObservationBlock> blocks,
                   const std::array<const FineBulkStatistics*, 2>& prepared,
                   const std::array<double, 4 * 2 * models>& transitions,
                   double variance, WorkerScratch& scratch, StateBlockWork* work, const std::stop_token& stop,
                   double path_power = 1, SlowGainState* slow = nullptr,
                   const SlowGainTransitions& slow_transitions = no_slow_transitions,
                   bool stationary_fading = false, std::span<FineFloatComplex> float_scratch = {}) {
    const auto tap_count = taps.size(), tap_square = tap_count * tap_count;
    std::array<Complex, 2 * 3 * 2> correlations{};
    std::array<Complex, 2 * 3 * 4> grams{};
    std::array<unsigned, 2> ranks{};
    const double tap_power = path_power / tap_count;
    std::array<double, 2> powers{tap_power, tap_power};
    if (work) *work = {};
    const auto basis_began = work ? FineClock::now() : FineClock::time_point{};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].observed) {
            ranks[i] = highest_observation_rank(blocks[i]);
            const auto gram = std::span(grams).subspan(i * 3 * tap_square, 3 * tap_square);
            const auto correlation = std::span(correlations).subspan(i * 3 * tap_count, 3 * tap_count);
            if (const auto* bulk = prepared[i]) {
                std::copy_n(bulk->correlations.begin(), correlation.size(), correlation.begin());
                std::copy_n(bulk->gram.begin(), gram.size(), gram.begin());
                state.nonzero_basis |= bulk->nonzero_basis;
            } else {
                sufficient_statistics(basis, row, taps, blocks[i], variance, scratch,
                    correlation, gram, state.nonzero_basis, stop, float_scratch);
            }
            if (work) {
                long double energy = 0;
                for (std::size_t bit = 1; bit <= 2; ++bit)
                    for (std::size_t tap = 0; tap < tap_count; ++tap)
                        energy += static_cast<long double>(gram[bit * tap_square + tap * tap_count + tap].real()) * variance;
                work->template_energy += energy;
                work->positive_basis_blocks += energy > 0;
            }
        }
    }
    if (work) scratch.basis_statistics_seconds += seconds_since(basis_began);
    const auto gaussian_began = work ? FineClock::now() : FineClock::time_point{};

    auto null_skips = advance_gain<models>(state, tap_count, blocks.size(), correlations, grams, ranks, powers,
                                           transitions, stop);
    if (slow) {
        const std::array split_power{.5 * path_power, .5 * path_power};
        if (stationary_fading && tap_count == 2)
            pack_direct_gain_statistics(correlations, grams, ranks);
        null_skips += advance_gain<slow_models>(*slow, stationary_fading ? 1 : tap_count, blocks.size(), correlations,
            grams, ranks, stationary_fading ? split_power : powers, slow_transitions, stop, stationary_fading);
    }
    if (work) {
        work->gaussian_null_skips = null_skips;
        scratch.gaussian_seconds += seconds_since(gaussian_began);
    }
}

template <class GainState>
std::array<double, 2> bit_scores(const std::vector<GainState>& states) {
    std::array<double, 2> scores{-infinity, -infinity};
    for (const auto& state : states)
        for (std::size_t model = 0; model < state.weights.size() / 2; ++model)
            for (std::size_t bit = 0; bit < 2; ++bit)
                scores[bit] = log_add(scores[bit], state.weights[model * 2 + bit]);
    if (!std::isfinite(scores[0]) || !std::isfinite(scores[1]))
        throw Error("Robust fine tracking did not produce finite bit evidence");
    return scores;
}

unsigned useful_fine_workers(unsigned workers) {
    // One permit belongs to the calling owner. The two lanes expose at most
    // this many distinct independent candidate updates in one dispatch wave.
    return std::min<unsigned>(workers, 2 * maximum_proposals);
}

long double gamma_upper(long double shape, long double x) {
    // Continued fraction for Q(shape,x), evaluated only in the upper tail.
    const auto tiny = std::numeric_limits<long double>::min() / std::numeric_limits<long double>::epsilon();
    auto b = x + 1 - shape, c = 1 / tiny, d = 1 / b, fraction = d;
    for (unsigned i = 1; i <= 100'000; ++i) {
        const long double a = -static_cast<long double>(i) * (i - shape);
        b += 2;
        d = a * d + b;
        c = b + a / c;
        if (std::abs(d) < tiny) d = std::copysign(tiny, d);
        if (std::abs(c) < tiny) c = std::copysign(tiny, c);
        d = 1 / d;
        const auto delta = c * d;
        fraction *= delta;
        if (std::abs(delta - 1) <= 8 * std::numeric_limits<long double>::epsilon())
            return std::exp(-x + shape * std::log(x) - std::lgamma(shape)) * fraction;
    }
    throw Error("Robust quiet noise threshold did not converge");
}
} // namespace

double quiet_noise_threshold(std::size_t modes) {
    if (!modes) throw Error("Robust quiet statistic has no observed modes");
    const long double shape = modes, tail = .001L;
    long double lower = shape, upper = shape + 16 * std::sqrt(shape) + 16;
    if (gamma_upper(shape, upper) > tail) throw Error("Robust quiet noise threshold is outside its bound");
    for (unsigned i = 0; i < 80; ++i) {
        const auto middle = std::midpoint(lower, upper);
        if (middle == lower || middle == upper) break;
        if (gamma_upper(shape, middle) > tail) lower = middle;
        else upper = middle;
    }
    return static_cast<double>(upper);
}

namespace {
// More keyed sources than this keep tile filling on demand.
constexpr std::size_t maximum_fill_coordinates = 16;

constexpr std::size_t dispatch_extent_bytes = 12 * sizeof(std::size_t);
// One dispatch's projected observation, without its mode storage.
constexpr std::size_t dispatch_pair_bytes = 1024;

// A segment holds at most three blocks, so at most two dispatches.
std::size_t maximum_dispatches(const BlockCalendar& calendar) {
    return checked_multiply(2, calendar.rows / calendar.segment_rows + 1);
}

FineObservation whole_slot(const ReceiverSlotPlan& slot) {
    if (slot.stop_sample <= slot.first_sample || slot.stop_sample - slot.first_sample > SIZE_MAX)
        throw Error("invalid public fine observation size");
    return {slot.first_sample, static_cast<std::size_t>(slot.stop_sample - slot.first_sample)};
}

ReceiverSlotPlan observation_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                  FineObservation observation) {
    const auto whole = whole_slot(slot);
    if (!std::isfinite(config.sample_rate_hz) || config.sample_rate_hz < 1 ||
        !std::isfinite(config.noise_variance) || config.noise_variance <= 0 ||
        slot.quiet_first_sample < slot.first_sample || slot.quiet_stop_sample < slot.quiet_first_sample ||
        slot.quiet_stop_sample > slot.stop_sample || !observation.rows ||
        observation.first_sample < slot.first_sample || observation.first_sample >= slot.stop_sample ||
        observation.rows > slot.stop_sample - observation.first_sample)
        throw Error("fine observation lies outside its public slot");
    if (observation.kind == FineRunKind::operational &&
        (observation.first_sample != whole.first_sample || observation.rows != whole.rows))
        throw Error("operational fine observation requires the whole public slot");
    if (observation.kind != FineRunKind::operational && observation.kind != FineRunKind::probe &&
        observation.kind != FineRunKind::cold_maximum)
        throw Error("invalid fine observation kind");

    auto plan = slot;
    plan.first_sample = observation.first_sample;
    plan.stop_sample = plan.first_sample + observation.rows;
    plan.quiet_first_sample = std::clamp(slot.quiet_first_sample, plan.first_sample, plan.stop_sample);
    plan.quiet_stop_sample = std::clamp(slot.quiet_stop_sample, plan.first_sample, plan.stop_sample);
    plan.window_capture_lead_seconds -=
        static_cast<long double>(plan.first_sample - slot.first_sample) / config.sample_rate_hz;
    if (!std::isfinite(plan.window_capture_lead_seconds))
        throw Error("fine observation lead is nonfinite");
    return plan;
}

struct FineWorkspaceQuote {
    std::size_t bytes;
    std::string_view native_fallback_reason;
    // Public blocks of one resident call. More than two selects windows.
    std::size_t resident_blocks = 2;
};

FineWorkspaceQuote workspace_for_plan(const ReceiverConfig& config, const ReceiverSlotPlan& plan,
                                      const BlockCalendar& calendar, unsigned workers, const FineObservation& observation,
                                      std::stop_token stop = {}) {
    if (!workers) throw Error("fine observation requires a positive worker share");
    // Reject unsupported public support before leasing a slot.
    const ProjectionBands validated(config, plan, ProjectionBands::Policy::legacy,
                                    calendar.block_rows > round_rows(.25, config.sample_rate_hz));
    const auto lengths = calendar.lengths();
    const auto max_rows = *std::max_element(lengths.begin(), lengths.end());
    std::size_t bytes = 64 * 1024, fft_rows = 0, float_rows = 0, max_modes = 0;
    bool isolated_varying_data = false;
    const auto add = [&](std::size_t count, std::size_t element) {
        bytes = checked_add(bytes, checked_multiply(count, element));
    };
    for (const auto rows : lengths) {
        if (!rows) continue;
        const auto modes = visit_projection_modes(rows, validated, stop, [](std::size_t) {});
        max_modes = std::max(max_modes, modes);
        isolated_varying_data |= rows == 1500 && modes == 47 && config.sample_rate_hz == 6000 &&
                                 config.time_varying_channel && !config.helper_enabled;
        if (validated.taper(rows, modes)) add(checked_multiply(rows, modes), sizeof(double));
        if (validated.use_float(rows, modes)) {
            add(1, FloatProjection::workspace_bytes());
            float_rows = FloatProjection::scratch_rows;
        }
        bytes = checked_add(bytes, DftPlan::workspace_bytes(rows));
        add(rows, sizeof(std::size_t));
        fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
    }
    // The dispatch being scored and the one observed ahead of it.
    add(fft_rows + 4 * max_rows, sizeof(Complex));
    const bool adaptive = observation.lanes == FineLanes::fixed_and_adaptive;
    // Only the diagnostic route can own both retained and predicted adaptive states.
    add((adaptive ? 3 : 1) * maximum_proposals, sizeof(State));
    if (affine_gain_plan(config, plan).coherence[0] > 0)
        add(maximum_proposals, sizeof(SlowGainState));
    if (adaptive)
        add(2 * maximum_states * (2 * maximum_rate_radius + 1),
            sizeof(TransitionTarget) + sizeof(TransitionEdge) + sizeof(RankedIndex));
    // Every dispatch's public geometry for one call.
    add(maximum_dispatches(calendar), dispatch_extent_bytes);
    // The fixed-affine lane's maps, source groups and chain progress.
    add(maximum_proposals, sizeof(MappedBasisRow) + 3 * sizeof(std::uint32_t));
    add(receiver_epoch_offsets.size(), sizeof(std::vector<std::uint32_t>));
    if (observation.record_work)
        add((adaptive ? 3 : 2) * maximum_proposals, sizeof(StateBlockWork));
    const auto active = useful_fine_workers(workers);
    add(checked_multiply(1 + active, float_rows), sizeof(FineFloatComplex));
    const auto mapped_rows = std::min(max_rows, maximum_basis_rows);
    const auto taps = config.spread_delay_seconds || config.spread_doppler_hz ? 2U : 1U;
    const FineBulkGeometry bulk_geometry{max_rows, max_modes, taps, workers,
        std::string_view(modem::detail::search_fft::kernel_name()) == "avx2", isolated_varying_data};
    const auto fallback = config.bulk_backend ? config.bulk_backend->fine_fallback_reason(bulk_geometry)
        : std::string_view{};
    std::size_t resident_blocks = 2;
    if (config.bulk_backend && fallback.empty()) {
        bytes = checked_add(bytes, config.bulk_backend->fine_host_bytes(max_rows, taps));
        const auto bulk_states = (adaptive ? 2 : 1) * maximum_proposals;
        add(2 * bulk_states, sizeof(FineBulkStatistics));
        add(bulk_states, sizeof(MappedBasisRow) + sizeof(std::size_t));
        add(mapped_rows, sizeof(double));
        if (!config.bulk_backend->fine_maps_on_host()) add(active, sizeof(WorkerScratch));
        // The fixed-affine maps of a whole window are known in advance. The
        // adaptive lane's follow its pruning and keep single dispatches.
        if (!adaptive) resident_blocks = std::max<std::size_t>(2, config.bulk_backend->fine_resident_blocks(bulk_geometry));
        if (resident_blocks >= 4) {
            // Three windows of projected dispatches: the one whose states
            // advance, the one on the device and the one observed ahead.
            // Two windows of statistics, the requests of one, and each
            // dispatch's receipts.
            add(checked_multiply(3, resident_blocks) / 2, dispatch_pair_bytes);
            add(checked_multiply(3 * resident_blocks - 4, max_rows), sizeof(Complex));
            add(checked_multiply(2 * resident_blocks - 2, bulk_states), sizeof(FineBulkStatistics));
            add(resident_blocks, sizeof(FineResidentBlock) + 128 + 8 * sizeof(std::size_t));
            add(maximum_dispatches(calendar), 2 * sizeof(std::size_t));
            if (observation.record_work)
                add(checked_multiply(resident_blocks / 2, maximum_proposals), sizeof(StateBlockWork));
        }
    }
    if (!config.bulk_backend || !fallback.empty() || config.bulk_backend->fine_maps_on_host()) {
        add(active, checked_add(checked_multiply(fft_rows + 4 * max_rows +
            (max_rows > mapped_rows ? 4 * mapped_rows : 0), sizeof(Complex)),
            checked_multiply(mapped_rows, sizeof(double))));
        if (config.bulk_backend && fallback.empty()) add(active, sizeof(WorkerScratch));
    }
    bytes = checked_add(bytes, config.bulk_backend ? config.bulk_backend->persistent_host_workspace() : 0);
    return {checked_add(bytes, MappedBasis::workspace_bytes(config, plan, mapped_rows, active)), fallback,
            resident_blocks};
}

void add_work(std::uint64_t& counter, std::uint64_t amount) {
    if (amount > UINT64_MAX - counter) throw Error("Robust fine work counter overflows");
    counter += amount;
}

void check_bulk_statistics(std::span<const FineBulkStatistics> output, std::size_t taps) {
    const auto finite = [](Complex value) { return std::isfinite(value.real()) && std::isfinite(value.imag()); };
    const auto null_is_zero = [](Complex value) { return value == Complex{}; };
    for (const auto& row : output) {
        const auto correlations = std::span(row.correlations).first(3 * taps);
        const auto gram = std::span(row.gram).first(3 * taps * taps);
        if (!std::all_of(correlations.begin(), correlations.end(), finite) ||
            !std::all_of(gram.begin(), gram.end(), finite))
            throw Error("Robust bulk backend did not supply finite complete sufficient statistics");
        const auto null_correlations = correlations.first(taps);
        const auto null_gram = gram.first(taps * taps);
        if (!std::all_of(null_correlations.begin(), null_correlations.end(), null_is_zero) ||
            !std::all_of(null_gram.begin(), null_gram.end(), null_is_zero))
            throw Error("Robust bulk likelihood must preserve the exact zero-signal null");
    }
}
} // namespace

FineBlockGeometry fine_block_geometry(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto fs = config.sample_rate_hz;
    if (!std::isfinite(fs) || fs < 1)
        throw Error("Invalid Robust fine sample rate");
    const FineBlockGeometry standard{round_rows(.25, fs), round_rows(.5, fs)};
    if (config.time_varying_channel || (config.helper_enabled && !ProjectionBands::can_taper_helper(config)))
        return standard;
    const auto cells_per_second = geometry(slot.format).fine_chip_rate_hz;
    const auto cell_rows = fs / cells_per_second;
    if (!(cell_rows > standard.block_rows && cell_rows <= maximum_basis_rows)) return standard;
    const auto block = round_rows(1 / cells_per_second, fs);
    // Keep room for whole observed blocks in the fixed physical quiet aperture,
    // including a possible merged tail. Probe crops reuse this public geometry.
    if (slot.quiet_stop_sample <= slot.quiet_first_sample ||
        slot.quiet_stop_sample - slot.quiet_first_sample < 3 * block) return standard;
    const ProjectionBands bands(config, slot, ProjectionBands::Policy::legacy, true);
    // A DC-only block collapses the two keyed shapes into scalar gains. A
    // cell-length block adds modes that distinguish their within-cell phase.
    if (bands.required.upper_hz >= fs / (2 * standard.block_rows) ||
        bands.required.upper_hz < fs / (2 * block)) return standard;
    const FineBlockGeometry cell{block, 2 * block};
    if (config.helper_enabled) {
        const BlockCalendar calendar(slot, cell);
        for (const auto rows : calendar.lengths()) {
            if (!rows) continue;
            const auto harmonics = std::floor(bands.required.upper_hz * rows / fs + .5);
            if (harmonics > maximum_tapered_modes / 2 ||
                !bands.taper(rows, 2 * static_cast<std::size_t>(harmonics) + 1)) return standard;
        }
    }
    return cell;
}

std::size_t fine_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                           FineObservation observation, unsigned workers) {
    const auto plan = observation_plan(config, slot, observation);
    const BlockCalendar calendar(plan, fine_block_geometry(config, slot));
    return workspace_for_plan(config, plan, calendar, workers, observation).bytes;
}

std::size_t fine_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return fine_workspace(config, slot, whole_slot(slot), workers);
}

namespace {

using BeatCoefficients = std::array<double, 4>;
using BeatProducts = std::array<BeatCoefficients, BeatCoefficients{}.size()>;

struct ColoredBeatScore {
    BeatCoefficients power{};
    BeatProducts gram{};

    void add(const std::array<Complex, 2>& harmonics, const std::array<Complex, 2>& rotations,
             const BeatCoefficients& moments, const BeatProducts& basis_gram) {
        BeatProducts columns{};
        for (std::size_t h = 0; h < harmonics.size(); ++h) {
            const auto predicted = harmonics[h] * rotations[h];
            columns[2 * h][2 * h] = predicted.real();
            columns[2 * h][2 * h + 1] = -predicted.imag();
            columns[2 * h + 1][2 * h] = -predicted.imag();
            columns[2 * h + 1][2 * h + 1] = -predicted.real();
        }
        for (std::size_t j = 0; j < power.size(); ++j) {
            for (std::size_t row = 0; row < moments.size(); ++row)
                power[j] += columns[j][row] * moments[row];
            BeatCoefficients transformed{};
            for (std::size_t row = 0; row < moments.size(); ++row)
                for (std::size_t col = 0; col < moments.size(); ++col)
                    transformed[row] += basis_gram[row][col] * columns[j][col];
            for (std::size_t k = 0; k <= j; ++k)
                for (std::size_t row = 0; row < transformed.size(); ++row)
                    gram[j][k] += columns[k][row] * transformed[row];
        }
    }

    double log_evidence() const {
        constexpr std::array grid{-.125, -.064, -.032, -.016, -.008, -.004, -.002, -.001, -.0005,
                                    0.,  .0005, .001, .002, .004, .008, .016, .032, .064, .125};
        // The normalized first and second harmonics have magnitudes at most
        // 1/sqrt(2) and 1/2. Centering the complex basis doubles its bound.
        // With each real gain component in [-1/8, 1/8], every |a_i| is < 1/2.
        constexpr double radius = .5;
        const double search_penalty = (-std::log1p(-radius) - radius) / (radius * radius);
        BeatCoefficients gain{};
        for (unsigned sweep = 0; sweep < 4; ++sweep)
            for (std::size_t j = 0; j < gain.size(); ++j) {
                if (!(gram[j][j] > 0)) continue;
                double slope = power[j];
                for (std::size_t k = 0; k < gain.size(); ++k)
                    if (k != j)
                        slope -= 2 * search_penalty * gain[k] * gram[std::max(j, k)][std::min(j, k)];
                const auto target = std::clamp(slope / (2 * search_penalty * gram[j][j]), grid.front(), grid.back());
                if (!std::isfinite(target)) throw Error("Nonfinite Robust colored power gain");
                const auto upper = std::ranges::lower_bound(grid, target);
                gain[j] = upper == grid.begin() || *upper - target < target - *(upper - 1) ? *upper : *(upper - 1);
            }
        // Stationary real Gaussian PCM gives constant E|IQ_i|^2 for the
        // complete FIR supports. Centering makes tr(B) = 0 for
        // B = 2 Sigma^(1/2) diag(a_i, a_i) Sigma^(1/2) / noise_variance.
        // The joint covariance cap gives tr(B^2) <= 2 sum(a_i^2).
        // The signed Gaussian log-MGF is thus at most penalty * sum(a_i^2)
        // by -log(1-lambda) - lambda <= penalty * lambda^2 for lambda <= rho.
        // Each fixed gain atom can use its own deterministic radius.
        const auto rho = std::numbers::sqrt2 * std::hypot(gain[0], gain[1]) + std::hypot(gain[2], gain[3]);
        const auto penalty = rho > 0 ? (-std::log1p(-rho) - rho) / (rho * rho) : .5;
        double evidence = 0;
        for (std::size_t j = 0; j < gain.size(); ++j) {
            evidence += gain[j] * (power[j] - penalty * gain[j] * gram[j][j]);
            for (std::size_t k = 0; k < j; ++k)
                evidence -= 2 * penalty * gain[j] * gain[k] * gram[j][k];
        }
        if (!std::isfinite(evidence)) throw Error("Nonfinite Robust colored power evidence");
        // Retain one atom from the full fixed gain prior. The search may use
        // the complete observation, including when it selects the zero atom.
        return std::max(0., evidence) - gain.size() * std::log(static_cast<double>(grid.size()));
    }
};

double carrier_beat_prediction_strength(const CarrierBeatReceipt& evidence, std::uint8_t bit) {
    double weakest = infinity;
    for (const auto& profile : evidence.profiles) {
        for (std::size_t half = 0; half < 2; ++half) {
            const auto held = profile.held_out_explained_fraction[bit][half];
            const auto competing = profile.held_out_explained_fraction[1 - bit][half];
            if (!(held > std::max(0., competing))) return 0;
            weakest = std::min(weakest, held);
        }
    }
    return weakest;
}

CarrierBeatReceipt carrier_beat_impl(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid,
        const FineCoordinate& coordinate, bool protected_only, std::size_t scratch_limit,
        CarrierBeatScanReceipt* scan, std::stop_token stop, bool compare_white_power = false,
        bool exploratory = false, bool compare_colored_power = false) {
    check_cancelled(stop);
    CarrierBeatReceipt report;
    report.status = "unsupported_carrier_beat_geometry";
    const auto whole = whole_slot(slot);
    if ((!iq.empty() && iq.size() != whole.rows) || (!valid.empty() && valid.size() != iq.size()))
        throw Error("Robust carrier beat requires the whole public input");
    if ((compare_white_power || compare_colored_power) && (!protected_only || scan ||
        !std::isfinite(config.noise_variance) ||
        !(config.noise_variance > 0)))
        throw Error("Robust power comparison requires a fixed protected aperture and declared variance");
    if (config.helper_enabled || config.physical_rf_retuning || slot.format.fhss ||
        !std::isfinite(config.sample_rate_hz) || config.sample_rate_hz < 12)
        return report;
    if (slot.quiet_first_sample < slot.first_sample || slot.quiet_stop_sample > slot.stop_sample ||
        slot.quiet_first_sample > slot.quiet_stop_sample)
        throw Error("Invalid Robust carrier beat aperture");
    if (!(coordinate.arrival_seconds >= slot.arrival_lower_seconds &&
          coordinate.arrival_seconds <= slot.arrival_upper_seconds) ||
        !std::isfinite(slot.arrival_lower_seconds) || !std::isfinite(slot.arrival_upper_seconds) ||
        !std::isfinite(coordinate.clock_ppm) || std::abs(coordinate.clock_ppm) > config.clock_bound_ppm ||
        std::ranges::find(receiver_epoch_offsets, coordinate.epoch_offset_seconds) == receiver_epoch_offsets.end() ||
        slot.format.epoch < 6 || slot.format.epoch > UINT64_MAX - 6)
        throw Error("Invalid Robust carrier beat coordinate");

    const auto waveform = geometry(slot.format);
    const auto fs = config.sample_rate_hz;
    const auto scale = 1 + coordinate.clock_ppm * 1e-6;
    if (!(scale > 0)) throw Error("Invalid Robust carrier beat clock");
    const auto spacing = waveform.carrier_spacing_hz * scale;
    const auto rows = round_rows(std::min(.25, 1 / (waveform.fine_chip_rate_hz * scale)), fs);
    if (rows < 5 || rows > maximum_basis_rows || spacing * rows < 3 * fs || 4 * spacing >= fs)
        return report;
    const auto first = protected_only ? slot.quiet_first_sample - slot.first_sample : 0;
    const auto end = protected_only ? slot.quiet_stop_sample - slot.first_sample : whole.rows;
    const auto blocks = (end - first) / rows;
    if (blocks * static_cast<double>(rows) * waveform.fine_chip_rate_hz * scale < 2 * fs)
        return report;
    using Harmonics = std::array<Complex, 2>;
    using HarmonicBasis = std::array<double, 5>;
    using Templates = std::array<std::array<Harmonics, 2>, 2>; // bit, path model, harmonic
    report.block_rows = rows;
    report.blocks = blocks;
    report.workspace_bytes = checked_add(8192, checked_add(
        checked_multiply(rows, sizeof(HarmonicBasis)), checked_multiply(2, waveform.source_workspace_bytes)));
    if (compare_white_power || compare_colored_power)
        report.workspace_bytes = checked_add(report.workspace_bytes, 4096);
    const auto stride = static_cast<long double>(rows) / fs;
    long double anchor = coordinate.arrival_seconds;
    std::size_t shifts = 1, template_blocks = blocks;
    if (scan) {
        constexpr std::size_t maximum_block_coordinate_pairs = 25'000'000;
        const auto maximum_shifts = maximum_block_coordinate_pairs / receiver_epoch_offsets.size() / blocks;
        const auto width = static_cast<long double>(slot.arrival_upper_seconds) - slot.arrival_lower_seconds;
        const auto intervals = std::floor(width / stride);
        if (intervals >= maximum_shifts) {
            report.status = "carrier_beat_scan_work_limit";
            return report;
        }
        shifts += static_cast<std::size_t>(intervals);
        // Residual span is shared at both edges, so every public arrival is
        // at most a half stride away from a scored center.
        anchor = slot.arrival_lower_seconds + (width - intervals * stride) / 2;
        template_blocks = checked_add(blocks, shifts - 1);
        report.workspace_bytes = checked_add(report.workspace_bytes, checked_add(64 * 1024,
            checked_add(checked_multiply(blocks, sizeof(Harmonics)),
                checked_multiply(template_blocks, sizeof(Templates)))));
        scan->coordinates = shifts * receiver_epoch_offsets.size();
    }
    if (report.workspace_bytes > scratch_limit)
        throw ReceiverWorkspaceError("Robust carrier beat exceeds the admitted workspace");
    if (iq.empty()) {
        report.status = "quoted";
        return report;
    }
    if (!valid.empty() && !std::ranges::all_of(valid.subspan(first, blocks * rows),
            [](std::uint8_t value) { return value != 0; })) {
        report.status = "invalid_carrier_beat_observation";
        return report;
    }

    // Least squares on the power waveform, including DC and both signed
    // harmonics. Finite blocks need not contain an integer number of cycles.
    std::vector<HarmonicBasis> basis(rows);
    std::array<HarmonicBasis, 5> lower{};
    const auto radians_per_row = tau * spacing / fs;
    for (std::size_t row = 0; row < rows; ++row) {
        const auto phase = radians_per_row * row;
        const auto first_harmonic = std::polar(1., phase);
        const auto second_harmonic = first_harmonic * first_harmonic;
        auto& column = basis[row];
        column = {1., first_harmonic.real(), first_harmonic.imag(),
            second_harmonic.real(), second_harmonic.imag()};
        for (std::size_t j = 0; j < column.size(); ++j)
            for (std::size_t k = 0; k <= j; ++k)
                lower[j][k] += column[j] * column[k];
    }
    BeatCoefficients basis_mean{};
    BeatProducts basis_gram{};
    if (compare_colored_power) {
        for (std::size_t j = 0; j < basis_mean.size(); ++j) {
            basis_mean[j] = lower[j + 1][0] / rows;
            for (std::size_t k = 0; k <= j; ++k)
                basis_gram[j][k] = basis_gram[k][j] = lower[j + 1][k + 1] - basis_mean[j] * lower[k + 1][0];
        }
    }
    for (std::size_t j = 0; j < lower.size(); ++j) {
        for (std::size_t k = 0; k <= j; ++k) {
            for (std::size_t q = 0; q < k; ++q) lower[j][k] -= lower[j][q] * lower[k][q];
            lower[j][k] = j == k ? std::sqrt(lower[j][j]) : lower[j][k] / lower[k][k];
        }
        if (!(lower[j][j] > 0)) throw Error("Robust carrier beat harmonics are singular");
    }
    const auto measure = [&](std::span<const Complex> input, std::size_t offset,
                             BeatCoefficients* moments) -> Harmonics {
        HarmonicBasis fit{};
        for (std::size_t row = 0; row < rows; ++row) {
            const auto power = std::norm(input[row]);
            for (std::size_t j = 0; j < fit.size(); ++j) fit[j] += power * basis[row][j];
        }
        if (!std::isfinite(fit[0])) throw Error("Nonfinite Robust carrier beat energy");
        if (!(fit[0] > 0)) return {};
        if (moments)
            for (std::size_t j = 0; j < moments->size(); ++j)
                (*moments)[j] = (fit[j + 1] - basis_mean[j] * fit[0]) / config.noise_variance;
        const auto normalization = .5 * rows / fit[0];
        for (std::size_t j = 0; j < fit.size(); ++j) {
            for (std::size_t k = 0; k < j; ++k) fit[j] -= lower[j][k] * fit[k];
            fit[j] /= lower[j][j];
        }
        for (std::size_t j = fit.size(); j-- > 0;) {
            for (std::size_t k = j + 1; k < fit.size(); ++k) fit[j] -= lower[k][j] * fit[k];
            fit[j] /= lower[j][j];
        }
        const auto phase = -tau * std::remainder(static_cast<long double>(spacing) * offset / fs, 1.L);
        const auto rotation = std::polar(1., static_cast<double>(phase));
        // Normalizing by the block's mean energy cancels arbitrary common
        // phase and voltage scale. Restore a common time origin across blocks.
        return {Complex(fit[1], -fit[2]) * rotation * normalization,
            Complex(fit[3], -fit[4]) * rotation * rotation * normalization};
    };

    const auto source_rate = effective_sample_rate(slot.format);
    const auto arrival = anchor + slot.window_capture_lead_seconds +
        slot.source_start_seconds / scale;
    const auto source_rows_per_input = source_rate * scale / fs;
    // Incoherent paths omit their mutual cross-beats. The fixed nominal
    // delay weights preserve each individual path's carrier self-products.
    const double selective_power = config.selective_modulation_rate_hz > 0 ?
        (config.selective_slow_depth * config.selective_slow_depth +
         config.selective_fast_depth * config.selective_fast_depth) / 9 : 0;
    const auto delay = selective_power > 0 ? 1 / (3 * waveform.nominal_carrier_bandwidth_hz) : 0;
    struct Path {
        double lag = 0, power = 0;
        Harmonics rotation{};
    };
    std::array<Path, 6> paths{};
    std::size_t path_count = 0;
    const std::array echo_offsets{0., config.spread_delay_seconds / 2};
    const auto echo_terms = config.spread_delay_seconds > 0 ? echo_offsets.size() : 1;
    for (const auto echo : std::span(echo_offsets).first(echo_terms)) {
        for (unsigned tap = 0; tap < (selective_power > 0 ? 3U : 1U); ++tap) {
            const auto lag = echo + tap * delay;
            const auto rotation = std::polar(1., -tau * spacing * lag);
            paths[path_count++] = {lag, (tap == 0 ? 1. : 0.) + selective_power,
                {rotation, rotation * rotation}};
        }
    }
    struct TemplatePower {
        Harmonics beats{};
        double mean = 0;
    };
    const auto predict = [&](PatternSource& source, long double offset) {
        std::array<TemplatePower, 2> expected{};
        constexpr unsigned integration_points = 4;
        for (unsigned point = 0; point < integration_points; ++point) {
            const auto sample = offset + (point + .5L) * rows / integration_points;
            const auto origin = sample * source_rows_per_input - arrival * source_rate * scale;
            for (std::size_t tap = 0; tap < path_count; ++tap) {
                const auto& path = paths[tap];
                const auto c = source.carrier_samples_at(origin - path.lag * source_rate * scale);
                const auto power = std::norm(c[0]) + std::norm(c[1]) + std::norm(c[2]);
                const Harmonics beats{c[1] * std::conj(c[0]) + c[2] * std::conj(c[1]), c[2] * std::conj(c[0])};
                if (tap == 0) {
                    expected[0].mean += power;
                    for (std::size_t h = 0; h < beats.size(); ++h) expected[0].beats[h] += beats[h];
                }
                expected[1].mean += path.power * power;
                for (std::size_t h = 0; h < beats.size(); ++h)
                    expected[1].beats[h] += path.power * path.rotation[h] * beats[h];
            }
        }
        std::array<Harmonics, 2> result{};
        for (std::size_t model = 0; model < expected.size(); ++model)
            if (expected[model].mean > 0)
                for (std::size_t h = 0; h < result[model].size(); ++h)
                    result[model][h] = expected[model].beats[h] / expected[model].mean;
        return result;
    };

    const auto sources_for = [&](std::int64_t epoch_offset) {
        auto format = slot.format;
        if (epoch_offset < 0) format.epoch -= static_cast<std::uint64_t>(-epoch_offset);
        else format.epoch += static_cast<std::uint64_t>(epoch_offset);
        return std::array{PatternSource(format, key, 0), PatternSource(format, key, 1)};
    };

    struct Correlation {
        Complex dot{};
        double energy = 0;
    };
    using FitHalf = std::array<std::array<std::array<Correlation, 2>, 2>, 2>;
    using FitBank = std::array<FitHalf, 2>;
    std::array<std::array<double, 2>, 2> half_energy{};
    const auto add_observation = [&](std::size_t block, BeatCoefficients* moments = nullptr) {
        const auto offset = first + block * rows;
        const auto values = measure(iq.subspan(offset, rows), offset, moments);
        auto& energy = half_energy[block >= blocks / 2];
        for (std::size_t h = 0; h < values.size(); ++h) energy[h] += std::norm(values[h]);
        return values;
    };
    const auto add_fit = [](FitHalf& fit, const Harmonics& measured, const Templates& expected) {
        for (std::size_t bit = 0; bit < expected.size(); ++bit)
            for (std::size_t model = 0; model < expected[bit].size(); ++model)
                for (std::size_t h = 0; h < measured.size(); ++h) {
                    const auto value = expected[bit][model][h];
                    auto& aggregate = fit[model][bit][h];
                    aggregate.dot += measured[h] * std::conj(value);
                    aggregate.energy += std::norm(value);
                }
    };
    const auto finish = [&](const FitBank& fit) {
        auto result = report;
        for (std::size_t h = 0; h < result.observed_harmonic_energy.size(); ++h)
            result.observed_harmonic_energy[h] = half_energy[0][h] + half_energy[1][h];
        const auto total = result.observed_harmonic_energy[0] + result.observed_harmonic_energy[1];
        for (std::size_t model = 0; model < result.profiles.size(); ++model)
            for (std::size_t bit = 0; bit < result.profiles[model].bit_projection_fraction.size(); ++bit) {
                auto& profile = result.profiles[model];
                std::array<double, 2> predicted{};
                double explained = 0;
                for (std::size_t h = 0; h < result.observed_harmonic_energy.size(); ++h) {
                    const auto& earlier = fit[0][model][bit][h];
                    const auto& later = fit[1][model][bit][h];
                    const Correlation value{earlier.dot + later.dot, earlier.energy + later.energy};
                    const auto both = value.energy * result.observed_harmonic_energy[h];
                    if (both > 0) profile.bit_correlations[bit][h] = value.dot / std::sqrt(both);
                    if (value.energy > 0) explained += std::norm(value.dot) / value.energy;
                    // Reduction of squared error on held samples compared
                    // with predicting zero, fitting gains on the other half.
                    for (std::size_t half = 0; half < fit.size(); ++half) {
                        const auto& training = fit[1 - half][model][bit][h];
                        const auto& held = fit[half][model][bit][h];
                        if (!(training.energy > 0)) continue;
                        const auto gain = training.dot / training.energy;
                        predicted[half] += 2 * (held.dot * std::conj(gain)).real() - std::norm(gain) * held.energy;
                    }
                }
                if (total > 0) profile.bit_projection_fraction[bit] = std::clamp(explained / total, 0., 1.);
                for (std::size_t half = 0; half < fit.size(); ++half) {
                    const auto held = half_energy[half][0] + half_energy[half][1];
                    if (held > 0) profile.held_out_explained_fraction[bit][half] = predicted[half] / held;
                }
            }
        result.status = "scored";
        return result;
    };
    if (!scan) {
        auto sources = sources_for(coordinate.epoch_offset_seconds);
        FitBank fit{};
        std::array<std::array<ColoredBeatScore, 2>, 2> colored_scores{}; // bit, model
        for (std::size_t block = 0; block < blocks; ++block) {
            check_cancelled(stop);
            const auto offset = first + block * rows;
            const Templates expected{predict(sources[0], offset), predict(sources[1], offset)};
            BeatCoefficients moments{};
            add_fit(fit[block >= blocks / 2],
                    add_observation(block, compare_colored_power ? &moments : nullptr), expected);
            if (compare_colored_power) {
                const auto turns = std::remainder(static_cast<long double>(spacing) * offset / fs, 1.L);
                const auto rotation = std::polar(1., static_cast<double>(tau * turns));
                const Harmonics rotations{rotation, rotation * rotation};
                for (std::size_t bit = 0; bit < expected.size(); ++bit)
                    for (std::size_t model = 0; model < expected[bit].size(); ++model)
                        colored_scores[bit][model].add(expected[bit][model], rotations, moments, basis_gram);
            }
        }
        auto result = finish(fit);
        if (compare_colored_power) {
            auto& combined = result.colored_power_log_evidence.emplace();
            for (std::size_t bit = 0; bit < combined.size(); ++bit)
                combined[bit] = log_add(colored_scores[bit][0].log_evidence(),
                                        colored_scores[bit][1].log_evidence()) - std::log(2.);
        }
        if (!compare_white_power) return result;

        constexpr std::array scales{.5, 1., 2.};
        using TiltScores = std::array<double, scales.size()>;
        std::array<std::array<std::array<TiltScores, 2>, 2>, 2> log_scores{}; // held half, bit, model, scale
        for (std::size_t block = 0; block < blocks; ++block) {
            check_cancelled(stop);
            const std::size_t half = block >= blocks / 2;
            const auto offset = first + block * rows;
            const Templates expected{predict(sources[0], offset), predict(sources[1], offset)};
            const auto turns = std::remainder(static_cast<long double>(spacing) * offset / fs, 1.L);
            const auto rotation = std::polar(1., static_cast<double>(tau * turns));
            const Harmonics rotations{rotation, rotation * rotation};
            Templates predicted{};
            for (std::size_t bit = 0; bit < expected.size(); ++bit)
                for (std::size_t model = 0; model < expected[bit].size(); ++model)
                    for (std::size_t h = 0; h < rotations.size(); ++h) {
                        const auto& training = fit[1 - half][model][bit][h];
                        if (training.energy > 0)
                            predicted[bit][model][h] = (training.dot / training.energy) *
                                expected[bit][model][h] * rotations[h];
                    }
            for (std::size_t row = 0; row < rows; ++row) {
                if (!(row % 4096)) check_cancelled(stop);
                const auto power = std::norm(iq[offset + row]) / config.noise_variance;
                if (!std::isfinite(power)) throw Error("Nonfinite Robust normalized white power");
                for (std::size_t bit = 0; bit < predicted.size(); ++bit)
                    for (std::size_t model = 0; model < predicted[bit].size(); ++model) {
                        const auto& p = predicted[bit][model];
                        const auto& b = basis[row];
                        const double tilt = 2 * (p[0].real() * b[1] - p[0].imag() * b[2] +
                            p[1].real() * b[3] - p[1].imag() * b[4]);
                        for (std::size_t k = 0; k < scales.size(); ++k) {
                            const auto a = std::clamp(tilt * scales[k], -.5, .5);
                            log_scores[half][bit][model][k] += a * power + std::log1p(-a);
                        }
                    }
            }
        }
        // With the fit fixed from the other half, each held power is Exp(1)
        // under white I/Q. Normalize every likelihood and average directions,
        // models and scales. Multiplying the two directions would reuse
        // training data as independent evidence.
        const auto log_count = std::log(static_cast<double>(4 * scales.size()));
        auto& combined = result.white_power_log_evidence.emplace();
        for (std::size_t bit = 0; bit < combined.size(); ++bit) {
            double evidence = -infinity;
            for (const auto& direction : log_scores)
                for (const auto& model : direction[bit])
                    evidence = std::accumulate(model.begin(), model.end(), evidence, log_add);
            combined[bit] = evidence - log_count;
        }
        return result;
    }

    std::vector<Harmonics> observation(blocks);
    for (std::size_t block = 0; block < blocks; ++block) {
        check_cancelled(stop);
        observation[block] = add_observation(block);
    }
    std::vector<Templates> templates(template_blocks);
    std::array<double, 2> best{};
    double best_exploratory = 0;
    for (const auto epoch : receiver_epoch_offsets) {
        check_cancelled(stop);
        auto sources = sources_for(epoch);
        // Delaying a template by S whole blocks uses its earlier sample at
        // row n-S*rows. The origin can precede the observation so all legal
        // shifts are slices of this one keyed bank.
        const auto origin = static_cast<long double>(first) - static_cast<long double>(shifts - 1) * rows;
        for (std::size_t block = 0; block < template_blocks; ++block) {
            check_cancelled(stop);
            const auto offset = origin + static_cast<long double>(block) * rows;
            templates[block] = {predict(sources[0], offset), predict(sources[1], offset)};
        }
        for (std::size_t shift = 0; shift < shifts; ++shift) {
            check_cancelled(stop);
            const auto offset = shifts - 1 - shift;
            FitBank fit{};
            for (std::size_t block = 0; block < blocks; ++block) {
                if (!(block % 4096)) check_cancelled(stop);
                add_fit(fit[block >= blocks / 2], observation[block], templates[offset + block]);
            }
            auto evidence = finish(fit);
            const auto delay = std::clamp(static_cast<double>(anchor + shift * stride),
                slot.arrival_lower_seconds, slot.arrival_upper_seconds);
            if (exploratory) {
                for (std::uint8_t bit = 0; bit < 2; ++bit) {
                    const auto strength = carrier_beat_prediction_strength(evidence, bit);
                    if (strength <= best_exploratory) continue;
                    best_exploratory = strength;
                    scan->exploratory_nomination = CarrierBeatNomination{{epoch, delay, 0, 0}, evidence, bit};
                }
            }
            const auto bit = qualified_carrier_beat_bit(evidence);
            if (!bit) continue;
            const auto strength = std::min(evidence.profiles[0].bit_projection_fraction[*bit],
                                           evidence.profiles[1].bit_projection_fraction[*bit]);
            if (strength <= best[*bit]) continue;
            best[*bit] = strength;
            scan->nominations[*bit] = CarrierBeatNomination{{epoch, delay, 0, 0}, std::move(evidence), *bit};
        }
    }
    report.status = "scored";
    return report;
}

} // namespace

CarrierBeatReceipt score_carrier_beat(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid,
        const FineCoordinate& coordinate, bool protected_only, std::size_t scratch_limit,
        std::stop_token stop, bool compare_white_power, bool compare_colored_power) {
    return carrier_beat_impl(config, slot, key, iq, valid, coordinate, protected_only, scratch_limit,
        nullptr, stop, compare_white_power, false, compare_colored_power);
}

std::optional<std::uint8_t> qualified_carrier_beat_bit(const CarrierBeatReceipt& evidence) {
    if (evidence.status != "scored") return std::nullopt;
    for (std::uint8_t bit = 0; bit < 2; ++bit) {
        if (carrier_beat_prediction_strength(evidence, bit) > 0 &&
            std::ranges::all_of(evidence.profiles, [bit](const auto& profile) {
                return profile.bit_projection_fraction[bit] > .5 &&
                    profile.bit_projection_fraction[bit] > profile.bit_projection_fraction[1 - bit];
            })) return bit;
    }
    return std::nullopt;
}

CarrierBeatScanReceipt scan_carrier_beat(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const Crypto& key, std::span<const Complex> iq, std::span<const std::uint8_t> valid,
        std::size_t scratch_limit, std::stop_token stop, bool exploratory) {
    CarrierBeatScanReceipt scan;
    const FineCoordinate coordinate{0, slot.arrival_lower_seconds, 0, 0};
    const auto result = carrier_beat_impl(config, slot, key, iq, valid, coordinate, true, scratch_limit, &scan,
        stop, false, exploratory);
    scan.status = result.status;
    scan.workspace_bytes = result.workspace_bytes;
    return scan;
}

struct FineSession::Impl {
    // The public geometry of one dispatch: at most two consecutive blocks
    // of one segment. It depends on the calendar and validity mask alone.
    struct Extent {
        std::size_t segment_first = 0, segment_rows = 0, base = 0, first = 0, count = 0;
        std::array<std::size_t, 2> lengths{};
        std::array<bool, 2> observed{};
        bool opens_segment = false, closes_segment = false;
    };
    static_assert(sizeof(Extent) <= dispatch_extent_bytes);

    // One dispatch's projected observation.
    struct Pair {
        const Extent* extent = nullptr;
        std::array<ObservationBlock, 2> blocks;
        std::array<double, 4 * 2 * models> transitions{};
        SlowGainTransitions slow_transitions{};
        std::uint64_t observed_rows = 0, modes = 0, basis_calls = 0;
        std::span<const ObservationBlock> observations() const { return std::span(blocks).first(extent->count); }
    };
    static_assert(sizeof(Pair) <= dispatch_pair_bytes);

    // The dispatches whose fixed-affine statistics one resident call holds.
    // Its states then advance through them in public order, as the urgent
    // leaves [first_leaf, first_leaf + leaves).
    struct Window {
        std::size_t begin = 0, end = 0, base = 0, statistics = 0, first_leaf = 0, leaves = 0;
        bool active = false;
    };

    // What the running urgent leaves read. The owner changes it only
    // between waves, when none of them is running.
    struct Wave {
        const Pair* pair = nullptr;
        Pair* ahead = nullptr;
        ReceiverExecutor::Stage* stage = nullptr;
        std::size_t first_leaf = 0, release = 0;
        long double anchor = 0;
    };

    struct TemplateWave {
        const ObservationBlock* block = nullptr;
        std::span<FineBulkStatistics> statistics;
        std::span<std::complex<float>> mapped;
        std::size_t first_state = 0, first_leaf = 0;
    };

    ReceiverConfig config;
    FineObservation observation;
    ReceiverSlotPlan plan;
    std::span<const Complex> iq;
    std::span<const std::uint8_t> validity;
    std::stop_token stop;
    BlockCalendar calendar;
    TimingGrid grid;
    std::vector<FineCoordinate> coordinates;
    std::array<Tap, 2> paths;
    std::size_t tap_count;
    unsigned workers, stage_workers;

    std::vector<Projection> projections;
    std::vector<Complex> observation_scratch;
    std::vector<FineFloatComplex> observation_float_scratch, worker_float_scratch;
    std::unique_ptr<MappedBasis> basis;
    std::unique_ptr<FineBulk> bulk;
    FineBulkWork bulk_work;
    std::vector<MappedBasisRow> bulk_rows;
    ConsecutiveBasisOffsets bulk_offsets;
    std::vector<std::size_t> bulk_progress;
    // One entry per block of a dispatch, or of a window of dispatches.
    std::vector<std::vector<FineBulkStatistics>> bulk_statistics;
    // Set when the backend renders this slot's dense tiles itself. The
    // owner then addresses the tiles of each resident call's rows.
    const FineTileSource* tile_source = nullptr;
    std::unique_ptr<MappedBasis::Addresses> addresses;
    // Dispatches per resident window, or zero for single dispatches.
    std::size_t window_dispatches = 0;
    // Lanes that own transform scratch project a window's dispatches as
    // separate leaves. Otherwise one leaf projects the whole window. Their
    // summed elapsed time is drained into worker_observation_seconds.
    bool dispatch_observation = false;
    std::atomic<std::int64_t> observation_nanoseconds{0};
    Window window;
    std::vector<std::array<std::size_t, 2>> windows;
    std::vector<FineResidentBlock> resident_requests;
    std::vector<std::size_t> resident_slots, resident_progress;
    std::vector<std::uint8_t> resident_done;
    std::vector<std::uint64_t> window_calls;
    std::vector<std::complex<float>> rendered_templates, projected_templates;
    TemplateWave template_wave;
    std::vector<WorkerScratch> scratch;
    // A standalone session owns its lanes. A stream shares its receiver pool.
    std::unique_ptr<ReceiverExecutor> owned_executor;
    ReceiverExecutor& executor;
    // Every dispatch of the current call, then the one being scored and the
    // one observed ahead of it. Resident windows hold three groups of
    // dispatches: advancing its states, on the device, and observed ahead.
    std::vector<Extent> extents;
    std::vector<Pair> ring;
    Wave wave;
    std::size_t pair_serial = 0, call_serial = 0;
    // Background leaves per dispatch: every fixed-affine state, then one
    // tile-filling leaf per keyed source. Fills run `fill_depth` dispatches
    // ahead, about two tiles, because one fill can outlast a dispatch.
    std::size_t leaf_block = 0, fill_depth = 0;
    // The fixed-affine lane's maps never change, so each of its states is an
    // independent chain. Its states are grouped by the keyed source they read.
    std::vector<MappedBasisRow> nominal_rows;
    std::vector<std::vector<std::uint32_t>> epoch_states;
    // Dispatches completed by each fixed-affine state in the current call.
    execution::Mutex chain_mutex;
    execution::Condition chain_advanced;
    std::vector<std::uint32_t> nominal_progress;
    std::vector<std::vector<StateBlockWork>> nominal_state_work;
    std::vector<StateBlockWork> adaptive_state_work;
    std::vector<RankedIndex> ranking;
    std::vector<State> nominal, adaptive;
    AffineGainPlan additional_gain;
    std::vector<SlowGainState> slow_nominal;

    long double first_anchor = 0, segment_anchor = 0, previous_anchor = 0, previous_last = 0, previous_block_center = 0;
    std::size_t available = 0, processed = 0, segments = 0, scored_blocks = 0, last_block_rows = 0;
    std::size_t invalid_rows = 0, discarded_rows = 0, erased_blocks = 0;
    std::size_t quiet_modes = 0, observed_modes = 0, workspace = 0;
    std::uint64_t basis_rows = 0, adaptive_edges = 0;
    double quiet_energy = 0, quiet_threshold = 0;
    bool nominal_nonzero = false, adaptive_nonzero = false;
    bool finished = false, poisoned = false;
    FineLaneWork nominal_work, adaptive_work;
    FinePhaseTimes times;

    Impl(const ReceiverConfig& input_config, const ReceiverSlotPlan& slot, const Crypto& key,
         FineObservation extent, std::span<const Complex> samples, std::span<const std::uint8_t> valid,
         std::span<const FineCoordinate> selected, double projected_rate, double clock_gap,
         unsigned allocation, std::size_t scratch_limit, std::stop_token cancellation,
         ReceiverExecutor* shared, AffineGainEvaluation evaluation)
        : config(input_config), observation(extent), plan(observation_plan(config, slot, observation)),
          iq(samples), validity(valid), stop(cancellation), calendar(plan, fine_block_geometry(config, slot)),
          grid(projected_rate, clock_gap),
          paths{{{0, 0}, {config.spread_delay_seconds, config.spread_doppler_hz}}},
          tap_count(paths[1].delay_seconds || paths[1].doppler_hz ? 2 : 1), workers(allocation),
          stage_workers(useful_fine_workers(workers)),
          owned_executor(shared ? nullptr : std::make_unique<ReceiverExecutor>(std::max(1U, stage_workers))),
          executor(shared ? *shared : *owned_executor), additional_gain(affine_gain_plan(config, plan, evaluation)) {
        check_cancelled(stop);
        if (iq.size() != observation.rows || (!validity.empty() && validity.size() != iq.size()))
            throw Error("fine input lease differs from the declared observation");
        if (selected.empty() || selected.size() > 64)
            throw Error("fine coordinate bank exceeds its bounded public size");
        for (std::size_t i = 0; i < selected.size(); ++i) {
            const auto& coordinate = selected[i];
            if (coordinate.epoch_offset_seconds < receiver_epoch_offsets.front() ||
                coordinate.epoch_offset_seconds > receiver_epoch_offsets.back() ||
                !std::isfinite(coordinate.arrival_seconds) || !std::isfinite(coordinate.frequency_hz) ||
                !std::isfinite(coordinate.clock_ppm) ||
                std::find(selected.begin(), selected.begin() + static_cast<std::ptrdiff_t>(i), coordinate) !=
                    selected.begin() + static_cast<std::ptrdiff_t>(i))
                throw Error("fine coordinate bank requires finite, distinct public atoms");
        }
        const bool cold = observation.kind == FineRunKind::cold_maximum;
        if (cold && observation.rows != 2 * calendar.segment_rows)
            throw Error("cold maximum-state work requires exactly two complete fine segments");

        const auto quote = workspace_for_plan(config, plan, calendar, workers, observation, stop);
        workspace = quote.bytes;
        if (workspace > scratch_limit)
            throw ReceiverWorkspaceError("complete fine receiver exceeds its admitted workspace");
        coordinates.assign(selected.begin(), selected.end());
        nominal = grid.initial(coordinates.size(), tap_count, cold);
        if (adaptive_enabled()) adaptive = nominal;
        if (additional_gain.coherence[0] > 0) {
            slow_nominal.resize(nominal.size());
            const auto prior = -std::log(static_cast<double>(nominal.size())) - log_two;
            const auto dimensions = additional_gain.stationary_fading ? 2 : tap_count;
            for (auto& state : slow_nominal) {
                state.weights.fill(prior);
                for (auto& channel : state.posterior) {
                    if (additional_gain.stationary_fading) channel.uncertainty = {1., .5, .5, .5};
                    else for (std::size_t tap = 0; tap < dimensions; ++tap)
                        channel.uncertainty[tap * dimensions + tap] =
                            initial_gain_diagonal(dimensions, 1. / static_cast<double>(dimensions));
                }
            }
        }
        nominal_work.initial_states = nominal.size();
        nominal_work.state_cap = nominal_work.proposal_cap = nominal.size();
        if (adaptive_enabled()) {
            adaptive_work.initial_states = adaptive.size();
            adaptive_work.state_cap = maximum_states;
            adaptive_work.proposal_cap = maximum_proposals;
        }

        const auto lengths = calendar.lengths();
        projections.reserve(lengths.size());
        const ProjectionBands bands(config, plan, ProjectionBands::Policy::legacy,
                                    calendar.block_rows > round_rows(.25, config.sample_rate_hz));
        std::size_t fft_rows = 0, float_rows = 0, max_rows = 0;
        for (const auto rows : lengths) {
            if (!rows) continue;
            projections.emplace_back(rows, bands, stop);
            fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
            float_rows = std::max(float_rows, projections.back().float_scratch_rows());
            max_rows = std::max(max_rows, rows);
        }
        observation_scratch.resize(fft_rows);
        observation_float_scratch.resize(float_rows);
        worker_float_scratch.resize(checked_multiply(stage_workers, float_rows));
        const auto mapped_rows = std::min(max_rows, maximum_basis_rows);
        basis = std::make_unique<MappedBasis>(config, plan, key, mapped_rows, stage_workers);
        if (config.bulk_backend && quote.native_fallback_reason.empty()) {
            bulk = config.bulk_backend->make_fine(projected_rate, 2 * config.clock_bound_ppm);
            if (!bulk) throw Error("requested Robust bulk backend did not construct a fine session");
            bulk_work = bulk->completed_work();
            const auto bulk_states = (adaptive_enabled() ? 2 : 1) * maximum_proposals;
            bulk_rows.reserve(bulk_states);
            bulk_progress.reserve(bulk_states);
            bulk_offsets = ConsecutiveBasisOffsets(mapped_rows);
            // The preflight admitted this many resident blocks at most.
            const auto resident_blocks = std::min(quote.resident_blocks, bulk->resident_block_capacity());
            if (resident_blocks >= 4) window_dispatches = resident_blocks / 2;
            bulk->admit_resident_blocks(window_dispatches ? 2 * window_dispatches : 2);
            // A window's states advance while the next one's statistics
            // arrive, so windows alternate between two halves.
            const auto block_slots = std::max<std::size_t>(2, 4 * window_dispatches);
            bulk_statistics.resize(block_slots);
            for (auto& item : bulk_statistics) item.reserve(window_dispatches ? nominal.size() : bulk_states);
            resident_progress.resize(block_slots);
            resident_done.resize(block_slots);
            resident_requests.reserve(block_slots);
            resident_slots.reserve(block_slots);
            window_calls.resize(std::max<std::size_t>(1, 2 * window_dispatches));
            windows.reserve(maximum_dispatches(calendar));
            if (const auto* source = basis->tile_source(); source && bulk->renders_source_tiles(*source)) {
                tile_source = source;
                addresses = std::make_unique<MappedBasis::Addresses>(*basis);
            }
        } else if (config.bulk_backend) {
            config.bulk_backend->record_native_fine(quote.native_fallback_reason);
        }
        scratch.reserve(stage_workers);
        const auto host_capacity = bulk ? bulk->host_projection_capacity() : 0;
        if (host_capacity) {
            rendered_templates.resize(checked_multiply(host_capacity, 1500 + bulk->host_projection_stages_peaks()));
            projected_templates.resize(checked_multiply(host_capacity, 47));
        }
        for (unsigned worker = 0; worker < stage_workers; ++worker)
            scratch.emplace_back(!bulk || host_capacity ? max_rows : 0, !bulk || host_capacity ? fft_rows : 0);
        dispatch_observation = window_dispatches && (!bulk || host_capacity);
        ring.resize(std::max<std::size_t>(2, 3 * window_dispatches));
        for (auto& pair : ring)
            for (auto& block : pair.blocks) block.projected.reserve(max_rows);
        extents.reserve(maximum_dispatches(calendar));
        // More sources than this would have fills ahead displace live tiles.
        if (!bulk && coordinates.size() <= maximum_fill_coordinates) {
            const auto cache = basis->report();
            const auto tile_rows = static_cast<double>(cache.tile_samples) / cache.dense_factor;
            fill_depth = static_cast<std::size_t>(std::clamp(
                std::round(2 * tile_rows / static_cast<double>(calendar.segment_rows)), 1., 8.));
        }
        nominal_progress.resize(nominal.size());
        if (adaptive_enabled()) ranking.reserve(2 * maximum_states * (2 * maximum_rate_radius + 1));
        if (observation.record_work) {
            nominal_state_work.resize(std::max<std::size_t>(2, window_dispatches));
            for (auto& item : nominal_state_work) item.resize(nominal.size());
            if (adaptive_enabled()) adaptive_state_work.resize(maximum_proposals);
        }
    }

    template<class Operation>
    void time(double& receipt, Operation operation) {
        const auto began = observation.record_work ? FineClock::now() : FineClock::time_point{};
        operation();
        if (observation.record_work) receipt += seconds_since(began);
    }

    bool observation_complete() const { return processed == observation.rows; }
    bool adaptive_enabled() const { return observation.lanes == FineLanes::fixed_and_adaptive; }
    bool representative_work() const { return nominal_nonzero && (!adaptive_enabled() || adaptive_nonzero); }
    std::span<const Tap> taps() const { return std::span(paths).first(tap_count); }

    // The public geometry of the dispatch after `previous`, or of the first
    // available one. A segment starts only when all of its rows are exposed.
    bool plan_pair(Extent& next, const Extent* previous) const {
        std::size_t segment_first, segment_rows, base, first;
        if (previous && !previous->closes_segment) {
            segment_first = previous->segment_first;
            segment_rows = previous->segment_rows;
            base = previous->base + previous->count;
            first = previous->first + previous->lengths[0] + (previous->count > 1 ? previous->lengths[1] : 0);
        } else {
            segment_first = previous ? previous->segment_first + previous->segment_rows : processed;
            if (segment_first == observation.rows) return false;
            segment_rows = calendar.segment_length(segment_first);
            if (segment_rows > available - segment_first) return false;
            base = 0;
            first = segment_first;
        }
        const auto block_count = calendar.block_count(segment_rows);
        next.segment_first = segment_first;
        next.segment_rows = segment_rows;
        next.base = base;
        next.first = first;
        next.count = std::min(next.lengths.size(), block_count - base);
        next.opens_segment = base == 0;
        next.closes_segment = base + next.count == block_count;
        for (std::size_t block = 0; block < next.count; ++block) {
            const auto rows = calendar.block_length(segment_rows, base + block);
            next.lengths[block] = rows;
            const auto valid = validity.empty() ? validity : validity.subspan(first, rows);
            next.observed[block] = std::find(valid.begin(), valid.end(), 0) == valid.end();
            first += rows;
        }
        return true;
    }

    // A block's public geometry, elapsed interval and erasure receipts, in
    // chronological order. Only an observed block is then projected.
    void place(ObservationBlock& block, std::size_t first, std::size_t rows, std::stop_token leaf_stop) {
        check_cancelled(leaf_stop);
        const auto projection = std::find_if(projections.begin(), projections.end(),
            [&](const Projection& candidate) { return candidate.rows() == rows; });
        if (projection == projections.end()) throw Error("Robust likelihood block has no public projection");
        block.first = first;
        block.projection = &*projection;
        const auto center = (static_cast<long double>(first) + (rows - 1) / 2.L) / config.sample_rate_hz;
        block.elapsed = first ? static_cast<double>(center - previous_block_center) : 0.;
        previous_block_center = center;
        last_block_rows = rows;

        const auto valid = validity.empty() ? validity : validity.subspan(first, rows);
        const auto missing = valid.empty() ? 0 : static_cast<std::size_t>(std::count(valid.begin(), valid.end(), 0));
        block.observed = missing == 0;
        if (!block.observed) {
            ++erased_blocks;
            discarded_rows += rows - missing;
            block.projected.clear();
        }
    }

    // Independent of every other block: the caller owns the scratch.
    void project(ObservationBlock& block, std::span<Complex> fft, std::span<FineFloatComplex> float_scratch,
                 std::stop_token leaf_stop) const {
        check_cancelled(leaf_stop);
        const auto raw = iq.subspan(block.first, block.projection->rows());
        block.projected.assign(raw.begin(), raw.end());
        block.projection->apply(block.projected, fft, leaf_stop, float_scratch);
        block.projected.resize(block.projection->modes());
    }

    // A projected block's mode and quiet-energy receipts, in chronological order.
    void count(const ObservationBlock& block) {
        const auto rows = block.projection->rows();
        observed_modes += block.projection->modes();
        const auto quiet_first = plan.quiet_first_sample - plan.first_sample;
        const auto quiet_stop = plan.quiet_stop_sample - plan.first_sample;
        if (quiet_first <= block.first && block.first + rows <= quiet_stop) {
            quiet_modes += block.projection->modes();
            quiet_energy += finite_energy(block.projected) / config.noise_variance;
            if (!std::isfinite(quiet_energy)) throw Error("Robust quiet energy is nonfinite");
        }
    }

    // Places one dispatch's blocks in chronological order, with their
    // transitions and receipts, projecting them too unless deferred.
    void open_dispatch(Pair& pair, const Extent& extent, bool project_now, std::stop_token leaf_stop) {
        check_cancelled(leaf_stop);
        auto first = extent.first;
        pair.extent = &extent;
        pair.observed_rows = pair.modes = pair.basis_calls = 0;
        pair.transitions = {};
        for (std::size_t i = 0; i < extent.count; ++i) {
            auto& block = pair.blocks[i];
            place(block, first, extent.lengths[i], leaf_stop);
            if (project_now && block.observed) {
                project(block, observation_scratch, observation_float_scratch, leaf_stop);
                count(block);
            }
            first += extent.lengths[i];
            // Elapsed intervals and channel models are common to all
            // states, including for a missing block whose elapsed time
            // still evolves the posterior.
            const auto write_transitions = [&](const auto& coherence, auto& output) {
                for (std::size_t model = 0; model < coherence.size(); ++model) {
                    const auto offset = 4 * (i * coherence.size() + model);
                    const auto exponent = -block.elapsed / coherence[model];
                    const auto decay = std::exp(exponent);
                    output[offset] = decay * decay;
                    output[offset + 1] = -std::expm1(2 * exponent);
                    output[offset + 2] = decay;
                }
            };
            write_transitions(coherence_seconds, pair.transitions);
            if (!slow_nominal.empty()) write_transitions(additional_gain.coherence, pair.slow_transitions);
            if (!block.observed) continue;
            const auto rows = block.projection->rows();
            pair.observed_rows += rows;
            pair.modes += block.projection->modes();
            pair.basis_calls += 1 + (rows - 1) / maximum_basis_rows;
        }
    }

    // Projects one dispatch's blocks in chronological order. Calls never
    // overlap: the next dispatch waits for this observation to finish before
    // another observation starts or either ring entry is reused.
    void observe(Pair& pair, const Extent& extent, std::stop_token leaf_stop) {
        time(times.observation_seconds, [&] {
            open_dispatch(pair, extent, true, leaf_stop);
            if (observation.observed) observation.observed(extent.first, leaf_stop);
            check_cancelled(leaf_stop);
        });
    }

    // Serial work at a segment boundary. Background fixed-affine leaves keep
    // the other lanes busy meanwhile.
    void open_segment(const Extent& pair) {
        const auto fs = static_cast<long double>(config.sample_rate_hz);
        const auto first = pair.segment_first;
        segment_anchor = (static_cast<long double>(first) + (pair.segment_rows - 1) / 2.L) / fs;
        if (!first) {
            first_anchor = segment_anchor;
            nominal_rows.clear();
            nominal_rows.reserve(nominal.size());
            epoch_states.clear();
            std::array<std::size_t, receiver_epoch_offsets.size()> group;
            group.fill(receiver_epoch_offsets.size());
            for (std::size_t i = 0; i < nominal.size(); ++i) {
                const auto& key = nominal[i].key;
                nominal_rows.push_back(mapped_row(config, plan, grid, coordinates[key.group], key, first_anchor, true));
                auto& slot = group[static_cast<std::size_t>(nominal_rows.back().epoch_offset_seconds -
                                                            receiver_epoch_offsets.front())];
                if (slot == group.size()) {
                    slot = epoch_states.size();
                    epoch_states.emplace_back();
                }
                epoch_states[slot].push_back(static_cast<std::uint32_t>(i));
            }
        } else if (adaptive_enabled()) {
            const auto boundary = (previous_last + static_cast<long double>(first) / fs) / 2 - previous_anchor;
            time(times.transition_seconds, [&] {
                adaptive = predict(adaptive, grid, static_cast<double>(segment_anchor - previous_anchor),
                    static_cast<double>(boundary), config.data_if_hz, tap_count, adaptive_edges,
                    observation.record_work ? &adaptive_work : nullptr, ranking, stop);
            });
            adaptive_work.transition_edges = adaptive_edges;
        }
        nominal_work.last_emission_states = nominal.size();
        nominal_work.peak_emission_states = nominal.size();
        adaptive_work.last_emission_states = adaptive.size();
        adaptive_work.peak_emission_states = std::max(adaptive_work.peak_emission_states, adaptive.size());
    }

    void close_segment(const Extent& pair) {
        const auto fs = static_cast<long double>(config.sample_rate_hz);
        if (adaptive_enabled()) {
            adaptive_nonzero |= std::any_of(adaptive.begin(), adaptive.end(),
                [](const State& state) { return state.nonzero_basis; });
            time(times.pruning_seconds, [&] {
                if (observation.record_work && adaptive.size() > maximum_states) {
                    ++adaptive_work.pruning_events;
                    adaptive_work.states_dropped += adaptive.size() - maximum_states;
                }
                retain(adaptive, maximum_states, ranking);
            });
        }
        nominal_work.peak_retained_states = nominal.size();
        adaptive_work.peak_retained_states = std::max(adaptive_work.peak_retained_states, adaptive.size());
        previous_anchor = segment_anchor;
        previous_last = static_cast<long double>(pair.segment_first + pair.segment_rows - 1) / fs;
        processed += pair.segment_rows;
        ++segments;
    }

    FineBulkBlock bulk_shape(const ObservationBlock& block) const {
        return {block.projection->rows(), tap_count, block.projection->indices(),
                block.projected, config.noise_variance,
                config.sample_rate_hz == 6000 && config.time_varying_channel && !config.helper_enabled};
    }

    void reset_bulk_statistics(std::size_t block, std::size_t states) {
        // Distinguish unwritten required fields from legitimate DATA zeros.
        constexpr auto missing = std::numeric_limits<double>::quiet_NaN();
        FineBulkStatistics unset;
        unset.correlations.fill({missing, missing});
        unset.gram.fill({missing, missing});
        bulk_statistics[block].assign(states, unset);
    }

    FineBulkWork confirmed_bulk_work(std::uint64_t expected_blocks, std::uint64_t expected_values) const {
        const auto completed = bulk->completed_work();
        if (!completed.executed || completed.statistics_blocks < bulk_work.statistics_blocks ||
            completed.statistics_blocks - bulk_work.statistics_blocks != expected_blocks ||
            completed.projected_values < bulk_work.projected_values ||
            completed.projected_values - bulk_work.projected_values != expected_values)
            throw Error("Robust bulk backend did not execute its complete public statistics work");
        return completed;
    }

    // The resident request of one prepared block, with its statistics in
    // `slot`. The backend then asks for each candidate once, in order. A
    // backend that renders this slot's tiles receives their addresses.
    bool resident_request(const ObservationBlock& block, std::size_t slot, std::size_t states,
                          std::uint64_t& projected_values, const std::stop_token& stop) {
        if (!block.observed || !block.line.empty()) return false;
        const auto shape = bulk_shape(block);
        if (shape.rows > bulk_offsets.size() || !bulk->supports_resident_statistics(shape)) return false;
        reset_bulk_statistics(slot, states);
        resident_progress[slot] = 0;
        resident_requests.push_back({shape,
            [this, &block, slot, states, rows = shape.rows, stop](std::size_t candidate, std::size_t first,
                    std::size_t take, const BulkMappedSink& sink) {
                if (candidate >= states || candidate != resident_progress[slot] || first || take != rows)
                    throw Error("Robust resident fine backend requested an invalid public block");
                if (addresses)
                    addresses->address(candidate, block.first, bulk_offsets, rows, sink, stop);
                else
                    basis->visit_pair(bulk_rows[candidate], taps(), block.first, bulk_offsets, rows, sink, stop);
                ++resident_progress[slot];
            }, bulk_statistics[slot], tile_source});
        resident_slots.push_back(slot);
        projected_values += static_cast<std::uint64_t>(states) * 2 * tap_count * shape.projection_indices.size();
        return true;
    }

    // Runs the gathered requests in one resident call and marks the slots
    // whose statistics it supplied. A declined call marks none. Addressed
    // sources map this call's rows, and their receipts are credited when
    // the call returns or fails.
    void resident_statistics(std::size_t states, std::uint64_t projected_values, const std::stop_token& stop) {
        std::fill(resident_done.begin(), resident_done.end(), std::uint8_t{0});
        const auto count = resident_requests.size();
        bool executed = false;
        if (count) {
            const struct Closing {
                MappedBasis::Addresses* addresses;
                ~Closing() { if (addresses) addresses->close(); }
            } closing{addresses.get()};
            if (addresses) addresses->open(bulk_rows, taps());
            executed = bulk->resident_statistics(resident_requests, states, stop);
        }
        resident_requests.clear();
        if (!executed) return;
        check_cancelled(stop);
        const auto receipt = confirmed_bulk_work(static_cast<std::uint64_t>(states) * count, projected_values);
        for (const auto slot : resident_slots) {
            if (resident_progress[slot] != states)
                throw Error("Robust resident fine backend did not map its complete public block");
            check_bulk_statistics(bulk_statistics[slot], tap_count);
            resident_done[slot] = 1;
        }
        bulk_work = receipt;
    }

    std::array<bool, 2> resident_statistics_for(const Pair& pair, std::size_t states,
        std::array<std::uint64_t, 2>& bulk_calls, const std::stop_token& stop) {
        resident_requests.clear();
        resident_slots.clear();
        std::uint64_t projected_values = 0;
        for (std::size_t i = 0; i < pair.extent->count; ++i)
            (void)resident_request(pair.blocks[i], i, states, projected_values, stop);
        resident_statistics(states, projected_values, stop);
        const std::array<bool, 2> completed{resident_done[0] != 0, resident_done[1] != 0};
        const auto count = static_cast<std::size_t>(completed[0]) + completed[1];
        bulk_calls[0] += count * nominal.size();
        bulk_calls[1] += count * adaptive.size();
        return completed;
    }

    // Statistics of one observed block that the resident route did not
    // supply: the host projection or the backend's own mapped statistics.
    void fallback_statistics(const ObservationBlock& block, std::size_t slot, std::size_t states,
                             ReceiverExecutor::Stage& stage, std::size_t& posted,
                             std::array<std::uint64_t, 2>& bulk_calls, std::stop_token leaf_stop) {
        const auto shape = bulk_shape(block);
        const auto rows = shape.rows;
        auto& output = bulk_statistics[slot];
        reset_bulk_statistics(slot, states);
        const auto expected_values = static_cast<std::uint64_t>(states) * 2 * tap_count *
            shape.projection_indices.size();
        if (const auto capacity = bulk->host_projection_capacity()) {
            host_mapped_statistics(shape, block, output, stage, posted, capacity);
            bulk_calls[0] += nominal.size() * (1 + (rows - 1) / maximum_basis_rows);
            bulk_calls[1] += adaptive.size() * (1 + (rows - 1) / maximum_basis_rows);
            check_bulk_statistics(output, tap_count);
            bulk->complete_host_statistics(states, expected_values);
        } else {
            bulk_progress.assign(states, 0);
            bulk->statistics(shape, states, [&](std::size_t candidate, std::size_t first,
                    std::size_t take, const BulkMappedSink& sink) {
                if (candidate >= states || first != bulk_progress[candidate] || first > rows || take > rows - first ||
                    !take || take > bulk_offsets.size())
                    throw Error("Robust bulk backend requested a map outside its public block");
                basis->visit_pair(bulk_rows[candidate], taps(), block.first + first,
                                  bulk_offsets, take, sink, leaf_stop);
                bulk_progress[candidate] += take;
                ++bulk_calls[candidate < nominal.size() ? 0 : 1];
            }, output, leaf_stop);
            if (!std::all_of(bulk_progress.begin(), bulk_progress.end(), [&](auto extent) { return extent == rows; }))
                throw Error("Robust bulk backend did not process its complete public block");
        }
        check_cancelled(leaf_stop);
        const auto receipt = confirmed_bulk_work(states, expected_values);
        if (!bulk->host_projection_capacity()) check_bulk_statistics(output, tap_count);
        bulk_work = receipt;
    }

    // Every fixed-affine map of the slot, as each dispatch would build them.
    void fixed_affine_rows() {
        bulk_rows.clear();
        for (const auto& state : nominal)
            bulk_rows.push_back(mapped_row(config, plan, grid, coordinates[state.key.group], state.key,
                                           first_anchor, true));
    }

    // Device statistics of every block of a window of dispatches, on the
    // owner, in one resident call.
    void window_statistics(std::size_t index, const std::stop_token& leaf_stop) {
        check_cancelled(leaf_stop);
        const auto [begin, end] = windows[index];
        const auto base = window_base(index), statistics = statistics_base(index), calls = calls_base(index);
        const auto states = nominal.size();
        time(times.bulk_statistics_seconds, [&] {
            resident_requests.clear();
            resident_slots.clear();
            std::uint64_t projected_values = 0;
            for (auto dispatch = begin; dispatch != end; ++dispatch) {
                const auto local = dispatch - begin;
                window_calls[calls + local] = 0;
                const auto& pair = ring[base + local];
                for (std::size_t i = 0; i < pair.extent->count; ++i)
                    (void)resident_request(pair.blocks[i], statistics + 2 * local + i, states, projected_values,
                                           leaf_stop);
            }
            resident_statistics(states, projected_values, leaf_stop);
        });
    }

    // The window's blocks outside the resident geometry, each by the native
    // route. They run after the previous window's states have advanced, so
    // their own leaves do not wait behind those states.
    void window_fallbacks(std::size_t index, ReceiverExecutor::Stage& stage, std::size_t& posted) {
        const auto leaf_stop = stage.stop_token();
        check_cancelled(leaf_stop);
        const auto [begin, end] = windows[index];
        const auto base = window_base(index), statistics = statistics_base(index), calls = calls_base(index);
        const auto states = nominal.size();
        time(times.bulk_statistics_seconds, [&] {
            for (auto dispatch = begin; dispatch != end; ++dispatch) {
                const auto local = dispatch - begin;
                const auto& pair = ring[base + local];
                for (std::size_t i = 0; i < pair.extent->count; ++i) {
                    check_cancelled(leaf_stop);
                    const auto slot = statistics + 2 * local + i;
                    if (!pair.blocks[i].observed) continue;
                    if (resident_done[slot]) {
                        window_calls[calls + local] += states;
                        continue;
                    }
                    std::array<std::uint64_t, 2> native_calls{};
                    fallback_statistics(pair.blocks[i], slot, states, stage, posted, native_calls, leaf_stop);
                    window_calls[calls + local] += native_calls[0];
                }
            }
        });
    }

    // The ring group, statistics half and call counters of a window.
    std::size_t window_base(std::size_t index) const { return (index % 3) * window_dispatches; }
    std::size_t statistics_base(std::size_t index) const { return (index % 2) * 2 * window_dispatches; }
    std::size_t calls_base(std::size_t index) const { return (index % 2) * window_dispatches; }

    // The owner's receipts and segment bookkeeping for a window whose states
    // have advanced, one dispatch at a time in public order.
    void close_window(std::size_t index, std::size_t nominal_models) {
        const auto [begin, end] = windows[index];
        const auto base = window_base(index), calls = calls_base(index);
        const auto fixed = nominal.size();
        for (auto dispatch = begin; dispatch != end; ++dispatch) {
            const auto local = dispatch - begin;
            const auto& pair = ring[base + local];
            const auto& extent = *pair.extent;
            // The first segment of a call opened before its statistics.
            if ((index || local) && extent.opens_segment) open_segment(extent);
            add_work(basis_rows, pair.observed_rows * 2 * tap_count * fixed);
            if (observation.record_work)
                charge(nominal_work, pair, fixed, window_calls[calls + local], nominal_state_work[local],
                       nominal_models);
            scored_blocks += extent.count;
            if (extent.closes_segment) close_segment(extent);
        }
    }

    // Device statistics for both lanes of one dispatch, on the owner.
    std::array<std::uint64_t, 2> bulk_statistics_for(const Pair& pair, ReceiverExecutor::Stage& stage,
                                                     std::size_t& posted) {
        const auto leaf_stop = stage.stop_token();
        check_cancelled(leaf_stop);
        std::array<std::uint64_t, 2> bulk_calls{};
        if (!pair.observed_rows) return bulk_calls;
        const auto states = nominal.size() + adaptive.size();
        time(times.bulk_statistics_seconds, [&] {
            bulk_rows.clear();
            const auto append = [&](const std::vector<State>& lane, bool fixed) {
                for (const auto& state : lane)
                    bulk_rows.push_back(mapped_row(config, plan, grid, coordinates[state.key.group], state.key,
                                                   fixed ? first_anchor : segment_anchor, fixed));
            };
            append(nominal, true);
            append(adaptive, false);
            const auto resident = resident_statistics_for(pair, states, bulk_calls, leaf_stop);
            for (std::size_t i = 0; i < pair.extent->count; ++i) {
                check_cancelled(leaf_stop);
                if (!pair.blocks[i].observed || resident[i]) continue;
                fallback_statistics(pair.blocks[i], i, states, stage, posted, bulk_calls, leaf_stop);
            }
        });
        return bulk_calls;
    }

    void host_mapped_statistics(const FineBulkBlock& shape, const ObservationBlock& block,
                                std::span<FineBulkStatistics> output, ReceiverExecutor::Stage& stage,
                                std::size_t& posted, std::size_t capacity) {
        const auto stop = stage.stop_token();
        const bool device = block.line.empty() && bulk->begin_host_projection(shape, stop);
        const auto columns = 2 * shape.taps;
        const auto modes = shape.projection_indices.size();
        const auto input_stride = shape.rows + bulk->host_projection_stages_peaks();
        const auto batch = device ? capacity / columns : output.size();
        if (!batch) throw Error("Robust device template batch cannot fit one candidate");
        for (std::size_t first = 0; first < output.size(); first += batch) {
            const auto width = std::min(batch, output.size() - first);
            const auto statistics = output.subspan(first, width);
            std::span<std::complex<float>> mapped;
            if (device) {
                const auto samples = width * columns * input_stride;
                mapped = bulk->host_projection_staging(samples);
                if (mapped.empty()) mapped = std::span(rendered_templates).first(samples);
                if (mapped.size() != samples) throw Error("Robust device template staging returned an invalid extent");
            }
            template_wave = {&block, statistics, mapped, first, posted};
            stage.post(width);
            stage.wait_urgent();
            posted += width;
            template_wave = {};
            if (!device) continue;
            const auto projection = std::span(projected_templates).first(width * columns * modes);
            bulk->project_host_templates(mapped, projection, stop);
            for (std::size_t index = 0; index < width; ++index) {
                const auto value = projection.subspan(index * columns * modes, columns * modes);
                projected_statistics(std::span<const std::complex<float>>(value), modes, tap_count, block,
                    config.noise_variance, statistics[index].correlations, statistics[index].gram);
            }
        }
    }

    void map_template_leaf(unsigned lane, std::size_t index, std::stop_token stop) {
        const auto& block = *template_wave.block;
        const auto& row = bulk_rows[template_wave.first_state + index];
        auto& result = template_wave.statistics[index];
        result = {};
        if (!template_wave.mapped.empty()) {
            const auto raw = render_templates(*basis, row, taps(), block, scratch[lane], stop);
            result.nonzero_basis = std::ranges::any_of(raw, [](Complex value) { return value != Complex{}; });
            if (bulk->host_projection_stages_peaks()) {
                const auto rows = block.projection->rows();
                const auto stride = rows + 1;
                const auto columns = 2 * tap_count;
                auto samples = template_wave.mapped.subspan(index * columns * stride, columns * stride);
                for (std::size_t column = 0; column < columns; ++column) {
                    const auto source = raw.subspan(column * rows, rows);
                    stage_nvfp4_lane(source, samples.subspan(column * stride, stride));
                }
            } else {
                auto samples = template_wave.mapped.subspan(index * raw.size(), raw.size());
                std::ranges::transform(raw, samples.begin(), [](Complex value) { return std::complex<float>(value); });
            }
        } else {
            const auto float_rows = observation_float_scratch.size();
            sufficient_statistics(*basis, row, taps(), block, config.noise_variance, scratch[lane],
                result.correlations, result.gram, result.nonzero_basis, stop,
                std::span(worker_float_scratch).subspan(lane * float_rows, float_rows));
        }
    }

    SlowGainState* slow_gain_for(std::size_t index) {
        return slow_nominal.empty() ? nullptr : &slow_nominal[index];
    }

    std::array<double, 2> nominal_scores(std::array<double, 2>* slow_evidence = nullptr) const {
        auto scores = bit_scores(nominal);
        if (slow_nominal.empty()) return scores;
        const auto slow_scores = bit_scores(slow_nominal);
        if (slow_evidence) *slow_evidence = slow_scores;
        for (std::size_t bit = 0; bit < scores.size(); ++bit)
            scores[bit] = log_add(scores[bit] + std::log1p(-slow_model_weight),
                                  slow_scores[bit] + std::log(slow_model_weight));
        return scores;
    }

    void retain_affine_coordinates(ReceiverSlotScore& score) const {
        const auto per_bit = [](const auto& weights, std::size_t bit) {
            auto result = -infinity;
            for (std::size_t model = 0; model < weights.size() / 2; ++model)
                result = log_add(result, weights[model * 2 + bit]);
            return result;
        };
        const auto ordinary_weight = std::log1p(-slow_model_weight);
        const auto slow_weight = std::log(slow_model_weight);
        std::array<double, 2> best{-infinity, -infinity};
        // The fixed lane keeps its residual timing states in group order.
        // Weight a public coordinate by the sum over its entire group.
        for (std::size_t i = 0; i < nominal.size();) {
            const auto group = nominal[i].key.group;
            std::array<double, 2> group_evidence{-infinity, -infinity};
            bool nonzero_basis = false;
            for (; i < nominal.size() && nominal[i].key.group == group; ++i) {
                const auto& state = nominal[i];
                nonzero_basis |= state.nonzero_basis;
                for (std::size_t bit = 0; bit < group_evidence.size(); ++bit) {
                    auto evidence = per_bit(state.weights, bit);
                    if (!slow_nominal.empty())
                        evidence = log_add(evidence + ordinary_weight,
                            per_bit(slow_nominal[i].weights, bit) + slow_weight);
                    group_evidence[bit] = log_add(group_evidence[bit], evidence);
                }
            }
            if (!nonzero_basis) continue;
            const auto& coordinate = coordinates[group];
            for (std::size_t bit = 0; bit < best.size(); ++bit) {
                if (group_evidence[bit] <= best[bit]) continue;
                best[bit] = group_evidence[bit];
                score.affine_tracking[bit] = ReceiverSlotScore::TimingProposal{coordinate.epoch_offset_seconds,
                    coordinate.arrival_seconds, coordinate.frequency_hz, coordinate.clock_ppm};
            }
        }
    }

    void update(State& state, const MappedBasisRow& row, const Pair& pair,
                const std::array<const FineBulkStatistics*, 2>& prepared, unsigned lane,
                StateBlockWork* work, const std::stop_token& leaf_stop, SlowGainState* slow = nullptr) {
        check_cancelled(leaf_stop);
        const auto float_rows = observation_float_scratch.size();
        const auto float_scratch = std::span(worker_float_scratch).subspan(lane * float_rows, float_rows);
        advance_state(state, row, *basis, taps(), pair.observations(), prepared, pair.transitions,
                      config.noise_variance, scratch[lane], work, leaf_stop, 1., slow, pair.slow_transitions,
                      additional_gain.stationary_fading, float_scratch);
    }

    // The tiles one keyed source must render for later dispatches. That
    // preparation is serial per source, and would otherwise stall every
    // state of that source when its dispatch arrives. Exactly the tiles the
    // fixed-affine states will request are filled, so receipts are unchanged.
    void fill_ahead(std::size_t dispatch, std::size_t group, std::stop_token leaf_stop) {
        // The first leaf also covers the dispatches before its own target.
        const auto last = dispatch + 1 + fill_depth;
        for (auto target = dispatch ? last : std::size_t{1}; target <= last && target < extents.size(); ++target) {
            const auto& extent = extents[target];
            auto first = extent.first;
            for (std::size_t block = 0; block < extent.count; ++block) {
                const auto rows = extent.lengths[block];
                if (extent.observed[block]) {
                    for (const auto state : epoch_states[group]) {
                        for (std::size_t offset = 0; offset < rows; offset += maximum_basis_rows)
                            basis->prepare_pair(nominal_rows[state], taps(), first + offset,
                                                std::min(maximum_basis_rows, rows - offset), leaf_stop);
                    }
                }
                first += rows;
            }
        }
    }

    // Urgent leaves gate the next chronological step: the dispatch ahead is
    // observed while this dispatch's adaptive states, whose pruning and
    // prediction follow, are scored.
    void urgent(unsigned lane, std::size_t leaf, const std::stop_token& leaf_stop) {
        // A window's state leaves can still be running when the next
        // window posts the native leaves of a block outside the device route.
        if (window.active && leaf >= window.first_leaf && leaf - window.first_leaf < window.leaves) {
            advance_window(lane, leaf - window.first_leaf, leaf_stop);
            return;
        }
        if (template_wave.block) {
            map_template_leaf(lane, leaf - template_wave.first_leaf, leaf_stop);
            return;
        }
        auto index = leaf - wave.first_leaf;
        if (wave.ahead) {
            if (!index) {
                observe(*wave.ahead, extents[static_cast<std::size_t>(wave.pair->extent - extents.data()) + 1],
                        leaf_stop);
                wave.stage->release(wave.release);
                return;
            }
            --index;
        }
        const auto& pair = *wave.pair;
        if (bulk) {
            const bool fixed = index < nominal.size();
            auto& state = fixed ? nominal[index] : adaptive[index - nominal.size()];
            std::array<const FineBulkStatistics*, 2> prepared{};
            for (std::size_t i = 0; i < pair.extent->count; ++i)
                if (pair.blocks[i].observed) prepared[i] = &bulk_statistics[i][index];
            StateBlockWork* work = !observation.record_work ? nullptr :
                fixed ? &nominal_state_work[0][index] : &adaptive_state_work[index - nominal.size()];
            update(state, {}, pair, prepared, lane, work, leaf_stop, fixed ? slow_gain_for(index) : nullptr);
            return;
        }
        auto& state = adaptive[index];
        update(state, mapped_row(config, plan, grid, coordinates[state.key.group], state.key, wave.anchor, false),
               pair, {}, lane, observation.record_work ? &adaptive_state_work[index] : nullptr, leaf_stop);
    }

    // One fixed-affine state through every dispatch of the window, in public
    // order. States are independent chains, so each leaf owns one of them.
    void advance_window(unsigned lane, std::size_t index, const std::stop_token& leaf_stop) {
        for (auto dispatch = window.begin; dispatch != window.end; ++dispatch) {
            const auto local = dispatch - window.begin;
            const auto& pair = ring[window.base + local];
            std::array<const FineBulkStatistics*, 2> prepared{};
            for (std::size_t i = 0; i < pair.extent->count; ++i)
                if (pair.blocks[i].observed)
                    prepared[i] = &bulk_statistics[window.statistics + 2 * local + i][index];
            update(nominal[index], {}, pair, prepared, lane,
                   observation.record_work ? &nominal_state_work[local][index] : nullptr, leaf_stop,
                   slow_gain_for(index));
        }
    }

    // A window's observation leaves: one per dispatch, or one for all.
    std::size_t observation_begin(std::size_t index) const {
        return dispatch_observation ? windows[index][0] : index;
    }
    std::size_t observation_end(std::size_t index) const {
        return dispatch_observation ? windows[index][1] : index + 1;
    }

    // The owner places a window's blocks in chronological order, into the
    // ring group that the window three before it has released, before any
    // of its observation leaves can start.
    void open_window(std::size_t index, std::stop_token stop) {
        const auto [begin, end] = windows[index];
        const auto base = window_base(index);
        time(times.observation_seconds, [&] {
            for (auto dispatch = begin; dispatch != end; ++dispatch)
                open_dispatch(ring[base + dispatch - begin], extents[dispatch], false, stop);
        });
    }

    // Projects the observed blocks of one placed dispatch. Dispatches of a
    // window are independent: each owns its ring entry, and the lane its
    // scratch. Arithmetic is that of a serial observation.
    void project_dispatch(std::size_t dispatch, std::span<Complex> fft, std::span<FineFloatComplex> float_scratch,
                          const std::stop_token& leaf_stop) {
        const auto began = observation.record_work ? FineClock::now() : FineClock::time_point{};
        auto& pair = ring[window_base(dispatch / window_dispatches) + dispatch % window_dispatches];
        for (auto& block : std::span(pair.blocks).first(pair.extent->count))
            if (block.observed) project(block, fft, float_scratch, leaf_stop);
        if (observation.record_work)
            observation_nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(
                FineClock::now() - began).count();
    }

    // The window's mode and quiet-energy receipts and diagnostic witnesses,
    // in chronological order, once every one of its dispatches is projected.
    void close_observation(std::size_t index, std::stop_token stop) {
        const auto [begin, end] = windows[index];
        const auto base = window_base(index);
        time(times.observation_seconds, [&] {
            for (auto dispatch = begin; dispatch != end; ++dispatch) {
                const auto& pair = ring[base + dispatch - begin];
                for (const auto& block : pair.observations())
                    if (block.observed) count(block);
                if (observation.observed) observation.observed(pair.extent->first, stop);
                check_cancelled(stop);
            }
        });
        if (observation.record_work)
            times.worker_observation_seconds += 1e-9 * static_cast<double>(observation_nanoseconds.exchange(0));
    }

    // Compiled work releases one independent observation. Native work is
    // dispatch-major: each fixed-affine state, then that dispatch's tile fills.
    // The native owner waits only for the states.
    void background(unsigned lane, std::size_t leaf, const std::stop_token& leaf_stop) {
        if (window_dispatches) {
            if (dispatch_observation) {
                const auto float_rows = observation_float_scratch.size();
                project_dispatch(leaf, scratch[lane].fft,
                                 std::span(worker_float_scratch).subspan(lane * float_rows, float_rows), leaf_stop);
                return;
            }
            const auto [begin, end] = windows[leaf];
            for (auto dispatch = begin; dispatch != end; ++dispatch)
                project_dispatch(dispatch, observation_scratch, observation_float_scratch, leaf_stop);
            return;
        }
        if (bulk) {
            // Only one CPU observation is released beside the owner's device
            // call. Its address must not depend on wave, which the owner may
            // update while this independent projection is still running.
            const auto next = leaf + 1;
            observe(ring[(call_serial + next) % ring.size()], extents[next], leaf_stop);
            return;
        }
        const auto local = leaf / leaf_block, index = leaf % leaf_block;
        if (index >= nominal.size()) {
            fill_ahead(local, index - nominal.size(), leaf_stop);
            return;
        }
        const auto ready = [&] { return nominal_progress[index] == local; };
        if (std::unique_lock lock(chain_mutex); !ready()) {
            // This state's previous dispatch is still finishing on another
            // lane, as happens when few states share many lanes. The wake
            // registration must outlive the lock that its callback takes.
            lock.unlock();
            std::stop_callback wake(leaf_stop, [this] {
                std::lock_guard cancelling(chain_mutex);
                chain_advanced.notify_all();
            });
            lock.lock();
            chain_advanced.wait(lock, [&] { return ready() || leaf_stop.stop_requested(); });
            lock.unlock();
        }
        check_cancelled(leaf_stop);
        const auto slot = (call_serial + local) % ring.size();
        update(nominal[index], nominal_rows[index], ring[slot], {}, lane,
               observation.record_work ? &nominal_state_work[slot][index] : nullptr, leaf_stop,
               slow_gain_for(index));
        {
            std::lock_guard lock(chain_mutex);
            nominal_progress[index] = static_cast<std::uint32_t>(local + 1);
        }
        chain_advanced.notify_all();
    }

    void charge(FineLaneWork& work, const Pair& pair, std::size_t size, std::uint64_t calls,
                std::span<const StateBlockWork> states, std::size_t model_count = models) {
        add_work(work.basis_rows, pair.observed_rows * 2 * tap_count * size);
        add_work(work.basis_calls, calls);
        add_work(work.projection_modes, pair.modes * 2 * tap_count * size);
        add_work(work.gaussian_updates, pair.extent->count * size * 3 * model_count);
        for (const auto& state : states) {
            add_work(work.gaussian_null_skips, state.gaussian_null_skips);
            add_work(work.positive_basis_blocks, state.positive_basis_blocks);
            work.projected_template_energy += state.template_energy;
        }
    }

    // Scores every dispatch whose segment is wholly available. The adaptive
    // lane is chronological: predict, score, prune. The fixed-affine lane has
    // no such barrier, so its leaves run in the background, at most one
    // dispatch ahead, and fill the lanes that the adaptive lane leaves idle
    // during its serial steps and at the end of each of its waves.
    // Compiled statistics stay serial on the owner while a CPU lane observes
    // the next dispatch in the same two-pair ring.
    // Resident statistics for a window of dispatches at a time. The device
    // maps, projects and reduces every block of the window in one call while
    // the next window is observed and the lanes advance the states of the
    // previous one. The owner places the next window's blocks before release,
    // lanes project one dispatch each (or the whole window on one lane
    // without per-lane transform scratch), and the owner adds their mode and
    // quiet-energy receipts in public order. Blocks outside the resident
    // geometry follow once those states have advanced. Each state advances
    // through a window's dispatches in public order, and the owner then
    // closes them in order.
    void run_windows() {
        windows.clear();
        for (std::size_t begin = 0; begin < extents.size(); begin += window_dispatches)
            windows.push_back({begin, std::min(extents.size(), begin + window_dispatches)});
        const auto fixed = nominal.size();
        const auto nominal_models = models + (slow_nominal.empty() ? 0 : slow_models);
        ReceiverExecutor::Stage stage(executor, plan.index, stage_workers, stop,
            [this](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) { urgent(lane, leaf, leaf_stop); },
            [this](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) { background(lane, leaf, leaf_stop); });
        std::size_t posted = 0;
        // Waits for the states of a window and closes its dispatches.
        const auto finish = [&](std::size_t index) {
            time(times.dispatch_seconds, [&] { stage.wait_urgent(); });
            window.active = false;
            close_window(index, nominal_models);
        };
        // As for a single dispatch: keep a leaf's original failure.
        const auto keep_leaf_failure = [&] {
            if (stage.stop_token().stop_requested()) stage.wait_urgent();
        };
        // Places a window's blocks, then lets lanes project them.
        const auto release = [&](std::size_t index) {
            try {
                open_window(index, stage.stop_token());
            } catch (...) {
                keep_leaf_failure();
                throw;
            }
            stage.release(observation_end(index));
        };
        // An observation is awaited while no state leaf is posted, so the
        // owner goes on to its own staging instead of taking state leaves.
        const auto observed = [&](std::size_t index) {
            time(times.dispatch_seconds, [&] {
                stage.wait_background(observation_end(index), observation_begin(index));
            });
            try {
                close_observation(index, stage.stop_token());
            } catch (...) {
                keep_leaf_failure();
                throw;
            }
        };
        release(0);
        observed(0);
        for (std::size_t index = 0; index < windows.size(); ++index) {
            const bool more = index + 1 < windows.size();
            // The window three before the next one has released its group.
            if (more) release(index + 1);
            if (!index) {
                open_segment(extents.front());
                fixed_affine_rows();
            }
            try {
                window_statistics(index, stage.stop_token());
            } catch (...) {
                keep_leaf_failure();
                throw;
            }
            if (index) finish(index - 1);
            try {
                window_fallbacks(index, stage, posted);
            } catch (...) {
                keep_leaf_failure();
                throw;
            }
            if (more) observed(index + 1);
            const auto [begin, end] = windows[index];
            window = {begin, end, window_base(index), statistics_base(index), posted, fixed, true};
            time(times.dispatch_seconds, [&] { stage.post(fixed); });
            posted += fixed;
        }
        finish(windows.size() - 1);
        pair_serial += extents.size();
        window = {};
        nominal_nonzero |= std::any_of(nominal.begin(), nominal.end(),
            [](const State& state) { return state.nonzero_basis; });
    }

    void run() {
        extents.clear();
        for (Extent next; plan_pair(next, extents.empty() ? nullptr : &extents.back());) extents.push_back(next);
        if (extents.empty()) return;
        if (window_dispatches) return run_windows();
        const auto serial = pair_serial;
        observe(ring[serial % ring.size()], extents.front(), stop);
        const bool overlap = !bulk;
        const auto fixed = nominal.size();
        const auto nominal_models = models + (slow_nominal.empty() ? 0 : slow_models);
        call_serial = serial;
        std::fill(nominal_progress.begin(), nominal_progress.end(), 0U);
        ReceiverExecutor::Stage stage(executor, plan.index, stage_workers, stop,
            [this](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) { urgent(lane, leaf, leaf_stop); },
            [this](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) { background(lane, leaf, leaf_stop); });
        std::size_t posted = 0;
        for (std::size_t local = 0; local < extents.size(); ++local) {
            auto& pair = ring[(serial + local) % ring.size()];
            const auto& extent = *pair.extent;
            const bool more = local + 1 < extents.size();
            if (extent.opens_segment) open_segment(extent);
            if (overlap && !local) {
                // The keyed sources are known once the first segment opens.
                leaf_block = fixed + (fill_depth ? epoch_states.size() : 0);
                stage.release(fixed);
            }
            // Observing the dispatch ahead releases its states, and with
            // them this dispatch's tile fills.
            wave = {&pair, overlap && more ? &ring[(serial + local + 1) % ring.size()] : nullptr, &stage, posted,
                    (local + 1) * leaf_block + fixed, segment_anchor};
            // The backend stays on the owner, while a spare lane projects the
            // next immutable input pair. With one lane retain serial order.
            if (bulk && more && stage.lanes() > 1) stage.release(local + 1);
            std::array<std::uint64_t, 2> bulk_calls{};
            if (bulk) {
                try {
                    bulk_calls = bulk_statistics_for(pair, stage, posted);
                    wave.first_leaf = posted;
                } catch (...) {
                    // A backend may report cancellation after the ahead leaf
                    // failed. Preserve that original stage failure; the stage
                    // destructor drains its borrowed observation before exit.
                    if (stage.stop_token().stop_requested()) stage.wait_urgent();
                    throw;
                }
            }
            const auto leaves = (wave.ahead ? 1 : 0) + adaptive.size() + (overlap ? 0 : fixed);
            time(times.dispatch_seconds, [&] {
                stage.post(leaves);
                stage.wait_urgent();
            });
            posted += leaves;
            add_work(basis_rows, pair.observed_rows * 2 * tap_count * (fixed + adaptive.size()));
            if (observation.record_work) {
                if (adaptive_enabled())
                    charge(adaptive_work, pair, adaptive.size(), bulk ? bulk_calls[1] : pair.basis_calls * adaptive.size(),
                           std::span(adaptive_state_work).first(adaptive.size()));
                if (!overlap)
                    charge(nominal_work, pair, fixed, bulk_calls[0], nominal_state_work[0], nominal_models);
            }
            scored_blocks += extent.count;
            if (extent.closes_segment) close_segment(extent);
            if (overlap) {
                // Earlier dispatches' tile fills may still be running.
                time(times.dispatch_seconds, [&] {
                    stage.wait_background(local * leaf_block + fixed, local * leaf_block);
                });
                if (observation.record_work)
                    charge(nominal_work, pair, fixed, pair.basis_calls * fixed,
                           nominal_state_work[(serial + local) % ring.size()], nominal_models);
            } else if (more) {
                if (stage.lanes() > 1) {
                    time(times.dispatch_seconds, [&] { stage.wait_background(local + 1, local); });
                } else {
                    observe(ring[(serial + local + 1) % ring.size()], extents[local + 1], stage.stop_token());
                }
            }
        }
        // A fill counts its miss before it renders. Let every one finish.
        if (overlap) stage.wait_background((extents.size() - 1) * leaf_block + fixed);
        pair_serial = serial + extents.size();
        wave = {};
        nominal_nonzero |= std::any_of(nominal.begin(), nominal.end(),
            [](const State& state) { return state.nonzero_basis; });
    }
};

FineSession::FineSession(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                         FineObservation observation, std::span<const Complex> iq,
                         std::span<const std::uint8_t> valid, std::span<const FineCoordinate> coordinates,
                         double projected_rate, double clock_gap, unsigned workers,
                         std::size_t scratch_limit, std::stop_token stop, ReceiverExecutor* executor,
                         AffineGainEvaluation evaluation) {
    const auto began = observation.record_work ? FineClock::now() : FineClock::time_point{};
    impl_ = std::make_unique<Impl>(config, slot, key, observation, iq, valid, coordinates,
                                   projected_rate, clock_gap, workers, scratch_limit, stop, executor, evaluation);
    if (observation.record_work) impl_->times.preparation_seconds = seconds_since(began);
}

FineSession::~FineSession() = default;

void FineSession::advance_to(std::size_t available_rows) {
    auto& p = *impl_;
    if (p.finished || p.poisoned) throw Error("fine session cannot advance after finish or failed work");
    if (available_rows < p.available || available_rows > p.observation.rows)
        throw Error("fine checkpoint regresses or exceeds its immutable observation");
    try {
        check_cancelled(p.stop);
        // Production input was checked by the complete slot driver before
        // Helper/coarse work. Diagnostic probes and explicit masks have no
        // such witness and validate only the newly exposed samples.
        if (p.observation.kind != FineRunKind::operational || !p.validity.empty()) {
            p.time(p.times.input_check_seconds, [&] {
                for (auto i = p.available; i < available_rows; ++i) {
                    if (!(i % 4096)) check_cancelled(p.stop);
                    const auto mask = p.validity.empty() ? 1 : p.validity[i];
                    if (mask > 1 || !std::isfinite(p.iq[i].real()) || !std::isfinite(p.iq[i].imag()))
                        throw Error("fine input must be finite with a binary validity mask");
                    p.invalid_rows += mask == 0;
                }
            });
        }
        p.available = available_rows;
        p.run();
        check_cancelled(p.stop);
    } catch (...) {
        // Never retry a partially updated pair of likelihood lanes.
        p.poisoned = true;
        throw;
    }
}

FineProgress FineSession::progress() const {
    const auto& p = *impl_;
    if (p.poisoned) throw Error("fine session progress is unavailable after failed work");
    FineProgress result;
    result.observation = p.observation;
    result.public_slot = p.plan.index;
    result.pattern_epoch = p.plan.format.epoch;
    result.symbol_ordinal = p.plan.format.symbol_ordinal;
    result.available_rows = p.available;
    result.processed_rows = p.processed;
    result.segments = p.segments;
    result.blocks = p.scored_blocks;
    result.block_rows = p.calendar.block_rows;
    result.segment_rows = p.calendar.segment_rows;
    result.last_block_rows = p.last_block_rows;
    result.requested_invalid_rows = p.invalid_rows;
    result.additional_discarded_rows = p.discarded_rows;
    result.erased_blocks = p.erased_blocks;
    result.observed_projection_modes = p.observed_modes;
    result.quiet_modes = p.quiet_modes;
    result.workspace_bytes = p.workspace;
    result.sample_rate_hz = p.config.sample_rate_hz;
    result.delay_step_seconds = p.grid.delay_step_seconds;
    result.rate_step_ppm = p.grid.rate_step_ppm;
    result.quiet_energy = p.quiet_energy;
    result.quiet_threshold = p.quiet_threshold;
    result.workers = p.workers;
    result.stage_workers = p.stage_workers;
    result.tap_count = static_cast<unsigned>(p.tap_count);
    result.rate_radius = p.grid.radius;
    result.observation_complete = p.observation_complete();
    result.representative_work = p.representative_work();
    result.finished = p.finished;
    result.slow_gain_coherence_seconds = p.additional_gain.coherence;
    result.cache = p.basis->report();
    if (p.bulk) result.bulk = p.bulk->receipt();
    result.times = p.times;
    const auto lane = [&](FineLaneWork& out, const FineLaneWork& work, const std::vector<State>& states,
                          const std::array<double, 2>& scores) {
        out = work;
        out.bit_scores = scores;
        out.retained_states.reserve(states.size());
        for (const auto& state : states) {
            out.retained_states.push_back(state.key);
            if (p.observation.record_work) out.retained_prior_mass += state.prior_mass;
        }
    };
    lane(result.initial_affine, p.nominal_work, p.nominal, p.nominal_scores(&result.slow_gain_bit_evidence));
    if (p.adaptive_enabled()) lane(result.adaptive, p.adaptive_work, p.adaptive, bit_scores(p.adaptive));
    for (const auto& worker : p.scratch) {
        result.times.worker_basis_statistics_seconds += worker.basis_statistics_seconds;
        result.times.worker_gaussian_seconds += worker.gaussian_seconds;
    }
    return result;
}

void FineSession::finish(ReceiverSlotScore& score) {
    auto& p = *impl_;
    if (p.finished || p.poisoned) throw Error("fine session cannot finish after finish or failed work");
    try {
        p.time(p.times.finish_seconds, [&] {
            check_cancelled(p.stop);
            const auto nominal = p.nominal_scores();
            const bool all_valid = p.observation_complete() && !p.invalid_rows;
            if (all_valid && p.quiet_modes) p.quiet_threshold = quiet_noise_threshold(p.quiet_modes);
            check_cancelled(p.stop);

            score.index = p.plan.index;
            score.first_sample = p.plan.first_sample;
            score.stop_sample = p.plan.stop_sample;
            score.quiet_first_sample = p.plan.quiet_first_sample;
            score.quiet_stop_sample = p.plan.quiet_stop_sample;
            score.consumed_samples = p.processed;
            score.sample_rate_hz = p.config.sample_rate_hz;
            score.decision = SlotDecision::ambiguous;
            score.bit.reset();
            score.operational_passes_conditional_bound = false;
            score.all_valid = all_valid;
            score.complete = p.observation.kind == FineRunKind::operational &&
                p.observation_complete() && p.representative_work() && p.scored_blocks;
            score.zero_h0_verified = p.scored_blocks != 0;
            score.noise_statistics_valid = score.complete && score.all_valid && p.quiet_modes;
            score.bit_evidence = nominal;
            score.affine_tracking = {};
            if (score.complete && score.all_valid) p.retain_affine_coordinates(score);
            score.slow_gain_coherence_seconds = p.additional_gain.coherence;
            score.affine_stationary_fading_gain = p.additional_gain.stationary_fading;
            score.adaptive_diagnostics = p.adaptive_enabled() && p.scored_blocks;
            score.adaptive_bit_evidence.fill(-infinity);
            score.combined_bit_evidence.fill(-infinity);
            if (score.adaptive_diagnostics) {
                score.adaptive_bit_evidence = bit_scores(p.adaptive);
                for (std::size_t bit = 0; bit < 2; ++bit)
                    score.combined_bit_evidence[bit] = log_add(nominal[bit], score.adaptive_bit_evidence[bit]) - log_two;
            }
            score.quiet_energy = p.quiet_energy;
            score.quiet_threshold = p.quiet_threshold;
            score.quiet_modes = p.quiet_modes;
            score.fine_candidates = p.coordinates.size();
            score.affine_blocks = p.scored_blocks;
            score.adaptive_blocks = score.adaptive_diagnostics ? p.scored_blocks : 0;
            score.adaptive_peak_states = p.adaptive_work.peak_emission_states;
            score.adaptive_transition_edges = p.adaptive_edges;
            score.basis_rows = p.basis_rows;
            p.finished = true;
        });
    } catch (...) {
        p.poisoned = true;
        throw;
    }
}

void score_fine(const ReceiverConfig& config, const ReceiverSlotPlan& plan, const Crypto& key,
                std::span<const Complex> iq, std::span<const FineCoordinate> coordinates,
                double projected_sample_rate_hz, double coarse_clock_gap_ppm,
                unsigned workers, ReceiverSlotScore& score, std::size_t scratch_limit, std::stop_token stop,
                ReceiverExecutor* executor, AffineGainEvaluation evaluation, FineLanes lanes) {
    auto observation = whole_slot(plan);
    observation.lanes = lanes;
    FineSession session(config, plan, key, observation, iq, {}, coordinates, projected_sample_rate_hz,
                        coarse_clock_gap_ppm, workers, scratch_limit, stop, executor, evaluation);
    session.advance_to(iq.size());
    session.finish(score);
}

namespace {
constexpr std::size_t maximum_varying_coordinates = 24;
constexpr std::size_t maximum_segmented_timing_coordinates = 8;
constexpr double varying_power = 1. / 16;
constexpr std::array tracking_powers{varying_power, 1. / 256, 1. / 512, 1. / 4096};
constexpr std::size_t maximum_tracking_states = 65;
constexpr double nominal_independent_gain_weight = 1. / 8;
// Consecutive public blocks scored per three-phase batch: their storage is
// charged in the varying quote. A statistics leaf covers about this many
// rows of one family item, so lanes stay inside a short basis window.
constexpr std::size_t maximum_varying_batch_bytes = 16U << 20;
constexpr std::size_t maximum_varying_batch_blocks = 256;
constexpr std::size_t varying_chunk_rows = 2048;

struct DriftGram {
    // Cholesky factor of I + A^H A/(noise variance * precision).
    double first = 1, second = 1, logdet = 0;
    Complex lower{};

    DriftGram(double g0, double g1, Complex cross, double precision, double variance) {
        const auto scale = 1 / (variance * precision);
        first = std::sqrt(1 + g0 * scale);
        lower = std::conj(cross) * (scale / first);
        const auto echo = 1 + g1 * scale;
        const auto second_pivot = echo - std::norm(lower);
        // Use the same identity term even when direct subtraction can no
        // longer resolve it. Bound this slower formula to delicate pivots.
        const auto fragile = second_pivot <= 256 * std::numeric_limits<double>::epsilon() * echo;
        second = std::sqrt(fragile ? gaussian_positive_second_pivot(g0, g1, cross, scale) : second_pivot);
        logdet = 2 * (std::log(first) + std::log(second));
        if (!(first > 0 && second > 0 && std::isfinite(logdet)))
            throw Error("Robust varying likelihood has invalid template Gram");
    }

    double score(Complex r0, Complex r1, double gain) const {
        const auto x = r0 / first;
        const auto y = (r1 - lower * x) / second;
        return (std::norm(x) + std::norm(y)) * gain - logdet;
    }
};

struct FrequencyTrackingPlan {
    std::size_t fft_rows = 0, states = 0, step_bins = 1;
    double step_hz = 0;
    std::int64_t radius = 0;
    bool coherent_gain_blocks = false;
    std::string_view frequency_extension_status;
};

FrequencyTrackingPlan tracking_plan(const ReceiverConfig& config, std::size_t block_rows,
                                     std::size_t final_rows, bool coherent_gain_blocks = false) {
    const auto bound = config.frequency_bound_hz + config.time_varying_atmospheric_hz;
    if (!(bound >= 0 && bound < .5 * config.sample_rate_hz)) return {};
    if (!coherent_gain_blocks && (config.time_varying_common_doppler_hz * block_rows /
        config.sample_rate_hz <= .25 || !(bound > 0)))
        return {};
    FrequencyTrackingPlan result;
    result.coherent_gain_blocks = coherent_gain_blocks;
    result.fft_rows = std::bit_ceil((coherent_gain_blocks ? 4 : 2) * final_rows);
    const auto bin_hz = config.sample_rate_hz /
        (config.helper_enabled ? 2 * block_rows : result.fft_rows);
    constexpr auto maximum_radius = static_cast<std::int64_t>(maximum_tracking_states / 2);
    result.step_bins = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(
        bound / (bin_hz * maximum_radius))));
    const auto resolve_radius = [&] {
        result.step_hz = result.step_bins * bin_hz;
        result.radius = static_cast<std::int64_t>(std::ceil(bound / result.step_hz));
        if (result.radius * result.step_hz < bound) ++result.radius;
    };
    resolve_radius();
    // The two floating-point quotients can round on opposite sides of
    // the same integer boundary. Coarsen the lattice, never drop its edge.
    if (result.radius > maximum_radius) {
        ++result.step_bins;
        resolve_radius();
    }
    if (result.radius > maximum_radius || result.radius * result.step_hz >= .5 * config.sample_rate_hz) return {};
    result.states = static_cast<std::size_t>(2 * result.radius + 1);
    return result;
}

// Keep the original common-frequency lattice, extending only its signed
// outer cells for a dominant ray carrying common and differential Doppler.
FrequencyTrackingPlan independent_tracking_plan(const ReceiverConfig& config, std::size_t block_rows,
                                               std::size_t final_rows) {
    auto plan = tracking_plan(config, block_rows, final_rows);
    if (!plan.states) return plan;
    plan.frequency_extension_status = "common_frequency_only";
    if (config.spread_doppler_hz == 0) return plan;
    const auto bound = config.frequency_bound_hz + config.time_varying_atmospheric_hz +
        std::abs(config.spread_doppler_hz);
    const auto extent = std::ceil(bound / plan.step_hz);
    if (!(std::isfinite(extent) && extent >= 0 && extent < static_cast<double>(INT64_MAX / 2) &&
          extent < static_cast<double>(SIZE_MAX / 2))) {
        plan.frequency_extension_status = "relative_doppler_extension_unavailable";
        return plan;
    }
    auto radius = std::max(plan.radius, static_cast<std::int64_t>(extent));
    if (radius * plan.step_hz < bound) ++radius;
    if (radius * plan.step_hz >= .5 * config.sample_rate_hz) {
        plan.frequency_extension_status = "relative_doppler_extension_unavailable";
        return plan;
    }
    if (radius > plan.radius) plan.frequency_extension_status = "common_plus_relative_doppler";
    plan.radius = radius;
    plan.states = checked_add(checked_multiply(static_cast<std::size_t>(radius), 2), 1);
    return plan;
}

using FrequencyWeights = std::array<double, maximum_tracking_states>;
using TrackingLogWeights = std::array<std::array<FrequencyWeights, tracking_powers.size()>, 2>;
struct GainCentroidState {
    std::array<std::vector<double>, 2> fixed_centroid, moving_centroid;
};
struct IndependentGainState {
    std::array<GainCentroidState, independent_gain_powers.size()> powers;
    std::vector<double> emissions, next;
    bool nonzero_basis = false;

    explicit IndependentGainState(std::size_t frequencies) :
        emissions(2 * independent_gain_powers.size() * frequencies), next(frequencies) {
        for (auto& power : powers)
            for (std::size_t bit = 0; bit < 2; ++bit) {
                power.fixed_centroid[bit].resize(frequencies);
                power.moving_centroid[bit].resize(frequencies);
            }
    }
};
struct CoherentGainState {
    ChannelPosterior posterior;
    double log_evidence = 0;
};
using TrackingPosteriors =
    std::array<std::array<std::array<CoherentGainState, maximum_tracking_states>, tracking_powers.size()>, 2>;
constexpr std::size_t span_maximum_rows = 32'768, span_maximum_blocks = 256, span_maximum_coordinates = 24;

double tracking_emission(double direct, double echo, Complex cross, Complex r0, Complex r1,
                         std::size_t taps, double power, double variance, unsigned rank_bound) {
    const double precision = taps / power;
    if (!rank_bound) return 0.;
    if (taps == 2 && rank_bound == 1) {
        const auto score = gaussian_rank_one_log_likelihood(direct, echo, cross, r0, r1,
            1. / (variance * precision), 1. / variance);
        if (!std::isfinite(score)) throw Error("Robust projected rank-one tracking evidence is nonfinite");
        return score;
    }
    const DriftGram gram(direct, echo, cross, precision, variance);
    const double scaling = 1. / (std::sqrt(precision) * std::sqrt(variance));
    return gram.score(r0 * scaling, r1 * scaling, 1 / variance);
}

// Normalize the two equal-power independent spatial gains against the
// same projected white null as the short tracked/fixed alternatives. The
// block statistics come from sufficient_statistics in the statistics phase.
void advance_independent_center(IndependentGainState& state, const FineBulkStatistics& statistics,
                                const ObservationBlock& observation) {
    state.nonzero_basis |= statistics.nonzero_basis;
    const auto rank = highest_observation_rank(observation);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto hypothesis = bit + 1;
        const auto g = std::span(statistics.gram).subspan(4 * hypothesis, 4);
        const auto r = std::span(statistics.correlations).subspan(2 * hypothesis, 2);
        // These statistics already include the inverse noise variance.
        for (std::size_t power = 0; power < state.powers.size(); ++power) {
            auto& evidence = state.powers[power].fixed_centroid[bit][0];
            evidence += tracking_emission(g[0].real(), g[3].real(), g[1], r[0], r[1], 2,
                                          independent_gain_powers[power], 1., rank);
            if (!std::isfinite(evidence)) throw Error("Robust nominal independent gain evidence is nonfinite");
        }
    }
}

double coherent_gain_update(ChannelPosterior& posterior, double direct, double echo, Complex cross,
                            Complex r0, Complex r1, std::size_t taps, double power, double variance,
                            double correlation, unsigned rank_bound) {
    const double path_power = power / taps;
    const std::array<double, 2> powers{path_power, path_power};
    const std::array<double, 4> transition{correlation * correlation,
        1 - correlation * correlation, correlation, 0.};
    std::array<Complex, 3 * 2> r{}, mean{};
    std::array<Complex, 3 * 4> g{}, uncertainty{};
    const auto square = taps * taps;
    r[taps] = r0 / variance;
    g[square] = direct / variance;
    if (taps == 2) {
        r[taps + 1] = r1 / variance;
        g[square + 1] = cross / variance;
        g[square + 2] = std::conj(g[square + 1]);
        g[square + 3] = echo / variance;
    }
    std::copy_n(posterior.mean.begin(), taps, mean.data() + taps);
    std::copy_n(posterior.uncertainty.begin(), square, uncertainty.data() + square);
    std::array<double, 3> score{};
    std::array<double, 2> diagnostic{};
    const auto correlation_data = reinterpret_cast<const double*>(r.data());
    const auto gram_data = reinterpret_cast<const double*>(g.data());
    const auto mean_data = reinterpret_cast<double*>(mean.data());
    const auto uncertainty_data = reinterpret_cast<double*>(uncertainty.data());
    const auto status = taps == 2 ? triad_gaussian_factor2_update_v1(1, 1, 1, 0, 1,
        correlation_data, gram_data, &rank_bound, powers.data(), transition.data(), mean_data,
        uncertainty_data, score.data(), diagnostic.data()) :
        triad_gaussian_update_v1(taps, 1, 1, 1, 0, 1, correlation_data, gram_data,
            powers.data(), transition.data(), mean_data, uncertainty_data, score.data(), diagnostic.data());
    if (status || score[0] != 0. || score[2] != 0.)
        throw Error("Robust coherent gain update failed (" + std::to_string(status) + ")");
    std::copy_n(mean.data() + taps, taps, posterior.mean.begin());
    std::copy_n(uncertainty.data() + square, square, posterior.uncertainty.begin());
    return score[1];
}

void update_frequency_weights(std::span<const double> evidence, std::span<double> previous, bool constant,
                              std::span<double> temporary = {}, std::stop_token stop = {}) {
    if (constant) {
        for (std::size_t i = 0; i < previous.size(); ++i) previous[i] += evidence[i];
        return;
    }
    std::array<double, maximum_tracking_states> storage{};
    auto next = temporary.empty() ? std::span(storage).first(previous.size()) : temporary;
    if (next.size() < previous.size()) throw ReceiverWorkspaceError("Robust frequency transition scratch is too small");
    const auto total = std::accumulate(previous.begin(), previous.end(), -infinity, log_add);
    // Markov prior: 96% stay/local walk and a 4% uniformly distributed
    // reacquisition. Reflect blocked edge steps into the stay mass.
    const auto reset = total + std::log(.04 / previous.size());
    constexpr double neighbor = .12, stay = .72;
    const auto log_neighbor = std::log(neighbor);
    const auto log_stay = std::log(stay);
    const auto log_edge = std::log(stay + neighbor);
    for (std::size_t i = 0; i < previous.size(); ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        auto predicted = log_add(previous[i] + (i == 0 || i + 1 == previous.size() ? log_edge : log_stay), reset);
        if (i) predicted = log_add(predicted, previous[i - 1] + log_neighbor);
        if (i + 1 < previous.size()) predicted = log_add(predicted, previous[i + 1] + log_neighbor);
        if (!std::isfinite(evidence[i])) throw Error("Robust tracked frequency evidence is nonfinite");
        next[i] = predicted + evidence[i];
    }
    std::copy_n(next.begin(), previous.size(), previous.begin());
}

} // namespace

std::int64_t projected_half_bin(std::size_t rows, double fs, double step_hz, std::int64_t radius, std::size_t state) {
    const auto half_bins = 2 * rows * step_hz * (static_cast<std::int64_t>(state) - radius) / fs;
    // Distinguish the two sampled Nyquist endpoints. Keep a rounding
    // error within the half-bin lattice even on the merged last block.
    const auto last = static_cast<std::int64_t>(rows) - 1;
    return std::clamp(static_cast<std::int64_t>(std::llround(half_bins)), -last, last);
}

namespace {

template<class Visitor>
void visit_projected_tracks(std::span<const Complex> templates, std::size_t taps, const ObservationBlock& block,
                            double fs, const FrequencyTrackingPlan& plan, WorkerScratch& scratch,
                            std::span<Complex> spectrum, const Visitor& visit, std::stop_token stop) {
    const auto& projection = *block.projection;
    const auto rows = projection.rows();
    const auto frequency_bin = [&](std::size_t i) {
        return projected_half_bin(rows, fs, plan.step_hz, plan.radius, i);
    };
    const auto indices = projection.indices();
    const auto rank_bound = highest_observation_rank(block);
    // The second lattice reuses the same columns shifted half a DFT bin.
    // Integer shifts then rotate the full transformed template; the actual
    // likelihood observes only orthogonal DATA bins outside Helper support.
    for (std::int64_t half = 0; half < 2; ++half) {
        for (std::size_t column = 0; column < 2 * taps; ++column) {
            auto values = spectrum.subspan(column * rows, rows);
            auto phase = Complex{1};
            const auto increment = std::polar(1., half * std::numbers::pi / rows);
            for (std::size_t i = 0; i < rows; ++i) {
                if (!(i % 4096)) check_cancelled(stop);
                values[i] = templates[column * rows + i] * phase;
                phase *= increment;
            }
            projection.transform_full(values, scratch.fft, stop);
        }
        for (std::size_t state = 0; state < plan.states; ++state) {
            if (!(state % 4096)) check_cancelled(stop);
            const auto bin = frequency_bin(state);
            const auto residue = (bin % 2 + 2) % 2;
            if (residue != half) continue;
            const auto shift = (bin - half) / 2;
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const auto column = bit * taps * rows;
                std::array<Complex, 2> correlation{};
                std::array<double, 2> diagonal{};
                Complex cross{};
                std::array<Complex, 2> line_correlation{};
                for (std::size_t i = 0; i < indices.size(); ++i) {
                    auto bin = static_cast<std::int64_t>(indices[i]) - shift;
                    if (bin < 0) bin += static_cast<std::int64_t>(rows);
                    if (bin >= static_cast<std::int64_t>(rows)) bin -= static_cast<std::int64_t>(rows);
                    const auto template_bin = static_cast<std::size_t>(bin);
                    const auto first = spectrum[column + template_bin];
                    correlation[0] += std::conj(first) * block.projected[i];
                    diagonal[0] += std::norm(first);
                    if (!block.line.empty()) line_correlation[0] += std::conj(block.line[i]) * first;
                    if (taps == 2) {
                        const auto echo = spectrum[column + rows + template_bin];
                        correlation[1] += std::conj(echo) * block.projected[i];
                        diagonal[1] += std::norm(echo);
                        cross += std::conj(first) * echo;
                        if (!block.line.empty()) line_correlation[1] += std::conj(block.line[i]) * echo;
                    }
                }
                if (!block.line.empty()) {
                    for (std::size_t path = 0; path < taps; ++path)
                        diagonal[path] = std::max(0., diagonal[path] - std::norm(line_correlation[path]));
                    cross -= std::conj(line_correlation[0]) * line_correlation[1];
                }
                visit(bit, state, bin, rank_bound, correlation, diagonal, cross);
            }
        }
    }
}

// One (bit, frequency state) of a projected track block, in the order the
// visitor produced it. The recursion phase replays that order exactly. A
// device session publishes the independent atoms' cells in this layout.
using ProjectedTrackCell = VaryingIndependentRow;
static_assert(sizeof(ProjectedTrackCell) == 80, "the varying batch quote charges 80-byte track cells");

void projected_track_cells(std::span<const Complex> templates, std::size_t taps, const ObservationBlock& block,
                           double fs, const FrequencyTrackingPlan& plan, WorkerScratch& scratch,
                           std::span<Complex> spectrum, std::span<ProjectedTrackCell> cells, std::stop_token stop) {
    std::size_t next = 0;
    visit_projected_tracks(templates, taps, block, fs, plan, scratch, spectrum,
        [&](std::size_t bit, std::size_t state, std::int64_t half_bin, unsigned,
            const auto& correlation, const auto& diagonal, Complex cross) {
            if (next == cells.size()) throw ReceiverWorkspaceError("Robust projected track cells exceed their batch");
            cells[next++] = {correlation, diagonal, cross, half_bin, static_cast<std::uint32_t>(bit),
                             static_cast<std::uint32_t>(state)};
        }, stop);
    if (next != cells.size()) throw Error("Robust projected track omitted a frequency state");
}

void advance_projected_track(std::span<const ProjectedTrackCell> cells, std::size_t taps,
                             const ObservationBlock& block, double variance, double fs,
                             const FrequencyTrackingPlan& plan, TrackingLogWeights& log_weights,
                             TrackingPosteriors* persistent, double gain_correlation) {
    const auto rank = highest_observation_rank(block);
    TrackingLogWeights evidence{};
    for (const auto& cell : cells) {
        const auto bit = cell.bit, state = cell.state;
        for (std::size_t power = 0; power < tracking_powers.size(); ++power) {
            evidence[bit][power][state] = tracking_emission(cell.diagonal[0], cell.diagonal[1], cell.cross,
                cell.correlations[0], cell.correlations[1], taps, tracking_powers[power], variance, rank);
            if (persistent) {
                // Correct the deterministic per-state frequency
                // reference before transporting a complex gain.
                const auto hz = cell.half_bin * fs / (2 * block.projection->rows());
                const auto phase = std::polar(1., -tau * hz * block.first / fs);
                auto& posterior = (*persistent)[bit][power][state];
                const auto value = coherent_gain_update(posterior.posterior, cell.diagonal[0], cell.diagonal[1],
                    cell.cross, cell.correlations[0] * phase, cell.correlations[1] * phase, taps, tracking_powers[power],
                    variance, gain_correlation, rank);
                // The persistent branch uses its own accumulated
                // likelihood. Keep its independent gain peer below.
                posterior.log_evidence += value;
            }
        }
    }
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::size_t power = 0; power < tracking_powers.size(); ++power)
            update_frequency_weights(std::span(evidence[bit][power]).first(plan.states),
                                     std::span(log_weights[bit][power]).first(plan.states), plan.coherent_gain_blocks);
}

void advance_independent_gain(IndependentGainState& state, std::span<const ProjectedTrackCell> cells, bool nonzero_basis,
                              const ObservationBlock& block, double variance, const FrequencyTrackingPlan& plan,
                              std::stop_token stop) {
    state.nonzero_basis |= nonzero_basis;
    const auto rank = highest_observation_rank(block);
    auto evidence = std::span(state.emissions);
    for (const auto& cell : cells) {
        for (std::size_t power = 0; power < state.powers.size(); ++power) {
            const auto value = tracking_emission(cell.diagonal[0], cell.diagonal[1], cell.cross, cell.correlations[0],
                cell.correlations[1], 2, independent_gain_powers[power], variance, rank);
            evidence[(2 * power + cell.bit) * plan.states + cell.state] = value;
            auto& fixed = state.powers[power].fixed_centroid[cell.bit][cell.state];
            fixed += value;
            if (!std::isfinite(fixed)) throw Error("Robust stationary centroid evidence is nonfinite");
        }
    }
    for (std::size_t power = 0; power < state.powers.size(); ++power)
        for (std::size_t bit = 0; bit < 2; ++bit)
            update_frequency_weights(evidence.subspan((2 * power + bit) * plan.states, plan.states),
                state.powers[power].moving_centroid[bit], false, state.next, stop);
}

struct IndependentGainEvidence {
    double fixed = -infinity, moving = -infinity;
    std::array<double, independent_gain_powers.size()> powers{};
};

IndependentGainEvidence independent_gain_evidence(std::span<const IndependentGainState> atoms,
                                                  std::size_t bit, std::size_t frequency_states) {
    IndependentGainEvidence result;
    const auto frequencies = std::max<std::size_t>(1, frequency_states);
    for (std::size_t power = 0; power < result.powers.size(); ++power) {
        double fixed = -infinity, moving = -infinity;
        for (const auto& atom : atoms) {
            const auto& state = atom.powers[power];
            fixed = std::accumulate(state.fixed_centroid[bit].begin(),
                state.fixed_centroid[bit].begin() + frequencies, fixed, log_add);
            if (frequency_states)
                moving = std::accumulate(state.moving_centroid[bit].begin(),
                    state.moving_centroid[bit].begin() + frequencies, moving, log_add);
        }
        result.powers[power] = frequency_states ? log_add(fixed, moving) - log_two : fixed;
        // A level is common to the complete aperture. Marginalize only
        // after accumulating its fixed or moving frequency likelihood.
        const auto log_prior = std::log(independent_gain_power_priors[power]);
        result.fixed = log_add(result.fixed, fixed + log_prior);
        result.moving = log_add(result.moving, moving + log_prior);
    }
    return result;
}

TrackedFrequencySpanBlock track_span_maximum(
        double direct, double echo, Complex cross, std::span<const Complex> spectrum,
        std::span<const Complex> templates, std::span<const Complex> observation,
        const FrequencyTrackingPlan& plan, std::size_t taps, double fs, double energy,
        std::stop_token stop) {
    TrackedFrequencySpanBlock result;
    const bool echo_pivot = echo > direct;
    const auto pivot = echo_pivot ? echo : direct;
    const auto other = echo_pivot ? direct : echo;
    if (!(pivot > 0) || energy == 0) return result;

    // Project onto the larger column, then its orthogonal complement.
    // The same Gram applies to each common frequency in this raw block.
    const auto coefficient = (echo_pivot ? std::conj(cross) : cross) / pivot;
    const auto residual = other - std::norm(coefficient) * pivot;
    result.rank_ambiguous = other > 0 && residual <= 1e-10 * pivot;
    const auto check_bound = [&](double projected) {
        if (!std::isfinite(projected) || projected > energy * (1 + 1e-7))
            throw Error("Robust tracked span projection exceeds its observation energy");
        return std::min(projected, energy);
    };
    std::size_t best_bin = 0;
    for (std::size_t i = 0; i < plan.states; ++i) {
        const auto signed_bin = (static_cast<std::int64_t>(i) - plan.radius) *
            static_cast<std::int64_t>(plan.step_bins);
        const auto bin = static_cast<std::size_t>(signed_bin < 0 ?
            signed_bin + static_cast<std::int64_t>(plan.fft_rows) : signed_bin);
        const auto r0 = spectrum[bin];
        const auto r1 = taps == 2 ? spectrum[plan.fft_rows + bin] : Complex{};
        const auto p = echo_pivot ? r1 : r0;
        const auto q = echo_pivot ? r0 : r1;
        auto captured = std::norm(p) / pivot;
        if (other > 0 && !result.rank_ambiguous)
            captured += std::norm(q - std::conj(coefficient) * p) / residual;
        captured = check_bound(captured);
        const auto frequency = signed_bin * fs / plan.fft_rows;
        if (captured > result.captured_energy ||
            (captured == result.captured_energy && std::abs(frequency) < std::abs(result.frequency_hz))) {
            result.captured_energy = captured;
            result.frequency_hz = frequency;
            best_bin = bin;
        }
    }
    // Verify the retained maximum against direct signed correlations and
    // a time-domain residual using the fitted template columns.
    const auto phase_step = std::polar(1., -tau * result.frequency_hz / fs);
    const auto raw0 = spectrum[best_bin];
    const auto raw1 = taps == 2 ? spectrum[plan.fft_rows + best_bin] : Complex{};
    const auto p = echo_pivot ? raw1 : raw0;
    const auto q = echo_pivot ? raw0 : raw1;
    const auto primary_gain = p / pivot;
    const auto residual_gain = other > 0 && !result.rank_ambiguous ?
        (q - std::conj(coefficient) * p) / residual : Complex{};
    Complex phase{1};
    std::array<Complex, 2> sum{};
    double residual_energy = 0;
    for (std::size_t i = 0; i < observation.size(); ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        const auto y = observation[i] * phase;
        const auto x0 = templates[i];
        const auto x1 = taps == 2 ? templates[observation.size() + i] : Complex{};
        sum[0] += std::conj(x0) * y;
        sum[1] += std::conj(x1) * y;
        const auto primary = echo_pivot ? x1 : x0;
        const auto secondary = echo_pivot ? x0 : x1;
        residual_energy += std::norm(y - primary_gain * primary - residual_gain * (secondary - coefficient * primary));
        phase *= phase_step;
    }
    for (std::size_t tap = 0; tap < taps; ++tap) {
        const auto scale = std::sqrt(tap == 0 ? direct : echo) * std::sqrt(energy);
        if (std::abs(sum[tap] - spectrum[tap * plan.fft_rows + best_bin]) > 2e-10 * scale)
            throw Error("Robust tracked span FFT disagrees with the direct signed correlation");
    }
    if (!std::isfinite(residual_energy) ||
        std::abs(residual_energy + result.captured_energy - energy) > 1e-7 * energy)
        throw Error("Robust tracked span fit disagrees with the direct residual energy");
    result.verified = true;
    return result;
}

// The unprojected tracker's block statistics for one bit: the template Gram
// shared by every frequency state, beside its lattice correlations.
struct RawTrackGram {
    double direct = 0, echo = 0;
    Complex cross{};
};

std::size_t raw_track_lattice(std::size_t states) { return checked_multiply(4, states); }

void raw_track_statistics(MappedBasis& basis, const MappedBasisRow& map, std::span<const Tap> taps,
                          std::size_t first, std::span<const Complex> observation,
                          const FrequencyTrackingPlan& plan,
                          const modem::detail::search_fft::DoublePlan& fft, WorkerScratch& scratch,
                          std::span<Complex> spectrum, std::span<RawTrackGram, 2> grams, std::span<Complex> lattice,
                          double sample_rate, std::array<TrackedFrequencySpanBlock, 2>* span_maxima,
                          double observation_energy, std::stop_token stop) {
    const auto rows = observation.size();
    const auto columns = 2 * taps.size();
    const auto templates = render_templates(basis, map, taps, first, rows, scratch, stop);
    for (std::size_t column = 0; column < columns; ++column) {
        auto values = spectrum.subspan(column * plan.fft_rows, plan.fft_rows);
        std::fill(values.begin(), values.end(), Complex{});
        for (std::size_t i = 0; i < rows; ++i)
            values[i] = std::conj(templates[column * rows + i]) * observation[i];
        modem::detail::search_fft::fft(values, false, fft, stop);
    }
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto column = bit * taps.size();
        double direct_energy = 0, echo_energy = 0;
        Complex cross{};
        for (std::size_t i = 0; i < rows; ++i) {
            const auto direct = templates[column * rows + i];
            direct_energy += std::norm(direct);
            if (taps.size() == 2) {
                const auto echo = templates[(column + 1) * rows + i];
                echo_energy += std::norm(echo);
                cross += std::conj(direct) * echo;
            }
        }
        if (span_maxima)
            (*span_maxima)[bit] = track_span_maximum(direct_energy, echo_energy, cross,
                spectrum.subspan(column * plan.fft_rows, taps.size() * plan.fft_rows),
                templates.subspan(column * rows, taps.size() * rows), observation, plan, taps.size(),
                sample_rate, observation_energy, stop);
        grams[bit] = {direct_energy, echo_energy, cross};
        auto values = lattice.subspan(2 * bit * plan.states, 2 * plan.states);
        for (std::size_t i = 0; i < plan.states; ++i) {
            const auto signed_bin = (static_cast<std::int64_t>(i) - plan.radius) *
                static_cast<std::int64_t>(plan.step_bins);
            const auto bin = static_cast<std::size_t>(signed_bin < 0 ?
                signed_bin + static_cast<std::int64_t>(plan.fft_rows) : signed_bin);
            values[i] = spectrum[column * plan.fft_rows + bin];
            values[plan.states + i] = taps.size() == 2 ? spectrum[(column + 1) * plan.fft_rows + bin] : Complex{};
        }
    }
}

void advance_raw_track(std::span<const RawTrackGram, 2> grams, std::span<const Complex> lattice, std::size_t taps,
                       std::size_t first, std::size_t rows, double variance, const FrequencyTrackingPlan& plan,
                       TrackingLogWeights& log_weights, double sample_rate, TrackingPosteriors* persistent,
                       double gain_correlation) {
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto& block = grams[bit];
        const auto values = lattice.subspan(2 * bit * plan.states, 2 * plan.states);
        // A constant finite Gaussian gain model for this block. Frequency
        // states change only its phase, so all states share the same Gram.
        for (std::size_t power = 0; power < tracking_powers.size(); ++power) {
            const double precision = taps / tracking_powers[power];
            const DriftGram gram(block.direct, block.echo, block.cross, precision, variance);
            const double scaling = 1. / (std::sqrt(precision) * std::sqrt(variance));
            FrequencyWeights evidence{};
            for (std::size_t i = 0; i < plan.states; ++i) {
                const auto signed_bin = (static_cast<std::int64_t>(i) - plan.radius) *
                    static_cast<std::int64_t>(plan.step_bins);
                const auto r0 = values[i];
                const auto r1 = values[plan.states + i];
                evidence[i] = gram.score(r0 * scaling, r1 * scaling, 1 / variance);
                if (persistent) {
                    const auto hz = signed_bin * sample_rate / plan.fft_rows;
                    const auto phase = std::polar(1., -tau * hz * first / sample_rate);
                    auto& posterior = (*persistent)[bit][power][i];
                    posterior.log_evidence += coherent_gain_update(posterior.posterior, block.direct, block.echo,
                        block.cross, r0 * phase, r1 * phase, taps, tracking_powers[power], variance, gain_correlation,
                        static_cast<unsigned>(std::min<std::size_t>(2, rows)));
                }
            }
            update_frequency_weights(std::span(evidence).first(plan.states),
                                     std::span(log_weights[bit][power]).first(plan.states), plan.coherent_gain_blocks);
        }
    }
}

// One public block of a batch: its projected observation and the inputs of
// every recursion at this block. Storage is reserved once for the batch.
struct VaryingBlock {
    ObservationBlock observation;
    std::vector<Complex> line_storage;
    std::array<double, 4 * 2 * models> transitions{};
    double gain_correlation = 0, observation_energy = 0;
};

// A stage's device session for the template-side statistics of its affine
// items and tracked independent atoms, its staging layout, the native
// projection tables and the stage constants it reads from staging.
struct VaryingDeviceRoute {
    std::unique_ptr<VaryingBulk> session;
    VaryingBulkShape shape;
    VaryingStagingLayout layout;
    std::array<VaryingProjectionTables, 2> tables;
    std::span<const std::uint32_t> taps;
    std::span<const std::int64_t> shifts;
    std::size_t device_batches = 0, native_batches = 0;
};

// One lane's phase receipts, on its own cache line. A lane's idle time in a
// phase runs from its first leaf there to the end of the phase.
struct alignas(64) VaryingLaneTime {
    double projection = 0, statistics = 0, recursion = 0, phase_busy = 0;
    FineClock::time_point phase_first{};
    std::size_t phase = 0;
};

// The families of one block's batch storage. A quote passes public maxima;
// the scorer checks its actual families against the quoted bound.
struct VaryingBatchShape {
    std::size_t final_rows = 0, affine = 0, tracks = 0, track_states = 0;
    std::size_t independent = 0, independent_states = 0;
    bool line = false, projected_tracks = false;
};

std::size_t varying_track_block_bytes(bool projected, std::size_t states) {
    return projected ? checked_multiply(2 * states, sizeof(ProjectedTrackCell)) :
        checked_add(2 * sizeof(RawTrackGram), checked_multiply(raw_track_lattice(states), sizeof(Complex)));
}

std::size_t varying_batch_block_bytes(const VaryingBatchShape& shape) {
    auto bytes = checked_add(sizeof(VaryingBlock),
        checked_multiply(checked_multiply(shape.line ? 2 : 1, shape.final_rows), sizeof(Complex)));
    bytes = checked_add(bytes, checked_multiply(shape.affine, sizeof(FineBulkStatistics)));
    if (shape.track_states)
        bytes = checked_add(bytes, checked_multiply(shape.tracks,
            varying_track_block_bytes(shape.projected_tracks, shape.track_states)));
    // A nominal or signed-lag atom keeps its projected cells and a basis
    // flag, or the central fallback's sufficient statistics.
    const auto independent = std::max(sizeof(FineBulkStatistics), checked_add(1,
        checked_multiply(2 * std::max<std::size_t>(1, shape.independent_states), sizeof(ProjectedTrackCell))));
    return checked_add(bytes, checked_multiply(shape.independent, independent));
}

struct VaryingBasis {
    std::array<Tap, 2> taps{};
    std::size_t count = 1;
};

// A physical two-path basis and a shorter interior pair. Selective stress
// also gives a useful delay scale, distinct from the physical echo bound.
// Each pair owns its own normalized likelihood. Repeated geometry coalesces.
std::array<VaryingBasis, 3> varying_bases(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                        std::size_t& count) {
    std::array<VaryingBasis, 3> result{};
    const auto spread = std::abs(config.spread_doppler_hz);
    const auto delay = config.spread_delay_seconds;
    const auto append = [&](double second_delay, double doppler) {
        for (const auto& candidate : std::span(result).first(count))
            if (candidate.taps[1].delay_seconds == second_delay && candidate.taps[1].doppler_hz == doppler)
                return;
        auto& model = result[count++];
        model.taps[1] = {second_delay, doppler};
        model.count = second_delay || doppler ? 2 : 1;
    };
    // Stronger narrow echo may sit well inside the preset. The all-sign
    // envelope isn't a guarantee that these two delay anchors fit it.
    append(delay, config.spread_doppler_hz);
    if (delay || spread) append(delay * .5, 0);
    if (config.selective_modulation_rate_hz > 0 &&
        (config.selective_slow_depth || config.selective_fast_depth))
        append(1 / (3 * geometry(slot.format).nominal_carrier_bandwidth_hz), 0);
    return result;
}

bool projected_block_supported(std::size_t rows, const ProjectionBands& bands) {
    if (!bands.omit_overlapping_modes)
        throw Error("Robust supplemental block requires its separable DATA projection");
    const double spacing = bands.sample_rate / static_cast<double>(rows);
    const double half = spacing / 2;
    if (!bands.use_exclusions) return true;
    // Signed DFT centers within the expanded DATA interval. Both bands'
    // boundaries use the same half-bin convention as the projection.
    const auto first = -static_cast<double>(rows / 2);
    const auto last = static_cast<double>((rows - 1) / 2);
    const auto lower = std::max(first, std::ceil((bands.required.lower_hz - half) / spacing));
    const auto upper = std::min(last, std::floor((bands.required.upper_hz + half) / spacing));
    auto bin = lower;
    // At most three excluded intervals can cover successive bins.
    // Advance past their union without walking every raw DFT index.
    for (std::size_t attempt = 0; attempt <= bands.excluded.size() && bin <= upper; ++attempt) {
        auto next = bin;
        for (const auto& excluded : bands.excluded) {
            const auto start = std::ceil((excluded.lower_hz - half) / spacing);
            const auto stop = std::floor((excluded.upper_hz + half) / spacing);
            if (start <= bin && bin <= stop) next = std::max(next, stop + 1);
        }
        if (next == bin) return true;
        bin = next;
    }
    return false;
}
} // namespace

std::size_t nominal_epoch_timing_capacity(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    if (!config.time_varying_channel || config.helper_enabled) return 0;
    return (config.clock_bound_ppm ? 4 : 1) * (slot.arrival_lower_seconds == slot.arrival_upper_seconds ? 1 : 2);
}

void enable_midpoint_tracking(VaryingFinePlan& plan) {
    if (!plan.track_frequency || plan.coherent_gain_blocks || plan.midpoint_tracking)
        throw Error("Robust midpoint family requires its ordinary short tracking plan");
    plan.workspace_bytes = checked_add(plan.workspace_bytes,
        maximum_midpoint_timing_coordinates * (sizeof(TrackingLogWeights) + sizeof(MappedBasisRow) +
            2 * sizeof(FineCoordinate)));
    // Each added coordinate's track statistics in every block of a batch.
    const auto batch_tracks = checked_multiply(maximum_midpoint_timing_coordinates, plan.batch_track_bytes);
    plan.batch_block_bytes = checked_add(plan.batch_block_bytes, batch_tracks);
    plan.workspace_bytes = checked_add(plan.workspace_bytes, checked_multiply(plan.batch_blocks, batch_tracks));
    plan.midpoint_tracking = true;
}

VaryingFinePlan varying_fine_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                                 bool project_line_atom, bool coherent_gain_blocks, std::size_t tracking_final_rows) {
    if (!config.time_varying_channel) return {};
    const auto fs = config.sample_rate_hz;
    const auto rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
    const auto chip_rate = geometry(slot.format).fine_chip_rate_hz;
    const auto nominal_rows = round_rows(coherent_gain_blocks ? 1. : .25, fs);
    // Keep at least two fine cells in a fitted block. Over a single cell
    // an unconstrained frequency-selective fit can erase bit distinctions.
    // A quarter cycle is the target residual change, not a coverage claim.
    const auto residual = std::max({config.time_varying_common_doppler_hz,
        3 * config.random_doppler_width_hz + config.delay_motion_doppler_bound_hz,
        .1 * config.selective_modulation_rate_hz, std::abs(config.spread_doppler_hz) * .25});
    if (!(2 / chip_rate < (coherent_gain_blocks ? 1. : .25)))
        return {0, 0, "fine_cells_too_slow_for_short_block"};
    // Limit the declared diffuse width to 3/8 cycle per medium block when
    // supported by the minimum cadence. A narrower width retains .5 s.
    const auto coherent_interval = std::clamp(.375 / std::max(.75, config.random_doppler_width_hz), .25, .5);
    const auto ideal = std::max(2 / chip_rate,
        coherent_gain_blocks ? coherent_interval : .25 / std::max(2., residual));
    // Above this time-bandwidth product the fixed center grid would
    // undersample the residual within each medium-coherence block.
    if (coherent_gain_blocks && (config.frequency_bound_hz + config.time_varying_atmospheric_hz) * ideal > 8)
        return {0, 0, "coherent_gain_center_grid_exceeds_work_limit"};
    auto block = std::min(rows, round_rows(ideal, fs));
    if (block >= nominal_rows) return {0, 0, "no_short_block_geometry"};
    std::optional<ProjectionBands> bands;
    try {
        bands.emplace(config, slot, ProjectionBands::Policy::separable_varying);
    } catch (const Error&) {
        return {0, 0, "varying_data_and_helper_bands_not_separable"};
    }
    const auto remainder_for = [&](std::size_t span) { return rows % span; };
    for (;;) {
        const auto remainder = remainder_for(block);
        const auto final = remainder ? block + remainder : block;
        if (projected_block_supported(block, *bands) && projected_block_supported(final, *bands)) break;
        // Doubling retains bounded initialization and lets wider bands use
        // enough orthogonal modes to exclude Helper, where a gap exists.
        if (block >= std::min(rows, nominal_rows) / 2)
            return {0, 0, "short_block_cannot_resolve_helper"};
        block *= 2;
    }
    const auto final = block + remainder_for(block);
    const auto fft = std::max(DftPlan::scratch_samples(block), DftPlan::scratch_samples(final));
    auto bytes = checked_add(DftPlan::workspace_bytes(block), DftPlan::workspace_bytes(final));
    const auto add = [&](std::size_t count, std::size_t width) {
        bytes = checked_add(bytes, checked_multiply(count, width));
    };
    add(block + final, sizeof(std::size_t)); // projection indices
    std::size_t geometry_count = 0;
    (void)varying_bases(config, slot, geometry_count);
    const auto states = maximum_varying_coordinates * geometry_count;
    const auto mapped = std::min(maximum_basis_rows, final);
    auto tracking_config = config;
    tracking_config.helper_enabled |= project_line_atom;
    if (tracking_final_rows && (tracking_final_rows < block || tracking_final_rows >= 2 * block))
        return {0, 0, "protected_varying_model_grid_unavailable"};
    // A crop can merge more rows into the last block. Its templates can be
    // rendered in chunks and all fit in the original FFT (at least 2 * block).
    const auto tracking_rows = tracking_final_rows ? tracking_final_rows : final;
    auto track = tracking_rows <= maximum_basis_rows ? tracking_plan(tracking_config, block, tracking_rows,
        coherent_gain_blocks) : FrequencyTrackingPlan{};
    if (track.states && tracking_config.helper_enabled) {
        const auto non_dc = [&](std::size_t rows) {
            const auto spacing = fs / rows;
            const auto use = [&](double hz) {
                if (hz < bands->required.lower_hz - spacing / 2 || hz > bands->required.upper_hz + spacing / 2)
                    return false;
                return !bands->use_exclusions || std::ranges::none_of(bands->excluded, [&](const auto& excluded) {
                    return hz >= excluded.lower_hz - spacing / 2 && hz <= excluded.upper_hz + spacing / 2;
                });
            };
            return use(spacing) || use(-spacing) || !use(0);
        };
        if (!non_dc(block) || !non_dc(final)) track = {};
    }
    if (coherent_gain_blocks && !track.states) return {0, 0, "coherent_gain_blocks_unavailable"};
    const std::size_t independent = !coherent_gain_blocks && track.states && config.spread_delay_seconds > 0 ?
        (slot.arrival_lower_seconds == slot.arrival_upper_seconds ? 1 : 2) : 0;
    const auto nominated = independent && !project_line_atom && config.clock_bound_ppm ?
        receiver_signed_lag_gain_max_candidates : 0;
    const auto gain_atoms = independent + nominated;
    const auto active = std::min<std::size_t>(workers, states + gain_atoms);
    if (!coherent_gain_blocks) add(states, sizeof(State) + sizeof(MappedBasisRow));
    add(gain_atoms, sizeof(IndependentGainState) + sizeof(MappedBasisRow));
    auto independent_config = tracking_config;
    independent_config.helper_enabled = true;
    const auto independent_track = gain_atoms ? independent_tracking_plan(independent_config, block, final) : FrequencyTrackingPlan{};
    // Sixteen accumulated weights, eight emissions and one transition vector
    // per frequency and independent timing atom. Allocate before scoring.
    add(checked_multiply(gain_atoms, std::max<std::size_t>(1, independent_track.states)),
        (4 * independent_gain_powers.size() + 2 * independent_gain_powers.size() + 1) * sizeof(double));
    add(active, sizeof(WorkerScratch) + sizeof(VaryingLaneTime));
    add(checked_multiply(active, 4 * final + fft + (final > mapped ? 4 * mapped : 0)), sizeof(Complex));
    add(checked_multiply(active, mapped), sizeof(double));
    add(1, MappedBasis::workspace_bytes(config, slot, mapped, static_cast<unsigned>(active)));
    if (track.states) {
        add(track.fft_rows, sizeof(Complex));
        add(maximum_varying_coordinates + maximum_segmented_timing_coordinates,
            sizeof(TrackingLogWeights) + sizeof(MappedBasisRow));
        if (!coherent_gain_blocks && !project_line_atom)
            add(nominal_epoch_timing_capacity(config, slot),
                sizeof(TrackingLogWeights) + sizeof(MappedBasisRow) + 2 * sizeof(FineCoordinate));
        if (coherent_gain_blocks)
            add(maximum_varying_coordinates + maximum_segmented_timing_coordinates, sizeof(TrackingPosteriors));
        add(checked_multiply(active, 4 * track.fft_rows), sizeof(Complex));
    }
    // A batch's observations, block statistics and track cells stay live
    // until its recursions finish. Quote every family at its public maximum.
    const bool projected_tracks = tracking_config.helper_enabled;
    const auto tracks = track.states ? maximum_varying_coordinates + maximum_segmented_timing_coordinates +
        (!coherent_gain_blocks && !project_line_atom ? nominal_epoch_timing_capacity(config, slot) : 0) : 0;
    const auto block_bytes = varying_batch_block_bytes({final, coherent_gain_blocks ? 0 : states, tracks, track.states,
        gain_atoms, independent_track.states, project_line_atom, projected_tracks});
    const auto batch_blocks = std::clamp<std::size_t>(maximum_varying_batch_bytes / block_bytes, 1,
        std::min<std::size_t>(rows / block, maximum_varying_batch_blocks));
    add(batch_blocks, block_bytes);
    add(1, 64 * 1024);
    VaryingFinePlan result{block, bytes, coherent_gain_blocks ? "coherent_gain_blocks" :
            (residual * final / fs > .25 ? "short_block_partial_bandwidth" : "short_block"),
            track.states != 0, coherent_gain_blocks, false, independent};
    result.batch_blocks = batch_blocks;
    result.batch_block_bytes = block_bytes;
    result.batch_track_bytes = track.states ? varying_track_block_bytes(projected_tracks, track.states) : 0;
    result.tracking_final_rows = tracking_final_rows;
    return result;
}

std::size_t tracked_frequency_span_workspace(const ReceiverSlotPlan& slot, const VaryingFinePlan& plan,
                                              bool clock_shadow) {
    const auto rows = slot.stop_sample - slot.first_sample;
    if (!plan.block_rows || !plan.track_frequency || plan.coherent_gain_blocks ||
        rows > span_maximum_rows || rows < plan.block_rows || rows / plan.block_rows > span_maximum_blocks)
        throw ReceiverWorkspaceError("Robust tracked span exceeds its short-track geometry bounds");
    const auto blocks = static_cast<std::size_t>(rows / plan.block_rows);
    const auto coordinates = clock_shadow ?
        maximum_varying_coordinates + maximum_segmented_timing_coordinates : span_maximum_coordinates;
    return blocks * sizeof(double) + coordinates *
        (sizeof(TrackedFrequencySpanCoordinate) + blocks * sizeof(std::array<TrackedFrequencySpanBlock, 2>));
}

void score_varying_fine(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                        const VaryingFinePlan& plan, std::span<const Complex> iq,
                        std::span<const FineCoordinate> selected, std::span<const FineCoordinate> supplemental_timing,
                        double projected_rate, double clock_gap,
                        unsigned workers, ReceiverSlotScore& score, std::stop_token stop,
                        ReceiverExecutor& executor, const LineAtom* line, TrackedFrequencySpan* span,
                        TrackedFrequencyClockShadow* clock_shadow,
                        const MidpointTrackInput* midpoint) {
    check_cancelled(stop);
    const auto extra_timing = plan.track_frequency && !plan.coherent_gain_blocks && !line ?
        nominal_epoch_timing_capacity(config, slot) : 0;
    if (!plan.block_rows || selected.empty() || selected.size() > maximum_varying_coordinates ||
        supplemental_timing.size() > maximum_segmented_timing_coordinates + extra_timing ||
        iq.size() != slot.stop_sample - slot.first_sample)
        throw Error("varying fine scorer requires its bounded public slot and coordinates");
    const auto nominal_count = plan.nominal_independent_gain_candidates;
    if (nominal_count && (plan.coherent_gain_blocks || !plan.track_frequency || !(config.spread_delay_seconds > 0) ||
        nominal_count != (slot.arrival_lower_seconds == slot.arrival_upper_seconds ? 1U : 2U)))
        throw Error("Robust nominal independent gain requires its bounded short varying plan");
    if (plan.midpoint_tracking != (midpoint != nullptr) ||
        (midpoint && (line || span || clock_shadow ||
            !midpoint->public_lag_count || !std::isfinite(midpoint->original_timing_log_count) ||
            midpoint->nominations.size() > maximum_midpoint_timing_coordinates ||
            midpoint->gain_nominations.size() > receiver_signed_lag_gain_max_candidates ||
            (!midpoint->gain_nominations.empty() && !midpoint->gain_family_admitted))))
        throw Error("Robust midpoint family requires its bounded ordinary short tracking lease");
    if (clock_shadow) {
        if (!plan.track_frequency || plan.coherent_gain_blocks || config.helper_enabled || line ||
            !supplemental_timing.empty() || clock_shadow->nominations.size() > maximum_segmented_timing_coordinates)
            throw Error("Robust clock shadow requires room in the unprojected short track timing bank");
        const auto nominations = clock_shadow->nominations;
        *clock_shadow = {};
        clock_shadow->nominations = nominations;
    }
    if (span) {
        if (config.helper_enabled || line)
            throw Error("Robust tracked span requires unprojected Helper-off statistics");
        (void)tracked_frequency_span_workspace(slot, plan, clock_shadow != nullptr);
        span->observation_energy.assign(iq.size() / plan.block_rows, 0.);
        span->coordinates.clear();
    }
    const auto began = FineClock::now();
    const auto block_rows = plan.block_rows;
    const auto final_rows = block_rows + iq.size() % block_rows;
    const auto fft_rows = std::max(DftPlan::scratch_samples(block_rows), DftPlan::scratch_samples(final_rows));
    const ProjectionBands bands(config, slot, ProjectionBands::Policy::separable_varying);
    const Projection block_projection(block_rows, bands, stop);
    const Projection final_projection(final_rows, bands, stop);
    const TimingGrid grid(projected_rate, clock_gap);
    std::size_t model_count = 0;
    const auto bases = varying_bases(config, slot, model_count);
    const bool lag_family = nominal_count && midpoint && midpoint->gain_family_admitted;
    const auto lag_count = lag_family ? midpoint->gain_nominations.size() : 0;
    const auto independent_count = nominal_count + lag_count;
    const auto active = std::min<std::size_t>(workers, selected.size() * model_count + independent_count);
    MappedBasis basis(config, slot, key, std::min(final_rows, maximum_basis_rows), static_cast<unsigned>(active));
    std::vector<WorkerScratch> scratch;
    scratch.reserve(active);
    for (std::size_t lane = 0; lane < active; ++lane) scratch.emplace_back(final_rows, fft_rows);
    std::vector<MappedBasisRow> mapped;
    mapped.reserve(selected.size());
    for (const auto& coordinate : selected)
        mapped.push_back(mapped_row(config, slot, grid, coordinate, {}, 0, true));
    std::vector<MappedBasisRow> independent_maps;
    std::vector<IndependentGainState> independent_states;
    independent_maps.reserve(independent_count);
    if (independent_count) {
        const AdmissionGrid arrival_grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds, 0., 0., 1.);
        const auto lower = arrival_grid.lower;
        const auto upper = nominal_count == 2 ? lower + arrival_grid.arrival_intervals *
            ((arrival_grid.upper - lower) / arrival_grid.arrival_intervals) : lower;
        const std::array arrivals{lower, upper};
        const auto append = [&](const FineCoordinate& coordinate) {
            independent_maps.push_back(mapped_row(config, slot, grid, coordinate, {}, 0, true));
        };
        for (std::size_t i = 0; i < nominal_count; ++i) append({0, arrivals[i], 0, 0});
        if (lag_family) {
            const auto nominations = midpoint->gain_nominations;
            if (!(midpoint->middle_seconds > 0 && std::isfinite(midpoint->middle_seconds)) ||
                midpoint->public_lag_count > UINT32_MAX || midpoint->first_lag > 0)
                throw Error("signed lag gain family requires its fixed public timing dictionary");
            for (std::size_t i = 0; i < nominations.size(); ++i) {
                const auto lag = nominations[i];
                if (lag < midpoint->first_lag ||
                    lag >= midpoint->first_lag + static_cast<std::int64_t>(midpoint->public_lag_count) ||
                    std::find(nominations.begin(), nominations.begin() + static_cast<std::ptrdiff_t>(i), lag) !=
                        nominations.begin() + static_cast<std::ptrdiff_t>(i))
                    throw Error("signed lag gain nomination is outside its public timing lease");
                const auto coordinate = midpoint_timing_coordinate(slot, midpoint->middle_seconds, config.clock_bound_ppm,
                    static_cast<std::size_t>(projected_rate), lag, 0);
                append(coordinate);
            }
        }
    }
    auto tracking_config = config;
    tracking_config.helper_enabled |= line != nullptr;
    const auto track = plan.track_frequency ?
        tracking_plan(tracking_config, block_rows, plan.tracking_final_rows ? plan.tracking_final_rows : final_rows,
            plan.coherent_gain_blocks) : FrequencyTrackingPlan{};
    if (track.states && final_rows > track.fft_rows)
        throw Error("Robust final tracking block exceeds its public FFT");
    auto projected_tracking_config = tracking_config;
    projected_tracking_config.helper_enabled = true;
    const auto nontrivial_projection = [](const Projection& projection) {
        return projection.modes() > 1 || projection.indices().front() != 0;
    };
    const auto independent_track = independent_count && track.states && nontrivial_projection(block_projection) &&
        nontrivial_projection(final_projection) ? independent_tracking_plan(projected_tracking_config, block_rows, final_rows) :
                                                FrequencyTrackingPlan{};
    independent_states.reserve(independent_count);
    for (std::size_t i = 0; i < independent_count; ++i)
        independent_states.emplace_back(std::max<std::size_t>(1, independent_track.states));
    if (independent_track.states) {
        const auto log_prior = -std::log(static_cast<double>(independent_track.states));
        for (auto& atom : independent_states)
            for (auto& state : atom.powers)
                for (std::size_t bit = 0; bit < state.fixed_centroid.size(); ++bit) {
                    std::ranges::fill(state.fixed_centroid[bit], log_prior);
                    std::ranges::fill(state.moving_centroid[bit], log_prior);
                }
    }
    if ((span || clock_shadow || midpoint) && !track.states)
        throw Error("Robust short track evaluation has no resolved frequency states");
    std::optional<modem::detail::search_fft::DoublePlan> tracking_fft;
    std::vector<TrackingLogWeights> tracking_weights;
    std::vector<TrackingPosteriors> tracking_posteriors;
    std::vector<FineCoordinate> tracking_coordinates;
    std::vector<std::vector<Complex>> tracking_scratch;
    std::vector<MappedBasisRow> tracking_maps;
    std::size_t ordinary_tracking_coordinates = 0;
    std::array<std::size_t, maximum_midpoint_timing_coordinates> midpoint_indices{};
    std::size_t midpoint_count = 0;
    if (track.states) {
        tracking_fft.emplace(track.fft_rows, stop);
        tracking_coordinates.reserve(selected.size() + supplemental_timing.size() +
            (clock_shadow ? clock_shadow->nominations.size() : 0) +
            (midpoint ? midpoint->nominations.size() : 0));
        const auto append = [&](FineCoordinate coordinate) {
            coordinate.frequency_hz = 0;
            const auto found = std::ranges::find(tracking_coordinates, coordinate);
            const auto index = static_cast<std::size_t>(found - tracking_coordinates.begin());
            if (found == tracking_coordinates.end()) tracking_coordinates.push_back(coordinate);
            return index;
        };
        for (const auto& coordinate : selected) append(coordinate);
        for (const auto& coordinate : supplemental_timing) append(coordinate);
        ordinary_tracking_coordinates = tracking_coordinates.size();
        if (clock_shadow) {
            for (const auto& coordinate : clock_shadow->nominations) append(coordinate);
            clock_shadow->ordinary_coordinates = ordinary_tracking_coordinates;
            clock_shadow->added_coordinates = tracking_coordinates.size() - ordinary_tracking_coordinates;
        }
        if (midpoint) {
            for (const auto& coordinate : midpoint->nominations) {
                if (std::ranges::find(receiver_epoch_offsets, coordinate.epoch_offset_seconds) == receiver_epoch_offsets.end() ||
                    coordinate.frequency_hz != 0 || !std::isfinite(coordinate.clock_ppm) ||
                    std::abs(coordinate.clock_ppm) > config.clock_bound_ppm ||
                    !(coordinate.arrival_seconds >= slot.arrival_lower_seconds &&
                        coordinate.arrival_seconds <= slot.arrival_upper_seconds))
                    throw Error("Robust midpoint timing nomination exceeds the declared public bounds");
                const auto index = append(coordinate);
                const auto retained = std::span(midpoint_indices).first(midpoint_count);
                if (std::ranges::find(retained, index) == retained.end())
                    midpoint_indices[midpoint_count++] = index;
            }
            score.midpoint_timing_candidates = midpoint_count;
        }
        if (span) {
            if (!clock_shadow && tracking_coordinates.size() > span_maximum_coordinates)
                throw ReceiverWorkspaceError("Robust tracked span exceeds its timing coordinate bound");
            span->coordinates.reserve(tracking_coordinates.size());
            for (const auto& coordinate : tracking_coordinates)
                span->coordinates.push_back({coordinate,
                    std::vector<std::array<TrackedFrequencySpanBlock, 2>>(span->observation_energy.size())});
        }
        tracking_weights.resize(tracking_coordinates.size());
        // Preserve the ordinary per-atom normalization. The shadow's raw
        // coordinate average is accounted for only when aggregating it.
        const auto prior = -std::log(static_cast<double>(
            track.states * ordinary_tracking_coordinates * tracking_powers.size()));
        for (auto& atom : tracking_weights)
            for (auto& bit : atom)
                for (auto& power : bit) power.fill(prior);
        if (plan.coherent_gain_blocks) {
            tracking_posteriors.resize(tracking_coordinates.size());
            const std::size_t taps = config.spread_delay_seconds || config.spread_doppler_hz ? 2 : 1;
            for (auto& atom : tracking_posteriors)
                for (auto& bit : atom)
                    for (std::size_t power = 0; power < tracking_powers.size(); ++power)
                        for (auto& state : std::span(bit[power]).first(track.states)) {
                            state.log_evidence = prior;
                            for (std::size_t tap = 0; tap < taps; ++tap)
                                state.posterior.uncertainty[tap * taps + tap] =
                                    initial_gain_diagonal(taps, tracking_powers[power] / taps);
                        }
        }
        tracking_scratch.resize(active);
        for (auto& lane : tracking_scratch) lane.resize(4 * track.fft_rows);
        tracking_maps.reserve(tracking_coordinates.size());
        for (const auto& coordinate : tracking_coordinates)
            tracking_maps.push_back(mapped_row(config, slot, grid, coordinate, {}, 0, true));
    }
    std::vector<State> states(plan.coherent_gain_blocks ? 0 : selected.size() * model_count);
    const auto prior = states.empty() ? 0. : -std::log(static_cast<double>(states.size() * models));
    for (std::size_t candidate = 0; candidate < states.size(); ++candidate) {
        auto& state = states[candidate];
        const auto taps = bases[candidate % model_count].count;
        state.weights.fill(prior);
        for (auto& channel : state.posterior)
            for (std::size_t tap = 0; tap < taps; ++tap)
                channel.uncertainty[tap * taps + tap] = initial_gain_diagonal(taps, varying_power / taps);
    }
    // Consecutive blocks are scored in batches of three executor phases:
    // P projects each block's observation; S computes every family's block
    // statistics, chunk by chunk and longest family first; R advances each
    // recursive chain through the batch in block order. Every statistic and
    // recursion is the same per-element operation, in the same order, as one
    // wave per block, so a batch of one block is the block-serial schedule.
    const auto total_blocks = static_cast<std::size_t>(iq.size() / block_rows);
    if (!plan.batch_blocks || !total_blocks) throw Error("Robust varying fine requires its admitted block batch");
    auto batch_capacity = std::min(plan.batch_blocks, total_blocks);
    const bool projected_tracks = tracking_config.helper_enabled;
    const auto track_count = tracking_weights.size();
    const bool independent_tracked = independent_track.states != 0;
    if (varying_batch_block_bytes({final_rows, states.size(), track_count, track.states, independent_count,
            independent_track.states, line != nullptr, projected_tracks}) > plan.batch_block_bytes)
        throw ReceiverWorkspaceError("Robust varying batch exceeds its quoted block storage");
    // A device session may compute the affine items' and tracked atoms'
    // statistics; its admitted batch bounds the host batch, which never
    // changes a result.
    const auto center_atoms = independent_tracked ? 0 : independent_count;
    const auto affine_items = states.size() + center_atoms;
    const auto tracked_atoms = independent_tracked ? independent_count : 0;
    constexpr std::size_t independent_taps = 2;
    const auto affine_taps = [&](std::size_t item) -> std::size_t {
        return item < states.size() ? bases[item % model_count].count : independent_taps;
    };
    VaryingDeviceRoute device;
    if (auto* backend = config.bulk_backend.get()) {
        const bool fp64_forced = config.native_coarse_precision == NativeCoarsePrecision::fp64;
        auto& shape = device.shape;
        const auto* block_plan = block_projection.plan();
        const auto* final_plan = final_projection.plan();
        shape.block_rows = block_rows;
        shape.final_rows = final_rows;
        shape.bluestein_rows = {block_plan ? block_plan->transform_rows() : 0, final_plan ? final_plan->transform_rows() : 0};
        shape.modes = {block_projection.modes(), final_projection.modes()};
        shape.independent_states = tracked_atoms ? independent_track.states : 0;
        shape.affine_states = affine_items;
        shape.independent_atoms = tracked_atoms;
        shape.center_atoms = center_atoms;
        shape.taps = tracked_atoms || center_atoms ? independent_taps : 1;
        for (std::size_t item = 0; item < affine_items; ++item) {
            shape.taps = std::max(shape.taps, affine_taps(item));
            shape.affine_columns = checked_add(shape.affine_columns, 2 * affine_taps(item));
        }
        shape.batch_blocks = batch_capacity;
        shape.arithmetic = VaryingArithmetic::fp64;
        // The receiver's own declines: evaluator diagnostics, the line
        // branch, coherent posteriors and the Helper projected tracker.
        // They follow the shape-independent policy (a native or FP64
        // request, forced host scoring, missing FP64 shaders, automatic
        // qualification) and precede the shaped one, so a stage the route
        // would serve reports the receiver's reason even where its shape has
        // no device items, as a coherent-gain shape has none.
        const auto own = span || clock_shadow ? std::optional{StageReason::diagnostic} :
            line ? std::optional{StageReason::line} :
            plan.coherent_gain_blocks ? std::optional{StageReason::coherent_gain} :
            projected_tracks ? std::optional{StageReason::projected_tracker} : std::nullopt;
        if (own && backend->stage_decline(StageShape{StageFamily::varying}, fp64_forced).empty()) {
            backend->record_stage_native(StageFamily::varying, *own, StageNative::decline);
        } else if (!backend->stage_decline(shape, fp64_forced).empty()) {
            // The factory records the policy decline and admits nothing.
            if (backend->make_varying(shape, fp64_forced, stop))
                throw Error("Robust varying device factory admitted a declined stage");
        } else if (own) {
            // A backend whose shape policy accepts what its family policy declines.
            backend->record_stage_native(StageFamily::varying, *own, StageNative::decline);
        } else {
            // The session's host memory (plan and shader compilation, and a
            // unified-memory lease) shares the owner's preserved stage:
            // halve the batch until it fits beside this quote.
            while (shape.batch_blocks &&
                   backend->stage_host_bytes(shape, fp64_forced) > plan.device_envelope_bytes)
                shape.batch_blocks /= 2;
            if (!shape.batch_blocks)
                backend->record_stage_native(StageFamily::varying, backend->receipt().unified_memory ?
                    StageReason::unified_memory_envelope : StageReason::workspace, StageNative::fallback);
            else device.session = backend->make_varying(shape, fp64_forced, stop);
        }
        if (device.session) {
            const auto admitted = device.session->batch_blocks();
            if (!admitted || admitted > shape.batch_blocks)
                throw Error("Robust varying device session exceeds its requested batch");
            batch_capacity = std::min(batch_capacity, admitted);
            device.layout = varying_staging_layout(shape, admitted);
            for (std::size_t length = 0; length < 2; ++length) {
                const auto& projection = length ? final_projection : block_projection;
                const auto* plan_tables = projection.plan();
                device.tables[length] = {plan_tables->chirp(), plan_tables->kernel_spectrum(), projection.indices(),
                                         projection.normalization()};
            }
            // The stage's constants live in staging slot 0.
            const auto constants = device.session->template_staging(0);
            auto* taps = reinterpret_cast<std::uint32_t*>(constants.data() + device.layout.taps_offset);
            for (std::size_t item = 0; item < affine_items; ++item) taps[item] = static_cast<std::uint32_t>(affine_taps(item));
            device.taps = {taps, affine_items};
            auto* bins = reinterpret_cast<std::int64_t*>(constants.data() + device.layout.shifts_offset);
            for (std::size_t length = 0; length < 2 && tracked_atoms; ++length)
                for (std::size_t state = 0; state < independent_track.states; ++state)
                    bins[length * independent_track.states + state] =
                        projected_half_bin(length ? final_rows : block_rows, config.sample_rate_hz,
                                           independent_track.step_hz, independent_track.radius, state);
            device.shifts = {bins, 2 * shape.independent_states};
        }
    }
    std::vector<VaryingBlock> batch(batch_capacity);
    for (auto& entry : batch) {
        entry.observation.observed = true;
        entry.observation.projected.reserve(final_rows);
        if (line) entry.line_storage.resize(final_rows);
    }
    // Item-major [item][block] storage: a statistics leaf writes, and a
    // recursion leaf reads, one item's consecutive blocks.
    const auto track_cells = 2 * track.states, independent_cells = 2 * independent_track.states;
    std::vector<FineBulkStatistics> affine_statistics(checked_multiply(states.size(), batch_capacity));
    std::vector<FineBulkStatistics> center_statistics(independent_tracked ? 0 :
        checked_multiply(independent_count, batch_capacity));
    std::vector<ProjectedTrackCell> independent_track_cells(independent_tracked ?
        checked_multiply(checked_multiply(independent_count, batch_capacity), independent_cells) : 0);
    std::vector<unsigned char> independent_nonzero(independent_tracked ? independent_count * batch_capacity : 0);
    std::vector<ProjectedTrackCell> projected_track_storage(projected_tracks ?
        checked_multiply(checked_multiply(track_count, batch_capacity), track_cells) : 0);
    std::vector<RawTrackGram> raw_track_grams(projected_tracks ? 0 : checked_multiply(2 * track_count, batch_capacity));
    std::vector<Complex> raw_track_values(projected_tracks ? 0 :
        checked_multiply(checked_multiply(track_count, batch_capacity), raw_track_lattice(track.states)));
    // Statistics items in the order a chunk's leaves are claimed: the 16-FFT
    // gain atoms, the 8-FFT fixed-affine states, then the tracker's 4 FFTs.
    const auto items = independent_count + states.size() + track_count;
    const auto chunk_blocks = std::max<std::size_t>(1, std::min(batch_capacity, varying_chunk_rows / block_rows));
    const auto block_extent = [&](std::size_t index) {
        return std::pair{index * block_rows, index + 1 == total_blocks ? final_rows : block_rows};
    };
    // One mapping per item with its taps; the delay and offset match the leaf.
    const auto track_delay = config.spread_delay_seconds * (plan.coherent_gain_blocks ? 1. : .5);
    const auto track_offset = plan.coherent_gain_blocks ? config.spread_doppler_hz : 0.;
    const std::array<Tap, 2> track_paths{{{0, 0}, {track_delay, track_offset}}};
    const auto track_taps = std::span(track_paths).first(track_delay || track_offset ? 2 : 1);
    const std::array<Tap, 2> independent_paths{{{0, 0}, {config.spread_delay_seconds, 0}}};
    const auto item_basis = [&](std::size_t item) -> std::pair<const MappedBasisRow&, std::span<const Tap>> {
        if (item < independent_count) return {independent_maps[item], independent_paths};
        item -= independent_count;
        if (item < states.size()) {
            const auto& paths = bases[item % model_count];
            return {mapped[item / model_count], std::span(paths.taps).first(paths.count)};
        }
        return {tracking_maps[item - states.size()], track_taps};
    };
    // Fill ahead, as FineSession does, the tiles a later chunk's leaves pin:
    // the per-epoch preparation is serial, and would stall those leaves. A
    // multi-block chunk covers at most 2,048 rows, no longer than one dense
    // tile even at the 4x source rate, so its first and last blocks pin every
    // tile it touches. A tile missed beside the merged final block is filled
    // on demand. Rendered values never depend on when a tile was filled.
    const auto prepare_blocks = [&](std::size_t begin, std::size_t end, std::stop_token leaf_stop) {
        if (begin >= end) return;
        for (std::size_t item = 0; item < items; ++item) {
            const auto [map, taps] = item_basis(item);
            for (const auto index : {begin, end - 1}) {
                const auto [block_first, rows] = block_extent(index);
                for (std::size_t offset = 0; offset < rows; offset += maximum_basis_rows)
                    basis.prepare_pair(map, taps, block_first + offset, std::min(maximum_basis_rows, rows - offset),
                                       leaf_stop);
                if (end - 1 == begin) break;
            }
        }
    };
    // Lanes another owner held, or that never claimed a leaf, are not idle
    // time of this stage: only a lane's span from its first leaf counts.
    std::vector<VaryingLaneTime> lane_time(active);
    std::size_t phase_index = 0;
    double idle_lane_seconds = 0;
    const auto phase = [&](std::size_t count, const ReceiverExecutor::Leaf& leaf) {
        ++phase_index;
        executor.parallel(slot.index, count, static_cast<unsigned>(active), stop, leaf);
        const auto ended = FineClock::now();
        for (const auto& lane : lane_time)
            if (lane.phase == phase_index)
                idle_lane_seconds += std::max(0., std::chrono::duration<double>(ended - lane.phase_first).count() -
                                                  lane.phase_busy);
    };
    const auto lane_began = [&](unsigned lane, FineClock::time_point began) {
        auto& time = lane_time[lane];
        if (time.phase == phase_index) return;
        time.phase = phase_index;
        time.phase_first = began;
        time.phase_busy = 0;
    };
    const auto lane_spent = [&](double& phase_total, unsigned lane, FineClock::time_point began) {
        const auto spent = seconds_since(began);
        phase_total += spent;
        lane_time[lane].phase_busy += spent;
    };
    // One statistics leaf: a chunk of one item's blocks of the batch that
    // starts at block batch_first.
    const auto statistics_leaf = [&](unsigned lane, std::size_t chunk, std::size_t item, std::size_t batch_first,
                                     std::size_t count, std::stop_token leaf_stop) {
        const auto begin = chunk * chunk_blocks, end = std::min(count, begin + chunk_blocks);
        if (!item) {
            const auto global = batch_first + end;
            if (!batch_first && !chunk) prepare_blocks(batch_first, global, leaf_stop);
            prepare_blocks(global, std::min({global + chunk_blocks, (global / batch_capacity + 1) * batch_capacity,
                total_blocks}), leaf_stop);
        }
        auto& lane_scratch = scratch[lane];
        if (item < independent_count) {
            auto& atom = independent_states[item];
            bool nonzero = atom.nonzero_basis;
            for (auto index = begin; index < end; ++index) {
                check_cancelled(leaf_stop);
                const auto& block = batch[index].observation;
                const auto slot_index = item * batch_capacity + index;
                if (!independent_tracked) {
                    auto& statistics = center_statistics[slot_index];
                    statistics = {};
                    sufficient_statistics(basis, independent_maps[item], independent_paths, block,
                        config.noise_variance, lane_scratch, statistics.correlations, statistics.gram, nonzero,
                        leaf_stop);
                    statistics.nonzero_basis = nonzero;
                    continue;
                }
                const auto templates = render_templates(basis, independent_maps[item], independent_paths,
                    block, lane_scratch, leaf_stop);
                if (!nonzero) nonzero = std::ranges::any_of(templates, [](auto value) { return value != Complex{}; });
                independent_nonzero[slot_index] = nonzero;
                projected_track_cells(templates, independent_paths.size(), block, config.sample_rate_hz,
                    independent_track, lane_scratch, tracking_scratch[lane],
                    std::span(independent_track_cells).subspan(slot_index * independent_cells, independent_cells),
                    leaf_stop);
            }
        } else if (item < independent_count + states.size()) {
            const auto candidate = item - independent_count;
            const auto& paths = bases[candidate % model_count];
            bool nonzero = states[candidate].nonzero_basis;
            for (auto index = begin; index < end; ++index) {
                check_cancelled(leaf_stop);
                auto& statistics = affine_statistics[candidate * batch_capacity + index];
                statistics = {};
                sufficient_statistics(basis, mapped[candidate / model_count], std::span(paths.taps).first(paths.count),
                    batch[index].observation, config.noise_variance, lane_scratch, statistics.correlations,
                    statistics.gram, nonzero, leaf_stop);
                statistics.nonzero_basis = nonzero;
            }
        } else {
            // One short interior echo with a free gain. A stable
            // geometry term and the original dynamic gains remain in
            // the other half of this public likelihood mixture.
            const auto coordinate = item - independent_count - states.size();
            for (auto index = begin; index < end; ++index) {
                check_cancelled(leaf_stop);
                const auto& entry = batch[index];
                const auto& block = entry.observation;
                const auto rows = block.projection->rows();
                const auto slot_index = coordinate * batch_capacity + index;
                if (projected_tracks) {
                    const auto templates = render_templates(basis, tracking_maps[coordinate], track_taps,
                        block, lane_scratch, leaf_stop);
                    projected_track_cells(templates, track_taps.size(), block, config.sample_rate_hz, track,
                        lane_scratch, tracking_scratch[lane],
                        std::span(projected_track_storage).subspan(slot_index * track_cells, track_cells), leaf_stop);
                    continue;
                }
                const auto lattice = raw_track_lattice(track.states);
                raw_track_statistics(basis, tracking_maps[coordinate], track_taps, block.first,
                    iq.subspan(block.first, rows), track, *tracking_fft, lane_scratch, tracking_scratch[lane],
                    std::span(raw_track_grams).subspan(2 * slot_index).first<2>(),
                    std::span(raw_track_values).subspan(slot_index * lattice, lattice), config.sample_rate_hz,
                    span ? &span->coordinates[coordinate].blocks[block.first / block_rows] : nullptr,
                    entry.observation_energy, leaf_stop);
            }
        }
    };
    // A device item's chunk of the batch that starts at block render_first,
    // rendered into a staging slot with each block's nonzero-basis flag, by
    // the native requests: the affine items as sufficient_statistics renders
    // them and the tracked atoms as the projected tracker does. Its first
    // item fills the tiles ahead, as a statistics leaf does.
    const auto device_items = device.session ? affine_items + tracked_atoms : 0;
    const auto render_leaf = [&](unsigned lane, std::size_t chunk, std::size_t item, std::size_t render_first,
                                 std::size_t count, std::size_t staging_slot, std::stop_token leaf_stop) {
        const auto begin = chunk * chunk_blocks, end = std::min(count, begin + chunk_blocks);
        if (!item) {
            const auto global = render_first + end;
            if (!render_first && !chunk) prepare_blocks(render_first, global, leaf_stop);
            prepare_blocks(global, std::min({global + chunk_blocks, (global / batch_capacity + 1) * batch_capacity,
                total_blocks}), leaf_stop);
        }
        auto& lane_scratch = scratch[lane];
        const auto staging = device.session->template_staging(staging_slot);
        auto* templates = reinterpret_cast<Complex*>(staging.data());
        auto* flags = reinterpret_cast<unsigned char*>(staging.data() + device.layout.flags_offset);
        std::size_t column = device.shape.affine_columns + 4 * (item - std::min(item, affine_items));
        if (item < affine_items) {
            column = 0;
            for (std::size_t prior = 0; prior < item; ++prior) column += 2 * affine_taps(prior);
        }
        const auto columns = item < affine_items ? 2 * affine_taps(item) : 2 * independent_taps;
        for (auto index = begin; index < end; ++index) {
            check_cancelled(leaf_stop);
            const auto [block_first, rows] = block_extent(render_first + index);
            const auto out = std::span(templates + index * device.layout.block_values + column * rows, columns * rows);
            auto host_templates = std::span(lane_scratch.raw).first(columns * rows);
            bool nonzero = false;
            if (item < states.size()) {
                const auto& paths = bases[item % model_count];
                render_templates_into(basis, mapped[item / model_count], std::span(paths.taps).first(paths.count),
                                      block_first, rows, host_templates, lane_scratch, leaf_stop);
            } else if (item < affine_items) {
                render_templates_into(basis, independent_maps[item - states.size()], independent_paths, block_first,
                                      rows, host_templates, lane_scratch, leaf_stop);
            } else {
                render_templates_into(basis, independent_maps[item - affine_items], independent_paths,
                    block_first, rows, host_templates, lane_scratch, leaf_stop);
                nonzero = std::ranges::any_of(host_templates, [](auto value) { return value != Complex{}; });
            }
            // Required for a complete receipt even on slots that contain only noise.
            for (std::size_t c = 0; item < affine_items && c < columns && !nonzero; ++c)
                nonzero = finite_energy(host_templates.subspan(c * rows, rows)) > 0;
            // Read the witness from cached host scratch; mapped device-local reads are costly.
            std::ranges::copy(host_templates, out.begin());
            flags[index * device.layout.items + item] = nonzero;
        }
    };
    // One device call for the batch from batch_first, whose templates and
    // projected observations are in its staging slot. A published call's
    // statistics take each block's rendered nonzero-basis flag, whose OR
    // over the stage is the native one.
    const auto device_statistics = [&](std::size_t staging_slot, std::size_t batch_first, std::size_t count,
                                       std::stop_token leaf_stop) {
        const auto staging = device.session->template_staging(staging_slot);
        VaryingBatch request;
        request.first_block = batch_first;
        request.blocks = count;
        request.terminal_last = batch_first + count == total_blocks;
        request.tables = device.tables;
        request.projected_y = {reinterpret_cast<const Complex*>(staging.data() + device.layout.projected_offset),
                               count * device.layout.mode_stride};
        request.independent_shifts = device.shifts;
        request.staging_slot = staging_slot;
        request.affine_taps = device.taps;
        request.variance = config.noise_variance;
        VaryingBatchOutput output{affine_statistics, independent_track_cells, center_statistics, batch_capacity};
        StageDeviceWork work;
        if (!device.session->statistics(request, output, work, leaf_stop)) return false;
        const auto* flags = reinterpret_cast<const unsigned char*>(staging.data() + device.layout.flags_offset);
        for (std::size_t index = 0; index < count; ++index)
            for (std::size_t item = 0; item < device_items; ++item) {
                const bool nonzero = flags[index * device.layout.items + item] != 0;
                if (item < states.size()) affine_statistics[item * batch_capacity + index].nonzero_basis = nonzero;
                else if (item < affine_items)
                    center_statistics[(item - states.size()) * batch_capacity + index].nonzero_basis = nonzero;
                else independent_nonzero[(item - affine_items) * batch_capacity + index] = nonzero;
            }
        return true;
    };
    std::uint64_t rendered_rows = 0;
    double previous_center = 0;
    std::size_t batch_index = 0;
    if (device.session) {
        // The first batch's templates, before its observations are projected.
        const auto count = std::min(batch_capacity, total_blocks);
        phase((count + chunk_blocks - 1) / chunk_blocks * device_items,
              [&](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) {
            const auto began = FineClock::now();
            lane_began(lane, began);
            render_leaf(lane, leaf / device_items, leaf % device_items, 0, count, 0, leaf_stop);
            lane_spent(lane_time[lane].statistics, lane, began);
        });
    }
    for (std::size_t first = 0, batch_first = 0; first < iq.size(); batch_first += batch_capacity) {
        check_cancelled(stop);
        std::size_t count = 0;
        for (; count < batch_capacity && first < iq.size(); ++count) {
            auto& block = batch[count].observation;
            const auto terminal = iq.size() - first < 2 * block_rows;
            const auto& projection = terminal ? final_projection : block_projection;
            const auto rows = projection.rows();
            block.first = first;
            block.projection = &projection;
            const auto center = (static_cast<double>(first) + (rows - 1) * .5) / config.sample_rate_hz;
            block.elapsed = first ? center - previous_center : 0;
            previous_center = center;
            first += rows;
        }
        // P: each block's projected observation and its recursion inputs.
        phase(count, [&](unsigned lane, std::size_t index, std::stop_token leaf_stop) {
            const auto began = FineClock::now();
            lane_began(lane, began);
            auto& entry = batch[index];
            auto& block = entry.observation;
            const auto& projection = *block.projection;
            const auto rows = projection.rows();
            // Diffuse Gaussian amplitude correlations decay with squared lag.
            // The first-order approximation retains the correct block-step
            // correlation without keeping any realized channel history.
            const auto gain_width = config.random_doppler_width_hz > 0 ? config.random_doppler_width_hz : .35;
            const auto gain_lag = std::numbers::pi * gain_width * block.elapsed;
            entry.gain_correlation = std::exp(-.5 * gain_lag * gain_lag);
            const auto raw = iq.subspan(block.first, rows);
            entry.observation_energy = span ? finite_energy(raw) : 0.;
            if (span) span->observation_energy[block.first / block_rows] = entry.observation_energy;
            auto& observation_scratch = scratch[lane].fft;
            block.projected.assign(raw.begin(), raw.end());
            projection.apply(block.projected, observation_scratch, leaf_stop);
            block.projected.resize(projection.modes());
            if (line) {
                block.line = project_line(*line, projection, block.first, config.sample_rate_hz,
                    std::span(entry.line_storage).first(rows), observation_scratch, leaf_stop);
                remove_line(block.projected, block.line);
            }
            if (device.session) {
                // The device reads this batch's observations from its staging slot.
                const auto staging = device.session->template_staging(batch_index % 2);
                std::ranges::copy(block.projected, reinterpret_cast<Complex*>(staging.data() +
                    device.layout.projected_offset) + index * device.layout.mode_stride);
            }
            entry.transitions = {};
            const auto coherence = block_rows / config.sample_rate_hz;
            for (std::size_t model = 0; model < models; ++model) {
                const double exponent = -block.elapsed / (coherence * (1 << model));
                entry.transitions[4 * model] = std::exp(2 * exponent);
                entry.transitions[4 * model + 1] = -std::expm1(2 * exponent);
                entry.transitions[4 * model + 2] = std::exp(exponent);
            }
            lane_spent(lane_time[lane].projection, lane, began);
        });
        // S: block statistics of every family, chunk-major. A chunk's first
        // leaf fills the next chunk's tiles while the others score this one.
        // With a device session, its call scores this batch's affine items
        // and tracked atoms beside the tracker's leaves, while the remaining
        // leaves render the next batch into the other staging slot. A call
        // the session returns natively scores those items here instead.
        const auto chunks = (count + chunk_blocks - 1) / chunk_blocks;
        const auto host_items = independent_count + states.size();
        const auto native_statistics = [&](std::size_t phase_items) {
            phase(chunks * phase_items, [&](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) {
                const auto began = FineClock::now();
                lane_began(lane, began);
                statistics_leaf(lane, leaf / phase_items, leaf % phase_items, batch_first, count, leaf_stop);
                lane_spent(lane_time[lane].statistics, lane, began);
            });
        };
        if (!device.session) {
            native_statistics(items);
        } else {
            const auto slot_index = batch_index % 2;
            const auto next_first = batch_first + count;
            const auto next_count = std::min(batch_capacity, total_blocks - next_first);
            const auto next_chunks = (next_count + chunk_blocks - 1) / chunk_blocks;
            const auto track_leaves = chunks * track_count;
            bool accepted = false;
            phase(1 + track_leaves + next_chunks * device_items,
                  [&](unsigned lane, std::size_t leaf, std::stop_token leaf_stop) {
                const auto began = FineClock::now();
                lane_began(lane, began);
                if (!leaf) {
                    accepted = device_statistics(slot_index, batch_first, count, leaf_stop);
                } else if (leaf - 1 < track_leaves) {
                    const auto track_leaf = leaf - 1;
                    statistics_leaf(lane, track_leaf / track_count, host_items + track_leaf % track_count,
                                    batch_first, count, leaf_stop);
                } else {
                    const auto render = leaf - 1 - track_leaves;
                    render_leaf(lane, render / device_items, render % device_items, next_first, next_count,
                                1 - slot_index, leaf_stop);
                }
                lane_spent(lane_time[lane].statistics, lane, began);
            });
            check_cancelled(stop);
            if (accepted) {
                ++device.device_batches;
            } else {
                ++device.native_batches;
                native_statistics(host_items);
            }
        }
        // R: one leaf per recursive chain, through the batch in block order.
        phase(items, [&](unsigned lane, std::size_t chain, std::stop_token leaf_stop) {
            const auto began = FineClock::now();
            lane_began(lane, began);
            if (chain < independent_count) {
                auto& atom = independent_states[chain];
                for (std::size_t index = 0; index < count; ++index) {
                    check_cancelled(leaf_stop);
                    const auto& block = batch[index].observation;
                    const auto slot_index = chain * batch_capacity + index;
                    if (!independent_tracked) {
                        advance_independent_center(atom, center_statistics[slot_index], block);
                        continue;
                    }
                    advance_independent_gain(atom,
                        std::span(independent_track_cells).subspan(slot_index * independent_cells, independent_cells),
                        independent_nonzero[slot_index] != 0, block, config.noise_variance, independent_track, leaf_stop);
                }
            } else if (chain < independent_count + track_count) {
                const auto coordinate = chain - independent_count;
                auto* persistent = tracking_posteriors.empty() ? nullptr : &tracking_posteriors[coordinate];
                for (std::size_t index = 0; index < count; ++index) {
                    check_cancelled(leaf_stop);
                    const auto& entry = batch[index];
                    const auto& block = entry.observation;
                    const auto slot_index = coordinate * batch_capacity + index;
                    if (projected_tracks) {
                        advance_projected_track(std::span(projected_track_storage).subspan(slot_index * track_cells,
                            track_cells), track_taps.size(), block, config.noise_variance, config.sample_rate_hz, track,
                            tracking_weights[coordinate], persistent, entry.gain_correlation);
                        continue;
                    }
                    const auto lattice = raw_track_lattice(track.states);
                    advance_raw_track(std::span<const RawTrackGram>(raw_track_grams).subspan(2 * slot_index).first<2>(),
                        std::span(raw_track_values).subspan(slot_index * lattice, lattice), track_taps.size(),
                        block.first, block.projection->rows(), config.noise_variance, track, tracking_weights[coordinate],
                        config.sample_rate_hz, persistent, entry.gain_correlation);
                }
            } else {
                const auto candidate = chain - independent_count - track_count;
                const auto& paths = bases[candidate % model_count];
                for (std::size_t index = 0; index < count; ++index) {
                    check_cancelled(leaf_stop);
                    const auto& entry = batch[index];
                    advance_state(states[candidate], mapped[candidate / model_count], basis,
                        std::span(paths.taps).first(paths.count), std::span(&entry.observation, 1),
                        {&affine_statistics[candidate * batch_capacity + index], nullptr},
                        entry.transitions, config.noise_variance, scratch[lane], nullptr, leaf_stop, varying_power);
                }
            }
            lane_spent(lane_time[lane].recursion, lane, began);
        });
        for (std::size_t index = 0; index < count; ++index) {
            const auto rows = batch[index].observation.projection->rows();
            for (std::size_t model = 0; !plan.coherent_gain_blocks && model < model_count; ++model)
                add_work(rendered_rows, checked_multiply(rows, 2 * bases[model].count * selected.size()));
            add_work(score.nominal_independent_gain_basis_rows, checked_multiply(rows, 4 * nominal_count));
            add_work(score.signed_lag_gain_basis_rows, checked_multiply(rows, 4 * lag_count));
            add_work(score.independent_gain_emission_cells, checked_multiply(2 * independent_gain_powers.size() *
                independent_count, std::max<std::size_t>(1, independent_track.states)));
            if (track.states) {
                const auto per_coordinate = checked_multiply(rows,
                    2 * (config.spread_delay_seconds || (plan.coherent_gain_blocks && config.spread_doppler_hz) ? 2 : 1));
                const auto extra = midpoint ? tracking_coordinates.size() - ordinary_tracking_coordinates : 0;
                add_work(rendered_rows, checked_multiply(per_coordinate, ordinary_tracking_coordinates + extra));
                if (midpoint) add_work(score.midpoint_timing_basis_rows, checked_multiply(per_coordinate, extra));
                if (clock_shadow)
                    add_work(clock_shadow->added_basis_rows, checked_multiply(per_coordinate, clock_shadow->added_coordinates));
            }
            ++score.varying_blocks;
        }
        ++score.varying_batches;
        ++batch_index;
    }
    // The route of the affine and tracked independent statistics; the
    // raw-composite tracker, observation projection and every recursion
    // are native FP64 on every route.
    if (device.device_batches) {
        score.varying_fine_route = device.native_batches ? "vulkan_partly_native" : "vulkan";
        score.varying_fine_arithmetic = device.native_batches ? "vulkan_fp64_fft_fp64_state_partly_native_fp64" :
                                                                "vulkan_fp64_fft_fp64_state";
    } else {
        score.varying_fine_route = "native";
        score.varying_fine_arithmetic = "native_fp64";
    }
    // Release the device lease with the stage's batches.
    device.session.reset();
    score.varying_batch_blocks = batch_capacity;
    score.varying_workspace_bytes = plan.workspace_bytes;
    for (const auto& lane : lane_time) {
        score.varying_projection_seconds += lane.projection;
        score.varying_statistics_seconds += lane.statistics;
        score.varying_recursion_seconds += lane.recursion;
    }
    score.varying_wait_seconds += idle_lane_seconds;
    const auto independent = std::span(independent_states).first(independent_count);
    if (!plan.coherent_gain_blocks && !std::ranges::any_of(states, [](const State& state) { return state.nonzero_basis; }) &&
        !std::ranges::any_of(independent, [](const auto& state) { return state.nonzero_basis; })) {
        score.varying_status = "no_observed_varying_basis";
        return;
    }
    score.varying_candidates = selected.size();
    score.varying_block_rows = block_rows;
    add_work(rendered_rows, score.nominal_independent_gain_basis_rows);
    add_work(rendered_rows, score.signed_lag_gain_basis_rows);
    score.varying_basis_rows = rendered_rows;
    score.varying_bit_evidence = plan.coherent_gain_blocks ?
        std::array<double, 2>{-infinity, -infinity} : bit_scores(states);
    score.fixed_varying_bit_evidence = score.varying_bit_evidence;
    const auto frequency_log_count = track.states ? std::log1p(2 * std::ceil(
        (config.frequency_bound_hz + config.time_varying_atmospheric_hz) /
        std::min(.01, receiver_coarse_frequency_step(slot.format, config.sample_rate_hz)))) : 0.;
    if (track.states) {
        // The same selected full-grid mass is applied by the caller. Undo
        // that factor locally for this branch's separate timing prior.
        const auto log_adjustment = std::log(static_cast<double>(ordinary_tracking_coordinates)) -
            std::log(static_cast<double>(selected.size())) + frequency_log_count;
        score.varying_frequency_states = track.states;
        score.tracked_frequency_log_prior_adjustment = log_adjustment;
        const auto log_extra_average = clock_shadow ? std::log(static_cast<double>(tracking_coordinates.size()) /
            ordinary_tracking_coordinates) : 0.;
        if (clock_shadow) clock_shadow->tracked_log_prior_adjustment = log_adjustment + log_extra_average;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            auto total = -infinity;
            for (const auto& atom : std::span(tracking_weights).first(ordinary_tracking_coordinates))
                for (const auto& power : atom[bit])
                    total = std::accumulate(power.begin(), power.begin() + track.states, total, log_add);
            if (!tracking_posteriors.empty()) {
                double persistent = -infinity;
                for (const auto& atom : tracking_posteriors)
                    for (const auto& power : atom[bit])
                        for (const auto& state : std::span(power).first(track.states))
                            persistent = log_add(persistent, state.log_evidence);
                total = log_add(total, persistent) - log_two;
            }
            score.tracked_frequency_bit_evidence[bit] = total;
            auto calibrated = total + log_adjustment;
            if (midpoint) {
                double added = -infinity;
                for (const auto index : std::span(midpoint_indices).first(midpoint_count))
                    for (const auto& power : tracking_weights[index][bit])
                        added = std::accumulate(power.begin(), power.begin() + track.states, added, log_add);
                // Every state starts with the ordinary coordinate count.
                // Change only the new family's full timing-cell measure:
                // old (epoch, arrival, clock) cells to (epoch, signed lag).
                const auto midpoint_adjustment = log_adjustment + midpoint->original_timing_log_count -
                    std::log(static_cast<double>(receiver_epoch_offsets.size())) -
                    std::log(static_cast<double>(midpoint->public_lag_count));
                calibrated = log_add(calibrated + std::log(7. / 8),
                    added + midpoint_adjustment + std::log(1. / 8));
            }
            score.varying_bit_evidence[bit] = plan.coherent_gain_blocks ? calibrated :
                log_add(score.varying_bit_evidence[bit], calibrated) - log_two;
            if (clock_shadow) {
                for (const auto& atom : std::span(tracking_weights).subspan(ordinary_tracking_coordinates))
                    for (const auto& power : atom[bit])
                        total = std::accumulate(power.begin(), power.begin() + track.states, total, log_add);
                clock_shadow->tracked_bit_evidence[bit] = total - log_extra_average;
                clock_shadow->varying_bit_evidence[bit] =
                    log_add(score.fixed_varying_bit_evidence[bit], total + log_adjustment) - log_two;
            }
        }
    }
    score.nominal_independent_gain_candidates = nominal_count;
    score.signed_lag_gain_candidates = lag_count;
    if (nominal_count) {
        // As for the ordinary tracker, this finite frequency family owns
        // a complete, normalized frequency prior. Restore only the old
        // frequency-grid divisor, leaving the full public timing mass.
        // The central-only projection fallback still pays the entire grid.
        score.nominal_independent_gain_frequency_states = independent_track.states;
        score.nominal_independent_gain_frequency_status = independent_track.frequency_extension_status;
        if (independent_track.states) score.nominal_independent_gain_log_prior_adjustment = frequency_log_count;
        const auto log_mass = score.nominal_independent_gain_log_prior_adjustment - std::log(static_cast<double>(selected.size()));
        const auto old_weight = std::log1p(-nominal_independent_gain_weight);
        const auto new_weight = std::log(nominal_independent_gain_weight);
        // The existing K nominal maps retain their full-prior weight 1/T.
        // Give every signed lag and epoch in the fixed public dictionary an
        // equal part of the remaining mass. Unselected maps spend nothing.
        double lag_adjustment = 0.;
        if (lag_count) {
            const auto timing_log_count = midpoint->original_timing_log_count;
            const auto old_mass = std::exp(std::log(static_cast<double>(nominal_count)) - timing_log_count);
            if (!(old_mass > 0 && old_mass < 1))
                throw Error("signed lag gain has no remaining public timing mass");
            lag_adjustment = timing_log_count + std::log1p(-old_mass) -
                std::log(static_cast<double>(receiver_epoch_offsets.size())) -
                std::log(static_cast<double>(midpoint->public_lag_count));
        }
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto nominal = independent_gain_evidence(independent.first(nominal_count), bit, independent_track.states);
            for (std::size_t power = 0; power < nominal.powers.size(); ++power)
                score.nominal_independent_gain_power_bit_evidence[power][bit] = nominal.powers[power] + log_mass;
            score.nominal_independent_fixed_centroid_bit_evidence[bit] = nominal.fixed + log_mass;
            score.nominal_independent_moving_centroid_bit_evidence[bit] = nominal.moving + log_mass;
            score.nominal_independent_gain_bit_evidence[bit] = independent_track.states ?
                log_add(nominal.fixed, nominal.moving) - log_two + log_mass : nominal.fixed + log_mass;
            auto gain = score.nominal_independent_gain_bit_evidence[bit];
            if (lag_count) {
                const auto lag_atoms = independent.last(lag_count);
                const auto lag = independent_gain_evidence(lag_atoms, bit, independent_track.states);
                const auto candidate = independent_track.states ?
                    log_add(lag.fixed, lag.moving) - log_two : lag.fixed;
                score.signed_lag_gain_bit_evidence[bit] = candidate + log_mass + lag_adjustment;
                for (std::size_t i = 0; i < lag_count; ++i) {
                    const auto atom = independent_gain_evidence(lag_atoms.subspan(i, 1), bit, independent_track.states);
                    const auto term = independent_track.states ?
                        log_add(atom.fixed, atom.moving) - log_two : atom.fixed;
                    score.signed_lag_gain_map_bit_evidence[i][bit] = term + log_mass + lag_adjustment;
                }
                gain = log_add(gain, score.signed_lag_gain_bit_evidence[bit]);
            }
            score.varying_bit_evidence[bit] = log_add(score.varying_bit_evidence[bit] + old_weight,
                gain + new_weight);
            if (clock_shadow)
                clock_shadow->varying_bit_evidence[bit] = log_add(clock_shadow->varying_bit_evidence[bit] + old_weight,
                    gain + new_weight);
        }
    }
    score.varying_fine_seconds = seconds_since(began);
}

namespace {
// The first rows of the whole public blocks of one length that lie inside the
// quiet aperture, in calendar order: segment, then block.
std::vector<std::size_t> varying_quiet_blocks(const BlockCalendar& calendar, const ReceiverSlotPlan& slot,
                                              std::size_t rows, std::stop_token stop) {
    const auto quiet_first = slot.quiet_first_sample - slot.first_sample;
    const auto quiet_stop = slot.quiet_stop_sample - slot.first_sample;
    std::vector<std::size_t> blocks;
    for (std::size_t segment_first = 0; segment_first < quiet_stop;) {
        check_cancelled(stop);
        const auto segment = calendar.segment_length(segment_first);
        auto first = segment_first;
        for (std::size_t block = 0; block < calendar.block_count(segment); ++block) {
            const auto length = calendar.block_length(segment, block);
            if (length == rows && quiet_first <= first && first + rows <= quiet_stop) blocks.push_back(first);
            first += length;
        }
        segment_first += segment;
    }
    return blocks;
}
} // namespace

std::size_t packed_affine_quiet_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const BlockCalendar calendar(config, slot);
    const ProjectionBands bands(config, slot);
    std::size_t bytes = 4096, rows_maximum = 0, scratch = 0, float_rows = 0;
    for (const auto rows : calendar.lengths()) {
        if (!rows) continue;
        const auto modes = visit_projection_modes(rows, bands, {}, [](std::size_t) {});
        bytes = checked_add(bytes, DftPlan::workspace_bytes(rows));
        bytes = checked_add(bytes, checked_multiply(rows, sizeof(std::size_t)));
        if (bands.use_float(rows, modes)) {
            bytes = checked_add(bytes, FloatProjection::workspace_bytes());
            float_rows = FloatProjection::scratch_rows;
        }
        rows_maximum = std::max(rows_maximum, rows);
        scratch = std::max(scratch, DftPlan::scratch_samples(rows));
    }
    bytes = checked_add(bytes, checked_multiply(float_rows, sizeof(FineFloatComplex)));
    return checked_add(bytes, checked_multiply(rows_maximum + scratch, sizeof(Complex)));
}

void score_packed_affine_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        std::span<const Complex> iq, ReceiverSlotScore& score, std::stop_token stop) {
    const BlockCalendar calendar(config, slot);
    const ProjectionBands bands(config, slot);
    const auto lengths = calendar.lengths();
    std::array<std::optional<Projection>, 4> projections;
    std::size_t scratch_rows = 0, float_rows = 0;
    for (std::size_t index = 0; index < lengths.size() && lengths[index]; ++index) {
        projections[index].emplace(lengths[index], bands, stop);
        scratch_rows = std::max(scratch_rows, DftPlan::scratch_samples(lengths[index]));
        float_rows = std::max(float_rows, projections[index]->float_scratch_rows());
    }
    std::vector<Complex> values(*std::ranges::max_element(lengths)), scratch(scratch_rows);
    std::vector<FineFloatComplex> float_scratch(float_rows);
    score.quiet_energy = 0;
    score.quiet_modes = 0;
    for (std::size_t segment = 0; segment < iq.size();) {
        const auto segment_rows = calendar.segment_length(segment);
        auto first = segment;
        for (std::size_t block = 0; block < calendar.block_count(segment_rows); ++block) {
            check_cancelled(stop);
            const auto rows = calendar.block_length(segment_rows, block);
            const auto index = std::ranges::find(lengths, rows) - lengths.begin();
            const auto& projection = *projections[index];
            const auto observed = std::span(values).first(rows);
            std::ranges::copy(iq.subspan(first, rows), observed.begin());
            projection.apply(observed, scratch, stop, float_scratch);
            score.quiet_energy += finite_energy(observed.first(projection.modes())) / config.noise_variance;
            score.quiet_modes += projection.modes();
            first += rows;
        }
        segment += segment_rows;
    }
    if (!std::isfinite(score.quiet_energy)) throw Error("Robust quiet energy is nonfinite");
    score.quiet_threshold = quiet_noise_threshold(score.quiet_modes);
    score.noise_statistics_valid = score.complete && score.all_valid && score.quiet_modes;
}

std::size_t varying_quiet_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    if (!config.time_varying_channel) return 0;
    const BlockCalendar calendar(config, slot);
    const auto lengths = calendar.lengths();
    const auto maximum = *std::ranges::max_element(lengths);
    auto bytes = checked_add(DftPlan::workspace_bytes(maximum), checked_multiply(maximum, sizeof(std::size_t)));
    std::optional<ProjectionBands> bands;
    try {
        bands.emplace(config, slot, ProjectionBands::Policy::separable_varying);
    } catch (const Error&) {
        return bytes;
    }
    // One length at a time: each lane's block and transform scratch, the
    // block list, and every projected mode's term until its ordered sum.
    // Disjoint whole blocks inside the aperture bound the block count.
    const auto quiet_rows = slot.quiet_stop_sample > slot.quiet_first_sample ?
        static_cast<std::size_t>(slot.quiet_stop_sample - slot.quiet_first_sample) : std::size_t{0};
    std::size_t length_bytes = 0;
    for (const auto rows : lengths) {
        if (!rows || !projected_block_supported(rows, *bands)) continue;
        const auto blocks = quiet_rows / rows;
        if (!blocks) continue;
        std::size_t modes = rows;
        try {
            modes = visit_projection_modes(rows, *bands, {}, [](std::size_t) {});
        } catch (const Error&) {
        }
        const auto lanes = std::min<std::size_t>(std::max(1U, workers), blocks);
        auto stage = checked_multiply(lanes, checked_multiply(rows + DftPlan::scratch_samples(rows), sizeof(Complex)));
        stage = checked_add(stage, checked_multiply(blocks, sizeof(std::size_t)));
        stage = checked_add(stage, checked_multiply(checked_multiply(blocks, modes), sizeof(double)));
        length_bytes = std::max(length_bytes, stage);
    }
    return checked_add(bytes, length_bytes);
}

namespace {
constexpr std::size_t maximum_line_fft_rows = 262'144;
constexpr std::size_t line_frequency_refinement = 512;

std::size_t line_fft_rows(const ReceiverSlotPlan& slot) {
    const auto rows = static_cast<std::size_t>(slot.quiet_stop_sample - slot.quiet_first_sample);
    if (rows < 16) return 0;
    return rows >= maximum_line_fft_rows / 2 ? maximum_line_fft_rows : std::bit_ceil(2 * rows);
}
} // namespace

std::size_t varying_line_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto fft_rows = config.time_varying_channel ? line_fft_rows(slot) : 0;
    return checked_multiply(2 * fft_rows, sizeof(Complex));
}

std::optional<LineAtom> find_varying_line(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                        std::span<const Complex> iq, std::stop_token stop, double threshold) {
    const auto fft_rows = line_fft_rows(slot);
    if (!fft_rows) return std::nullopt;
    std::optional<ProjectionBands> bands;
    try {
        bands.emplace(config, slot, ProjectionBands::Policy::separable_varying);
    } catch (const Error&) {
        return std::nullopt;
    }
    const auto first = slot.quiet_first_sample - slot.first_sample;
    const auto stop_row = slot.quiet_stop_sample - slot.first_sample;
    const auto observed = iq.subspan(first, stop_row - first);
    const auto fs = config.sample_rate_hz;
    const auto search_row_count = std::min(observed.size(), fft_rows / 2);
    const auto search = observed.subspan((observed.size() - search_row_count) / 2, search_row_count);
    const auto retained_hz = [&](double frequency) {
        return frequency >= bands->required.lower_hz && frequency <= bands->required.upper_hz &&
            (!bands->use_exclusions || std::ranges::none_of(bands->excluded, [&](const auto& band) {
                return band.lower_hz <= frequency && frequency <= band.upper_hz;
            }));
    };
    std::vector<Complex> spectrum(fft_rows);
    std::ranges::copy(search, spectrum.begin());
    const modem::detail::search_fft::DoublePlan fft(fft_rows, stop);
    modem::detail::search_fft::fft(std::span(spectrum), false, fft, stop);
    const double fft_spacing = fs / fft_rows;
    const auto radius = static_cast<std::int64_t>(std::min(fft_rows / 2 - 1,
        static_cast<std::size_t>(std::ceil(bands->required.upper_hz / fft_spacing))));
    double power = 0, frequency = 0;
    for (auto bin = -radius; bin <= radius; ++bin) {
        if (!(static_cast<std::uint64_t>(bin) & 4095)) check_cancelled(stop);
        if (!retained_hz(bin * fft_spacing)) continue;
        const auto index = static_cast<std::size_t>(bin < 0 ? bin + fft_rows : bin);
        const auto candidate = std::norm(spectrum[index]);
        if (candidate > power) {
            power = candidate;
            frequency = bin * fft_spacing;
        }
    }
    // Reject ordinary noise peaks before the more precise search. This
    // only selects which fixed nuisance model, if any, will be scored.
    if (power <= search_row_count * config.noise_variance * threshold)
        return std::nullopt;
    const auto likelihood_power = [&](double hz, std::span<const Complex> samples) {
        const auto increment = std::polar(1., -tau * hz / fs);
        auto phase = Complex{1};
        Complex correlation{};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (!(i % 4096)) {
                check_cancelled(stop);
                phase = std::polar(1., -tau * hz * i / fs);
            }
            correlation += samples[i] * phase;
            phase *= increment;
        }
        return std::norm(correlation);
    };
    const double spacing = fs / (observed.size() * static_cast<double>(line_frequency_refinement));
    const auto radius_exact = std::ceil(bands->required.upper_hz / spacing);
    if (!std::isfinite(radius_exact) || radius_exact >= 0x1p60) return std::nullopt;
    const auto public_radius = static_cast<std::int64_t>(radius_exact);
    // Coarse-to-fine local coherent proposals on a predetermined grid.
    // The normalization counts every public grid position, including
    // unscored ones and those outside the separable band.
    const auto step_exact = std::ceil(fft_spacing / spacing);
    if (!std::isfinite(step_exact) || step_exact >= 0x1p60) return std::nullopt;
    auto step = std::bit_ceil(static_cast<std::uint64_t>(step_exact));
    auto center = static_cast<std::int64_t>(std::llround(frequency / (step * spacing))) * static_cast<std::int64_t>(step);
    for (; step; step /= 2) {
        auto best = center;
        power = -1;
        for (const auto offset : {-1, 0, 1}) {
            const auto atom = center + offset * static_cast<std::int64_t>(step);
            if (std::abs(atom) > public_radius || !retained_hz(atom * spacing)) continue;
            const auto trial = likelihood_power(atom * spacing, step * spacing > fs / observed.size() / 4 ? search : observed);
            if (trial > power) {
                best = atom;
                power = trial;
            }
        }
        center = best;
    }
    if (!(power > 0)) return std::nullopt;
    const auto variance = config.noise_variance;
    const auto inverse_signal_to_noise = variance / observed.size();
    const auto log_determinant = inverse_signal_to_noise < 1 ?
        std::log1p(inverse_signal_to_noise) + std::log(static_cast<double>(observed.size())) - std::log(variance) :
        std::log1p(1 / inverse_signal_to_noise);
    // Unit variance proper complex line amplitude, independent of DATA
    // in the orthogonal complement. Keep the selected line's public mass.
    const auto log_weighted = (power / observed.size()) / variance / (1 + inverse_signal_to_noise) -
        log_determinant - std::log1p(2 * public_radius);
    if (!(log_weighted >= threshold)) return std::nullopt;
    return LineAtom{first, stop_row, center * spacing, log_weighted};
}

namespace {
void project_varying_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                           std::span<const Complex> iq, ReceiverSlotScore& score, std::stop_token stop,
                           unsigned workers, ReceiverExecutor& executor) {
    if (!config.time_varying_channel || iq.size() != slot.stop_sample - slot.first_sample)
        throw Error("Robust varying quiet requires its complete public observation");
    std::optional<ProjectionBands> bands;
    try {
        bands.emplace(config, slot, ProjectionBands::Policy::separable_varying);
    } catch (const Error&) {
        score.varying_quiet_status = "varying_quiet_capture_band_unavailable";
        return;
    }
    const BlockCalendar calendar(config, slot);
    // The legacy calendar fixes the physical aperture. This extra test uses
    // the same whole, disjoint blocks and only changes public frequency bins.
    // Candidate scores, confidence and retained models never choose a bin.
    // Blocks project independently on the slot's lanes. Their mode terms
    // are then summed in the original length, block and mode order.
    for (const auto rows : calendar.lengths()) {
        if (!rows || !projected_block_supported(rows, *bands)) continue;
        Projection projection(rows, *bands, stop);
        const auto blocks = varying_quiet_blocks(calendar, slot, rows, stop);
        if (blocks.empty()) continue;
        const auto modes = projection.modes();
        const auto lanes = std::min<std::size_t>(std::max(1U, workers), blocks.size());
        std::vector<std::vector<Complex>> projected(lanes), scratch(lanes);
        std::vector<double> terms(checked_multiply(blocks.size(), modes));
        executor.parallel(slot.index, blocks.size(), static_cast<unsigned>(lanes), stop,
            [&](unsigned lane, std::size_t index, std::stop_token job_stop) {
                check_cancelled(job_stop);
                auto& values = projected[lane];
                if (values.empty()) {
                    values.resize(rows);
                    scratch[lane].resize(DftPlan::scratch_samples(rows));
                }
                std::ranges::copy(iq.subspan(blocks[index], rows), values.begin());
                projection.apply(values, scratch[lane], job_stop);
                auto term = terms.begin() + static_cast<std::ptrdiff_t>(index * modes);
                for (const auto value : std::span(values).first(modes))
                    *term++ = std::norm(value) / config.noise_variance;
            });
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            score.varying_quiet_modes = checked_add(score.varying_quiet_modes, modes);
            for (const auto term : std::span(terms).subspan(block * modes, modes))
                score.varying_quiet_energy += term;
        }
    }
    if (!score.varying_quiet_modes) {
        score.varying_quiet_status = "varying_quiet_aperture_has_no_separable_data_modes";
        return;
    }
    if (!std::isfinite(score.varying_quiet_energy))
        throw Error("Robust varying quiet energy is nonfinite");
    score.varying_quiet_threshold = quiet_noise_threshold(score.varying_quiet_modes);
    score.varying_quiet_status = score.varying_quiet_energy <= score.varying_quiet_threshold ?
        "scored_public_varying_quiet_aperture" : "energy_in_public_varying_quiet_aperture";
}
} // namespace

void score_varying_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                         std::span<const Complex> iq, ReceiverSlotScore& score, std::stop_token stop,
                         unsigned workers, ReceiverExecutor& executor) {
    const auto began = FineClock::now();
    project_varying_quiet(config, slot, iq, score, stop, workers, executor);
    score.varying_quiet_seconds = seconds_since(began);
}

namespace {
constexpr std::size_t preferred_drift_fft_rows = 262'144;
// Preferred proposal work per retained timing atom. Three slopes are the
// minimum fair bank: zero followed by the best ridge for each bit.
constexpr std::size_t target_drift_fft_rows_per_atom = 2 * 1024 * 1024;
constexpr std::size_t retained_drift_ridge_timing_atoms = 8;
constexpr std::size_t maximum_drift_public_timing_atoms = maximum_varying_coordinates +
    2 * receiver_epoch_offsets.size() + maximum_segmented_timing_coordinates;
// Timing atoms are independent leaves whose evidence is combined in atom
// order, so the stage may use the whole worker budget. A lane beyond the
// public atom bound could never run. An extended plan narrows only to fit
// its preserved owner lease. An ordinary plan always admits the former
// eight-lane width and widens only into the owner lease's other stages.
constexpr std::size_t maximum_drift_stage_workers = maximum_drift_public_timing_atoms;
constexpr std::size_t ordinary_drift_stage_workers = 8;
constexpr std::size_t minimum_drift_segments = 8;
constexpr std::int64_t maximum_drift_proposal_slope_radius = 128;
constexpr std::int64_t maximum_drift_proposal_frequency_radius = 512;
// The coarse ridge grid strides keep at most 2*512+1 centers and 2*128+1
// interior slopes plus both public slope edges.
constexpr std::size_t maximum_drift_ridge_centers = 2 * maximum_drift_proposal_frequency_radius + 1;
constexpr std::size_t maximum_drift_ridge_slopes = 2 * maximum_drift_proposal_slope_radius + 3;
// Stack use of a ridge search on the lane's charged worker stack: a block's
// slopes, records and windows.
constexpr std::size_t drift_ridge_stack_bytes = 32 * 1024;
// Slope blocks keep their FP64 sums at most about 256 KiB, 16 slopes at the
// largest 1,025-center grid and more on smaller grids up to the block
// maximum, so they stay cache resident while each segment row is read once
// per block. One slope at a time re-streams every row's center window
// (about 111 MB of the 462 MB table at the HF shape). On the 5650U at the
// HF ridge shape (1,762 segments x 16,384 bins, 984 centers at stride 4,
// -O3 -ffp-contract=off) a ridge load took 1.66 ns on one thread and
// 11.9 ns with six concurrent lanes, DRAM bound; blocks of 16 took 0.45 and
// 0.83 ns (32: 0.44 and 0.75; six-lane runs indicative only, on a loaded
// shared host) with bit-identical records.
constexpr std::size_t drift_ridge_block_bytes = 16 * 2 * maximum_drift_ridge_centers * sizeof(double);

// Lane-summed phases of one atom evaluation, one clock read per boundary:
// template render and products, ridge segment FFTs and power tables, ridge
// records/selection, slope dechirp/FFT/scan, and exact statistics.
enum DriftPhase : std::size_t {
    drift_render, drift_ridge, drift_ridge_search, drift_slope, drift_exact, drift_phases
};

template<class T>
struct DriftScratch {
    std::array<std::vector<Complex>, 4> products;
    std::vector<std::complex<T>> frequency;
    std::vector<std::complex<T>> segment_spectrum;
    std::vector<double> segment_power;
    std::vector<double> segment_centers;
    // The render tile, idle during the ridge search, holds its block sums.
    std::vector<Complex> raw;
    std::vector<Complex> rendered;
    std::vector<double> offsets;
    std::vector<Complex> projected_columns;
    std::vector<Complex> projection_scratch;
    std::array<double, drift_phases> seconds{};
    // Device proposal route: this lane's ridge records and slope frontiers,
    // and its device and native atoms (evaluations with device-published
    // proposals, and those with a native fallback; a mixed one is both).
    std::vector<DriftRidgeRecord> device_records;
    std::vector<DriftFrontierRow> device_frontiers;
    std::size_t device_evaluations = 0, native_evaluations = 0;
};

struct DriftStatistics {
    std::array<Complex, 2> correlation{};
    std::array<double, 2> diagonal{};
    Complex off_diagonal{};
};

struct DriftProjection {
    DriftProjection(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const DriftPlan& plan,
                    std::span<const Complex> input, const LineAtom* line, std::stop_token stop)
        : small_rows(input.size() / plan.projection_blocks), large_blocks(input.size() % plan.projection_blocks),
          blocks(plan.projection_blocks), ranking(input.begin(), input.end()), observed(input.begin(), input.end()) {
        const ProjectionBands bands(config, slot, ProjectionBands::Policy::separable_varying);
        small.emplace(small_rows, bands, stop);
        if (large_blocks) large.emplace(small_rows + 1, bands, stop);
        std::vector<Complex> scratch(plan.projection_scratch_rows ? plan.projection_scratch_rows :
                                     DftPlan::scratch_samples(max_rows()));
        if (line) line_storage.resize(input.size());
        for (std::size_t block = 0, first = 0; block < blocks; ++block) {
            const auto& transform = at(block);
            const auto rows = transform.rows();
            const auto target = std::span(ranking).subspan(first, rows);
            if (line) {
                // Ranking can use the same fitted line's time-domain
                // complement before the exact projected likelihood.
                const auto begin = std::max(plan.first + first, line->first);
                const auto end = std::min(plan.first + first + rows, line->stop);
                if (begin < end) {
                    auto overlap = target.subspan(begin - plan.first - first, end - begin);
                    auto tone = std::span(line_storage).subspan(first, overlap.size());
                    const auto norm = 1 / std::sqrt(static_cast<double>(overlap.size()));
                    const auto increment = std::polar(1., tau * line->frequency_hz / config.sample_rate_hz);
                    auto phase = Complex{norm};
                    for (auto& value : tone) {
                        value = phase;
                        phase *= increment;
                    }
                    remove_line(overlap, tone);
                }
            }
            transform.retain_time(target, scratch, stop);
            transform.apply(std::span(observed).subspan(first, rows), scratch, stop);
            if (line) {
                const auto unit = project_line(*line, transform, plan.first + first, config.sample_rate_hz,
                    std::span(line_storage).subspan(first, rows), scratch, stop);
                if (!unit.empty()) remove_line(std::span(observed).subspan(first, rows), unit);
                else std::ranges::fill(std::span(line_storage).subspan(first, rows), Complex{});
            }
            first += rows;
        }
    }

    const Projection& at(std::size_t block) const { return block < large_blocks ? *large : *small; }
    std::size_t max_rows() const { return small_rows + (large_blocks != 0); }

    std::size_t small_rows, large_blocks, blocks;
    std::optional<Projection> small, large;
    std::vector<Complex> ranking, observed, line_storage;
};

struct DriftTimingCache {
    std::array<std::array<DriftPeak, retained_drift_per_bit>, 2> peaks{};
    std::array<double, 2> nomination{-infinity, -infinity};
    std::size_t slope_count = 0;
    int exponent = 0;
    bool ready = false;
};
// Include the rank-pass caches, timing vectors, the first eight lane
// owners/frontiers/statistics and fixed controls in the metadata quote. Each
// wider lane adds its own bounded lane reservation.
constexpr std::size_t drift_metadata_reservation = 64 * 1024;
constexpr std::size_t drift_reserved_metadata_lanes = 8;
constexpr std::size_t drift_lane_metadata_reservation = 2 * 1024;
constexpr auto drift_lane_metadata_bound = sizeof(DriftScratch<double>) +
    sizeof(std::array<std::array<DriftPeak, retained_drift_per_bit>, 2>) +
    sizeof(std::array<std::array<DriftStatistics, retained_drift_per_bit>, 2>);
static_assert(sizeof(DriftScratch<double>) >= sizeof(DriftScratch<float>) &&
              drift_lane_metadata_bound <= drift_lane_metadata_reservation);
constexpr auto drift_metadata_bound = maximum_drift_public_timing_atoms *
    (sizeof(DriftTimingCache) + sizeof(FineCoordinate) + sizeof(std::array<double, 2>) +
     2 * sizeof(std::size_t) + sizeof(double) + 2 * sizeof(std::uint64_t)) +
    drift_reserved_metadata_lanes * drift_lane_metadata_bound +
    sizeof(DriftProjection) + 6 * sizeof(std::vector<std::size_t>) + sizeof(std::vector<std::int64_t>) + 4096;
static_assert(drift_metadata_bound <= drift_metadata_reservation);

static_assert(drift_frontier_ranks == retained_drift_per_bit);

std::size_t drift_metadata_bytes(std::size_t lanes, std::size_t slope_budget) {
    const auto slope_table = checked_multiply(slope_budget,
        maximum_drift_public_timing_atoms * sizeof(std::int64_t));
    const auto base = std::max(drift_metadata_reservation, checked_add(drift_metadata_bound, slope_table));
    return checked_add(base, checked_multiply(
        lanes - std::min(lanes, drift_reserved_metadata_lanes), drift_lane_metadata_reservation));
}

bool same_timing(const FineCoordinate& a, const FineCoordinate& b) {
    return a.epoch_offset_seconds == b.epoch_offset_seconds && a.arrival_seconds == b.arrival_seconds &&
           a.clock_ppm == b.clock_ppm;
}

// The drift stage's timing atoms: the selected coordinates, then the epoch
// timing, each distinct (epoch, arrival, clock) once in that order.
std::vector<FineCoordinate> drift_timing_atoms(std::span<const FineCoordinate> selected,
                                               std::span<const FineCoordinate> epoch_timing) {
    std::vector<FineCoordinate> timing;
    timing.reserve(selected.size() + epoch_timing.size());
    const auto append = [&](const FineCoordinate& coordinate) {
        if (std::none_of(timing.begin(), timing.end(), [&](const auto& other) { return same_timing(coordinate, other); }))
            timing.push_back(coordinate);
    };
    for (const auto& coordinate : selected) append(coordinate);
    for (const auto& coordinate : epoch_timing) append(coordinate);
    return timing;
}

// The fast ridge's exact slope budget; a slow plan keeps the rest.
std::size_t drift_fast_slope_budget(const DriftPlan& plan) {
    return plan.slow_ridge_segments ? std::max<std::size_t>(3, plan.slope_budget / 4) : plan.slope_budget;
}

// FNV-1a over the little-endian bytes of one value.
constexpr std::uint64_t drift_digest_basis = 14695981039346656037ULL;
void drift_digest_add(std::uint64_t& digest, std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
        digest ^= (value >> (8 * byte)) & 0xffU;
        digest *= 1099511628211ULL;
    }
}

std::size_t fft_bin(std::int64_t signed_bin, std::size_t fft_rows) {
    return static_cast<std::size_t>(signed_bin < 0 ? signed_bin + static_cast<std::int64_t>(fft_rows) : signed_bin);
}

// A ridge segment's center time relative to the slope reference.
double drift_segment_center(const DriftPlan& plan, double fs, std::size_t segments, std::size_t segment) {
    const auto first = plan.rows * segment / segments;
    const auto stop_row = plan.rows * (segment + 1) / segments;
    return (plan.first + (plan.sample_stride - 1) * .5) / (fs * plan.sample_stride) +
        (first + stop_row - 1) * .5 / fs - plan.slope_reference_seconds;
}

// The ridge grid of a segment count; its slope stride reads only the first
// and last segment centers.
DriftRidgeGrid drift_ridge_grid_shape(const DriftPlan& plan, double fs, std::size_t segments) {
    if (!plan.rows || !segments || plan.slope_radius < 1)
        throw Error("Robust drift ridge grid requires public rows, slopes and one center per segment");
    DriftRidgeGrid grid;
    grid.segments = segments;
    grid.fft_rows = std::bit_ceil(2 * ((plan.rows + segments - 1) / segments));
    grid.frequency_step = fs / grid.fft_rows;
    const auto front = drift_segment_center(plan, fs, segments, 0);
    const auto back = drift_segment_center(plan, fs, segments, segments - 1);
    const auto nominal_slope_stride = std::max<std::int64_t>(1, std::llround(grid.frequency_step /
        (plan.slope_step * std::max(std::abs(front), std::abs(back)))));
    grid.slope_stride = std::max(nominal_slope_stride,
        1 + (plan.slope_radius - 1) / maximum_drift_proposal_slope_radius);
    grid.bin_radius = std::min(static_cast<std::int64_t>(grid.fft_rows / 2 - 1),
        static_cast<std::int64_t>(std::ceil(plan.frequency_radius * plan.frequency_step / grid.frequency_step)));
    grid.bin_stride = std::max<std::int64_t>(1,
        1 + (grid.bin_radius - 1) / maximum_drift_proposal_frequency_radius);
    grid.centers = static_cast<std::size_t>(2 * grid.bin_radius / grid.bin_stride) + 1;
    // The interior grid includes both public edges only when they lie on it.
    const auto interior = plan.slope_radius / grid.slope_stride;
    grid.slopes = static_cast<std::size_t>(2 * interior + 1) + (interior * grid.slope_stride == plan.slope_radius ? 0 : 2);
    if (grid.bin_radius < 0 || grid.centers > maximum_drift_ridge_centers || grid.slopes > maximum_drift_ridge_slopes)
        throw Error("Robust drift ridge grid exceeds its bounded centers or slopes");
    return grid;
}
} // namespace

// Match a linear ridge through short coherent spectra. This ranking only
// chooses exact slope atoms of the fixed full-slot likelihood below. Noise
// maxima get no posterior mass credit for discarded alternatives.
bool drift_ridge_exhaustive(const DriftPlan& plan, std::size_t slope_budget) {
    return 2 * plan.slope_radius + 1 <= static_cast<std::int64_t>(slope_budget);
}

DriftRidgeGrid drift_ridge_grid(const DriftPlan& plan, double fs, std::size_t segments,
                                std::span<double> centers) {
    if (centers.size() != segments)
        throw Error("Robust drift ridge grid requires public rows, slopes and one center per segment");
    const auto grid = drift_ridge_grid_shape(plan, fs, segments);
    for (std::size_t segment = 0; segment < segments; ++segment)
        centers[segment] = drift_segment_center(plan, fs, segments, segment);
    return grid;
}

std::int64_t drift_ridge_slope(const DriftPlan& plan, const DriftRidgeGrid& grid, std::size_t index) {
    if (index >= grid.slopes) throw Error("Robust drift ridge slope index is outside the grid");
    if (!index) return -plan.slope_radius;
    if (index + 1 == grid.slopes) return plan.slope_radius;
    const auto interior = plan.slope_radius / grid.slope_stride;
    const auto first = interior * grid.slope_stride == plan.slope_radius ? 0 : 1;
    return (static_cast<std::int64_t>(index) - first - interior) * grid.slope_stride;
}

std::int64_t drift_ridge_offset(const DriftPlan& plan, const DriftRidgeGrid& grid, double center, std::int64_t slope) {
    return std::llround(slope * plan.slope_step * center / grid.frequency_step);
}

namespace {
// One slope's window in one segment: its first center's signed bin and
// the center index bounds of its wrapped and unwrapped reads.
struct DriftRidgeWindow {
    std::int64_t origin = 0, low = 0, negative = 0, high = 0;
};
} // namespace

void drift_ridge_records(const DriftPlan& plan, const DriftRidgeGrid& grid, std::span<const double> power,
                         std::span<const double> centers, std::span<const std::int64_t> slopes,
                         std::span<DriftRidgeRecord> records, std::span<double> sums) {
    const auto segments = grid.segments;
    const auto fft_rows = grid.fft_rows;
    const auto block = slopes.size();
    const auto width = grid.centers;
    if (power.size() != 2 * segments * fft_rows || centers.size() != segments || !width ||
        width > maximum_drift_ridge_centers || !block || block > maximum_drift_ridge_block ||
        records.size() != block || sums.size() < 2 * block * width)
        throw Error("Robust drift ridge records require the grid's power table, segment centers and block sums");
    const auto half = static_cast<std::int64_t>(fft_rows / 2);
    const auto count = static_cast<std::int64_t>(width);
    const auto stride = grid.bin_stride;
    // Center indices i >= 0 with i * stride < bound.
    const auto below = [&](std::int64_t bound) {
        return bound <= 0 ? std::int64_t{0} : std::min(count, (bound - 1) / stride + 1);
    };
    std::fill_n(sums.begin(), 2 * block * width, 0.);
    std::array<DriftRidgeWindow, maximum_drift_ridge_block> windows;
    for (std::size_t segment = 0; segment < segments; ++segment) {
        // Center index i of a slope reads signed bin origin + i * stride. It
        // skips this segment unless that bin lies in (-half, half), and a
        // negative bin wraps to the upper half of the spectrum.
        for (std::size_t b = 0; b < block; ++b) {
            const auto origin = drift_ridge_offset(plan, grid, centers[segment], slopes[b]) - grid.bin_radius;
            windows[b] = {origin, below(1 - half - origin), below(-origin), below(half - origin)};
        }
        // Each (slope, bit, center) sum gets its addends in segment order
        // whatever the block; the row stays cached for the block.
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto* row = power.data() + (bit * segments + segment) * fft_rows;
            for (std::size_t b = 0; b < block; ++b) {
                const auto [origin, low, negative, high] = windows[b];
                const auto wrapped = origin + static_cast<std::int64_t>(fft_rows);
                auto* sum = sums.data() + (2 * b + bit) * width;
                for (auto i = low; i < negative; ++i) sum[i] += row[wrapped + i * stride];
                for (auto i = negative; i < high; ++i) sum[i] += row[origin + i * stride];
            }
        }
    }
    for (std::size_t b = 0; b < block; ++b) {
        DriftRidgeRecord record;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto* sum = sums.data() + (2 * b + bit) * width;
            for (std::int64_t i = 0; i < count; ++i)
                if (sum[i] > record.score[bit]) {
                    record.score[bit] = sum[i];
                    record.center[bit] = i * stride - grid.bin_radius;
                }
        }
        records[b] = record;
    }
}

std::size_t drift_ridge_block(const DriftRidgeGrid& grid) {
    if (!grid.centers || grid.centers > maximum_drift_ridge_centers)
        throw Error("Robust drift ridge block requires the grid's bounded centers");
    return std::min(maximum_drift_ridge_block, drift_ridge_block_bytes / (2 * grid.centers * sizeof(double)));
}

DriftRidgeFrontier drift_ridge_frontier(const DriftPlan& plan, const DriftRidgeGrid& grid,
                                        std::span<const double> power, std::span<const double> centers,
                                        std::size_t block, std::span<double> sums, std::stop_token stop) {
    block = std::min({block, grid.slopes, maximum_drift_ridge_block});
    if (!block) throw Error("Robust drift ridge search requires one slope per block");
    std::array<std::int64_t, maximum_drift_ridge_block> slopes;
    std::array<DriftRidgeRecord, maximum_drift_ridge_block> records;
    static_assert(sizeof(slopes) + sizeof(records) + sizeof(std::array<DriftRidgeWindow, maximum_drift_ridge_block>) <=
                  drift_ridge_stack_bytes);
    DriftRidgeFrontier frontier;
    for (std::size_t first = 0; first < grid.slopes; first += block) {
        check_cancelled(stop);
        const auto count = std::min(block, grid.slopes - first);
        for (std::size_t j = 0; j < count; ++j) slopes[j] = drift_ridge_slope(plan, grid, first + j);
        drift_ridge_records(plan, grid, power, centers, std::span(slopes).first(count),
                            std::span(records).first(count), sums);
        for (std::size_t j = 0; j < count; ++j) frontier.insert(slopes[j], records[j]);
    }
    return frontier;
}

void DriftRidgeFrontier::insert(std::int64_t slope, const DriftRidgeRecord& record) {
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const Ridge score{record.score[bit], slope, record.center[bit]};
        auto& frontier = best_[bit];
        const auto where = std::ranges::find_if(frontier, [&](const auto& peak) {
            return score.score > peak.score;
        });
        if (where == frontier.end()) continue;
        std::move_backward(where, frontier.end() - 1, frontier.end());
        *where = score;
    }
}

DriftProposals DriftRidgeFrontier::select(const DriftPlan& plan, const DriftRidgeGrid& grid,
                                          std::span<const double> power, std::span<const double> centers,
                                          std::size_t slope_budget) const {
    DriftProposals proposals;
    auto& result = proposals.slopes;
    result.reserve(slope_budget);
    const auto append = [&](std::int64_t slope) {
        if (result.size() < slope_budget && std::abs(slope) <= plan.slope_radius &&
            std::ranges::find(result, slope) == result.end()) result.push_back(slope);
    };
    append(0);
    if (drift_ridge_exhaustive(plan, slope_budget)) {
        for (auto slope = -plan.slope_radius; slope <= plan.slope_radius; ++slope) append(slope);
        return proposals;
    }
    const auto drift_segments = grid.segments;
    const auto fft_rows = grid.fft_rows;
    // Only the slow-plan refinement reads the power table.
    if ((plan.slow_ridge_segments && power.size() != 2 * drift_segments * fft_rows) || centers.size() != drift_segments)
        throw Error("Robust drift ridge selection requires the grid's power table and segment centers");
    const auto frequency_step = grid.frequency_step;
    const auto slope_stride = grid.slope_stride;
    const auto bin_radius = grid.bin_radius;
    const auto bin_stride = grid.bin_stride;
    const auto retained = best_[0].size();
    auto best = best_;
    // Adjacent coarse slope hypotheses can differ by many full-aperture
    // bins. Refine the best ridges on the stored spectra before spending
    // an exact dechirp FFT on a nominal neighbor.
    const auto coarse_best = best;
    const auto interpolate_power = [&](std::size_t bit, std::size_t segment, double bin) {
        const auto low = static_cast<std::int64_t>(std::floor(bin));
        if (low <= -static_cast<std::int64_t>(fft_rows / 2) || low + 1 >= static_cast<std::int64_t>(fft_rows / 2))
            return 0.;
        const auto* row = power.data() + (bit * drift_segments + segment) * fft_rows;
        return std::lerp(row[fft_bin(low, fft_rows)], row[fft_bin(low + 1, fft_rows)], bin - low);
    };
    for (std::size_t bit = 0; plan.slow_ridge_segments && bit < 2; ++bit) {
        for (auto& peak : best[bit]) peak = {};
        for (const auto& seed : coarse_best[bit]) {
            if (!std::isfinite(seed.score)) continue;
            auto local = seed;
            auto consider_local = [&](std::int64_t slope, std::int64_t center) {
                if (std::abs(slope) > plan.slope_radius || std::abs(center) > bin_radius) return;
                double score = 0;
                for (std::size_t segment = 0; segment < drift_segments; ++segment)
                    score += interpolate_power(bit, segment, center + slope * plan.slope_step * centers[segment] / frequency_step);
                if (score > local.score) local = {score, slope, center};
            };
            local.score = -infinity;
            consider_local(seed.slope, seed.center);
            auto slope_step = std::max<std::int64_t>(1, slope_stride / 2);
            auto center_step = std::max<std::int64_t>(1, bin_stride / 2);
            for (;;) {
                const auto anchor = local;
                for (const auto ds : {-slope_step, std::int64_t{0}, slope_step})
                    for (const auto df : {-center_step, std::int64_t{0}, center_step})
                        consider_local(anchor.slope + ds, anchor.center + df);
                if (slope_step == 1 && center_step == 1) break;
                slope_step = std::max<std::int64_t>(1, slope_step / 2);
                center_step = std::max<std::int64_t>(1, center_step / 2);
            }
            auto& frontier = best[bit];
            if (std::ranges::any_of(frontier, [&](const auto& peak) {
                return std::isfinite(peak.score) && local.slope == peak.slope;
            })) continue;
            const auto where = std::ranges::find_if(frontier, [&](const auto& peak) { return local.score > peak.score; });
            if (where != frontier.end()) {
                std::move_backward(where, frontier.end() - 1, frontier.end());
                *where = local;
            }
        }
    }
    for (const auto& peak : {best[0][0], best[1][0]})
        if (std::isfinite(peak.score)) append(peak.slope);
    for (std::int64_t delta = 1; result.size() < slope_budget && delta <= slope_stride; ++delta) {
        const bool wide = plan.slow_ridge_segments && slope_stride > 16;
        const auto ranks = wide ? 1U : retained;
        const auto offset = wide ? delta * std::max<std::int64_t>(1, slope_stride / 12) : delta;
        for (std::size_t rank = 0; rank < ranks && result.size() < slope_budget; ++rank)
            for (const auto& peak : {best[0][rank], best[1][rank]})
                if (std::isfinite(peak.score)) {
                    append(peak.slope - offset);
                    append(peak.slope + offset);
                }
    }
    proposals.rank = std::max(best[0][0].score, best[1][0].score);
    return proposals;
}

void merge_drift_peak(DriftPeakFrontier& frontier, const DriftPeak& peak) {
    const auto where = std::ranges::find_if(frontier, [&](const DriftPeak& other) { return peak.rank > other.rank; });
    if (where == frontier.end()) return;
    std::move_backward(where, frontier.end() - 1, frontier.end());
    *where = peak;
}

namespace {
// Segment spectra of the normalized products and their FP64 power table
// [bit][segment][bin], each tap normalized by its template energy.
template<class T, class Fft>
void drift_ridge_tables(DriftScratch<T>& scratch, const DriftPlan& plan, const DriftRidgeGrid& grid, const Fft& fft,
                        double normalized, std::size_t taps, const std::array<std::array<double, 2>, 2>& energy,
                        std::stop_token stop) {
    const auto drift_segments = grid.segments;
    const auto fft_rows = grid.fft_rows;
    const auto& products = scratch.products;
    const auto columns = 2 * taps;
    scratch.segment_spectrum.resize(fft_rows);
    scratch.segment_power.assign(2 * drift_segments * fft_rows, 0.);
    for (std::size_t segment = 0; segment < drift_segments; ++segment) {
        const auto first = plan.rows * segment / drift_segments;
        const auto stop_row = plan.rows * (segment + 1) / drift_segments;
        for (std::size_t column = 0; column < columns; ++column) {
            auto& spectrum = scratch.segment_spectrum;
            std::fill(spectrum.begin(), spectrum.end(), std::complex<T>{});
            for (auto i = first; i < stop_row; ++i)
                spectrum[i - first] = static_cast<std::complex<T>>(products[column][i] * normalized);
            modem::detail::search_fft::fft(spectrum, false, fft, stop);
            const auto bit = column / taps;
            const auto tap = column % taps;
            if (energy[bit][tap] <= 0) continue;
            auto* power = scratch.segment_power.data() + (bit * drift_segments + segment) * fft_rows;
            for (std::size_t bin = 0; bin < fft_rows; ++bin)
                power[bin] += std::norm(static_cast<Complex>(spectrum[bin])) / energy[bit][tap];
        }
    }
}

// lap(phase) charges the time since the previous boundary to that phase.
template<class T, class Fft, class Lap>
DriftProposals drift_slope_proposals(
        DriftScratch<T>& scratch, const DriftPlan& plan, const Fft& fft, double fs, double normalized, std::size_t taps,
        const std::array<std::array<double, 2>, 2>& energy, std::size_t drift_segments,
        std::size_t slope_budget, std::stop_token stop, const Lap& lap) {
    if (drift_ridge_exhaustive(plan, slope_budget)) {
        auto proposals = DriftRidgeFrontier{}.select(plan, {}, {}, {}, slope_budget);
        lap(drift_ridge_search);
        return proposals;
    }
    auto& centers = scratch.segment_centers;
    centers.resize(drift_segments);
    const auto grid = drift_ridge_grid(plan, fs, drift_segments, centers);
    drift_ridge_tables(scratch, plan, grid, fft, normalized, taps, energy, stop);
    lap(drift_ridge);
    // The block sums reuse the lane's render tile (charged in the lane quote
    // as 4 * render rows complex words), addressed as its real and imaginary
    // doubles. An evaluation reads the tile only right after rendering into
    // it, so it is idle here. Its 2 * taps * tile complex words, tile =
    // min(16,384, raw rows), hold one slope's 2 * centers sums at every
    // admitted grid: centers <= min(1,025, fft_rows - 1), and with at least
    // two segments fft_rows <= 2 * rows <= 2 * raw rows. Only an aperture
    // under 16,384 raw rows can hold fewer slopes than the cache rule.
    const auto sums = std::span(reinterpret_cast<double*>(scratch.raw.data()), 2 * scratch.raw.size());
    const auto block = std::min(drift_ridge_block(grid), sums.size() / (2 * grid.centers));
    if (!block) throw Error("Robust drift ridge block exceeds the lane's render tile");
    const auto frontier = drift_ridge_frontier(plan, grid, scratch.segment_power, centers, block, sums, stop);
    auto proposals = frontier.select(plan, grid, scratch.segment_power, centers, slope_budget);
    lap(drift_ridge_search);
    return proposals;
}

// The public geometry of one device ridge call, built once per drift stage
// for both relative-Doppler signs and every lane, and only once a session
// holds it: the grid, segment centers and row bounds, the native slope
// order and the host-rounded offsets [slope][segment].
struct DriftDeviceRidge {
    DriftRidgeGrid grid;
    std::vector<double> centers;
    std::vector<std::size_t> bounds;
    std::vector<std::int64_t> slopes, offsets;
    DriftRidgeGeometry request(bool return_power) const {
        return {grid.segments, grid.fft_rows, bounds, slopes, offsets, grid.bin_radius, grid.bin_stride, return_power};
    }
    // Every table above at a grid's shape.
    static std::size_t host_bytes(const DriftRidgeGrid& grid) {
        const auto segments = grid.segments, slopes_count = grid.slopes;
        return checked_multiply(checked_add(checked_add(2 * segments + 1, slopes_count),
                                            checked_multiply(slopes_count, segments)), sizeof(std::int64_t));
    }
    DriftDeviceRidge(const DriftPlan& plan, double rate, const DriftRidgeGrid& shape) : centers(shape.segments) {
        const auto segments = shape.segments;
        grid = drift_ridge_grid(plan, rate, segments, centers);
        bounds.resize(segments + 1);
        for (std::size_t segment = 0; segment <= segments; ++segment) bounds[segment] = plan.rows * segment / segments;
        slopes.resize(grid.slopes);
        offsets.resize(checked_multiply(grid.slopes, segments));
        for (std::size_t index = 0; index < grid.slopes; ++index) {
            slopes[index] = drift_ridge_slope(plan, grid, index);
            for (std::size_t segment = 0; segment < segments; ++segment)
                offsets[index * segments + segment] = drift_ridge_offset(plan, grid, centers[segment], slopes[index]);
        }
    }
};
// A device stage's ridges, absent where the call's slope budget is
// exhaustive or the plan has no slow segments, with its counts over both
// signs: evaluations with device-published proposals, and those with a
// native fallback (a mixed evaluation counts in both). A stage without a
// session holds no ridge tables.
struct DriftDeviceStage {
    DriftBulk* session = nullptr;
    ReceiverBulkBackend* backend = nullptr;
    std::optional<DriftDeviceRidge> fast, slow;
    std::size_t device_evaluations = 0, native_evaluations = 0;
};

// A device ridge's grid shape, computed without its tables.
std::optional<DriftRidgeGrid> drift_device_grid(const DriftPlan& plan, double rate, std::size_t segments,
                                                std::size_t slope_budget) {
    if (!segments || drift_ridge_exhaustive(plan, slope_budget)) return std::nullopt;
    return drift_ridge_grid_shape(plan, rate, segments);
}

// Opens the one device proposal session of a drift stage, or records why
// the stage stays native. The policy's own declines are the factory's
// record. The route's host tables and lane records, and the session's host
// bytes (its compilation and a unified-memory lease), must fit in the stage
// envelope above the plan's native quote, which stays held for per-atom
// fallbacks; the transform batch halves to fit.
// The policy, envelope and lease read only grid shapes, so a declined or
// native stage allocates nothing here; the tables exist only with a session.
std::unique_ptr<DriftBulk> open_drift_device(const ReceiverConfig& config, const DriftPlan& plan,
        std::span<const FineCoordinate> selected, std::span<const FineCoordinate> epoch_timing,
        DriftDeviceStage& stage, std::stop_token stop) {
    auto* backend = config.bulk_backend.get();
    if (!backend || !plan.workspace_bytes || !plan.stage_workers) return {};
    const bool fp64 = config.native_coarse_precision == NativeCoarsePrecision::fp64;
    const auto rate = config.sample_rate_hz / plan.sample_stride;
    const auto fast_budget = drift_fast_slope_budget(plan);
    const auto fast = drift_device_grid(plan, rate, plan.ridge_segments, fast_budget);
    const auto slow = drift_device_grid(plan, rate, plan.slow_ridge_segments, plan.slope_budget - fast_budget + 1);
    // A transient list, freed before the stage's own (its metadata term).
    const auto lanes = std::min<std::size_t>(plan.stage_workers, drift_timing_atoms(selected, epoch_timing).size());
    const bool echo = config.spread_delay_seconds || config.spread_doppler_hz;
    DriftBulkShape shape;
    shape.rows = plan.rows; shape.taps = echo ? 2 : 1; shape.nfft = plan.nfft;
    shape.frequency_radius = static_cast<std::size_t>(plan.frequency_radius);
    std::size_t host_bytes = 0, lane_bytes = 0;
    if (fast) { shape.segments = fast->segments; shape.segment_fft = fast->fft_rows; }
    if (slow) { shape.slow_segments = slow->segments; shape.slow_segment_fft = slow->fft_rows; }
    for (const auto* grid : {&fast, &slow}) {
        if (!*grid) continue;
        shape.ridge_slopes = std::max(shape.ridge_slopes, (*grid)->slopes);
        shape.ridge_centers = std::max(shape.ridge_centers, (*grid)->centers);
        host_bytes = checked_add(host_bytes, DriftDeviceRidge::host_bytes(**grid));
    }
    shape.slope_budget = shape.slope_batch = plan.slope_budget;
    shape.lanes = lanes;
    lane_bytes = checked_add(checked_multiply(shape.ridge_slopes, sizeof(DriftRidgeRecord)),
        checked_multiply(plan.slope_budget, 2 * retained_drift_per_bit * sizeof(DriftFrontierRow)));
    host_bytes = checked_add(host_bytes, checked_multiply(lanes, lane_bytes));
    std::unique_ptr<DriftBulk> session;
    if (!backend->stage_decline(shape, fp64).empty()) {
        session = backend->make_drift(shape, fp64, stop);
    } else {
        const auto slack = plan.device_envelope_bytes - std::min(plan.device_envelope_bytes, plan.workspace_bytes);
        if (host_bytes > slack) {
            backend->record_stage_native(StageFamily::drift, StageReason::workspace, StageNative::fallback);
            return {};
        }
        for (;; shape.slope_batch /= 2) {
            if (!shape.slope_batch) {
                backend->record_stage_native(StageFamily::drift, backend->receipt().unified_memory ?
                    StageReason::unified_memory_envelope : StageReason::workspace, StageNative::fallback);
                return {};
            }
            if (backend->stage_host_bytes(shape, fp64) <= slack - host_bytes) break;
        }
        session = backend->make_drift(shape, fp64, stop);
    }
    if (!session) return {};
    stage.backend = backend;
    if (fast) stage.fast.emplace(plan, rate, *fast);
    if (slow) stage.slow.emplace(plan, rate, *slow);
    stage.session = session.get();
    return session;
}
} // namespace

DriftPlan drift_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                     bool project_line_atom, std::size_t preserved_workspace_bytes,
                     std::size_t ordinary_headroom_bytes, const DriftPlan* public_lattice) {
    if (!config.time_varying_channel || !(config.time_varying_common_doppler_hz > 0)) return {};
    const auto fs = config.sample_rate_hz;
    const auto source_geometry = geometry(slot.format);
    const double duration = source_geometry.source_samples / effective_sample_rate(slot.format);
    const double clock = config.clock_bound_ppm * 1e-6;
    const auto first = slot.window_capture_lead_seconds + slot.arrival_lower_seconds +
                       slot.source_start_seconds / (1 + clock);
    const auto stop = slot.window_capture_lead_seconds + slot.arrival_upper_seconds +
        (slot.source_start_seconds + duration) / (1 - clock) + config.spread_delay_seconds;
    const auto full_rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
    const auto start_row = static_cast<std::size_t>(std::clamp(std::floor(first * fs), 0.L,
                                                              static_cast<long double>(full_rows)));
    const auto stop_row = static_cast<std::size_t>(std::clamp(std::ceil(stop * fs), 0.L,
                                                             static_cast<long double>(full_rows)));
    if (stop_row <= start_row || stop_row - start_row < minimum_drift_segments)
        return {.status = "drift_public_aperture_too_short"};
    DriftPlan result;
    result.first = start_row;
    result.raw_rows = stop_row - start_row;
    if (public_lattice && (!public_lattice->workspace_bytes || !public_lattice->stage_workers ||
                          !public_lattice->sample_stride || !preserved_workspace_bytes))
        return {.status = "protected_drift_public_lattice_unavailable"};
    result.sample_stride = public_lattice ? public_lattice->sample_stride :
        1 + (result.raw_rows - 1) / (preferred_drift_fft_rows / 2);
    bool extended = public_lattice != nullptr;
    const auto center_bound = config.frequency_bound_hz + config.time_varying_atmospheric_hz;
    const auto edge_bound = center_bound + config.time_varying_common_doppler_hz *
        ((stop - first) / duration);
    // A box sum is a proposal accelerator only. At the public residual
    // edge its coherent gain stays above sinc(pi/4) even in the wide
    // two-scale bank. Exact likelihoods still use every raw sample.
    const auto wide_frequency = wide_varying_frequency_support(config, duration);
    if (!public_lattice && result.sample_stride > 1) {
        const auto largest_stride = std::min<double>(result.raw_rows / 2048,
            fs / ((wide_frequency ? 4 : 8) * edge_bound));
        if (largest_stride < result.sample_stride) {
            if (!preserved_workspace_bytes)
                return {.status = "drift_public_bandwidth_exceeds_reduced_proposal_limit"};
            extended = true;
        }
        // With no reduction the proposal uses the exact original samples;
        // the box-sum attenuation bound imposes no additional restriction.
        result.sample_stride = static_cast<std::size_t>(std::max(1., largest_stride));
    }
    result.rows = 1 + (result.raw_rows - 1) / result.sample_stride;
    const auto proposal_rate = fs / result.sample_stride;
    result.project_data = config.helper_enabled || project_line_atom;
    const auto render_rows = std::min(maximum_basis_rows, result.raw_rows);
    std::size_t projection_rows = 0, protected_projection_workspace = 0;
    if (result.project_data) {
        result.projection_blocks = result.sample_stride == 1 && !project_line_atom ?
            1 : 1 + (result.raw_rows - 1) / std::min(render_rows, round_rows(.25, fs));
        const auto min_rows = result.raw_rows / result.projection_blocks;
        projection_rows = min_rows + (result.raw_rows % result.projection_blocks != 0);
        try {
            const ProjectionBands bands(config, slot, ProjectionBands::Policy::separable_varying);
            if (!projected_block_supported(min_rows, bands) || !projected_block_supported(projection_rows, bands))
                return {.status = "drift_data_and_helper_bands_not_separable"};
            if (public_lattice) {
                // When the larger block is a power of two, only the
                // smaller block may need Bluestein's longer scratch.
                result.projection_scratch_rows = std::max(DftPlan::scratch_samples(min_rows),
                    DftPlan::scratch_samples(projection_rows));
                protected_projection_workspace = DftPlan::workspace_bytes(min_rows);
                if (projection_rows != min_rows)
                    protected_projection_workspace = checked_add(protected_projection_workspace,
                        DftPlan::workspace_bytes(projection_rows));
            }
        } catch (const Error&) {
            return {.status = "drift_data_and_helper_bands_not_separable"};
        }
    }
    constexpr auto largest_fft = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
    if (result.rows > largest_fft / 2)
        return {.status = "drift_proposal_transform_shape_overflow"};
    if (public_lattice) {
        const auto& original = *public_lattice;
        if (result.rows < minimum_drift_segments || !original.raw_rows || !original.rows ||
            !std::has_single_bit(original.nfft) || original.nfft < 2 * result.rows ||
            original.frequency_step != proposal_rate / original.nfft || original.frequency_radius < 0 ||
            original.frequency_radius >= static_cast<std::int64_t>(original.nfft / 2) ||
            original.slope_radius < 1 || original.slope_radius >= (1LL << 40) ||
            !std::isfinite(original.slope_step) || original.slope_step <= 0 ||
            !std::isfinite(original.slope_reference_seconds) || original.slope_budget < 3 || original.slope_budget > 64 ||
            original.ridge_segments < minimum_drift_segments || original.ridge_segments > original.rows ||
            (original.slow_ridge_segments && (original.slow_ridge_segments < minimum_drift_segments ||
                                             original.slow_ridge_segments >= original.ridge_segments)))
            return {.status = "protected_drift_public_lattice_unavailable"};
        result.nfft = original.nfft;
        result.frequency_step = original.frequency_step;
        result.frequency_radius = original.frequency_radius;
        result.slope_step = original.slope_step;
        result.slope_radius = original.slope_radius;
        result.slope_budget = original.slope_budget;
        result.slope_reference_seconds = original.slope_reference_seconds;
        // Keep approximately the original proposal segment duration. The
        // exact candidate FFT still uses the full original lattice, with
        // only the cropped products and its usual transform zero padding.
        const auto cropped_segments = [&](std::size_t count) {
            const auto scaled = std::ceil(static_cast<long double>(count) *
                std::min(result.raw_rows, original.raw_rows) / original.raw_rows);
            return std::min(result.rows, std::max(minimum_drift_segments, static_cast<std::size_t>(scaled)));
        };
        result.ridge_segments = cropped_segments(original.ridge_segments);
        if (original.slow_ridge_segments) {
            result.slow_ridge_segments = cropped_segments(original.slow_ridge_segments);
            result.ridge_segments = std::min(result.rows,
                std::max(result.ridge_segments, result.slow_ridge_segments + 1));
        }
    } else {
        result.nfft = std::bit_ceil(2 * result.rows);
        result.frequency_step = proposal_rate / result.nfft;
        result.frequency_radius = static_cast<std::int64_t>(std::ceil(std::min(fs * .499,
            config.frequency_bound_hz + config.time_varying_atmospheric_hz) / result.frequency_step));
        result.frequency_radius = std::min(result.frequency_radius, static_cast<std::int64_t>(result.nfft / 2 - 1));
        const auto public_radius = std::ceil(2 * config.time_varying_common_doppler_hz * duration);
        if (!std::isfinite(public_radius) || public_radius >= (1ULL << 40))
            return {.status = "drift_public_slope_bound_overflow"};
        result.slope_radius = static_cast<std::int64_t>(public_radius);
        result.slope_budget = std::min<std::size_t>(64,
            std::max<std::size_t>(3, target_drift_fft_rows_per_atom / result.nfft));
        // For a centered quadratic, half-step mismatch at a waveform edge
        // is at most pi/8 radians at this nominal slope spacing.
        result.slope_step = 1 / (duration * duration);
        result.slope_reference_seconds = first + (stop - first) * .5L;
        // Keep within-segment quadratic phase departure bounded as the
        // public aperture grows. A single fixed eight-block transform loses
        // coherence on long windows even for moderate edge frequencies.
        const auto segments = [&](double radius) {
            return std::max(minimum_drift_segments, static_cast<std::size_t>(std::min<double>(result.rows,
                std::ceil(std::sqrt(radius * result.slope_step) * (stop - first)))));
        };
        result.ridge_segments = segments(result.slope_radius);
        if (result.slope_budget >= 8 && wide_frequency) {
            const auto slow_segments = segments(result.slope_radius / 8.);
            if (slow_segments < result.ridge_segments) result.slow_ridge_segments = slow_segments;
        }
    }
    result.status = 2 * result.slope_radius + 1 > static_cast<std::int64_t>(result.slope_budget) ?
        "linear_drift_ridge_proposals" : "linear_drift";
    const auto quote = [&](std::size_t active) {
        const auto tile = std::max(render_rows, projection_rows);
        const auto segment_extent = [&](std::size_t count) {
            return count ? std::bit_ceil(2 * (1 + (result.rows - 1) / count)) : std::size_t{0};
        };
        const auto fast_fft = segment_extent(result.ridge_segments);
        const auto slow_fft = segment_extent(result.slow_ridge_segments);
        const auto segment_fft = std::max(fast_fft, slow_fft);
        const auto segment_power_rows = std::max(checked_multiply(result.ridge_segments, fast_fft),
            checked_multiply(result.slow_ridge_segments, slow_fft));
        const auto fft_value_bytes = config.native_coarse_precision == NativeCoarsePrecision::fp64 ?
            sizeof(Complex) : sizeof(std::complex<float>);
        auto bytes = checked_multiply(checked_add(result.nfft, checked_add(fast_fft, slow_fft)), fft_value_bytes);
        const auto per_lane = checked_add(
            checked_multiply(checked_multiply(4, checked_add(result.rows, checked_add(tile, render_rows))), sizeof(Complex)),
            checked_add(checked_multiply(checked_add(checked_multiply(4, result.nfft), segment_fft), fft_value_bytes),
                        checked_multiply(checked_add(tile, checked_multiply(2, segment_power_rows)), sizeof(double))));
        // Each segment's center and one spare word: the native ridge rounds
        // its block's segment offsets on the stack, with no offset table.
        const auto ridge_metadata = checked_multiply(result.ridge_segments, sizeof(double) + sizeof(std::int64_t));
        bytes = checked_add(bytes, checked_multiply(active, checked_add(per_lane, ridge_metadata)));
        bytes = checked_add(bytes, MappedBasis::workspace_bytes(config, slot, render_rows, static_cast<unsigned>(active)));
        if (result.project_data) {
            const auto scratch = public_lattice ? result.projection_scratch_rows : DftPlan::scratch_samples(projection_rows);
            bytes = checked_add(bytes, public_lattice ? protected_projection_workspace :
                checked_multiply(2, DftPlan::workspace_bytes(projection_rows)));
            bytes = checked_add(bytes, checked_multiply(checked_multiply(2, projection_rows), sizeof(std::size_t)));
            const auto observation = checked_add(checked_multiply(2, result.raw_rows), scratch);
            bytes = checked_add(bytes, checked_multiply(observation, sizeof(Complex)));
            if (project_line_atom) bytes = checked_add(bytes, checked_multiply(result.raw_rows, sizeof(Complex)));
            // Each admitted lane projects two template columns, reused for
            // each retained bit/hypothesis. No candidate-wide spectra bank.
            bytes = checked_add(bytes, checked_multiply(active,
                checked_multiply(checked_add(checked_multiply(2, projection_rows), scratch), sizeof(Complex))));
        }
        if (config.spread_doppler_hz != 0) bytes = checked_add(bytes, sizeof(ReceiverSlotScore));
        return checked_add(bytes, drift_metadata_bytes(active, result.slope_budget));
    };
    const auto widest = static_cast<unsigned>(std::min<std::size_t>({workers, maximum_drift_stage_workers,
        public_lattice ? public_lattice->stage_workers : maximum_drift_stage_workers}));
    const auto admit = [&](unsigned active, std::size_t bytes) {
        result.workspace_bytes = bytes;
        result.stage_workers = active;
        const auto envelope = public_lattice ? std::min(public_lattice->device_envelope_bytes, preserved_workspace_bytes) :
            (extended ? preserved_workspace_bytes : ordinary_headroom_bytes);
        result.device_envelope_bytes = std::max(bytes, envelope);
        return result;
    };
    if (!extended && widest) {
        // The former width is always admitted and its quote is the floor. A
        // wider quote between that floor and the headroom leaves the largest
        // stage, hence every owner lease and admitted shape, unchanged.
        const auto floor = static_cast<unsigned>(std::min<std::size_t>(workers, ordinary_drift_stage_workers));
        const auto floor_bytes = quote(floor);
        for (auto active = widest; active > floor && ordinary_headroom_bytes > floor_bytes; --active) {
            try {
                const auto bytes = quote(active);
                if (bytes >= floor_bytes && bytes <= ordinary_headroom_bytes) return admit(active, bytes);
            } catch (const Error& error) {
                if (std::string_view(error.what()) == "Robust reception cancelled") throw;
            }
        }
        return admit(floor, floor_bytes);
    }
    bool unsupported = false;
    for (auto active = widest; active; --active) {
        try {
            const auto bytes = quote(active);
            if (bytes <= preserved_workspace_bytes) {
                result.status = public_lattice ? "protected_drift_public_lattice" :
                    "linear_drift_ridge_proposals_within_preserved_owner_lease";
                return admit(active, bytes);
            }
        } catch (const Error& error) {
            if (std::string_view(error.what()) == "Robust reception cancelled") throw;
            unsupported = true;
        }
    }
    if (public_lattice) return {.status = unsupported ? "protected_drift_workspace_shape_unavailable" :
                                                      "protected_drift_exceeds_preserved_owner_lease"};
    return {.status = unsupported ? "drift_extension_workspace_shape_unavailable" :
                                   "drift_extension_exceeds_preserved_owner_lease"};
}

namespace {
template<class T, class Fft>
void score_drift_impl(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                       const DriftPlan& plan, std::span<const Complex> iq,
                       std::span<const FineCoordinate> selected, std::span<const FineCoordinate> epoch_timing,
                       unsigned workers,
                       ReceiverSlotScore& score, std::stop_token stop, ReceiverExecutor& executor, const LineAtom* line,
                       DriftZeroSlopeEvaluation zero_evaluation, DriftDeviceStage* device) {
    if (!plan.workspace_bytes || !plan.stage_workers || plan.stage_workers > workers ||
        selected.empty() || selected.size() > maximum_varying_coordinates ||
        epoch_timing.size() > 2 * receiver_epoch_offsets.size() + maximum_segmented_timing_coordinates ||
        iq.size() != slot.stop_sample - slot.first_sample)
        throw Error("Robust drift scorer requires bounded public timing atoms and observation");
    const auto began = FineClock::now();
    const auto timing = drift_timing_atoms(selected, epoch_timing);
    const auto active = std::min<std::size_t>(plan.stage_workers, timing.size());
    const bool rank_timing = timing.size() > retained_drift_ridge_timing_atoms;
    const bool reuse_timing = rank_timing && zero_evaluation == DriftZeroSlopeEvaluation::reuse;
    std::vector<DriftTimingCache> timing_cache(rank_timing ? timing.size() : 0);
    // One bounded list per timing atom, local to this relative-Doppler sign.
    // Leave device evaluations to their existing per-call fallback policy.
    std::vector<std::int64_t> ranked_slopes(reuse_timing && !device ?
        checked_multiply(timing.size(), plan.slope_budget) : 0);
    const auto tile = std::min(maximum_basis_rows, plan.raw_rows);
    const auto count = static_cast<std::size_t>(2 * plan.frequency_radius + 1);
    const auto public_slopes = 2 * plan.slope_radius + 1;
    const std::array<Tap, 2> paths{{{0, 0}, {config.spread_delay_seconds, config.spread_doppler_hz}}};
    const auto taps = (paths[1].delay_seconds || paths[1].doppler_hz) ? 2U : 1U;
    const double variance = config.noise_variance;
    const auto input = iq.subspan(plan.first, plan.raw_rows);
    std::optional<DriftProjection> projection;
    if (plan.project_data) projection.emplace(config, slot, plan, input, line, stop);
    const auto ranking_observation = projection ? std::span<const Complex>(projection->ranking) : input;
    MappedBasis basis(config, slot, key, tile, static_cast<unsigned>(active));
    const Fft fft(plan.nfft, stop);
    const auto segment_rows = std::bit_ceil(2 * ((plan.rows + plan.ridge_segments - 1) / plan.ridge_segments));
    const Fft segment_fft(segment_rows, stop);
    std::optional<Fft> slow_fft;
    if (plan.slow_ridge_segments)
        slow_fft.emplace(std::bit_ceil(2 * ((plan.rows + plan.slow_ridge_segments - 1) / plan.slow_ridge_segments)), stop);
    std::vector<DriftScratch<T>> scratch(active);
    std::vector<std::array<double, 2>> evidence(timing.size(), {-infinity, -infinity});
    std::vector<std::size_t> scored_slopes(timing.size());
    std::vector<double> ranks(timing.size());
    std::vector<std::uint64_t> retained_digests(timing.size(), drift_digest_basis);
    std::vector<std::size_t> survivors(timing.size());
    std::iota(survivors.begin(), survivors.end(), 0);
    if (device && device->session && device->session->staging(0).size() != 2 * taps * plan.rows)
        throw Error("Robust drift device staging differs from its public shape");
    // A device lane reserves its tables once, at their quoted sizes: every
    // ridge slope's record, the slope budget's frontiers and, with a slow
    // refinement, the larger of the fast and slow power tables. Growing them
    // call by call could keep up to twice the larger table.
    if (device && device->session) {
        std::size_t slopes = 0, power = 0;
        for (const auto* ridge : {&device->fast, &device->slow})
            if (*ridge) {
                slopes = std::max(slopes, (*ridge)->grid.slopes);
                power = std::max(power, checked_multiply(2 * (*ridge)->grid.segments, (*ridge)->grid.fft_rows));
            }
        for (auto& own : scratch) {
            own.device_records.reserve(slopes);
            own.device_frontiers.reserve(checked_multiply(plan.slope_budget, 2 * retained_drift_per_bit));
            if (plan.slow_ridge_segments) own.segment_power.reserve(power);
        }
    }
    const auto evaluate = [&](unsigned lane, std::size_t index, bool rank_only, std::stop_token leaf_stop) {
            auto& own = scratch[lane];
            auto mark = FineClock::now();
            const auto lap = [&](DriftPhase phase) {
                const auto now = FineClock::now();
                own.seconds[phase] += std::chrono::duration<double>(now - mark).count();
                mark = now;
            };
            for (auto& product : std::span(own.products).first(2 * taps)) product.assign(plan.rows, {});
            own.raw.resize(2 * taps * tile);
            own.offsets.resize(tile);
            std::iota(own.offsets.begin(), own.offsets.end(), 0.);
            const auto& coordinate = timing[index];
            const double scale = 1 + coordinate.clock_ppm * 1e-6;
            const auto start = slot.window_capture_lead_seconds + coordinate.arrival_seconds +
                               slot.source_start_seconds / scale;
            const MappedBasisRow row{coordinate.epoch_offset_seconds, static_cast<double>(start),
                                     -config.data_if_hz * (scale - 1), coordinate.clock_ppm};
            std::array<std::array<double, 2>, 2> energy{};
            std::array<Complex, 2> cross{};
            for (std::size_t first = 0; first < plan.raw_rows;) {
                const auto rows = std::min(tile, plan.raw_rows - first);
                auto rendered = std::span(own.raw).first(2 * taps * rows);
                basis.render_pair(row, std::span(paths).first(taps), plan.first + first,
                                  std::span(own.offsets).first(rows), rendered, leaf_stop);
                for (std::size_t i = 0; i < rows; ++i) {
                    const auto observation = ranking_observation[first + i];
                    for (std::size_t bit = 0; bit < 2; ++bit) {
                        const auto offset = bit * taps;
                        const auto direct = rendered[offset * rows + i];
                        energy[bit][0] += std::norm(direct);
                        const auto store = [&](std::size_t path, Complex value) {
                            own.products[offset + path][(first + i) / plan.sample_stride] +=
                                observation * std::conj(value);
                        };
                        store(0, direct);
                        if (taps == 2) {
                            const auto echo = rendered[(offset + 1) * rows + i];
                            energy[bit][1] += std::norm(echo);
                            cross[bit] += std::conj(direct) * echo;
                            store(1, echo);
                        }
                    }
                }
                first += rows;
            }
            double maximum_product = 0;
            for (const auto& product : std::span(own.products).first(2 * taps))
                for (const auto value : product)
                    maximum_product = std::max({maximum_product, std::abs(value.real()), std::abs(value.imag())});
            const std::array<double, 3> precision{static_cast<double>(taps), 16. * taps, 256. * taps};
            const auto exponent = maximum_product ? std::clamp(std::ilogb(maximum_product), -1022, 1023) : 0;
            const auto normalized = std::scalbn(1., -exponent);
            const auto inverse_variance = 1 / variance;
            const auto projected_rate = config.sample_rate_hz / plan.sample_stride;
            const double delta = plan.sample_stride / config.sample_rate_hz;
            const double first_t = (plan.first + (plan.sample_stride - 1) * .5) /
                config.sample_rate_hz - plan.slope_reference_seconds;
            // A device session scores this evaluation's proposals from the
            // lane's staged products round32(P * normalized), the native ridge
            // transform input. A product outside the FP32 range, or a device
            // call that returns false, leaves the rest of this evaluation to
            // the native proposals; every exact statistic stays native FP64.
            // published: a device call published ridge records or slope
            // frontiers for this evaluation.
            bool on_device = device && device->session, published = false;
            if (on_device) {
                const auto staged = device->session->staging(lane);
                constexpr double largest = std::numeric_limits<float>::max();
                for (std::size_t column = 0; column < 2 * taps; ++column)
                    for (std::size_t i = 0; i < plan.rows; ++i) {
                        if (!(i & 65535U)) check_cancelled(leaf_stop);
                        const auto value = own.products[column][i] * normalized;
                        on_device = on_device && std::abs(value.real()) <= largest && std::abs(value.imag()) <= largest;
                        staged[column * plan.rows + i] = static_cast<std::complex<float>>(value);
                    }
                if (!on_device)
                    device->backend->record_stage_native(StageFamily::drift, StageReason::nonfinite_input,
                                                         StageNative::session_fallback);
            }
            lap(drift_render);
            // Dechirp one slope's normalized products, then transform each column.
            const auto dechirp_fft = [&](std::int64_t slope) {
                check_cancelled(leaf_stop);
                own.frequency.resize(4 * plan.nfft);
                const double rate = slope * plan.slope_step;
                const auto curve = std::polar(1., -std::numbers::pi * rate * delta * delta * 2);
                Complex phase, step;
                for (std::size_t i = 0; i < plan.rows; ++i) {
                    if (!(i % 256)) {
                        check_cancelled(leaf_stop);
                        const double t = first_t + i * delta;
                        phase = std::polar(1., -std::numbers::pi * rate * t * t);
                        step = std::polar(1., -std::numbers::pi * rate * delta * (2 * t + delta));
                    }
                    const auto correction = phase * normalized;
                    for (std::size_t column = 0; column < 2 * taps; ++column)
                        own.frequency[column * plan.nfft + i] =
                            static_cast<std::complex<T>>(own.products[column][i] * correction);
                    phase *= step;
                    step *= curve;
                }
                for (std::size_t column = 0; column < 2 * taps; ++column) {
                    auto values = std::span(own.frequency).subspan(column * plan.nfft, plan.nfft);
                    std::fill(values.begin() + static_cast<std::ptrdiff_t>(plan.rows), values.end(), std::complex<T>{});
                    modem::detail::search_fft::fft(values, false, fft, leaf_stop);
                }
            };
            // Merge every public bin of the transformed slope into the
            // atom's persistent frontiers, and optionally its nominations.
            const auto frontier_merge = [&](std::int64_t slope,
                    std::array<std::array<DriftPeak, retained_drift_per_bit>, 2>& peaks,
                    std::array<double, 2>* nomination) {
                for (auto bin = -plan.frequency_radius; bin <= plan.frequency_radius; ++bin) {
                    if (!(static_cast<std::uint64_t>(bin) & 4095U)) check_cancelled(leaf_stop);
                    const auto offset = fft_bin(bin, plan.nfft);
                    for (std::size_t bit = 0; bit < 2; ++bit) {
                        const auto column = bit * taps;
                        const auto r0 = static_cast<Complex>(own.frequency[column * plan.nfft + offset]);
                        const auto r1 = taps == 2 ?
                            static_cast<Complex>(own.frequency[(column + 1) * plan.nfft + offset]) : Complex{};
                        const auto rank = std::norm(r0) + std::norm(r1);
                        if (nomination) {
                            double normalized_power = 0;
                            if (energy[bit][0] > 0) normalized_power += std::norm(r0) / energy[bit][0];
                            if (taps == 2 && energy[bit][1] > 0) normalized_power += std::norm(r1) / energy[bit][1];
                            (*nomination)[bit] = std::max((*nomination)[bit], normalized_power);
                        }
                        merge_drift_peak(peaks[bit], {rank, slope, bin});
                    }
                }
            };
            const auto score_slope = [&](std::int64_t slope,
                    std::array<std::array<DriftPeak, retained_drift_per_bit>, 2>& peaks,
                    std::array<double, 2>* nomination) {
                dechirp_fft(slope);
                frontier_merge(slope, peaks, nomination);
            };
            // The device ridge supplies the per-slope records in native slope
            // order; the frontier, refinement and slope list stay shared.
            const auto ridge_proposals = [&](const Fft& transform, const DriftDeviceRidge* ridge,
                                             std::size_t segments, std::size_t budget, bool slow) {
                if (on_device && ridge) {
                    const auto& grid = ridge->grid;
                    const bool refine = plan.slow_ridge_segments != 0;
                    std::span<double> power;
                    if (refine) {
                        own.segment_power.resize(2 * grid.segments * grid.fft_rows);
                        power = own.segment_power;
                    }
                    own.device_records.resize(grid.slopes);
                    DriftAtomRequest request;
                    request.lane = lane; request.energy = energy; request.normalized = normalized;
                    (slow ? request.slow : request.fast) = ridge->request(refine);
                    DriftAtomResult result;
                    (slow ? result.slow : result.fast) = own.device_records;
                    (slow ? result.slow_power : result.fast_power) = power;
                    StageDeviceWork work;
                    if (device->session->ridge(request, result, work, leaf_stop)) {
                        published = true;
                        lap(drift_ridge);
                        DriftRidgeFrontier frontier;
                        for (std::size_t index = 0; index < grid.slopes; ++index)
                            frontier.insert(ridge->slopes[index], own.device_records[index]);
                        auto proposals = frontier.select(plan, grid, power, ridge->centers, budget);
                        lap(drift_ridge_search);
                        return proposals;
                    }
                    on_device = false;
                }
                return drift_slope_proposals(own, plan, transform, projected_rate, normalized, taps,
                                             energy, segments, budget, leaf_stop, lap);
            };
            // Each requested slope's top eight per bit, merged in request
            // order as the native scan inserts them.
            const auto device_slopes = [&](std::span<const std::int64_t> list,
                    std::array<std::array<DriftPeak, retained_drift_per_bit>, 2>& peaks,
                    std::array<double, 2>* nomination) {
                if (list.empty()) return true;
                own.device_frontiers.resize(list.size() * 2 * retained_drift_per_bit);
                DriftAtomRequest request;
                request.lane = lane; request.energy = energy; request.normalized = normalized;
                request.slopes = list; request.rate_step = plan.slope_step;
                request.delta = delta; request.first_t = first_t; request.nominate = nomination != nullptr;
                DriftAtomResult result;
                result.frontiers = own.device_frontiers;
                StageDeviceWork work;
                if (!device->session->slopes(request, result, work, leaf_stop)) return on_device = false;
                published = true;
                for (std::size_t index = 0; index < list.size(); ++index)
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        for (std::size_t rank = 0; rank < retained_drift_per_bit; ++rank) {
                            const auto& row = own.device_frontiers[(2 * index + bit) * retained_drift_per_bit + rank];
                            merge_drift_peak(peaks[bit], {row.rank, list[index], row.bin});
                        }
                if (nomination)
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        (*nomination)[bit] = std::max((*nomination)[bit], result.nomination[bit]);
                return true;
            };
            // A device atom: a device call published this evaluation's ridge
            // records or slope frontiers, or it needed no device call. A
            // native atom: any of its steps fell back. An evaluation whose
            // device ridge published before a later call fell back is both.
            const auto count_evaluation = [&] {
                if (!device || !device->session) return;
                if (published || on_device) ++own.device_evaluations;
                if (!on_device) ++own.native_evaluations;
            };
            // Use the same bounded exact slope budget. A small fast
            // frontier remains even where the slower ridge is more
            // sensitive. No data-selected slope gets extra prior mass.
            const auto fast_budget = drift_fast_slope_budget(plan);
            const auto device_ridge = [&](const std::optional<DriftDeviceRidge>& ridge) {
                return ridge ? &*ridge : nullptr;
            };
            const auto find_slopes = [&] {
                auto proposals = ridge_proposals(segment_fft, device ? device_ridge(device->fast) : nullptr,
                                                 plan.ridge_segments, fast_budget, false);
                if (slow_fft) {
                    const auto slow = ridge_proposals(*slow_fft, device ? device_ridge(device->slow) : nullptr,
                        plan.slow_ridge_segments, plan.slope_budget - fast_budget + 1, true);
                    proposals.rank = std::max(proposals.rank, slow.rank);
                    for (const auto slope : slow.slopes)
                        if (std::ranges::find(proposals.slopes, slope) == proposals.slopes.end())
                            proposals.slopes.push_back(slope);
                    lap(drift_ridge_search);
                }
                return proposals;
            };
            const auto* cached = reuse_timing && timing_cache[index].ready && timing_cache[index].exponent == exponent ?
                &timing_cache[index] : nullptr;
            const bool reuse_slopes = cached && cached->slope_count;
            const auto proposals = reuse_slopes ? DriftProposals{} : find_slopes();
            const auto slope_offset = index * plan.slope_budget;
            auto slopes = reuse_slopes ? std::span<const std::int64_t>(ranked_slopes).subspan(slope_offset, cached->slope_count) :
                std::span<const std::int64_t>(proposals.slopes);
            if (rank_only) {
                auto& entry = timing_cache[index];
                if (!ranked_slopes.empty()) {
                    if (slopes.size() > plan.slope_budget) throw Error("Robust ranked drift slopes exceed their budget");
                    std::ranges::copy(slopes, std::span(ranked_slopes).subspan(slope_offset, plan.slope_budget).begin());
                    entry.slope_count = slopes.size();
                }
                constexpr std::array<std::int64_t, 1> zero_slope{0};
                if (!on_device || !device_slopes(zero_slope, entry.peaks, &entry.nomination))
                    score_slope(0, entry.peaks, &entry.nomination);
                lap(drift_slope);
                count_evaluation();
                for (auto& rank : entry.nomination)
                    rank = rank > 0 ? std::log(rank) + 2 * exponent * std::numbers::ln2 - std::log(variance) : -infinity;
                entry.exponent = exponent;
                entry.ready = true;
                // All atoms use the same physical variance. Undo scaling to
                // make matched-product ranks comparable between atoms.
                ranks[index] = std::scalbn(proposals.rank, 2 * exponent);
                return;
            }
            std::array<std::array<DriftPeak, retained_drift_per_bit>, 2> peaks{};
            scored_slopes[index] = slopes.size();
            if (cached) {
                peaks = cached->peaks;
                // Both ridge lists start with zero and the merge deduplicates it.
                if (!slopes.empty() && slopes.front() == 0) slopes = slopes.subspan(1);
            }
            if (!on_device || !device_slopes(slopes, peaks, nullptr))
                for (const auto slope : slopes) score_slope(slope, peaks, nullptr);
            lap(drift_slope);
            count_evaluation();
            // The atom's timing and exactly scored (bit, slope, bin) set, in a
            // canonical order.
            {
                std::array<std::array<std::int64_t, 3>, 2 * retained_drift_per_bit> retained{};
                std::size_t retained_count = 0;
                for (std::size_t bit = 0; bit < 2; ++bit)
                    for (const auto& peak : peaks[bit])
                        if (std::isfinite(peak.rank))
                            retained[retained_count++] = {static_cast<std::int64_t>(bit), peak.slope, peak.center};
                std::sort(retained.begin(), retained.begin() + static_cast<std::ptrdiff_t>(retained_count));
                auto& digest = retained_digests[index];
                drift_digest_add(digest, static_cast<std::uint64_t>(coordinate.epoch_offset_seconds));
                drift_digest_add(digest, std::bit_cast<std::uint64_t>(coordinate.arrival_seconds));
                drift_digest_add(digest, std::bit_cast<std::uint64_t>(coordinate.clock_ppm));
                for (std::size_t i = 0; i < retained_count; ++i)
                    for (const auto value : retained[i]) drift_digest_add(digest, static_cast<std::uint64_t>(value));
            }
            std::array<std::array<DriftStatistics, retained_drift_per_bit>, 2> statistics{};
            const auto with_phase = [&](const DriftPeak& peak, std::size_t first, std::size_t rows,
                                        double direction, const auto& accumulate) {
                const auto delta = 1 / config.sample_rate_hz;
                const auto frequency = direction * peak.center * plan.frequency_step;
                const auto rate = direction * peak.slope * plan.slope_step;
                const auto first_time = (plan.first + first) * delta - plan.slope_reference_seconds;
                const auto curve = std::polar(1., tau * rate * delta * delta);
                Complex phase, step;
                for (std::size_t i = 0; i < rows; ++i) {
                    if (!(i % 256)) {
                        check_cancelled(leaf_stop);
                        const auto t = first_time + i * delta;
                        phase = std::polar(1., tau * (frequency * t + .5 * rate * t * t));
                        step = std::polar(1., tau * delta * (frequency + rate * (t + .5 * delta)));
                    }
                    accumulate(i, phase);
                    phase *= step;
                    step *= curve;
                }
            };
            if (!projection && plan.sample_stride == 1) {
                // The unreduced proposal products are already exact FP64
                // sufficient statistics apart from the carrier phase.
                for (std::size_t bit = 0; bit < 2; ++bit)
                    for (std::size_t atom = 0; atom < peaks[bit].size(); ++atom) {
                        const auto& peak = peaks[bit][atom];
                        if (!std::isfinite(peak.rank)) continue;
                        auto& stats = statistics[bit][atom];
                        with_phase(peak, 0, plan.raw_rows, -1., [&](std::size_t i, Complex phase) {
                            for (std::size_t path = 0; path < taps; ++path)
                                stats.correlation[path] += own.products[bit * taps + path][i] * phase;
                        });
                    }
            } else {
                // Every retained atom shares the same raw render. For a long
                // aperture, never retain a raw template for every atom.
                const auto blocks = projection ? projection->blocks : 1 + (plan.raw_rows - 1) / tile;
                const auto max_rows = projection ? projection->max_rows() : tile;
                own.rendered.resize(2 * taps * max_rows);
                if (projection) {
                    own.projected_columns.resize(taps * max_rows);
                    own.projection_scratch.resize(plan.projection_scratch_rows ? plan.projection_scratch_rows :
                                                  DftPlan::scratch_samples(max_rows));
                }
                for (std::size_t block = 0, first = 0; block < blocks; ++block) {
                    const auto* transform = projection ? &projection->at(block) : nullptr;
                    const auto rows = transform ? transform->rows() : std::min(tile, plan.raw_rows - first);
                    for (std::size_t offset = 0; offset < rows; offset += tile) {
                        const auto count = std::min(tile, rows - offset);
                        const auto rendered = std::span(own.raw).first(2 * taps * count);
                        basis.render_pair(row, std::span(paths).first(taps), plan.first + first + offset,
                                          std::span(own.offsets).first(count), rendered, leaf_stop);
                        for (std::size_t column = 0; column < 2 * taps; ++column)
                            std::ranges::copy(rendered.subspan(column * count, count),
                                own.rendered.begin() + static_cast<std::ptrdiff_t>(column * rows + offset));
                    }
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        for (std::size_t atom = 0; atom < peaks[bit].size(); ++atom) {
                            const auto& peak = peaks[bit][atom];
                            if (!std::isfinite(peak.rank)) continue;
                            auto& stats = statistics[bit][atom];
                            const auto columns = std::span(own.rendered).subspan(bit * taps * rows, taps * rows);
                            if (!transform) {
                                with_phase(peak, first, rows, -1., [&](std::size_t i, Complex phase) {
                                    const auto observation = input[first + i] * phase;
                                    for (std::size_t path = 0; path < taps; ++path)
                                        stats.correlation[path] += std::conj(columns[path * rows + i]) * observation;
                                });
                                continue;
                            }
                            with_phase(peak, first, rows, 1., [&](std::size_t i, Complex phase) {
                                for (std::size_t path = 0; path < taps; ++path)
                                    own.projected_columns[path * rows + i] = columns[path * rows + i] * phase;
                            });
                            for (std::size_t path = 0; path < taps; ++path) {
                                auto column = std::span(own.projected_columns).subspan(path * rows, rows);
                                transform->apply(column, own.projection_scratch, leaf_stop);
                                if (line) remove_line(column, std::span(projection->line_storage).subspan(first, transform->modes()));
                                for (std::size_t i = 0; i < transform->modes(); ++i) {
                                    stats.correlation[path] += std::conj(column[i]) * projection->observed[first + i];
                                    stats.diagonal[path] += std::norm(column[i]);
                                    if (path == 1)
                                        stats.off_diagonal += std::conj(own.projected_columns[i]) * column[i];
                                }
                            }
                        }
                    first += rows;
                }
            }
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t atom = 0; atom < peaks[bit].size(); ++atom) {
                    if (!std::isfinite(peaks[bit][atom].rank)) continue;
                    const auto& stats = statistics[bit][atom];
                    const auto& diagonal = projection ? stats.diagonal : energy[bit];
                    const auto off_diagonal = projection ? stats.off_diagonal : cross[bit];
                    for (const auto p : precision) {
                        const DriftGram gram(diagonal[0], diagonal[1], off_diagonal, p, variance);
                        const auto scale = 1. / (std::sqrt(variance) * std::sqrt(p));
                        const auto value = gram.score(stats.correlation[0] * scale, stats.correlation[1] * scale,
                                                      inverse_variance);
                        if (!std::isfinite(value)) throw Error("Robust projected drift evidence is nonfinite");
                        evidence[index][bit] = log_add(evidence[index][bit], value);
                    }
                }
            lap(drift_exact);
    };
    if (rank_timing) {
        executor.parallel(slot.index, timing.size(), static_cast<unsigned>(active), stop,
            [&](unsigned lane, std::size_t index, std::stop_token leaf_stop) {
                evaluate(lane, index, true, leaf_stop);
            });
        std::partial_sort(survivors.begin(), survivors.begin() + retained_drift_ridge_timing_atoms, survivors.end(),
            [&](std::size_t a, std::size_t b) { return ranks[a] != ranks[b] ? ranks[a] > ranks[b] : a < b; });
        survivors.resize(retained_drift_ridge_timing_atoms);
        // Preserve every old ridge winner. The two additional nominations
        // use a separately normalized coherent rank, never a comparison of
        // unlike ridge and coherent statistics. All retain their original
        // full public timing prior in the exact likelihood below.
        for (std::size_t bit = 0; bit < 2; ++bit) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < timing_cache.size(); ++i)
                if (timing_cache[i].nomination[bit] > timing_cache[best].nomination[bit]) best = i;
            if (std::isfinite(timing_cache[best].nomination[bit]) &&
                std::ranges::find(survivors, best) == survivors.end()) survivors.push_back(best);
        }
        std::ranges::sort(survivors);
    }
    executor.parallel(slot.index, survivors.size(), static_cast<unsigned>(active), stop,
        [&](unsigned lane, std::size_t index, std::stop_token leaf_stop) {
            evaluate(lane, survivors[index], false, leaf_stop);
        });
    const auto log_mass = std::log(static_cast<double>(count)) + std::log(static_cast<double>(public_slopes)) +
        std::log(3.) + std::log(static_cast<double>(survivors.size()));
    for (std::size_t bit = 0; bit < 2; ++bit) {
        double total = -infinity;
        for (const auto& atom : evidence) total = log_add(total, atom[bit]);
        score.drift_bit_evidence[bit] = total - log_mass;
    }
    // A set digest: independent of the order of equal timing atoms.
    std::vector<std::uint64_t> retained;
    retained.reserve(survivors.size());
    for (const auto index : survivors) retained.push_back(retained_digests[index]);
    std::ranges::sort(retained);
    score.drift_retained_digest = drift_digest_basis;
    for (const auto digest : retained) drift_digest_add(score.drift_retained_digest, digest);
    score.drift_device_atoms = 0;
    for (const auto& own : scratch) {
        score.drift_device_atoms += own.device_evaluations;
        if (device) {
            device->device_evaluations += own.device_evaluations;
            device->native_evaluations += own.native_evaluations;
        }
    }
    score.drift_stage_workers = static_cast<unsigned>(active);
    score.drift_coherent_timing_fft_rows = rank_timing ?
        checked_multiply(timing.size(), checked_multiply(2 * taps, plan.nfft)) : 0;
    score.drift_coherent_timing_fft_stages = checked_multiply(score.drift_coherent_timing_fft_rows,
        std::countr_zero(plan.nfft));
    score.drift_timing_atoms = survivors.size();
    score.drift_slopes = std::accumulate(scored_slopes.begin(), scored_slopes.end(), std::size_t{});
    score.drift_frequency_bins = count;
    score.drift_observation_rows = plan.raw_rows;
    score.drift_proposal_stride = plan.sample_stride;
    std::array<double, drift_phases> seconds{};
    for (const auto& own : scratch)
        for (std::size_t phase = 0; phase < drift_phases; ++phase) seconds[phase] += own.seconds[phase];
    score.drift_render_seconds = seconds[drift_render];
    score.drift_ridge_seconds = seconds[drift_ridge];
    score.drift_ridge_search_seconds = seconds[drift_ridge_search];
    score.drift_slope_seconds = seconds[drift_slope];
    score.drift_exact_seconds = seconds[drift_exact];
    score.drift_seconds = seconds_since(began);
}
} // namespace

std::array<double, 2> mix_drift_sign_evidence(const std::array<double, 2>& first, std::size_t first_atoms,
                                           const std::array<double, 2>& second, std::size_t second_atoms) {
    const auto atoms = checked_add(first_atoms, second_atoms);
    if (!atoms) throw Error("Robust signed drift mixture requires public timing atoms");
    const auto first_mass = first_atoms ? std::log(static_cast<double>(first_atoms)) : -infinity;
    const auto second_mass = second_atoms ? std::log(static_cast<double>(second_atoms)) : -infinity;
    const auto normalization = log_two + std::log(static_cast<double>(atoms));
    std::array<double, 2> result;
    for (std::size_t bit = 0; bit < 2; ++bit)
        result[bit] = log_add(first[bit] + first_mass, second[bit] + second_mass) - normalization;
    return result;
}

void score_drift_fine(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                       const DriftPlan& plan, std::span<const Complex> iq,
                       std::span<const FineCoordinate> selected, std::span<const FineCoordinate> epoch_timing,
                       unsigned workers, ReceiverSlotScore& score, std::stop_token stop,
                       ReceiverExecutor& executor, const LineAtom* line, DriftZeroSlopeEvaluation zero_evaluation) {
    namespace fft = modem::detail::search_fft;
    const auto began = FineClock::now();
    // One device proposal session, if any, serves both relative-Doppler
    // signs and ends with the stage.
    DriftDeviceStage device;
    const auto session = open_drift_device(config, plan, selected, epoch_timing, device, stop);
    const auto evaluate = [&](const ReceiverConfig& physical, ReceiverSlotScore& target) {
        if (config.native_coarse_precision == NativeCoarsePrecision::fp64)
            score_drift_impl<double, fft::DoublePlan>(physical, slot, key, plan, iq, selected, epoch_timing,
                                                    workers, target, stop, executor, line, zero_evaluation, nullptr);
        else
            score_drift_impl<float, fft::FloatPlan>(physical, slot, key, plan, iq, selected, epoch_timing,
                                                   workers, target, stop, executor, line, zero_evaluation,
                                                   session ? &device : nullptr);
    };
    // A session's route, with any evaluation that fell back to the native proposals.
    const auto name_route = [&] {
        if (!session) return;
        score.drift_proposal_route = std::string(session->receipt().vulkan ? "vulkan" : "device") +
            "_fp32_fft_fp64_ridge" + (device.native_evaluations ? "_with_native_atoms" : "");
    };
    evaluate(config, score);
    score.drift_relative_doppler_models = 1;
    if (config.spread_doppler_hz == 0) {
        name_route();
        return;
    }

    // The public relative-Doppler allowance admits either sign. Evaluate
    // each coherent spatial basis under half the existing drift mass.
    // Scratch and workers are reused sequentially for the second sign.
    auto reflected = config;
    reflected.spread_doppler_hz = -config.spread_doppler_hz;
    ReceiverSlotScore mirrored;
    evaluate(reflected, mirrored);
    const auto first_atoms = score.drift_timing_atoms;
    const auto second_atoms = mirrored.drift_timing_atoms;
    const auto atoms = checked_add(first_atoms, second_atoms);
    // Each primitive averages its own retained timing atoms. Restore
    // those sums before mixing signs, then use the combined count for
    // the caller's unchanged full-public-timing prior adjustment.
    score.drift_bit_evidence = mix_drift_sign_evidence(score.drift_bit_evidence, first_atoms,
                                                     mirrored.drift_bit_evidence, second_atoms);
    score.drift_timing_atoms = atoms;
    score.drift_slopes = checked_add(score.drift_slopes, mirrored.drift_slopes);
    score.drift_device_atoms = checked_add(score.drift_device_atoms, mirrored.drift_device_atoms);
    const auto first_digest = std::exchange(score.drift_retained_digest, drift_digest_basis);
    drift_digest_add(score.drift_retained_digest, first_digest);
    drift_digest_add(score.drift_retained_digest, mirrored.drift_retained_digest);
    name_route();
    score.drift_relative_doppler_models = 2;
    score.drift_stage_workers = std::max(score.drift_stage_workers, mirrored.drift_stage_workers);
    score.drift_coherent_timing_fft_rows = checked_add(score.drift_coherent_timing_fft_rows,
        mirrored.drift_coherent_timing_fft_rows);
    score.drift_coherent_timing_fft_stages = checked_add(score.drift_coherent_timing_fft_stages,
        mirrored.drift_coherent_timing_fft_stages);
    score.drift_render_seconds += mirrored.drift_render_seconds;
    score.drift_ridge_seconds += mirrored.drift_ridge_seconds;
    score.drift_ridge_search_seconds += mirrored.drift_ridge_search_seconds;
    score.drift_slope_seconds += mirrored.drift_slope_seconds;
    score.drift_exact_seconds += mirrored.drift_exact_seconds;
    score.drift_seconds = seconds_since(began);
}

} // namespace datapump::triad::v3::receiver_detail
