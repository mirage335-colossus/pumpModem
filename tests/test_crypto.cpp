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

void test_preamble_domain() {
    Bytes master(32);
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto crypto(master);
    constexpr std::uint64_t epoch=1720000000;
    constexpr auto preamble=StreamDomain::Preamble;
    constexpr auto final=std::numeric_limits<std::uint64_t>::max();
    // Independent Python hashlib/hmac HKDF plus openssl enc AES-256-CTR:
    // same frozen Data epoch key as test_streams, with only the high counter
    // bytes replaced by ASCII "preamble". The final vector fixes all 64
    // low counter bits, including the largest supported byte offset.
    const auto beginning=from_hex("8338c986304db443a52d1127070e6003350b6900834a4384f7146632ddd3ddc3"
                                  "22fbc085226fc6d659a8481cc9ad1c0d882899c85afbd6dba8582974ae27092b");
    check(crypto.stream(StreamPurpose::Data,epoch,0,beginning.size(),preamble)==beginning,
          "preamble counter format vector mismatch");
    check(crypto.stream(StreamPurpose::Data,epoch,final-15,16,preamble)==
          from_hex("73a09cfa4b394d4908a49a1fcf838baa"),"final preamble counter vector mismatch");
    const auto data=crypto.stream(StreamPurpose::Data,epoch,0,1024,preamble);
    for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
        const auto payload=crypto.stream(purpose,epoch,0,1024);
        check(payload==crypto.stream(purpose,epoch,0,1024,StreamDomain::Payload),
              "explicit payload domain changed existing stream bytes");
        const auto all=crypto.stream(purpose,epoch,0,1024,preamble);
        check(all!=payload,"preamble and payload counter domains overlap");
        check(all!=crypto.stream(purpose,epoch+1,0,1024,preamble),"preamble epoch keys overlap");
        if(purpose!=StreamPurpose::Data)check(all!=data,"preamble purpose keys overlap");
        for(auto offset:{0U,1U,15U,16U,17U,255U,511U}) {
            const auto slice=crypto.stream(purpose,epoch,offset,127,preamble);
            check(std::equal(slice.begin(),slice.end(),all.begin()+offset),"preamble random access mismatch");
        }
        Bytes chunks;
        for(std::size_t offset=0,index=0;offset<all.size();++index) {
            constexpr std::array<std::size_t,5> sizes{1,15,17,127,513};
            const auto count=std::min(sizes[index%sizes.size()],all.size()-offset);
            const auto slice=crypto.stream(purpose,epoch,offset,count,preamble);
            chunks.insert(chunks.end(),slice.begin(),slice.end());offset+=count;
        }
        check(chunks==all,"preamble chunk boundaries changed bytes");
        const auto tail=crypto.stream(purpose,epoch,final-30,31,preamble);
        check(tail!=crypto.stream(purpose,epoch,final-30,31),"final offsets crossed preamble/payload domains");
        for(std::uint64_t skip:{0ULL,1ULL,15ULL,16ULL,30ULL}) {
            const auto slice=crypto.stream(purpose,epoch,final-30+skip,static_cast<std::size_t>(31-skip),preamble);
            check(std::equal(slice.begin(),slice.end(),tail.begin()+static_cast<std::ptrdiff_t>(skip)),
                  "final preamble byte offset lost random-access consistency");
        }
    }
    check(crypto.stream(StreamPurpose::Data,epoch,final,0,preamble).empty(),"empty preamble stream failed");
    rejects([&]{crypto.stream(StreamPurpose::Data,epoch,final,2,preamble);},"preamble byte offset wrapped");
    rejects([&]{crypto.stream(StreamPurpose::Data,epoch,final-15,17,preamble);},"preamble final block overflow accepted");
    rejects([&]{crypto.stream(StreamPurpose::Data,epoch,0,1,static_cast<StreamDomain>(99));},"invalid stream domain accepted");
    rejects([&]{crypto.stream(StreamPurpose::Data,epoch,0,0,static_cast<StreamDomain>(99));},"empty stream ignored invalid domain");
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

