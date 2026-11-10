#include "datapump/triad_stream_receiver.hpp"

#include "datapump/execution.hpp"
#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace datapump::triad::v3 {
namespace {
using namespace detail;
constexpr std::size_t sample_bytes = sizeof(Complex)+sizeof(std::uint8_t);
constexpr std::size_t input_overhead = 4096;
constexpr std::size_t raw_block_overhead = 512;
constexpr std::size_t slot_receipt_bytes = 4096;
constexpr std::size_t maximum_chunk = 16384;

std::size_t add(std::size_t a, std::size_t b) {
    if (b>std::numeric_limits<std::size_t>::max()-a) throw ReceiverWorkspaceError("Robust workspace overflow");
    return a+b;
}

std::size_t times(std::size_t a, std::size_t b) {
    if (a && b>std::numeric_limits<std::size_t>::max()/a) throw ReceiverWorkspaceError("Robust workspace overflow");
    return a*b;
}

std::size_t window_samples(const ReceiverSlotPlan& plan) {
    if (plan.stop_sample<=plan.first_sample || plan.stop_sample-plan.first_sample>std::numeric_limits<std::size_t>::max())
        throw Error("Invalid Robust public observation window");
    return static_cast<std::size_t>(plan.stop_sample-plan.first_sample);
}

std::size_t window_bytes(std::size_t samples) { return times(samples, sample_bytes); }

std::size_t receipt_reservation(const StreamReceiverOptions& stream, const MessageReceiverOptions& message,
                                unsigned owners) {
    // Charge bounded episode work, queued message metadata and the returned
    // copy, plus each live and completed slot record and one callback chunk.
    const auto message_bytes = add(4096, add(times(8, message.maximum_decoded_bytes),
        add(times(64, message.maximum_slots), times(8, message.maximum_data_bits))));
    const auto message_records = add(4, times(2, stream.maximum_pending_messages));
    const auto slot_records = add(times(2, stream.recent_slot_receipts), times(2, owners));
    return add(add(times(message_bytes, message_records), times(slot_receipt_bytes, slot_records)),
               times(64, stream.maximum_input_chunk_samples));
}

struct StreamWindow {
    std::vector<Complex> iq;
    std::vector<std::uint8_t> valid;
};

struct SlotWake {
    execution::Mutex mutex;
    execution::StopCondition changed;
};

class SlotTask {
public:
    SlotTask(SlotWake& wake, const ReceiverConfig& config, const Crypto& key, const StreamScorer& scorer,
             const ReceiverSlotPlan& plan, std::shared_ptr<const StreamWindow> input,
             unsigned workers, std::size_t workspace, std::stop_token stop)
        : task_([this, &wake, &config, &key, &scorer, plan, input = std::move(input), workers, workspace, stop]
                 (std::stop_token owner_stop) mutable {
            std::stop_source combined;
            std::stop_callback a(stop, [&] { combined.request_stop(); });
            std::stop_callback b(owner_stop, [&] { combined.request_stop(); });
            try {
                score_ = scorer(config, plan, key, input->iq, input->valid,
                    workers, workspace, combined.get_token());
            } catch (...) {
                failure_ = std::current_exception();
            }
            input.reset();
            {
                std::lock_guard lock(wake.mutex);
                ready_.store(true, std::memory_order_release);
            }
            wake.changed.notify_all();
        }) {}
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    std::optional<ReceiverSlotScore> poll() {
        if (!ready()) return {};
        task_.join();
        if (failure_) std::rethrow_exception(failure_);
        return std::move(score_);
    }
private:
    std::atomic<bool> ready_{false};
    std::optional<ReceiverSlotScore> score_;
    std::exception_ptr failure_;
    // Destroy this first, joining before the worker's result storage is freed.
    execution::Task task_;
};

struct RawBlock {
    std::uint64_t first;
    StreamWindow samples;
    std::uint64_t stop() const { return first+samples.iq.size(); }
    std::size_t initialized_bytes() const {
        return add(times(samples.iq.size(), sizeof(Complex)), samples.valid.size());
    }
    std::size_t bytes() const {
        return add(raw_block_overhead, add(times(samples.iq.capacity(), sizeof(Complex)), samples.valid.capacity()));
    }
};

struct Work {
    ReceiverSlotPlan plan;
    unsigned workers = 0;
    std::size_t dsp = 0, input = 0, reserved = 0;
};

struct Job {
    Work work;
    std::shared_ptr<const StreamWindow> input;
    std::unique_ptr<SlotTask> task;
    std::optional<ReceiverSlotScore> score;
    bool valid_observation = false, valid_interval = false;
};

} // namespace

