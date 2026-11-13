#include "triad_receiver_fine.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_basis.hpp"
#include "search_fft.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>

extern "C" int triad_gaussian_update_v1(
    std::size_t taps, std::size_t candidates, std::size_t models, std::size_t blocks,
    std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* mean, double* covariance,
    double* scores, double* diagnostics) noexcept;

namespace datapump::triad::v3::receiver_detail {
namespace {
constexpr std::size_t maximum_states = 64, maximum_proposals = 256;
constexpr std::size_t maximum_segment_rows = 1'048'576, maximum_basis_rows = 16'384;
constexpr int maximum_rate_radius = 32;
constexpr double acceleration_ppm_per_second = 10.;
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr std::array<double, receiver_coherence_count> coherence_seconds{.5, 2., infinity};
constexpr std::size_t models = coherence_seconds.size(), components = 2 * models;
constexpr double log_two = std::numbers::ln2;
constexpr double tau = 2 * std::numbers::pi;
using FineClock = std::chrono::steady_clock;

double seconds_since(FineClock::time_point began) {
    return std::chrono::duration<double>(FineClock::now() - began).count();
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
        : rows(static_cast<std::size_t>(plan.stop_sample - plan.first_sample)),
          block_rows(round_rows(.25, config.sample_rate_hz)),
          segment_rows(round_rows(.5, config.sample_rate_hz)) {
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
    double sample_rate;

    enum class Policy { legacy, separable_varying };

    ProjectionBands(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                    Policy policy = Policy::legacy)
        : use_exclusions(config.helper_enabled), omit_overlapping_modes(policy == Policy::separable_varying),
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

class Projection {
public:
    Projection(std::size_t rows, const ProjectionBands& bands, std::stop_token stop)
        : rows_(rows), normalization_(1 / std::sqrt(static_cast<double>(rows))) {
        indices_.reserve(rows);
        visit_projection_modes(rows, bands, stop, [&](std::size_t index) { indices_.push_back(index); });
        if (indices_.size() != 1 || indices_.front() != 0) plan_.emplace(rows, stop);
    }

    std::size_t rows() const { return rows_; }
    std::size_t modes() const { return indices_.size(); }
    std::span<const std::size_t> indices() const { return indices_; }

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
    void apply(std::span<Complex> values, std::span<Complex> scratch, std::stop_token stop) const {
        if (!plan_) {
            Complex dc{};
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (!(i % 4096)) check_cancelled(stop);
                dc += values[i];
            }
            values[0] = dc * normalization_;
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
        if (!plan_) {
            Complex dc{};
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (!(i % 4096)) check_cancelled(stop);
                dc += values[i];
            }
            std::fill(values.begin(), values.end(), dc / static_cast<double>(rows_));
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
    std::size_t rows_;
    double normalization_;
    std::vector<std::size_t> indices_;
    std::optional<DftPlan> plan_;
};

struct ChannelPosterior {
    std::array<Complex, 2> mean{};
    std::array<Complex, 4> covariance{};
};

struct State {
    FineStateKey key;
    std::array<double, components> weights{};
    std::array<ChannelPosterior, components> posterior{};
    double prior_mass = 0;
    bool nonzero_basis = false;
};

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
                    channel.covariance[tap * taps + tap] = 1. / static_cast<double>(taps);
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
            for (auto i = target.first_edge; i < target.stop_edge; ++i) {
                const auto& edge = edges[i];
                const auto& in = previous[edge.source].posterior[c];
                const auto p = weight(edge);
                std::array<Complex, 2> difference{};
                for (std::size_t k = 0; k < taps; ++k)
                    difference[k] = in.mean[k] * edge.phase - out.mean[k];
                for (std::size_t k = 0; k < taps; ++k)
                    for (std::size_t l = k; l < taps; ++l)
                        out.covariance[k * taps + l] += p *
                            (in.covariance[k * taps + l] + difference[k] * std::conj(difference[l]));
            }
            for (std::size_t k = 0; k < taps; ++k) {
                out.covariance[k * taps + k] = out.covariance[k * taps + k].real();
                for (std::size_t l = k + 1; l < taps; ++l)
                    out.covariance[l * taps + k] = std::conj(out.covariance[k * taps + l]);
            }
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

void sufficient_statistics(MappedBasis& basis, const MappedBasisRow& row, std::span<const Tap> taps,
                           const ObservationBlock& block, double variance, WorkerScratch& scratch,
                           std::span<Complex> correlations, std::span<Complex> gram,
                           bool& nonzero_basis, std::stop_token stop) {
    const auto rows = block.projection->rows();
    const auto columns = 2 * taps.size();
    auto raw = std::span(scratch.raw).first(rows * columns);
    for (std::size_t first = 0; first < rows; first += maximum_basis_rows) {
        const auto count = std::min(maximum_basis_rows, rows - first);
        auto rendered = count == rows ? raw : std::span(scratch.chunk).first(count * columns);
        basis.render_pair(row, taps, block.first + first, std::span(scratch.offsets).first(count),
                          rendered, stop);
        if (count != rows) {
            for (std::size_t column = 0; column < columns; ++column)
                std::copy_n(rendered.begin() + static_cast<std::ptrdiff_t>(column * count), count,
                            raw.begin() + static_cast<std::ptrdiff_t>(column * rows + first));
        }
    }
    for (std::size_t column = 0; column < columns; ++column) {
        auto values = raw.subspan(column * rows, rows);
        // Required for a complete receipt even on slots that contain only noise.
        if (!nonzero_basis) nonzero_basis = finite_energy(values) > 0;
        block.projection->apply(values, scratch.fft, stop);
        remove_line(values, block.line);
    }
    const auto tap_count = taps.size();
    for (std::size_t hypothesis = 1; hypothesis <= 2; ++hypothesis) {
        const auto offset = (hypothesis - 1) * tap_count;
        for (std::size_t tap = 0; tap < tap_count; ++tap) {
            auto a = raw.subspan((offset + tap) * rows, block.projected.size());
            Complex correlation{};
            for (std::size_t i = 0; i < a.size(); ++i) correlation += std::conj(a[i]) * block.projected[i];
            correlations[hypothesis * tap_count + tap] = correlation / variance;
            const auto start = hypothesis * tap_count * tap_count;
            for (std::size_t other = tap; other < tap_count; ++other) {
                auto b = raw.subspan((offset + other) * rows, block.projected.size());
                Complex product{};
                for (std::size_t i = 0; i < a.size(); ++i) product += std::conj(a[i]) * b[i];
                product /= variance;
                gram[start + tap * tap_count + other] = product;
                gram[start + other * tap_count + tap] = std::conj(product);
            }
        }
    }
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
    std::uint64_t positive_basis_blocks = 0;
    long double template_energy = 0;
};

void advance_state(State& state, const MappedBasisRow& row, MappedBasis& basis,
                   std::span<const Tap> taps, std::span<const ObservationBlock> blocks,
                   const std::array<const FineBulkStatistics*, 2>& prepared,
                   const std::array<double, 4 * 2 * models>& transitions,
                   double variance, WorkerScratch& scratch, StateBlockWork* work, std::stop_token stop,
                   double path_power = 1) {
    const auto tap_count = taps.size(), tap_square = tap_count * tap_count;
    std::array<Complex, 2 * 3 * 2> correlations{};
    std::array<Complex, 2 * 3 * 4> grams{};
    const double tap_power = path_power / tap_count;
    std::array<double, 2> powers{tap_power, tap_power};
    if (work) *work = {};
    const auto basis_began = work ? FineClock::now() : FineClock::time_point{};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].observed) {
            const auto gram = std::span(grams).subspan(i * 3 * tap_square, 3 * tap_square);
            const auto correlation = std::span(correlations).subspan(i * 3 * tap_count, 3 * tap_count);
            if (const auto* bulk = prepared[i]) {
                std::copy_n(bulk->correlations.begin(), correlation.size(), correlation.begin());
                std::copy_n(bulk->gram.begin(), gram.size(), gram.begin());
                state.nonzero_basis |= bulk->nonzero_basis;
            } else {
                sufficient_statistics(basis, row, taps, blocks[i], variance, scratch,
                    correlation, gram, state.nonzero_basis, stop);
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

    std::array<Complex, models * 3 * 2> means{};
    std::array<Complex, models * 3 * 4> covariance{};
    std::array<double, models * 3> scores{};
    std::array<double, 2 * models> diagnostics{};
    for (std::size_t model = 0; model < models; ++model) {
        const auto null = model * 3 * tap_square;
        for (std::size_t tap = 0; tap < tap_count; ++tap)
            covariance[null + tap * tap_count + tap] = powers[tap];
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto h = model * 3 + bit + 1;
            const auto& channel = state.posterior[model * 2 + bit];
            std::copy_n(channel.mean.begin(), tap_count, means.data() + h * tap_count);
            std::copy_n(channel.covariance.begin(), tap_square, covariance.data() + h * tap_square);
        }
    }

    check_cancelled(stop);
    const auto status = triad_gaussian_update_v1(tap_count, 1, models, blocks.size(), 0, 1,
        reinterpret_cast<const double*>(correlations.data()), reinterpret_cast<const double*>(grams.data()),
        powers.data(), transitions.data(), reinterpret_cast<double*>(means.data()),
        reinterpret_cast<double*>(covariance.data()), scores.data(), diagnostics.data());
    if (status) throw Error("Robust fine Gaussian update failed (" + std::to_string(status) + ")");

    for (std::size_t model = 0; model < models; ++model) {
        if (scores[model * 3] != 0) throw Error("Robust Gaussian common null acquired nonzero evidence");
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto h = model * 3 + bit + 1;
            const auto component = model * 2 + bit;
            auto& channel = state.posterior[component];
            std::copy_n(means.data() + h * tap_count, tap_count, channel.mean.begin());
            std::copy_n(covariance.data() + h * tap_square, tap_square, channel.covariance.begin());
            state.weights[component] += scores[h];
            if (!std::isfinite(state.weights[component]))
                throw Error("Robust Gaussian likelihood evidence is nonfinite");
        }
    }
    if (work) scratch.gaussian_seconds += seconds_since(gaussian_began);
}

std::array<double, 2> bit_scores(const std::vector<State>& states) {
    std::array<double, 2> scores{-infinity, -infinity};
    for (const auto& state : states)
        for (std::size_t model = 0; model < models; ++model)
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
};

FineWorkspaceQuote workspace_for_plan(const ReceiverConfig& config, const ReceiverSlotPlan& plan,
                                      const BlockCalendar& calendar, unsigned workers, bool record_work,
                                      std::stop_token stop = {}) {
    if (!workers) throw Error("fine observation requires a positive worker share");
    // Reject unsupported public support before leasing a slot.
    const ProjectionBands validated(config, plan);
    const auto lengths = calendar.lengths();
    const auto max_rows = *std::max_element(lengths.begin(), lengths.end());
    std::size_t bytes = 64 * 1024, fft_rows = 0, max_modes = 0;
    const auto add = [&](std::size_t count, std::size_t element) {
        bytes = checked_add(bytes, checked_multiply(count, element));
    };
    for (const auto rows : lengths) {
        if (!rows) continue;
        max_modes = std::max(max_modes, visit_projection_modes(rows, validated, stop, [](std::size_t) {}));
        bytes = checked_add(bytes, DftPlan::workspace_bytes(rows));
        add(rows, sizeof(std::size_t));
        fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
    }
    // The dispatch being scored and the one observed ahead of it.
    add(fft_rows + 4 * max_rows, sizeof(Complex));
    // Both lane states can coexist with the predicted/merged adaptive states.
    add(3 * maximum_proposals, sizeof(State));
    add(2 * maximum_states * (2 * maximum_rate_radius + 1),
        sizeof(TransitionTarget) + sizeof(TransitionEdge) + sizeof(RankedIndex));
    // Every dispatch's public geometry for one call.
    add(maximum_dispatches(calendar), dispatch_extent_bytes);
    // The fixed-affine lane's maps, source groups and chain progress.
    add(maximum_proposals, sizeof(MappedBasisRow) + 3 * sizeof(std::uint32_t));
    add(receiver_epoch_offsets.size(), sizeof(std::vector<std::uint32_t>));
    if (record_work) add(3 * maximum_proposals, sizeof(StateBlockWork));
    const auto active = useful_fine_workers(workers);
    const auto mapped_rows = std::min(max_rows, maximum_basis_rows);
    const auto taps = config.spread_delay_seconds || config.spread_doppler_hz ? 2U : 1U;
    const auto fallback = config.bulk_backend ? config.bulk_backend->fine_fallback_reason(
        {max_rows, max_modes, taps, workers, std::string_view(modem::detail::search_fft::kernel_name()) == "avx2"})
        : std::string_view{};
    if (config.bulk_backend && fallback.empty()) {
        bytes = checked_add(bytes, config.bulk_backend->fine_host_bytes(max_rows, taps));
        add(4 * maximum_proposals, sizeof(FineBulkStatistics));
        add(2 * maximum_proposals, sizeof(MappedBasisRow));
        add(2 * maximum_proposals, sizeof(std::size_t));
        add(mapped_rows, sizeof(double));
        add(active, sizeof(WorkerScratch));
    } else {
        add(active, checked_add(checked_multiply(fft_rows + 4 * max_rows +
            (max_rows > mapped_rows ? 4 * mapped_rows : 0), sizeof(Complex)),
            checked_multiply(mapped_rows, sizeof(double))));
    }
    bytes = checked_add(bytes, config.bulk_backend ? config.bulk_backend->persistent_host_workspace() : 0);
    return {checked_add(bytes, MappedBasis::workspace_bytes(config, plan, mapped_rows, active)), fallback};
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

FineBlockGeometry fine_block_geometry(double sample_rate_hz) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz < 1)
        throw Error("Invalid Robust fine sample rate");
    return {round_rows(.25, sample_rate_hz), round_rows(.5, sample_rate_hz)};
}

std::size_t fine_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                           FineObservation observation, unsigned workers) {
    const auto plan = observation_plan(config, slot, observation);
    const BlockCalendar calendar(config, plan);
    return workspace_for_plan(config, plan, calendar, workers, observation.record_work).bytes;
}

std::size_t fine_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return fine_workspace(config, slot, whole_slot(slot), workers);
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
        std::uint64_t observed_rows = 0, modes = 0, basis_calls = 0;
        std::span<const ObservationBlock> observations() const { return std::span(blocks).first(extent->count); }
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
    std::unique_ptr<MappedBasis> basis;
    std::unique_ptr<FineBulk> bulk;
    FineBulkWork bulk_work;
    std::vector<MappedBasisRow> bulk_rows;
    std::vector<double> bulk_offsets;
    std::vector<std::size_t> bulk_progress;
    std::array<std::vector<FineBulkStatistics>, 2> bulk_statistics;
    std::vector<WorkerScratch> scratch;
    // A standalone session owns its lanes. A stream shares its receiver pool.
    std::unique_ptr<ReceiverExecutor> owned_executor;
    ReceiverExecutor& executor;
    // Every dispatch of the current call, then the one being scored and the
    // one observed ahead of it.
    std::vector<Extent> extents;
    std::array<Pair, 2> ring;
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
    std::array<std::vector<StateBlockWork>, 2> nominal_state_work;
    std::vector<StateBlockWork> adaptive_state_work;
    std::vector<RankedIndex> ranking;
    std::vector<State> nominal, adaptive;

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
         ReceiverExecutor* shared)
        : config(input_config), observation(extent), plan(observation_plan(config, slot, observation)),
          iq(samples), validity(valid), stop(cancellation), calendar(config, plan), grid(projected_rate, clock_gap),
          paths{{{0, 0}, {config.spread_delay_seconds, config.spread_doppler_hz}}},
          tap_count(paths[1].delay_seconds || paths[1].doppler_hz ? 2 : 1), workers(allocation),
          stage_workers(useful_fine_workers(workers)),
          owned_executor(shared ? nullptr : std::make_unique<ReceiverExecutor>(std::max(1U, stage_workers))),
          executor(shared ? *shared : *owned_executor) {
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

        const auto quote = workspace_for_plan(config, plan, calendar, workers, observation.record_work, stop);
        workspace = quote.bytes;
        if (workspace > scratch_limit)
            throw ReceiverWorkspaceError("complete fine receiver exceeds its admitted workspace");
        coordinates.assign(selected.begin(), selected.end());
        nominal = grid.initial(coordinates.size(), tap_count, cold);
        adaptive = nominal;
        nominal_work.initial_states = adaptive_work.initial_states = nominal.size();
        nominal_work.state_cap = nominal_work.proposal_cap = nominal.size();
        adaptive_work.state_cap = maximum_states;
        adaptive_work.proposal_cap = maximum_proposals;

        const auto lengths = calendar.lengths();
        projections.reserve(lengths.size());
        const ProjectionBands bands(config, plan);
        std::size_t fft_rows = 0, max_rows = 0;
        for (const auto rows : lengths) {
            if (!rows) continue;
            projections.emplace_back(rows, bands, stop);
            fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
            max_rows = std::max(max_rows, rows);
        }
        observation_scratch.resize(fft_rows);
        const auto mapped_rows = std::min(max_rows, maximum_basis_rows);
        basis = std::make_unique<MappedBasis>(config, plan, key, mapped_rows, stage_workers);
        if (config.bulk_backend && quote.native_fallback_reason.empty()) {
            bulk = config.bulk_backend->make_fine(projected_rate, 2 * config.clock_bound_ppm);
            if (!bulk) throw Error("requested Robust bulk backend did not construct a fine session");
            bulk_work = bulk->completed_work();
            bulk_rows.reserve(2 * maximum_proposals);
            bulk_progress.reserve(2 * maximum_proposals);
            bulk_offsets.resize(mapped_rows);
            std::iota(bulk_offsets.begin(), bulk_offsets.end(), 0.);
            for (auto& item : bulk_statistics) item.reserve(2 * maximum_proposals);
        } else if (config.bulk_backend) {
            config.bulk_backend->record_native_fine(quote.native_fallback_reason);
        }
        scratch.reserve(stage_workers);
        for (unsigned worker = 0; worker < stage_workers; ++worker)
            scratch.emplace_back(bulk ? 0 : max_rows, bulk ? 0 : fft_rows);
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
        ranking.reserve(2 * maximum_states * (2 * maximum_rate_radius + 1));
        if (observation.record_work) {
            for (auto& item : nominal_state_work) item.resize(nominal.size());
            adaptive_state_work.resize(maximum_proposals);
        }
    }

    template<class Operation>
    void time(double& receipt, Operation operation) {
        const auto began = observation.record_work ? FineClock::now() : FineClock::time_point{};
        operation();
        if (observation.record_work) receipt += seconds_since(began);
    }

    bool observation_complete() const { return processed == observation.rows; }
    bool representative_work() const { return nominal_nonzero && adaptive_nonzero; }
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

    void prepare(ObservationBlock& block, std::size_t first, std::size_t rows, std::stop_token leaf_stop) {
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
            return;
        }
        const auto raw = iq.subspan(first, rows);
        block.projected.assign(raw.begin(), raw.end());
        projection->apply(block.projected, observation_scratch, leaf_stop);
        block.projected.resize(projection->modes());
        observed_modes += projection->modes();
        const auto quiet_first = plan.quiet_first_sample - plan.first_sample;
        const auto quiet_stop = plan.quiet_stop_sample - plan.first_sample;
        if (quiet_first <= first && first + rows <= quiet_stop) {
            quiet_modes += projection->modes();
            quiet_energy += finite_energy(block.projected) / config.noise_variance;
            if (!std::isfinite(quiet_energy)) throw Error("Robust quiet energy is nonfinite");
        }
    }

    // Projects one dispatch's blocks in chronological order. Calls never
    // overlap: the next dispatch waits for this observation to finish before
    // another observation starts or either ring entry is reused.
    void observe(Pair& pair, const Extent& extent, std::stop_token leaf_stop) {
        time(times.observation_seconds, [&] {
            check_cancelled(leaf_stop);
            auto first = extent.first;
            pair.extent = &extent;
            pair.observed_rows = pair.modes = pair.basis_calls = 0;
            pair.transitions = {};
            for (std::size_t i = 0; i < extent.count; ++i) {
                auto& block = pair.blocks[i];
                prepare(block, first, extent.lengths[i], leaf_stop);
                first += extent.lengths[i];
                // Elapsed intervals and channel models are common to all
                // states, including for a missing block whose elapsed time
                // still evolves the posterior.
                for (std::size_t model = 0; model < models; ++model) {
                    const auto offset = 4 * (i * models + model);
                    const auto exponent = -block.elapsed / coherence_seconds[model];
                    const auto decay = std::exp(exponent);
                    pair.transitions[offset] = decay * decay;
                    pair.transitions[offset + 1] = -std::expm1(2 * exponent);
                    pair.transitions[offset + 2] = decay;
                }
                if (!block.observed) continue;
                const auto rows = block.projection->rows();
                pair.observed_rows += rows;
                pair.modes += block.projection->modes();
                pair.basis_calls += 1 + (rows - 1) / maximum_basis_rows;
            }
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
        } else {
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
        adaptive_nonzero |= std::any_of(adaptive.begin(), adaptive.end(),
            [](const State& state) { return state.nonzero_basis; });
        time(times.pruning_seconds, [&] {
            if (observation.record_work && adaptive.size() > maximum_states) {
                ++adaptive_work.pruning_events;
                adaptive_work.states_dropped += adaptive.size() - maximum_states;
            }
            retain(adaptive, maximum_states, ranking);
        });
        nominal_work.peak_retained_states = nominal.size();
        adaptive_work.peak_retained_states = std::max(adaptive_work.peak_retained_states, adaptive.size());
        previous_anchor = segment_anchor;
        previous_last = static_cast<long double>(pair.segment_first + pair.segment_rows - 1) / fs;
        processed += pair.segment_rows;
        ++segments;
    }

    // Device statistics for both lanes of one dispatch, on the owner.
    std::array<std::uint64_t, 2> bulk_statistics_for(const Pair& pair, std::stop_token leaf_stop) {
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
            for (std::size_t i = 0; i < pair.extent->count; ++i) {
                check_cancelled(leaf_stop);
                const auto& block = pair.blocks[i];
                if (!block.observed) continue;
                const auto rows = block.projection->rows();
                auto& output = bulk_statistics[i];
                // Leave unwritten required fields distinguishable from
                // legitimate all-zero statistics outside DATA support.
                constexpr auto missing = std::numeric_limits<double>::quiet_NaN();
                FineBulkStatistics unset;
                unset.correlations.fill({missing, missing});
                unset.gram.fill({missing, missing});
                output.assign(states, unset);
                bulk_progress.assign(states, 0);
                const FineBulkBlock shape{rows, tap_count, block.projection->indices(),
                                          block.projected, config.noise_variance};
                bulk->statistics(shape, states, [&](std::size_t candidate, std::size_t first,
                        std::size_t take, const BulkMappedSink& sink) {
                    if (candidate >= states || first != bulk_progress[candidate] || first > rows || take > rows - first ||
                        !take || take > bulk_offsets.size())
                        throw Error("Robust bulk backend requested a map outside its public block");
                    basis->visit_pair(bulk_rows[candidate], taps(), block.first + first,
                                      std::span(bulk_offsets).first(take), sink, leaf_stop);
                    bulk_progress[candidate] += take;
                    ++bulk_calls[candidate < nominal.size() ? 0 : 1];
                }, output, leaf_stop);
                check_cancelled(leaf_stop);
                if (!std::all_of(bulk_progress.begin(), bulk_progress.end(), [&](auto extent) { return extent == rows; }))
                    throw Error("Robust bulk backend did not process its complete public block");
                const auto completed = bulk->completed_work();
                const auto expected_values = static_cast<std::uint64_t>(states) * 2 * tap_count *
                    shape.projection_indices.size();
                if (!completed.executed || completed.statistics_blocks < bulk_work.statistics_blocks ||
                    completed.statistics_blocks - bulk_work.statistics_blocks != states ||
                    completed.projected_values < bulk_work.projected_values ||
                    completed.projected_values - bulk_work.projected_values != expected_values)
                    throw Error("Robust bulk backend did not execute its complete public statistics work");
                check_bulk_statistics(output, tap_count);
                bulk_work = completed;
            }
        });
        return bulk_calls;
    }

    void update(State& state, const MappedBasisRow& row, const Pair& pair,
                const std::array<const FineBulkStatistics*, 2>& prepared, unsigned lane,
                StateBlockWork* work, std::stop_token leaf_stop) {
        check_cancelled(leaf_stop);
        advance_state(state, row, *basis, taps(), pair.observations(), prepared, pair.transitions,
                      config.noise_variance, scratch[lane], work, leaf_stop);
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
    void urgent(unsigned lane, std::size_t leaf, std::stop_token leaf_stop) {
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
            update(state, {}, pair, prepared, lane, work, leaf_stop);
            return;
        }
        auto& state = adaptive[index];
        update(state, mapped_row(config, plan, grid, coordinates[state.key.group], state.key, wave.anchor, false),
               pair, {}, lane, observation.record_work ? &adaptive_state_work[index] : nullptr, leaf_stop);
    }

    // Compiled work releases one independent observation. Native work is
    // dispatch-major: each fixed-affine state, then that dispatch's tile fills.
    // The native owner waits only for the states.
    void background(unsigned lane, std::size_t leaf, std::stop_token leaf_stop) {
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
               observation.record_work ? &nominal_state_work[slot][index] : nullptr, leaf_stop);
        {
            std::lock_guard lock(chain_mutex);
            nominal_progress[index] = static_cast<std::uint32_t>(local + 1);
        }
        chain_advanced.notify_all();
    }

    void charge(FineLaneWork& work, const Pair& pair, std::size_t size, std::uint64_t calls,
                std::span<const StateBlockWork> states) {
        add_work(work.basis_rows, pair.observed_rows * 2 * tap_count * size);
        add_work(work.basis_calls, calls);
        add_work(work.projection_modes, pair.modes * 2 * tap_count * size);
        add_work(work.gaussian_updates, pair.extent->count * size * 3 * models);
        for (const auto& state : states) {
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
    void run() {
        extents.clear();
        for (Extent next; plan_pair(next, extents.empty() ? nullptr : &extents.back());) extents.push_back(next);
        if (extents.empty()) return;
        const auto serial = pair_serial;
        observe(ring[serial % ring.size()], extents.front(), stop);
        const bool overlap = !bulk;
        const auto fixed = nominal.size();
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
                    bulk_calls = bulk_statistics_for(pair, stage.stop_token());
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
                charge(adaptive_work, pair, adaptive.size(), bulk ? bulk_calls[1] : pair.basis_calls * adaptive.size(),
                       std::span(adaptive_state_work).first(adaptive.size()));
                if (!overlap) charge(nominal_work, pair, fixed, bulk_calls[0], nominal_state_work[0]);
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
                           nominal_state_work[(serial + local) % ring.size()]);
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
                         std::size_t scratch_limit, std::stop_token stop, ReceiverExecutor* executor) {
    const auto began = observation.record_work ? FineClock::now() : FineClock::time_point{};
    impl_ = std::make_unique<Impl>(config, slot, key, observation, iq, valid, coordinates,
                                   projected_rate, clock_gap, workers, scratch_limit, stop, executor);
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
    result.cache = p.basis->report();
    if (p.bulk) result.bulk = p.bulk->receipt();
    result.times = p.times;
    const auto lane = [&](FineLaneWork& out, const FineLaneWork& work, const std::vector<State>& states) {
        out = work;
        out.bit_scores = bit_scores(states);
        out.retained_states.reserve(states.size());
        for (const auto& state : states) {
            out.retained_states.push_back(state.key);
            if (p.observation.record_work) out.retained_prior_mass += state.prior_mass;
        }
    };
    lane(result.initial_affine, p.nominal_work, p.nominal);
    lane(result.adaptive, p.adaptive_work, p.adaptive);
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
            const auto nominal = bit_scores(p.nominal);
            const auto adaptive = bit_scores(p.adaptive);
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
            score.adaptive_bit_evidence = adaptive;
            for (std::size_t bit = 0; bit < 2; ++bit)
                score.combined_bit_evidence[bit] = log_add(nominal[bit], adaptive[bit]) - log_two;
            score.quiet_energy = p.quiet_energy;
            score.quiet_threshold = p.quiet_threshold;
            score.quiet_modes = p.quiet_modes;
            score.fine_candidates = p.coordinates.size();
            score.affine_blocks = score.adaptive_blocks = p.scored_blocks;
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
                ReceiverExecutor* executor) {
    FineSession session(config, plan, key, whole_slot(plan), iq, {}, coordinates, projected_sample_rate_hz,
                        coarse_clock_gap_ppm, workers, scratch_limit, stop, executor);
    session.advance_to(iq.size());
    session.finish(score);
}

namespace {
constexpr std::size_t maximum_varying_coordinates = 24;
constexpr std::size_t maximum_segmented_timing_coordinates = 8;
constexpr double varying_power = 1. / 16;
constexpr std::array tracking_powers{varying_power, 1. / 256, 1. / 512, 1. / 4096};
constexpr std::size_t maximum_tracking_states = 65;

struct DriftGram {
    // Cholesky factor of I + A^H A/(noise variance * precision).
    double first = 1, second = 1, logdet = 0;
    Complex lower{};

    DriftGram(double g0, double g1, Complex cross, double precision, double variance) {
        const auto scale = 1 / (variance * precision);
        first = std::sqrt(1 + g0 * scale);
        lower = std::conj(cross) * (scale / first);
        second = std::sqrt(1 + g1 * scale - std::norm(lower));
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
};

FrequencyTrackingPlan tracking_plan(const ReceiverConfig& config, std::size_t block_rows,
                                     std::size_t final_rows, bool coherent_gain_blocks = false) {
    const auto bound = config.frequency_bound_hz + config.time_varying_atmospheric_hz;
    if (!coherent_gain_blocks && (config.time_varying_common_doppler_hz * block_rows /
        config.sample_rate_hz <= .25 || !(bound > 0)))
        return {};
    FrequencyTrackingPlan result;
    result.coherent_gain_blocks = coherent_gain_blocks;
    result.fft_rows = std::bit_ceil((coherent_gain_blocks ? 4 : 2) * final_rows);
    const auto bin_hz = config.sample_rate_hz /
        (config.helper_enabled ? 2 * block_rows : result.fft_rows);
    result.step_bins = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(
        bound / (bin_hz * (maximum_tracking_states / 2)))));
    result.step_hz = result.step_bins * bin_hz;
    result.radius = static_cast<std::int64_t>(std::ceil(bound / result.step_hz));
    if (result.radius * result.step_hz >= .5 * config.sample_rate_hz) return {};
    result.states = static_cast<std::size_t>(2 * result.radius + 1);
    return result;
}

using FrequencyWeights = std::array<double, maximum_tracking_states>;
using TrackingLogWeights = std::array<std::array<FrequencyWeights, tracking_powers.size()>, 2>;
struct CoherentGainState {
    ChannelPosterior posterior;
    double log_evidence = 0;
};
using TrackingPosteriors =
    std::array<std::array<std::array<CoherentGainState, maximum_tracking_states>, tracking_powers.size()>, 2>;

double tracking_emission(double direct, double echo, Complex cross, Complex r0, Complex r1,
                         std::size_t taps, double power, double variance) {
    const double precision = taps / power;
    const DriftGram gram(direct, echo, cross, precision, variance);
    const double scaling = 1. / (std::sqrt(precision) * std::sqrt(variance));
    return gram.score(r0 * scaling, r1 * scaling, 1 / variance);
}

double coherent_gain_update(ChannelPosterior& posterior, double direct, double echo, Complex cross,
                            Complex r0, Complex r1, std::size_t taps, double power, double variance,
                            double correlation) {
    const double path_power = power / taps;
    const std::array<double, 2> powers{path_power, path_power};
    const std::array<double, 4> transition{correlation * correlation,
        1 - correlation * correlation, correlation, 0.};
    std::array<Complex, 3 * 2> r{}, mean{};
    std::array<Complex, 3 * 4> g{}, covariance{};
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
    std::copy_n(posterior.covariance.begin(), square, covariance.data() + square);
    std::array<double, 3> score{};
    std::array<double, 2> diagnostic{};
    const auto status = triad_gaussian_update_v1(taps, 1, 1, 1, 0, 1,
        reinterpret_cast<const double*>(r.data()), reinterpret_cast<const double*>(g.data()),
        powers.data(), transition.data(), reinterpret_cast<double*>(mean.data()),
        reinterpret_cast<double*>(covariance.data()), score.data(), diagnostic.data());
    if (status || score[0] != 0. || score[2] != 0.)
        throw Error("Robust coherent gain update failed (" + std::to_string(status) + ")");
    std::copy_n(mean.data() + taps, taps, posterior.mean.begin());
    std::copy_n(covariance.data() + square, square, posterior.covariance.begin());
    return score[1];
}

void update_frequency_weights(std::span<const double> evidence, std::span<double> previous, bool constant) {
    if (constant) {
        for (std::size_t i = 0; i < previous.size(); ++i) previous[i] += evidence[i];
        return;
    }
    std::array<double, maximum_tracking_states> next{};
    const auto total = std::accumulate(previous.begin(), previous.end(), -infinity, log_add);
    // Markov prior: 96% stay/local walk and a 4% uniformly distributed
    // reacquisition. Reflect blocked edge steps into the stay mass.
    const auto reset = total + std::log(.04 / previous.size());
    constexpr double neighbor = .12, stay = .72;
    const auto log_neighbor = std::log(neighbor);
    const auto log_stay = std::log(stay);
    const auto log_edge = std::log(stay + neighbor);
    for (std::size_t i = 0; i < previous.size(); ++i) {
        auto predicted = log_add(previous[i] + (i == 0 || i + 1 == previous.size() ? log_edge : log_stay), reset);
        if (i) predicted = log_add(predicted, previous[i - 1] + log_neighbor);
        if (i + 1 < previous.size()) predicted = log_add(predicted, previous[i + 1] + log_neighbor);
        if (!std::isfinite(evidence[i])) throw Error("Robust tracked frequency evidence is nonfinite");
        next[i] = predicted + evidence[i];
    }
    std::copy_n(next.begin(), previous.size(), previous.begin());
}

void track_projected_block(std::span<const Complex> templates, std::size_t taps,
                            const ObservationBlock& block, double variance, double fs,
                            const FrequencyTrackingPlan& plan, WorkerScratch& scratch,
                            std::span<Complex> spectrum, TrackingLogWeights& log_weights,
                            TrackingPosteriors* persistent, double gain_correlation,
                            std::stop_token stop) {
    const auto& projection = *block.projection;
    const auto rows = projection.rows();
    TrackingLogWeights evidence{};
    std::array<std::int64_t, maximum_tracking_states> bins{};
    for (std::size_t i = 0; i < plan.states; ++i) {
        const auto half_bins = 2 * rows * plan.step_hz * (static_cast<std::int64_t>(i) - plan.radius) / fs;
        // Distinguish the two sampled Nyquist endpoints. Keep a rounding
        // error within the half-bin lattice even on the merged last block.
        const auto last = static_cast<std::int64_t>(rows) - 1;
        bins[i] = std::clamp(static_cast<std::int64_t>(std::llround(half_bins)), -last, last);
    }
    const auto indices = projection.indices();
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
            const auto residue = (bins[state] % 2 + 2) % 2;
            if (residue != half) continue;
            const auto shift = (bins[state] - half) / 2;
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
                for (std::size_t power = 0; power < tracking_powers.size(); ++power) {
                    evidence[bit][power][state] = tracking_emission(diagonal[0], diagonal[1], cross,
                        correlation[0], correlation[1], taps, tracking_powers[power], variance);
                    if (persistent) {
                        // Correct the deterministic per-state frequency
                        // reference before transporting a complex gain.
                        const auto hz = bins[state] * fs / (2 * rows);
                        const auto phase = std::polar(1., -tau * hz * block.first / fs);
                        auto& posterior = (*persistent)[bit][power][state];
                        const auto value = coherent_gain_update(posterior.posterior, diagonal[0], diagonal[1], cross,
                            correlation[0] * phase, correlation[1] * phase, taps, tracking_powers[power],
                            variance, gain_correlation);
                        // The persistent branch uses its own accumulated
                        // likelihood. Keep its independent gain peer below.
                        posterior.log_evidence += value;
                    }
                }
            }
        }
    }
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::size_t power = 0; power < tracking_powers.size(); ++power)
            update_frequency_weights(std::span(evidence[bit][power]).first(plan.states),
                                     std::span(log_weights[bit][power]).first(plan.states), plan.coherent_gain_blocks);
}

