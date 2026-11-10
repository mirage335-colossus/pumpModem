#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
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
    auto stream = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, score);
    const auto two = p.slot(1).observation.stop;
    check(two<=o.maximum_input_chunk_samples, "fixture does not admit both windows at once");
    feed(stream, two);
    const auto deadline = std::chrono::steady_clock::now()+2s;
    while ((!later_finished || !shares[0]) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(1ms);
    check(later_finished && shares[0]==2 && shares[1]==2, "overlapping ready slots did not share the full worker budget");
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
    const auto messages = stream.drain_messages();
    check(messages.size()==1 && messages.front().first_public_slot==0 && messages.front().stop_public_slot==5 &&
          messages.front().message.physical_complete && messages.front().message.filtered_text=="e",
          "ordered native scheduler did not emit the completed short message");
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
    check(stream.resume(true).observed_bits==1, "first DATA slot did not reach the codec");
    feed(stream, p.slot(4).observation.stop, 1);
    const auto recovered = stream.resume(true);
    check(recovered.state==StreamReceiverState::receiving && !recovered.recovering &&
          recovered.next_public_slot==5 && stream.drain_messages().empty(),
          "loss between observations decoded a suffix or failed quiet rearm");
    feed(stream, p.slot(9).observation.stop);
    stream.resume(true);
    const auto messages = stream.drain_messages();
    check(messages.size()==1 && messages.front().first_public_slot==5 && messages.front().stop_public_slot==10 &&
          messages.front().message.filtered_text=="e", "rearmed receiver did not accept the next complete episode");
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

    std::atomic<bool> observed_stop = false, started = false;
    auto blocking = [&](const ReceiverConfig& c, const ReceiverSlotPlan& plan, const Crypto&,
        auto, auto, unsigned, std::size_t, std::stop_token stop) {
        started = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
        observed_stop = true;
        return row(c, plan, SlotDecision::quiet);
    };
    auto cancelled = StreamReceiverFixture::make(p, receiver(p), Crypto::amateur(), o, workspace, blocking);
    feed(cancelled, p.slot(0).observation.stop);
    const auto deadline = std::chrono::steady_clock::now()+2s;
    while (!started && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
    check(started, "cancellation fixture did not start a live owner");
    std::jthread request([&] {
        std::this_thread::sleep_for(2ms);
        cancelled.request_cancel();
    });
    const auto stop = cancelled.resume(true);
    check(observed_stop && stop.state==StreamReceiverState::cancelled && stop.next_public_slot==0 &&
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

int main() {
    try {
        resource_resolution();
        order_and_backpressure();
        inter_window_gap_and_rearm();
        application_growth_keeps_live_dsp_reservations();
        eof_cancel_and_workspace();
        long_addressed_episode_and_decode_reservation();
        std::cout << "Native Robust stream scheduler checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
