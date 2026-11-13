#include "ssb_acquisition.hpp"
#include "ssb_folded_preamble.hpp"
#include "ssb_inverse.hpp"
#include "single_carrier_waveform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {

using Complex = std::complex<double>;
constexpr std::size_t word_symbols = 64;
constexpr std::size_t periodic_size = word_symbols * SsbPulseCache::samples_per_symbol;
constexpr unsigned maximum_training_words = 26;
constexpr std::size_t maximum_training_centers = 32768;
constexpr auto tau = 2. * std::numbers::pi;
constexpr double maximum_delay_seconds = .024;
constexpr double maximum_clock_ratio = .001;
constexpr double maximum_frequency_hz = 500.;
constexpr std::size_t maximum_marker_guard = 192;
constexpr std::size_t maximum_marker_rows = 2048;

double tick_difference(std::uint64_t tick, std::uint64_t reference) {
    return tick >= reference ? double(tick - reference) : -double(reference - tick);
}

double wrap(double value, double period) {
    const auto reduced = std::fmod(value, period);
    return reduced < 0. ? reduced + period : reduced;
}

Complex rotation(double frequency, double ticks, double sample_rate) {
    return std::polar(1., tau * std::remainder(frequency * ticks / sample_rate, 1.));
}

double energy(std::span<const Complex> samples) {
    double result = 0.;
    for (const auto v : samples) result += std::norm(v);
    return result;
}

Complex inner(std::span<const Complex> left, std::span<const Complex> right) {
    Complex result{};
    for (std::size_t i = 0; i < left.size(); ++i) result += std::conj(left[i]) * right[i];
    return result;
}

// A cached filtered pulse folded against the exactly repeating public word.
// The tick moment carries the filter's effect on clock and frequency slopes.
struct PeriodicWave {
    SsbPeriodicWave grid;

    std::pair<Complex, Complex> sample(double symbol) const {
        const auto p = wrap(symbol, word_symbols) * SsbPulseCache::samples_per_symbol;
        const auto i = static_cast<std::size_t>(p) % periodic_size;
        const auto f = p - std::floor(p);
        const auto j = (i + 1) % periodic_size;
        return {grid.values[i] + f * (grid.values[j] - grid.values[i]),
                grid.moments[i] + f * (grid.moments[j] - grid.moments[i])};
    }

    std::pair<Complex, Complex> slope(double symbol) const {
        constexpr auto half_grid = .5 / SsbPulseCache::samples_per_symbol;
        const auto [a, ma] = sample(symbol - half_grid);
        const auto [b, mb] = sample(symbol + half_grid);
        return {double(SsbPulseCache::samples_per_symbol) * (b - a),
                double(SsbPulseCache::samples_per_symbol) * (mb - ma)};
    }

    std::size_t workspace_bytes() const {
        return (grid.values.capacity() + grid.moments.capacity()) * sizeof(Complex);
    }
};

std::array<Complex, word_symbols> public_training_word(const Profile& profile) {
    const SingleCarrierSchedule schedule(profile);
    if (!profile.capacity_mode || schedule.training_count != 2048)
        throw std::invalid_argument("Fast SSB acquisition requires the capacity preamble and marker");
    std::array<Complex, word_symbols> result{};
    for (std::size_t i = 0; i < word_symbols; ++i) result[i] = schedule.training_symbol(i);
    return result;
}

struct Atom {
    double delay;
    double frequency;
    Complex gain{};
    PeriodicWave periodic;
    std::vector<Complex> column;
};

struct FitSession {
    const Profile& profile;
    const SsbPulse& pulse;
    const SsbPeriodicPulse& periodic;
    const SsbAcquisitionRequest& bounds;
    const SsbAcquisitionView view;
    const std::size_t train_index;
    const std::span<const Complex> observed;
    const std::uint64_t reference_tick;
    const double first_relative;
    const double beta;
    const double radius;
    const double word_ticks;
    const double train_energy;
    std::stop_token stop;
    SsbFoldedPreamble proposal;
    double clock = 0.;
    std::vector<Complex> residual, work;
    std::vector<Atom> atoms;

    FitSession(const Profile& p, const SsbPulse& pulse, const SsbPeriodicPulse& periodic,
               const SsbAcquisitionRequest& request,
               SsbAcquisitionView input, std::size_t first, std::size_t count,
               std::span<const Complex> reference, std::stop_token token)
        : profile(p), pulse(pulse), periodic(periodic), bounds(request), view(input), train_index(first),
          observed(input.samples.subspan(first, count)),
          reference_tick(input.first_center_input_tick + (first + count / 2) * input.stride),
          first_relative(tick_difference(request.training_first_center_tick, reference_tick)),
          beta(p.symbol_rate / p.sample_rate), radius(single_carrier_pulse_radius(p)),
          word_ticks(word_symbols / beta), train_energy(energy(observed)), stop(token),
          proposal(reference, p.symbol_rate * 5.5, request.max_abs_doppler_hz),
          residual(observed.begin(), observed.end()), work(count) {
        atoms.reserve(DelayDopplerFit::maximum_atoms);
    }

    double ticks(std::size_t index) const {
        return tick_difference(view.first_center_input_tick + index * view.stride, reference_tick);
    }

    double modulo(double delay) const { return first_relative + wrap(delay - first_relative, word_ticks); }

