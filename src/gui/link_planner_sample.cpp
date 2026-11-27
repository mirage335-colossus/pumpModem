#include "link_planner_model.hpp"

#include "datapump/execution.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "../triad_receiver_coarse.hpp"
#include "../triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
                    const std::function<void(const SampleProgress&)>& progress) {
    SampleResult result;
    if (stop.stop_requested()) {
        result.status = "Sampling cancelled.";
        return result;
    }
    if (!inputs.settings) {
        result.status = "Receiver settings are unavailable.";
        return result;
    }
    if (const auto reason = live::detail::receiver_worker_unavailability()) {
        result.status = *reason;
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
    const auto backend_label = settings.gpu ? (inputs.backend_id.empty() ? "GPU backend" : inputs.backend_id) : "Native CPU";
    SampleProgress attempt;
    attempt.backend_id = backend_label;
    auto attempt_started = started;
    auto next_progress = started;
    const auto update_progress = [&](bool force = false) {
        const auto now = Clock::now();
        attempt.elapsed_seconds = std::chrono::duration<double>(now - attempt_started).count();
        if (progress && !stop.stop_requested() && (force || now >= next_progress)) {
            next_progress = now + progress_period;
            progress(attempt);
        }
    };
    unsigned admitted=0, matches=0, quiet_pairs=0;
    try {
        update_progress(true);
        execution::checkpoint();
        auto plan = live::detail::plan_profile(settings, calendar, stop);
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
            message, 2, external_bytes, stop);
        if (!quote.fits) {
            result.status = "Selected receive geometry exceeds the admitted DSP workspace.";
            return result;
        }

        if (resources.memory_limit_bytes <= external_bytes) {
            result.status = "Selected channel and waveform storage exhaust the receiver memory allowance.";
            return result;
        }
        limits.memory_limit_bytes = resources.memory_limit_bytes - external_bytes;
        // Score one complete public episode for each bit value.
        constexpr std::array<std::uint8_t, SampleResult::trial_budget> bits{0, 1};
        for (unsigned trial = 0; trial < bits.size(); ++trial) {
            execution::checkpoint();
            if (stop.stop_requested()) break;
            attempt = {.stage = SampleProgress::Stage::preparing, .trial = trial + 1};
            attempt.backend_id = backend_label;
            attempt_started = Clock::now();
            update_progress(true);
            live::detail::Profile profile = live::detail::prepare_profile(settings, calendar, stop,
                {.simulation_seed = 0x4c494e4b0000ULL + trial});
            if (backend) profile.receiver.bulk_backend = backend;
            attempt.live_probe = std::make_shared<native::receiver_detail::CoarseProbeProgress>();
            profile.receiver.coarse_probe_progress = attempt.live_probe;
            native::EmissionOptions emit_options;
            emit_options.mode = native::EmissionMode::Message;
            emit_options.slot_count = 1;
            emit_options.bits = std::span<const std::uint8_t>(&bits[trial], 1);
            emit_options.stop = stop;
            native::Emission emission(profile.waveform, profile.key, emit_options);
            auto channel_config = profile.channel;
            channel_config.minimum_capture_samples = physical_stop;
            native::StreamingChannel channel(emission.source(), channel_config);
            const auto rate = profile.waveform.effective_sample_rate();
            attempt.episode_seconds = static_cast<double>(channel.total_samples()) / rate;
            native::CompleteStreamReceiver receiver(profile.waveform, profile.receiver, profile.key, limits, message);
            std::stop_callback cancel(stop, [&] { receiver.request_cancel(); });
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
                while (!stop.stop_requested()) {
                    execution::checkpoint();
                    const auto first = channel.position();
                    const auto count = channel.read(iq, valid);
                    if (!count || stop.stop_requested()) break;
                    while (!stop.stop_requested() &&
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
                if (!stop.stop_requested() && channel.position() == channel.total_samples()) {
                    attempt.stage = SampleProgress::Stage::scoring;
                    update_progress(true);
                    auto report = receiver.finish_input(false);
                    while (!stop.stop_requested() && report.state == native::StreamReceiverState::receiving) {
                        drain(receiver);
                        report = receiver.wait_for_slot_completion();
                        observe(report, report.next_public_slot > attempt.scored_slots);
                    }
                    drain(receiver);
                    if (!stop.stop_requested() &&
                        (report.state != native::StreamReceiverState::input_ended || !report.input_closed))
                        throw Error(report.reason.empty() ? "Receiver did not finish processing the sampled input." : report.reason);
                }
            } catch (...) {
                const auto report = receiver.report();
                observe(report);
                if (report.state != native::StreamReceiverState::input_ended) check_receiving(report);
                throw;
            }
            const auto report = receiver.report();
            observe(report);
            if (stop.stop_requested()) break;
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
            admitted += data;
            matches += match;
            quiet_pairs += quiet;
            if (match && quiet) ++result.correct;
        }
        if (result.trials == bits.size()) result.status = "Configured backend · " + backend_label + ".";
        else if (result.status.empty()) result.status = "The receiver did not complete the requested trials.";
    } catch (const std::exception& error) {
        result.status = error.what();
    }
    if (stop.stop_requested()) return {.status = "Sampling cancelled."};
    if (result.trials < SampleResult::trial_budget && attempt.trial) {
        result.status += " " + sample_progress_text(attempt) + ".";
    }
    if (result.trials && result.correct != result.trials) {
        result.status += " DATA admitted " + std::to_string(admitted) + ", correct " + std::to_string(matches) +
            " · Both quiet slots scored in " + std::to_string(quiet_pairs) + " of " +
            std::to_string(result.trials) + " trials.";
    }
    return result;
}
}
