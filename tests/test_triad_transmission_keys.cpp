#include "datapump/helper_metadata.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_transmission_keys.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad;
using namespace datapump::triad::v3;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::span<const std::uint32_t> symbols(const helper::MetadataPacket& packet) {
    return {packet.symbols.data(), packet.count};
}

void policy_matrix() {
    const auto selected = Crypto::temporary();
    const auto amateur = Crypto::amateur();
    const auto options = helper::Options{.power_w=40, .data_power_w=4, .callsign="N0CALL"};
    const auto plan = helper::plan(options, 1725, 200, 48000);
    for (const auto* key : {&selected, &amateur}) for (const bool encrypt : {false, true}) {
        const auto resolved = resolve_transmission_keys(*key, encrypt, TransmissionMode::Message);
        check(resolved.encrypt_data == encrypt, "Message changed the DATA encryption setting");
        Format format;
        format.encrypt = resolved.encrypt_data;
        PatternSource direct(format, *key, 1), through_policy(format, resolved.data, 1);
        const auto at = static_cast<long double>(direct.geometry().source_samples / 2);
        check(direct.sample_at(at) == through_policy.sample_at(at),
              "Message changed ordinary DATA samples");

        helper::Generator old_generator(options, plan, 100, true, *key);
        helper::Generator resolved_generator(options, plan, 100, true, resolved.helper);
        for (unsigned index = 0; index < 9; ++index) {
            const auto seconds = 100.232L + .020L * index;
            check(old_generator.mix_analytic({}, seconds, 48000) ==
                  resolved_generator.mix_analytic({}, seconds, 48000),
                  "Message changed ordinary Helper generator samples");
        }

        helper::Options disabled;
        const auto disabled_plan = helper::plan(disabled, 1725, 200, 48000);
        helper::Generator disabled_generator(disabled, disabled_plan, 100, true, resolved.helper);
        check(disabled_generator.mix_analytic({}, 100.2L, 48000) == Complex{},
              "key policy enabled a disabled Helper");

        const auto packet = helper::metadata_packet(1310720, "N0CALL", &resolved.helper);
        const auto decoded = helper::decode_metadata_packet(symbols(packet), &resolved.helper, 1310720);
        check(decoded && decoded->encrypted && decoded->callsign == "N0CALL",
              "Message Helper metadata did not use the configured key");
    }
}

void noise_policy() {
    const auto selected = Crypto::temporary();
    const auto amateur = Crypto::amateur();
    for (const auto* configured : {&selected, &amateur}) for (const bool encrypt : {false, true}) {
        const auto first = resolve_transmission_keys(*configured, encrypt, TransmissionMode::Noise);
        const auto second = resolve_transmission_keys(*configured, encrypt, TransmissionMode::Noise);
        check(first.encrypt_data && second.encrypt_data, "Noise DATA encryption was not forced");
        Format format;
        format.encrypt = first.encrypt_data;
        PatternSource first_data(format, first.data, 1), second_data(format, second.data, 1);
        PatternSource selected_data(format, *configured, 1), amateur_data(format, amateur, 1);
        const auto at = static_cast<long double>(first_data.geometry().source_samples / 2);
        const auto sample = first_data.sample_at(at);
        check(sample != second_data.sample_at(at), "Noise DATA reused a session key");
        check(sample != selected_data.sample_at(at), "Noise DATA used the configured key");
        check(sample != amateur_data.sample_at(at), "Noise DATA used the Amateur key");
    }

    const auto first = resolve_transmission_keys(selected, false, TransmissionMode::Noise);
    const auto first_packet = helper::metadata_packet(1310720, "N0CALL", &first.helper);
    const auto next_packet = helper::metadata_packet(1310720 + 26214, "N0CALL", &first.helper);
    const auto decoded = helper::decode_metadata_packet(symbols(first_packet), &amateur, 1310720);
    check(decoded && decoded->encrypted && decoded->callsign == "N0CALL",
          "Noise callsign did not decode with the Amateur key");
    check(!std::equal(first_packet.symbols.begin() + 1, first_packet.symbols.end(),
                      next_packet.symbols.begin() + 1),
          "Noise callsign ciphertext repeated at a later absolute dwell");
    const auto options = helper::Options{.power_w=40, .data_power_w=4, .callsign="N0CALL"};
    const auto plan = helper::plan(options, 1725, 200, 48000);
    helper::Generator public_generator(options, plan, 100, true, first.helper);
    helper::Generator selected_generator(options, plan, 100, true, selected);
    bool different = false;
    for (unsigned index = 0; index < 9; ++index) {
        const auto seconds = 100.232L + .020L * index;
        different |= public_generator.mix_analytic({}, seconds, 48000) !=
                     selected_generator.mix_analytic({}, seconds, 48000);
    }
    check(different, "Noise Helper generator did not use its Amateur key");
}
}

int main() {
    try { policy_matrix(); noise_policy(); std::cout << "triad transmission key policy passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