    void generate(const Atom& a, double delay, double frequency, double c, std::span<Complex> output) const {
        auto phase = rotation(frequency, first_relative, profile.sample_rate);
        const auto phase_step = rotation(frequency, view.stride, profile.sample_rate);
        const auto first = ((1. + c) * first_relative - delay) * beta - radius;
        const auto step = (1. + c) * view.stride * beta;
        for (std::size_t i = 0; i < output.size(); ++i) {
            output[i] = phase * a.periodic.sample(first + i * step).first;
            phase *= phase_step;
        }
    }

    double project(Atom& a, double delay, double frequency, double c) {
        generate(a, delay, frequency, c, work);
        const auto power = energy(work);
        const auto gain = inner(work, residual) / power;
        const auto reduction = std::norm(gain) * power;
        if (!std::isfinite(reduction)) return 0.;
        a.gain = gain;
        return reduction;
    }

    void apply(const Atom& a, double sign) {
        for (std::size_t i = 0; i < residual.size(); ++i) residual[i] += sign * a.gain * a.column[i];
    }

    void refresh(Atom& a) {
        pulse.make_periodic(periodic, clock, a.frequency, a.periodic.grid, stop);
        a.column.resize(observed.size());
        generate(a, a.delay, a.frequency, clock, a.column);
    }

