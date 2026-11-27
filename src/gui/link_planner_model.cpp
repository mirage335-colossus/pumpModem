#include "link_planner_model.hpp"
#include "../triad_receiver_coarse.hpp"
#include "../triad_stream_resources.hpp"
#include "datapump/execution.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace datapump::gui::planner {
namespace {
namespace native = triad::v3;
constexpr double minimum_target = -120;
constexpr double maximum_target = 200;
constexpr double step_db = 1;

struct Evaluation {
    Point point;
    std::string limit;
    bool exact=false;
};

class Sweep {
public:
    Sweep(const Inputs& inputs, double cn0, const std::optional<std::string>& worker_unavailability,
          std::stop_token stop)
        : settings(*inputs.settings), actual_cn0(cn0),
          calendar(settings.calendar.value_or(live::detail::automatic_live_calendar)),
          backend(settings.gpu ? inputs.backend.lock() : nullptr), stop(stop) {
        settings.wrong_key_admission = "current";
        resources.memory_limit_bytes = settings.receiver_memory_limit;
        resources.maximum_input_chunk_samples = live::detail::receiver_input_chunk_samples;
        resources.maximum_pending_messages = live::detail::receiver_pending_messages;
        try {
            const auto limits = native::resolve_stream_receiver_resources(resources);
            if (worker_unavailability) {
                resource_error = *worker_unavailability;
                return;
            }
            resources.memory_limit_bytes = limits.memory_limit_bytes;
            resources.workers = limits.workers;
            resources.slot_owners = limits.slot_owners;
        } catch (const Error& error) {
            // Duration and fixed-energy detection do not require a host workspace quote.
            resource_error = error.what();
        }
    }

    bool workspace_available() const { return resources.workers && (!settings.gpu || backend); }
    unsigned workers() const { return resources.workers.value_or(0); }
    const std::string& workspace_error() const { return resource_error; }

    const Evaluation& evaluate(double target, bool exact = false) {
        if (const auto prior = evaluations.find(target); prior != evaluations.end() &&
            (!exact || prior->second.exact))
            return prior->second;
        auto& entry = evaluations[target];
        entry = {};
        entry.exact = exact;
        auto& point = entry.point;
        point.target_db_hz = target;
        execution::checkpoint();
        if (stop.stop_requested()) return entry;
        if (target < minimum_target || target > maximum_target) {
            entry.limit = "Native duration planning accepts −120 to +200 dB-Hz.";
            return entry;
        }
        // Once the minimum legal tile satisfies the fixed-energy bound,
        // increasing its target strength leaves this exact shape selected.
        auto& minimum = minimum_tile[exact];
        if (minimum && minimum->point.target_db_hz <= target) {
            entry = *minimum;
            point.target_db_hz = target;
            return entry;
        }
        settings.target_cn0_db_hz = target;
        try {
            auto plan = exact ? live::detail::plan_profile(settings, calendar, stop) :
                live::detail::preview_profile(settings, calendar, stop);
            point.bit_seconds = plan.waveform.geometry().bit_seconds;
            if (!plan.receiver.time_varying_channel && !plan.waveform.config().format.fhss) {
                const auto energy = plan.duration.active_seconds * std::pow(10., actual_cn0 / 10.);
                if (energy > 0 && std::isfinite(energy))
                    point.rx_data_detection_probability = native::duration_prediction(energy,
                        plan.duration.effective_modes, plan.duration.true_atom_log_lr_threshold, false)
                        .detection_lower_bound;
            }
            if (workspace_available()) {
                plan.receiver.bulk_backend = backend;
                const auto quote = workspace(plan, exact);
                point.workspace_fraction = static_cast<double>(quote.required_bytes) / quote.limit_bytes;
                point.workspace_fits = quote.fits;
                if (!quote.fits) entry.limit = "Nominal receive windows exceed the local workspace allowance.";
            }
            if (plan.duration.tiles == 1) minimum = entry;
        } catch (const Error& error) {
            entry.limit = error.what();
        }
        return entry;
    }

