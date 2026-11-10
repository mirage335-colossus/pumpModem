#include "triad_receiver_fine.hpp"
#include "triad_receiver_basis.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <compare>
#include <exception>
#include <functional>
#include <limits>
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
    BlockCalendar(const ReceiverConfig& config, const ReceiverSlotPlan& plan)
        : rows(static_cast<std::size_t>(plan.stop_sample - plan.first_sample)),
          block_rows(round_rows(.25, config.sample_rate_hz)),
          segment_rows(round_rows(.5, config.sample_rate_hz)) {
        if (!rows || std::min(rows, segment_rows) > maximum_segment_rows)
            throw Error("Robust receiver segment exceeds 1048576 samples");
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
        if (rows >= segment_rows) add(segment_rows);
        add(rows % segment_rows);
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

class Projection {
public:
    Projection(std::size_t rows, const ProjectionBands& bands, std::stop_token stop)
        : rows_(rows), normalization_(1 / std::sqrt(static_cast<double>(rows))) {
        const double spacing = bands.sample_rate / static_cast<double>(rows);
        const double half_bin = spacing / 2;
        indices_.reserve(rows);
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
            indices_.push_back(index);
        }
        if (indices_.empty()) throw Error("Robust likelihood block has no observed DATA modes");
        if (indices_.size() != 1 || indices_.front() != 0) plan_.emplace(rows, stop);
    }

    std::size_t rows() const { return rows_; }
    std::size_t modes() const { return indices_.size(); }

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

class FineWorkers {
public:
    explicit FineWorkers(unsigned workers) {
        threads_.reserve(workers - 1);
        try {
            for (unsigned worker = 1; worker < workers; ++worker)
                threads_.emplace_back([this, worker] { run(worker); });
        } catch (...) {
            close();
            throw;
        }
    }

    ~FineWorkers() { close(); }

    void dispatch(std::size_t count, std::function<void(unsigned, std::size_t)> function) {
        {
            std::lock_guard lock(mutex_);
            function_ = std::move(function);
            count_ = count;
            next_ = 0;
            failure_ = {};
            failed_ = false;
            pending_ = threads_.size();
            ++generation_;
        }
        ready_.notify_all();
        consume(0);
        std::unique_lock lock(mutex_);
        done_.wait(lock, [&] { return !pending_; });
        function_ = {};
        if (failure_) std::rethrow_exception(failure_);
    }

private:
    void close() {
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
        }
        ready_.notify_all();
        for (auto& thread : threads_) thread.join();
        threads_.clear();
    }

    void consume(unsigned worker) {
        try {
            while (!failed_.load(std::memory_order_relaxed)) {
                const auto index = next_.fetch_add(1, std::memory_order_relaxed);
                if (index >= count_) break;
                function_(worker, index);
            }
        } catch (...) {
            failed_.store(true, std::memory_order_relaxed);
            std::lock_guard lock(mutex_);
            if (!failure_) failure_ = std::current_exception();
        }
    }

    void run(unsigned worker) {
        std::unique_lock lock(mutex_);
        std::uint64_t seen = 0;
        while (true) {
            ready_.wait(lock, [&] { return closing_ || seen != generation_; });
            if (closing_) return;
            seen = generation_;
            lock.unlock();
            consume(worker);
            lock.lock();
            if (!--pending_) done_.notify_one();
        }
    }

    execution::Mutex mutex_;
    execution::Condition ready_, done_;
    std::function<void(unsigned, std::size_t)> function_;
    std::vector<execution::Task> threads_;
    std::atomic<std::size_t> next_{0};
    std::atomic<bool> failed_{false};
    std::exception_ptr failure_;
    std::size_t count_ = 0, pending_ = 0;
    std::uint64_t generation_ = 0;
    bool closing_ = false;
};

struct StateKey {
    unsigned group = 0;
    std::int64_t delay = 0;
    int rate = 0;
    auto operator<=>(const StateKey&) const = default;
};

struct ChannelPosterior {
    std::array<Complex, 2> mean{};
    std::array<Complex, 4> covariance{};
};

