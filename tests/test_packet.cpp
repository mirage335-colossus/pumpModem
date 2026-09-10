#include "datapump/packet.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
template<class F> void rejects(F function, const char* what) {
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error(what);
}
Message text_message(const std::string& value) {
    Message message;
    message.data.assign(value.begin(), value.end());
    return message;
}
void test_rs() {
    for (const auto parity : {2U, 10U, 32U, 42U, 90U}) {
        Bytes original(255 - parity);
        for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<std::uint8_t>(i * 17 + 3);
        const auto pristine = packet_codec::rs_encode(original, parity);
        check(pristine.size() == 255, "RS codeword length");
        check(std::equal(original.begin(), original.end(), pristine.begin()), "RS systematic data");
        auto damaged = pristine;
        for (std::size_t i = 0; i < parity / 2; ++i) damaged[(i * 7) % 255] ^= static_cast<std::uint8_t>(i + 1);
        check(packet_codec::rs_correct(damaged, parity) == parity / 2, "RS correction count at limit");
        check(damaged == pristine, "RS correction at limit");
        check(packet_codec::rs_correct(damaged, parity) == 0, "RS pristine count");
    }
    auto short_code = packet_codec::rs_encode(Bytes{0x01}, 2);
    check(short_code == Bytes({1, 3, 2}), "RS known polynomial vector (x+1)(x+2)");
    short_code.back() ^= 0xff;
    check(packet_codec::rs_correct(short_code, 2) == 1, "short RS correction");
    rejects([] { packet_codec::rs_encode(Bytes(255), 2); }, "reject invalid RS size");

    std::mt19937 random(0x504d);
    for (unsigned trial = 0; trial < 300; ++trial) {
        const std::size_t parity = 2 * (1 + random() % 45);
        const std::size_t length = 1 + random() % (255 - parity);
        Bytes original(length);
        for (auto& byte : original) byte = static_cast<std::uint8_t>(random());
        auto damaged = packet_codec::rs_encode(original, parity);
        const auto pristine = damaged;
        std::vector<std::size_t> positions(damaged.size());
        for (std::size_t i = 0; i < positions.size(); ++i) positions[i] = i;
        std::shuffle(positions.begin(), positions.end(), random);
        const std::size_t count = 1 + random() % (parity / 2);
        for (std::size_t i = 0; i < count; ++i) damaged[positions[i]] ^= static_cast<std::uint8_t>(1 + random() % 255);
        check(packet_codec::rs_correct(damaged, parity) == count && damaged == pristine, "seeded shortened RS properties");
    }
}
void test_compression() {
    // Frozen prefix-code vector: 000 space, 001 e, 010 t, then zero padding.
    check(packet_codec::compress_short_v2(Bytes{' ', 'e', 't'}) == Bytes({0x05, 0x00}), "three-bit frequent byte codes");
    check(packet_codec::compress_short_v2(Bytes(80, 'e')).size() == 30, "common bytes use exactly three bits");
    for (std::size_t length=0; length<256; ++length) {
        Bytes input(length);
        for (std::size_t i=0;i<length;++i) input[i]=static_cast<std::uint8_t>(i+length);
        check(packet_codec::decompress_short_v2(packet_codec::compress_short_v2(input),length)==input,"prefix code preserves every byte");
    }
    rejects([] { packet_codec::decompress_short_v2({},1); }, "prefix truncation rejected");
    rejects([] { packet_codec::decompress_short_v2(Bytes{0xff},1); }, "partial literal rejected");
    rejects([] { packet_codec::decompress_short_v2(Bytes{1},1); }, "nonzero prefix padding rejected");
    const auto common = text_message("the message is received and the station is ready for the next message\n").data;
    const auto compressed = packet_codec::compress_short(common);
    check(compressed.size() < common.size(), "common text compression");
    check(packet_codec::decompress_short(compressed, common.size()) == common, "dictionary roundtrip");
    check(compressed == packet_codec::compress_short(common), "dictionary deterministic");
    for (std::size_t length = 0; length < 256; ++length) {
        Bytes input(length);
        for (std::size_t i = 0; i < length; ++i) input[i] = static_cast<std::uint8_t>(i + length);
        check(packet_codec::decompress_short(packet_codec::compress_short(input), length) == input, "all byte values lossless");
    }
    auto invalid = compressed;
    invalid.push_back(0);
    rejects([&] { packet_codec::decompress_short(invalid, common.size()); }, "reject trailing compressed bytes");
    rejects([] { packet_codec::decompress_short({}, 1); }, "reject compressed truncation");
    rejects([] { packet_codec::decompress_short({}, 256); }, "bound compressed allocation");
}
void test_partial_preview() {
    for (auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        for (const auto& content:{std::string(100,'e'),std::string("Hello, this is progressively received text."),std::string(600,'x')}) {
            auto message=text_message(content); message.callsign="N0CALL";
            PacketOptions options; options.fec=fec;
            const auto wire=encode_packet(message,options);
            bool saw_partial=false;
            for (std::size_t n=0;n<wire.size();++n) {
                auto preview=preview_packet_partial(Bytes(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(n)));
                if (preview && !preview->message.data.empty()) {
                    check(preview->wire_size==wire.size(),"preview frame extent");
                    check(preview->message.data.size()<=message.data.size(),"bounded preview output");
                    check(std::equal(preview->message.data.begin(),preview->message.data.end(),message.data.begin()),"preview matches actual content prefix");
                    saw_partial=true;
                }
            }
            check(saw_partial,"text visible before final validation/footer");
            check(decode_packet(wire,options).message.data==message.data,"final corrected content preserved");
        }
    }
    auto damaged=encode_packet(text_message("hello")); damaged[0]^=0xff;
    check(preview_packet_partial(damaged).has_value(),"preview corrects protected bootstrap");
    check(!preview_packet_partial(Bytes(1000,0xff)).has_value(),"invalid preview cannot manufacture metadata");
}
void test_packets() {
    for (const auto mode : {FecMode::off, FecMode::rs20, FecMode::rs60}) {
        PacketOptions options;
        options.fec = mode;
        auto message = text_message("the message is received\n\xF0\x9F\x8C\x8D");
        message.callsign = "N0CALL";
        message.grid = "AA00aa";
        message.repeatable = true;
        auto wire = encode_packet(message, options);
        const auto decoded = decode_packet(wire);
        check(decoded.message.data == message.data, "packet content");
        check(decoded.message.callsign == message.callsign && decoded.message.grid == message.grid, "packet metadata");
        check(decoded.message.repeatable, "repeat flag");
        check(std::any_of(decoded.message.id.begin(), decoded.message.id.end(), [](auto b) { return b != 0; }), "random identifier");
        check(encode_packet(decoded.message, options) == wire, "repeat retains identifier and checksum");
        check(!decoded.authenticated, "digest is not authentication");
        check(packet_frame_size(wire) == wire.size(), "self-described packet size");
        wire.insert(wire.end(), 31, 0x55);
        check(decode_packet(wire).consumed_bytes == decoded.consumed_bytes, "ignore demodulator tail");
        wire.resize(decoded.consumed_bytes - 1);
        rejects([&] { decode_packet(wire); }, "reject truncated frame");
    }
    check(!packet_frame_size(Bytes(packet_prefix_size - 1)), "incomplete bootstrap");
    rejects([] { decode_packet(Bytes(100, 0xff)); }, "reject random noise");
    auto message = text_message(std::string(4096, 'x'));
    auto wire = encode_packet(message);
    rejects([&] { encode_packet(message, {}, 1024); }, "encoder memory budget before payload copies");
    rejects([&] { decode_packet(wire, {}, 1024); }, "memory budget before body allocation");
    rejects([&] { packet_frame_size(wire, 1024); }, "prefix memory budget");
    auto invalid = message;
    invalid.kind = static_cast<MessageKind>(255);
    rejects([&] { encode_packet(invalid); }, "reject unknown message kind");
    message.data.clear();
    check(decode_packet(encode_packet(message)).message.data.empty(), "empty packet");
}
void test_corruption() {
    auto message = text_message(std::string(2000, 'x'));
    auto wire = encode_packet(message);
    for (std::size_t i = 0; i < 16; ++i) wire[i * 4] ^= static_cast<std::uint8_t>(i + 1);
    const auto repaired = decode_packet(wire);
    check(repaired.corrected_bytes == 16, "bootstrap error correction");
    check(repaired.message.data == message.data, "bootstrap repair data");
    wire = encode_packet(message);
    for (std::size_t i = 0; i < 100; ++i) wire[packet_prefix_size + i] ^= 0x5a;
    check(decode_packet(wire).message.data == message.data, "interleaver handles burst errors");
    PacketOptions off;
    off.fec = FecMode::off;
    wire = encode_packet(message, off);
    wire[packet_prefix_size + 40] ^= 1;
    rejects([&] { decode_packet(wire); }, "digest detects uncorrected corruption");
    wire = encode_packet(text_message("x"));
    for (std::size_t i = packet_prefix_size; i < wire.size(); ++i) wire[i] ^= 0xaa;
    rejects([&] { decode_packet(wire); }, "reject corruption past RS capability");
}
void test_authentication() {
    PacketOptions options;
    // This deliberately simple test double checks callback boundaries, not MAC strength.
    options.authenticator = [](const Bytes& input) {
        Bytes result(32, 0xa7);
        for (std::size_t i = 0; i < input.size(); ++i) result[i % 32] ^= input[i];
        return result;
    };
    options.verifier = [&](const Bytes& input, const Bytes& tag) { return options.authenticator(input) == tag; };
    const auto wire = encode_packet(text_message("authenticated message"), options);
    check(decode_packet(wire, options).authenticated, "MAC callback invoked");
    rejects([&] { decode_packet(wire); }, "reject MAC packet without verifier");
    auto wrong = options;
    wrong.verifier = [](const Bytes&, const Bytes&) { return false; };
    rejects([&] { decode_packet(wire, wrong); }, "reject incorrect MAC");
    const auto plain = encode_packet(text_message("unauthenticated message"));
    rejects([&] { decode_packet(plain, options); }, "configured MAC must reject digest downgrade");
    options.authenticator = [](const Bytes&) { return Bytes(1); };
    rejects([&] { encode_packet(text_message("x"), options); }, "reject wrong MAC size");
}
void test_filenames() {
    for (const auto* name : {"../x", "..", ".", "/etc/passwd", "a/b", "a\\b", "C:x", "CON", "con.txt", "LPT1.bin", "COM\xc2\xb9.txt", "LPT\xc2\xb2", "CON .txt", "file.", "file ", "a\n.txt"}) {
        check(!safe_filename(name), "reject unsafe filename");
        Message message;
        message.kind = MessageKind::file;
        message.filename = name;
        rejects([&] { encode_packet(message); }, "unsafe file metadata rejected");
    }
    check(safe_filename("my picture.png"), "ordinary basename allowed");
    Message message;
    message.kind = MessageKind::screenshot;
    message.filename = "screen.png";
    message.data = {0, 1, 0xff, 0x80};
    const auto decoded = decode_packet(encode_packet(message)).message;
    check(decoded.kind == MessageKind::screenshot && decoded.filename == message.filename && decoded.data == message.data, "binary screenshot");
    for (const std::string invalid : {"\xff", "\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f"}) {
        message.filename = "file.bin";
        message.callsign = invalid;
        rejects([&] { encode_packet(message); }, "reject invalid UTF8 callsign");
        message.callsign.clear();
        message.grid = invalid;
        rejects([&] { encode_packet(message); }, "reject invalid UTF8 grid");
        message.grid.clear();
        message.filename = invalid + ".bin";
        rejects([&] { encode_packet(message); }, "reject invalid UTF8 filename");
        check(!safe_filename(message.filename), "safe filename requires UTF8");
    }
    auto binary_text = text_message(std::string("\xff\x80", 2));
    check(decode_packet(encode_packet(binary_text)).message.data == binary_text.data,
          "arbitrary text payload bytes remain lossless");
}