struct CompleteStreamReceiver::Impl {
    PublicProfile profile;
    ReceiverConfig config;
    Crypto key;
    StreamReceiverOptions options;
    StreamReceiverResources resources;
    StreamWorkspace workspace;
    StreamScorer scorer;
    StreamFootprint footprint;
    ContinuousMessageReceiver codec;
    std::stop_source stop;
    const std::uint64_t last_public_slot;
    const std::size_t initial_footprint, stack_bytes, fixed_reservation, maximum_buffer;
    std::uint64_t input_stop, dispatched_stop;
    std::optional<ReceiverSlotPlan> next;
    std::deque<RawBlock> raw;
    std::size_t raw_bytes = 0, raw_initialized = 0;
    bool interval_valid = true, closed = false;
    std::size_t peak_reserved = 0;
    StreamReceiverState state = StreamReceiverState::receiving;
    std::string reason;
    SlotWake wake;
    std::deque<Job> jobs;
    std::deque<StreamSlotReceipt> receipts;

    Impl(PublicProfile profile_value, ReceiverConfig config_value, Crypto key_value,
         StreamReceiverOptions stream, MessageDecoder decoder, MessageReceiverOptions message,
         StreamWorkspace workspace_fn, StreamScorer score_fn, StreamFootprint footprint_fn)
        : profile(std::move(profile_value)), config(std::move(config_value)), key(std::move(key_value)),
          options(stream), resources(resolve_stream_receiver_resources(options)),
          workspace(std::move(workspace_fn)), scorer(std::move(score_fn)),
          footprint(footprint_fn ? std::move(footprint_fn) : StreamFootprint(stream_controller_footprint_bytes)),
          codec(std::move(decoder), message, options.first_slot, options.maximum_pending_messages),
          last_public_slot(profile.last_slot()), initial_footprint(footprint()),
          stack_bytes(stream_worker_stack_bytes()),
          fixed_reservation(receipt_reservation(options, message, resources.slot_owners)),
          maximum_buffer(options.maximum_buffer_samples.value_or(resources.memory_limit_bytes/4/sample_bytes)),
          input_stop(options.first_input_sample.value_or(0)), dispatched_stop(0) {
        if (!workspace || !scorer || !options.maximum_input_chunk_samples || options.maximum_input_chunk_samples>maximum_chunk ||
            !maximum_buffer || window_bytes(maximum_buffer)>resources.memory_limit_bytes ||
            options.maximum_gap_samples>maximum_buffer ||
            !options.recent_slot_receipts || options.recent_slot_receipts>1024)
            throw Error("Invalid local Robust stream input or receipt limits");
        const auto& declared = profile.config();
        if (config.sample_rate_hz!=profile.effective_sample_rate() || config.data_if_hz!=profile.data_if_hz() ||
            config.clock_bound_ppm!=declared.clock_bound_ppm || config.frequency_bound_hz!=declared.cfo_bound_hz ||
            config.helper_enabled!=declared.helper_enabled)
            throw Error("Robust scorer and public stream profile differ");
        if (options.first_slot>last_public_slot) throw Error("Robust initial slot is outside the configured calendar");
        peak_reserved = add(initial_footprint, fixed_reservation);
        if (peak_reserved>=resources.memory_limit_bytes)
            throw ReceiverWorkspaceError("Robust receiver controller exhausts the local memory limit");
        next = plan(options.first_slot);
        if (input_stop>=next->stop_sample) throw Error("Robust input starts after its first selected public window");
        dispatched_stop = next->first_sample;
        if (options.require_quiet_rearm || input_stop>next->first_sample)
            codec.invalidate_episode("capture_started_after_unknown_episode_boundary");
    }

    ~Impl() { stop.request_stop(); }

