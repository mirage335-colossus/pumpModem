#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::detail;
using namespace std::chrono_literals;

void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

PublicProfile profile(bool overlap = false) {
    PublicProfileConfig c;
    c.format.sample_rate = 96;
    c.format.inner_rate_hz = 36;
    c.format.added_symbols = 1;
    c.format.added_interleave = false;
    c.format.bit_seconds = 2;
    c.format.encrypt = false;
    c.format.epoch = 1800000000;
    c.format.symbol_ordinal = 91;
    c.data_if_hz = 24;
    c.capture_lead_seconds = overlap ? 64 : 0;
    return PublicProfile(c);
}

ReceiverConfig receiver(const PublicProfile& p) {
    ReceiverConfig c;
    c.sample_rate_hz = p.effective_sample_rate();
    c.data_if_hz = p.data_if_hz();
    c.noise_variance = 1;
    return c;
}

StreamReceiverOptions options() {
    StreamReceiverOptions o;
    o.workers = 4;
    o.slot_owners = 2;
    o.memory_limit_bytes = 256*1024*1024;
    return o;
}

std::size_t workspace(const ReceiverConfig&, const ReceiverSlotPlan& p, unsigned) {
    return (p.stop_sample-p.first_sample)*(sizeof(Complex)+1)+4096;
}

ReceiverSlotScore row(const ReceiverConfig& c, const ReceiverSlotPlan& p,
                      SlotDecision decision, std::uint8_t bit = 0) {
    ReceiverSlotScore r;
    r.index = p.index;
    r.first_sample = p.first_sample;
    r.stop_sample = p.stop_sample;
    r.quiet_first_sample = p.quiet_first_sample;
    r.quiet_stop_sample = p.quiet_stop_sample;
    r.sample_rate_hz = c.sample_rate_hz;
    r.consumed_samples = p.stop_sample-p.first_sample;
    r.decision = decision;
    r.complete = r.all_valid = r.zero_h0_verified = r.noise_statistics_valid = true;
    r.quiet_energy = 1;
    r.quiet_threshold = 2;
    r.quiet_modes = 20;
    r.operational_log_e = 30;
    r.operational_threshold = 20;
    r.workspace_peak_bytes = workspace(c, p, 1);
    if (decision==SlotDecision::data) {
        r.bit = bit;
        r.operational_passes_conditional_bound = true;
        if (bit<2) r.operational_bit_evidence[bit]=30;
        r.operational_bit_log_separation=30;
    }
    return r;
}

ReceiverSlotScore single_e(const ReceiverConfig& c, const ReceiverSlotPlan& p, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token) {
    return p.index<3 ? row(c, p, SlotDecision::data, p.index==2) : row(c, p, SlotDecision::quiet);
}

void feed(CompleteStreamReceiver& r, std::uint64_t stop, std::uint64_t skip = 0) {
    std::vector<Complex> samples(16384, {1, -2});
    auto first = r.report().input_stop+skip;
    while (first<stop) {
        const auto count = std::min<std::uint64_t>(stop-first, samples.size());
        if (r.push(first, std::span(samples).first(count))==StreamPushResult::backpressured) {
            const auto report = r.resume(true);
            check(report.state==StreamReceiverState::receiving && !report.output_backpressured,
                  "fixture input unexpectedly could not advance");
        } else first += count;
    }
}

void order_and_backpressure() {
    const auto p = profile(true);
    auto o = options();
    o.maximum_pending_messages = 1;
    constexpr std::size_t mib = 1024*1024;
    o.memory_limit_bytes = 96*mib + *o.workers*stream_worker_stack_bytes();
    std::atomic<bool> later_finished = false, third_finished = false, release_first = false;
    std::array<std::atomic<unsigned>, 2> shares{};
    auto score = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto& key,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory, std::stop_token stop) {
        check(iq.size()==valid.size() && iq.size()==plan.stop_sample-plan.first_sample,
            "incorrect complete window extent");
        check(std::all_of(valid.begin(), valid.end(), [](auto v) { return v==1; }) &&
            std::all_of(iq.begin(), iq.end(), [](auto v) { return v==Complex(1,-2); }),
            "overlap window lost or changed actual samples");
        if (plan.index<shares.size()) shares[plan.index] = workers;
        if (!plan.index) {
            while (!release_first && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
        } else if (plan.index==1) later_finished = true;
        else if (plan.index==2) third_finished = true;
        return single_e(c, plan, key, iq, valid, workers, memory, stop);
    };
    const auto quoted = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, unsigned workers) {
        return workspace(c, plan, workers) + (workers==*o.workers ? 64*mib : 8*mib);
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, quoted, score,
        decode_short_message, {}, [] { return 8*mib; });
    const auto two = p.slot(1).observation.stop;
    check(two<=o.maximum_input_chunk_samples, "fixture does not admit both windows at once");
    feed(stream, two);
    const auto deadline = std::chrono::steady_clock::now()+2s;
    while ((!later_finished || !shares[0]) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(1ms);
    check(later_finished && shares[0]==2 && shares[1]==2,
          "a wider ready group did not fall back to balanced scratch shapes under RAM pressure");
    check(stream.resume().next_public_slot==0, "out-of-order slot was committed early");
    feed(stream, p.slot(2).observation.stop);
    const auto third_deadline = std::chrono::steady_clock::now()+2s;
    while (!third_finished && std::chrono::steady_clock::now()<third_deadline) {
        stream.resume();
        std::this_thread::sleep_for(1ms);
    }
    check(third_finished && stream.report().next_public_slot==0,
          "completed later owner did not release its permits before ordered commit");
    release_first = true;

    feed(stream, p.slot(4).observation.stop);
    auto state = stream.resume(true);
    check(state.state==StreamReceiverState::receiving && state.output_backpressured && state.next_public_slot==5,
          "scored completion did not apply output backpressure");
    const std::array extra{Complex{1,-2}};
    check(stream.push(state.input_stop, extra)==StreamPushResult::backpressured &&
          stream.report().input_stop==state.input_stop, "backpressure consumed captured input");
    const auto rejected = stream.report();
    check(rejected.output_backpressured && rejected.active_owners==0,
          "fixture did not reach output-only backpressure");
    const auto messages = stream.drain_messages();
    check(messages.size()==1 && messages.front().first_public_slot==0 && messages.front().stop_public_slot==5 &&
          messages.front().message.physical_complete && messages.front().message.filtered_text=="e",
          "ordered native scheduler did not emit the completed short message");
    check(messages.front().message.wrong_key_evidence.available &&
          messages.front().message.wrong_key_evidence.data_bits==3 &&
          messages.front().message.wrong_key_evidence.passing_bits.back()==3,
          "ordered completion lost whole-message browser evidence");
    check(!stream.report().output_backpressured && stream.report().next_public_slot==rejected.next_public_slot,
          "draining output did not free ingress independently of slot progress");
    check(stream.push(state.input_stop, extra)==StreamPushResult::accepted, "unchanged retry after drain was rejected");
    feed(stream, p.slot(5).observation.stop);
    state = stream.resume(true);
    check(state.next_public_slot==6 && state.pending_messages==0 && state.state==StreamReceiverState::receiving,
          "continuous receiver did not keep listening");
    auto slots = stream.drain_recent_slots();
    check(slots.size()==6 && stream.drain_recent_slots().empty(), "slot receipt drain lost ordered results");
    for (std::size_t i=0; i<slots.size(); ++i)
        check(slots[i].score.index==i && slots[i].symbol_ordinal==91+i && slots[i].pattern_epoch==p.slot(i).pattern_epoch,
              "codec reset changed global waveform coordinates");
}