    bool fit_gains() {
        HermitianBand gram;
        gram.reset(atoms.size(), atoms.size() - 1);
        std::vector<Complex> rhs(atoms.size());
        double maximum_diagonal = 0.;
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            if (stop.stop_requested()) return false;
            const auto row = gram.lower_row(i);
            for (std::size_t j = 0; j <= i; ++j) row[j] = inner(atoms[i].column, atoms[j].column);
            maximum_diagonal = std::max(maximum_diagonal, row[i].real());
            rhs[i] = inner(atoms[i].column, observed);
        }
        if (gram.factor(maximum_diagonal * 1e-7, stop) != HermitianBand::FactorResult::complete ||
            !gram.solve_in_place(rhs, stop)) return false;
        std::copy(observed.begin(), observed.end(), residual.begin());
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            atoms[i].gain = rhs[i];
            apply(atoms[i], -1.);
        }
        return true;
    }

    // Project out the atom's complex gain before solving for delay/frequency.
    // Rebuild the frequency-dependent filter only after the local trial.
    void refine(Atom& a, unsigned iterations) {
        auto best = project(a, a.delay, a.frequency, clock);
        const auto original_frequency = a.frequency;
        const auto sample_rate = double(profile.sample_rate);
        for (unsigned iteration = 0; iteration < iterations && !stop.stop_requested(); ++iteration) {
            auto power = 0.;
            Complex cross_delay{}, cross_frequency{}, delay_error{}, frequency_error{};
            double dd = 0., df = 0., ff = 0.;
            auto phase = rotation(a.frequency, first_relative, sample_rate);
            const auto phase_step = rotation(a.frequency, view.stride, sample_rate);
            const auto gain = a.gain;
            for (std::size_t i = 0; i < residual.size(); ++i) {
                const auto t = first_relative + i * double(view.stride);
                const auto u = ((1. + clock) * t - a.delay) * beta - radius;
                const auto [v, moment] = a.periodic.sample(u);
                const auto [slope, unused] = a.periodic.slope(u);
                (void)unused;
                const auto value = phase * v;
                const auto d = -beta * gain * phase * slope;
                const auto f = Complex{0., tau / sample_rate} * gain * phase * (t * v - moment);
                const auto error = residual[i] - gain * value;
                power += std::norm(value);
                cross_delay += std::conj(value) * d;
                cross_frequency += std::conj(value) * f;
                dd += std::norm(d);
                df += (std::conj(d) * f).real();
                ff += std::norm(f);
                delay_error += std::conj(d) * error;
                frequency_error += std::conj(f) * error;
                phase *= phase_step;
            }
            if (!(power > 0.)) break;
            dd -= std::norm(cross_delay) / power;
            df -= (std::conj(cross_delay) * cross_frequency).real() / power;
            ff -= std::norm(cross_frequency) / power;
            const auto determinant = dd * ff - df * df;
            if (!(dd > 0. && ff > 0. && determinant > dd * ff * 1e-4)) break;
            const auto delay_step = std::clamp((ff * delay_error.real() - df * frequency_error.real()) /
                determinant, -double(view.stride) * .75, double(view.stride) * .75);
            const auto frequency_step = std::clamp((dd * frequency_error.real() - df * delay_error.real()) /
                determinant, -.85, .85);
            bool improved = false;
            for (const auto scale : {1., .5, .25}) {
                const auto delay = modulo(a.delay + scale * delay_step);
                const auto frequency = std::clamp(a.frequency + scale * frequency_step,
                    -bounds.max_abs_doppler_hz, bounds.max_abs_doppler_hz);
                const auto prior_gain = a.gain;
                const auto score = project(a, delay, frequency, clock);
                if (score > best * (1. + 1e-8)) {
                    a.delay = delay;
                    a.frequency = frequency;
                    best = score;
                    improved = true;
                    break;
                }
                a.gain = prior_gain;
            }
            if (!improved) break;
        }
        if (a.frequency != original_frequency) refresh(a);
        else generate(a, a.delay, a.frequency, clock, a.column);
        const auto power = energy(a.column);
        if (power > 0.) a.gain = inner(a.column, residual) / power;
    }

    void refine_clock() {
        // Reuse the existing filtered grids for small clock proposals.
        // The final accepted clock rebuilds every exact clock-dependent FIR.
        const auto original = clock;
        auto selected = original;
        const auto control = energy(residual);
        auto best = control;
        std::vector<Complex> error(observed.size());
        for (const auto step : {50e-6, 10e-6}) {
            const auto center = selected;
            for (int i = -5; i <= 5; ++i) {
                const auto trial = std::clamp(center + i * step,
                    -bounds.max_abs_clock_ratio, bounds.max_abs_clock_ratio);
                if (trial == center || stop.stop_requested()) continue;
                std::copy(observed.begin(), observed.end(), error.begin());
                for (const auto& a : atoms) {
                    generate(a, a.delay, a.frequency, trial, work);
                    for (std::size_t k = 0; k < work.size(); ++k) error[k] -= a.gain * work[k];
                }
                const auto score = energy(error);
                if (score < best) {
                    best = score;
                    selected = trial;
                }
            }
        }
        if (best >= control * .9999) return;
        clock = selected;
        for (auto& a : atoms) refresh(a);
        if (fit_gains() && energy(residual) < control) return;
        clock = original;
        for (auto& a : atoms) refresh(a);
        fit_gains();
    }

    bool learn() {
        const SsbAcquisitionView errors{bounds.training_first_center_tick, view.stride, residual};
        for (std::size_t n = 0; n < DelayDopplerFit::maximum_atoms && !stop.stop_requested(); ++n) {
            if (energy(residual) <= train_energy * 1e-5) break;
            const auto uniform = regrid_ssb_preamble(errors, bounds.training_first_center_tick,
                bounds.training_words, profile.symbol_rate * 5.5, profile.sample_rate);
            if (!uniform) return false;
            const auto peak = proposal.search(*uniform, stop);
            if (!peak || peak->coherence < .005) break;
            Atom a{first_relative + peak->delay_samples * word_ticks / ssb_proposal_word_samples,
                   peak->frequency_hz, {}, {}, {}};
            refresh(a);
            refine(a, 5);
            atoms.push_back(std::move(a));
            if (!fit_gains()) return false;
            if (n == 1 || n == 7 || n + 1 == DelayDopplerFit::maximum_atoms) {
                refine_clock();
                for (unsigned pass = 0; pass < 2; ++pass) {
                    for (auto& atom : atoms) {
                        if (stop.stop_requested()) return false;
                        apply(atom, 1.);
                        refine(atom, 2);
                        apply(atom, -1.);
                    }
                    if (!fit_gains()) return false;
                }
            }
        }
        return !stop.stop_requested() && !atoms.empty() && fit_gains();
    }

    std::optional<std::vector<Atom>> recent_model(std::size_t& workspace) {
        constexpr double rate_step_limit = .25;
        const auto count = atoms.size();
        const auto first = static_cast<std::size_t>(std::ceil((bounds.training_words - bounds.training_words / 2) *
            word_ticks / view.stride));
        const auto samples = observed.size() - first;
        const auto error = std::span<const Complex>(residual).subspan(first);
        // Each real column is a direction in complex sample space. The gain
        // directions and one rate direction per atom are fit together.
        const auto directions = count * 3;
        std::vector<Complex> design(directions * samples);
        workspace += (design.capacity() + directions * (directions + 2)) * sizeof(Complex);
        const auto column = [&](std::size_t index) { return std::span(design).subspan(index * samples, samples); };
        for (std::size_t a = 0; a < count; ++a) {
            if (stop.stop_requested()) return {};
            const auto& atom = atoms[a];
            for (std::size_t i = 0; i < samples; ++i) {
                const auto t = first_relative + (first + i) * double(view.stride);
                const auto u = ((1. + clock) * t - atom.delay) * beta - radius;
                const auto [value, moment] = atom.periodic.sample(u);
                const auto phase = rotation(atom.frequency, t, profile.sample_rate);
                const auto gain = phase * value;
                column(3 * a)[i] = gain;
                column(3 * a + 1)[i] = Complex{0., 1.} * gain;
                column(3 * a + 2)[i] = Complex{0., tau * rate_step_limit / profile.sample_rate} *
                    atom.gain * phase * (t * value - moment);
            }
        }
        HermitianBand gram;
        gram.reset(directions, directions - 1);
        std::vector<Complex> step(directions);
        double scale = 0.;
        for (std::size_t i = 0; i < directions; ++i) {
            if (stop.stop_requested()) return {};
            const auto row = gram.lower_row(i);
            for (std::size_t j = 0; j <= i; ++j) row[j] = inner(column(i), column(j)).real();
            scale = std::max(scale, row[i].real());
            step[i] = inner(column(i), error).real();
        }
        if (gram.factor(scale * 1e-3, stop) != HermitianBand::FactorResult::complete ||
            !gram.solve_in_place(step, stop)) return {};

        std::vector<Atom> trial;
        trial.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (stop.stop_requested()) return {};
            const auto& baseline = atoms[i];
            const auto rate = std::clamp(baseline.frequency + rate_step_limit * std::clamp(step[3 * i + 2].real(), -1., 1.),
                -bounds.max_abs_doppler_hz, bounds.max_abs_doppler_hz);
            Atom a{baseline.delay, rate, baseline.gain, {}, {}};
            refresh(a);
            workspace += a.periodic.workspace_bytes() + a.column.capacity() * sizeof(Complex);
            trial.push_back(std::move(a));
        }
        // Reprofile the gains against the rebuilt exact filter, using only
        // the same recent fit observations and a ridge toward prior gains.
        gram.reset(count, count - 1);
        step.assign(count, Complex{});
        scale = 0.;
        const auto recent = observed.subspan(first);
        for (std::size_t i = 0; i < count; ++i) {
            if (stop.stop_requested()) return {};
            const auto left = std::span<const Complex>(trial[i].column).subspan(first);
            const auto row = gram.lower_row(i);
            for (std::size_t j = 0; j <= i; ++j)
                row[j] = inner(left, std::span<const Complex>(trial[j].column).subspan(first));
            scale = std::max(scale, row[i].real());
            step[i] = inner(left, recent);
        }
        const auto ridge = scale * 1e-3;
        for (std::size_t i = 0; i < count; ++i) step[i] += ridge * atoms[i].gain;
        if (gram.factor(ridge, stop) != HermitianBand::FactorResult::complete ||
            !gram.solve_in_place(step, stop)) return {};
        std::copy(recent.begin(), recent.end(), work.begin());
        for (std::size_t a = 0; a < count; ++a) {
            trial[a].gain = step[a];
            for (std::size_t i = 0; i < samples; ++i) work[i] -= step[a] * trial[a].column[first + i];
        }
        if (!(energy(std::span(work).first(samples)) < energy(error))) return {};
        return trial;
    }

    double prediction_error(std::size_t first, std::size_t count) const {
        double power = 0., error = 0.;
        for (auto i = first; i < first + count; ++i) {
            const auto t = ticks(i);
            Complex predicted{};
            for (const auto& a : atoms) {
                const auto u = ((1. + clock) * t - a.delay) * beta - radius;
                predicted += a.gain * rotation(a.frequency, t, profile.sample_rate) * a.periodic.sample(u).first;
            }
            const auto y = view.samples[i];
            power += std::norm(y);
            error += std::norm(y - predicted);
        }
        return power > 0. ? error / power : std::numeric_limits<double>::infinity();
    }

    std::size_t workspace_bytes() const {
        auto bytes = pulse.workspace_bytes() + proposal.workspace_bytes() +
            (residual.capacity() + work.capacity()) * sizeof(Complex) +
            atoms.capacity() * sizeof(Atom);
        for (const auto& a : atoms) bytes += a.periodic.workspace_bytes() + a.column.capacity() * sizeof(Complex);
        return bytes;
    }
};