    ReceiverSlotPlan plan(std::uint64_t index) const {
        const auto slot = profile.slot(index);
        const auto seconds = [&](HelperBurstCoordinate coordinate) {
            return profile.calendar().relative_seconds(profile.origin(), coordinate);
        };
        ReceiverSlotPlan result;
        result.index = slot.index;
        result.first_sample = slot.observation.first;
        result.stop_sample = slot.observation.stop;
        result.quiet_first_sample = slot.quiet_aperture.first;
        result.quiet_stop_sample = slot.quiet_aperture.stop;
        result.format = profile.basis_format(slot);
        result.arrival_lower_seconds = profile.config().arrival_low_seconds;
        result.arrival_upper_seconds = profile.config().arrival_high_seconds;
        result.window_capture_lead_seconds = profile.config().capture_lead_seconds-
            static_cast<long double>(result.first_sample)/config.sample_rate_hz;
        result.source_start_seconds = seconds(slot.tx.support_begin);
        result.helper_begin_seconds = seconds(slot.tx.helper_begin);
        result.helper_end_seconds = seconds(slot.tx.helper_end);
        if (window_samples(result)>maximum_buffer || result.quiet_first_sample<result.first_sample ||
            result.quiet_stop_sample<result.quiet_first_sample || result.quiet_stop_sample>result.stop_sample)
            throw ReceiverWorkspaceError("Robust public observation exceeds the local input bound");
        return result;
    }

    bool backpressured() const { return codec.pending_messages()>=codec.maximum_pending_messages(); }
    bool ready() const { return next && input_stop>=next->stop_sample; }
    unsigned active() const {
        return static_cast<unsigned>(std::count_if(jobs.begin(), jobs.end(), [](const Job& job) { return bool(job.task); }));
    }
    unsigned active_workers() const {
        unsigned total = 0;
        for (const auto& job : jobs) if (job.task) total += job.work.workers;
        return total;
    }

    void terminate(StreamReceiverState result, const char* why) {
        stop.request_stop();
        codec.cancel();
        state = result;
        reason = why;
        // Job destruction joins the owner before releasing the immutable input.
        jobs.clear();
        raw.clear();
        raw_bytes = raw_initialized = 0;
    }

    std::size_t reserved() const {
        auto initialized = raw_initialized;
        auto prospective = add(fixed_reservation, raw_bytes-raw_initialized);
        for (const auto& job : jobs) if (job.task) {
            initialized = add(initialized, job.work.input);
            prospective = add(prospective, job.work.reserved-job.work.input);
        }
        // The raw and window payloads are already initialized and visible in
        // this process, with an explicit ownership floor. Whole-process growth
        // cannot prove that a scorer has materialized any of its remaining
        // bound. Keep the DSP, stacks and pending outputs reserved in addition
        // to the observed footprint, even when part is already resident.
        const auto observed = std::max(add(initial_footprint, initialized), footprint());
        return add(observed, prospective);
    }

    bool full_coverage(std::uint64_t first, std::uint64_t last) const {
        for (const auto& block : raw) {
            if (first>=last) break;
            if (block.stop()<=first) continue;
            if (block.first>first) return false;
            const auto stop_at = std::min(last, block.stop());
            const auto& mask = block.samples.valid;
            if (!mask.empty()) {
                const auto interval = std::span(mask).subspan(first-block.first, stop_at-first);
                if (std::ranges::find(interval, 0)!=interval.end()) return false;
            }
            first = stop_at;
        }
        return first>=last;
    }

    void advance(const ReceiverSlotPlan& current) {
        dispatched_stop = current.stop_sample;
        interval_valid = true;
        if (current.index==last_public_slot) next.reset();
        else {
            next = plan(current.index+1);
            if (next->first_sample<=current.first_sample || next->stop_sample<=current.stop_sample)
                throw Error("Robust public windows do not advance in physical time");
            interval_valid = full_coverage(dispatched_stop, std::min(next->first_sample, input_stop));
        }
        const auto first = next ? next->first_sample : input_stop;
        while (!raw.empty() && raw.front().stop()<=first) {
            raw_bytes -= raw.front().bytes();
            raw_initialized -= raw.front().initialized_bytes();
            raw.pop_front();
        }
    }

    StreamWindow assemble(const ReceiverSlotPlan& item) const {
        const auto count = window_samples(item);
        StreamWindow result{std::vector<Complex>(count), std::vector<std::uint8_t>(count, 0)};
        for (const auto& block : raw) {
            if (block.first>=item.stop_sample) break;
            if (block.stop()<=item.first_sample) continue;
            const auto first = std::max(block.first, item.first_sample);
            const auto last = std::min(block.stop(), item.stop_sample);
            const auto n = static_cast<std::size_t>(last-first);
            const auto source = static_cast<std::size_t>(first-block.first);
            const auto target = static_cast<std::size_t>(first-item.first_sample);
            std::copy_n(block.samples.iq.data()+source, n, result.iq.data()+target);
            if (block.samples.valid.empty()) std::fill_n(result.valid.data()+target, n, std::uint8_t{1});
            else std::copy_n(block.samples.valid.data()+source, n, result.valid.data()+target);
        }
        return result;
    }