struct State {
    StateKey key;
    std::array<double, components> weights{};
    std::array<ChannelPosterior, components> posterior{};
    bool nonzero_basis = false;
};

double rank(const std::array<double, components>& weights) {
    return std::accumulate(weights.begin(), weights.end(), -infinity, log_add);
}

template<typename T>
void retain(std::vector<T>& states, std::size_t count) {
    if (states.size() <= count) return;
    std::partial_sort(states.begin(), states.begin() + static_cast<std::ptrdiff_t>(count), states.end(),
        [](const T& a, const T& b) {
            const auto first = rank(a.weights), second = rank(b.weights);
            return first != second ? first > second : a.key < b.key;
        });
    states.resize(count);
    std::sort(states.begin(), states.end(), [](const T& a, const T& b) { return a.key < b.key; });
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

    std::vector<State> initial(std::size_t coordinates, std::size_t taps) const {
        const auto count = coordinates * 3 * (radius ? 3 : 1);
        if (!count || count > maximum_proposals)
            throw Error("Robust initial timing bank exceeds the proposal cap");
        const auto log_prior = -std::log(static_cast<double>(count)) - std::log(static_cast<double>(models));
        std::vector<State> states;
        states.reserve(count);
        const auto append = [&](unsigned group, int delay, int rate) {
            State state;
            state.key = {group, delay, rate};
            state.weights.fill(log_prior);
            for (auto& channel : state.posterior)
                for (std::size_t tap = 0; tap < taps; ++tap)
                    channel.covariance[tap * taps + tap] = 1. / static_cast<double>(taps);
            states.push_back(state);
        };
        for (unsigned group = 0; group < coordinates; ++group)
            for (int delay = -1; delay <= 1; ++delay) {
                if (radius) append(group, delay, -radius);
                append(group, delay, 0);
                if (radius) append(group, delay, radius);
            }
        return states;
    }
};

struct TransitionEdge {
    StateKey target;
    std::size_t source;
    double log_probability;
    Complex phase;
};

struct TransitionTarget {
    StateKey key;
    std::array<double, components> weights;
    std::size_t first_edge, stop_edge;
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
                           std::uint64_t& edges_scored, std::stop_token stop) {
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
        } while (i < edges.size() && edges[i].target == target.key);
        target.stop_edge = i;
        targets.push_back(target);
    }
    retain(targets, maximum_proposals);

    std::vector<State> result;
    result.reserve(targets.size());
    for (const auto& target : targets) {
        check_cancelled(stop);
        State state;
        state.key = target.key;
        state.weights = target.weights;
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
};

struct WorkerScratch {
    std::vector<Complex> raw, chunk, fft;
    std::vector<double> offsets;

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
                          const TimingGrid& grid, const FineCoordinate& coordinate, const StateKey& key,
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

void advance_state(State& state, const MappedBasisRow& row, MappedBasis& basis,
                   std::span<const Tap> taps, std::span<const ObservationBlock> blocks,
                   double variance, WorkerScratch& scratch, std::stop_token stop) {
    const auto tap_count = taps.size(), tap_square = tap_count * tap_count;
    std::array<Complex, 2 * 3 * 2> correlations{};
    std::array<Complex, 2 * 3 * 4> grams{};
    std::array<double, 4 * 2 * models> transitions{};
    std::array<double, 2> powers{1. / tap_count, 1. / tap_count};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        sufficient_statistics(basis, row, taps, blocks[i], variance, scratch,
            std::span(correlations).subspan(i * 3 * tap_count, 3 * tap_count),
            std::span(grams).subspan(i * 3 * tap_square, 3 * tap_square), state.nonzero_basis, stop);
        for (std::size_t model = 0; model < models; ++model) {
            const auto offset = 4 * (i * models + model);
            const auto exponent = -blocks[i].elapsed / coherence_seconds[model];
            const auto decay = std::exp(exponent);
            transitions[offset] = decay * decay;
            transitions[offset + 1] = -std::expm1(2 * exponent);
            transitions[offset + 2] = decay;
        }
    }

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

std::size_t fine_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& plan, unsigned workers) {
    const BlockCalendar calendar(config, plan);
    // Reject unsupported public support before leasing a slot.
    const ProjectionBands validated(config, plan);
    (void)validated;
    const auto lengths = calendar.lengths();
    const auto max_rows = *std::max_element(lengths.begin(), lengths.end());
    std::size_t bytes = 64 * 1024, fft_rows = 0;
    const auto add = [&](std::size_t count, std::size_t element) {
        bytes = checked_add(bytes, checked_multiply(count, element));
    };
    for (const auto rows : lengths) {
        if (!rows) continue;
        bytes = checked_add(bytes, DftPlan::workspace_bytes(rows));
        add(rows, sizeof(std::size_t));
        fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
    }
    add(fft_rows + 2 * max_rows, sizeof(Complex));
    // Both lane states can coexist with the predicted/merged adaptive states.
    add(3 * maximum_proposals, sizeof(State));
    add(2 * maximum_states * (2 * maximum_rate_radius + 1),
        sizeof(TransitionTarget) + sizeof(TransitionEdge));
    const auto active = useful_fine_workers(workers);
    const auto mapped_rows = std::min(max_rows, maximum_basis_rows);
    add(active, checked_add(checked_multiply(fft_rows + 4 * max_rows +
        (max_rows > mapped_rows ? 4 * mapped_rows : 0), sizeof(Complex)),
        checked_multiply(mapped_rows, sizeof(double))));
    return checked_add(bytes, MappedBasis::workspace_bytes(config, plan, mapped_rows, active));
}