struct PhysicalEpoch {
    double epoch;
    std::vector<std::size_t> included;
    std::vector<SsbAcquisitionAtom> atoms;
};

struct MarkerScore {
    double nmse = std::numeric_limits<double>::infinity();
    double coherence = 0.;
    double decoy_ratio = 0.;
    bool decoy_evaluated = false;

    bool primary_passes() const { return nmse <= .20 && coherence >= .90; }
    bool passes() const { return primary_passes() && decoy_evaluated && decoy_ratio >= 4.; }
};

bool observed_range(SsbAcquisitionView view, std::uint64_t reference, double first, double last,
                    std::size_t& begin, std::size_t& end) {
    const auto start = tick_difference(view.first_center_input_tick, reference);
    const auto a = std::ceil((first - start) / view.stride);
    const auto b = std::ceil((last - start) / view.stride);
    if (a < 0. || b <= a || b > double(view.samples.size())) return false;
    begin = static_cast<std::size_t>(a);
    end = static_cast<std::size_t>(b);
    return true;
}

double at_source_symbol(const FitSession& fit, double epoch, double symbol) {
    return (epoch + (symbol + fit.radius) / fit.beta) / (1. + fit.clock);
}

std::vector<PhysicalEpoch> epochs(const FitSession& fit, const SingleCarrierSchedule& schedule) {
    const auto first = fit.first_relative + fit.bounds.earliest_epoch_from_training_ticks;
    const auto last = fit.first_relative + fit.bounds.latest_epoch_from_training_ticks;
    const auto late_symbol = schedule.training_count - 3 * word_symbols;
    const auto earliest_periodic = (fit.radius + fit.pulse.support_symbols(fit.clock)) / fit.beta;
    const auto maximum_delay = fit.bounds.max_excess_delay_ticks;
    const auto timing_slack = double(fit.view.stride);
    std::vector<PhysicalEpoch> result;
    for (std::size_t i = 0; i < std::min<std::size_t>(6, fit.atoms.size()); ++i) {
        const auto anchor = fit.atoms[i].delay;
        const auto base = first + wrap(anchor - first, fit.word_ticks);
        for (auto epoch = base; epoch <= last; epoch += fit.word_ticks) {
            if (fit.first_relative * (1. + fit.clock) < epoch + earliest_periodic + maximum_delay) continue;
            if (at_source_symbol(fit, epoch, late_symbol) < fit.first_relative + fit.observed.size() * fit.view.stride)
                continue;
            if (std::any_of(result.begin(), result.end(), [&](const auto& prior) {
                    return std::abs(prior.epoch - epoch) < timing_slack / 4.;
                })) continue;
            PhysicalEpoch candidate{epoch, {}, {}};
            for (std::size_t j = 0; j < fit.atoms.size(); ++j) {
                const auto& a = fit.atoms[j];
                const auto relative = std::remainder(a.delay - epoch - maximum_delay / 2., fit.word_ticks)
                    + maximum_delay / 2.;
                if (relative < -timing_slack || relative > maximum_delay + timing_slack) continue;
                candidate.included.push_back(j);
                candidate.atoms.push_back({relative, a.frequency, a.gain});
            }
            if (!candidate.atoms.empty()) result.push_back(std::move(candidate));
        }
    }
    return result;
}

