#include "../src/hf_admission_replay.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace appraisal;

void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const Error&) { rejected = true; }
    check(rejected, "malformed replay input was admitted");
}

void run() {
    PublicProfileConfig pc;
    pc.format.sample_rate = 1920;
    pc.format.inner_rate_hz = 120;
    pc.format.added_symbols = 1;
    pc.format.bit_seconds = 8.;
    pc.format.epoch = 1793850000;
    pc.format.symbol_ordinal = 741;
    pc.format.configured_carrier_hz = 10000500;
    pc.data_if_hz = 500.;
    pc.helper_enabled = false;
    pc.lead_samples = 480;
    pc.receiver_filter_halo_samples = 8;
    const PublicProfile profile(pc);
    ReceiverConfig config;
    config.sample_rate_hz = 1920;
    config.data_if_hz = 500.;
    config.noise_variance = .0001;
    const NativeMessageProfile message{FecMode::rs20, true, 16, Crypto::amateur()};
    const auto wire = encode_message_bits(Bytes{'B'}, FecMode::rs20);
    std::vector<StreamSlotReceipt> receipts;
    for (std::size_t i = 0; i < wire.size() + 2; ++i) {
        const auto plan = make_receiver_slot_plan(profile, config, i);
        ReceiverSlotScore row;
        row.index = i;
        row.first_sample = plan.first_sample;
        row.stop_sample = plan.stop_sample;
        row.quiet_first_sample = plan.quiet_first_sample;
        row.quiet_stop_sample = plan.quiet_stop_sample;
        row.consumed_samples = plan.stop_sample - plan.first_sample;
        row.sample_rate_hz = 1920;
        row.complete = row.all_valid = row.zero_h0_verified = row.noise_statistics_valid = true;
        row.decision = i < wire.size() ? SlotDecision::data : SlotDecision::quiet;
        row.quiet_modes = 20;
        row.quiet_energy = 1;
        row.quiet_threshold = 2;
        row.operational_threshold = 20;
        if (i < wire.size()) {
            row.bit = wire[i];
            row.operational_passes_conditional_bound = true;
            row.operational_log_e = 100;
            row.operational_bit_evidence[*row.bit] = 100;
            // Two bits in one RS byte have deliberately near-balanced scores.
            row.operational_bit_evidence[1 - *row.bit] = (i == 10 || i == 11) ? 99 : 0;
            row.operational_bit_log_separation = (i == 10 || i == 11) ? 1 : 100;
        }
        receipts.push_back({row, plan.format.epoch, plan.format.symbol_ordinal, 1, {}});
    }

    ContinuousMessageReceiver native(message, [&profile](auto i) { return message_address(profile, i); });
    HfProtectedReassembly consumer(FecMode::rs20);
    std::vector<CompletedMessage> completed;
    std::vector<ProtectedReassemblyCandidate> recovered;
    for (const auto& receipt : receipts) {
        check(native.consume(receipt.score), "native baseline score fixture failed");
        for (auto& event : native.drain_messages()) {
            if (auto candidate = consumer.consume(event)) recovered.push_back(std::move(*candidate));
            completed.push_back(std::move(event));
        }
        consumer.advance(native.next_slot_index());
    }
    check(completed.size() == 1 && completed.front().message.decoded_content &&
        completed.front().message.decoded_content->message.data == Bytes{'B'}, "synthetic baseline did not deliver B");
    const auto current = replay_hf_admission_policy(profile, message, receipts, WrongKeyAdmission::current);
    check(hf_admission_matches_baseline(current, completed, recovered, consumer.report()), "baseline semantic parity failed");
    const auto strict = replay_hf_admission_policy(profile, message, receipts, WrongKeyAdmission::strict);
    check(strict.messages.size() == 1 && strict.messages.front().message.decoded_content &&
        strict.messages.front().message.decoded_content->message.data == Bytes{'B'} &&
        strict.messages.front().message.missing_slots == 2, "native RS decoder did not repair the two admitted erasures");
    check(strict.messages.front().message.decoded_content->fec_stats.data.erased_bytes == 1,
        "two erased bits were not one native RS byte erasure");
    check(strict.slots[10].rejected && strict.slots[11].rejected &&
        strict.slots[10].decision == SlotDecision::ambiguous &&
        strict.slots[wire.size()].decision == SlotDecision::quiet &&
        strict.slots.back().decision == SlotDecision::quiet, "admission changed physical quiet");
    auto bad = receipts;
    bad[3].score.all_valid = false;
    rejects([&] { replay_hf_admission_policy(profile, message, bad, WrongKeyAdmission::strict); });
    bad = receipts; bad[3].score.wrong_key_admission = WrongKeyAdmission::strict;
    rejects([&] { replay_hf_admission_policy(profile, message, bad, WrongKeyAdmission::strict); });
    bad = receipts; bad[3].score.index = 2;
    rejects([&] { replay_hf_admission_policy(profile, message, bad, WrongKeyAdmission::strict); });
    bad = receipts; ++bad[3].symbol_ordinal;
    rejects([&] { replay_hf_admission_policy(profile, message, bad, WrongKeyAdmission::strict); });
    rejects([&] { replay_hf_admission_policy(profile, message, {}, WrongKeyAdmission::strict); });
    std::stop_source stop; stop.request_stop();
    rejects([&] { replay_hf_admission_policy(profile, message, receipts, WrongKeyAdmission::strict, stop.get_token()); });
    // Parity cannot pass merely because decoded bytes happen to match.
    completed.front().message.erasure_slot_indices.push_back(1);
    check(!hf_admission_matches_baseline(current, completed, recovered, consumer.report()),
        "baseline parity ignored changed positioned erasure metadata");
}
} // namespace

int main() {
    try { run(); std::cout << "HF admission replay checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
