#include "datapump/crypto.hpp"
#include "fixture_keyfile.hpp"
#include "v4_crypto_vectors.hpp"
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace datapump;
namespace {
void check(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F f) {try{f();}catch(const Error&){return;}throw Error("invalid keyring accepted");}
Bytes read(const std::filesystem::path& path) {std::ifstream file(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(file),{});}
void write(const std::filesystem::path& path,const Bytes& bytes) {std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
Bytes hex(std::string_view value){Bytes bytes;for(std::size_t i=0;i<value.size();i+=2)bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(value.substr(i,2)),nullptr,16)));return bytes;}
Bytes frozen_file(std::string_view prefix,std::size_t header_bytes,bool patterned,std::string_view outer,std::string_view tag) {
    auto file=hex(prefix);for(std::size_t i=0;i<header_bytes;++i)file.push_back(patterned?static_cast<std::uint8_t>(i*37+11):0x5a);
    const auto encrypted=hex(outer),authentication=hex(tag);file.insert(file.end(),encrypted.begin(),encrypted.end());file.insert(file.end(),authentication.begin(),authentication.end());return file;
}
Bytes independent_tag(const Bytes& file,std::size_t header_bytes,const Bytes& key) {
    const std::string_view label="datapump/v4/keyfile-auth/";Bytes message(label.begin(),label.end());
    message.insert(message.end(),file.begin(),file.begin()+48);const auto outer_size=file.size()-48-header_bytes-32;
    for(unsigned i=0;i<8;++i)message.push_back(static_cast<std::uint8_t>(outer_size>>(56-8*i)));
    message.insert(message.end(),file.begin()+static_cast<std::ptrdiff_t>(48+header_bytes),file.end()-32);
    auto* algorithm=EVP_MAC_fetch(nullptr,"HMAC",nullptr);check(algorithm!=nullptr,"test HMAC unavailable");
    auto* context=EVP_MAC_CTX_new(algorithm);EVP_MAC_free(algorithm);check(context!=nullptr,"test HMAC allocation failed");
    char digest[]="SHA256";OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,digest,0),OSSL_PARAM_construct_end()};
    Bytes result(32);std::size_t length=0;
    const bool ok=EVP_MAC_init(context,key.data(),key.size(),params)==1 && EVP_MAC_update(context,message.data(),message.size())==1 && EVP_MAC_final(context,result.data(),&length,result.size())==1 && length==result.size();
    EVP_MAC_CTX_free(context);check(ok,"test independent HMAC failed");return result;
}
void independent_imports(const std::filesystem::path& dir,const testing::KeyfilePolicy& policy) {
    using namespace v4_vectors;
    // Frozen independent Python hashlib/HMAC + cryptography AES-GCM +
    // libgcrypt Serpent oracle; generator never loads DataPump code.
    const auto original=frozen_file(keyring_prefix,4096,false,keyring_outer_ciphertext_and_gcm_tag,keyring_serpent_auth_tag);
    const auto path=dir/"independent-v4";write(path,original);
    const auto expected=Crypto::from_independent_roots(hex(serpent_root),hex(aes_root));
    const auto entries=testing::load_keyring(path,policy);
    check(entries.size()==1 && entries[0].name=="Fixture","independent keyring identity changed");
    check(entries[0].key.identity_fingerprint(hex(identity_context))==hex(identity_confidentiality),"independent entry roots changed on import");
    check(entries[0].key.confidentiality_stream(1720000000,0,64)==hex(streams_0_64_cascade),"independent ciphertext roots changed on import");
    check(entries[0].key.mac({})==expected.mac({}),"AES purpose keys changed on import");
    for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss})
        check(entries[0].key.stream(purpose,1720000000,0,64)==expected.stream(purpose,1720000000,0,64),"independent AES purpose keys changed on import");
    auto bad_s=original;bad_s.back()^=1;write(path,bad_s);rejects([&]{testing::load_keyring(path,policy);});
    // Alter only the outer GCM tag, then recompute a valid independent S tag.
    // The inner ciphertext is intact; rejection requires authenticating GCM.
    auto bad_a=original;bad_a[bad_a.size()-33]^=1;
    const auto tag=independent_tag(bad_a,4096,hex(keyring_serpent_auth_key));std::copy(tag.begin(),tag.end(),bad_a.end()-32);
    write(path,bad_a);rejects([&]{testing::load_keyring(path,policy);});
    // Patterned odd-length header and pad fix the lane partition, both tails,
    // and use of every byte rather than only the first input chunk.
    const testing::KeyfilePolicy odd_policy{4097,8193};
    const auto padded=frozen_file(patterned_padded_keyring_prefix,4097,true,patterned_padded_keyring_outer_ciphertext_and_gcm_tag,patterned_padded_keyring_serpent_auth_tag);
    const auto padded_path=dir/"independent-v4-padded",pad=dir/"independent-pad";
    Bytes pad_data(8193);for(std::size_t i=0;i<pad_data.size();++i)pad_data[i]=static_cast<std::uint8_t>(i*73+5);
    write(padded_path,padded);write(pad,pad_data);
    check(testing::load_keyring(padded_path,odd_policy,pad).front().key.identity_fingerprint(hex(identity_context))==hex(identity_confidentiality),"odd header/pad lane partition vector mismatch");
    pad_data.back()^=1;write(pad,pad_data);rejects([&]{testing::load_keyring(padded_path,odd_policy,pad);});
    // Every historical magic is rejected from a prefix alone; dimensions or
    // payload must never trigger an implicit legacy reconstruction path.
    for(char version:{'1','2','3'}) {
        Bytes prefix(48);const std::string magic="DPMKEY0"+std::string(1,version);std::copy(magic.begin(),magic.end(),prefix.begin());
        write(path,prefix);bool old_rejected=false;
        try{(void)testing::load_keyring(path,policy);}catch(const Error& error){old_rejected=std::string_view(error.what()).find("unsupported keyfile format")!=std::string_view::npos;}
        check(old_rejected,"legacy keyfile reached version-dependent decoding");
    }
}


}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("datapump-keyring-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        const testing::KeyfilePolicy policy{4096,8193};
        independent_imports(dir,policy);
        const auto path=dir/"keys";
        const std::vector<std::string> names{"Station A","Station B","Caf\xc3\xa9"};
        (void)testing::create_keyring(path,names,policy,std::nullopt,fixture_keyfile::generation());
        const auto original=read(path);
        const auto keys=testing::load_keyring(path,policy),again=testing::load_keyring(path,policy);
        check(keys.size()==3,"named key count");
        std::set<std::array<std::uint8_t,32>> streams;
        for(std::size_t i=0;i<keys.size();++i) {
            check(keys[i].name==names[i],"key identity survives encrypted storage");
            check(keys[i].key.mac({})==again[i].key.mac({}),"all restored key sets are deterministic");
            check(keys[i].key.confidentiality_stream(1,13,127)==again[i].key.confidentiality_stream(1,13,127),"v4 independent roots survive serialization");
            check(keys[i].key.confidentiality_stream(1,0,32)!=keys[i].key.stream(StreamPurpose::Data,1,0,32),"v4 payload retained AES-only stream");
            for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
                const auto bytes=keys[i].key.stream(purpose,1,0,32);
                std::array<std::uint8_t,32> block{};std::copy(bytes.begin(),bytes.end(),block.begin());
                streams.insert(block);
            }
        }
        check(streams.size()==12,"entries and all stream purposes independent");
        check(keys[0].key.mac({})!=keys[1].key.mac({}),"MAC keys independent between entries");
        for(const auto& name:names) check(std::search(original.begin(),original.end(),name.begin(),name.end())==original.end(),"names encrypted in payload");
        rejects([&]{(void)testing::create_keyring(path,names,policy,std::nullopt,fixture_keyfile::generation());});
        check(read(path)==original,"exclusive creation leaves old keyring intact");
        rejects([&]{load_keyring(path);});
        for(auto position:{std::size_t(7),std::size_t(31),std::size_t(48),original.size()-25,original.size()-1}) {
            auto bytes=original;bytes[position]^=1;write(path,bytes);
            rejects([&]{testing::load_keyring(path,policy);});
        }
        auto truncated=original;truncated.pop_back();write(path,truncated);
        rejects([&]{testing::load_keyring(path,policy);});
        auto trailing=original;trailing.push_back(0);write(path,trailing);rejects([&]{testing::load_keyring(path,policy);});
        write(path,original);
        const auto legacy=dir/"legacy";(void)testing::create_keyfile(legacy,policy,std::nullopt,fixture_keyfile::generation());
        const auto one=testing::load_keyring(legacy,policy);
        check(one.size()==1 && one[0].name=="Default","current single-entry keyfile identity");
        check(one[0].key.mac({})==testing::load_keyfile(legacy,policy).mac({}),"single-entry roots preserved");
        rejects([&]{(void)testing::create_keyring(dir/"empty",{},policy);});
        rejects([&]{(void)testing::create_keyring(dir/"duplicate",{"same","same"},policy);});
        rejects([&]{(void)testing::create_keyring(dir/"invalid",{"bad\nname"},policy);});
        rejects([&]{(void)testing::create_keyring(dir/"unicode-control",{"bad\xc2\x9b"},policy);});
        rejects([&]{(void)testing::create_keyring(dir/"malformed",{std::string("\xff")},policy);});
        rejects([&]{(void)testing::create_keyring(dir/"too-long",{std::string(65,'a')},policy);});
        std::vector<std::string> too_many;
        for(unsigned i=0;i<129;++i) too_many.push_back(std::to_string(i));
        rejects([&]{(void)testing::create_keyring(dir/"too-many",too_many,policy);});
        const auto pad=dir/"pad";write(pad,Bytes(8193,0x42));
        (void)(void)testing::create_keyring(dir/"bound",names,policy,pad,fixture_keyfile::generation());
        check(testing::load_keyring(dir/"bound",policy,pad).size()==3,"optional external pad supports full keyring");
        rejects([&]{testing::load_keyring(dir/"bound",policy);});
        write(pad,Bytes(8193,0x43));
        rejects([&]{testing::load_keyring(dir/"bound",policy,pad);});
        std::filesystem::remove_all(dir);
        std::cout<<"keyring tests passed\n";
    } catch(const std::exception& error) {std::filesystem::remove_all(dir);std::cerr<<error.what()<<'\n';return 1;}
}
