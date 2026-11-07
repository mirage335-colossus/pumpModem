#include "datapump/crypto.hpp"
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
void independent_imports(const std::filesystem::path& dir,const testing::KeyfilePolicy& policy) {
    // Independently generated with hashlib/HMAC, cryptography AES-GCM and
    // libgcrypt Serpent-256 CTR; see fixtures/crypto/generate-keyring-vectors.py.
    const std::array<std::pair<std::string_view,std::string_view>,2> vectors{{
        {"44504d4b45593032000000000000100000000000000000000000000000000001000102030405060708090a0b000000a9",
         "4e584aaeb773924aba4a5ea5122e2fb8843aaab80b09033138e87d803261155311a53acd6a1ae8b281b788708f4b66a5d3bb875845efdc6788e4a2c94100077bc71665fb0b9404689f98942473f731beddbcc23da4b72f236ef96a854cb7f9ca4f14f371492ef2fad5d67647c5ad853d0d3aa91792956e394ec42b5ba51b42647b6855e75de1c062d47907590125dd459293fe2943b2dbc000619f368047dd12050ae9b52a72d2f86903c4a6627ac3a7562945c113687b3690"},
        {"44504d4b45593033000000000000100000000000000000000000000000000001000102030405060708090a0b000000e9",
         "26b66b3bf3dc9de29c2e19ccc8ca916d21c594a3acebc2538989cf0c6afbd9ed59975256e8655ad734289396f064ac83fd7dad6562b28b49e43e4cb3d30dcef9aba2cbc5f680c0ae5ccf90751d60befea5406b3ce16fd9e3b232480fe1aaa539c6d021d74e04aed11b9dca41a031b875ea99e47ad528a1fd57566fc4deab39ea8f48132bfe30e5a7103e52c6cd165bc6a9f89badba4f7deab1dfbaf41d85979d9ec158cc58304830687933a85b4f594245d421a0b1edce1f7270ceaa975402005d95402a5e041c9b33655c6ebb46fe77c05369ae7cf9dd74eec38f41096d1f165637cfae967034d1642ff6d8389d3255fa93b3648e74acabb8"},
    }};
    Bytes master(32);for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i);
    const Crypto expected(master);
    for(std::size_t version=0;version<vectors.size();++version){
        auto file=hex(vectors[version].first);file.insert(file.end(),4096,0x5a);
        const auto encrypted=hex(vectors[version].second);file.insert(file.end(),encrypted.begin(),encrypted.end());
        const auto path=dir/(version?"independent-v3":"independent-v2");write(path,file);
        const auto entries=testing::load_keyring(path,policy);
        check(entries.size()==1 && entries[0].name=="Fixture","independent keyring fixture identity changed");
        check(entries[0].key.mac({})==expected.mac({}),"legacy purpose keys changed on import");
        for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss})
            check(entries[0].key.stream(purpose,1720000000,0,64)==expected.stream(purpose,1720000000,0,64),"independent purpose keys changed on import");
        if(!version)check(entries[0].key.confidentiality_stream(1720000000,0,64)==expected.confidentiality_stream(1720000000,0,64),"v2 import derived a different legacy cascade root");
        else check(entries[0].key.confidentiality_stream(1720000000,0,64)==hex("827a7b12e6fb843db246f41a92e93233a7b953ba7fa63244d282e52528344e211581b6ff5e21bea2ce29f65a1fc177f3348031bc58019b237817bc3baefdf23f"),"v3 import lost independent confidentiality root");
        auto damaged=file;damaged.back()^=1;write(path,damaged);
        rejects([&]{testing::load_keyring(path,policy);});
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
        testing::create_keyring(path,names,policy);
        const auto original=read(path);
        const auto keys=testing::load_keyring(path,policy),again=testing::load_keyring(path,policy);
        check(keys.size()==3,"named key count");
        std::set<std::array<std::uint8_t,32>> streams;
        for(std::size_t i=0;i<keys.size();++i) {
            check(keys[i].name==names[i],"key identity survives encrypted storage");
            check(keys[i].key.mac({})==again[i].key.mac({}),"all restored key sets are deterministic");
            check(keys[i].key.confidentiality_stream(1,13,127)==again[i].key.confidentiality_stream(1,13,127),"v3 confidentiality root survives serialization");
            check(keys[i].key.confidentiality_stream(1,0,32)!=keys[i].key.stream(StreamPurpose::Data,1,0,32),"v3 payload retained AES-only stream");
            for(auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
                const auto bytes=keys[i].key.stream(purpose,1,0,32);
                std::array<std::uint8_t,32> block{};std::copy(bytes.begin(),bytes.end(),block.begin());
                streams.insert(block);
            }
        }
        check(streams.size()==12,"entries and all stream purposes independent");
        check(keys[0].key.mac({})!=keys[1].key.mac({}),"MAC keys independent between entries");
        for(const auto& name:names) check(std::search(original.begin(),original.end(),name.begin(),name.end())==original.end(),"names encrypted in payload");
        rejects([&]{testing::create_keyring(path,names,policy);});
        check(read(path)==original,"exclusive creation leaves old keyring intact");
        rejects([&]{load_keyring(path);});
        for(auto position:{std::size_t(7),std::size_t(31),std::size_t(48),original.size()-25,original.size()-1}) {
            auto bytes=original;bytes[position]^=1;write(path,bytes);
            rejects([&]{testing::load_keyring(path,policy);});
        }
        auto truncated=original;truncated.pop_back();write(path,truncated);
        rejects([&]{testing::load_keyring(path,policy);});
        write(path,original);
        const auto legacy=dir/"legacy";testing::create_keyfile(legacy,policy);
        const auto one=testing::load_keyring(legacy,policy);
        check(one.size()==1 && one[0].name=="Default","legacy keyfile compatibility");
        check(one[0].key.mac({})==testing::load_keyfile(legacy,policy).mac({}),"legacy secrets preserved");
        rejects([&]{testing::create_keyring(dir/"empty",{},policy);});
        rejects([&]{testing::create_keyring(dir/"duplicate",{"same","same"},policy);});
        rejects([&]{testing::create_keyring(dir/"invalid",{"bad\nname"},policy);});
        rejects([&]{testing::create_keyring(dir/"unicode-control",{"bad\xc2\x9b"},policy);});
        rejects([&]{testing::create_keyring(dir/"malformed",{std::string("\xff")},policy);});
        rejects([&]{testing::create_keyring(dir/"too-long",{std::string(65,'a')},policy);});
        std::vector<std::string> too_many;
        for(unsigned i=0;i<129;++i) too_many.push_back(std::to_string(i));
        rejects([&]{testing::create_keyring(dir/"too-many",too_many,policy);});
        const auto pad=dir/"pad";write(pad,Bytes(8193,0x42));
        testing::create_keyring(dir/"bound",names,policy,pad);
        check(testing::load_keyring(dir/"bound",policy,pad).size()==3,"optional legacy pad supports full keyring");
        rejects([&]{testing::load_keyring(dir/"bound",policy);});
        write(pad,Bytes(8193,0x43));
        rejects([&]{testing::load_keyring(dir/"bound",policy,pad);});
        std::filesystem::remove_all(dir);
        std::cout<<"keyring tests passed\n";
    } catch(const std::exception& error) {std::filesystem::remove_all(dir);std::cerr<<error.what()<<'\n';return 1;}
}
