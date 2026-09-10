#pragma once
#include "datapump/types.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>

namespace datapump {

enum class MessageKind : std::uint8_t { text = 0, file = 1, screenshot = 2 };
enum class FecMode : std::uint8_t { off = 0, rs20 = 1, rs60 = 2 };

struct Message {
    MessageKind kind = MessageKind::text;
    Bytes data;
    std::string filename;
    std::string callsign;
    std::string grid;
    bool repeatable = false;
    // A zero identifier asks the encoder to generate a cryptographically random one.
    std::array<std::uint8_t, 16> id{};
};

struct PacketOptions {
    FecMode fec = FecMode::rs20;
    bool compression = true;
    // A 32-byte independently keyed MAC; absent callbacks select SHA-256 integrity.
    // Encryption belongs outside this codec, around the entire transmitted frame.
    std::function<Bytes(const Bytes&)> authenticator;
    std::function<bool(const Bytes&, const Bytes&)> verifier;
};

struct DecodedPacket {
    Message message;
    std::size_t corrected_bytes = 0;
    std::size_t consumed_bytes = 0;
    bool authenticated = false;
};

inline constexpr std::size_t packet_prefix_size = 72;
Bytes encode_packet(const Message& message, const PacketOptions& options = {},
                    std::size_t max_memory = default_memory_limit);
DecodedPacket decode_packet(const Bytes& wire, const PacketOptions& options = {},
                            std::size_t max_memory = default_memory_limit);
// Incomplete prefixes return nullopt; invalid or oversized complete prefixes throw.
// Trailing demodulator bytes do not contribute to the packet length.
std::optional<std::size_t> packet_frame_size(const Bytes& prefix,
                                           std::size_t max_memory = default_memory_limit);
bool safe_filename(const std::string& name);

namespace packet_codec {
// Shortened systematic RS over GF(256), polynomial 0x11d, first root alpha^0 = 1.
Bytes rs_encode(const Bytes& data, std::size_t parity_symbols);
std::size_t rs_correct(Bytes& codeword, std::size_t parity_symbols);
// The caller retains the original size; encoded bytes are MSB-first with zero padding.
Bytes compress_short(const Bytes& input);
Bytes decompress_short(const Bytes& encoded, std::size_t original_size);
}
}
