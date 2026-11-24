#include "link_planner_model.hpp"

#include "datapump/execution.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "../triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace datapump::gui::planner {
namespace {
namespace native = triad::v3;
using Clock = std::chrono::steady_clock;

// Keep the probe bounded by actual admitted workspace and elapsed work rather
// than rejecting otherwise streamable geometries at an arbitrary sample count.
constexpr auto wall_budget = std::chrono::seconds(30);

void drain(native::CompleteStreamReceiver& receiver) {
    (void)receiver.drain_messages();
}
}

SampleResult sample(const Inputs& inputs, std::stop_token stop) {
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
    std::stop_source timed_stop;
    std::stop_callback external_stop(stop, [&] { timed_stop.request_stop(); });
    execution::Mutex timer_mutex;
    execution::StopCondition timer_condition;
    execution::Task deadline([&](std::stop_token timer_stop) {
        std::unique_lock lock(timer_mutex);
        timer_condition.wait_until(lock, timer_stop, started + wall_budget, [] { return false; });
        if (!timer_stop.stop_requested()) timed_stop.request_stop();
    });
    const auto work_stop = timed_stop.get_token();
    unsigned admitted=0, matches=0, quiet_pairs=0;
    try {
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
            const auto one_started = Clock::now();
            live::detail::Profile profile = live::detail::prepare_profile(settings, calendar, work_stop,
                {.simulation_seed = 0x4c494e4b0000ULL + trial});
            if (backend) profile.receiver.bulk_backend = backend;
            native::EmissionOptions emit_options;
            emit_options.mode = native::EmissionMode::Message;
            emit_options.slot_count = 1;
            emit_options.bits = std::span<const std::uint8_t>(&bits[trial], 1);
            emit_options.stop = work_stop;
            native::Emission emission(profile.waveform, profile.key, emit_options);
            auto channel_config = profile.channel;
            channel_config.minimum_capture_samples = physical_stop;
            native::StreamingChannel channel(emission.source(), channel_config);
            native::CompleteStreamReceiver receiver(profile.waveform, profile.receiver, profile.key,
                limits, message);
            std::stop_callback cancel(work_stop, [&] { receiver.request_cancel(); });
            std::array<native::Complex, live::detail::receiver_input_chunk_samples> iq{};
            std::array<std::uint8_t, live::detail::receiver_input_chunk_samples> valid{};
            while (!work_stop.stop_requested()) {
                execution::checkpoint();
                const auto first = channel.position();
                const auto count = channel.read(iq, valid);
                if (!count) break;
                auto pushed = receiver.push(first, std::span(iq).first(count), std::span(valid).first(count));
                while (pushed == native::StreamPushResult::backpressured && !work_stop.stop_requested()) {
                    const auto rejected = receiver.report();
                    drain(receiver);
                    if (!rejected.output_backpressured) {
                        (void)receiver.wait_for_slot_completion();
                        drain(receiver);
                    }
                    pushed = receiver.push(first, std::span(iq).first(count), std::span(valid).first(count));
                }
                if (pushed != native::StreamPushResult::accepted) {
                    result.status = "Receiver did not accept the complete sampled input.";
                    break;
                }
                const auto report = receiver.resume(false);
                drain(receiver);
                if (report.state == native::StreamReceiverState::failed ||
                    report.state == native::StreamReceiverState::resource_limit ||
                    report.state == native::StreamReceiverState::cancelled) {
                    result.status = report.reason;
                    break;
                }
            }
            if (work_stop.stop_requested()) break;
            if (!result.status.empty() || channel.position() != channel.total_samples()) break;
            auto report = receiver.finish_input();
            drain(receiver);
            while (report.state == native::StreamReceiverState::receiving && report.output_backpressured) {
                report = receiver.finish_input();
                drain(receiver);
            }
            if (report.state != native::StreamReceiverState::input_ended || !report.input_closed) {
                result.status = report.reason.empty() ? "Receiver did not finish processing the sampled input." : report.reason;
                break;
            }
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
            const auto elapsed = std::chrono::duration<double>(Clock::now() - one_started).count();
            const auto audio = static_cast<double>(channel.total_samples()) / profile.waveform.effective_sample_rate();
            if (elapsed > 0 && audio > 0)
                result.processing_ratio = std::max(result.processing_ratio.value_or(0.), elapsed / audio);
            ++result.trials;
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
    if (stop.stop_requested()) result.status = "Sampling cancelled.";
    else if (work_stop.stop_requested()) result.status = "The 30-second probe budget was reached after " +
        std::to_string(result.trials) + " complete trials.";
    if (result.trials && result.correct != result.trials) {
        result.status += " DATA admitted " + std::to_string(admitted) + ", correct " + std::to_string(matches) +
            " · Both quiet slots scored in " + std::to_string(quiet_pairs) + " of " +
            std::to_string(result.trials) + " trials.";
    }
    return result;
}
}