// Fixed public marker templates and one shared future-symbol nuisance space.
// This compares predictions from the frozen preamble model on fresh centers.
MarkerScore score_marker(const FitSession& fit, const SingleCarrierSchedule& schedule,
                         const PhysicalEpoch& epoch, std::span<const SsbPulseCache> caches,
                         double noise, std::size_t& workspace, SsbAcquisitionDiagnostics& diagnostics) {
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    const auto boundary = static_cast<int>(schedule.training_count);
    const auto marker = static_cast<int>(schedule.marker_span);
    const auto support = fit.pulse.support_symbols(fit.clock);
    std::size_t begin = 0, end = 0;
    if (!observed_range(fit.view, fit.reference_tick, at_source_symbol(fit, epoch.epoch, boundary - 8),
            at_source_symbol(fit, epoch.epoch, boundary + marker + 20), begin, end) ||
        begin < fit.train_index + fit.observed.size() || end - begin > maximum_marker_rows) return {};
    const auto guard = static_cast<std::size_t>(std::ceil(support + 20. + fit.beta * fit.view.stride));
    if (guard > maximum_marker_guard) return {};
    const auto columns = marker + guard;
    const auto rows = end - begin;
    std::vector<Complex> matrix(rows * columns), y(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        if (i % 32 == 0 && fit.stop.stop_requested()) return {};
        y[i] = fit.view.samples[begin + i];
        const auto t = fit.ticks(begin + i);
        const auto local_time = (1. + fit.clock) * t - epoch.epoch;
        for (std::size_t a = 0; a < epoch.atoms.size(); ++a) {
            const auto& atom = epoch.atoms[a];
            const auto gain = atom.gain * rotation(atom.frequency_hz, t, fit.profile.sample_rate);
            const auto u = (local_time - atom.delay_ticks) * fit.beta - fit.radius;
            const auto first = std::max(0, static_cast<int>(std::ceil(u - support)));
            const auto last = static_cast<int>(std::floor(u + support));
            if (last >= boundary + int(columns)) return {};
            if (last < first) continue;
            const auto& cache = caches[epoch.included[a]];
            const auto add_term = [&](int k,Complex pulse) {
                const auto value = gain * pulse;
                if (k < boundary) y[i] -= value * schedule.training_symbol(k);
                else matrix[i * columns + k - boundary] += value;
            };
            if (const auto run = cache.symbol_run(u - first,static_cast<std::size_t>(last - first + 1))) {
                for (int k = first; k <= last; ++k) add_term(k,run->value(static_cast<std::size_t>(k - first)));
            } else {
                for (int k = first; k <= last; ++k) add_term(k,cache.kernel(u - k).value);
            }
        }
    }
    const auto gram_started=Clock::now();
    diagnostics.marker_row_seconds+=std::chrono::duration<double>(gram_started-started).count();
    HermitianBand gram;
    gram.reset(guard, guard - 1);
    double largest_column = 0.;
    for (std::size_t i = 0; i < guard; ++i) {
        if (fit.stop.stop_requested()) return {};
        const auto row = gram.lower_row(i);
        for (std::size_t t = 0; t < rows; ++t) {
            const auto* coefficients = matrix.data() + t * columns + marker;
            const auto left = std::conj(coefficients[i]);
            if (left == Complex{}) continue;
            for (std::size_t j = 0; j <= i; ++j) row[j] += left * coefficients[j];
        }
        largest_column = std::max(largest_column, row[i].real());
    }
    const auto ridge = std::max(noise, largest_column * 1e-7);
    const auto factor_started=Clock::now();
    diagnostics.marker_gram_seconds+=std::chrono::duration<double>(factor_started-gram_started).count();
    const auto factor=gram.factor(ridge, fit.stop);
    diagnostics.marker_factor_seconds+=std::chrono::duration<double>(Clock::now()-factor_started).count();
    if (factor != HermitianBand::FactorResult::complete) return {};

    const auto eliminate = [&](std::vector<Complex> input) {
        std::vector<Complex> rhs(guard);
        for (std::size_t t = 0; t < rows; ++t) {
            const auto* coefficients = matrix.data() + t * columns + marker;
            for (std::size_t j = 0; j < guard; ++j) rhs[j] += std::conj(coefficients[j]) * input[t];
        }
        if (!gram.solve_in_place(rhs, fit.stop)) return std::vector<Complex>{};
        input.reserve(rows + guard);
        for (std::size_t t = 0; t < rows; ++t) {
            const auto* coefficients = matrix.data() + t * columns + marker;
            for (std::size_t j = 0; j < guard; ++j) input[t] -= coefficients[j] * rhs[j];
        }
        for (const auto value : rhs) input.push_back(std::sqrt(ridge) * value);
        return input;
    };
    const auto observed = eliminate(std::move(y));
    const auto observed_energy = energy(observed);
    if (!(observed_energy > 0.)) return {};

    const auto hypothesis = [&](int shift, Complex phase, bool continued) {
        std::vector<Complex> predicted(rows);
        for (std::size_t t = 0; t < rows; ++t) {
            for (int k = 0; k < marker; ++k) {
                const auto value = continued ? schedule.training_symbol(k) :
                    phase * schedule.position(0, (k + shift + marker) % marker).known_value;
                predicted[t] += matrix[t * columns + k] * value;
            }
        }
        return eliminate(std::move(predicted));
    };
    const auto score = [&](std::span<const Complex> prediction) {
        if (prediction.empty()) return std::numeric_limits<double>::infinity();
        double error = 0.;
        for (std::size_t i = 0; i < prediction.size(); ++i) error += std::norm(observed[i] - prediction[i]);
        return error / observed_energy;
    };
    const auto predicted = hypothesis(0, 1., false);
    const auto predicted_energy = energy(predicted);
    if (!(predicted_energy > 0.)) return {};
    const auto nmse = score(predicted);
    const auto coherence = inner(predicted, observed).real() / std::sqrt(predicted_energy * observed_energy);
    workspace = std::max(workspace, gram.workspace_bytes() + (matrix.capacity() +
        3 * observed.capacity() + 2 * guard) * sizeof(Complex));
    const auto decoys_started=Clock::now();
    diagnostics.marker_primary_seconds+=std::chrono::duration<double>(decoys_started-started).count();
    MarkerScore result{nmse,coherence};
    if(!result.primary_passes()) {
        ++diagnostics.marker_primary_rejections;
        return result;
    }
    // No decoy can repair a failed primary gate. For every viable primary
    // keep the same complete decoy bank and independent acceptance margin.
    auto decoy = score(hypothesis(0, 1., true));
    for (const int offset : {-8, -1, 1, 8, 32})
        decoy = std::min(decoy, score(hypothesis(offset, 1., false)));
    for (const auto phase : {Complex{-1., 0.}, Complex{0., 1.}})
        decoy = std::min(decoy, score(hypothesis(0, phase, false)));
    ++diagnostics.marker_decoy_banks;
    diagnostics.marker_decoy_seconds+=std::chrono::duration<double>(Clock::now()-decoys_started).count();
    result.decoy_ratio=decoy/std::max(nmse,1e-15);
    result.decoy_evaluated=true;
    return result;
}

