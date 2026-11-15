#include "../src/hf_protected_reassembly.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace datapump;
using namespace datapump::triad::v3;
using appraisal::HfProtectedReassembly;
using appraisal::ProtectedReassemblyBoundary;
using appraisal::ProtectedReassemblyCandidate;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

class NativeEvents {
public:
    explicit NativeEvents(std::uint64_t first = 0, FecMode fec = FecMode::rs20)
        : receiver_(NativeMessageProfile{fec}, {}, first) {}

    void codeword(std::span<const std::uint8_t> bits) {
        for (const auto bit : bits) slot(SlotDecision::data, bit);
    }

    void quiet() {
        slot(SlotDecision::quiet);
        slot(SlotDecision::quiet);
    }

    void slot(SlotDecision decision, std::uint8_t bit = 0) {
        ReceiverSlotScore row;
        row.index = receiver_.next_slot_index();
        row.first_sample = row.index * 100;
        row.stop_sample = row.first_sample + 100;
        row.quiet_first_sample = row.first_sample + 10;
        row.quiet_stop_sample = row.stop_sample - 10;
        row.consumed_samples = 100;
        row.sample_rate_hz = 100;
        row.complete = row.all_valid = row.zero_h0_verified = row.noise_statistics_valid = true;
        row.decision = decision;
        if (decision == SlotDecision::data) {
            row.bit = bit;
            row.operational_passes_conditional_bound = true;
            row.operational_log_e = 30;
            row.operational_threshold = 20;
        }
        row.quiet_energy = 1;
        row.quiet_threshold = 2;
        row.quiet_modes = 20;
        check(receiver_.consume(row) && !receiver_.recovering(), "native synthetic score fixture entered recovery");
        for (auto& event : receiver_.drain_messages()) events.push_back(std::move(event));
    }

    std::vector<CompletedMessage> events;

private:
    ContinuousMessageReceiver receiver_;
};

void check_rejected(const CompletedMessage& event) {
    check(event.message.physical_complete && event.message.state == MessageState::invalid_message &&
          event.message.reason == "codec_rejected_message" && !event.message.decoded_content,
          "the native fixture did not first reject the physically completed fragment");
}

Bytes encoded(std::uint8_t text) {
    return encode_message_bits(Bytes{text}, FecMode::rs20);
}

void clean_native_message_stays_native() {
    NativeEvents fixture;
    fixture.codeword(encoded('B'));
    fixture.quiet();
    check(fixture.events.size() == 1, "clean fixture lost its native message event");
    const auto& native = fixture.events.front().message;
    check(native.state == MessageState::complete && native.decoded_content &&
          native.decoded_content->message.data == Bytes{'B'}, "clean native RS20 message failed");
    HfProtectedReassembly app(FecMode::rs20);
    check(!app.consume(fixture.events.front()) && app.report().decode_attempts == 0 &&
          app.report().last_action == "native_message_accepted", "clean native completion entered fragment recovery");
}