// The ready group keeps its width. Overlap leases use memory left after
// quoting the actual next windows that can use vacant owners.
void overlap_lease_uses_only_spare_memory() {
    constexpr std::size_t mib = 1024*1024, extra = 24*mib, overhead = 4096;
    const auto p = profile(true);
    const auto sequential = [](const ReceiverConfig& c, const ReceiverSlotPlan& plan, unsigned workers) {
        return workspace(c, plan, workers) + 8*mib;
    };
    struct Admitted { std::array<std::size_t, 2> lease; std::array<unsigned, 2> workers; std::size_t reserved; };
    const auto admit = [&](std::size_t limit, unsigned owners, const StreamWorkspace& quote,
                           bool together = true) {
        auto o = options();
        o.memory_limit_bytes = limit;
        o.slot_owners = owners;
        std::array<std::atomic<std::size_t>, 2> lease{};
        std::array<std::atomic<unsigned>, 2> share{};
        std::atomic<bool> release = false;
        const auto wider = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, unsigned workers) {
            return quote(c, plan, workers) + extra;
        };
        auto score = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
            auto, auto, unsigned workers, std::size_t memory, std::stop_token stop) {
            if (plan.index<lease.size()) {
                share[plan.index] = workers;
                lease[plan.index] = memory;
            }
            while (!release && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
            return row(c, plan, SlotDecision::quiet);
        };
        auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, quote, score,
            decode_short_message, {}, [] { return 8*mib; }, wider);
        const auto expected = std::min(owners, 2U);
        const auto end = p.slot(expected-1).observation.stop;
        check(end<=o.maximum_input_chunk_samples, "overlap lease fixture does not fit one input chunk");
        const auto deadline = std::chrono::steady_clock::now()+5s;
        if (!together) {
            // The second window becomes ready while the first owner runs.
            feed(stream, p.slot(0).observation.stop);
            while (!lease[0] && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
            check(lease[0] && !lease[1], "overlap lease fixture did not admit its first window alone");
        }
        feed(stream, end);
        const auto started = [&] { return lease[0] && (expected==1 || lease[1]); };
        while (!started() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(1ms);
        const auto report = stream.report();
        check(started() && report.active_owners==expected,
              "an overlap lease narrowed the ready group or delayed an owner");
        const Admitted admitted{{lease[0].load(), lease[1].load()}, {share[0].load(), share[1].load()},
                                report.peak_reserved_bytes};
        release = true;
        check(stream.resume(true).state==StreamReceiverState::receiving, "overlap lease fixture did not drain");
        return admitted;
    };
    const auto config = receiver(p);
    const std::array plans{make_receiver_slot_plan(p, config, 0), make_receiver_slot_plan(p, config, 1)};
    const std::array narrow{sequential(config, plans[0], 4), sequential(config, plans[1], 4)};

    // Ample memory: both ready slots overlap, with both owners of the cap used.
    const auto spare = admit(256*mib, 2, sequential);
    check(spare.lease==std::array{narrow[0]+extra, narrow[1]+extra} && spare.workers==std::array{4U, 4U},
          "spare memory did not grant every ready slot its overlap lease");
    // Everything the controller reserves beside the two leases.
    const auto fixed = spare.reserved - (spare.lease[0]+spare.lease[1]+2*overhead);
    const auto group = fixed + narrow[0] + narrow[1] + 2*overhead;

    // Both owner positions are occupied, so there is no third lease to protect.
    const auto both = admit(group + 2*extra + 2*mib, 2, sequential);
    check(both.lease==std::array{narrow[0]+extra, narrow[1]+extra},
          "a full owner group reserved a nonexistent extra owner");
    const auto one = admit(group + extra + 2*mib, 2, sequential);
    check(one.lease==std::array{narrow[0]+extra, narrow[1]},
          "memory for one overlap lease was not given to the oldest ready slot alone");
    const auto none = admit(group + extra - 2*mib, 2, sequential);
    check(none.lease==narrow, "an overlap lease exceeded the memory available to the ready group");

    const auto single = admit(256*mib, 1, sequential);
    const auto tight_single = admit(single.reserved + 2*mib, 1, sequential);
    check(tight_single.lease==std::array<std::size_t, 2>{narrow[0]+extra, 0},
          "a single-owner receiver reserved a nonexistent extra owner");

    // Two more owners under the cap need two further leases first.
    const auto future = sequential(config, make_receiver_slot_plan(p, config, 2), 4) +
                        sequential(config, make_receiver_slot_plan(p, config, 3), 4) + 2*overhead;
    const auto capped = admit(group + future + extra - 2*mib, 4, sequential);
    check(capped.lease==narrow, "an overlap lease took memory that the owner cap could still use");
    const auto uncapped = admit(group + future + extra + 2*mib, 4, sequential);
    check(uncapped.lease==std::array{narrow[0]+extra, narrow[1]},
          "memory beyond every remaining owner's lease did not grant an overlap lease");

    // Protect the full quote for the next window before it is ready.
    const auto held = admit(group + extra - 2*mib, 2, sequential, false);
    check(held.lease==narrow && held.workers==std::array{4U, 4U},
          "an overlap lease took the memory of a window that became ready later");
    const auto later = admit(group + extra + 4*mib, 2, sequential, false);
    check(later.lease==std::array{narrow[0]+extra, narrow[1]} && later.workers==std::array{4U, 4U},
          "a later window was narrowed or its predecessor denied spare memory");
    constexpr auto larger = 12*mib;
    const auto variable = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, unsigned workers) {
        return sequential(c, plan, workers) + (plan.index==1 ? larger : 0);
    };
    const auto later_larger = admit(group + larger + extra - 2*mib, 2, variable, false);
    check(later_larger.lease==std::array{narrow[0], narrow[1]+larger} &&
          later_larger.workers==std::array{4U, 4U},
          "a widened predecessor consumed the larger next window's sequential lease");

    // A group that fits only at balanced shares has no memory to spare.
    const auto shaped = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, unsigned workers) {
        return workspace(c, plan, workers) + (workers==4 ? 64*mib : 8*mib);
    };
    const auto balanced = admit(fixed + 2*(8*mib + overhead) + 2*workspace(config, plans[1], 2) + 80*mib, 2, shaped);
    check(balanced.workers==std::array{2U, 2U} &&
          balanced.lease==std::array{shaped(config, plans[0], 2), shaped(config, plans[1], 2)},
          "a balanced group was granted an overlap lease");
}

