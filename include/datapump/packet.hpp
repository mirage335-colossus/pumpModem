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

// Exact bit differences between received and corrected systematic body bytes:
// metadata, encoded (possibly compressed) content and the digest/MAC. Header,
// parity and demodulator tail bytes are excluded. XOR encryption/whitening
// preserve these bit differences. This is available only after full validation.
struct PacketBitAccuracy {
    std::uint64_t received_data_bits = 0;
    std::uint64_t corrected_data_bits = 0;
};

struct DecodedPacket {
    Message message;
    std::size_t corrected_bytes = 0;
    std::size_t consumed_bytes = 0;
    bool authenticated = false;
    // Absent for a default/manual packet. Invalid or partial input never
    // returns a DecodedPacket; valid no-FEC packets have zero corrected bits.
    std::optional<PacketBitAccuracy> pre_fec_accuracy;
};

// Maximum extents for bounded acquisition probes, not on-air field sizes.
inline constexpr std::size_t packet_header_size = 8;
inline constexpr std::size_t packet_header_parity = 6;
inline constexpr std::size_t packet_prefix_size = packet_header_size + packet_header_parity;
inline constexpr std::size_t packet_min_prefix_size = 4;
Bytes encode_packet(const Message& message, const PacketOptions& options = {},
                    std::size_t max_memory = default_memory_limit);
DecodedPacket decode_packet(const Bytes& wire, const PacketOptions& options = {},
                            std::size_t max_memory = default_memory_limit);
// Numeric structure from the codec's protected header and actual shortened-RS
// rules. Contains no payload, metadata values, integrity bytes or key material.
struct PacketLayout {
    FecMode fec=FecMode::off;
    bool compressed=false,authenticated=false;
    std::size_t header_bytes=0,header_parity_bytes=0,metadata_bytes=0;
    std::size_t original_bytes=0,payload_bytes=0,integrity_bytes=0;
    std::size_t body_bytes=0,body_parity_bytes=0,wire_bytes=0;
    std::size_t block_capacity=0,block_count=0,full_block_parity=0;
    std::size_t last_block_data=0,last_block_parity=0;
};
PacketLayout packet_layout(const Bytes& wire,
                           std::size_t max_memory = default_memory_limit);
// Hypothetical empty-content baseline with the original message's effective
// FEC retained for airtime accounting. No bytes or nonconforming tiny frame
// are emitted; actual original/payload sizes in this numeric layout are zero.
PacketLayout packet_empty_layout(const Message& message,FecMode requested_fec);
// Incomplete prefixes return nullopt; invalid or oversized complete prefixes throw.
// Trailing demodulator bytes do not contribute to the packet length.
// Bounded hypothesis check on decrypted bytes. Incomplete probes remain
// possible; a complete maximum probe must contain a valid repaired header.
bool packet_bootstrap_possible(const Bytes& prefix,
                               std::size_t max_memory = default_memory_limit);
std::optional<std::size_t> packet_frame_size(const Bytes& prefix,
                                           std::size_t max_memory = default_memory_limit);
// Acquisition probe: invalid, incomplete, oversized and ambiguous headers
// return nullopt without using exceptions for ordinary rejection. Allocation
// failures still propagate. The strict packet_frame_size API remains above.
std::optional<std::size_t> packet_probe_frame_size(const Bytes& prefix,
                                                 std::size_t max_memory = default_memory_limit);
std::optional<std::size_t> packet_header_extent(const Bytes& prefix,
                                              std::size_t max_memory = default_memory_limit);
bool safe_filename(const std::string& name);

// A bounded best-effort view of a partial frame. Never authenticated or safe
// to save/copy as verified content; it is replaced by decode_packet's result.
struct PacketPreview { Message message; std::size_t wire_size = 0; };
std::optional<PacketPreview> preview_packet_partial(const Bytes& wire,
                    std::size_t max_memory = default_memory_limit);

namespace packet_codec {
// Shortened systematic RS over GF(256), polynomial 0x11d, first root alpha^0 = 1.
Bytes rs_encode(const Bytes& data, std::size_t parity_symbols);
std::size_t rs_correct(Bytes& codeword, std::size_t parity_symbols);
}
}
