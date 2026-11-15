// Component-scope regression: synthetic scored slot events exercise the
// canonical codec and quiet-slot completion path only. This does not qualify
// DSP/waveform reception or global ordinal behavior.
#include "datapump/triad_message_receiver.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;

void check(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}

ReceiverSlotScore scored_slot(std::uint64_t index, SlotDecision decision,
                              std::uint8_t bit = 0) {
    ReceiverSlotScore row;
    row.index = index;
    row.first_sample = index * 100;
    row.stop_sample = row.first_sample + 100;
    row.quiet_first_sample = row.first_sample + 10;
    row.quiet_stop_sample = row.stop_sample - 10;
    row.consumed_samples = 100;
    row.sample_rate_hz = 100;
    row.decision = decision;
    if (decision == SlotDecision::data) row.bit = bit;
    row.complete = row.all_valid = row.zero_h0_verified = row.noise_statistics_valid = true;
    row.operational_passes_conditional_bound = decision == SlotDecision::data;
    row.operational_log_e = 30;
    row.operational_threshold = 20;
    row.quiet_energy = 1;
    row.quiet_threshold = 2;
    row.quiet_modes = 20;
    row.quiet_noise_upper_tail_probability = .001;
    return row;
}

const MessageResult& finish_with_bits(QuietSlotMessageReceiver& receiver,
                                      const std::vector<MessageBit>& bits) {
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) receiver.consume_slot(i, scored_slot(i, SlotDecision::data, *bits[i]));
        else receiver.consume_slot(i, scored_slot(i, SlotDecision::ambiguous));
    }
    const auto first_quiet = static_cast<std::uint64_t>(bits.size());
    receiver.consume_slot(first_quiet, scored_slot(first_quiet, SlotDecision::quiet));
    return receiver.consume_slot(first_quiet + 1,
                                 scored_slot(first_quiet + 1, SlotDecision::quiet));
}

std::vector<MessageBit> known_bits(const Bytes& bits) {
    return {bits.begin(), bits.end()};
}

void require_correct_complete_message(FecMode mode, const Bytes& expected) {
    const auto encoded = encode_message_bits(expected, mode);
    QuietSlotMessageReceiver receiver(message_decoder(mode), message_receiver_options(mode));
    const auto& result = finish_with_bits(receiver, known_bits(encoded));
    check(result.physical_complete && result.state == MessageState::complete &&
          result.decoded_content && result.decoded_content->message.data == expected,
          "complete canonical payload failed through quiet-slot receiver, mode=" +
              std::to_string(static_cast<unsigned>(mode)));
    check(result.trailing_quiet_slots ==
              std::vector<std::uint64_t>{encoded.size(), encoded.size() + 1},
          "physical completion quiet-slot suffix changed");
}

void unprotected_interior_fragment_is_accepted() {
    const Bytes expected{'B'};
    const auto encoded = encode_message_bits(expected, FecMode::off);
    check(encoded.size() == 13, "canonical FEC-off B bit length changed");

    // B is the 13-bit escaped literal 11111 01000010. Its interior 000
    // decodes as the valid three-bit space code when treated as a new episode.
    constexpr std::size_t fragment_start = 7;
    const std::vector<MessageBit> fragment{encoded[fragment_start],
                                           encoded[fragment_start + 1],
                                           encoded[fragment_start + 2]};
    check(fragment == std::vector<MessageBit>{0, 0, 0},
          "canonical B no longer contains the expected interior 000 fragment");

    QuietSlotMessageReceiver receiver(message_decoder(FecMode::off),
                                      message_receiver_options(FecMode::off));
    const auto& result = finish_with_bits(receiver, fragment);
    check(result.physical_complete && result.state == MessageState::complete &&
          result.decoded_content && result.decoded_content->message.data == Bytes{' '},
          "FEC-off interior 000 fragment no longer reproduces accepted-space failure");
}