void track_frequency_block(MappedBasis& basis, const MappedBasisRow& map, std::span<const Tap> taps,
                            std::size_t first, std::span<const Complex> observation, double variance,
                            const FrequencyTrackingPlan& plan,
                            const modem::detail::search_fft::DoublePlan& fft, WorkerScratch& scratch,
                            std::span<Complex> spectrum, TrackingLogWeights& log_weights,
                            const ObservationBlock* projected, double sample_rate, TrackingPosteriors* persistent,
                            double gain_correlation, std::stop_token stop) {
    const auto rows = observation.size();
    const auto columns = 2 * taps.size();
    auto templates = std::span(scratch.raw).first(columns * rows);
    // Tracking uses a compact block, so this render is always one cache request.
    basis.render_pair(map, taps, first, std::span(scratch.offsets).first(rows), templates, stop);
    if (projected) {
        track_projected_block(templates, taps.size(), *projected, variance, sample_rate, plan,
                              scratch, spectrum, log_weights, persistent, gain_correlation, stop);
        return;
    }
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
        // A constant finite Gaussian gain model for this block. Frequency
        // states change only its phase, so all states share the same Gram.
        for (std::size_t power = 0; power < tracking_powers.size(); ++power) {
            const double precision = taps.size() / tracking_powers[power];
            const DriftGram gram(direct_energy, echo_energy, cross, precision, variance);
            const double scaling = 1. / (std::sqrt(precision) * std::sqrt(variance));
            FrequencyWeights evidence{};
            for (std::size_t i = 0; i < plan.states; ++i) {
                const auto signed_bin = (static_cast<std::int64_t>(i) - plan.radius) *
                    static_cast<std::int64_t>(plan.step_bins);
                const auto bin = static_cast<std::size_t>(signed_bin < 0 ?
                    signed_bin + static_cast<std::int64_t>(plan.fft_rows) : signed_bin);
                const auto r0 = spectrum[column * plan.fft_rows + bin];
                const auto r1 = taps.size() == 2 ? spectrum[(column + 1) * plan.fft_rows + bin] : Complex{};
                evidence[i] = gram.score(r0 * scaling, r1 * scaling, 1 / variance);
                if (persistent) {
                    const auto hz = signed_bin * sample_rate / plan.fft_rows;
                    const auto phase = std::polar(1., -tau * hz * first / sample_rate);
                    auto& posterior = (*persistent)[bit][power][i];
                    posterior.log_evidence += coherent_gain_update(posterior.posterior, direct_energy, echo_energy, cross,
                        r0 * phase, r1 * phase, taps.size(), tracking_powers[power], variance, gain_correlation);
                }
            }
            update_frequency_weights(std::span(evidence).first(plan.states),
                                     std::span(log_weights[bit][power]).first(plan.states), plan.coherent_gain_blocks);
        }
    }
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
        append(1 / (3 * geometry(slot.format).carrier_spacing_hz), 0);
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

