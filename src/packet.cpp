#include "datapump/packet.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace datapump {
namespace {
constexpr std::size_t header_size = 40;
constexpr std::size_t header_parity = 32;
constexpr std::size_t tag_size = 32;
constexpr std::size_t body_metadata_size = 26; // ID, ID checksum, three string lengths.
constexpr std::uint8_t compressed_flag = 1, authenticated_flag = 2, repeat_flag = 4;

struct Field {
    std::array<std::uint8_t, 512> exp{};
    std::array<std::uint8_t, 256> log{};
    Field() {
        unsigned value = 1;
        for (unsigned i = 0; i < 255; ++i) {
            exp[i] = static_cast<std::uint8_t>(value);
            log[value] = static_cast<std::uint8_t>(i);
            value <<= 1;
            if ((value & 256) != 0) value ^= 0x11d;
        }
        for (std::size_t i = 255; i < exp.size(); ++i) exp[i] = exp[i - 255];
    }
    std::uint8_t mul(std::uint8_t a, std::uint8_t b) const {
        return a == 0 || b == 0 ? 0 : exp[static_cast<unsigned>(log[a]) + log[b]];
    }
    std::uint8_t div(std::uint8_t a, std::uint8_t b) const {
        if (b == 0) throw Error("Reed-Solomon division by zero");
        return a == 0 ? 0 : exp[static_cast<unsigned>(log[a]) + 255 - log[b]];
    }
};
const Field gf;

std::uint8_t evaluate(const Bytes& polynomial, std::uint8_t x) {
    std::uint8_t value = 0;
    for (const auto coefficient : polynomial) value = gf.mul(value, x) ^ coefficient;
    return value;
}
Bytes syndromes(const Bytes& codeword, std::size_t parity) {
    Bytes result(parity);
    for (std::size_t i = 0; i < parity; ++i) result[i] = evaluate(codeword, gf.exp[i]);
    return result;
}
bool all_zero(const Bytes& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto value) { return value == 0; });
}
std::size_t add_size(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a) throw Error("Packet length overflow");
    return a + b;
}
std::size_t mul_size(std::size_t a, std::size_t b) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) throw Error("Packet length overflow");
    return a * b;
}
void put_integer(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) bytes[offset + count - 1 - i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint64_t integer(const Bytes& bytes, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
std::size_t bounded_integer(const Bytes& bytes, std::size_t offset, std::size_t count, std::size_t bound) {
    const auto value = integer(bytes, offset, count);
    if (value > bound) throw Error("Packet exceeds memory limit");
    return static_cast<std::size_t>(value);
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
}
std::uint32_t id_checksum(const std::array<std::uint8_t, 16>& id, bool repeatable) {
    std::array<std::uint8_t, 17> input{};
    std::copy(id.begin(), id.end(), input.begin());
    input.back() = repeatable ? 1 : 0;
    return crc32(input.data(), input.size());
}
Bytes digest(const Bytes& bytes) {
    Bytes result(tag_size);
    unsigned length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), result.data(), &length, EVP_sha256(), nullptr) != 1 || length != tag_size)
        throw Error("SHA-256 failed");
    return result;
}
void validate_kind(MessageKind kind) {
    if (kind != MessageKind::text && kind != MessageKind::file && kind != MessageKind::screenshot)
        throw Error("Unknown packet content kind");
}
void validate_fec(FecMode mode) {
    if (mode != FecMode::off && mode != FecMode::rs20 && mode != FecMode::rs60)
        throw Error("Unknown Reed-Solomon mode");
}
bool valid_utf8(std::string_view value) {
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[offset++]);
        if (first < 0x80) continue;
        std::uint32_t codepoint = 0, minimum = 0;
        std::size_t continuation = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; codepoint = first & 0x1fU; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; codepoint = first & 0x0fU; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; codepoint = first & 0x07U; minimum = 0x10000; }
        else return false;
        if (continuation > value.size() - offset) return false;
        for (std::size_t i = 0; i < continuation; ++i) {
            const auto byte = static_cast<std::uint8_t>(value[offset++]);
            if ((byte & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6) | (byte & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}
void validate_metadata(const Message& message) {
    validate_kind(message.kind);
    if ((!message.filename.empty() || message.kind != MessageKind::text) && !safe_filename(message.filename))
        throw Error("Unsafe attachment filename");
    const auto clean = [](const std::string& value, std::size_t maximum) {
        return value.size() <= maximum && valid_utf8(value) && std::none_of(value.begin(), value.end(), [](unsigned char byte) {
            return byte < 32 || byte == 127;
        });
    };
    if (!clean(message.callsign, 64) || !clean(message.grid, 32)) throw Error("Invalid callsign or grid metadata");
}
std::size_t block_capacity(FecMode mode) { return mode == FecMode::rs20 ? 210 : 150; }
std::size_t parity_count(std::size_t count, FecMode mode) {
    if (mode == FecMode::off || count == 0) return 0;
    const std::size_t parity = (count * (mode == FecMode::rs20 ? 1 : 3) + 4) / 5;
    return (parity + 1) & ~std::size_t{1};
}
std::size_t coded_size(std::size_t length, FecMode mode) {
    if (mode == FecMode::off) return length;
    const auto capacity = block_capacity(mode);
    const auto whole = length / capacity;
    const auto remainder = length % capacity;
    return add_size(mul_size(whole, capacity + parity_count(capacity, mode)), remainder + parity_count(remainder, mode));
}

struct Header {
    Bytes bytes;
    FecMode fec = FecMode::off;
    std::uint8_t flags = 0;
    MessageKind kind = MessageKind::text;
    std::size_t body_length = 0, original_length = 0, payload_length = 0, wire_length = 0, corrected = 0;
};
Header read_header(const Bytes& wire, std::size_t max_memory) {
    if (wire.size() < packet_prefix_size) throw Error("Incomplete packet bootstrap");
    Header result;
    result.bytes.assign(wire.begin(), wire.begin() + packet_prefix_size);
    result.corrected = packet_codec::rs_correct(result.bytes, header_parity);
    result.bytes.resize(header_size);
    const auto& h = result.bytes;
    if (h[0] != 'D' || h[1] != 'P' || h[2] != '0' || h[3] != '1' || h[4] != 1 ||
        h[34] != 0 || h[35] != 0 || integer(h, 32, 2) != tag_size ||
        integer(h, 36, 4) != crc32(h.data(), 36)) throw Error("Invalid packet bootstrap");
    result.fec = static_cast<FecMode>(h[5]);
    result.flags = h[6];
    result.kind = static_cast<MessageKind>(h[7]);
    validate_fec(result.fec);
    validate_kind(result.kind);
    if ((result.flags & ~std::uint8_t{7}) != 0) throw Error("Unknown packet flags");
    result.body_length = bounded_integer(h, 8, 8, max_memory);
    result.original_length = bounded_integer(h, 16, 8, max_memory);
    result.payload_length = bounded_integer(h, 24, 8, max_memory);
    if (result.body_length < body_metadata_size + tag_size || result.payload_length > result.body_length - body_metadata_size - tag_size)
        throw Error("Invalid packet payload length");
    const auto metadata_length = result.body_length - result.payload_length - tag_size;
    if (metadata_length > body_metadata_size + 255 + 64 + 32) throw Error("Invalid packet metadata length");
    if ((result.flags & compressed_flag) != 0) {
        if (result.original_length >= 256 || result.payload_length >= result.original_length)
            throw Error("Invalid short compression lengths");
    } else if (result.payload_length != result.original_length) throw Error("Inconsistent packet payload lengths");
    result.wire_length = add_size(packet_prefix_size, coded_size(result.body_length, result.fec));
    // Bound working buffers before allocating any length obtained from the wire.
    // The caller's input buffer is not counted; the codec reserves coded body,
    // recovered body, canonical authentication input, and the resulting payload.
    const auto working = add_size(add_size(coded_size(result.body_length, result.fec), mul_size(result.body_length, 2)),
                                  add_size(result.original_length, 4096));
    if (working > max_memory) throw Error("Packet exceeds decoder working memory limit");
    return result;
}
Bytes encode_body(const Bytes& body, FecMode mode) {
    if (mode == FecMode::off) return body;
    const auto capacity = block_capacity(mode);
    const auto block_count = (body.size() + capacity - 1) / capacity;
    const auto full_width = capacity + parity_count(capacity, mode);
    Bytes rows;
    rows.reserve(coded_size(body.size(), mode));
    for (std::size_t offset = 0; offset < body.size(); offset += capacity) {
        const auto count = std::min(capacity, body.size() - offset);
        const Bytes chunk(body.begin() + static_cast<std::ptrdiff_t>(offset), body.begin() + static_cast<std::ptrdiff_t>(offset + count));
        const auto encoded = packet_codec::rs_encode(chunk, parity_count(count, mode));
        rows.insert(rows.end(), encoded.begin(), encoded.end());
    }
    Bytes result;
    result.reserve(rows.size());
    // Transmit columns: consecutive channel errors spread across RS codewords.
    for (std::size_t column = 0; column < full_width; ++column)
        for (std::size_t row = 0; row < block_count; ++row) {
            const auto index = row * full_width + column;
            if (index < rows.size()) result.push_back(rows[index]);
        }
    return result;
}
Bytes decode_body(const Bytes& wire, const Header& header, std::size_t& corrected) {
    if (header.fec == FecMode::off)
        return Bytes(wire.begin() + packet_prefix_size, wire.begin() + static_cast<std::ptrdiff_t>(header.wire_length));
    const auto capacity = block_capacity(header.fec);
    const auto block_count = (header.body_length + capacity - 1) / capacity;
    const auto full_width = capacity + parity_count(capacity, header.fec);
    Bytes rows(header.wire_length - packet_prefix_size);
    std::size_t source = packet_prefix_size;
    for (std::size_t column = 0; column < full_width; ++column)
        for (std::size_t row = 0; row < block_count; ++row) {
            const auto index = row * full_width + column;
            if (index < rows.size()) rows[index] = wire[source++];
        }
    Bytes result;
    result.reserve(header.body_length);
    for (std::size_t row = 0; row < block_count; ++row) {
        const auto count = std::min(capacity, header.body_length - row * capacity);
        const auto start = row * full_width;
        const auto parity = parity_count(count, header.fec);
        Bytes block(rows.begin() + static_cast<std::ptrdiff_t>(start), rows.begin() + static_cast<std::ptrdiff_t>(start + count + parity));
        corrected += packet_codec::rs_correct(block, parity);
        result.insert(result.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
    }
    return result;
}

constexpr std::string_view alphabet = " etaoinshrdlucmfwypvbgkqjxz0123\n";
static_assert(alphabet.size() == 32);
constexpr std::array<std::string_view, 64> dictionary = {
    "the ", "The ", "and ", "message", "received", "station", "ready", "please",
    "hello", "thank", "you", "this ", "that ", "with ", "from ", "have ",
    "for ", "your ", "will ", "are ", "not ", "can ", "all ", "test",
    "ing", "tion", " to ", " is ", " in ", " of ", " on ", " at ",
    "CQ", "QRS", "73", "599", "copy", "send", "file", "next",
    "time", "good", "signal", "power", "radio", "call", "grid", "data",
    "pump", "status", "normal", "distress", "over", "out", "yes", "no",
    "http", "://", ".com", "www.", "00", "11", "  ", "\r\n"
};
struct BitWriter {
    Bytes bytes;
    unsigned position = 0;
    void put(unsigned value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            if (position == 0) bytes.push_back(0);
            bytes.back() |= static_cast<std::uint8_t>(((value >> (count - i - 1)) & 1U) << (7 - position));
            position = (position + 1) % 8;
        }
    }
};
struct BitReader {
    const Bytes& bytes;
    std::size_t position = 0;
    unsigned get(unsigned count) {
        if (count > bytes.size() * 8 - position) throw Error("Truncated dictionary stream");
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i, ++position)
            value = (value << 1) | ((bytes[position / 8] >> (7 - position % 8)) & 1U);
        return value;
    }
};
}