    void refine_workspace_edges() {
        if (!workspace_available()) return;
        struct Interval { double left, right, excess; };
        std::vector<Interval> changing;
        for (auto left = evaluations.begin(), right = std::next(left); right != evaluations.end(); ++left, ++right) {
            const auto& a = left->second.point;
            const auto& b = right->second.point;
            if (a.bit_seconds == b.bit_seconds || b.target_db_hz - a.target_db_hz < .125) continue;
            const auto excess = [](const Point& point) {
                return point.workspace_fraction ? std::abs(*point.workspace_fraction - 1) :
                    1e6;
            };
            changing.push_back({a.target_db_hz, b.target_db_hz, std::min(excess(a), excess(b))});
        }
        std::stable_sort(changing.begin(), changing.end(), [](const Interval& a, const Interval& b) {
            return a.excess < b.excess;
        });
        // Probe within changing tile geometry even when neither endpoint fits.
        // Prioritize intervals near the workspace limit when the sweep reaches
        // its fixed sub-grid budget. Never present this scan as exhaustive.
        for (std::size_t i = 0; i < std::min<std::size_t>(changing.size(), 24) && !stop.stop_requested(); ++i) {
            const auto& [left, right, excess] = changing[i];
            for (unsigned j = 1; j < 16 && !stop.stop_requested(); ++j)
                evaluate(std::lerp(left, right, j / 16.));
        }

        // Keep every checked island. Refine each observed transition
        // independently, because the public geometry can change at a tile or
        // FFT boundary and the fit result need not be monotone across targets.
        std::vector<std::pair<double, double>> edges;
        for (auto left = evaluations.begin(), right = std::next(left); right != evaluations.end(); ++left, ++right) {
            if (left->second.point.workspace_fits != right->second.point.workspace_fits)
                edges.emplace_back(left->first, right->first);
        }
        for (auto [left, right] : edges) {
            const bool left_fits = evaluate(left).point.workspace_fits;
            for (unsigned i = 0; i < 9 && right - left > .005 && !stop.stop_requested(); ++i) {
                const auto middle = std::midpoint(left, right);
                if (evaluate(middle).point.workspace_fits == left_fits) left = middle;
                else right = middle;
            }
        }
    }

    std::vector<Point> points() const {
        std::vector<Point> result;
        result.reserve(evaluations.size());
        for (const auto& [target, entry] : evaluations) result.push_back(entry.point);
        return result;
    }

    TargetSteps choices(double target) {
        auto choices = select_targets(target, points());
        const auto verify = [&](std::optional<double> value) {
            return !value || evaluate(*value, true).point.workspace_fits;
        };
        // Preview shaping is cheap. Every proposed action must also pass the
        // exact production shaping and workspace quote before exposure.
        for (unsigned attempt = 0; attempt < 8 && !stop.stop_requested(); ++attempt) {
            const bool fitted = verify(choices.fitted);
            const bool stronger = verify(choices.stronger);
            const bool weaker = verify(choices.weaker);
            if (fitted && stronger && weaker) return choices;
            choices = select_targets(target, points());
        }
        const auto checked = [&](std::optional<double> value) {
            if (!value) return value;
            const auto& entry = evaluations.at(*value);
            return entry.exact && entry.point.workspace_fits ? value : std::nullopt;
        };
        return {checked(choices.fitted), checked(choices.stronger), checked(choices.weaker)};
    }

private:
    native::detail::StreamWorkspaceQuote workspace(const live::detail::ProfilePlan& plan, bool exact) {
        // With fixed public settings, the target changes only the selected
        // tile count. Exact repeats share the same nominal window geometry.
        const auto key = std::pair{exact, plan.duration.tiles};
        if (const auto prior = quotes.find(key); prior != quotes.end()) return prior->second;
        const native::NativeMessageProfile message{settings.transfer.fec, settings.transfer.compression,
            std::min(settings.transfer.content_limit, native::native_message_editor_limit), settings.transfer.key};
        const auto presentation = message.content_limit + 64 * 1024U;
        const auto source = plan.waveform.emission_source_workspace_bytes();
        const auto result = native::detail::quote_stream_workspace(plan.waveform, plan.receiver,
            resources, message, 2, presentation + source, stop);
        quotes.emplace(key, result);
        return result;
    }

    live::Settings settings;
    const double actual_cn0;
    const live::Calendar calendar;
    const std::shared_ptr<native::receiver_detail::ReceiverBulkBackend> backend;
    const std::stop_token stop;
    native::StreamReceiverOptions resources;
    std::string resource_error;
    std::array<std::optional<Evaluation>, 2> minimum_tile;
    std::map<double, Evaluation> evaluations;
    std::map<std::pair<bool, std::uint64_t>, native::detail::StreamWorkspaceQuote> quotes;
};
}