VaryingFinePlan varying_fine_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                                 bool project_line_atom, bool coherent_gain_blocks) {
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
    add(final + fft, sizeof(Complex));       // observation + scratch
    if (project_line_atom) add(final, sizeof(Complex));
    std::size_t geometry_count = 0;
    (void)varying_bases(config, slot, geometry_count);
    const auto states = maximum_varying_coordinates * geometry_count;
    const auto active = std::min<std::size_t>(workers, states);
    const auto mapped = std::min(maximum_basis_rows, final);
    auto tracking_config = config;
    tracking_config.helper_enabled |= project_line_atom;
    auto track = final <= maximum_basis_rows ?
        tracking_plan(tracking_config, block, final, coherent_gain_blocks) : FrequencyTrackingPlan{};
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
    if (!coherent_gain_blocks) add(states, sizeof(State) + sizeof(MappedBasisRow));
    add(active, sizeof(WorkerScratch));
    add(checked_multiply(active, 4 * final + fft + (final > mapped ? 4 * mapped : 0)), sizeof(Complex));
    add(checked_multiply(active, mapped), sizeof(double));
    add(1, MappedBasis::workspace_bytes(config, slot, mapped, static_cast<unsigned>(active)));
    if (track.states) {
        add(track.fft_rows, sizeof(Complex));
        add(maximum_varying_coordinates + maximum_segmented_timing_coordinates,
            sizeof(TrackingLogWeights) + sizeof(MappedBasisRow));
        if (coherent_gain_blocks)
            add(maximum_varying_coordinates + maximum_segmented_timing_coordinates, sizeof(TrackingPosteriors));
        add(checked_multiply(active, 4 * track.fft_rows), sizeof(Complex));
    }
    add(1, 64 * 1024);
    return {block, bytes, coherent_gain_blocks ? "coherent_gain_blocks" :
            (residual * final / fs > .25 ? "short_block_partial_bandwidth" : "short_block"),
            track.states != 0, coherent_gain_blocks};
}

