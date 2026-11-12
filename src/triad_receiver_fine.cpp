#include "triad_receiver_fine.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_basis.hpp"
#include "search_fft.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
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
    double sample_rate;

    ProjectionBands(const ReceiverConfig& config, const ReceiverSlotPlan& slot)
        : use_exclusions(config.helper_enabled), sample_rate(config.sample_rate_hz) {
        const double excursion = std::abs(config.spread_doppler_hz) +
            3 * config.random_doppler_width_hz + config.delay_motion_doppler_bound_hz;
        const double clock = config.clock_bound_ppm * 1e-6;
        const double common = excursion + config.frequency_bound_hz;
        const double half = geometry(slot.format).occupied_bandwidth_hz * .5 * (1 + clock) + common;
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
                if (std::max(required.lower_hz, band.lower_hz) <=
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
        if (bands.use_exclusions) {
            for (const auto& band : bands.excluded) {
                if (hz >= band.lower_hz - half_bin && hz <= band.upper_hz + half_bin)
                    throw Error("Robust likelihood block cannot resolve DATA and Helper bands");
            }
        }
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
    std::size_t first = 0;
    double elapsed = 0;
    bool observed = true;
};

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
                   double variance, WorkerScratch& scratch, StateBlockWork* work, std::stop_token stop) {
    const auto tap_count = taps.size(), tap_square = tap_count * tap_count;
    std::array<Complex, 2 * 3 * 2> correlations{};
    std::array<Complex, 2 * 3 * 4> grams{};
    std::array<double, 2> powers{1. / tap_count, 1. / tap_count};
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

} // namespace datapump::triad::v3::receiver_detail