void test_suppression_domain() {
    Bytes master(32);
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto crypto(master);
    constexpr std::uint64_t epoch=1720000000;
    constexpr auto domain=StreamDomain::Suppression;
    // Independent Python hashlib/hmac HKDF plus openssl AES-256-CTR, using
    // the existing Data epoch key and high counter bytes ASCII "suppress".
    const auto vector=from_hex("43e0f33002feb8ada0341c2489c1a27a43457c9cb855eaf2ec443fa528abfc3b"
                               "59ea5b82942cd8fb2d950e2714fa77b17989eedb47f45f47cfc2f72857c71432");
    check(crypto.stream(StreamPurpose::Data,epoch,0,vector.size(),domain)==vector,
          "suppression counter format vector mismatch");
    for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
        const auto all=crypto.stream(purpose,epoch,0,1024,domain);
        check(all!=crypto.stream(purpose,epoch,0,1024) &&
              all!=crypto.stream(purpose,epoch,0,1024,StreamDomain::Preamble),
              "suppression must not reuse payload or preamble keystream bytes");
        check(all!=crypto.stream(purpose,epoch+1,0,1024,domain),"suppression epoch keys overlap");
        for(auto offset:{0U,1U,15U,16U,17U,255U,511U}) {
            const auto slice=crypto.stream(purpose,epoch,offset,127,domain);
            check(std::equal(slice.begin(),slice.end(),all.begin()+offset),"suppression random access mismatch");
        }
        constexpr auto final=std::numeric_limits<std::uint64_t>::max();
        const auto tail=crypto.stream(purpose,epoch,final-30,31,domain);
        const auto last=crypto.stream(purpose,epoch,final,1,domain);
        check(tail.back()==last.front(),"suppression final byte lost random access");
        rejects([&]{crypto.stream(purpose,epoch,final,2,domain);},"suppression byte offset wrapped");
    }
}

void test_private_pattern_domains() {
    Bytes master(32);
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto crypto(master);
    constexpr std::uint64_t epoch=1720000000;
    // Independent Python hashlib/hmac HKDF plus openssl AES-256-CTR;
    // fixed high counter bytes are ASCII pat-v2-0 and pat-v2-1.
    check(crypto.stream(StreamPurpose::Scrambler,epoch,0,64,StreamDomain::PatternZeroV2)==
        from_hex("51fa6125c11f500aca0dd70b71ac682cee81002fcfc6f7603ea963b0296a97ea"
                 "19ca8d3d301b735545fb6f749ace1d944746274b15f2fd6b5851fd893337e323"),"private candidate counter vector mismatch");
    check(crypto.stream(StreamPurpose::Scrambler,epoch,0,64,StreamDomain::PatternOneV2)==
        from_hex("496e78c43f012621dee29e57d81300b8acb974dc637b46b52ec152555e41d49e"
                 "8d96ae9de3ba4ab33b14b38c20b31e9df93b7cbaeb06f6da9fc287f02157a162"),"private candidate counter vector mismatch");
    check(crypto.stream(StreamPurpose::Dsss,epoch,0,64,StreamDomain::PatternZeroV2)==
        from_hex("4d9d774023174363c104e0342b2ae9914f8e28047d10b6512e2a8a39fbe69b7d"
                 "e0e55f019585913a8ac83f8af22d88d843fb3244392444c9e80c4104b864aeb6"),"private candidate counter vector mismatch");
    check(crypto.stream(StreamPurpose::Dsss,epoch,0,64,StreamDomain::PatternOneV2)==
        from_hex("6d8b2552e7e0cf0e4ea2b5ff704690d7d09fd5f2a436fcba9447874d48b5c4b6"
                 "4f54cbdabd94421d689aba72020ab6f1e8def17e8a05efb975ef53f9e9ea8a99"),"private candidate counter vector mismatch");
    for(auto purpose:{StreamPurpose::Scrambler,StreamPurpose::Dsss})
    for(auto domain:{StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2}) {
        const auto all=crypto.stream(purpose,epoch,0,1024,domain);
        for(auto other:{StreamDomain::Payload,StreamDomain::Preamble,StreamDomain::Suppression,
                        StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2})
            if(other!=domain)check(all!=crypto.stream(purpose,epoch,0,1024,other),
                                  "private candidate domain overlaps another stream");
        check(all!=crypto.stream(purpose,epoch+1,0,1024,domain),"private candidate epoch reused");
        for(auto offset:{0U,1U,15U,16U,17U,255U,511U}) {
            const auto part=crypto.stream(purpose,epoch,offset,127,domain);
            check(std::equal(part.begin(),part.end(),all.begin()+offset),"private candidate seek mismatch");
        }
        constexpr auto final=std::numeric_limits<std::uint64_t>::max();
        const auto tail=crypto.stream(purpose,epoch,final-30,31,domain);
        check(tail.back()==crypto.stream(purpose,epoch,final,1,domain).front(),"private candidate final seek mismatch");
        rejects([&]{crypto.stream(purpose,epoch,final,2,domain);},"private candidate byte offset wrapped");
    }
}

