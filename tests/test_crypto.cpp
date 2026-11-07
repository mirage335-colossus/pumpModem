#include "datapump/crypto.hpp"
#include "datapump/execution.hpp"
#include "fixture_keyfile.hpp"
#include "v4_crypto_vectors.hpp"
#include <openssl/evp.h>
#include <cmath>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

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

Crypto independent_key(std::uint8_t serpent_first=128,std::uint8_t aes_first=0) {
    Bytes s(64),a(64);for(std::size_t i=0;i<64;++i){s[i]=static_cast<std::uint8_t>(128+i);a[i]=static_cast<std::uint8_t>(i);}
    s[0]=serpent_first;a[0]=aes_first;return Crypto::from_independent_roots(s,a);
}
Bytes xor_bytes(Bytes first,std::span<const std::uint8_t> second) {
    check(first.size()==second.size(),"test XOR width mismatch");
    for(std::size_t i=0;i<first.size();++i)first[i]^=second[i];
    return first;
}
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
    const auto confidential=independent_key();
    auto encrypted = confidential.xor_data(plain, 1720000000, 19);
    check(plain != encrypted, "encryption did not alter data");
    check(plain == confidential.xor_data(encrypted, 1720000000, 19), "CTR round trip failed");
    check(confidential.xor_data({}, 0).empty(), "empty stream failed");
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

void test_cover_domains() {
    const Crypto key(Bytes(32,0x6a));
    std::vector<Bytes> all;
    for(const auto domain:{StreamDomain::Payload,StreamDomain::Preamble,StreamDomain::Suppression,
        StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2,
        StreamDomain::CoverPreambleV1,StreamDomain::CoverSuppressionV1,StreamDomain::CoverWaitV1})
        for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Scrambler,StreamPurpose::Dsss,StreamPurpose::Fhss}) {
            const auto bytes=key.stream(purpose,1800000321,0,1024,domain);
            check(std::find(all.begin(),all.end(),bytes)==all.end(),"cover domain or purpose reused old stream bytes");
            const auto part=key.stream(purpose,1800000321,509,131,domain);
            check(std::equal(part.begin(),part.end(),bytes.begin()+509),"cover stream restarted across a cache/block boundary");
            all.push_back(bytes);
        }
}

void test_utc_hopping_domain() {
    const Crypto key(Bytes(32,0x6a));
    const auto bytes=key.stream(StreamPurpose::Fhss,0,0,2048,StreamDomain::FhssUtcV3);
    for(const auto old:{StreamDomain::Payload,StreamDomain::FakeFhssV1,StreamDomain::FhssCycleV2})
        check(bytes!=key.stream(StreamPurpose::Fhss,0,0,2048,old),"UTC hopping reused an earlier stream domain");
    const auto part=key.stream(StreamPurpose::Fhss,0,1019,513,StreamDomain::FhssUtcV3);
    check(std::equal(part.begin(),part.end(),bytes.begin()+1019),"UTC hopping stream restarted at a block boundary");
    for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Scrambler,StreamPurpose::Dsss})
        rejects([&]{key.stream(purpose,0,0,1,StreamDomain::FhssUtcV3);},"UTC hopping accepted another stream purpose");
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