TargetSteps select_targets(double requested, std::span<const Point> points) {
    TargetSteps result;
    std::optional<double> weaker_edge, stronger_edge;
    for (const auto& point : points) {
        if (!point.workspace_fits) continue;
        const auto target = point.target_db_hz;
        if (target >= requested && !result.fitted) result.fitted = target;
        if (target < requested) {
            if (!weaker_edge) weaker_edge = target;
            if (target <= requested - step_db) result.weaker = target;
        }
        if (target > requested) {
            stronger_edge = target;
            if (target >= requested + step_db && !result.stronger) result.stronger = target;
        }
    }
    if (!result.weaker) result.weaker = weaker_edge;
    if (!result.stronger) result.stronger = stronger_edge;
    if (!result.fitted) {
        const auto fitting = std::find_if(points.rbegin(), points.rend(),
            [](const Point& point) { return point.workspace_fits; });
        if (fitting != points.rend()) result.fitted = fitting->target_db_hz;
    }
    return result;
}

Model budget_preview(const Inputs& inputs) {
    Model result;
    result.inputs = inputs;
    if (std::isfinite(inputs.tx_dbm) && std::isfinite(inputs.path_loss_db) &&
        std::isfinite(inputs.noise_density_dbm_hz)) {
        result.received_dbm = inputs.tx_dbm - inputs.path_loss_db;
        result.actual_cn0_db_hz = result.received_dbm - inputs.noise_density_dbm_hz;
        if (std::isfinite(result.actual_cn0_db_hz) && result.actual_cn0_db_hz >= -200)
            result.budget_target_db_hz = std::floor(std::min(200., result.actual_cn0_db_hz));
    } else result.error = "Link budget inputs must be finite.";
    return result;
}

Model build(const Inputs& inputs, std::stop_token stop) {
    auto result = budget_preview(inputs);
    if (!inputs.settings || !result.error.empty() || stop.stop_requested()) return result;
    if (!std::isfinite(inputs.target_db_hz) || inputs.target_db_hz < -200 || inputs.target_db_hz > maximum_target) {
        result.error = "Target SNR must be finite and in −200 to +200 dB-Hz.";
        return result;
    }

    result.cpu_status = "Run a simulation to measure finite processing pace on this hardware.";
    if (inputs.settings->finite_duplex)
        result.cpu_status = "This finite-duplex session is reserved for its physical audio episode.";
    result.rx_status = "Provisional true-candidate DATA detection bound. Complete RX success is unqualified.";
    result.workspace_status = "Nominal host reservation for one DATA window and two quiet windows. Target actions use exact shaping.";
    try {
        const auto worker_unavailability = live::detail::receiver_worker_unavailability();
        Sweep sweep(inputs, result.actual_cn0_db_hz, worker_unavailability, stop);
        result.workers = sweep.workers();
        const auto& selected = sweep.evaluate(inputs.target_db_hz, true);
        result.selected = selected.point;
        result.receiver_status = selected.limit;
        if (!selected.point.rx_data_detection_probability && selected.point.bit_seconds)
            result.rx_status = "The fixed-energy DATA model is unavailable for this channel or FHSS setting.";
        if (!sweep.workspace_error().empty()) {
            result.workspace_status = sweep.workspace_error();
            if (worker_unavailability)
                result.cpu_status = *worker_unavailability;
            else if (!inputs.settings->finite_duplex)
                result.cpu_status = "A receiver workspace allowance is required to measure processing pace.";
        } else if (inputs.awaiting_gpu)
            result.workspace_status = "Native CPU workspace preview while the selected GPU route initializes.";
        else if (!sweep.workspace_available())
            result.workspace_status = "Host workspace will be available after the selected GPU route initializes.";

        // Probe every integer dB in the native domain. The model also evaluates
        // the exact input and the two normal steps before refining fit edges.
        // Geometry, not a monotonic workspace assumption, determines fit.
        for (auto target = minimum_target; target <= maximum_target && !stop.stop_requested(); target += step_db)
            sweep.evaluate(target);
        for (const auto offset : {-step_db, step_db})
            sweep.evaluate(std::clamp(inputs.target_db_hz + offset, minimum_target, maximum_target), true);
        sweep.refine_workspace_edges();
        if (stop.stop_requested()) return result;
        const auto steps = sweep.choices(inputs.target_db_hz);
        if (stop.stop_requested()) return result;
        auto points = sweep.points();
        result.fitted_target = steps.fitted;
        result.stronger_fit_target = steps.stronger;
        result.weaker_fit_target = steps.weaker;

        const auto first_fit = std::find_if(points.begin(), points.end(),
            [](const Point& point) { return point.workspace_fits; });
        const auto budget = result.budget_target_db_hz.value_or(inputs.target_db_hz);
        const auto low = std::min({-12., inputs.target_db_hz - 8, budget - 2,
            first_fit != points.end() ? first_fit->target_db_hz - 8 : -12.});
        const auto high = std::max({48., inputs.target_db_hz + 12, budget + 2});
        for (const auto& point : points)
            if (point.target_db_hz >= low && point.target_db_hz <= high) result.points.push_back(point);
        result.available = true;
    } catch (const Error& error) {
        result.error = error.what();
    }
    return result;
}