void protected_modes_reject_the_known_fragment() {
    const std::vector<MessageBit> fragment{0, 0, 0};
    for (const auto mode : {FecMode::rs20, FecMode::rs60}) {
        QuietSlotMessageReceiver receiver(message_decoder(mode), message_receiver_options(mode));
        const auto& result = finish_with_bits(receiver, fragment);
        check(result.physical_complete && result.state == MessageState::invalid_message &&
              !result.decoded_content && result.reason == "codec_rejected_message",
              "protected mode accepted or failed to reject the interior fragment, mode=" +
                  std::to_string(static_cast<unsigned>(mode)));
    }
}

void enumerate_proper_protected_substrings() {
    const Bytes expected{'B'};
    for (const auto mode : {FecMode::rs20, FecMode::rs60}) {
        const auto encoded = encode_message_bits(expected, mode);
        const std::size_t expected_wire_bits = mode == FecMode::rs20 ? 72 : 96;
        check(encoded.size() == expected_wire_bits,
              "canonical protected B wire size changed, mode=" +
                  std::to_string(static_cast<unsigned>(mode)) + " bits=" +
                  std::to_string(encoded.size()));
        check(encoded.size() <= 128, "bounded fragment enumeration limit exceeded");

        std::size_t checked = 0;
        for (std::size_t begin = 0; begin < encoded.size(); ++begin) {
            for (std::size_t stop = begin + 1; stop <= encoded.size(); ++stop) {
                if (begin == 0 && stop == encoded.size()) continue;
                std::vector<MessageBit> fragment(encoded.begin() + begin,
                                                 encoded.begin() + stop);
                QuietSlotMessageReceiver receiver(message_decoder(mode),
                                                  message_receiver_options(mode));
                const auto& result = finish_with_bits(receiver, fragment);
                ++checked;
                if (result.decoded_content) {
                    std::ostringstream witness;
                    witness << "protected mode accepted proper B substring [" << begin << ','
                            << stop << ") as \"" << result.filtered_text << "\"; mode="
                            << static_cast<unsigned>(mode) << " state="
                            << static_cast<unsigned>(result.state);
                    throw std::runtime_error(witness.str());
                }
                check(result.physical_complete && result.state == MessageState::invalid_message,
                      "protected substring did not end as codec rejection, mode=" +
                          std::to_string(static_cast<unsigned>(mode)) + " range=[" +
                          std::to_string(begin) + "," + std::to_string(stop) + ")");
            }
        }
        const auto expected_count = encoded.size() * (encoded.size() + 1) / 2 - 1;
        check(checked == expected_count, "proper-substring enumeration was incomplete");
    }
}

void representative_erasure_burst_recovers() {
    const Bytes expected{'B'};
    for (const auto mode : {FecMode::rs20, FecMode::rs60}) {
        const auto encoded = encode_message_bits(expected, mode);
        auto bits = known_bits(encoded);
        // Eight contiguous missing bit slots erase exactly one packed codeword
        // byte. The canonical RS decoder should recover the complete message.
        constexpr std::size_t burst_start = 16;
        for (std::size_t i = burst_start; i < burst_start + 8; ++i) bits[i].reset();
        QuietSlotMessageReceiver receiver(message_decoder(mode), message_receiver_options(mode));
        const auto& result = finish_with_bits(receiver, bits);
        check(result.physical_complete && result.state == MessageState::complete &&
              result.decoded_content && result.decoded_content->message.data == expected &&
              result.decoded_content->fec_stats.data.erased_bytes == 1,
              "canonical FEC did not recover a one-byte interior erasure burst, mode=" +
                  std::to_string(static_cast<unsigned>(mode)));
    }
}

void fragment_integrity_regressions() {
    for (const auto mode : {FecMode::off, FecMode::rs20, FecMode::rs60}) {
        require_correct_complete_message(mode, Bytes{'B'});
        require_correct_complete_message(mode, Bytes{'e'});
        require_correct_complete_message(mode, Bytes{'A', 'B'});
    }
    unprotected_interior_fragment_is_accepted();
    protected_modes_reject_the_known_fragment();
    enumerate_proper_protected_substrings();
    representative_erasure_burst_recovers();
}

} // namespace

int main() {
    try {
        fragment_integrity_regressions();
        std::cout << "HF fragment integrity component checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