bool valid_view(SsbAcquisitionView view) {
    return view.stride && !view.samples.empty() && view.samples.size() - 1 <=
        (std::numeric_limits<std::uint64_t>::max() - view.first_center_input_tick) / view.stride;
}

} // namespace

std::optional<std::vector<Complex>> regrid_ssb_preamble(SsbAcquisitionView view, std::uint64_t first_input_tick,
        unsigned words, double proposal_sample_rate, double input_rate) {
    if (!valid_view(view) || first_input_tick < view.first_center_input_tick || !words || words > 32 ||
        !std::isfinite(proposal_sample_rate) || proposal_sample_rate <= 0. ||
        !std::isfinite(input_rate) || input_rate <= 0.) return {};
    const auto count = words * ssb_proposal_word_samples;
    const auto start = tick_difference(first_input_tick, view.first_center_input_tick) / view.stride;
    const auto step = (input_rate / proposal_sample_rate) / view.stride;
    if (start + (count - 1) * step > double(view.samples.size() - 1)) return {};
    std::vector<Complex> output(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto coordinate = start + i * step;
        const auto index = static_cast<std::size_t>(coordinate);
        const auto f = coordinate - index;
        const auto a = view.samples[index ? index - 1 : index];
        const auto b = view.samples[index];
        const auto c = view.samples[std::min(index + 1, view.samples.size() - 1)];
        const auto d = view.samples[std::min(index + 2, view.samples.size() - 1)];
        output[i] = b + f * (.5 * (c - a) + f * (a - 2.5 * b + 2. * c - .5 * d +
            f * (.5 * (d - a) + 1.5 * (b - c))));
    }
    return output;
}

DelayDopplerFit::DelayDopplerFit(const Profile& profile, const SsbFrontendGeometry& frontend)
    : profile_(profile), stride_(frontend.decimation), pulse_(profile, frontend),
      periodic_(pulse_.prepare_periodic(public_training_word(profile))) {
    if (!stride_)
        throw std::invalid_argument("Fast SSB acquisition requires the capacity preamble and marker");
    PeriodicWave wave;
    pulse_.make_periodic(periodic_, 0., 0., wave.grid);
    const auto radius = single_carrier_pulse_radius(profile);
    for (std::size_t i = 0; i < proposal_word_.size(); ++i)
        proposal_word_[i] = wave.sample(double(i) / 5.5 - radius).first;
}

SsbAcquisitionTiming DelayDopplerFit::timing(const SsbAcquisitionRequest& bounds) const {
    if (bounds.training_words < 8 || bounds.training_words > maximum_training_words ||
        !(bounds.max_excess_delay_ticks >= 0. && bounds.max_excess_delay_ticks <= profile_.sample_rate * maximum_delay_seconds) ||
        !(bounds.max_abs_clock_ratio >= 0. && bounds.max_abs_clock_ratio <= maximum_clock_ratio) ||
        !(bounds.max_abs_doppler_hz >= 0. && bounds.max_abs_doppler_hz <= maximum_frequency_hz))
        throw std::invalid_argument("Invalid Fast SSB acquisition search bounds");
    const SingleCarrierSchedule schedule(profile_);
    const auto ticks_per_symbol = profile_.sample_rate / profile_.symbol_rate;
    const auto radius = single_carrier_pulse_radius(profile_);
    const auto support = pulse_.support_symbols(bounds.max_abs_clock_ratio);
    const auto drift = (schedule.training_count + radius + schedule.marker_span + 20.) * ticks_per_symbol *
        bounds.max_abs_clock_ratio;
    const auto first = (radius + support) * ticks_per_symbol + bounds.max_excess_delay_ticks + drift;
    const auto last = (schedule.training_count - 3 * word_symbols + radius -
        bounds.training_words * word_symbols) * ticks_per_symbol - drift - stride_;
    const auto end = (schedule.training_count + radius + schedule.marker_span + 20.) * ticks_per_symbol + drift + stride_;
    return {first, last, end};
}