void test_interleaved_dsss_domains() {
    Bytes master(32);for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto crypto(master);
    constexpr std::array domains{StreamDomain::OuterDsss10RotationV2,StreamDomain::OuterDsss100RotationV2,
        StreamDomain::OuterDsss1000RotationV2,StreamDomain::OuterDsss10PermutationV2,
        StreamDomain::OuterDsss100PermutationV2,StreamDomain::OuterDsss1000PermutationV2};
    // Independent Python hashlib/hmac HKDF and openssl enc AES-256-CTR,
    // high counter pads dr2-0010/dr2-0100/dr2-1000 and dp2 equivalents.
    constexpr std::array vectors{
        "4437244a4f39abf92eb2b81c8dfcd8f5bf3622b6954608607a0e24af7005fb70",
        "3b1ce5c87fb12d048a3a3dffa9063eafe15b4d7f6324b7302049a4f11cb26bd8",
        "570bfab8c1f7fc01895eeef0bbe72c2d3bc4bce3703b2610157fb52eee98caea",
        "84c92df398776c7a5f87ca375dbf5e357f794e934d881b5029487108081b84ad",
        "2c2ee8aa19ea48777398b0c7292011b6f6374e76df1e289865bf9db6d539bc50",
        "4c1040d12f59a1b94c0bc5aff21fc63cce0d0b2b5d00e149d88230406c848cd7"};
    constexpr std::uint64_t epoch=1720000000;
    for(std::size_t i=0;i<domains.size();++i) {
        const auto domain=domains[i];
        check(crypto.stream(StreamPurpose::Dsss,epoch,0,32,domain)==from_hex(vectors[i]),
              "interleaved DSSS counter vector mismatch");
        const auto all=crypto.stream(StreamPurpose::Dsss,epoch,0,1024,domain);
        for(const auto other:domains)if(other!=domain)
            check(all!=crypto.stream(StreamPurpose::Dsss,epoch,0,1024,other),"V2 DSSS domains alias");
        for(const auto other:{StreamDomain::Payload,StreamDomain::Preamble,StreamDomain::Suppression,
            StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2,StreamDomain::OuterDsss10V1,
            StreamDomain::OuterDsss100V1,StreamDomain::OuterDsss1000V1})
            check(all!=crypto.stream(StreamPurpose::Dsss,epoch,0,1024,other),"V2 DSSS aliases existing stream");
        for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
            rejects([&]{crypto.stream(purpose,epoch,0,32,domain);},"V2 DSSS accepted another purpose key");
            rejects([&]{crypto.stream(purpose,epoch,0,0,domain);},"empty V2 DSSS accepted another purpose key");
        }
        for(const auto offset:{1U,15U,16U,31U,255U,511U}) {
            const auto part=crypto.stream(StreamPurpose::Dsss,epoch,offset,127,domain);
            check(std::equal(part.begin(),part.end(),all.begin()+offset),"V2 DSSS random seek mismatch");
        }
        check(all!=crypto.stream(StreamPurpose::Dsss,epoch+1,0,1024,domain),"V2 DSSS epoch aliases");
        constexpr auto final=std::numeric_limits<std::uint64_t>::max();
        rejects([&]{crypto.stream(StreamPurpose::Dsss,epoch,final,2,domain);},"V2 DSSS byte offset wraps");
        check(crypto.stream(StreamPurpose::Dsss,epoch,final-31,32,domain).back()==
              crypto.stream(StreamPurpose::Dsss,epoch,final,1,domain).front(),"V2 DSSS final seek mismatch");
    }
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
        test_preamble_domain();
        test_suppression_domain();
        test_private_pattern_domains();
        test_interleaved_dsss_domains();
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
