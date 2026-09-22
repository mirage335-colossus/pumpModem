#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>
using namespace datapump;
void check(bool good) { if(!good) throw Error("runtime test failed"); }
template<class F> void rejects(F f) { bool caught=false; try {f();} catch(const Error&) {caught=true;} check(caught); }
int main() { try {
    rejects([]{runtime::dsp_workspace_budget(0);});
    rejects([]{runtime::dsp_workspace_budget(101);});
    rejects([]{runtime::dsp_workspace_budget(std::numeric_limits<unsigned>::max());});
    const auto default_workspace=runtime::default_dsp_workspace_bytes();
    // Host headroom is live data: verify supported endpoints without assuming
    // identical available RAM on successive reads or a particular host size.
    (void)runtime::available_memory_bytes();
    (void)runtime::dsp_workspace_budget(1);
    (void)runtime::dsp_workspace_budget(100);
    check(runtime::default_dsp_workspace_bytes()==default_workspace);
    ReceiveCache cache(6);
    cache.put({"a","",{1,2,3},false,false});
    check(!cache.get("a",true));
    cache.put({"a","",{4,5},true,true});
    check(cache.get("a",true)->data==Bytes({4,5}));
    cache.put({"b","",{6,7,8,9},true,false});
    cache.put({"c","",{0},false,false});
    check(!cache.get("a") && cache.size_bytes()==5);
    rejects([&]{cache.put({"d","",Bytes(7),true,false});});
    check(cache.get("b")->data.size()==4);
    check(runtime::default_dsp_workspace_bytes()==default_workspace);
    check(cache.erase("c") && !cache.erase("missing"));
    TransmitGate gate;
    auto t=TransmitGate::Clock::time_point{};
    gate.started(t); rejects([&]{gate.started(t);});
    gate.finished(t+std::chrono::seconds(3));
    check(gate.remaining(t+std::chrono::seconds(4))==std::chrono::seconds(5));
    rejects([&]{gate.started(t+std::chrono::seconds(5));});
    gate.started(t+std::chrono::seconds(9));
    rejects([]{TransmitGate bad(std::chrono::milliseconds(-1));});
    auto candidates=drift_candidates(100,2,true);
    check(candidates==std::vector<std::uint64_t>({100,99,101,98,102}));
    check(drift_candidates(0,1,true)==std::vector<std::uint64_t>({0,1}));
    rejects([]{drift_candidates(1,121,false);});
    rejects([]{drift_candidates(1,32769,true);});
    check(sample_nanoseconds(3,3)==1000000000ULL);
    check(sample_nanoseconds(48001,48000)==1000020833ULL);
    rejects([]{sample_nanoseconds(1,0);});
    rejects([]{sample_nanoseconds(std::numeric_limits<std::uint64_t>::max(),1);});
    BandSchedule bands({{"20m",14000000,14350000},{"40m",7000000,7300000}},60);
    check(bands.at(59).name=="20m" && bands.at(60).name=="40m" && bands.at(120).name=="20m");
    check(json_escape("a\n\"\\")=="a\\n\\\"\\\\");
    check(base64_encode(Bytes{'f','o','o'})=="Zm9v");
    check(terminal_text(Bytes{27,']','5','2',';',7})=="__52__");
    check(terminal_text(Bytes{'c','a','f',0xc3,0xa9})=="caf__");
    check(terminal_text(Bytes{0xc2,0x9b})=="__");
    // Exhaustive independent allowlist: controls and every non-ASCII byte
    // remain placeholders, including in the developer-only ASCII view.
    constexpr std::string_view allowed="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.@ -_/=";
    Bytes every_byte;for(unsigned value=0;value<256;++value)every_byte.push_back(static_cast<std::uint8_t>(value));
    const auto restricted=received_text(every_byte);
    const auto shellcode=received_text(every_byte,true);
    check(restricted.size()==every_byte.size() && shellcode.size()==every_byte.size());
    for(unsigned value=0;value<256;++value) {
        const auto byte=static_cast<char>(value);
        check(restricted[value]==(allowed.find(byte)!=std::string_view::npos?byte:'_'));
        check(shellcode[value]==(value>=32 && value<=126?byte:'_'));
    }
    check(received_text(allowed)==allowed);
    check(received_text(std::string_view("A\0;\\&\n\xc3\xa9",8))=="A_______");
    check(received_text(std::string_view("A\0;\\&\n\xc3\xa9",8),true)=="A_;\\&___");
    std::istringstream input("12345"); rejects([&]{read_bounded(input,4);});
    struct TemporaryDirectory {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() {check(std::filesystem::create_directory(path));}
        ~TemporaryDirectory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } files;
    const auto empty=files.path/"empty.bin";
    write_new_file(empty.string(),{});
    check(std::filesystem::file_size(empty)==0);
    rejects([&]{write_new_file(empty.string(),Bytes{1});});
    check(std::filesystem::file_size(empty)==0);
    const auto binary=files.path/"binary.bin";
    const Bytes expected{0,255,0};
    write_new_file(binary.string(),expected);
    std::ifstream saved(binary,std::ios::binary);
    check(read_bounded(saved,expected.size())==expected);
    std::cout<<"runtime tests passed\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;} }