void test_fresh_fhss_domain() {
    Bytes master(32);for(unsigned i=0;i<32;++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto key(master);constexpr auto domain=StreamDomain::FhssCycleV2;
    constexpr std::uint64_t epoch=1720000000;
    // Independent Python hashlib/hmac HKDF + openssl enc AES-256-CTR,
    // high counter ASCII fh-cyc02, low counter byte_offset/16.
    const auto first=from_hex("55ab83752a6e89b7fc0d36ac4bf8b7d92c326bef53d26224e5d0c41144f75233");
    const auto second=from_hex("7940e22cafc1939c3cca246dfd7d655534dbf62d79aba58b3433cf5aa6efdc40");
    check(key.stream(StreamPurpose::Fhss,epoch,0,32,domain)==first&&
          key.stream(StreamPurpose::Fhss,epoch,1024,32,domain)==second,"fresh FHSS format vectors changed");
    check(key.stream(StreamPurpose::Fhss,epoch,1025,31,domain)==Bytes(second.begin()+1,second.end()),
          "fresh FHSS unaligned addressing changed");
    check(first!=key.stream(StreamPurpose::Fhss,epoch+1,0,32,domain)&&
          first!=Crypto(Bytes(32,7)).stream(StreamPurpose::Fhss,epoch,0,32,domain),"FHSS key/epoch separation lost");
    for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler})
        for(auto count:{0U,32U})rejects([&]{key.stream(purpose,epoch,0,count,domain);},"FHSS domain accepted wrong purpose");
    for(auto other:{StreamDomain::Payload,StreamDomain::Preamble,StreamDomain::Suppression,
                   StreamDomain::PatternZeroV2,StreamDomain::PatternOneV2,StreamDomain::FakeFhssV1})
        check(first!=key.stream(StreamPurpose::Fhss,epoch,0,32,other),"FHSS domain reused old addresses");
    const auto last=std::numeric_limits<std::uint64_t>::max();
    check(key.stream(StreamPurpose::Fhss,epoch,last-1023,1024,domain).size()==1024,"last FHSS region failed");
    rejects([&]{key.stream(StreamPurpose::Fhss,epoch,last-1023,1025,domain);},"FHSS stream wrapped");
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
    const auto confidential=independent_key();
    auto cipher = confidential.xor_data(first, 99);
    const auto stream = confidential.xor_data(Bytes(first.size()), 99);
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
    (void)testing::create_keyfile(key, policy,std::nullopt,fixture_keyfile::generation());
    check(std::filesystem::file_size(key) == 48 + policy.header_bytes + 2 + 7 + 128 + 16 + 32, "wrong keyfile length");
    const auto original = read_file(key);
    const auto probe = testing::load_keyfile(key, policy).stream(StreamPurpose::Data, 50, 5, 128);
    check(probe == testing::load_keyfile(key, policy).stream(StreamPurpose::Data, 50, 5, 128), "keyfile round trip failed");
    rejects([&] { (void)testing::create_keyfile(key, policy,std::nullopt,fixture_keyfile::generation()); }, "keyfile overwrite accepted");
    check(read_file(key) == original, "overwrite changed existing keyfile");
    rejects([&] { load_keyfile(key); }, "production loader accepted reduced header");
#ifndef _WIN32
    const auto permissions = std::filesystem::status(key).permissions();
    check((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none, "keyfile permissions are too broad");
    const auto link = dir.path / "link.dpkey";
    std::filesystem::create_symlink(key, link);
    rejects([&] { (void)testing::create_keyfile(link, policy,std::nullopt,fixture_keyfile::generation()); }, "symlink keyfile overwrite accepted");
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
    (void)testing::create_keyfile(padded, policy, pad,fixture_keyfile::generation());
    const auto padded_probe = testing::load_keyfile(padded, policy, pad).mac(bytes("probe"));
    check(padded_probe == testing::load_keyfile(padded, policy, pad).mac(bytes("probe")), "pad round trip failed");
    rejects([&] { testing::load_keyfile(padded, policy); }, "required pad was optional");
    rejects([&] { testing::load_keyfile(padded, policy, wrong_pad); }, "wrong pad accepted");
    rejects([&] { testing::load_keyfile(key, policy, pad); }, "unexpected pad silently ignored");
    write_file(wrong_pad, Bytes(8192, 0x7c));
    rejects([&] { (void)testing::create_keyfile(dir.path / "short-pad.dpkey", policy, wrong_pad,fixture_keyfile::generation()); }, "short pad accepted");
    rejects([&] { (void)create_keyfile(dir.path / "prod-short-pad.dpkey", pad,fixture_keyfile::generation()); }, "production accepted pad under 1GiB");
    check(!std::filesystem::exists(dir.path / "prod-short-pad.dpkey"), "invalid pad left output file");
    write_file(pad, Bytes(8194, 0x7b));
    rejects([&] { testing::load_keyfile(padded, policy, pad); }, "changed pad length accepted");

    // Prove that hashing consumes later streaming chunks, not only a prefix.
    const auto streamed_pad = dir.path / "streamed-pad.bin";
    const auto streamed_key = dir.path / "streamed.dpkey";
    Bytes pad_data(2 * 65536 + 17, 0x51);
    write_file(streamed_pad, pad_data);
    (void)testing::create_keyfile(streamed_key, policy, streamed_pad,fixture_keyfile::generation());
    check(testing::load_keyfile(streamed_key, policy, streamed_pad).mac({}).size() == 32,
          "multi-chunk pad round trip failed");
    pad_data.back() ^= 1;
    write_file(streamed_pad, pad_data);
    rejects([&] { testing::load_keyfile(streamed_key, policy, streamed_pad); }, "pad bytes beyond streaming boundary were ignored");
}

