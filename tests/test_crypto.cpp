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
    // Independently generated with Python hashlib/HMAC HKDF and OpenSSL enc
    // AES-256-CTR, matching the public fixture recipe of the Data vector.
    check(crypto.stream(StreamPurpose::Dsss,1720000000,0,64)==from_hex(
        "0f916d2cc41666a80289468973791d940e1a93b5c60c7b57153ea3be0be88a8b"
        "eeec793107dce392a7318418af7bbf960f80e2e4a95e6355de0c8df2d1e8dcf2"),
        "DSSS purpose slot/label vector mismatch");
    check(crypto.stream(StreamPurpose::Scrambler,1720000000,0,64)==from_hex(
        "b482d23e9f728bee8dce8ac2bd9c87fd2a7103a03050d3808d16e1e2f8fdc8f4"
        "ce6f6cf74cbfb4106885370f27cb244254e22e1b482456120a69865890b33177"),
        "Scrambler purpose slot/label vector mismatch");
    for (auto offset : {0U, 1U, 15U, 16U, 17U, 255U, 511U}) {
        const auto slice = crypto.stream(StreamPurpose::Data, 1720000000, offset, 127);
        check(std::equal(slice.begin(), slice.end(), all.begin() + offset), "random access stream mismatch");
    }
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
    const auto first=bytes("timestamp|metadata|message one");
    const auto tag=crypto.mac(first);
    check(tag==from_hex("5015234b16cbd11766fb59c5221930c5d32a883975576b716ab3af4b6105869d"),
          "HMAC format vector mismatch");
    check(tag.size()==32 && crypto.mac(first)==tag,"MAC is not fixed-width/deterministic");
    check(crypto.mac(bytes("timestamp|metadata|message two"))!=tag,"changed message retained MAC");
    check(Crypto(Bytes(32,0x73)).mac(first)!=tag,"changed key retained MAC");
    check(crypto.mac({}).size()==32,"empty-message MAC failed");
}