// A stage that is still open when its executor closes keeps its owner's
// lane. No worker is added behind the join.
void closed_executor_adds_no_worker() {
    receiver_detail::ReceiverExecutor executor(4);
    std::atomic<unsigned> ran = 0;
    receiver_detail::ReceiverExecutor::Stage stage(executor, 0, 4, {},
        [&](unsigned lane, std::size_t, std::stop_token) {
            check(lane==0, "a closed executor ran a leaf away from its owner");
            ++ran;
        });
    executor.close();
    stage.post(3);
    stage.wait_urgent();
    check(ran==3 && executor.started_workers()==0 && executor.peak_workers()==0,
          "a closed executor added a worker for an open stage");
}

void retry_after_one_completion() {
    constexpr std::size_t mib = 1024*1024;
    constexpr auto dsp = 32*mib;
    const auto p = profile(true);
    auto o = options();
    std::size_t footprint = 8*mib;
    o.memory_limit_bytes = footprint + 2*(dsp + 2*stream_worker_stack_bytes()) + 8*mib;
    std::array<std::atomic<bool>, 2> release{};
    auto scorer = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned, std::size_t, std::stop_token stop) {
        const auto deadline = std::chrono::steady_clock::now()+5s;
        while (!release.at(plan.index) && !stop.stop_requested()) {
            check(std::chrono::steady_clock::now()<deadline, "ingress retry waited for the blocked owner");
            std::this_thread::sleep_for(1ms);
        }
        return row(c, plan, SlotDecision::quiet);
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o,
        [](const ReceiverConfig&, const ReceiverSlotPlan&, unsigned) { return dsp; },
        scorer, decode_short_message, {}, [&] { return footprint; });
    check(stream.wait_for_slot_completion().active_owners==0, "idle ingress wait required a live owner");
    const auto first = p.slot(1).observation.stop;
    check(first<=o.maximum_input_chunk_samples && first+1<p.slot(2).observation.stop,
          "ingress retry fixture requires exactly two ready slots");
    feed(stream, first);
    check(stream.report().active_owners==2, "ingress retry fixture did not admit both owners");
    footprint += 16*mib;
    const std::array extra{Complex{1,-2}};
    check(stream.push(first, extra)==StreamPushResult::backpressured && stream.report().input_stop==first,
          "workspace backpressure accepted the fixture's pending chunk");
    release[1] = true;
    const auto progress = stream.wait_for_slot_completion();
    check(progress.state==StreamReceiverState::receiving && progress.active_owners==1 &&
          progress.next_public_slot==0 && progress.uncommitted_slots==2 && progress.input_stop==first,
          "ingress wait did not return when the later owner released its workspace");
    check(stream.push(first, extra)==StreamPushResult::accepted && stream.report().input_stop==first+1,
          "unchanged ingress retry did not use released workspace");
    release[0] = true;
    const auto eof = stream.finish_input();
    check(eof.state==StreamReceiverState::input_ended && eof.next_public_slot==2,
          "EOF did not drain both accepted slots in order");
}

void staggered_windows_share_workers() {
    const auto p = profile();
    const auto o = options();
    std::array<std::atomic<bool>, 4> lanes{};
    std::atomic<unsigned> leaf_starts = 0;
    std::atomic<bool> first_serial = false, release_first = false, second_started = false;
    auto scorer = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned workers, std::size_t, std::stop_token stop,
        receiver_detail::ReceiverExecutor* executor) {
        const auto deadline = std::chrono::steady_clock::now()+5s;
        if (!plan.index) {
            check(workers==lanes.size(), "a lone slot did not retain the full worker allowance");
            executor->parallel(plan.index, workers, workers, stop, [&](unsigned lane, std::size_t, std::stop_token task_stop) {
                check(!lanes.at(lane).exchange(true), "concurrent leaf scratch lane was reused");
                ++leaf_starts;
                while (leaf_starts<workers && !task_stop.stop_requested()) {
                    check(std::chrono::steady_clock::now()<deadline, "a lone slot did not activate every worker");
                    std::this_thread::sleep_for(1ms);
                }
            });
            first_serial = true;
            while (!release_first && !stop.stop_requested()) {
                check(std::chrono::steady_clock::now()<deadline, "later-ready slot waited for the serial owner");
                std::this_thread::sleep_for(1ms);
            }
        } else {
            check(plan.index==1 && workers==*o.workers,
                  "later owner did not retain full scratch shape within the memory cap");
            second_started = true;
        }
        return row(c, plan, SlotDecision::quiet);
    };
    auto stream = StreamReceiverFixture::make_with_executor(p, receiver(p), Crypto::amateur(), o, workspace, scorer);
    feed(stream, p.slot(0).observation.stop);
    const auto deadline = std::chrono::steady_clock::now()+5s;
    while (!first_serial && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
    check(first_serial && leaf_starts==lanes.size(), "the first slot did not complete its parallel phase");
    feed(stream, p.slot(1).observation.stop);
    const auto second = stream.wait_for_slot_completion();
    check(second_started && second.state==StreamReceiverState::receiving && second.active_owners==1 &&
          second.uncommitted_slots==2 && second.next_public_slot==0,
          "later-ready slot did not complete while its predecessor was still serial");
    check(second.peak_running_owners==2 && second.peak_executor_workers==*o.workers,
          "receiver did not report actual overlapping roots and its bounded workers");
    release_first = true;
    const auto eof = stream.finish_input();
    check(eof.state==StreamReceiverState::input_ended && eof.next_public_slot==2,
          "pooled owners did not commit in public order");
}