void refresh_header_crc(Bytes& header) {
    std::uint32_t checksum = 0xffffffff;
    for (std::size_t i = 0; i < 36; ++i) {
        checksum ^= header[i];
        for (unsigned bit = 0; bit < 8; ++bit) checksum = (checksum >> 1) ^ (0xedb88320U & (0U - (checksum & 1U)));
    }
    checksum ^= 0xffffffff;
    for (std::size_t i = 0; i < 4; ++i) header[36 + i] = static_cast<std::uint8_t>(checksum >> (24 - i * 8));
}
void refresh_digest(Bytes& wire) {
    Bytes header(wire.begin(), wire.begin() + packet_prefix_size);
    packet_codec::rs_correct(header, 32);
    header.resize(40);
    header.insert(header.end(), wire.begin() + packet_prefix_size, wire.end() - 32);
    unsigned length = 0;
    check(EVP_Digest(header.data(), header.size(), wire.data() + wire.size() - 32, &length, EVP_sha256(), nullptr) == 1 && length == 32,
          "test packet digest generation");
}
void test_adversarial_metadata() {
    PacketOptions options;
    options.fec = FecMode::off;
    Message message;
    message.kind = MessageKind::file;
    message.filename = "ok.bin";
    message.data = {1, 2, 3};
    auto wire = encode_packet(message, options);
    // Valid checksum/FEC bootstrap with a deliberately enormous declared body.
    Bytes header(wire.begin(), wire.begin() + packet_prefix_size);
    packet_codec::rs_correct(header, 32);
    header.resize(40);
    std::fill(header.begin() + 8, header.begin() + 16, 0xff);
    refresh_header_crc(header);
    auto prefix = packet_codec::rs_encode(header, 32);
    rejects([&] { packet_frame_size(prefix); }, "reject checked attacker-controlled 64-bit allocation");

    // SHA-256 is unkeyed: a sender can supply a valid digest for hostile metadata.
    // Decoding must validate metadata independently after checking integrity.
    wire = encode_packet(message, options);
    const std::string bad_filename = "../bin";
    std::copy(bad_filename.begin(), bad_filename.end(), wire.begin() + packet_prefix_size + 26);
    refresh_digest(wire);
    rejects([&] { decode_packet(wire); }, "reject integrity-valid traversal metadata");
    wire = encode_packet(message, options);
    wire[packet_prefix_size + 26] = 0xff;
    refresh_digest(wire);
    rejects([&] { decode_packet(wire); }, "reject integrity-valid malformed UTF8 filename");
    auto call_message = text_message("x");
    call_message.callsign = "ABC";
    wire = encode_packet(call_message, options);
    wire[packet_prefix_size + 26] = 0xff;
    refresh_digest(wire);
    rejects([&] { decode_packet(wire); }, "reject integrity-valid malformed UTF8 callsign");
    wire = encode_packet(message, options);
    wire[packet_prefix_size + 20] = 0xff;
    wire[packet_prefix_size + 21] = 0xff;
    refresh_digest(wire);
    rejects([&] { decode_packet(wire); }, "reject integrity-valid metadata overflow");
    wire = encode_packet(message, options);
    wire[packet_prefix_size] ^= 1;
    refresh_digest(wire);
    rejects([&] { decode_packet(wire); }, "identifier checksum independently validated");
}
}
int main() {
    try {
        test_rs(); test_compression(); test_partial_preview(); test_packets(); test_corruption(); test_authentication(); test_filenames(); test_adversarial_metadata();
        std::cout << "packet tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "packet tests failed: " << error.what() << '\n';
        return 1;
    }
}