std::size_t DelayDopplerFit::training_centers(const SsbAcquisitionRequest& bounds) const {
    const auto word_ticks = word_symbols * profile_.sample_rate / profile_.symbol_rate;
    const auto centers = static_cast<std::size_t>(std::ceil(bounds.training_words * word_ticks / stride_)) + 1;
    if (centers > maximum_training_centers)
        throw std::invalid_argument("Fast SSB acquisition exceeds its native center budget");
    return centers;
}

std::size_t DelayDopplerFit::fit_workspace_bound(const SsbAcquisitionRequest& bounds) const {
    (void)timing(bounds);
    const auto centers = training_centers(bounds);
    constexpr auto complex_bytes = sizeof(Complex);
    constexpr auto directions = 3 * maximum_atoms;
    constexpr auto max_epochs = 6 * 33; // Six anchors, at most 32 complete word periods per anchor.
    const auto cache = pulse_.cache_storage_bound(bounds.max_abs_clock_ratio);
    const auto marker = SingleCarrierSchedule(profile_).marker_span;
    const auto marker_arrays = maximum_marker_rows * (marker + maximum_marker_guard) +
        maximum_marker_guard * maximum_marker_guard + 8 * (maximum_marker_rows + maximum_marker_guard);
    const auto atom_arrays = 2 * maximum_atoms * (2 * periodic_size + centers);
    const auto recent_arrays = directions * (centers + directions + 2);
    const auto train_arrays = 4 * centers + bounds.training_words * ssb_proposal_word_samples;
    const auto epoch_bytes = 2 * max_epochs * (sizeof(PhysicalEpoch) +
        2 * maximum_atoms * (sizeof(SsbAcquisitionAtom) + sizeof(std::size_t)) + 4 * sizeof(double));
    // Include both model banks and verification storage together. The small
    // separate allowance covers proposal plans, per-fit object state and
    // bounded vector/deque metadata, rather than assuming easy-path reuse.
    return complex_bytes * (atom_arrays + recent_arrays + train_arrays + marker_arrays) +
        maximum_atoms * cache.retained + cache.temporary + epoch_bytes + 256 * 1024;
}