namespace packet_codec {
Bytes rs_encode(const Bytes& data, std::size_t parity_symbols) {
    if (data.empty() || parity_symbols == 0 || parity_symbols >= 255 || data.size() > 255 - parity_symbols)
        throw Error("Invalid Reed-Solomon block dimensions");
    Bytes generator{1};
    for (std::size_t root = 0; root < parity_symbols; ++root) {
        Bytes next(generator.size() + 1);
        for (std::size_t i = 0; i < generator.size(); ++i) {
            next[i] ^= generator[i];
            next[i + 1] ^= gf.mul(generator[i], gf.exp[root]);
        }
        generator = std::move(next);
    }
    // Allocate the complete shortened codeword once. The parity region starts
    // at zero; the data region is the polynomial division input.
    Bytes result(data.size() + parity_symbols, 0);
    std::copy(data.begin(), data.end(), result.begin());
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto coefficient = result[i];
        for (std::size_t j = 1; j < generator.size(); ++j) result[i + j] ^= gf.mul(generator[j], coefficient);
    }
    std::copy(data.begin(), data.end(), result.begin());
    return result;
}

std::size_t rs_correct(Bytes& codeword, std::size_t parity_symbols) {
    if (parity_symbols == 0 || parity_symbols >= codeword.size() || codeword.size() > 255)
        throw Error("Invalid Reed-Solomon block dimensions");
    const auto syndrome = syndromes(codeword, parity_symbols);
    if (all_zero(syndrome)) return 0;
    // Berlekamp-Massey: locator coefficients are ordered by increasing power.
    Bytes locator(parity_symbols + 1), previous(parity_symbols + 1);
    locator[0] = previous[0] = 1;
    std::size_t degree = 0, shift = 1;
    std::uint8_t previous_discrepancy = 1;
    for (std::size_t step = 0; step < parity_symbols; ++step) {
        auto discrepancy = syndrome[step];
        for (std::size_t i = 1; i <= degree; ++i) discrepancy ^= gf.mul(locator[i], syndrome[step - i]);
        if (discrepancy == 0) { ++shift; continue; }
        const auto saved = locator;
        const auto factor = gf.div(discrepancy, previous_discrepancy);
        for (std::size_t i = 0; i + shift < locator.size(); ++i) locator[i + shift] ^= gf.mul(factor, previous[i]);
        if (2 * degree <= step) {
            degree = step + 1 - degree;
            previous = saved;
            previous_discrepancy = discrepancy;
            shift = 1;
        } else ++shift;
    }
    if (degree == 0 || degree * 2 > parity_symbols) throw Error("Uncorrectable Reed-Solomon block");
    std::vector<std::size_t> positions;
    Bytes locations;
    for (std::size_t position = 0; position < codeword.size(); ++position) {
        const auto exponent = codeword.size() - 1 - position;
        const auto inverse = gf.exp[255 - exponent];
        std::uint8_t value = locator[degree];
        for (std::size_t i = degree; i > 0; --i) value = gf.mul(value, inverse) ^ locator[i - 1];
        if (value == 0) {
            positions.push_back(position);
            locations.push_back(gf.exp[exponent]);
        }
    }
    if (positions.size() != degree) throw Error("Uncorrectable Reed-Solomon locator");
    // Solve the small Vandermonde system for error magnitudes. This avoids
    // convention-sensitive Forney offsets and also covers shortened codewords.
    std::vector<Bytes> matrix(degree, Bytes(degree + 1));
    for (std::size_t column = 0; column < degree; ++column) {
        std::uint8_t power = 1;
        for (std::size_t row = 0; row < degree; ++row) {
            matrix[row][column] = power;
            power = gf.mul(power, locations[column]);
        }
    }
    for (std::size_t row = 0; row < degree; ++row) matrix[row][degree] = syndrome[row];
    for (std::size_t column = 0; column < degree; ++column) {
        auto pivot = column;
        while (pivot < degree && matrix[pivot][column] == 0) ++pivot;
        if (pivot == degree) throw Error("Singular Reed-Solomon error system");
        std::swap(matrix[column], matrix[pivot]);
        const auto scale = matrix[column][column];
        for (std::size_t i = column; i <= degree; ++i) matrix[column][i] = gf.div(matrix[column][i], scale);
        for (std::size_t row = 0; row < degree; ++row) {
            if (row == column) continue;
            const auto factor = matrix[row][column];
            for (std::size_t i = column; i <= degree; ++i) matrix[row][i] ^= gf.mul(factor, matrix[column][i]);
        }
    }
    auto repaired = codeword;
    for (std::size_t i = 0; i < degree; ++i) repaired[positions[i]] ^= matrix[i][degree];
    if (!all_zero(syndromes(repaired, parity_symbols))) throw Error("Reed-Solomon verification failed");
    codeword = std::move(repaired);
    return degree;
}