void two_slot_fade(std::uint8_t text, std::size_t gap_first, std::size_t expected_erased_bytes,
                   std::uint64_t first_public_slot, bool initial_quiet) {
    constexpr std::size_t physical_quiet = 2;
    const auto word = encoded(text);
    const auto bits = std::span(word);
    NativeEvents fixture(first_public_slot);
    if (initial_quiet) fixture.slot(SlotDecision::quiet);
    fixture.codeword(bits.first(gap_first));
    fixture.quiet();
    fixture.codeword(bits.subspan(gap_first + physical_quiet));
    fixture.quiet();
    check(fixture.events.size() == 2, "interior fade did not produce two native completions");
    const auto& early = fixture.events.front();
    const auto& late = fixture.events.back();
    check_rejected(early);
    check_rejected(late);
    HfProtectedReassembly app(FecMode::rs20, first_public_slot);
    const auto first_bit = first_public_slot + initial_quiet;
    check(!app.consume(early) && app.report().pending_first_public_slot == first_bit &&
          app.report().decode_attempts == 0, "first rejection speculatively decoded or mislocated a fragment");
    app.advance(early.stop_public_slot);
    const auto candidate = app.consume(late);
    check(candidate && candidate->content.message.data == Bytes{text} && !candidate->content.authenticated &&
          candidate->first_public_slot == first_bit && candidate->stop_public_slot == late.stop_public_slot &&
          candidate->bit_positions == word.size() && candidate->erased_positions == 2 && candidate->fragments == 2,
          "native RS20 did not recover the exact frame from the two public fragments");
    const auto& fec = candidate->content.fec_stats;
    check(fec.data.erased_bytes + fec.integrity.erased_bytes + fec.parity.erased_bytes == expected_erased_bytes,
          "absolute fade positions mapped to the wrong number of erased RS bytes");
    check(app.report().decode_attempts == 1 && app.report().appends == 1 && app.report().candidates == 1 &&
          !app.report().pending_first_public_slot && app.report().last_gap_positions == 2,
          "one later physical completion did not result in exactly one bounded candidate decode");
    check_rejected(early);
    check_rejected(late);
}

std::vector<CompletedMessage> split_events(bool unrelated) {
    const auto left = encoded('B');
    const auto right = unrelated ? encoded('e') : left;
    const auto cut = unrelated ? 20U : 10U;
    NativeEvents fixture(73);
    fixture.codeword(std::span(left).first(cut));
    fixture.quiet();
    if (unrelated) fixture.codeword(std::span(right).first(left.size() - cut - 2));
    else fixture.codeword(std::span(right).subspan(cut + 2));
    fixture.quiet();
    check(fixture.events.size() == 2, "expected two native events for split validation");
    check_rejected(fixture.events[0]);
    check_rejected(fixture.events[1]);
    return std::move(fixture.events);
}

void unrelated_fragments_remain_rejected() {
    const auto events = split_events(true);
    HfProtectedReassembly app(FecMode::rs20, events.front().first_public_slot);
    check(!app.consume(events[0]) && !app.consume(events[1]) && app.report().decode_attempts == 1 &&
          app.report().last_attempt_positions == encoded('B').size() && app.report().last_action == "combined_short_frame_rejected",
          "two unrelated codec rejects with a legal combined size became accepted content");
}

void valid_completion_separates_fragments() {
    const auto word = encoded('B');
    NativeEvents fixture;
    fixture.codeword(std::span(word).first(10));
    fixture.quiet();
    fixture.codeword(encoded('e'));
    fixture.quiet();
    fixture.codeword(std::span(word).subspan(12));
    fixture.quiet();
    check(fixture.events.size() == 3, "separator fixture lost actual completions");
    check_rejected(fixture.events.front());
    check_rejected(fixture.events.back());
    check(fixture.events[1].message.state == MessageState::complete, "middle message failed to complete natively");
    HfProtectedReassembly app(FecMode::rs20);
    for (const auto& event : fixture.events) check(!app.consume(event), "candidate bridged a valid native completion");
    check(app.report().decode_attempts == 0 && app.report().seeds == 2,
          "valid native completion did not clear the earlier rejected fragment");
}