namespace {
using CoarseObservation = native::receiver_detail::CoarseProbeObservation;
constexpr std::size_t minimum_coarse_probe_pairs = 24;
constexpr unsigned minimum_coarse_probe_lanes = 8;

struct CoarseProjection {
    double lower_seconds, upper_seconds;
    bool rates_consistent;
};

std::optional<CoarseProjection> coarse_projection(const CoarseObservation& observation) {
    const auto [slow, fast] = std::minmax(observation.completed_pairs[0], observation.completed_pairs[1]);
    if (!observation.windows_complete || observation.completed_span_seconds || !observation.total_pairs ||
        slow < minimum_coarse_probe_pairs ||
        std::min(observation.contributing_lanes[0], observation.contributing_lanes[1]) < minimum_coarse_probe_lanes)
        return {};

    const auto pair_seconds = static_cast<double>(observation.total_pairs) * CoarseObservation::window_seconds;
    const bool rates_consistent = static_cast<long double>(fast) * 100 <= static_cast<long double>(slow) * 115;
    return CoarseProjection{pair_seconds / fast, pair_seconds / slow, rates_consistent};
}

std::string_view slot_phase(CoarseObservation::Phase phase) {
    using Phase = CoarseObservation::Phase;
    switch (phase) {
    case Phase::waiting: return "waiting for input";
    case Phase::preparing: return "checking the receive window";
    case Phase::coarse_preparation: return "preparing coarse search";
    case Phase::coarse_search: return "scoring coarse hypotheses";
    case Phase::coarse_selection: return "selecting coarse proposals";
    case Phase::fine: return "scoring fine candidates";
    case Phase::additional: return "scoring alternate candidates";
    case Phase::resolved: return "score returned";
    }
    return {};
}

std::string_view coarse_route(CoarseObservation::Route route) {
    using Route = CoarseObservation::Route;
    switch (route) {
    case Route::unknown: return {};
    case Route::native_frequency: return "native frequency search";
    case Route::native_lag: return "native lag search";
    case Route::grouped: return "memory-bounded native search";
    case Route::backend_frequency: return "backend frequency search";
    case Route::backend_lag: return "backend lag search";
    }
    return {};
}
}