void score_varying_fine(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                        const VaryingFinePlan& plan, std::span<const Complex> iq,
                        std::span<const FineCoordinate> selected, std::span<const FineCoordinate> segmented_timing,
                        double projected_rate, double clock_gap,
                        unsigned workers, ReceiverSlotScore& score, std::stop_token stop,
                        ReceiverExecutor& executor, const LineAtom* line) {
    check_cancelled(stop);
    if (!plan.block_rows || selected.empty() || selected.size() > maximum_varying_coordinates ||
        segmented_timing.size() > maximum_segmented_timing_coordinates ||
        iq.size() != slot.stop_sample - slot.first_sample)
        throw Error("varying fine scorer requires its bounded public slot and coordinates");
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
    const auto active = std::min<std::size_t>(workers, selected.size() * model_count);
    MappedBasis basis(config, slot, key, std::min(final_rows, maximum_basis_rows), static_cast<unsigned>(active));
    std::vector<WorkerScratch> scratch;
    scratch.reserve(active);
    for (std::size_t lane = 0; lane < active; ++lane) scratch.emplace_back(final_rows, fft_rows);
    std::vector<MappedBasisRow> mapped;
    mapped.reserve(selected.size());
    for (const auto& coordinate : selected)
        mapped.push_back(mapped_row(config, slot, grid, coordinate, {}, 0, true));
    auto tracking_config = config;
    tracking_config.helper_enabled |= line != nullptr;
    const auto track = plan.track_frequency ?
        tracking_plan(tracking_config, block_rows, final_rows, plan.coherent_gain_blocks) : FrequencyTrackingPlan{};
    std::optional<modem::detail::search_fft::DoublePlan> tracking_fft;
    std::vector<TrackingLogWeights> tracking_weights;
    std::vector<TrackingPosteriors> tracking_posteriors;
    std::vector<FineCoordinate> tracking_coordinates;
    std::vector<std::vector<Complex>> tracking_scratch;
    std::vector<MappedBasisRow> tracking_maps;
    if (track.states) {
        tracking_fft.emplace(track.fft_rows, stop);
        tracking_coordinates.reserve(selected.size() + segmented_timing.size());
        const auto append = [&](FineCoordinate coordinate) {
            coordinate.frequency_hz = 0;
            if (std::ranges::find(tracking_coordinates, coordinate) == tracking_coordinates.end())
                tracking_coordinates.push_back(coordinate);
        };
        for (const auto& coordinate : selected) append(coordinate);
        for (const auto& coordinate : segmented_timing) append(coordinate);
        tracking_weights.resize(tracking_coordinates.size());
        const auto prior = -std::log(static_cast<double>(track.states * tracking_coordinates.size() * tracking_powers.size()));
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
                                state.posterior.covariance[tap * taps + tap] = tracking_powers[power] / taps;
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
                channel.covariance[tap * taps + tap] = varying_power / taps;
    }
    std::vector<Complex> observation_scratch(fft_rows);
    std::vector<Complex> line_storage(line ? final_rows : 0);
    ObservationBlock block;
    block.observed = true;
    block.projected.reserve(final_rows);
    std::uint64_t rendered_rows = 0;
    double previous_center = 0;
    for (std::size_t first = 0; first < iq.size();) {
        check_cancelled(stop);
        const auto terminal = iq.size() - first < 2 * block_rows;
        const auto& projection = terminal ? final_projection : block_projection;
        const auto rows = projection.rows();
        block.first = first;
        block.projection = &projection;
        const auto center = (static_cast<double>(first) + (rows - 1) * .5) / config.sample_rate_hz;
        block.elapsed = first ? center - previous_center : 0;
        previous_center = center;
        // Diffuse Gaussian amplitude correlations decay with squared lag.
        // The first-order approximation retains the correct block-step
        // correlation without keeping any realized channel history.
        const auto gain_width = config.random_doppler_width_hz > 0 ? config.random_doppler_width_hz : .35;
        const auto gain_lag = std::numbers::pi * gain_width * block.elapsed;
        const auto gain_correlation = std::exp(-.5 * gain_lag * gain_lag);
        const auto raw = iq.subspan(first, rows);
        block.projected.assign(raw.begin(), raw.end());
        projection.apply(block.projected, observation_scratch, stop);
        block.projected.resize(projection.modes());
        if (line) {
            block.line = project_line(*line, projection, first, config.sample_rate_hz,
                std::span(line_storage).first(rows), observation_scratch, stop);
            remove_line(block.projected, block.line);
        }
        std::array<double, 4 * 2 * models> transitions{};
        const auto coherence = block_rows / config.sample_rate_hz;
        for (std::size_t model = 0; model < models; ++model) {
            const double exponent = -block.elapsed / (coherence * (1 << model));
            transitions[4 * model] = std::exp(2 * exponent);
            transitions[4 * model + 1] = -std::expm1(2 * exponent);
            transitions[4 * model + 2] = std::exp(exponent);
        }
        executor.parallel(slot.index, states.size() + tracking_weights.size(), static_cast<unsigned>(active), stop,
            [&](unsigned lane, std::size_t candidate, std::stop_token leaf_stop) {
                if (candidate >= states.size()) {
                    const auto index = candidate - states.size();
                    // One short interior echo with a free gain. A stable
                    // geometry term and the original dynamic gains remain in
                    // the other half of this public likelihood mixture.
                    const auto delay = config.spread_delay_seconds * (plan.coherent_gain_blocks ? 1. : .5);
                    const auto offset = plan.coherent_gain_blocks ? config.spread_doppler_hz : 0.;
                    const std::array<Tap, 2> paths{{{0, 0}, {delay, offset}}};
                    track_frequency_block(basis, tracking_maps[index], std::span(paths).first(delay || offset ? 2 : 1),
                        first, raw, config.noise_variance, track, *tracking_fft, scratch[lane],
                        tracking_scratch[lane], tracking_weights[index], tracking_config.helper_enabled ? &block : nullptr,
                        config.sample_rate_hz, tracking_posteriors.empty() ? nullptr : &tracking_posteriors[index],
                        gain_correlation, leaf_stop);
                    return;
                }
                const auto& paths = bases[candidate % model_count];
                advance_state(states[candidate], mapped[candidate / model_count], basis,
                    std::span(paths.taps).first(paths.count), std::span(&block, 1), {},
                    transitions, config.noise_variance, scratch[lane], nullptr, leaf_stop, varying_power);
            });
        for (std::size_t model = 0; !plan.coherent_gain_blocks && model < model_count; ++model)
            add_work(rendered_rows, checked_multiply(rows, 2 * bases[model].count * selected.size()));
        if (track.states)
            add_work(rendered_rows, checked_multiply(rows,
                2 * (config.spread_delay_seconds || (plan.coherent_gain_blocks && config.spread_doppler_hz) ? 2 : 1) *
                tracking_coordinates.size()));
        first += rows;
        ++score.varying_blocks;
    }
    if (!plan.coherent_gain_blocks && !std::ranges::any_of(states, [](const State& state) { return state.nonzero_basis; })) {
        score.varying_status = "no_observed_varying_basis";
        return;
    }
    score.varying_candidates = selected.size();
    score.varying_block_rows = block_rows;
    score.varying_basis_rows = rendered_rows;
    score.varying_bit_evidence = plan.coherent_gain_blocks ?
        std::array<double, 2>{-infinity, -infinity} : bit_scores(states);
    if (track.states) {
        // The same selected full-grid mass is applied by the caller. Undo
        // that factor locally for this branch's separate timing prior.
        const auto log_adjustment = std::log(static_cast<double>(tracking_coordinates.size())) -
            std::log(static_cast<double>(selected.size())) +
            std::log1p(2 * std::ceil((config.frequency_bound_hz + config.time_varying_atmospheric_hz) /
                std::min(.01, receiver_coarse_frequency_step(slot.format, config.sample_rate_hz))));
        score.varying_frequency_states = track.states;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            auto total = -infinity;
            for (const auto& atom : tracking_weights)
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
            const auto calibrated = total + log_adjustment;
            score.varying_bit_evidence[bit] = plan.coherent_gain_blocks ? calibrated :
                log_add(score.varying_bit_evidence[bit], calibrated) - log_two;
        }
    }
    score.varying_fine_seconds = seconds_since(began);
}