Bytes compress_short(const Bytes& input) {
    if (input.size() >= 256) throw Error("Dictionary compression is limited to short messages");
    BitWriter writer;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t best_length = 0, best_word = 0;
        for (std::size_t i = 0; i < dictionary.size(); ++i) {
            const auto word = dictionary[i];
            if (word.size() > best_length && word.size() <= input.size() - offset &&
                std::equal(word.begin(), word.end(), input.begin() + static_cast<std::ptrdiff_t>(offset))) {
                best_length = word.size(); best_word = i;
            }
        }
        if (best_length != 0) {
            writer.put(1, 2); writer.put(static_cast<unsigned>(best_word), 6); offset += best_length;
        } else {
            const auto index = alphabet.find(static_cast<char>(input[offset]));
            if (index != std::string_view::npos) { writer.put(0, 2); writer.put(static_cast<unsigned>(index), 5); }
            else { writer.put(2, 2); writer.put(input[offset], 8); }
            ++offset;
        }
    }
    return writer.bytes;
}
Bytes decompress_short(const Bytes& encoded, std::size_t original_size) {
    if (original_size >= 256 || encoded.size() > 320) throw Error("Invalid short dictionary size");
    BitReader reader{encoded};
    Bytes result;
    result.reserve(original_size);
    while (result.size() < original_size) {
        switch (reader.get(2)) {
        case 0: result.push_back(static_cast<std::uint8_t>(alphabet[reader.get(5)])); break;
        case 1: {
            const auto word = dictionary[reader.get(6)];
            if (word.size() > original_size - result.size()) throw Error("Dictionary expansion exceeds declared length");
            result.insert(result.end(), word.begin(), word.end());
            break;
        }
        case 2: result.push_back(static_cast<std::uint8_t>(reader.get(8))); break;
        default: throw Error("Reserved dictionary token");
        }
    }
    const auto remaining = encoded.size() * 8 - reader.position;
    if (remaining > 7 || (remaining != 0 && reader.get(static_cast<unsigned>(remaining)) != 0))
        throw Error("Noncanonical dictionary padding");
    return result;
}
}