void test_production_keyfile() {
    TempDir dir;
    const auto path = dir.path / "production.dpkey";
    const auto started=std::chrono::steady_clock::now();
    const auto generated=create_keyfile(path,std::nullopt,fixture_keyfile::generation());
    check(generated.audio_used && generated.warning.empty(),"production keyfile lost audio provenance");
    check(std::chrono::steady_clock::now()-started>=std::chrono::seconds(1),"keyfile entropy collections were not separated by one second");
    check(std::filesystem::file_size(path) == keyfile_header_bytes + 233, "production v4 header/entry dimensions changed");
    std::ifstream input(path,std::ios::binary);std::array<char,8> magic{};input.read(magic.data(),magic.size());
    check(std::string_view(magic.data(),magic.size())=="DPMKEY04","production still writes an AES-only legacy keyfile");
    const auto first = load_keyfile(path).mac(bytes("production round trip"));
    check(first == load_keyfile(path).mac(bytes("production round trip")), "production keyfile failed");
}

void test_confidentiality() {
    using namespace v4_vectors;
    const auto key=independent_key(),changed_s=independent_key(129),changed_a=independent_key(128,1);
    const Crypto waveform(Bytes(32));
    constexpr std::uint64_t epoch=1720000000,end=std::numeric_limits<std::uint64_t>::max();
    const auto all=key.confidentiality_stream(epoch,0,2048);
    // Independent Python hashlib/HMAC, libgcrypt Serpent and cryptography AES;
    // no DataPump implementation is used to generate these frozen components.
    check(Bytes(all.begin(),all.begin()+64)==from_hex(streams_0_64_cascade),"v4 stream KAT mismatch");
    check(xor_bytes(from_hex(streams_0_64_aes),from_hex(streams_0_64_serpent))==from_hex(streams_0_64_cascade),"independent stream components disagree");
    check(key.confidentiality_stream(epoch,1,31)==from_hex(streams_1_31_cascade) &&
          key.confidentiality_stream(epoch,15,34)==from_hex(streams_15_34_cascade) &&
          key.confidentiality_stream(epoch,39,24)==from_hex(streams_39_24_cascade),"v4 unaligned stream KAT mismatch");
    check(key.confidentiality_stream(epoch,end-31,32)==from_hex(streams_18446744073709551584_32_cascade),"v4 final stream KAT mismatch");
    // Changing one lane must leave the independently known opposite component
    // unchanged; these checks also catch accidentally hashing both roots.
    check(xor_bytes(changed_s.confidentiality_stream(epoch,0,32),from_hex(lane_variations_serpent_changed_stream_aes))==from_hex(lane_variations_serpent_changed_stream_serpent),"Serpent lane changed or cross-fed AES stream");
    check(xor_bytes(changed_a.confidentiality_stream(epoch,0,32),from_hex(lane_variations_aes_changed_stream_serpent))==from_hex(lane_variations_aes_changed_stream_aes),"AES lane changed or cross-fed Serpent stream");
    check(key.mac({})==changed_s.mac({}),"Serpent root changed waveform MAC");
    const auto expected_waveform=Crypto(std::span<const std::uint8_t>(from_hex(aes_root)).first(32));
    for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss})
        check(key.stream(purpose,epoch,0,1024)==changed_s.stream(purpose,epoch,0,1024) &&
              key.stream(purpose,epoch,0,1024)==expected_waveform.stream(purpose,epoch,0,1024),"Serpent root changed AES waveform streams");
    for(auto offset:{0U,1U,15U,16U,17U,255U,511U}) {
        const auto part=key.confidentiality_stream(epoch,offset,127);
        check(std::equal(part.begin(),part.end(),all.begin()+offset),"cascade random access changed across blocks");
    }
    check(all!=key.confidentiality_stream(epoch+1,0,all.size()),"cascade epochs overlap");
    check(key.confidentiality_stream(epoch,end,0).empty(),"empty cascade stream failed");
    rejects([&]{key.confidentiality_stream(epoch,end,2);},"cascade offset wrapped");
    const auto child=key.confidentiality_child(from_hex(child_context));
    const auto expected_child=Crypto::from_independent_roots(from_hex(child_serpent_root),from_hex(child_aes_root));
    check(child.confidentiality_stream(epoch,0,64)==expected_child.confidentiality_stream(epoch,0,64) && child.mac({})==expected_child.mac({}),"independent child root vector mismatch");
    const auto sch=changed_s.confidentiality_child(from_hex(child_context));
    const auto ach=changed_a.confidentiality_child(from_hex(child_context));
    const auto sch_expected=Crypto::from_independent_roots(from_hex(lane_variations_serpent_changed_child_serpent_root),from_hex(lane_variations_serpent_changed_child_aes_root));
    const auto ach_expected=Crypto::from_independent_roots(from_hex(lane_variations_aes_changed_child_serpent_root),from_hex(lane_variations_aes_changed_child_aes_root));
    check(sch.confidentiality_stream(epoch,0,64)==sch_expected.confidentiality_stream(epoch,0,64) && sch.mac({})==child.mac({}),"child lane cross-feed from Serpent");
    check(ach.confidentiality_stream(epoch,0,64)==ach_expected.confidentiality_stream(epoch,0,64),"child lane cross-feed from AES");
    check(child.confidentiality_stream(epoch,0,64)!=key.confidentiality_child(bytes("other")).confidentiality_stream(epoch,0,64),"child contexts overlap");
    const auto context=from_hex(cbc_context),salt=from_hex(cbc_salt),iv=from_hex(cbc_iv),plain=from_hex(cbc_plaintext);
    const auto encrypted=key.encrypt_confidentiality_cbc(plain,context,salt,iv);
    check(encrypted==from_hex(cbc_ciphertext) && key.decrypt_confidentiality_cbc(encrypted,context,salt,iv)==plain,"v4 independent CBC vector mismatch");
    check(changed_s.encrypt_confidentiality_cbc(plain,context,salt,iv)==from_hex(lane_variations_serpent_changed_cbc_ciphertext) &&
          changed_a.encrypt_confidentiality_cbc(plain,context,salt,iv)==from_hex(lane_variations_aes_changed_cbc_ciphertext),"CBC lane-isolation vectors mismatch");
    // Independently remove the outer AES layer with its frozen key to inspect
    // the inner Serpent ciphertext, including the same inner under changed A.
    const auto inner=[](const Bytes& cipher,const Bytes& akey,const Bytes& wire_iv) {
        auto* ctx=EVP_CIPHER_CTX_new();check(ctx!=nullptr,"test AES context allocation failed");
        Bytes result(cipher.size()+16);int n=0,last=0;
        const bool ok=EVP_DecryptInit_ex(ctx,EVP_aes_256_cbc(),nullptr,akey.data(),wire_iv.data())==1 &&
            EVP_CIPHER_CTX_set_padding(ctx,0)==1 && EVP_DecryptUpdate(ctx,result.data(),&n,cipher.data(),static_cast<int>(cipher.size()))==1 &&
            EVP_DecryptFinal_ex(ctx,result.data()+n,&last)==1;
        EVP_CIPHER_CTX_free(ctx);check(ok,"test AES layer removal failed");result.resize(static_cast<std::size_t>(n+last));return result;
    };
    check(inner(encrypted,from_hex(cbc_aes_key),iv)==from_hex(cbc_inner),"Serpent CBC inner vector mismatch");
    check(inner(changed_a.encrypt_confidentiality_cbc(plain,context,salt,iv),from_hex(lane_variations_aes_changed_cbc_aes_key),iv)==from_hex(cbc_inner),"changing AES changed inner Serpent CBC");
    check(inner(changed_s.encrypt_confidentiality_cbc(plain,context,salt,iv),from_hex(cbc_aes_key),iv)==from_hex(lane_variations_serpent_changed_cbc_inner),"changing Serpent changed AES CBC derivation");
    rejects([&]{key.encrypt_confidentiality_cbc(Bytes(15),context,salt,iv);},"CBC accepted partial block");
    rejects([&]{key.encrypt_confidentiality_cbc(plain,context,Bytes(31),iv);},"CBC accepted wrong salt width");
    rejects([&]{key.decrypt_confidentiality_cbc(encrypted,context,salt,Bytes(15));},"CBC accepted wrong IV width");
    for(auto count:{0U,1U})rejects([&]{waveform.confidentiality_stream(epoch,0,count);},"waveform-only key acquired confidentiality stream");
    rejects([&]{waveform.confidentiality_child({});},"waveform-only key acquired confidentiality child");
    rejects([&]{waveform.prepare_confidentiality_cbc(context,salt);},"waveform-only key acquired CBC");
    rejects([&]{waveform.confidentiality_mac({});},"waveform-only key acquired confidentiality MAC");
    rejects([&]{Crypto::from_independent_roots(Bytes(63),Bytes(64));},"short Serpent root accepted");
    rejects([&]{Crypto::from_independent_roots(Bytes(64),Bytes(65));},"long AES root accepted");
    Crypto copied=key;Crypto assigned(Bytes(32,9));assigned=child;
    check(copied.confidentiality_stream(epoch,0,64)==key.confidentiality_stream(epoch,0,64) && assigned.confidentiality_stream(epoch,0,64)==child.confidentiality_stream(epoch,0,64),"copy lost independent roots");
}
void test_confidentiality_authentication_and_identity() {
    using namespace v4_vectors;
    const auto key=independent_key(),changed_s=independent_key(129),changed_a=independent_key(128,1);
    const auto message=from_hex(mac_message),tag=key.confidentiality_mac(message);
    check(tag==from_hex(mac_combined) && key.verify_confidentiality(message,tag),"dual MAC independent vector mismatch");
    check(xor_bytes(tag,from_hex(mac_aes))==from_hex(mac_serpent),"dual MAC component vector mismatch");
    check(xor_bytes(changed_s.confidentiality_mac(message),from_hex(lane_variations_serpent_changed_mac_aes))==from_hex(lane_variations_serpent_changed_mac_serpent),"Serpent auth lane cross-fed AES");
    check(xor_bytes(changed_a.confidentiality_mac(message),from_hex(lane_variations_aes_changed_mac_serpent))==from_hex(lane_variations_aes_changed_mac_aes),"AES auth lane cross-fed Serpent");
    check(!key.verify_confidentiality(message,from_hex(mac_aes)) && !key.verify_confidentiality(message,from_hex(mac_serpent)),"single known auth lane forged combined MAC");
    check(!changed_s.verify_confidentiality(message,tag) && !changed_a.verify_confidentiality(message,tag),"changed lane accepted old MAC");
    auto changed=message;changed[0]^=1;check(!key.verify_confidentiality(changed,tag),"modified message accepted");
    auto damaged=tag;damaged[0]^=1;check(!key.verify_confidentiality(message,damaged),"modified dual tag accepted");
    check(!key.verify_confidentiality(message,std::span(tag).first(31)),"truncated dual tag accepted");
    damaged.push_back(0);check(!key.verify_confidentiality(message,damaged),"oversized dual tag accepted");
    check(key.verify_confidentiality({},key.confidentiality_mac({})),"empty dual MAC failed");
    const auto context=from_hex(identity_context);
    const auto waveform=Crypto(std::span<const std::uint8_t>(from_hex(aes_root)).first(32));
    check(key.identity_fingerprint(context)==from_hex(identity_confidentiality) && waveform.identity_fingerprint(context)==from_hex(identity_waveform),"identity independent vectors mismatch");
    check(key.identity_fingerprint(context)!=changed_s.identity_fingerprint(context),"same AES different Serpent keys share identity");
    check(key.identity_fingerprint(context)==independent_key().identity_fingerprint(context),"equal key values have unequal identities");
    check(key.identity_fingerprint(context)!=waveform.identity_fingerprint(context),"waveform and dual identities overlap");
    check(key.identity_fingerprint(context)!=key.identity_fingerprint(bytes("other")),"identity contexts overlap");
}
void test_audio_entropy_failures() {
    TempDir dir;const testing::KeyfilePolicy policy{64,8193};unsigned index=0;
    const auto attempt=[&](KeyfileAudioSource source,bool fallback=false) {
        KeyfileGenerationOptions options;options.audio=std::move(source);options.allow_audio_fallback=fallback;
        return testing::create_keyfile(dir.path/std::to_string(index++),policy,std::nullopt,options);
    };
    rejects([&]{attempt({});},"missing audio accepted without explicit fallback");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{.125F};sink.add_samples(a,4000);});},"short audio accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,512> a{};sink.add_samples(a,4000);});},"constant audio accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{std::numeric_limits<float>::quiet_NaN()};sink.add_samples(a,4000);});},"NaN audio accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{1.1F};sink.add_samples(a,4000);});},"out-of-range audio accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{.125F};sink.add_samples(a,3999);});},"unsupported rate accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{.125F};sink.add_samples(a,4000);sink.add_samples(a,8000);});},"changed rate accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){std::vector<float> a(12000);for(std::size_t i=0;i<a.size();++i)a[i]=(i&1)?.125F:.25F;sink.add_samples(a,4000);});},"insufficient variation accepted");
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){std::vector<float> a(12000);for(std::size_t i=0;i<a.size();++i)a[i]=1.0F-static_cast<float>(i%32)*0.000001F;sink.add_samples(a,4000);});},"clipped audio accepted");
    // A provider cannot swallow an invalid sample and resume to conceal it.
    rejects([&]{attempt([](AudioEntropySink& sink,std::stop_token){const std::array<float,1> a{2.0F};try{sink.add_samples(a,4000);}catch(const Error&){};});},"swallowed sink failure accepted");
    const auto missing=attempt({},true);check(!missing.audio_used && !missing.warning.empty(),"missing audio downgrade was silent");
    const auto unusable=attempt([](AudioEntropySink&,std::stop_token){throw AudioEntropyUnavailable("test device unavailable");},true);
    check(!unusable.audio_used && unusable.warning.find("test device unavailable")!=std::string::npos,"typed unavailable downgrade lost warning");
    rejects([&]{attempt([](AudioEntropySink&,std::stop_token){throw Error("test internal conditioner failure");},true);},"internal error silently downgraded");
    std::stop_source cancellation;auto options=fixture_keyfile::generation();options.stop=cancellation.get_token();options.allow_audio_fallback=true;cancellation.request_stop();
    rejects([&]{(void)testing::create_keyfile(dir.path/"cancelled",policy,std::nullopt,options);},"pre-cancelled generation continued");
    check(!std::filesystem::exists(dir.path/"cancelled"),"pre-cancelled generation created output");
    std::stop_source during;options.stop=during.get_token();options.audio=[&](AudioEntropySink&,std::stop_token){during.request_stop();throw AudioEntropyUnavailable("cancelled capture");};
    rejects([&]{(void)testing::create_keyfile(dir.path/"cancelled-during",policy,std::nullopt,options);},"cancelled capture silently downgraded");
}