    Work prepare(const ReceiverSlotPlan& plan, unsigned workers) const {
        const auto bytes = window_bytes(window_samples(plan));
        const auto dsp = workspace(config, plan, workers);
        if (dsp<bytes) throw Error("Robust slot workspace omitted the complete input lease");
        return {plan, workers, dsp, bytes, add(dsp, add(times(stack_bytes, workers), input_overhead))};
    }

    std::vector<Work> choose() const {
        const auto permits = resources.workers-active_workers();
        const auto capacity = std::min({static_cast<std::size_t>(permits),
            static_cast<std::size_t>(resources.slot_owners-active()),
            2*static_cast<std::size_t>(resources.slot_owners)-jobs.size()});
        std::vector<ReceiverSlotPlan> available;
        auto item = *next;
        for (std::size_t n=0; n<capacity; ++n) {
            if (input_stop<item.stop_sample) break;
            available.push_back(item);
            if (item.index==last_public_slot || n+1==capacity) break;
            item = plan(item.index+1);
        }
        const auto base = reserved();
        for (std::size_t width=available.size(); width; --width) {
            std::vector<Work> work;
            auto total = base;
            for (std::size_t i=0; i<width; ++i) {
                const auto share = static_cast<unsigned>(permits/width+(i>=width-permits%width));
                auto next_work = prepare(available[i], share);
                total = add(total, next_work.reserved);
                if (total>resources.memory_limit_bytes) break;
                work.push_back(next_work);
            }
            if (work.size()==width) return work;
        }
        return {};
    }

    void submit(const Work& work) {
        // The assembled arrays become the owner's immutable lease. No second
        // input allocation is needed when the scorer takes its spans.
        auto values = assemble(work.plan);
        const bool valid = std::find(values.valid.begin(), values.valid.end(), 0)==values.valid.end();
        const auto bytes = add(times(values.iq.capacity(), sizeof(Complex)), values.valid.capacity());
        if (bytes>add(window_bytes(window_samples(work.plan)), input_overhead))
            throw ReceiverWorkspaceError("Robust assembled input exceeded its admitted lease");
        auto input = std::make_shared<const StreamWindow>(std::move(values));
        jobs.push_back({work, std::move(input), {}, {}, valid, interval_valid});
        auto& job = jobs.back();
        job.task = std::make_unique<SlotTask>(wake, config, key, scorer, work.plan, job.input,
            work.workers, work.dsp, stop.get_token());
        advance(work.plan);
    }

    void dispatch() {
        if (!ready() || backpressured() || active()>=resources.slot_owners ||
            active_workers()>=resources.workers || jobs.size()>=2*static_cast<std::size_t>(resources.slot_owners)) return;
        const auto work = choose();
        if (work.empty() && !active())
            throw ReceiverWorkspaceError("Robust public window exceeds the admitted workspace");
        for (const auto& item : work) {
            if (stop.stop_requested()) break;
            const auto admitted = add(reserved(), item.reserved);
            if (admitted>resources.memory_limit_bytes) {
                if (active()) return;
                throw ReceiverWorkspaceError("Robust process growth exhausted the local workspace");
            }
            peak_reserved = std::max(peak_reserved, admitted);
            submit(item);
        }
    }

    void accept_score(Job& job, ReceiverSlotScore score) {
        const auto& p = job.work.plan;
        if (score.index!=p.index || score.first_sample!=p.first_sample || score.stop_sample!=p.stop_sample ||
            score.quiet_first_sample!=p.quiet_first_sample || score.quiet_stop_sample!=p.quiet_stop_sample ||
            score.sample_rate_hz!=config.sample_rate_hz || score.consumed_samples>window_samples(p) ||
            score.helper_status.size()>256 || score.reason.size()>256)
            throw Error("Robust slot owner returned an invalid receipt");
        if (score.workspace_peak_bytes>job.work.dsp)
            throw ReceiverWorkspaceError("Robust slot owner exceeded its admitted DSP lease");
        score.all_valid = score.all_valid && job.valid_observation;
        score.decision = QuietSlotMessageReceiver::classification(score);
        if (score.decision!=SlotDecision::data) score.bit.reset();
        job.score = std::move(score);
        job.input.reset();
    }