void score_fine(const ReceiverConfig& config, const ReceiverSlotPlan& plan, const Crypto& key,
                std::span<const Complex> iq, std::span<const FineCoordinate> coordinates,
                double projected_sample_rate_hz, double coarse_clock_gap_ppm,
                unsigned workers, ReceiverSlotScore& score, std::stop_token stop) {
    const BlockCalendar calendar(config, plan);
    const TimingGrid grid(projected_sample_rate_hz, coarse_clock_gap_ppm);
    const std::array<Tap, 2> paths{{{0, 0}, {config.spread_delay_seconds, config.spread_doppler_hz}}};
    const auto taps = std::span(paths).first(paths[1].delay_seconds || paths[1].doppler_hz ? 2 : 1);
    auto nominal = grid.initial(coordinates.size(), taps.size()), adaptive = nominal;

    const auto lengths = calendar.lengths();
    std::vector<Projection> projections;
    projections.reserve(lengths.size());
    const ProjectionBands bands(config, plan);
    std::size_t fft_rows = 0, max_rows = 0;
    for (const auto rows : lengths) {
        if (!rows) continue;
        projections.emplace_back(rows, bands, stop);
        fft_rows = std::max(fft_rows, DftPlan::scratch_samples(rows));
        max_rows = std::max(max_rows, rows);
    }
    std::vector<Complex> observation_scratch(fft_rows);
    const auto active = useful_fine_workers(workers);
    MappedBasis basis(config, plan, key, std::min(max_rows, maximum_basis_rows), active);
    std::vector<WorkerScratch> scratch;
    scratch.reserve(active);
    for (unsigned worker = 0; worker < active; ++worker) scratch.emplace_back(max_rows, fft_rows);
    FineWorkers executor(active);

    std::array<ObservationBlock, 2> observations;
    for (auto& block : observations) block.projected.reserve(max_rows);
    long double first_anchor = 0, previous_anchor = 0, previous_last = 0, previous_block_center = 0;
    const auto fs = static_cast<long double>(config.sample_rate_hz);
    const auto quiet_first = plan.quiet_first_sample - plan.first_sample;
    const auto quiet_stop = plan.quiet_stop_sample - plan.first_sample;
    std::size_t first = 0;
    bool nominal_nonzero = false, adaptive_nonzero = false;
    while (first < iq.size()) {
        check_cancelled(stop);
        const auto segment_rows = std::min(calendar.segment_rows, iq.size() - first);
        const auto anchor = (static_cast<long double>(first) + (segment_rows - 1) / 2.L) / fs;
        if (!first) first_anchor = anchor;
        else {
            const auto boundary = (previous_last + static_cast<long double>(first) / fs) / 2 - previous_anchor;
            adaptive = predict(adaptive, grid, static_cast<double>(anchor - previous_anchor),
                static_cast<double>(boundary), config.data_if_hz, taps.size(),
                score.adaptive_transition_edges, stop);
        }
        score.adaptive_peak_states = std::max(score.adaptive_peak_states, adaptive.size());

        auto block_first = first;
        const auto block_count = calendar.block_count(segment_rows);
        for (std::size_t base = 0; base < block_count; base += observations.size()) {
            const auto pair = std::min(observations.size(), block_count - base);
            std::uint64_t rows_rendered = 0;
            for (std::size_t block = 0; block < pair; ++block) {
                const auto rows = calendar.block_length(segment_rows, base + block);
                const auto projection = std::find_if(projections.begin(), projections.end(),
                    [&](const Projection& candidate) { return candidate.rows() == rows; });
                if (projection == projections.end()) throw Error("Robust likelihood block has no public projection");
                auto& item = observations[block];
                item.first = block_first;
                item.projection = &*projection;
                const auto raw = iq.subspan(block_first, rows);
                item.projected.assign(raw.begin(), raw.end());
                projection->apply(item.projected, observation_scratch, stop);
                item.projected.resize(projection->modes());
                const auto center = (static_cast<long double>(block_first) + (rows - 1) / 2.L) / fs;
                item.elapsed = block_first ? static_cast<double>(center - previous_block_center) : 0.;
                previous_block_center = center;
                if (quiet_first <= block_first && block_first + rows <= quiet_stop) {
                    score.quiet_modes += projection->modes();
                    score.quiet_energy += finite_energy(item.projected) / config.noise_variance;
                    if (!std::isfinite(score.quiet_energy)) throw Error("Robust quiet energy is nonfinite");
                }
                block_first += rows;
                rows_rendered += rows;
            }
            const auto states = nominal.size() + adaptive.size();
            const auto work = rows_rendered * 2 * taps.size() * states;
            if (work > UINT64_MAX - score.basis_rows) throw Error("Robust basis work counter overflows");
            score.basis_rows += work;
            executor.dispatch(states, [&](unsigned worker, std::size_t index) {
                check_cancelled(stop);
                const bool fixed = index < nominal.size();
                auto& state = fixed ? nominal[index] : adaptive[index - nominal.size()];
                const auto row = mapped_row(config, plan, grid, coordinates[state.key.group], state.key,
                                            fixed ? first_anchor : anchor, fixed);
                advance_state(state, row, basis, taps, std::span(observations).first(pair),
                              config.noise_variance, scratch[worker], stop);
            });
            score.affine_blocks += pair;
            score.adaptive_blocks += pair;
        }
        nominal_nonzero |= std::any_of(nominal.begin(), nominal.end(),
            [](const State& state) { return state.nonzero_basis; });
        adaptive_nonzero |= std::any_of(adaptive.begin(), adaptive.end(),
            [](const State& state) { return state.nonzero_basis; });
        retain(adaptive, maximum_states);
        previous_anchor = anchor;
        previous_last = static_cast<long double>(first + segment_rows - 1) / fs;
        first += segment_rows;
    }

    score.bit_evidence = bit_scores(nominal);
    score.adaptive_bit_evidence = bit_scores(adaptive);
    for (std::size_t bit = 0; bit < 2; ++bit)
        score.combined_bit_evidence[bit] = log_add(score.bit_evidence[bit], score.adaptive_bit_evidence[bit]) - log_two;
    score.consumed_samples = first;
    score.complete = nominal_nonzero && adaptive_nonzero && first == iq.size() &&
        score.affine_blocks && score.adaptive_blocks;
    score.zero_h0_verified = score.affine_blocks && score.adaptive_blocks;
    score.noise_statistics_valid = score.complete && score.all_valid && score.quiet_modes;
    if (score.noise_statistics_valid) score.quiet_threshold = quiet_noise_threshold(score.quiet_modes);
}
} // namespace datapump::triad::v3::receiver_detail