std::size_t varying_quiet_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    if (!config.time_varying_channel) return 0;
    const BlockCalendar calendar(config, slot);
    const auto lengths = calendar.lengths();
    const auto maximum = *std::ranges::max_element(lengths);
    return checked_add(DftPlan::workspace_bytes(maximum),
        checked_add(checked_multiply(maximum, sizeof(std::size_t)),
                    checked_multiply(maximum + DftPlan::scratch_samples(maximum), sizeof(Complex))));
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
                                        std::span<const Complex> iq, std::stop_token stop) {
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
    if (power <= search_row_count * config.noise_variance * receiver_operational_threshold())
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
    if (!(log_weighted >= receiver_operational_threshold())) return std::nullopt;
    return LineAtom{first, stop_row, center * spacing, log_weighted};
}

void score_varying_quiet(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                         std::span<const Complex> iq, ReceiverSlotScore& score, std::stop_token stop) {
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
    const auto quiet_first = slot.quiet_first_sample - slot.first_sample;
    const auto quiet_stop = slot.quiet_stop_sample - slot.first_sample;
    // The legacy calendar fixes the physical aperture. This extra test uses
    // the same whole, disjoint blocks and only changes public frequency bins.
    // Candidate scores, confidence and retained models never choose a bin.
    for (const auto rows : calendar.lengths()) {
        if (!rows || !projected_block_supported(rows, *bands)) continue;
        Projection projection(rows, *bands, stop);
        std::vector<Complex> projected(rows), scratch(DftPlan::scratch_samples(rows));
        for (std::size_t segment_first = 0; segment_first < quiet_stop;) {
            check_cancelled(stop);
            const auto segment = calendar.segment_length(segment_first);
            auto first = segment_first;
            for (std::size_t block = 0; block < calendar.block_count(segment); ++block) {
                const auto length = calendar.block_length(segment, block);
                if (length == rows && quiet_first <= first && first + rows <= quiet_stop) {
                    std::ranges::copy(iq.subspan(first, rows), projected.begin());
                    projection.apply(projected, scratch, stop);
                    score.varying_quiet_modes = checked_add(score.varying_quiet_modes, projection.modes());
                    for (const auto value : std::span(projected).first(projection.modes()))
                        score.varying_quiet_energy += std::norm(value) / config.noise_variance;
                }
                first += length;
            }
            segment_first += segment;
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

namespace {
constexpr std::size_t maximum_drift_fft_rows = 262'144;
// Per retained timing atom: at most four transform rows per slope (two
// bits and two paths). Bound failure and noise slots as well as positives.
constexpr std::size_t maximum_drift_fft_rows_per_atom = 2 * 1024 * 1024;
constexpr std::size_t maximum_drift_timing_atoms = 8;
constexpr std::size_t minimum_drift_segments = 8;
constexpr std::int64_t maximum_drift_proposal_slope_radius = 128;
constexpr std::int64_t maximum_drift_proposal_frequency_radius = 512;

template<class T>
struct DriftScratch {
    std::array<std::vector<Complex>, 4> products;
    std::vector<std::complex<T>> frequency;
    std::vector<std::complex<T>> segment_spectrum;
    std::vector<double> segment_power;
    std::vector<double> segment_centers;
    std::vector<std::int64_t> segment_offsets;
    std::vector<Complex> raw;
    std::vector<Complex> rendered;
    std::vector<double> offsets;
    std::vector<Complex> projected_columns;
    std::vector<Complex> projection_scratch;
};

struct DriftPeak {
    double rank = -infinity;
    std::int64_t slope = 0, center = 0;
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
        std::vector<Complex> scratch(DftPlan::scratch_samples(max_rows()));
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

constexpr std::size_t retained_drift_per_bit = 8;

bool same_timing(const FineCoordinate& a, const FineCoordinate& b) {
    return a.epoch_offset_seconds == b.epoch_offset_seconds && a.arrival_seconds == b.arrival_seconds &&
           a.clock_ppm == b.clock_ppm;
}

std::size_t fft_bin(std::int64_t signed_bin, std::size_t fft_rows) {
    return static_cast<std::size_t>(signed_bin < 0 ? signed_bin + static_cast<std::int64_t>(fft_rows) : signed_bin);
}

// Match a linear ridge through short coherent spectra. This ranking only
// chooses exact slope atoms of the fixed full-slot likelihood below. Noise
// maxima get no posterior mass credit for discarded alternatives.
struct DriftProposals {
    std::vector<std::int64_t> slopes;
    double rank = 0;
};

template<class T, class Fft>
DriftProposals drift_slope_proposals(
        DriftScratch<T>& scratch, const DriftPlan& plan, const Fft& fft, double fs, double normalized, std::size_t taps,
        const std::array<std::array<double, 2>, 2>& energy, std::size_t drift_segments,
        std::size_t slope_budget, std::stop_token stop) {
    DriftProposals proposals;
    auto& result = proposals.slopes;
    result.reserve(slope_budget);
    const auto append = [&](std::int64_t slope) {
        if (result.size() < slope_budget && std::abs(slope) <= plan.slope_radius &&
            std::ranges::find(result, slope) == result.end()) result.push_back(slope);
    };
    append(0);
    if (2 * plan.slope_radius + 1 <= static_cast<std::int64_t>(slope_budget)) {
        for (auto slope = -plan.slope_radius; slope <= plan.slope_radius; ++slope) append(slope);
        return proposals;
    }

    const auto fft_rows = std::bit_ceil(2 * ((plan.rows + drift_segments - 1) / drift_segments));
    const double frequency_step = fs / fft_rows;
    const auto& products = scratch.products;
    const auto columns = 2 * taps;
    scratch.segment_spectrum.resize(fft_rows);
    scratch.segment_power.assign(2 * drift_segments * fft_rows, 0.);
    auto& centers = scratch.segment_centers;
    centers.resize(drift_segments);
    for (std::size_t segment = 0; segment < drift_segments; ++segment) {
        const auto first = plan.rows * segment / drift_segments;
        const auto stop_row = plan.rows * (segment + 1) / drift_segments;
        centers[segment] = (plan.first + (plan.sample_stride - 1) * .5) / (fs * plan.sample_stride) +
            (first + stop_row - 1) * .5 / fs - plan.slope_reference_seconds;
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
    const auto nominal_slope_stride = std::max<std::int64_t>(1, std::llround(frequency_step /
        (plan.slope_step * std::max(std::abs(centers.front()), std::abs(centers[drift_segments - 1])))));
    const auto slope_stride = std::max(nominal_slope_stride,
        1 + (plan.slope_radius - 1) / maximum_drift_proposal_slope_radius);
    const auto bin_radius = std::min(static_cast<std::int64_t>(fft_rows / 2 - 1),
        static_cast<std::int64_t>(std::ceil(plan.frequency_radius * plan.frequency_step / frequency_step)));
    const auto bin_stride = std::max<std::int64_t>(1,
        1 + (bin_radius - 1) / maximum_drift_proposal_frequency_radius);
    struct Ridge { double score = -infinity; std::int64_t slope = 0, center = 0; };
    constexpr std::size_t retained = 4;
    std::array<std::array<Ridge, retained>, 2> best{};
    auto consider = [&](std::int64_t slope) {
        check_cancelled(stop);
        auto& offset = scratch.segment_offsets;
        offset.resize(drift_segments);
        for (std::size_t s = 0; s < drift_segments; ++s)
            offset[s] = std::llround(slope * plan.slope_step * centers[s] / frequency_step);
        std::array<Ridge, 2> score{};
        for (auto center = -bin_radius; center <= bin_radius; center += bin_stride) {
            std::array<double, 2> evidence{};
            for (std::size_t segment = 0; segment < drift_segments; ++segment) {
                const auto signed_bin = center + offset[segment];
                if (std::abs(signed_bin) >= static_cast<std::int64_t>(fft_rows / 2)) continue;
                const auto bin = fft_bin(signed_bin, fft_rows);
                for (std::size_t bit = 0; bit < 2; ++bit)
                    evidence[bit] += scratch.segment_power[(bit * drift_segments + segment) * fft_rows + bin];
            }
            for (std::size_t bit = 0; bit < 2; ++bit)
                if (evidence[bit] > score[bit].score) score[bit] = {evidence[bit], slope, center};
        }
        for (std::size_t bit = 0; bit < 2; ++bit) {
            auto& frontier = best[bit];
            const auto where = std::ranges::find_if(frontier, [&](const auto& peak) {
                return score[bit].score > peak.score;
            });
            if (where == frontier.end()) continue;
            std::move_backward(where, frontier.end() - 1, frontier.end());
            *where = score[bit];
        }
    };
    consider(-plan.slope_radius);
    for (auto slope = -(plan.slope_radius / slope_stride) * slope_stride;
         slope <= plan.slope_radius; slope += slope_stride) {
        if (slope != -plan.slope_radius && slope != plan.slope_radius) consider(slope);
    }
    consider(plan.slope_radius);
    // Adjacent coarse slope hypotheses can differ by many full-aperture
    // bins. Refine the best ridges on the stored spectra before spending
    // an exact dechirp FFT on a nominal neighbor.
    const auto coarse_best = best;
    const auto interpolate_power = [&](std::size_t bit, std::size_t segment, double bin) {
        const auto low = static_cast<std::int64_t>(std::floor(bin));
        if (low <= -static_cast<std::int64_t>(fft_rows / 2) || low + 1 >= static_cast<std::int64_t>(fft_rows / 2))
            return 0.;
        const auto* row = scratch.segment_power.data() + (bit * drift_segments + segment) * fft_rows;
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
} // namespace

DriftPlan drift_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers,
                     bool project_line_atom) {
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
    result.sample_stride = 1 + (result.raw_rows - 1) / (maximum_drift_fft_rows / 2);
    const auto center_bound = config.frequency_bound_hz + config.time_varying_atmospheric_hz;
    const auto edge_bound = center_bound + config.time_varying_common_doppler_hz *
        ((stop - first) / duration);
    // A box sum is a proposal accelerator only. At the public residual
    // edge its coherent gain stays above sinc(pi/4) even in the wide
    // two-scale bank. Exact likelihoods still use every raw sample.
    const auto wide_frequency = wide_varying_frequency_support(config, duration);
    if (result.sample_stride > 1) {
        const auto largest_stride = std::min<double>(result.raw_rows / 2048,
            fs / ((wide_frequency ? 4 : 8) * edge_bound));
        if (largest_stride < result.sample_stride)
            return {.status = "drift_public_bandwidth_exceeds_reduced_proposal_limit"};
        result.sample_stride = static_cast<std::size_t>(largest_stride);
    }
    result.rows = 1 + (result.raw_rows - 1) / result.sample_stride;
    const auto proposal_rate = fs / result.sample_stride;
    result.project_data = config.helper_enabled || project_line_atom;
    const auto render_rows = std::min(maximum_basis_rows, result.raw_rows);
    std::size_t projection_rows = 0;
    if (result.project_data) {
        result.projection_blocks = result.sample_stride == 1 && !project_line_atom ?
            1 : 1 + (result.raw_rows - 1) / std::min(render_rows, round_rows(.25, fs));
        const auto min_rows = result.raw_rows / result.projection_blocks;
        projection_rows = min_rows + (result.raw_rows % result.projection_blocks != 0);
        try {
            const ProjectionBands bands(config, slot, ProjectionBands::Policy::separable_varying);
            if (!projected_block_supported(min_rows, bands) || !projected_block_supported(projection_rows, bands))
                return {.status = "drift_data_and_helper_bands_not_separable"};
        } catch (const Error&) {
            return {.status = "drift_data_and_helper_bands_not_separable"};
        }
    }
    result.nfft = std::bit_ceil(2 * result.rows);
    result.frequency_step = proposal_rate / result.nfft;
    result.frequency_radius = static_cast<std::int64_t>(std::ceil(std::min(fs * .499,
        config.frequency_bound_hz + config.time_varying_atmospheric_hz) / result.frequency_step));
    result.frequency_radius = std::min(result.frequency_radius, static_cast<std::int64_t>(result.nfft / 2 - 1));
    const auto public_radius = std::ceil(2 * config.time_varying_common_doppler_hz * duration);
    if (!std::isfinite(public_radius) || public_radius >= (1ULL << 40))
        return {.status = "drift_public_slope_bound_overflow"};
    result.slope_radius = static_cast<std::int64_t>(public_radius);
    result.slope_budget = std::min<std::size_t>(64, maximum_drift_fft_rows_per_atom / result.nfft);
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
    result.status = 2 * result.slope_radius + 1 > static_cast<std::int64_t>(result.slope_budget) ?
        "linear_drift_ridge_proposals" : "linear_drift";
    const auto active = std::min<std::size_t>(workers, maximum_drift_timing_atoms);
    const auto tile = std::max(render_rows, projection_rows);
    const auto segment_extent = [&](std::size_t count) {
        return count ? std::bit_ceil(2 * ((result.rows + count - 1) / count)) : std::size_t{0};
    };
    const auto fast_fft = segment_extent(result.ridge_segments);
    const auto slow_fft = segment_extent(result.slow_ridge_segments);
    const auto segment_fft = std::max(fast_fft, slow_fft);
    const auto segment_power_rows = std::max(result.ridge_segments * fast_fft, result.slow_ridge_segments * slow_fft);
    const auto fft_value_bytes = config.native_coarse_precision == NativeCoarsePrecision::fp64 ?
        sizeof(Complex) : sizeof(std::complex<float>);
    auto bytes = checked_multiply(result.nfft + fast_fft + slow_fft, fft_value_bytes);
    const auto per_lane = checked_add(
        checked_multiply(4 * result.rows + 4 * (tile + render_rows), sizeof(Complex)),
        checked_add(checked_multiply(4 * result.nfft + segment_fft, fft_value_bytes),
                    checked_multiply(tile + 2 * segment_power_rows, sizeof(double))));
    const auto ridge_metadata = checked_multiply(result.ridge_segments, sizeof(double) + sizeof(std::int64_t));
    bytes = checked_add(bytes, checked_multiply(active, checked_add(per_lane, ridge_metadata)));
    bytes = checked_add(bytes, MappedBasis::workspace_bytes(config, slot, render_rows, static_cast<unsigned>(active)));
    if (result.project_data) {
        const auto scratch = DftPlan::scratch_samples(projection_rows);
        bytes = checked_add(bytes, checked_multiply(2, DftPlan::workspace_bytes(projection_rows)));
        bytes = checked_add(bytes, checked_multiply(2 * projection_rows, sizeof(std::size_t)));
        const auto observation = checked_add(checked_multiply(2, result.raw_rows), scratch);
        bytes = checked_add(bytes, checked_multiply(observation, sizeof(Complex)));
        if (project_line_atom) bytes = checked_add(bytes, checked_multiply(result.raw_rows, sizeof(Complex)));
        // Each admitted lane projects two template columns, reused for
        // each retained bit/hypothesis. No candidate-wide spectra bank.
        bytes = checked_add(bytes, checked_multiply(active,
            checked_multiply(2 * projection_rows + scratch, sizeof(Complex))));
    }
    result.workspace_bytes = checked_add(bytes, 64 * 1024);
    return result;
}

namespace {
template<class T, class Fft>
void score_drift_impl(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                       const DriftPlan& plan, std::span<const Complex> iq,
                       std::span<const FineCoordinate> selected, std::span<const FineCoordinate> epoch_timing,
                       unsigned workers,
                       ReceiverSlotScore& score, std::stop_token stop, ReceiverExecutor& executor, const LineAtom* line) {
    if (!plan.workspace_bytes || selected.empty() || selected.size() > maximum_varying_coordinates ||
        iq.size() != slot.stop_sample - slot.first_sample)
        throw Error("Robust drift scorer requires bounded public timing atoms and observation");
    const auto began = FineClock::now();
    std::vector<FineCoordinate> timing;
    timing.reserve(selected.size() + epoch_timing.size());
    const auto append = [&](const FineCoordinate& coordinate) {
        if (std::none_of(timing.begin(), timing.end(), [&](const auto& other) { return same_timing(coordinate, other); }))
            timing.push_back(coordinate);
    };
    for (const auto& coordinate : selected) append(coordinate);
    for (const auto& coordinate : epoch_timing) append(coordinate);
    const auto active = std::min({static_cast<std::size_t>(workers), timing.size(), maximum_drift_timing_atoms});
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
    std::vector<std::size_t> survivors(timing.size());
    std::iota(survivors.begin(), survivors.end(), 0);
    const auto evaluate = [&](unsigned lane, std::size_t index, bool rank_only, std::stop_token leaf_stop) {
            auto& own = scratch[lane];
            for (auto& product : std::span(own.products).first(2 * taps)) product.assign(plan.rows, {});
            own.frequency.resize(4 * plan.nfft);
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
            // Use the same bounded exact slope budget. A small fast
            // frontier remains even where the slower ridge is more
            // sensitive. No data-selected slope gets extra prior mass.
            const auto fast_budget = slow_fft ? std::max<std::size_t>(3, plan.slope_budget / 4) : plan.slope_budget;
            auto slopes = drift_slope_proposals(own, plan, segment_fft, projected_rate, normalized, taps,
                                                energy, plan.ridge_segments, fast_budget, leaf_stop);
            if (slow_fft) {
                const auto slow = drift_slope_proposals(own, plan, *slow_fft, projected_rate, normalized, taps,
                    energy, plan.slow_ridge_segments, plan.slope_budget - fast_budget + 1, leaf_stop);
                slopes.rank = std::max(slopes.rank, slow.rank);
                for (const auto slope : slow.slopes)
                    if (std::ranges::find(slopes.slopes, slope) == slopes.slopes.end())
                        slopes.slopes.push_back(slope);
            }
            if (rank_only) {
                // All atoms use the same physical variance. Undo scaling to
                // make matched-product ranks comparable between atoms.
                ranks[index] = std::scalbn(slopes.rank, 2 * exponent);
                return;
            }
            std::array<std::array<DriftPeak, retained_drift_per_bit>, 2> peaks{};
            scored_slopes[index] = slopes.slopes.size();
            for (const auto slope : slopes.slopes) {
                check_cancelled(leaf_stop);
                const double rate = slope * plan.slope_step;
                const double delta = plan.sample_stride / config.sample_rate_hz;
                const double first_t = (plan.first + (plan.sample_stride - 1) * .5) /
                    config.sample_rate_hz - plan.slope_reference_seconds;
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
                for (auto bin = -plan.frequency_radius; bin <= plan.frequency_radius; ++bin) {
                    if (!(static_cast<std::uint64_t>(bin) & 4095U)) check_cancelled(leaf_stop);
                    const auto offset = fft_bin(bin, plan.nfft);
                    for (std::size_t bit = 0; bit < 2; ++bit) {
                        const auto column = bit * taps;
                        const auto r0 = static_cast<Complex>(own.frequency[column * plan.nfft + offset]);
                        const auto r1 = taps == 2 ?
                            static_cast<Complex>(own.frequency[(column + 1) * plan.nfft + offset]) : Complex{};
                        auto& frontier = peaks[bit];
                        const auto rank = std::norm(r0) + std::norm(r1);
                        const auto where = std::ranges::find_if(frontier, [&](const DriftPeak& peak) {
                            return rank > peak.rank;
                        });
                        if (where != frontier.end()) {
                            std::move_backward(where, frontier.end() - 1, frontier.end());
                            *where = {rank, slope, bin};
                        }
                    }
                }
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
                    own.projection_scratch.resize(DftPlan::scratch_samples(max_rows));
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
    };
    if (survivors.size() > maximum_drift_timing_atoms) {
        executor.parallel(slot.index, timing.size(), static_cast<unsigned>(active), stop,
            [&](unsigned lane, std::size_t index, std::stop_token leaf_stop) {
                evaluate(lane, index, true, leaf_stop);
            });
        std::partial_sort(survivors.begin(), survivors.begin() + maximum_drift_timing_atoms, survivors.end(),
            [&](std::size_t a, std::size_t b) { return ranks[a] != ranks[b] ? ranks[a] > ranks[b] : a < b; });
        survivors.resize(maximum_drift_timing_atoms);
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
    score.drift_timing_atoms = survivors.size();
    score.drift_slopes = std::accumulate(scored_slopes.begin(), scored_slopes.end(), std::size_t{});
    score.drift_frequency_bins = count;
    score.drift_observation_rows = plan.raw_rows;
    score.drift_proposal_stride = plan.sample_stride;
    score.drift_seconds = seconds_since(began);
}
} // namespace

void score_drift_fine(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                       const DriftPlan& plan, std::span<const Complex> iq,
                       std::span<const FineCoordinate> selected, std::span<const FineCoordinate> epoch_timing,
                       unsigned workers, ReceiverSlotScore& score, std::stop_token stop,
                       ReceiverExecutor& executor, const LineAtom* line) {
    namespace fft = modem::detail::search_fft;
    if (config.native_coarse_precision == NativeCoarsePrecision::fp64)
        score_drift_impl<double, fft::DoublePlan>(config, slot, key, plan, iq, selected, epoch_timing,
                                                workers, score, stop, executor, line);
    else
        score_drift_impl<float, fft::FloatPlan>(config, slot, key, plan, iq, selected, epoch_timing,
                                               workers, score, stop, executor, line);
}

} // namespace datapump::triad::v3::receiver_detail
