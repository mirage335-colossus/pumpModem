#include "datapump/boundary_sync.hpp"
#include "datapump/transfer.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>

using namespace datapump;
namespace byte_sync = datapump::boundary_sync;
namespace {
void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
template<class Function> void rejects(Function function, const char* description) {
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error(description);
}
Bytes random_bits(std::size_t count) {
    std::mt19937 random(19073);
    Bytes bits(count);
    for (auto& bit : bits) bit = static_cast<std::uint8_t>(random() & 1U);
    return bits;
}
Bytes runtime_marker() {
    const auto wire = byte_sync::insert(Bytes(byte_sync::interval_bits));
    return Bytes(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(byte_sync::marker_bits));
}
bool equal_suffix(const Bytes& actual, const Bytes& expected, std::size_t start) {
    return actual.size() == expected.size() &&
        std::equal(actual.begin() + static_cast<std::ptrdiff_t>(start), actual.end(),
                   expected.begin() + static_cast<std::ptrdiff_t>(start));
}
Bytes slip(Bytes bits, std::size_t position, std::size_t count, bool addition) {
    if (addition) {
        const auto extra = random_bits(count);
        bits.insert(bits.begin() + static_cast<std::ptrdiff_t>(position), extra.begin(), extra.end());
    } else {
        bits.erase(bits.begin() + static_cast<std::ptrdiff_t>(position),
                   bits.begin() + static_cast<std::ptrdiff_t>(position + count));
    }
    return bits;
}
void roundtrip_and_exact_boundaries() {
    check(byte_sync::interval_bits == 256 * 8 && byte_sync::marker_bits == 2 * 12 * 8 &&
          byte_sync::maximum_slip_bits == 7, "fixed marker cadence and search bounds changed");
    const auto marker = runtime_marker();
    check(marker.size() == byte_sync::marker_bits &&
          std::equal(marker.begin(), marker.begin() + static_cast<std::ptrdiff_t>(marker.size() / 2),
                     marker.begin() + static_cast<std::ptrdiff_t>(marker.size() / 2)),
          "the marker must contain two copies of its runtime-derived 96-bit word");
    for (const auto bytes : {0U, 1U, 15U, 16U, 17U, 255U, 256U, 257U,
                            511U, 512U, 513U, 4096U}) {
        const auto original = random_bits(bytes * 8);
        const auto expected_size = original.size() + (1 + bytes / 256) * byte_sync::marker_bits;
        check(byte_sync::encoded_size(original.size()) == expected_size, "exact encoded size near a cadence boundary");
        const auto wire = byte_sync::insert(original, expected_size);
        check(wire.size() == expected_size, "marker count includes an exact final full interval");
        check(std::equal(marker.begin(), marker.end(), wire.begin()),
              "every compact packet stream must begin with the byte-boundary marker");
        check(byte_sync::recover(wire, wire.size()) == original, "pristine roundtrip within exact storage caps");
        if (bytes && bytes % 256 == 0)
            check(std::equal(marker.begin(), marker.end(), wire.end() - static_cast<std::ptrdiff_t>(marker.size())),
                  "an exact final full interval must retain its recovery marker");
    }
}
void leading_marker_recovery() {
    for (const auto bytes : {17U, 255U, 256U, 513U}) {
        const auto original = random_bits(bytes * 8);
        const auto pristine = byte_sync::insert(original);
        for (std::size_t count = 1; count <= byte_sync::maximum_slip_bits; ++count)
            check(byte_sync::recover(slip(pristine, 0, count, true)) == original,
                  "the leading marker must restore the first byte after bounded extra leading bits");
        for (const auto bit : {0U, 11U, 95U, 96U, 191U}) {
            auto damaged = pristine;
            damaged[bit] ^= 1;
            check(byte_sync::recover(damaged) == original,
                  "an aligned damaged leading marker must leave every packet byte intact");
        }
    }
    const auto original = random_bits(17 * 8);
    const auto pristine = byte_sync::insert(original);
    const auto extra = byte_sync::maximum_slip_bits + 1;
    const auto beyond_window = byte_sync::recover(slip(pristine, 0, extra, true));
    check(beyond_window.size() == original.size() + extra && beyond_window != original,
          "leading marker recovery must not search beyond the fixed seven-bit neighborhood");
}
void inserted_and_deleted_bits() {
    const auto original = random_bits(byte_sync::interval_bits * 3 + 40);
    const auto pristine = byte_sync::insert(original);
    for (std::size_t count = 1; count <= byte_sync::maximum_slip_bits; ++count) {
        for (const bool addition : {false, true}) {
            for (const auto position : {std::size_t{0}, std::size_t{101}, byte_sync::interval_bits - 24}) {
                const auto damaged = slip(pristine, byte_sync::marker_bits + position, count, addition);
                const auto recovered = byte_sync::recover(damaged);
                check(equal_suffix(recovered, original, byte_sync::interval_bits),
                      "an intact marker must restore every subsequent byte after a bounded bit slip");
                check(std::equal(recovered.begin(), recovered.begin() + static_cast<std::ptrdiff_t>(position),
                                 original.begin()), "recovery must preserve the preceding intact prefix");
                if (!addition)
                    check(std::all_of(recovered.begin() + static_cast<std::ptrdiff_t>(byte_sync::interval_bits - count),
                                      recovered.begin() + static_cast<std::ptrdiff_t>(byte_sync::interval_bits),
                                      [](auto bit) { return bit == 0; }),
                          "deleted bits must be represented by bounded trailing zero placeholders");
            }
            const auto exact = random_bits(byte_sync::interval_bits);
            const auto recovered = byte_sync::recover(slip(byte_sync::insert(exact), byte_sync::marker_bits + byte_sync::interval_bits - 24, count, addition));
            check(recovered.size() == exact.size(),
                  "an intact exact-end marker still repairs a shorter or longer final full interval");
        }
    }
}
void damaged_markers_and_deferred_recovery() {
    const auto original = random_bits(byte_sync::interval_bits * 3 + 40);
    const auto pristine = byte_sync::insert(original);
    for (const auto bit : {0U, 11U, 95U, 96U, 191U}) {
        auto damaged = pristine;
        damaged[byte_sync::marker_bits + byte_sync::interval_bits + bit] ^= 1;
        check(byte_sync::recover(damaged) == original,
              "a damaged full marker is stripped at its nominal position without changing intact data");
    }
    for (std::size_t count = 1; count <= byte_sync::maximum_slip_bits; ++count) {
        for (const bool addition : {false, true}) {
            auto damaged = slip(pristine, byte_sync::marker_bits + byte_sync::interval_bits - 24, count, addition);
            const auto first_marker = addition ? byte_sync::interval_bits + count : byte_sync::interval_bits - count;
            damaged[byte_sync::marker_bits + first_marker + 3] ^= 1;
            const auto recovered = byte_sync::recover(damaged);
            check(equal_suffix(recovered, original, byte_sync::interval_bits * 2),
                  "a slip followed by a damaged marker must recover at the next complete exact pair");
        }
    }
}
void embedded_markers_remain_data() {
    const auto marker = runtime_marker();
    for (const auto position : {std::size_t{0}, std::size_t{1}, std::size_t{89},
                               byte_sync::interval_bits - byte_sync::marker_bits - 3,
                               byte_sync::interval_bits - 7, byte_sync::interval_bits * 2 + 37}) {
        auto original = random_bits(byte_sync::interval_bits * 4);
        std::copy(marker.begin(), marker.end(), original.begin() + static_cast<std::ptrdiff_t>(position));
        check(byte_sync::recover(byte_sync::insert(original)) == original,
              "marker-shaped payload at any bit position must survive an intentional outer encoding");
    }

    // An unsynchronized input deliberately puts valid markers outside every
    // permitted search window. Its expected processing is fixed-slot removal,
    // regardless of what the retained bytes happen to contain.
    Bytes input(byte_sync::interval_bits * 4 + 400);
    for (const auto position : {std::size_t{41}, byte_sync::interval_bits + 64,
                               (byte_sync::interval_bits + byte_sync::marker_bits) * 2 + 93})
        std::copy(marker.begin(), marker.end(), input.begin() + static_cast<std::ptrdiff_t>(position));
    Bytes expected;
    std::size_t position = byte_sync::marker_bits;
    while (input.size() - position >= byte_sync::interval_bits + byte_sync::marker_bits) {
        expected.insert(expected.end(), input.begin() + static_cast<std::ptrdiff_t>(position),
                        input.begin() + static_cast<std::ptrdiff_t>(position + byte_sync::interval_bits));
        position += byte_sync::interval_bits + byte_sync::marker_bits;
    }
    expected.insert(expected.end(), input.begin() + static_cast<std::ptrdiff_t>(position), input.end());
    check(byte_sync::recover(input) == expected,
          "recovery must never scan farther for marker-shaped content or recursively reinterpret its output");
    const auto original = random_bits(byte_sync::interval_bits * 3 + 128);
    for (const bool addition : {false, true}) {
        const auto beyond_window = byte_sync::maximum_slip_bits + 1;
        const auto recovered = byte_sync::recover(slip(byte_sync::insert(original),
            byte_sync::marker_bits + byte_sync::interval_bits - 24, beyond_window, addition));
        const auto expected_size = addition ? original.size() + beyond_window : original.size() - beyond_window;
        check(recovered.size() == expected_size,
              "a marker outside the fixed seven-bit neighborhood must not expand the search or silently align data");
    }
}
void truncation_and_resource_bounds() {
    const auto original = random_bits(byte_sync::interval_bits * 2);
    const auto pristine = byte_sync::insert(original);
    for (const auto length : {std::size_t{0}, std::size_t{1}, byte_sync::marker_bits / 2, byte_sync::marker_bits - 1}) {
        const Bytes partial(pristine.begin(), pristine.begin() + static_cast<std::ptrdiff_t>(length));
        check(byte_sync::recover(partial) == partial, "an incomplete leading marker must remain uninterpreted");
    }
    for (const auto length : {std::size_t{0}, std::size_t{1}, byte_sync::interval_bits - 1,
                             byte_sync::interval_bits, byte_sync::interval_bits + 1,
                             byte_sync::interval_bits + byte_sync::marker_bits / 2,
                             byte_sync::interval_bits + byte_sync::marker_bits - 1}) {
        const Bytes partial(pristine.begin(), pristine.begin() + static_cast<std::ptrdiff_t>(byte_sync::marker_bits + length));
        const Bytes expected(partial.begin() + static_cast<std::ptrdiff_t>(byte_sync::marker_bits), partial.end());
        check(byte_sync::recover(partial) == expected, "an incomplete first periodic slot must remain uninterpreted after stripping the leading marker");
    }
    for (const auto tail_bits : {std::size_t{1}, std::size_t{7}, std::size_t{19}, byte_sync::interval_bits - 1}) {
        const auto length = byte_sync::interval_bits + 2 * byte_sync::marker_bits + tail_bits;
        const Bytes partial(pristine.begin(), pristine.begin() + static_cast<std::ptrdiff_t>(length));
        const Bytes expected(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(byte_sync::interval_bits + tail_bits));
        check(byte_sync::recover(partial) == expected, "a partial final data interval retains its exact meaningful bits");
    }
    rejects([&] { byte_sync::insert(original, pristine.size() - 1); }, "insert must enforce expanded output storage");
    rejects([&] { byte_sync::recover(pristine, pristine.size() - 1); }, "recover must enforce admitted input storage");
    rejects([&] { byte_sync::insert(Bytes{0, 1}); }, "insert must reject non-byte-aligned input");
    rejects([&] { byte_sync::encoded_size(7); }, "encoded size must reject non-byte-aligned input");
    rejects([&] { byte_sync::insert(Bytes{0, 1, 0, 1, 0, 1, 0, 2}); }, "insert must reject non-bit elements");
    rejects([&] { byte_sync::recover(Bytes{0, 2, 1}); }, "recover must validate even a short uninterpreted tail");
    const auto maximum_aligned = std::numeric_limits<std::size_t>::max() - 7;
    rejects([&] { byte_sync::encoded_size(maximum_aligned); }, "expanded size overflow must fail before allocation");
}
transfer::Options options(bool keyed, FecMode fec) {
    transfer::Options value;
    value.timestamp = 1800000000;
    value.content_limit = 1024 * 1024;
    value.dsp_workspace_bytes = 8 * 1024 * 1024;
    value.fec = fec;
    value.compression = false;
    value.modem.pattern_symbols = true;
    value.modem.constellation_bits = 1;
    if (keyed) value.key.emplace(Bytes(32, 0x37));
    return value;
}
Message file_message(std::size_t size = 4096) {
    Message message;
    message.kind = MessageKind::file;
    message.filename = "boundary-fixture.bin";
    message.data.resize(size);
    std::mt19937 random(912781);
    for (auto& byte : message.data) byte = static_cast<std::uint8_t>(random());
    for (std::size_t i = 0; i < message.id.size(); ++i) message.id[i] = static_cast<std::uint8_t>(i + 1);
    return message;
}
transfer::Received interpret(Bytes bits, const transfer::Options& value) {
    modem::PatternBurst burst;
    burst.bits = std::move(bits);
    burst.complete = true;
    burst.score = 35;
    return transfer::interpret_pattern(std::move(burst), value, value.timestamp);
}
Bytes packed_bits(std::span<const std::uint8_t> bits) {
    check(bits.size() % 8 == 0, "test packed-bit fixture must be byte aligned");
    Bytes bytes(bits.size() / 8);
    for (std::size_t i = 0; i < bits.size(); ++i)
        bytes[i / 8] |= static_cast<std::uint8_t>(bits[i] << (7 - i % 8));
    return bytes;
}
void packet_fec_and_crypto_integration() {
    const auto message = file_message();
    for (const bool keyed : {false, true}) for (const auto fec : {FecMode::off, FecMode::rs20, FecMode::rs60}) {
        const auto value = options(keyed, fec);
        const auto plain_bits = transfer::message_bits(message, value);
        const auto marked_plaintext = byte_sync::insert(plain_bits);
        auto encrypted_wire = marked_plaintext;
        transfer::xor_binary_bits(encrypted_wire, value);
        const auto wire = transfer::message_wire_bits(message, value);
        check(wire == encrypted_wire, "the existing data stream must encrypt the entire wire including markers");
        const auto estimate = transfer::estimate(message, value);
        const auto expected_samples = modem::training_sample_count(value.modem) +
            2*modem::pattern_pulse_padding_samples(value.modem) +
            wire.size() * modem::symbol_sample_count(value.modem);
        check(estimate.waveform_samples == expected_samples,
              "long-message airtime estimates must include every transmitted boundary marker");
        const auto transmitter = transfer::message_transmitter(message, value);
        check(transmitter->total_samples() == expected_samples,
              "streaming transmission must use the same complete marked bit count as its airtime estimate");
        if (keyed) {
            const auto marker = runtime_marker();
            check(!std::equal(marker.begin(), marker.end(), wire.begin()),
                  "the leading marker must also be masked by the existing data stream");
            check(!std::equal(marker.begin(), marker.end(), wire.begin() + static_cast<std::ptrdiff_t>(byte_sync::marker_bits + byte_sync::interval_bits)),
                  "a keyed transmission must not expose the public plaintext marker");
            auto decrypted = wire;
            transfer::xor_binary_bits(decrypted, value);
            check(decrypted == marked_plaintext && byte_sync::recover(decrypted) == plain_bits,
                  "ordinary whole-stream decryption precedes downstream byte recovery");
        }
        const auto pristine = interpret(wire, value);
        check(pristine.packet_validated && pristine.packet.message.data == message.data &&
              pristine.packet.authenticated == keyed,
              "synchronized packet roundtrip must retain full integrity and optional authentication");
        for (std::size_t count = 1; count <= byte_sync::maximum_slip_bits; ++count) {
            for (const bool addition : {false, true}) {
                const auto slipped = slip(wire, byte_sync::marker_bits + byte_sync::interval_bits - 24, count, addition);
                const auto received = interpret(slipped, value);
                if (keyed || fec == FecMode::off) {
                    check(!received.packet_validated,
                          "byte recovery must not claim integrity for ciphertext stream shifts or uncorrected data");
                } else {
                    check(received.packet_validated && received.packet.message.data == message.data &&
                          received.packet.authenticated == keyed && received.packet.corrected_bytes > 0,
                          "RS must repair the localized slip after marker recovery and before integrity validation");
                }
                if (keyed) {
                    // Constellation/timing alignment owns ciphertext positions.
                    // Exercise this layer only after ordinary aligned decryption.
                    auto decrypted = wire;
                    transfer::xor_binary_bits(decrypted, value);
                    const auto repaired = packed_bits(byte_sync::recover(slip(std::move(decrypted),
                        byte_sync::marker_bits + byte_sync::interval_bits - 24, count, addition)));
                    if (fec == FecMode::off) {
                        rejects([&] { decode_packet(repaired, transfer::packet_options(value, value.timestamp)); },
                                "post-decryption byte alignment alone must not validate corrupted content");
                    } else {
                        const auto packet = decode_packet(repaired, transfer::packet_options(value, value.timestamp));
                        check(packet.message.data == message.data && packet.authenticated && packet.corrected_bytes > 0,
                              "downstream RS must repair a bounded plaintext slip and retain keyed authentication");
                    }
                }
            }
        }
        for (const auto marker_position : {std::size_t{0}, byte_sync::marker_bits + byte_sync::interval_bits}) {
            auto damaged_marker = wire;
            damaged_marker[marker_position + 95] ^= 1;
            const auto intact_payload = interpret(std::move(damaged_marker), value);
            check(intact_payload.packet_validated && intact_payload.packet.message.data == message.data,
                  "an otherwise aligned damaged marker must not consume the packet's FEC budget");
        }
        auto unrecoverable = wire;
        for (std::size_t i = 0; i < byte_sync::interval_bits; ++i) unrecoverable[byte_sync::marker_bits + i] ^= 1;
        check(!interpret(std::move(unrecoverable), value).packet_validated,
              "damage beyond the protected packet budget must remain unvalidated after resynchronization");
        if (keyed) {
            auto wrong_key = value;
            wrong_key.key.emplace(Bytes(32, 0x73));
            check(!interpret(wire, wrong_key).packet_validated, "public boundary markers must not bypass a wrong key");
            modem::PatternBurst wrong_offset;
            wrong_offset.bits = wire;
            wrong_offset.first_stream_symbol = 1;
            wrong_offset.complete = true;
            check(!transfer::interpret_pattern(std::move(wrong_offset), value, value.timestamp).packet_validated,
                  "byte markers must not override the constellation's established keystream position");
        }
    }
}
void nested_packets_remain_opaque() {
    const auto inner = file_message(700);
    const auto plain_options = options(false, FecMode::off);
    const auto inner_packet = packed_bits(transfer::message_bits(inner, plain_options));
    const auto inner_wire = packed_bits(transfer::message_wire_bits(inner, plain_options));
    for (const bool keyed : {false, true}) {
        const auto value = options(keyed, FecMode::off);
        auto outer = file_message();
        std::copy(inner_packet.begin(), inner_packet.end(), outer.data.begin());
        std::copy(inner_wire.begin(), inner_wire.end(), outer.data.begin() + 1024);
        auto wire = transfer::message_wire_bits(outer, value);
        const auto received = interpret(wire, value);
        check(received.packet_validated && received.packet.message.data == outer.data &&
              received.packet.message.data != inner.data,
              "valid encoded packets and marker-bearing wire content inside an outer file must remain opaque bytes");
        for (std::size_t i = 0; i < 32; ++i) wire[byte_sync::marker_bits + i] ^= 1;
        const auto damaged = interpret(std::move(wire), value);
        check(!damaged.packet_validated && damaged.packet.message.data != inner.data,
              "outer packet damage must never trigger a search for a valid nested packet");
    }
}
}
int main() {
    try {
        roundtrip_and_exact_boundaries();
        leading_marker_recovery();
        inserted_and_deleted_bits();
        damaged_markers_and_deferred_recovery();
        embedded_markers_remain_data();
        truncation_and_resource_bounds();
        packet_fec_and_crypto_integration();
        nested_packets_remain_opaque();
        std::cout << "byte-boundary synchronization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "byte-boundary synchronization tests failed: " << error.what() << '\n';
        return 1;
    }
}