void publication_releases_root_demand() {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    std::atomic<bool> publishing = false, release = false, second = false;
    receiver_detail::ReceiverExecutor executor(2);
    executor.submit([&] {
        executor.parallel(0, 2, 2, {}, [](unsigned, std::size_t, std::stop_token) {});
    }, [&] {
        publishing = true;
        while (!release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        publishing = false;
    });
    while (!publishing && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
    check(publishing && executor.started_workers()==2, "root publication fixture did not start both workers");
    executor.submit([&] { second = true; }, [] {});
    while (!second && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
    check(second && publishing && executor.peak_running_roots()==1,
          "completed root still counted as scoring while publishing its result");
    release = true;
}

void runnable_leaf_precedes_new_root() {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    std::atomic<bool> helper_entered = false, release_helper = false, release_owner = false;
    std::atomic<bool> next_root = false, leaf_first = false, root_done = false;
    const auto await = [&](const std::atomic<bool>& flag) {
        while (!flag && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        return flag.load();
    };
    receiver_detail::ReceiverExecutor executor(2);
    executor.submit([&] {
        executor.parallel(0, 3, 2, {}, [&](unsigned, std::size_t index, std::stop_token) {
            if (index==0) (void)await(release_owner);
            else if (index==1) {
                helper_entered = true;
                (void)await(release_helper);
            } else {
                leaf_first = !next_root;
                release_owner = true;
            }
        });
    }, [] {});
    check(await(helper_entered), "fixture did not fill the shared worker pool");
    executor.submit([&] { next_root = true; }, [&] { root_done = true; });
    release_helper = true;
    check(await(root_done) && leaf_first, "later root displaced eligible leaf work from a free helper");
}

void oldest_eligible_batch_precedes_later_batch() {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    std::atomic<bool> later_helper = false, older_batch = false, older_helper = false;
    std::atomic<bool> later_claimed = false, older_first = false, older_done = false, later_done = false;
    const auto await = [&](const std::atomic<bool>& flag) {
        while (!flag && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        return flag.load();
    };
    receiver_detail::ReceiverExecutor executor(3);
    executor.submit([&] {
        // Register the older batch after the later one is already running.
        if (!await(later_helper)) return;
        executor.parallel(7, 2, 2, {}, [&](unsigned lane, std::size_t, std::stop_token) {
            if (!lane) {
                older_batch = true;
                (void)await(older_helper);
            } else {
                older_first = !later_claimed;
                older_helper = true;
            }
        });
    }, [&] { older_done = true; });
    executor.submit([&] {
        executor.parallel(8, 3, 2, {}, [&](unsigned, std::size_t index, std::stop_token) {
            if (index==0) (void)await(older_helper);
            else if (index==1) {
                later_helper = true;
                (void)await(older_batch);
            } else later_claimed = true;
        });
    }, [&] { later_done = true; });
    check(await(older_done) && await(later_done) && older_first && later_claimed && executor.peak_workers()==3,
          "free helper did not advance the oldest eligible public slot before the later batch");
}

void simultaneous_batches_isolate_leaf_failure() {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    constexpr auto planned_failure = "planned receiver leaf failure";
    std::atomic<bool> first_root = false, second_root = false;
    std::atomic<bool> first_entered = false, second_entered = false, failed = false;
    std::atomic<bool> first_done = false, second_done = false, healthy_failed = false;
    std::atomic<unsigned> healthy_work = 0;
    const auto await = [&](const std::atomic<bool>& flag) {
        while (!flag && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        return flag.load();
    };
    receiver_detail::ReceiverExecutor executor(4);
    executor.submit([&] {
        try {
            first_root = true;
            if (!await(second_root)) return;
            executor.parallel(0, 8, 4, {}, [&](unsigned lane, std::size_t, std::stop_token stop) {
                if (lane) {
                    if (await(first_entered) && await(second_entered)) throw Error(planned_failure);
                    return;
                }
                first_entered = true;
                while (!stop.stop_requested() && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(1ms);
            });
        } catch (const std::exception& error) {
            failed = std::string_view(error.what())==planned_failure;
        }
    }, [&] { first_done = true; });
    // Start both roots before permitting either batch to occupy the pool.
    check(await(first_root), "first concurrent root did not start");
    executor.submit([&] {
        try {
            second_root = true;
            executor.parallel(1, 8, 4, {}, [&](unsigned lane, std::size_t, std::stop_token) {
                if (!lane) {
                    second_entered = true;
                    if (!await(first_entered)) throw Error("first concurrent batch did not start");
                }
                ++healthy_work;
            });
        } catch (...) {
            healthy_failed = true;
        }
    }, [&] { second_done = true; });
    check(await(first_done) && await(second_done) && failed && !healthy_failed && healthy_work==8 &&
          executor.peak_running_roots()==2 && executor.peak_workers()==4,
          "one concurrent batch failure leaked, stalled or cancelled unrelated work");
}

// A standalone stage's caller is lane zero of a private pool. Background
// leaves occupy the lanes a running urgent leaf cannot use, each exactly
// once, and a leaf can release more of them.
void stage_background_overlaps_urgent_leaf() {
    const auto deadline = std::chrono::steady_clock::now()+5s;
    const auto await = [&](auto ready, std::stop_token stop = {}) {
        while (!ready() && !stop.stop_requested() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(1ms);
        return ready();
    };
    receiver_detail::ReceiverExecutor executor(3);
    std::array<std::atomic<unsigned>, 6> runs{};
    std::atomic<bool> urgent_entered = false, urgent_saw_overlap = false;
    std::atomic<bool> release_held = false, held_started = false, owner_passed_limit = false;
    std::atomic<unsigned> started = 0, early_completed = 0, completed = 0;
    receiver_detail::ReceiverExecutor::Stage* handle = nullptr;
    receiver_detail::ReceiverExecutor::Stage stage(executor, 0, 3, {},
        [&](unsigned lane, std::size_t, std::stop_token stop) {
            check(lane==0, "the owner did not run its own urgent leaf on lane zero");
            urgent_entered = true;
            urgent_saw_overlap = await([&] { return early_completed==4; }, stop);
        },
        [&](unsigned lane, std::size_t index, std::stop_token stop) {
            check(lane<3, "background leaf received a lane outside its stage");
            ++runs.at(index);
            if (index<2) {
                // Occupy the two helper lanes until the owner enters its leaf.
                ++started;
                check(await([&] { return urgent_entered.load(); }, stop), "the owner did not enter its urgent leaf");
            }
            if (index==4) {
                if (!lane) owner_passed_limit = true;
                held_started = true;
                (void)await([&] { return release_held.load(); }, stop);
            }
            if (index==3) handle->release(6);
            ++completed;
            if (index<4) ++early_completed;
        });
    handle = &stage;
    check(stage.lanes()==3, "stage did not report its admitted lane bound");
    stage.release(4);
    check(await([&] { return started==2; }), "the fixture did not occupy both helper lanes");
    stage.post(1);
    stage.wait_urgent();
    check(urgent_saw_overlap, "released background leaves did not overlap the running urgent leaf");
    // The owner does not start a released leaf past its limit. A helper does.
    stage.wait_background(4);
    check(await([&] { return held_started.load(); }) && !owner_passed_limit,
          "the owner, not a free helper, started the leaf past its limit");
    // Nor does it wait for that running leaf, which precedes this range.
    stage.wait_background(6, 5);
    check(runs[5]==1 && completed==5 && !release_held,
          "owner waited for a running background leaf before its range");
    release_held = true;
    stage.wait_background(6);
    for (const auto& count : runs) check(count==1, "a background leaf was skipped or repeated");
    check(completed==6, "stage returned before every background leaf completed");
    bool rejected = false;
    try {
        stage.wait_background(7);
    } catch (const Error&) {
        rejected = true;
    }
    check(rejected, "stage waited for background work that was never released");
}

// Urgent leaves gate the owner, so no lane starts background work while an
// urgent leaf is unclaimed. A background failure reaches the owner's next
// wait, and leaves not yet started are abandoned.
void stage_urgent_priority_and_failure() {
    constexpr auto planned_failure = "planned background leaf failure";
    receiver_detail::ReceiverExecutor executor(2);
    std::atomic<unsigned> urgent_started = 0, late_background = 0, early_background = 0;
    {
        receiver_detail::ReceiverExecutor::Stage stage(executor, 0, 2, {},
            [&](unsigned, std::size_t index, std::stop_token stop) {
                ++urgent_started;
                // Leave one lane in its urgent leaf until the other lane
                // has entered the last one. This makes entry order visible
                // without assuming a claimed leaf is already executing.
                if (index==2) return;
                const auto need = index==0 ? 2U : 3U;
                const auto deadline = std::chrono::steady_clock::now()+5s;
                while (urgent_started<need && !stop.stop_requested() && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(1ms);
                check(urgent_started>=need, "the urgent-priority fixture did not start its next leaf");
            },
            [&](unsigned, std::size_t, std::stop_token) {
                if (urgent_started<3) ++early_background;
                else ++late_background;
            });
        stage.post(3);
        stage.release(3);
        stage.wait_urgent();
        stage.wait_background(3);
        check(urgent_started==3 && !early_background && late_background==3,
              "a lane started background work before every urgent leaf was claimed");
    }
    std::atomic<unsigned> after_failure = 0;
    bool failed = false;
    std::stop_source external_stop;
    std::stop_token owner_stop;
    {
        receiver_detail::ReceiverExecutor::Stage stage(executor, 0, 2, external_stop.get_token(),
            [](unsigned, std::size_t, std::stop_token) {},
            [&](unsigned, std::size_t index, std::stop_token stop) {
                check(stop==owner_stop, "owner and leaves received different stage stop tokens");
                if (!index) throw Error(planned_failure);
                // Leaves already running observe the stage's cancellation.
                const auto deadline = std::chrono::steady_clock::now()+5s;
                while (!stop.stop_requested() && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(1ms);
                if (!stop.stop_requested()) ++after_failure;
            });
        owner_stop = stage.stop_token();
        check(owner_stop.stop_possible() && !owner_stop.stop_requested(), "a live stage lacked its own stop token");
        stage.release(64);
        try {
            stage.wait_background(64);
        } catch (const Error& error) {
            failed = std::string_view(error.what())==planned_failure;
        }
        check(owner_stop.stop_requested() && !external_stop.stop_requested(),
              "a leaf failure did not stop owner work independently of the receiver");
    }
    check(failed && !after_failure, "background leaf failure did not stop its stage and reach the owner");
}

void idle_pool_stacks_remain_reserved() {
    constexpr std::size_t mib = 1024*1024;
    const auto p = profile();
    const auto o = options();
    std::size_t footprint = 8*mib;
    auto score = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned workers, std::size_t, std::stop_token stop,
        receiver_detail::ReceiverExecutor* executor) {
        executor->parallel(plan.index, workers, workers, stop, [](unsigned, std::size_t, std::stop_token) {});
        return row(c, plan, SlotDecision::quiet);
    };
    auto stream = StreamReceiverFixture::make_with_executor(p, receiver(p), Crypto::amateur(), o, workspace, score,
        decode_short_message, {}, [&] { return footprint; });
    const auto controller_bytes = stream.report().peak_reserved_bytes-footprint;
    const auto captured = p.slot(0).observation.stop;
    const auto next = p.slot(1).observation.first;
    check(captured<=next && next-captured<=o.maximum_gap_samples, "idle stack fixture retains a prior input window");
    feed(stream, captured);
    const auto idle = stream.resume(true);
    check(idle.active_owners==0 && idle.next_public_slot==1 && idle.peak_executor_workers==*o.workers,
          "fixture did not leave all started workers idle");
    const auto half_stacks = *o.workers*stream_worker_stack_bytes()/2;
    footprint = *o.memory_limit_bytes-controller_bytes-half_stacks;
    const std::array extra{Complex{1,-2}};
    bool rejected = false;
    try {
        (void)stream.push(next, extra);
    } catch (const ReceiverWorkspaceError&) {
        rejected = true;
    }
    check(rejected && stream.report().input_stop==captured,
          "idle started workers' stack leases were released before shutdown");
    footprint = 8*mib;
    check(stream.push(next, extra)==StreamPushResult::accepted, "restored memory did not admit the unchanged input");
}

void inter_window_gap_and_rearm() {
    const auto p = profile();
    const auto score = [](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned, std::size_t, std::stop_token) {
        const auto i = plan.index;
        return (i<3 || (i>=5 && i<8)) ? row(c, plan, SlotDecision::data, i==2 || i==7) :
            row(c, plan, SlotDecision::quiet);
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), options(), workspace, score);
    const auto first_stop = p.slot(0).observation.stop;
    check(first_stop+1<p.slot(1).observation.first, "fixture does not place the gap outside either window");
    feed(stream, first_stop);
    const auto pending=stream.resume(true);
    check(pending.observed_bits==1, "first DATA slot did not reach the codec");
    check(pending.wrong_key_evidence.available && pending.wrong_key_evidence.data_bits==1 &&
          pending.wrong_key_evidence.passing_bits.back()==1,
          "pending stream report omitted baseline browser evidence");
    feed(stream, p.slot(4).observation.stop, 1);
    const auto recovered = stream.resume(true);
    check(recovered.state==StreamReceiverState::receiving && !recovered.recovering &&
          recovered.next_public_slot==5 && stream.drain_messages().empty(),
          "loss between observations decoded a suffix or failed quiet rearm");
    check(recovered.wrong_key_evidence.data_bits==0 && !recovered.wrong_key_evidence.available,
          "capture-gap rearm retained stale admission counts");
    feed(stream, p.slot(9).observation.stop);
    stream.resume(true);
    const auto messages = stream.drain_messages();
    check(messages.size()==1 && messages.front().first_public_slot==5 && messages.front().stop_public_slot==10 &&
          messages.front().message.filtered_text=="e", "rearmed receiver did not accept the next complete episode");
}

void rejected_episode_requires_two_contiguous_quiet_slots() {
    const auto p = profile();
    constexpr std::uint64_t first = 41;
    auto o = options();
    o.first_slot = first;
    o.first_input_sample = p.slot(first).observation.first;
    o.require_quiet_rearm = true;
    const std::array decisions{
        SlotDecision::ambiguous, SlotDecision::data, SlotDecision::quiet,
        SlotDecision::ambiguous, SlotDecision::quiet, SlotDecision::quiet,
        SlotDecision::data, SlotDecision::quiet, SlotDecision::quiet};
    const auto score = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned, std::size_t, std::stop_token) {
        const auto offset = static_cast<std::size_t>(plan.index-first);
        return row(c, plan, decisions.at(offset), offset==6 ? 1 : 0);
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, score);
    std::vector<StreamSlotReceipt> receipts;
    for (std::size_t i=0; i<decisions.size(); ++i) {
        const auto index = first+i;
        feed(stream, p.slot(index).observation.stop);
        (void)stream.resume(true);
        auto ready = stream.drain_recent_slots();
        receipts.insert(receipts.end(), std::make_move_iterator(ready.begin()), std::make_move_iterator(ready.end()));
        if (i<5)
            check(stream.report().recovering,
                  "a single quiet slot or interrupted quiet sequence rearmed the receiver");
        if (i==5)
            check(!stream.report().recovering && !stream.report().episode_first_public_slot,
                  "two contiguous quiet slots did not rearm the receiver cleanly");
        if (i==6)
            check(stream.report().episode_first_public_slot==first+6,
                  "post-rearm DATA did not start a fresh episode at its public slot");
    }
    check(receipts.size()==decisions.size(), "recovery sequence did not preserve all nine slot receipts");
    for (std::size_t i=0; i<receipts.size(); ++i) {
        const auto expected=p.slot(first+i).symbol_ordinal;
        check(receipts[i].score.index==first+i && receipts[i].symbol_ordinal==expected &&
              receipts[i].score.decision==decisions[i],
              "recovery failed to preserve addressed raw slot decisions");
    }
    check(receipts[1].score.bit==0 && receipts[6].score.bit==1,
          "recovery suppressed or altered a prearranged raw alarm bit");
    const auto messages=stream.drain_messages();
    check(messages.size()==1 && messages.front().first_public_slot==first+6 &&
          messages.front().stop_public_slot==first+9 && messages.front().message.physical_complete &&
          messages.front().message.raw_bits_complete() && messages.front().message.observed_bits==1 &&
          messages.front().message.received_bits==Bytes({1}) &&
          !messages.front().message.decoded_content,
          "rejected prefix decoded as a suffix or the rearmed complete episode failed");
}

void application_growth_keeps_live_dsp_reservations() {
    constexpr std::size_t mib = 1024 * 1024;
    constexpr std::size_t dsp = 48*mib;
    const auto stack = stream_worker_stack_bytes();
    const auto slot_charge = dsp+2*stack;
    auto footprint = 8*mib;
    auto o = options();
    o.memory_limit_bytes = footprint+2*slot_charge+2*stack+8*mib;
    const auto p = profile(true);
    std::atomic<bool> release_first = false;
    auto scorer = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto& key,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory, std::stop_token stop) {
        // Slot zero's large DSP allocation is still ahead. Free the second
        // owner while slot zero remains admitted at its full bound.
        if (!plan.index) while (!release_first && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
        return single_e(c, plan, key, iq, valid, workers, memory, stop);
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o,
        [](const ReceiverConfig&, const ReceiverSlotPlan&, unsigned) { return dsp; },
        scorer, decode_short_message, {}, [&] { return footprint; });
    feed(stream, p.slot(1).observation.stop);
    const auto deadline = std::chrono::steady_clock::now()+2s;
    auto state = stream.resume();
    while (state.active_owners!=1 && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(1ms);
        state = stream.resume();
    }
    check(state.active_owners==1 && state.uncommitted_slots==2 && state.next_public_slot==0,
          "memory fixture did not leave one deferred slot and one completed receipt");
    // An unrelated footprint increase can exceed the original in-flight
    // accounting without allocating any of that slot's deferred DSP.
    footprint += slot_charge+4*mib;
    feed(stream, p.slot(2).observation.stop);
    state = stream.resume();
    check(state.state==StreamReceiverState::receiving && state.active_owners==1 && state.uncommitted_slots==2,
          "external process growth concealed a live slot's unmaterialized workspace");
    release_first = true;
    state = stream.resume(true);
    check(state.state==StreamReceiverState::receiving && state.next_public_slot==3,
          "ready slot did not run after the deferred workspace lease was released");
}

void eof_cancel_and_workspace() {
    const auto p = profile();
    auto o = options();
    auto eof = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, single_e);
    feed(eof, p.slot(3).observation.stop);
    const auto eof_report = eof.finish_input();
    check(eof_report.state==StreamReceiverState::input_ended && eof_report.next_public_slot==4 &&
          eof.drain_messages().empty(), "EOF produced physical completion without two quiet slots");

    std::atomic<unsigned> observed_stop = 0, started = 0;
    auto blocking = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned workers, std::size_t, std::stop_token stop, receiver_detail::ReceiverExecutor* executor) {
        executor->parallel(plan.index, workers, workers, stop, [&](unsigned, std::size_t, std::stop_token task_stop) {
            ++started;
            while (!task_stop.stop_requested()) std::this_thread::sleep_for(1ms);
            ++observed_stop;
        });
        return row(c, plan, SlotDecision::quiet);
    };
    auto cancelled = StreamReceiverFixture::make_with_executor(p, receiver(p), Crypto::amateur(), o, workspace, blocking);
    feed(cancelled, p.slot(0).observation.stop);
    const auto deadline = std::chrono::steady_clock::now()+2s;
    while (started!=*o.workers && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
    check(started==*o.workers, "cancellation fixture did not start all live leaves");
    std::jthread request([&] {
        std::this_thread::sleep_for(2ms);
        cancelled.request_cancel();
    });
    const auto stop = cancelled.wait_for_slot_completion();
    check(observed_stop==*o.workers && stop.state==StreamReceiverState::cancelled && stop.next_public_slot==0 &&
          cancelled.drain_messages().empty() && cancelled.drain_recent_slots().empty(),
          "cancel released live work without draining it or committed a stale result");

    std::atomic<bool> unexpected_score = false;
    auto rejected = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o,
        [](const ReceiverConfig&, const ReceiverSlotPlan&, unsigned) { return 512*1024*1024; },
        [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
            auto, auto, unsigned, std::size_t, std::stop_token) {
            unexpected_score = true;
            return row(c, plan, SlotDecision::quiet);
        });
    feed(rejected, p.slot(0).observation.stop);
    check(!unexpected_score && rejected.report().state==StreamReceiverState::resource_limit &&
          rejected.drain_messages().empty(), "workspace rejection allocated or completed a slot");
}

void resource_resolution() {
    StreamReceiverOptions automatic;
    automatic.memory_limit_bytes = 1;
    const auto resolved = resolve_stream_receiver_resources(automatic);
    check(resolved.hardware_threads>0 && resolved.workers==2*resolved.hardware_threads &&
          resolved.slot_owners==resolved.workers && resolved.memory_limit_bytes==1,
          "automatic software capacity lost hardware threads or altered an explicit memory limit");
    automatic.workers = resolved.workers+3;
    const auto worker_only = resolve_stream_receiver_resources(automatic);
    check(worker_only.workers==*automatic.workers && worker_only.slot_owners==*automatic.workers,
          "automatic owner limit ignored an explicit worker-only allocation");
    automatic.slot_owners = *automatic.workers-1;
    const auto explicit_limits = resolve_stream_receiver_resources(automatic);
    check(explicit_limits.workers==*automatic.workers && explicit_limits.slot_owners==*automatic.slot_owners,
          "explicit intentional overcommit was changed");
}

void long_addressed_episode_and_decode_reservation() {
    const auto p = profile();
    constexpr std::uint64_t first = 11;
    const auto key = Crypto::amateur();
    const NativeMessageProfile message{FecMode::rs60, true, 512, key};
    const Bytes text(17, 'e');
    const auto draft = prepare_text_message(text, message);
    check(draft.wire_bits()==message_interval_bits, "bounded long scheduler fixture changed geometry");
    auto generate = draft.bit_source(first, [&](std::uint64_t index) { return message_address(p, index); });
    Bytes bits(draft.wire_bits());
    for (std::size_t i=0; i<bits.size(); ++i) bits[i] = generate(i);
    for (const auto grow_before_decode : {false,true}) {
        auto o = options();
        o.first_slot = first;
        o.first_input_sample = p.slot(first).observation.first;
        o.maximum_pending_messages = 1;
        std::atomic<std::size_t> footprint = 8*1024*1024;
        auto score = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
                        auto, auto, unsigned, std::size_t, std::stop_token) {
            const auto relative = plan.index-first;
            if (relative<bits.size()) return row(c, plan, SlotDecision::data, bits[relative]);
            if (grow_before_decode && relative==bits.size()+1) footprint = *o.memory_limit_bytes;
            return row(c, plan, SlotDecision::quiet);
        };
        auto stream = StreamReceiverFixture::make(p, receiver(p), key, o, workspace, score,
            message_decoder(message.fec), message_receiver_options(message), [&] { return footprint.load(); });
        const auto endpoint = p.slot(first+bits.size()+1).observation.stop;
        check(endpoint-*o.first_input_sample<8'000'000, "long scheduler fixture exceeded sample budget");
        feed(stream, endpoint);
        const auto report = stream.resume(true);
        const auto messages = stream.drain_messages();
        if (grow_before_decode) {
            check(report.state==StreamReceiverState::resource_limit && messages.empty(),
                  "application growth spent the reserved native source decoder workspace");
        } else {
            check(messages.size()==1 && messages.front().first_public_slot==first &&
                  messages.front().stop_public_slot==first+bits.size()+2 &&
                  messages.front().message.decoded_content &&
                  messages.front().message.decoded_content->authenticated &&
                  messages.front().message.decoded_content->message.data==text,
                  "native stream did not commit the addressed long content in slot order");
        }
    }
}
} // namespace

void repeated_bit_groups() {
    const auto p = profile();
    auto o = options();
    o.fixed_repeated_bit = true;
    auto test_scorer = [](const ReceiverConfig& c, const ReceiverSlotPlan& slot, const Crypto&,
        std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token) {
        auto result = row(c, slot, SlotDecision::quiet);
        result.calendar_opportunities_per_day = 1441;
        result.operational_threshold = 16;
        if (FixedRepeatedBit::is_attempt(slot.index))
            result.repeat_bit_evidence = slot.index < 15 ? std::array{8., -8.} : std::array{-8., 8.};
        return result;
    };
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, test_scorer);
    feed(stream, p.slot(29).observation.stop);
    auto state = stream.resume(true);
    check(state.state == StreamReceiverState::receiving && state.next_public_slot == 30,
          "repeated group fixture did not consume its public slots");
    const auto events = stream.drain_repeated_bits();
    check(events.size() == 2 && events[0].first_public_slot == 0 && events[0].last_public_slot == 9 &&
          events[1].first_public_slot == 15 && events[1].last_public_slot == 24 &&
          events[0].first_symbol_ordinal == 91 && events[0].last_symbol_ordinal == 100 &&
          events[0].attempts == 4 && events[0].bit == 0 && events[1].bit == 1 &&
          stream.drain_repeated_bits().empty(),
          "repeated evidence did not emit one ordered event per fixed group");
    check(stream.drain_messages().empty(), "repeat event manufactured physical completion");

    auto late = o;
    late.first_slot = 3;
    late.first_input_sample = p.slot(3).observation.first;
    auto joined = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), late, workspace, test_scorer);
    feed(joined, p.slot(14).observation.stop);
    joined.resume(true);
    check(joined.drain_repeated_bits().empty(), "late join accepted an incomplete group");

    auto erased_scorer = [test_scorer](const ReceiverConfig& c, const ReceiverSlotPlan& slot, const Crypto& k,
        std::span<const Complex> iq, std::span<const std::uint8_t> valid, unsigned workers,
        std::size_t memory, std::stop_token stop) {
        auto result = test_scorer(c, slot, k, iq, valid, workers, memory, stop);
        if (slot.index == 3) result.all_valid = false;
        return result;
    };
    auto erased = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, erased_scorer);
    feed(erased, p.slot(14).observation.stop);
    erased.resume(true);
    check(erased.drain_repeated_bits().empty(), "invalid attempt contributed to a repeated group");

    auto overlap_config = p.config();
    overlap_config.capture_lead_seconds = 200;
    const PublicProfile overlapping(overlap_config);
    auto overlap = StreamReceiverFixture::make(overlapping, receiver(overlapping), Crypto::amateur(),
        o, workspace, test_scorer);
    feed(overlap, overlapping.slot(14).observation.stop);
    overlap.resume(true);
    check(overlap.drain_repeated_bits().empty(), "overlapping repeated input apertures were accepted");

    auto ordinary_options = o;
    ordinary_options.fixed_repeated_bit = false;
    auto ordinary = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), ordinary_options,
        workspace, test_scorer);
    feed(ordinary, p.slot(14).observation.stop);
    ordinary.resume(true);
    check(ordinary.drain_repeated_bits().empty(), "ordinary mode emitted an opt-in repeat event");

    auto helper_config = receiver(p);
    helper_config.helper_enabled = true;
    bool helper_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(p, helper_config, Crypto::amateur(), o,
            workspace, test_scorer);
    } catch (const Error&) { helper_rejected = true; }
    check(helper_rejected, "repeat profile accepted Helper input");
    auto halo_config = p.config();
    halo_config.receiver_filter_halo_samples = 1;
    const PublicProfile halo(halo_config);
    bool halo_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(halo, receiver(halo), Crypto::amateur(), o,
            workspace, test_scorer);
    } catch (const Error&) { halo_rejected = true; }
    check(halo_rejected, "repeat profile accepted an unqualified receiver filter halo");

    check(repeat_input_dependencies_disjoint(120, 100, 10) &&
          !repeat_input_dependencies_disjoint(119, 100, 10) &&
          !repeat_input_dependencies_disjoint(99, 100, 0) &&
          !repeat_input_dependencies_disjoint(UINT64_MAX, 0, UINT64_MAX),
          "repeat input dependency check lost its inclusive gap or overflow guard");
    auto pcm_options = o;
    pcm_options.repeat_audio_input_halo_samples = 1;
    auto pcm = StreamReceiverFixture::make(halo, receiver(halo), Crypto::amateur(), pcm_options,
        workspace, test_scorer);
    feed(pcm, halo.slot(14).observation.stop);
    pcm.resume(true);
    check(pcm.drain_repeated_bits().size() == 1,
          "declared Gaussian logical-PCM provenance lost a disjoint repeat group");
    // Identical public geometry can represent padding around already-noisy,
    // unfiltered IQ, or an actual PCM FIR dependency. Only the latter expands
    // the statistical dependency beyond the already padded IQ aperture.
    auto padded_config = p.config();
    padded_config.receiver_filter_halo_samples = 2048;
    padded_config.capture_lead_seconds = 110;
    const PublicProfile padded(padded_config);
    const auto first_stop = padded.slot(0).observation.stop;
    const auto next_first = padded.slot(3).observation.first;
    if (next_first < first_stop || next_first-first_stop >= 4096)
        throw std::runtime_error("white-IQ padding fixture needs disjoint IQ but overlapping PCM dependencies: stop=" +
            std::to_string(first_stop) + " first=" + std::to_string(next_first));
    auto iq_options = o;
    iq_options.repeat_unfiltered_iq_padding_samples = 2048;
    auto iq = StreamReceiverFixture::make(padded, receiver(padded), Crypto::amateur(), iq_options,
        workspace, test_scorer);
    feed(iq, padded.slot(14).observation.stop);
    iq.resume(true);
    check(iq.drain_repeated_bits().size() == 1,
          "explicit unfiltered white-IQ padding did not accept disjoint IQ windows");
    auto expanded_pcm = o;
    expanded_pcm.repeat_audio_input_halo_samples = 2048;
    auto dependent = StreamReceiverFixture::make(padded, receiver(padded), Crypto::amateur(), expanded_pcm,
        workspace, test_scorer);
    feed(dependent, padded.slot(14).observation.stop);
    dependent.resume(true);
    check(dependent.drain_repeated_bits().empty(),
          "PCM mode accepted overlapping raw dependencies with identical public geometry");
    auto bad_iq = iq_options;
    bad_iq.repeat_unfiltered_iq_padding_samples = 2047;
    bool padding_mismatch_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(padded, receiver(padded), Crypto::amateur(), bad_iq,
            workspace, test_scorer);
    } catch (const Error&) { padding_mismatch_rejected = true; }
    check(padding_mismatch_rejected, "repeat profile accepted mismatched white-IQ padding");
    bad_iq = iq_options;
    bad_iq.repeat_audio_input_halo_samples = 2048;
    bool mixed_provenance_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(padded, receiver(padded), Crypto::amateur(), bad_iq,
            workspace, test_scorer);
    } catch (const Error&) { mixed_provenance_rejected = true; }
    check(mixed_provenance_rejected, "repeat profile accepted mixed IQ and PCM provenance");
    bad_iq = iq_options;
    bad_iq.fixed_repeated_bit = false;
    bool unconfigured_padding_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(padded, receiver(padded), Crypto::amateur(), bad_iq,
            workspace, test_scorer);
    } catch (const Error&) { unconfigured_padding_rejected = true; }
    check(unconfigured_padding_rejected, "white-IQ padding provenance accepted without repeat mode");
    auto bad_pcm = pcm_options;
    bad_pcm.repeat_audio_input_halo_samples = 2;
    bool mismatch_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(halo, receiver(halo), Crypto::amateur(), bad_pcm,
            workspace, test_scorer);
    } catch (const Error&) { mismatch_rejected = true; }
    check(mismatch_rejected, "repeat profile accepted mismatched logical-PCM halo");
    bad_pcm = pcm_options;
    bad_pcm.fixed_repeated_bit = false;
    bool unconfigured_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(halo, receiver(halo), Crypto::amateur(), bad_pcm,
            workspace, test_scorer);
    } catch (const Error&) { unconfigured_rejected = true; }
    check(unconfigured_rejected, "logical-PCM repeat provenance accepted without repeat mode");
    bad_pcm = pcm_options;
    bad_pcm.repeat_audio_input_halo_samples = 0;
    bool zero_halo_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(halo, receiver(halo), Crypto::amateur(), bad_pcm,
            workspace, test_scorer);
    } catch (const Error&) { zero_halo_rejected = true; }
    check(zero_halo_rejected, "logical-PCM repeat provenance accepted a zero halo");
    auto invalid_noise = receiver(halo);
    invalid_noise.noise_variance = 0;
    bool noise_rejected = false;
    try {
        auto unsupported = StreamReceiverFixture::make(halo, invalid_noise, Crypto::amateur(), pcm_options,
            workspace, test_scorer);
    } catch (const Error&) { noise_rejected = true; }
    check(noise_rejected, "repeat profile accepted an invalid logical-PCM noise envelope");
}

int main() {
    try {
        resource_resolution();
        order_and_backpressure();
        overlap_lease_uses_only_spare_memory();
        closed_executor_adds_no_worker();
        retry_after_one_completion();
        staggered_windows_share_workers();
        publication_releases_root_demand();
        runnable_leaf_precedes_new_root();
        oldest_eligible_batch_precedes_later_batch();
        simultaneous_batches_isolate_leaf_failure();
        stage_background_overlaps_urgent_leaf();
        stage_urgent_priority_and_failure();
        idle_pool_stacks_remain_reserved();
        inter_window_gap_and_rearm();
        rejected_episode_requires_two_contiguous_quiet_slots();
        application_growth_keeps_live_dsp_reservations();
        eof_cancel_and_workspace();
        long_addressed_episode_and_decode_reservation();
        repeated_bit_groups();
        std::cout << "Native Robust stream scheduler checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