std::optional<SsbAcquisitionLock> DelayDopplerFit::fit(SsbAcquisitionView view, const SsbAcquisitionRequest& request,
        std::stop_token stop, SsbAcquisitionDiagnostics* diagnostics) const {
    SsbAcquisitionDiagnostics report;
    const auto decline = [&](SsbAcquisitionDiagnostics::Status status) -> std::optional<SsbAcquisitionLock> {
        report.status = status;
        if (diagnostics) *diagnostics = report;
        return {};
    };
    timing(request);
    const auto word_ticks = word_symbols * profile_.sample_rate / profile_.symbol_rate;
    if (!valid_view(view) || view.stride != stride_ ||
        !std::isfinite(request.earliest_epoch_from_training_ticks) || !std::isfinite(request.latest_epoch_from_training_ticks) ||
        request.earliest_epoch_from_training_ticks > request.latest_epoch_from_training_ticks ||
        std::abs(request.earliest_epoch_from_training_ticks) > 32 * word_ticks ||
        std::abs(request.latest_epoch_from_training_ticks) > 32 * word_ticks ||
        request.latest_epoch_from_training_ticks - request.earliest_epoch_from_training_ticks > 32 * word_ticks ||
        request.training_first_center_tick < view.first_center_input_tick)
        throw std::invalid_argument("Invalid Fast SSB acquisition center view or epoch interval");
    const auto offset = request.training_first_center_tick - view.first_center_input_tick;
    if (offset % view.stride) throw std::invalid_argument("Fast SSB training start is not a frontend center");
    if (stop.stop_requested()) return decline(SsbAcquisitionDiagnostics::Status::cancelled);
    const auto first = offset / view.stride;
    const auto count = training_centers(request);
    if (first >= view.samples.size() || count > view.samples.size() - first)
        return decline(SsbAcquisitionDiagnostics::Status::insufficient_support);
    try {
        FitSession fit(profile_, pulse_, periodic_, request, view, first, count, proposal_word_, stop);
        if (!(fit.train_energy > 0. && std::isfinite(fit.train_energy)) || !fit.learn())
            return decline(stop.stop_requested() ? SsbAcquisitionDiagnostics::Status::cancelled :
                SsbAcquisitionDiagnostics::Status::no_periodic_model);
        report.training_nmse = energy(fit.residual) / fit.train_energy;
        report.atom_count = fit.atoms.size();
        // Upper allowance for one cache FFT construction (three work arrays,
        // twiddles and its new value/moment grids), even at the largest size.
        const auto cache_build_bound = 6 * SsbPulseCache::maximum_fft_size * sizeof(Complex);
        report.workspace_bound_bytes = fit.workspace_bytes() + cache_build_bound +
            (fit.observed.size() + request.training_words * ssb_proposal_word_samples) * sizeof(Complex);
        if (report.training_nmse > .20) return decline(SsbAcquisitionDiagnostics::Status::no_periodic_model);

        const SingleCarrierSchedule schedule(profile_);
        const auto candidate_epochs = epochs(fit, schedule);
        report.epoch_candidates = candidate_epochs.size();
        report.workspace_bound_bytes += candidate_epochs.capacity() *
            (sizeof(PhysicalEpoch) + 2 * maximum_atoms * (sizeof(SsbAcquisitionAtom) + sizeof(std::size_t)));
        const auto late_symbol = schedule.training_count - 3 * word_symbols;
        const auto noise = energy(fit.residual) / fit.observed.size();
        struct Verification {
            std::optional<PhysicalEpoch> accepted;
            std::vector<double> holdouts;
            MarkerScore best;
            double holdout = std::numeric_limits<double>::infinity();
            double paired_control = std::numeric_limits<double>::infinity();
            std::size_t workspace = 0;
        };
        const auto verify = [&](std::span<const double> control) {
            Verification verification;
            verification.holdouts.assign(candidate_epochs.size(), std::numeric_limits<double>::infinity());
            std::vector<std::size_t> eligible;
            for (std::size_t i = 0; i < candidate_epochs.size(); ++i) {
                const auto& epoch = candidate_epochs[i];
                std::size_t begin = 0, end = 0;
                if (!observed_range(view, fit.reference_tick, at_source_symbol(fit, epoch.epoch, late_symbol),
                        at_source_symbol(fit, epoch.epoch, late_symbol + word_symbols), begin, end)) continue;
                const auto holdout = fit.prediction_error(begin, end - begin);
                verification.holdouts[i] = holdout;
                if (holdout < verification.holdout) {
                    verification.holdout = holdout;
                    if (!control.empty()) verification.paired_control = control[i];
                }
                // The recent fit can be used only when it improves the same
                // untouched public word for this physical epoch.
                if (holdout <= .20 && (control.empty() || holdout <= .90 * control[i])) eligible.push_back(i);
            }
            if (eligible.empty() || stop.stop_requested()) return verification;

            std::vector<SsbPulseCache> caches;
            caches.reserve(fit.atoms.size());
            std::size_t cache_bytes = 0, marker_workspace = 0;
            for (const auto& a : fit.atoms) {
                caches.push_back(pulse_.make_cache(fit.clock, a.frequency, stop));
                cache_bytes += caches.back().workspace_bytes();
            }
            for (const auto i : eligible) {
                if (stop.stop_requested()) break;
                auto epoch = candidate_epochs[i];
                for (std::size_t a = 0; a < epoch.atoms.size(); ++a) {
                    epoch.atoms[a].gain = fit.atoms[epoch.included[a]].gain;
                    epoch.atoms[a].frequency_hz = fit.atoms[epoch.included[a]].frequency;
                }
                ++report.marker_evaluations;
                const auto score = score_marker(fit, schedule, epoch, caches, noise, marker_workspace,report);
                if (!verification.accepted && score.nmse < verification.best.nmse) verification.best = score;
                if (score.passes() && (!verification.accepted || score.nmse < verification.best.nmse - 1e-7 ||
                        (std::abs(score.nmse - verification.best.nmse) <= 1e-7 &&
                        epoch.epoch < verification.accepted->epoch))) {
                    verification.accepted = std::move(epoch);
                    verification.best = score;
                    verification.holdout = verification.holdouts[i];
                    if (!control.empty()) verification.paired_control = control[i];
                }
            }
            verification.workspace = cache_bytes + marker_workspace;
            return verification;
        };

        auto selected = verify({});
        auto maximum_verification_workspace = selected.workspace;
        if (!selected.accepted && std::isfinite(selected.holdout) &&
            (std::isfinite(selected.best.nmse) || selected.holdout > .20) && !stop.stop_requested()) {
            report.recent_refinement_attempted = true;
            if (auto recent = fit.recent_model(report.workspace_bound_bytes)) {
                fit.atoms.swap(*recent);
                auto alternative = verify(selected.holdouts);
                maximum_verification_workspace = std::max(maximum_verification_workspace, alternative.workspace);
                report.recent_refinement_control_holdout_nmse = alternative.paired_control;
                report.recent_refinement_holdout_nmse = alternative.holdout;
                report.recent_refinement_marker_nmse = alternative.best.nmse;
                report.recent_refinement_marker_signed_coherence = alternative.best.coherence;
                report.recent_refinement_marker_decoy_nmse_ratio = alternative.best.decoy_ratio;
                report.recent_refinement_marker_decoy_evaluated = alternative.best.decoy_evaluated;
                if (alternative.accepted) {
                    report.recent_refinement_used = true;
                    selected = std::move(alternative);
                } else {
                    fit.atoms.swap(*recent);
                }
            }
        }
        report.workspace_bound_bytes += maximum_verification_workspace;
        if (stop.stop_requested()) return decline(SsbAcquisitionDiagnostics::Status::cancelled);
        report.public_holdout_nmse = selected.holdout;
        report.marker_nmse = selected.best.nmse;
        report.marker_signed_coherence = selected.best.coherence;
        report.marker_decoy_nmse_ratio = selected.best.decoy_ratio;
        report.marker_decoy_evaluated = selected.best.decoy_evaluated;
        if (!selected.accepted) return decline(SsbAcquisitionDiagnostics::Status::no_verified_marker);
        report.status = SsbAcquisitionDiagnostics::Status::locked;
        if (diagnostics) *diagnostics = report;
        return SsbAcquisitionLock{fit.reference_tick, selected.accepted->epoch, fit.clock,
            std::move(selected.accepted->atoms), report};
    } catch (...) {
        if (stop.stop_requested()) return decline(SsbAcquisitionDiagnostics::Status::cancelled);
        throw;
    }
}

} // namespace datapump::fast::detail
