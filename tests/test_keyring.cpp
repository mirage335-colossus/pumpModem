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
}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("datapump-keyring-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        const testing::KeyfilePolicy policy{4096,8193};
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