void window_reset_and_unsupported_receipts() {
    const auto events = split_events(false);
    const auto& first = events.front();
    const auto& second = events.back();
    const auto start = first.first_public_slot;
    {
        HfProtectedReassembly app(FecMode::rs20, start);
        check(!app.consume(first), "seed event emitted a candidate");
        app.advance(start + HfProtectedReassembly::maximum_positions + 1);
        check(app.report().pending_first_public_slot == start, "last feasible continuation expired prematurely");
        app.advance(start + HfProtectedReassembly::maximum_positions + 2);
        check(!app.report().pending_first_public_slot && app.report().expirations == 1 && !app.consume(second) &&
              app.report().decode_attempts == 0, "pending hypothesis survived its public-position deadline");
    }
    for (const auto boundary : {ProtectedReassemblyBoundary::session_changed,
                                ProtectedReassemblyBoundary::native_recovery, ProtectedReassemblyBoundary::input_closed}) {
        HfProtectedReassembly app(FecMode::rs20, start);
        check(!app.consume(first), "seed event emitted a candidate");
        app.reset(first.stop_public_slot, boundary);
        check(!app.report().pending_first_public_slot && !app.consume(second) && app.report().decode_attempts == 0,
              "session, recovery or EOF boundary allowed fragments to merge");
        if (boundary == ProtectedReassemblyBoundary::input_closed)
            check(!app.report().pending_first_public_slot, "ending input allowed a late event to seed a new candidate");
    }
    for (const auto mode : {FecMode::off, FecMode::rs60}) {
        HfProtectedReassembly app(mode, start);
        check(!app.consume(first) && app.report().last_action == "unsupported_fec" && app.report().seeds == 0,
              "an unsupported codec profile seeded RS20 reassembly");
    }
    {
        HfProtectedReassembly app(FecMode::rs20, start);
        check(!app.consume(first), "seed event emitted a candidate");
        auto too_late = second;
        too_late.first_public_slot += HfProtectedReassembly::maximum_positions;
        too_late.stop_public_slot += HfProtectedReassembly::maximum_positions;
        check(!app.consume(too_late) && !app.report().pending_first_public_slot && app.report().decode_attempts == 0,
              "a later event outside the fixed public frame span triggered a decode");
    }
}

void malformed_or_unqualified_fragment_metadata() {
    const auto events = split_events(false);
    const auto& first = events.front();
    for (unsigned test = 0; test < 10; ++test) {
        auto bad = events.back();
        auto& message = bad.message;
        switch (test) {
        case 0: bad = first; break; // Duplicate completion.
        case 1: --bad.first_public_slot; --bad.stop_public_slot; break; // Overlap.
        case 2: ++bad.stop_public_slot; break; // Event extent versus local count.
        case 3: message.trailing_quiet_slots.pop_back(); break;
        case 4: message.erasure_slot_indices.push_back(2); break;
        case 5: message.long_form = true; break;
        case 6: message.state = MessageState::incomplete_message; message.reason = "leading_message_prefix_unqualified"; break;
        case 7: message.state = MessageState::resource_limit; break;
        case 8: message.state = MessageState::failed; break;
        default: message.physical_complete = false; break;
        }
        HfProtectedReassembly app(FecMode::rs20, first.first_public_slot);
        check(!app.consume(first) && !app.consume(bad) && !app.report().pending_first_public_slot &&
              app.report().decode_attempts == 0, "invalid completion metadata did not clear the pending hypothesis");
    }
    {
        HfProtectedReassembly app(FecMode::rs20);
        auto top = first;
        const auto end = std::numeric_limits<std::uint64_t>::max();
        top.stop_public_slot = end;
        top.first_public_slot = end - top.message.slots_consumed;
        check(!app.consume(top) && app.report().pending_first_public_slot == top.first_public_slot,
              "last representable native event corrupted its address interval");
        app.advance(end);
        check(app.report().expirations == 0 && app.report().decode_attempts == 0,
              "calendar coordinate arithmetic wrapped at the final slot");
        app.reset(end, ProtectedReassemblyBoundary::input_closed);
        check(!app.report().pending_first_public_slot, "input close retained a fragment at the calendar limit");
    }
}

} // namespace

int main() {
    try {
        clean_native_message_stays_native();
        two_slot_fade('B', 10, 1, 73, true);
        two_slot_fade('e', 15, 2, 907, false);
        unrelated_fragments_remain_rejected();
        valid_completion_separates_fragments();
        window_reset_and_unsupported_receipts();
        malformed_or_unqualified_fragment_metadata();
        std::cout << "Protected reassembly passed native short completions with within-/across-byte fades, boundaries and unrelated fragments\n";
    } catch (const std::exception& error) {
        std::cerr << "Protected reassembly component check failed: " << error.what() << '\n';
        return 1;
    }
}