    void collect() {
        for (auto& job : jobs) if (job.task) {
            auto result = job.task->poll();
            if (!result) continue;
            job.task.reset();
            accept_score(job, std::move(*result));
        }
    }

    void commit() {
        while (!jobs.empty() && jobs.front().score && !backpressured() && !stop.stop_requested()) {
            auto& job = jobs.front();
            if (!job.valid_observation || !job.valid_interval)
                codec.invalidate_episode("physical_input_was_incomplete");
            if (!codec.consume(*job.score) || codec.stopped()) throw Error("Robust public slot commit failed");
            if (receipts.size()==options.recent_slot_receipts) receipts.pop_front();
            receipts.push_back({std::move(*job.score), job.work.plan.format.epoch,
                job.work.plan.format.symbol_ordinal, job.work.workers});
            jobs.pop_front();
        }
    }

    void pump(bool wait) {
        if (state!=StreamReceiverState::receiving) return;
        try {
            do {
                if (stop.stop_requested()) {
                    terminate(StreamReceiverState::cancelled, "receive_cancelled_before_physical_completion");
                    return;
                }
                collect();
                commit();
                if (backpressured()) return;
                dispatch();
                if (jobs.empty() && !ready() && (closed || !next)) {
                    codec.finish_input();
                    state = closed ? StreamReceiverState::input_ended : StreamReceiverState::failed;
                    reason = closed ? "transport_ended" : "configured_public_calendar_exhausted";
                    raw.clear();
                    raw_bytes = raw_initialized = 0;
                    return;
                }
                if (!wait || !active()) return;
                std::unique_lock lock(wake.mutex);
                wake.changed.wait(lock, stop.get_token(), [&] {
                    return std::any_of(jobs.begin(), jobs.end(), [](const Job& job) {
                        return job.task && job.task->ready();
                    });
                });
            } while (true);
        } catch (const ReceiverWorkspaceError&) {
            terminate(StreamReceiverState::resource_limit, "receiver_workspace_limit");
        } catch (const std::bad_alloc&) {
            terminate(StreamReceiverState::resource_limit, "receiver_workspace_limit");
        } catch (...) {
            terminate(stop.stop_requested() ? StreamReceiverState::cancelled : StreamReceiverState::failed,
                stop.stop_requested() ? "receive_cancelled_before_physical_completion" : "complete_slot_processing_failed");
        }
    }

    StreamPushResult push(std::uint64_t first, std::span<const Complex> iq, std::span<const std::uint8_t> valid) {
        if (closed || state!=StreamReceiverState::receiving) throw Error("Robust stream input is closed");
        if (iq.empty() || iq.size()>options.maximum_input_chunk_samples || (!valid.empty() && valid.size()!=iq.size()) ||
            first<input_stop || first-input_stop>options.maximum_gap_samples || iq.size()>std::numeric_limits<std::uint64_t>::max()-first)
            throw Error("Invalid Robust stream chunk extent or sample origin");
        if (std::any_of(iq.begin(), iq.end(), [](const Complex& x) {
                return !std::isfinite(x.real()) || !std::isfinite(x.imag());
            }) || std::any_of(valid.begin(), valid.end(), [](auto value) { return value>1; }))
            throw Error("Invalid Robust stream samples or validity mask");
        pump(false);
        if (state!=StreamReceiverState::receiving) throw Error("Robust stream input is closed");
        if (backpressured()) return StreamPushResult::backpressured;
        const auto last = first+iq.size();
        const auto keep = next ? std::max(first, next->first_sample) : last;
        const auto count = keep<last ? static_cast<std::size_t>(last-keep) : 0;
        if (next && last>next->first_sample && last-next->first_sample>add(maximum_buffer, options.maximum_input_chunk_samples))
            return StreamPushResult::backpressured;
        if (count) {
            const auto required = add(raw_block_overhead, window_bytes(count));
            const auto admitted = add(reserved(), required);
            if (admitted>resources.memory_limit_bytes) {
                if (active()) return StreamPushResult::backpressured;
                throw ReceiverWorkspaceError("Robust captured input exceeds bounded backlog memory");
            }
            const auto begin = static_cast<std::size_t>(keep-first);
            const auto kept_iq = iq.subspan(begin);
            RawBlock block{keep, {{kept_iq.begin(), kept_iq.end()}, {}}};
            if (!valid.empty()) {
                const auto kept_valid = valid.subspan(begin);
                block.samples.valid.assign(kept_valid.begin(), kept_valid.end());
            }
            const auto stored = add(raw_bytes, block.bytes());
            const auto initialized = add(raw_initialized, block.initialized_bytes());
            raw.push_back(std::move(block));
            raw_bytes = stored;
            raw_initialized = initialized;
            peak_reserved = std::max(peak_reserved, admitted);
        }
        if (next && dispatched_stop<next->first_sample) {
            if (std::max(input_stop, dispatched_stop)<std::min(first, next->first_sample)) interval_valid = false;
            const auto begin = std::max(first, dispatched_stop), end = std::min(last, next->first_sample);
            if (!valid.empty() && begin<end) {
                const auto interval = valid.subspan(begin-first, end-begin);
                if (std::ranges::find(interval, 0)!=interval.end()) interval_valid = false;
            }
        }
        input_stop = last;
        pump(false);
        return StreamPushResult::accepted;
    }

