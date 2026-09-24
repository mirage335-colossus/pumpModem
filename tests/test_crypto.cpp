#include "datapump/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

using namespace datapump;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
Bytes bytes(std::string_view text) { return Bytes(text.begin(), text.end()); }
Bytes from_hex(std::string_view hex) {
    Bytes result;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return result;
}
Bytes read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void write_file(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(out), "fixture write failed");
}
struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("datapump-crypto-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDir() { check(std::filesystem::create_directory(path), "temporary directory creation failed"); }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

void test_streams() {
    Bytes master(32);
    for (std::size_t i = 0; i < master.size(); ++i) master[i] = static_cast<std::uint8_t>(i);
    Crypto crypto(master), same(master);
    rejects([&] { Crypto wrong(Bytes(31)); }, "short master key accepted");
    rejects([&] { Crypto wrong(Bytes(33)); }, "long master key accepted");
    const auto all = crypto.stream(StreamPurpose::Data, 1720000000, 0, 1024);
    // Frozen format vector: independent Python hashlib/hmac HKDF reference,
    // followed by the openssl enc AES-256-CTR CLI, zero initial counter.
    const auto vector = from_hex("4acaeb26d419c3bad12ea2ab9c0d76e0a5a15a4b9267c57b99bdd6f1caa6f600c"
                                 "425a4459dd369cf3186173c649efd74106a1624935e040d37192da26b567a8e");
    check(std::equal(vector.begin(), vector.end(), all.begin()), "keystream format vector mismatch");
    check(all == same.stream(StreamPurpose::Data, 1720000000, 0, 1024), "stream is not deterministic");
    check(all != crypto.stream(StreamPurpose::Data, 1720000001, 0, 1024), "epoch keys overlap");
    for (auto purpose : {StreamPurpose::Dsss, StreamPurpose::Scrambler, StreamPurpose::Fhss}) {
        check(all != crypto.stream(purpose, 1720000000, 0, 1024), "purpose keys overlap");
    }
    for (auto offset : {0U, 1U, 15U, 16U, 17U, 255U, 511U}) {
        const auto slice = crypto.stream(StreamPurpose::Data, 1720000000, offset, 127);
        check(std::equal(slice.begin(), slice.end(), all.begin() + offset), "random access stream mismatch");
    }
    auto plain = bytes("clipboard text\nsecond line");
    auto encrypted = crypto.xor_data(plain, 1720000000, 19);
    check(plain != encrypted, "encryption did not alter data");
    check(plain == crypto.xor_data(encrypted, 1720000000, 19), "CTR round trip failed");
    check(crypto.xor_data({}, 0).empty(), "empty stream failed");
    rejects([&] { crypto.stream(static_cast<StreamPurpose>(99), 0, 0, 1); }, "invalid purpose accepted");
    rejects([&] { crypto.stream(StreamPurpose::Data, 0, std::numeric_limits<std::uint64_t>::max(), 2); },
            "stream offset wrapped");
    check(crypto.stream(StreamPurpose::Data, std::numeric_limits<std::uint64_t>::max(),
                        std::numeric_limits<std::uint64_t>::max(), 1).size() == 1,
          "valid final offset failed");
    auto other_master = master;
    other_master[0] ^= 1;
    check(all != Crypto(other_master).stream(StreamPurpose::Data, 1720000000, 0, 1024), "master keys overlap");
}

void test_authentication() {
    Crypto crypto(Bytes(32, 0x72));
    const auto first = bytes("timestamp|metadata|message one");
    const auto second = bytes("timestamp|metadata|message two");
    const auto tag = crypto.mac(first);
    check(tag == from_hex("5015234b16cbd11766fb59c5221930c5d32a883975576b716ab3af4b6105869d"),
          "HMAC format vector mismatch");
    check(tag.size() == 32 && crypto.verify(first, tag), "MAC verification failed");
    check(!crypto.verify(second, tag), "changed message accepted");
    auto bad_tag = tag;
    bad_tag[31] ^= 1;
    check(!crypto.verify(first, bad_tag), "changed tag accepted");
    check(!crypto.verify(first, std::span(tag).first(31)), "truncated tag accepted");
    bad_tag.push_back(0);
    check(!crypto.verify(first, bad_tag), "oversize tag accepted");
    check(!Crypto(Bytes(32, 0x73)).verify(first, tag), "wrong MAC key accepted");
    check(crypto.verify({}, crypto.mac({})), "empty-message MAC failed");
    // Chosen plaintext and intentional CTR reuse reveal only reused stream
    // positions, not a usable MAC for changed plaintext.
    auto cipher = crypto.xor_data(first, 99);
    const auto stream = crypto.xor_data(Bytes(first.size()), 99);
    for (std::size_t i = 0; i < cipher.size(); ++i) cipher[i] ^= stream[i];
    check(cipher == first, "known plaintext reuse fixture failed");
    cipher.back() ^= 1;
    check(!crypto.verify(cipher, tag), "keystream knowledge forged authenticator");
}

void test_keyfiles() {
    TempDir dir;
    const testing::KeyfilePolicy policy{4096, 8193};
    const auto key = dir.path / "key.dpkey";
    testing::create_keyfile(key, policy);
    check(std::filesystem::file_size(key) == 48 + policy.header_bytes + 32 + 16, "wrong keyfile length");
    const auto original = read_file(key);
    const auto probe = testing::load_keyfile(key, policy).stream(StreamPurpose::Data, 50, 5, 128);
    check(probe == testing::load_keyfile(key, policy).stream(StreamPurpose::Data, 50, 5, 128), "keyfile round trip failed");
    rejects([&] { testing::create_keyfile(key, policy); }, "keyfile overwrite accepted");
    check(read_file(key) == original, "overwrite changed existing keyfile");
    rejects([&] { load_keyfile(key); }, "production loader accepted reduced header");
#ifndef _WIN32
    const auto permissions = std::filesystem::status(key).permissions();
    check((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none, "keyfile permissions are too broad");
    const auto link = dir.path / "link.dpkey";
    std::filesystem::create_symlink(key, link);
    rejects([&] { testing::create_keyfile(link, policy); }, "symlink keyfile overwrite accepted");
    check(read_file(key) == original, "symlink target was changed");
#endif
    for (std::size_t position : {std::size_t(0), std::size_t(15), std::size_t(31),
                                 std::size_t(32), std::size_t(48), original.size() - 33,
                                 original.size() - 17, original.size() - 1}) {
        auto corrupted = original;
        corrupted[position] ^= 1;
        write_file(key, corrupted);
        rejects([&] { testing::load_keyfile(key, policy); }, "corrupted keyfile accepted");
    }
    auto truncated = original;
    truncated.pop_back();
    write_file(key, truncated);
    rejects([&] { testing::load_keyfile(key, policy); }, "truncated keyfile accepted");
    auto extra = original;
    extra.push_back(0);
    write_file(key, extra);
    rejects([&] { testing::load_keyfile(key, policy); }, "trailing keyfile bytes accepted");
    write_file(key, original);

    const auto pad = dir.path / "pad.bin";
    const auto wrong_pad = dir.path / "wrong-pad.bin";
    write_file(pad, Bytes(8193, 0x7b));
    write_file(wrong_pad, Bytes(8193, 0x7c));
    const auto padded = dir.path / "padded.dpkey";
    testing::create_keyfile(padded, policy, pad);
    const auto padded_probe = testing::load_keyfile(padded, policy, pad).mac(bytes("probe"));
    check(padded_probe == testing::load_keyfile(padded, policy, pad).mac(bytes("probe")), "pad round trip failed");
    rejects([&] { testing::load_keyfile(padded, policy); }, "required pad was optional");
    rejects([&] { testing::load_keyfile(padded, policy, wrong_pad); }, "wrong pad accepted");
    rejects([&] { testing::load_keyfile(key, policy, pad); }, "unexpected pad silently ignored");
    write_file(wrong_pad, Bytes(8192, 0x7c));
    rejects([&] { testing::create_keyfile(dir.path / "short-pad.dpkey", policy, wrong_pad); }, "short pad accepted");
    rejects([&] { create_keyfile(dir.path / "prod-short-pad.dpkey", pad); }, "production accepted pad under 1GiB");
    check(!std::filesystem::exists(dir.path / "prod-short-pad.dpkey"), "invalid pad left output file");
    write_file(pad, Bytes(8194, 0x7b));
    rejects([&] { testing::load_keyfile(padded, policy, pad); }, "changed pad length accepted");

    // Prove that hashing consumes later streaming chunks, not only a prefix.
    const auto streamed_pad = dir.path / "streamed-pad.bin";
    const auto streamed_key = dir.path / "streamed.dpkey";
    Bytes pad_data(2 * 65536 + 17, 0x51);
    write_file(streamed_pad, pad_data);
    testing::create_keyfile(streamed_key, policy, streamed_pad);
    check(testing::load_keyfile(streamed_key, policy, streamed_pad).mac({}).size() == 32,
          "multi-chunk pad round trip failed");
    pad_data.back() ^= 1;
    write_file(streamed_pad, pad_data);
    rejects([&] { testing::load_keyfile(streamed_key, policy, streamed_pad); }, "pad bytes beyond streaming boundary were ignored");
}

void test_production_keyfile() {
    TempDir dir;
    const auto path = dir.path / "production.dpkey";
    create_keyfile(path);
    check(std::filesystem::file_size(path) == keyfile_header_bytes + 96, "production header is not 128MiB");
    const auto first = load_keyfile(path).mac(bytes("production round trip"));
    check(first == load_keyfile(path).mac(bytes("production round trip")), "production keyfile failed");
}
} // namespace

int main() {
    try {
        test_streams();
        test_authentication();
        test_keyfiles();
        test_production_keyfile();
        std::cout << "crypto tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "crypto test failure: " << error.what() << '\n';
        return 1;
    }
}