void test_shared_confidentiality_lifetime() {
    const Crypto legacy(Bytes(32,0x42));
    check(legacy.confidentiality_storage_id()==nullptr && legacy.confidentiality_storage_bytes()==0,
          "legacy waveform root allocated confidentiality storage");
    auto survivor=legacy;
    Bytes expected;
    const void* identity=nullptr;
    {
        const auto parent=independent_key().confidentiality_child(bytes("shared/lifetime"));
        identity=parent.confidentiality_storage_id();
        check(identity!=nullptr && parent.confidentiality_storage_bytes()>=128,
              "independent root has no accounted storage");
        expected=parent.confidentiality_stream(73,15,65);
        auto copied=parent;
        survivor=copied;
        check(copied.confidentiality_storage_id()==identity && survivor.confidentiality_storage_id()==identity,
              "copying duplicated immutable confidentiality material");
        copied=survivor; // Assignment between aliases must retain before release.
        copied=copied;
        copied=legacy;
        check(copied.confidentiality_storage_bytes()==0 && parent.confidentiality_stream(73,15,65)==expected,
              "replacing one copy changed another owner's root");
    }
    check(survivor.confidentiality_storage_id()==identity && survivor.confidentiality_stream(73,15,65)==expected,
          "confidentiality material expired with its original owner");
    std::atomic<bool> valid{true};
    std::array<execution::Task,4> workers;
    for(auto& worker:workers)worker=execution::Task([&]{
        for(unsigned i=0;i<64;++i){
            auto copied=survivor;auto assigned=legacy;assigned=copied;
            if(assigned.confidentiality_storage_id()!=identity || assigned.confidentiality_stream(73,15,65)!=expected)
                valid.store(false,std::memory_order_relaxed);
        }
    });
    for(auto& worker:workers)worker.join();
    check(valid.load(std::memory_order_relaxed),"concurrent value copies changed immutable root material");
}
void test_prepared_confidentiality_cbc() {
    static_assert(!std::is_copy_constructible_v<ConfidentialityCbc>);
    static_assert(!std::is_copy_assignable_v<ConfidentialityCbc>);
    const auto legacy=independent_key();
    const auto independent=legacy.confidentiality_child(bytes("prepared/independent-root"));
    const auto context_a=bytes("test/prepared/cbc/profile-a"),context_b=bytes("test/prepared/cbc/profile-b");
    const Bytes salt_a(32,0x39),salt_b(32,0x93);
    for(const auto* key:{&legacy,&independent}) {
        auto prepared_a=key->prepare_confidentiality_cbc(context_a,salt_a);
        auto prepared_b=key->prepare_confidentiality_cbc(context_b,salt_b);
        // The original one-shot path independently repeats full HKDF/key setup.
        // Compare different/repeated IVs and sizes while alternating directions
        // and two transfer contexts, so no previous CBC chain can leak through.
        for(const auto size:{16U,32U,176U,288U,1024U,4096U}) {
            Bytes plain(size);for(std::size_t i=0;i<plain.size();++i)plain[i]=static_cast<std::uint8_t>(i*73+size);
            for(const auto iv_id:{0U,1U,255U,0U,1U}) {
                Bytes iv(16);for(std::size_t i=0;i<iv.size();++i)iv[i]=static_cast<std::uint8_t>(iv_id+i*29);
                const auto expected_a=key->encrypt_confidentiality_cbc(plain,context_a,salt_a,iv);
                const auto expected_b=key->encrypt_confidentiality_cbc(plain,context_b,salt_b,iv);
                check(expected_a!=expected_b,"prepared CBC transfer contexts overlap");
                check(prepared_a->decrypt(expected_a,iv)==key->decrypt_confidentiality_cbc(expected_a,context_a,salt_a,iv),"prepared CBC decrypt-before-encrypt differs from one-shot");
                check(prepared_b->encrypt(plain,iv)==expected_b,"prepared CBC other transfer differs from one-shot");
                check(prepared_a->encrypt(plain,iv)==expected_a,"prepared CBC repeated/interleaved encryption differs from one-shot");
                check(prepared_b->decrypt(expected_b,iv)==plain,"prepared CBC other transfer decryption mismatch");
                check(prepared_a->decrypt(expected_a,iv)==plain,"prepared CBC interleaved decryption mismatch");
                check(prepared_a->encrypt(plain,iv)==expected_a,"prepared CBC repeated IV changes ciphertext");
            }
        }
        const Bytes plain(32,0x61),iv(16,0x43);
        const auto expected=key->encrypt_confidentiality_cbc(plain,context_a,salt_a,iv);
        rejects([&]{prepared_a->encrypt(Bytes(15),iv);},"prepared CBC accepted partial block");
        rejects([&]{prepared_a->decrypt(expected,Bytes(15));},"prepared CBC accepted short IV");
        rejects([&]{prepared_a->encrypt({},iv);},"prepared CBC accepted empty input");
        check(prepared_a->encrypt(plain,iv)==expected && prepared_a->decrypt(expected,iv)==plain,"invalid call damaged prepared CBC state");
    }
    rejects([&]{legacy.prepare_confidentiality_cbc(context_a,Bytes(31));},"prepared CBC accepted short salt");
    const auto owned=[&]{const auto temporary=independent_key();return temporary.prepare_confidentiality_cbc(context_a,salt_a);}();
    const Bytes plain(32,0x12),iv(16,0x34);
    check(owned->encrypt(plain,iv)==legacy.encrypt_confidentiality_cbc(plain,context_a,salt_a,iv),"prepared CBC retained expired Crypto reference");
}

} // namespace

int main() {
    try {
        test_streams();test_cover_domains();test_utc_hopping_domain();
        test_preamble_domain();
        test_suppression_domain();
        test_private_pattern_domains();
        test_interleaved_dsss_domains();
        test_fresh_fhss_domain();
        test_authentication();
        test_confidentiality();
        test_confidentiality_authentication_and_identity();
        test_audio_entropy_failures();
        test_shared_confidentiality_lifetime();
        test_prepared_confidentiality_cbc();
        test_keyfiles();
        test_production_keyfile();
        std::cout << "crypto tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "crypto test failure: " << error.what() << '\n';
        return 1;
    }
}