    StreamReceiverReport report() const {
        StreamReceiverReport result;
        result.state = state;
        result.resources = resources;
        result.reason = reason;
        result.input_stop = input_stop;
        result.next_public_slot = codec.next_slot_index();
        result.pending_messages = codec.pending_messages();
        result.active_owners = active();
        result.uncommitted_slots = jobs.size();
        result.peak_reserved_bytes = peak_reserved;
        const auto& current = codec.result();
        result.observed_bits = current.received_bits.size();
        result.erasures = current.erasure_slot_indices.size();
        if (current.slots_consumed && !codec.recovering())
            result.episode_first_public_slot = codec.next_slot_index()-current.slots_consumed;
        result.input_closed = closed;
        result.output_backpressured = backpressured();
        result.recovering = codec.recovering();
        return result;
    }
};

CompleteStreamReceiver::CompleteStreamReceiver(PublicProfile profile, ReceiverConfig config, Crypto key,
    StreamReceiverOptions options, MessageDecoder decoder, MessageReceiverOptions message)
    : impl_(std::make_unique<Impl>(std::move(profile), std::move(config), std::move(key), options,
        std::move(decoder), message, receiver_slot_workspace, score_receiver_slot, StreamFootprint{})) {}
CompleteStreamReceiver::CompleteStreamReceiver(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompleteStreamReceiver::~CompleteStreamReceiver() = default;

StreamPushResult CompleteStreamReceiver::push(std::uint64_t first, std::span<const Complex> iq,
    std::span<const std::uint8_t> valid) { return impl_->push(first, iq, valid); }

StreamReceiverReport CompleteStreamReceiver::resume(bool wait) { impl_->pump(wait); return report(); }
std::vector<CompletedMessage> CompleteStreamReceiver::drain_messages() { return impl_->codec.drain_messages(); }
std::vector<StreamSlotReceipt> CompleteStreamReceiver::drain_recent_slots() {
    std::vector<StreamSlotReceipt> result;
    result.reserve(impl_->receipts.size());
    for (auto& receipt : impl_->receipts) result.push_back(std::move(receipt));
    impl_->receipts.clear();
    return result;
}
StreamReceiverReport CompleteStreamReceiver::report() const { return impl_->report(); }
StreamReceiverReport CompleteStreamReceiver::finish_input() {
    impl_->closed = true;
    return resume(true);
}
StreamReceiverReport CompleteStreamReceiver::cancel() {
    if (impl_->state==StreamReceiverState::receiving)
        impl_->terminate(StreamReceiverState::cancelled, "receive_cancelled_before_physical_completion");
    return report();
}
void CompleteStreamReceiver::request_cancel() noexcept { impl_->stop.request_stop(); }

CompleteStreamReceiver detail::StreamReceiverFixture::make(PublicProfile profile, ReceiverConfig config, Crypto key,
    StreamReceiverOptions options, StreamWorkspace workspace, StreamScorer scorer,
    MessageDecoder decoder, MessageReceiverOptions message, StreamFootprint footprint) {
    return CompleteStreamReceiver(std::make_unique<CompleteStreamReceiver::Impl>(std::move(profile),
        std::move(config), std::move(key), options, std::move(decoder), message,
        std::move(workspace), std::move(scorer), std::move(footprint)));
}

} // namespace datapump::triad::v3