std::string sample_progress_text(const SampleProgress& progress) {
    std::ostringstream out;
    if (progress.trial) out << "Trial " << progress.trial << " of " << SampleResult::trial_budget << " · ";
    switch (progress.stage) {
    case SampleProgress::Stage::planning: out << "Planning receive geometry and workspace"; break;
    case SampleProgress::Stage::preparing: out << "Preparing the waveform and receiver"; break;
    case SampleProgress::Stage::feeding: out << "Streaming modeled receiver input"; break;
    case SampleProgress::Stage::scoring: out << "Finishing receiver scoring"; break;
    }
    if (!progress.backend_id.empty()) out << " · Selected backend " << progress.backend_id;
    if (progress.episode_seconds > 0) {
        out << " · " << duration(progress.input_seconds) << " of " << duration(progress.episode_seconds)
            << " modeled input accepted · " << progress.scored_slots << "/3 public slots scored";
    }
    out << " · " << std::fixed << std::setprecision(1) << progress.elapsed_seconds << " s elapsed";
    const auto snapshot = progress.live_probe ? progress.live_probe->observe(std::chrono::steady_clock::now()) :
        progress.coarse_probe;
    if (snapshot) {
        const auto& observation = *snapshot;
        const auto phase = slot_phase(observation.phase);
        const auto route = coarse_route(observation.route);
        if (!phase.empty()) out << " · First slot " << phase;
        if (!route.empty()) out << " · Coarse route " << route;
        if (observation.completed_span_seconds) {
            out << " · First-slot native frequency span completed in " << duration(*observation.completed_span_seconds);
            return out.str();
        }
        if (!observation.meter_started) {
            if (!route.empty()) out << " · Two-window timing applies to eligible full native frequency spans";
            return out.str();
        }
        const auto [first_pairs, second_pairs] = observation.completed_pairs;
        if (observation.windows_complete)
            out << " · First-slot coarse probe recorded " << first_pairs << " and " << second_pairs << " FFT pairs ("
                << static_cast<double>(first_pairs) / CoarseObservation::window_seconds << " and "
                << static_cast<double>(second_pairs) / CoarseObservation::window_seconds << " pairs/s)";
        if (const auto projection = coarse_projection(observation)) {
            out << (projection->rates_consistent ? " · First-slot coarse correlation work projects to " :
                " · Coarse rates varied by more than 15%. At those rates, first-slot coarse correlations would take ")
                << std::setprecision(0) << std::floor(projection->lower_seconds) << "–"
                << std::ceil(projection->upper_seconds) << " s";
            if (projection->rates_consistent) out << " at the observed rates";
            out << ". Other phases remain unmeasured";
        } else if (!observation.windows_complete) {
            out << " · A coarse projection needs two complete " << CoarseObservation::window_seconds << " s windows";
        } else {
            const auto [first_lanes, second_lanes] = observation.contributing_lanes;
            out << " across " << first_lanes << " and " << second_lanes << " lanes · A coarse projection needs "
                << minimum_coarse_probe_pairs << " FFT pairs and " << minimum_coarse_probe_lanes << " lanes in each window";
        }
    }
    return out.str();
}

void add_sample(Model& model, const SampleResult& sample) {
    model.sampling = false;
    if (model.selected && model.selected->bit_seconds && model.selected->workspace_fits)
        add_sample(model, *model.selected->bit_seconds, sample);
    else model.sample_status = sample.status;
}

void add_sample(Model& model, double bit_seconds, const SampleResult& sample) {
    std::optional<double> probability;
    if (sample.trials == SampleResult::trial_budget && sample.correct <= sample.trials)
        probability = static_cast<double>(sample.correct) / sample.trials;
    const auto matches = [bit_seconds](const Point& point) {
        return point.workspace_fits && point.bit_seconds == bit_seconds;
    };
    const auto apply = [&](Point& point) {
        point.cpu_seconds_per_audio_second = sample.processing_ratio;
        point.rx_success_probability = probability;
    };
    // The simulation holds actual power and the channel fixed. Targets on
    // the same tile plateau share the receiver geometry in this sweep.
    for (auto& point : model.points)
        if (matches(point)) apply(point);
    if (!model.selected || !matches(*model.selected)) return;
    apply(*model.selected);
    model.sample_status = sample.status;
    if (probability) {
        // A two-sided Hoeffding bound for the fixed, balanced pair.
        const auto radius = std::sqrt(std::log(40.) / (2 * sample.trials));
        const auto lower = std::max(0., *probability - radius);
        const auto upper = std::min(1., *probability + radius);
        std::ostringstream interval;
        interval << sample.correct << '/' << sample.trials << " complete raw-bit successes · 95% sampling interval "
                 << std::floor(100 * lower) << "–" << std::ceil(100 * upper) << "%.";
        if (!model.sample_status.empty()) model.sample_status += " ";
        model.sample_status += interval.str();
    }
    if (sample.processing_ratio)
        model.cpu_status = "Slowest completed finite simulation, including channel generation and receiver work.";
    else
        model.cpu_status = "A complete simulation trial is needed to measure processing pace.";
}

std::string duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "Unavailable";
    const double scale = seconds >= 86400 ? 86400 : seconds >= 3600 ? 3600 : seconds >= 60 ? 60 : 1;
    std::ostringstream output;
    output << std::setprecision(3) << seconds / scale <<
        (scale == 86400 ? " days" : scale == 3600 ? " h" : scale == 60 ? " min" : " s");
    return output.str();
}
}