bool safe_filename(const std::string& name) {
    if (name.empty() || name.size() > 255 || !valid_utf8(name) || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
    for (const unsigned char byte : name)
        if (byte < 32 || byte == 127 || byte == '/' || byte == '\\' || byte == ':' || byte == '<' || byte == '>' || byte == '"' || byte == '|' || byte == '?' || byte == '*')
            return false;
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (auto& character : stem) if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.starts_with("COM") || stem.starts_with("LPT")) {
        const auto suffix = stem.substr(3);
        if ((suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') ||
            suffix == "\xc2\xb9" || suffix == "\xc2\xb2" || suffix == "\xc2\xb3" ||
            suffix == "\xb9" || suffix == "\xb2" || suffix == "\xb3") return false;
    }
    return true;
}

Bytes encode_packet(const Message& message, const PacketOptions& options, std::size_t max_memory) {
    validate_metadata(message);
    validate_fec(options.fec);
    const auto metadata_size = body_metadata_size + message.filename.size() + message.callsign.size() + message.grid.size();
    const auto maximum_body = add_size(add_size(metadata_size, message.data.size()), tag_size);
    const auto maximum_coded = coded_size(maximum_body, options.fec);
    // Includes caller-owned message data, all codec buffers, and fixed scratch.
    // Use the uncompressed size so checking never depends on allocating first.
    const auto working = add_size(add_size(mul_size(maximum_coded, 2), mul_size(maximum_body, 2)),
                                  add_size(message.data.size(), 4096));
    if (working > max_memory) throw Error("Packet exceeds encoder working memory limit");
    Bytes payload = message.data;
    std::uint8_t flags = message.repeatable ? repeat_flag : 0;
    if (options.compression && !payload.empty() && payload.size() < 256) {
        auto compressed = packet_codec::compress_short(payload);
        if (compressed.size() < payload.size()) { payload = std::move(compressed); flags |= compressed_flag; }
    }
    if (options.authenticator) flags |= authenticated_flag;
    auto identifier = message.id;
    if (std::all_of(identifier.begin(), identifier.end(), [](auto byte) { return byte == 0; }) &&
        RAND_bytes(identifier.data(), static_cast<int>(identifier.size())) != 1) throw Error("Packet identifier generation failed");
    Bytes body(body_metadata_size);
    body.reserve(metadata_size + payload.size() + tag_size);
    std::copy(identifier.begin(), identifier.end(), body.begin());
    put_integer(body, 16, id_checksum(identifier, message.repeatable), 4);
    put_integer(body, 20, message.filename.size(), 2);
    put_integer(body, 22, message.callsign.size(), 2);
    put_integer(body, 24, message.grid.size(), 2);
    body.insert(body.end(), message.filename.begin(), message.filename.end());
    body.insert(body.end(), message.callsign.begin(), message.callsign.end());
    body.insert(body.end(), message.grid.begin(), message.grid.end());
    body.insert(body.end(), payload.begin(), payload.end());
    const auto payload_size = payload.size();
    payload = Bytes{};
    Bytes header(header_size);
    header[0] = 'D'; header[1] = 'P'; header[2] = '0'; header[3] = '1'; header[4] = 1;
    header[5] = static_cast<std::uint8_t>(options.fec); header[6] = flags; header[7] = static_cast<std::uint8_t>(message.kind);
    put_integer(header, 8, add_size(body.size(), tag_size), 8);
    put_integer(header, 16, message.data.size(), 8);
    put_integer(header, 24, payload_size, 8);
    put_integer(header, 32, tag_size, 2);
    put_integer(header, 36, crc32(header.data(), 36), 4);
    Bytes tag;
    {
        Bytes canonical;
        canonical.reserve(header.size() + body.size());
        canonical.insert(canonical.end(), header.begin(), header.end());
        canonical.insert(canonical.end(), body.begin(), body.end());
        tag = options.authenticator ? options.authenticator(canonical) : digest(canonical);
    }
    if (tag.size() != tag_size) throw Error("Packet authenticator must contain 32 bytes");
    body.insert(body.end(), tag.begin(), tag.end());
    auto result = packet_codec::rs_encode(header, header_parity);
    auto coded = encode_body(body, options.fec);
    result.insert(result.end(), coded.begin(), coded.end());
    return result;
}

std::optional<std::size_t> packet_frame_size(const Bytes& prefix, std::size_t max_memory) {
    if (prefix.size() < packet_prefix_size) return std::nullopt;
    return read_header(prefix, max_memory).wire_length;
}

DecodedPacket decode_packet(const Bytes& wire, const PacketOptions& options, std::size_t max_memory) {
    const auto header = read_header(wire, max_memory);
    if (wire.size() < header.wire_length) throw Error("Incomplete packet body");
    const bool authenticated = (header.flags & authenticated_flag) != 0;
    if (authenticated != static_cast<bool>(options.verifier)) throw Error("Packet authentication policy mismatch");
    if (!authenticated && options.authenticator) throw Error("Unauthenticated packet rejected by keyed receiver");
    DecodedPacket result;
    result.corrected_bytes = header.corrected;
    auto body = decode_body(wire, header, result.corrected_bytes);
    const auto content_end = body.size() - tag_size;
    const Bytes tag(body.begin() + static_cast<std::ptrdiff_t>(content_end), body.end());
    Bytes canonical = header.bytes;
    canonical.insert(canonical.end(), body.begin(), body.begin() + static_cast<std::ptrdiff_t>(content_end));
    if (authenticated) {
        if (!options.verifier(canonical, tag)) throw Error("Packet authentication failed");
    } else {
        const auto expected = digest(canonical);
        if (CRYPTO_memcmp(expected.data(), tag.data(), tag_size) != 0) throw Error("Packet integrity check failed");
    }
    auto& message = result.message;
    message.kind = header.kind;
    message.repeatable = (header.flags & repeat_flag) != 0;
    std::copy_n(body.begin(), message.id.size(), message.id.begin());
    if (integer(body, 16, 4) != id_checksum(message.id, message.repeatable)) throw Error("Packet identifier checksum failed");
    const auto filename_length = bounded_integer(body, 20, 2, 255);
    const auto callsign_length = bounded_integer(body, 22, 2, 64);
    const auto grid_length = bounded_integer(body, 24, 2, 32);
    const auto payload_offset = body_metadata_size + filename_length + callsign_length + grid_length;
    if (payload_offset > content_end || header.payload_length != content_end - payload_offset)
        throw Error("Invalid packet metadata lengths");
    auto string_at = [&](std::size_t offset, std::size_t count) {
        return std::string(body.begin() + static_cast<std::ptrdiff_t>(offset), body.begin() + static_cast<std::ptrdiff_t>(offset + count));
    };
    message.filename = string_at(body_metadata_size, filename_length);
    message.callsign = string_at(body_metadata_size + filename_length, callsign_length);
    message.grid = string_at(body_metadata_size + filename_length + callsign_length, grid_length);
    validate_metadata(message);
    Bytes payload(body.begin() + static_cast<std::ptrdiff_t>(payload_offset), body.begin() + static_cast<std::ptrdiff_t>(content_end));
    message.data = (header.flags & compressed_flag) != 0 ? packet_codec::decompress_short(payload, header.original_length) : std::move(payload);
    result.consumed_bytes = header.wire_length;
    result.authenticated = authenticated;
    return result;
}
}