void test_factory_keys() {
    const std::array<std::uint8_t,64> public_root{};
    const auto amateur=Crypto::amateur();
    const auto explicit_public=Crypto::from_independent_roots(public_root,public_root);
    const auto message=bytes("factory key signature");
    check(amateur.stream(StreamPurpose::Dsss,1720000000,0,64)==
          explicit_public.stream(StreamPurpose::Dsss,1720000000,0,64),
          "Amateur waveform key differs from the public zero-root key");
    check(amateur.confidentiality_stream(1720000000,0,64)==
          explicit_public.confidentiality_stream(1720000000,0,64) &&
          amateur.confidentiality_mac(message)==explicit_public.confidentiality_mac(message),
          "Amateur confidentiality key differs from the public zero-root key");

    const auto first=Crypto::temporary(),second=Crypto::temporary();
    check(first.stream(StreamPurpose::Dsss,1720000000,0,64)!=
          second.stream(StreamPurpose::Dsss,1720000000,0,64),
          "fresh temporary keys produced the same waveform signature");
    check(first.confidentiality_stream(1720000000,0,64)!=
          second.confidentiality_stream(1720000000,0,64) &&
          first.confidentiality_mac(message)!=second.confidentiality_mac(message),
          "fresh temporary keys produced the same confidentiality signature");
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
    auto prepared=key.prepare_confidentiality_cbc(context,salt);
    const auto encrypted=prepared->encrypt(plain,iv);
    check(encrypted==from_hex(cbc_ciphertext) && prepared->decrypt(encrypted,iv)==plain,"v4 independent CBC vector mismatch");
    check(changed_s.prepare_confidentiality_cbc(context,salt)->encrypt(plain,iv)==from_hex(lane_variations_serpent_changed_cbc_ciphertext) &&
          changed_a.prepare_confidentiality_cbc(context,salt)->encrypt(plain,iv)==from_hex(lane_variations_aes_changed_cbc_ciphertext),"CBC lane-isolation vectors mismatch");
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
    check(inner(changed_a.prepare_confidentiality_cbc(context,salt)->encrypt(plain,iv),from_hex(lane_variations_aes_changed_cbc_aes_key),iv)==from_hex(cbc_inner),"changing AES changed inner Serpent CBC");
    check(inner(changed_s.prepare_confidentiality_cbc(context,salt)->encrypt(plain,iv),from_hex(cbc_aes_key),iv)==from_hex(lane_variations_serpent_changed_cbc_inner),"changing Serpent changed AES CBC derivation");
    rejects([&]{prepared->encrypt(Bytes(15),iv);},"CBC accepted partial block");
    rejects([&]{key.prepare_confidentiality_cbc(context,Bytes(31));},"CBC accepted wrong salt width");
    rejects([&]{prepared->decrypt(encrypted,Bytes(15));},"CBC accepted wrong IV width");
    for(auto count:{0U,1U})rejects([&]{waveform.confidentiality_stream(epoch,0,count);},"waveform-only key acquired confidentiality stream");
    rejects([&]{waveform.confidentiality_child({});},"waveform-only key acquired confidentiality child");
    rejects([&]{waveform.prepare_confidentiality_cbc(context,salt);},"waveform-only key acquired CBC");
    rejects([&]{waveform.confidentiality_mac({});},"waveform-only key acquired confidentiality MAC");
    rejects([&]{Crypto::from_independent_roots(Bytes(63),Bytes(64));},"short Serpent root accepted");
    rejects([&]{Crypto::from_independent_roots(Bytes(64),Bytes(65));},"long AES root accepted");
    Crypto copied=key;Crypto assigned(Bytes(32,9));assigned=child;
    check(copied.confidentiality_stream(epoch,0,64)==key.confidentiality_stream(epoch,0,64) && assigned.confidentiality_stream(epoch,0,64)==child.confidentiality_stream(epoch,0,64),"copy lost independent roots");
}
void test_confidentiality_authentication() {
    using namespace v4_vectors;
    const auto key=independent_key(),changed_s=independent_key(129),changed_a=independent_key(128,1);
    const auto message=from_hex(mac_message),tag=key.confidentiality_mac(message);
    check(tag==from_hex(mac_combined),"dual MAC independent vector mismatch");
    check(xor_bytes(tag,from_hex(mac_aes))==from_hex(mac_serpent),"dual MAC component vector mismatch");
    check(xor_bytes(changed_s.confidentiality_mac(message),from_hex(lane_variations_serpent_changed_mac_aes))==from_hex(lane_variations_serpent_changed_mac_serpent),"Serpent auth lane cross-fed AES");
    check(xor_bytes(changed_a.confidentiality_mac(message),from_hex(lane_variations_aes_changed_mac_serpent))==from_hex(lane_variations_aes_changed_mac_aes),"AES auth lane cross-fed Serpent");
    check(tag!=from_hex(mac_aes) && tag!=from_hex(mac_serpent),"combined MAC equals a single lane");
    check(changed_s.confidentiality_mac(message)!=tag && changed_a.confidentiality_mac(message)!=tag,"changed lane retained old MAC");
    auto changed=message;changed[0]^=1;check(key.confidentiality_mac(changed)!=tag,"modified message retained MAC");
    check(key.confidentiality_mac({}).size()==32,"empty dual MAC failed");
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
    const Crypto waveform(Bytes(32,0x42));
    auto survivor=waveform;
    Bytes expected,expected_mac;
    {
        const auto parent=independent_key().confidentiality_child(bytes("shared/lifetime"));
        expected=parent.confidentiality_stream(73,15,65);
        expected_mac=parent.confidentiality_mac(bytes("lifetime"));
        auto copied=parent;survivor=copied;
        check(copied.confidentiality_stream(73,15,65)==expected &&
              survivor.confidentiality_mac(bytes("lifetime"))==expected_mac,"value copy lost confidentiality roots");
        copied=survivor; // Alias assignment must retain before releasing.
        copied=copied;
        check(copied.confidentiality_stream(73,15,65)==expected,"alias/self assignment changed roots");
        copied=waveform;
        rejects([&]{copied.confidentiality_stream(73,15,65);},"waveform replacement retained confidentiality");
        check(parent.confidentiality_stream(73,15,65)==expected,"replacing a copy changed another owner's root");
    }
    check(survivor.confidentiality_stream(73,15,65)==expected &&
          survivor.confidentiality_mac(bytes("lifetime"))==expected_mac,"roots expired with their original owner");
    std::atomic<bool> valid{true};
    std::array<execution::Task,4> workers;
    for(auto& worker:workers)worker=execution::Task([&]{
        for(unsigned i=0;i<64;++i){
            auto copied=survivor;auto assigned=waveform;assigned=copied;
            if(assigned.confidentiality_stream(73,15,65)!=expected ||
               assigned.confidentiality_mac(bytes("lifetime"))!=expected_mac)
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
        // Compare reused contexts with independently prepared fresh contexts.
        // Frozen independent CBC vectors above fix algorithm correctness; these
        // interleaved calls establish IV/chain reset and prepared-state lifetime.
        for(const auto size:{16U,32U,176U,288U,1024U,4096U}) {
            Bytes plain(size);for(std::size_t i=0;i<plain.size();++i)plain[i]=static_cast<std::uint8_t>(i*73+size);
            for(const auto iv_id:{0U,1U,255U,0U,1U}) {
                Bytes iv(16);for(std::size_t i=0;i<iv.size();++i)iv[i]=static_cast<std::uint8_t>(iv_id+i*29);
                const auto expected_a=key->prepare_confidentiality_cbc(context_a,salt_a)->encrypt(plain,iv);
                const auto expected_b=key->prepare_confidentiality_cbc(context_b,salt_b)->encrypt(plain,iv);
                check(expected_a!=expected_b,"prepared CBC transfer contexts overlap");
                check(prepared_a->decrypt(expected_a,iv)==plain,"prepared CBC decrypt-before-encrypt differs from fresh setup");
                check(prepared_b->encrypt(plain,iv)==expected_b,"prepared CBC other transfer differs from fresh setup");
                check(prepared_a->encrypt(plain,iv)==expected_a,"prepared CBC repeated/interleaved encryption differs from fresh setup");
                check(prepared_b->decrypt(expected_b,iv)==plain,"prepared CBC other transfer decryption mismatch");
                check(prepared_a->decrypt(expected_a,iv)==plain,"prepared CBC interleaved decryption mismatch");
                check(prepared_a->encrypt(plain,iv)==expected_a,"prepared CBC repeated IV changes ciphertext");
            }
        }
        const Bytes plain(32,0x61),iv(16,0x43);
        const auto expected=key->prepare_confidentiality_cbc(context_a,salt_a)->encrypt(plain,iv);
        rejects([&]{prepared_a->encrypt(Bytes(15),iv);},"prepared CBC accepted partial block");
        rejects([&]{prepared_a->decrypt(expected,Bytes(15));},"prepared CBC accepted short IV");
        rejects([&]{prepared_a->encrypt({},iv);},"prepared CBC accepted empty input");
        check(prepared_a->encrypt(plain,iv)==expected && prepared_a->decrypt(expected,iv)==plain,"invalid call damaged prepared CBC state");
    }
    rejects([&]{legacy.prepare_confidentiality_cbc(context_a,Bytes(31));},"prepared CBC accepted short salt");
    const auto owned=[&]{const auto temporary=independent_key();return temporary.prepare_confidentiality_cbc(context_a,salt_a);}();
    const Bytes plain(32,0x12),iv(16,0x34);
    check(owned->encrypt(plain,iv)==legacy.prepare_confidentiality_cbc(context_a,salt_a)->encrypt(plain,iv),"prepared CBC retained expired Crypto reference");
}

} // namespace

int main() {
    try {
        test_streams();
        test_authentication();
        test_factory_keys();
        test_confidentiality();
        test_confidentiality_authentication();
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
