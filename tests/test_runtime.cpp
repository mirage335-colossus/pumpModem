#include "datapump/runtime.hpp"
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
    check(terminal_text(Bytes{27,']','5','2',';',7})=="\\x1b]52;\\x07");
    check(terminal_text(Bytes{'c','a','f',0xc3,0xa9})=="caf\xc3\xa9");
    check(terminal_text(Bytes{0xc2,0x9b})=="\\xc2\\x9b");
    std::istringstream input("12345"); rejects([&]{read_bounded(input,4);});
    std::cout<<"runtime tests passed\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;} }
