#include "link_planner_model.hpp"

#include "datapump/execution.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "../triad_receiver_coarse.hpp"
#include "../triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>

namespace datapump::gui::planner {
namespace {
namespace native = triad::v3;
using Clock = std::chrono::steady_clock;

constexpr auto progress_period = std::chrono::milliseconds(500);

void drain(native::CompleteStreamReceiver& receiver) {
    (void)receiver.drain_messages();
}

void check_receiving(const native::StreamReceiverReport& report) {
    if (report.state != native::StreamReceiverState::receiving)
        throw Error(report.reason.empty() ? "Receiver stopped accepting the sampled input." : report.reason);
}
}

SampleResult sample(const Inputs& inputs, std::stop_token stop,
                    const std::function<void(const SampleProgress&)>& progress, SampleBudget budget) {
    SampleResult result;
    if (!inputs.settings) {
        result.status = "Receiver settings are unavailable.";
        return result;
    }
    auto settings = *inputs.settings;
    settings.tx_dbm = inputs.tx_dbm;
    settings.path_loss_db = inputs.path_loss_db;
    settings.noise_dbm_hz = inputs.noise_density_dbm_hz;
    settings.target_cn0_db_hz = inputs.target_db_hz;
    if (settings.gpu && inputs.backend.expired()) {
        result.status = "The selected GPU backend is not ready; no CPU fallback was sampled.";
        return result;
    }
    settings.simulation = true;
    // The GUI's wrong-key selection filters displayed records only.
    settings.wrong_key_admission = "current";
    const auto calendar = settings.calendar.value_or(live::detail::automatic_live_calendar);
    const auto started = Clock::now();
    const auto wall_budget = std::chrono::seconds(budget == SampleBudget::extended ?
        extended_probe_seconds : interactive_probe_seconds);
    const auto work_deadline = started + wall_budget;
    std::stop_source timed_stop;
    std::stop_callback external_stop(stop, [&] { timed_stop.request_stop(); });
    execution::Mutex timer_mutex;
    execution::StopCondition timer_condition;
    execution::Task deadline([&](std::stop_token timer_stop) {
        std::unique_lock lock(timer_mutex);
        timer_condition.wait_until(lock, timer_stop, work_deadline, [] { return false; });
        if (!timer_stop.stop_requested()) timed_stop.request_stop();
    });
    const auto work_stop = timed_stop.get_token();
    std::shared_ptr<native::receiver_detail::CoarseProbeProgress> coarse_probe;
    SampleProgress attempt;
    auto attempt_started = started;
    auto next_progress = started;
    bool attempt_active = true;
    bool attempt_ended = false;
    const auto update_progress = [&](bool force = false) {
        const auto now = Clock::now();
        attempt.elapsed_seconds = std::chrono::duration<double>(now - attempt_started).count();
        if (progress && !work_stop.stop_requested() && (force || now >= next_progress)) {
            next_progress = now + progress_period;
            progress(attempt);
        }
    };
    unsigned admitted=0, matches=0, quiet_pairs=0;
    try {
        update_progress(true);
        execution::checkpoint();
        auto plan = live::detail::plan_profile(settings, calendar, work_stop);
        const auto physical_stop = plan.waveform.slot(2).observation.stop;
        native::StreamReceiverOptions limits;
        limits.memory_limit_bytes = settings.receiver_memory_limit;
        limits.maximum_input_chunk_samples = live::detail::receiver_input_chunk_samples;
        limits.maximum_pending_messages = live::detail::receiver_pending_messages;
        const auto resources = native::resolve_stream_receiver_resources(limits);
        result.workers = resources.workers;
        limits.workers = resources.workers;
        limits.slot_owners = resources.slot_owners;
        limits.memory_limit_bytes = resources.memory_limit_bytes;
        native::NativeMessageProfile message{settings.transfer.fec, settings.transfer.compression,
            std::min(settings.transfer.content_limit, native::native_message_editor_limit), settings.transfer.key};
        const auto backend = settings.gpu ? inputs.backend.lock() : nullptr;
        if (backend) plan.receiver.bulk_backend = backend;
        if (settings.gpu && !backend) {
            result.status = "The selected GPU backend is not ready; no CPU fallback was sampled.";
            return result;
        }
        const auto external_bytes = plan.waveform.emission_source_workspace_bytes() +
            native::StreamingChannel::maximum_working_bytes;
        const auto quote = native::detail::quote_stream_workspace(plan.waveform, plan.receiver, limits,
            message, 2, external_bytes, work_stop);
        if (!quote.fits) {
            result.status = "Selected receive geometry exceeds the admitted DSP workspace.";
            return result;
        }

        if (resources.memory_limit_bytes <= external_bytes) {
            result.status = "Selected channel and waveform storage exhaust the receiver memory allowance.";
            return result;
        }
        limits.memory_limit_bytes = resources.memory_limit_bytes - external_bytes;
        // Score two balanced pairs. A partial pair does not enter the RX estimate.
        constexpr std::array<std::uint8_t, SampleResult::trial_budget> bits{0, 1, 0, 1};
        for (unsigned trial = 0; trial < bits.size(); ++trial) {
            execution::checkpoint();
            if (work_stop.stop_requested()) break;
            attempt = {.stage = SampleProgress::Stage::preparing, .trial = trial + 1};
            attempt_started = Clock::now();
            attempt_active = true;
            attempt_ended = false;
            coarse_probe.reset();
            update_progress(true);
            live::detail::Profile profile = live::detail::prepare_profile(settings, calendar, work_stop,
                {.simulation_seed = 0x4c494e4b0000ULL + trial});
            if (backend) profile.receiver.bulk_backend = backend;
            if (!settings.gpu) {
                coarse_probe = std::make_shared<native::receiver_detail::CoarseProbeProgress>();
                profile.receiver.coarse_probe_progress = coarse_probe;
            }
            native::EmissionOptions emit_options;
            emit_options.mode = native::EmissionMode::Message;
            emit_options.slot_count = 1;
            emit_options.bits = std::span<const std::uint8_t>(&bits[trial], 1);
            emit_options.stop = work_stop;
            native::Emission emission(profile.waveform, profile.key, emit_options);
            auto channel_config = profile.channel;
            channel_config.minimum_capture_samples = physical_stop;
            native::StreamingChannel channel(emission.source(), channel_config);
            const auto rate = profile.waveform.effective_sample_rate();
            attempt.episode_seconds = static_cast<double>(channel.total_samples()) / rate;
            native::CompleteStreamReceiver receiver(profile.waveform, profile.receiver, profile.key, limits, message);
            std::stop_callback cancel(work_stop, [&] { receiver.request_cancel(); });
            const auto observe = [&](const native::StreamReceiverReport& report, bool force = false) {
                attempt.input_seconds = static_cast<double>(report.input_stop) / rate;
                attempt.scored_slots = static_cast<unsigned>(std::min<std::uint64_t>(3, report.next_public_slot));
                update_progress(force);
            };
            attempt.stage = SampleProgress::Stage::feeding;
            update_progress(true);
            std::array<native::Complex, live::detail::receiver_input_chunk_samples> iq{};
            std::array<std::uint8_t, live::detail::receiver_input_chunk_samples> valid{};
            try {
                while (!work_stop.stop_requested()) {
                    execution::checkpoint();
                    const auto first = channel.position();
                    const auto count = channel.read(iq, valid);
                    if (!count || work_stop.stop_requested()) break;
                    while (!work_stop.stop_requested() &&
                        receiver.push(first, std::span(iq).first(count), std::span(valid).first(count)) ==
                            native::StreamPushResult::backpressured) {
                        const auto rejected = receiver.report();
                        check_receiving(rejected);
                        drain(receiver);
                        if (!rejected.output_backpressured) {
                            check_receiving(receiver.wait_for_slot_completion());
                            drain(receiver);
                        }
                        const auto after = receiver.report();
                        observe(after);
                        const bool output_drained = rejected.output_backpressured && !after.output_backpressured;
                        if (!output_drained && after.state == rejected.state && !after.active_owners &&
                            !after.output_backpressured && after.next_public_slot == rejected.next_public_slot &&
                            after.uncommitted_slots == rejected.uncommitted_slots)
                            throw Error("Sampled input cannot advance within the configured receiver workspace.");
                    }
                    // push() has already dispatched available work.
                    const auto report = receiver.report();
                    observe(report);
                    check_receiving(report);
                    drain(receiver);
                }
                if (!work_stop.stop_requested() && channel.position() == channel.total_samples()) {
                    attempt.stage = SampleProgress::Stage::scoring;
                    update_progress(true);
                    auto report = receiver.finish_input(false);
                    while (!work_stop.stop_requested() && report.state == native::StreamReceiverState::receiving) {
                        drain(receiver);
                        report = receiver.wait_for_slot_completion();
                        observe(report, report.next_public_slot > attempt.scored_slots);
                    }
                    drain(receiver);
                    if (!work_stop.stop_requested() &&
                        (report.state != native::StreamReceiverState::input_ended || !report.input_closed))
                        throw Error(report.reason.empty() ? "Receiver did not finish processing the sampled input." : report.reason);
                }
            } catch (...) {
                const auto report = receiver.report();
                observe(report);
                attempt_ended = true;
                if (report.state != native::StreamReceiverState::input_ended) check_receiving(report);
                throw;
            }
            const auto report = receiver.report();
            observe(report);
            attempt_ended = true;
            if (work_stop.stop_requested() && report.state != native::StreamReceiverState::input_ended) break;
            auto receipts = receiver.drain_recent_slots();
            if (receipts.size() != 3 || receipts[0].score.index != 0 ||
                receipts[1].score.index != 1 || receipts[2].score.index != 2) {
                result.status = "The sample committed " + std::to_string(receipts.size()) +
                    " slots instead of the requested DATA and two quiet slots.";
                break;
            }
            const bool data = receipts[0].score.complete && receipts[0].score.decision == native::SlotDecision::data;
            const bool match = data && receipts[0].score.bit && *receipts[0].score.bit == bits[trial];
            const bool quiet = receipts[1].score.complete && receipts[2].score.complete &&
                receipts[1].score.decision == native::SlotDecision::quiet &&
                receipts[2].score.decision == native::SlotDecision::quiet;
            if (attempt.elapsed_seconds > 0 && attempt.episode_seconds > 0)
                result.processing_ratio = std::max(result.processing_ratio.value_or(0.),
                    attempt.elapsed_seconds / attempt.episode_seconds);
            ++result.trials;
            attempt_active = false;
            admitted += data;
            matches += match;
            quiet_pairs += quiet;
            if (match && quiet) {
                ++result.correct;
                if (trial < 2) ++result.first_pair_correct;
            }
        }
        if (result.trials == bits.size()) result.status = settings.gpu ?
            "Selected GPU route with native CPU stages." : "Native CPU route.";
        else if (result.status.empty()) result.status = "The probe did not score all four trials.";
    } catch (const std::exception& error) {
        result.status = error.what();
    }
    deadline.request_stop();
    deadline.join();
    if (stop.stop_requested()) result.status = "Sampling cancelled.";
    else if (work_stop.stop_requested() && result.trials < SampleResult::trial_budget) {
        if (attempt_active) {
            if (!attempt_ended) update_progress();
            // A cooperative stop may spend extra time unwinding the receiver.
            const auto work_seconds = std::chrono::duration<double>(work_deadline - attempt_started).count();
            attempt.elapsed_seconds = std::min(attempt.elapsed_seconds, std::max(0., work_seconds));
            // The receiver and its workers have finished unwinding at this point.
            if (coarse_probe) attempt.coarse_probe = coarse_probe->observe(work_deadline);
            result.interrupted_trial = attempt;
        }
        result.status = "The " + std::to_string(wall_budget.count()) + "-second probe budget was reached after " +
            std::to_string(result.trials) + " complete trials.";
    }
    if (result.trials && result.correct != result.trials) {
        result.status += " DATA admitted " + std::to_string(admitted) + ", correct " + std::to_string(matches) +
            " · Both quiet slots scored in " + std::to_string(quiet_pairs) + " of " +
            std::to_string(result.trials) + " trials.";
    }
    return result;
}
}
