#include "../src/gui/profile_reference.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace datapump;
namespace reference=gui::profile_reference;
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void near(double actual,double expected,const char* message) {check(std::abs(actual-expected)<1e-8,message);}
const reference::Row& length(const reference::Model& model,unsigned chips) {
    const auto found=std::find_if(model.rows.begin(),model.rows.end(),[&](const auto& row){return !row.extended && row.nominal_chips==chips;});
    check(found!=model.rows.end(),"missing finite profile");return *found;
}
reference::Model model(double bandwidth,double target,double carrier,bool keyed=false,
                       tuning::PatternMode mode=tuning::PatternMode::auto_pattern) {
    const auto config=tuning::resolve(bandwidth,target,mode,keyed,carrier).config;
    return reference::build(config,target,mode,keyed);
}
void default_boundaries() {
    const auto profiles=model(3600,30,1500);
    check(profiles.rows.size()==12,"automatic list must include all reachable lengths and extended integration");
    check(profiles.rows.front().label==">=65.56dB-Hz 16complex 9ms 112.5bit/s","high-target reference label");
    const auto& selected=length(profiles,128);
    check(selected.active && selected.label=="<32.49dB-Hz 128complex 71ms 14.06bit/s","default selected profile label");
    near(*length(profiles,256).boundary_db_hz,29.480625354554377,"128 to 256 transition");
    near(*length(profiles,512).boundary_db_hz,26.470325397914562,"256 to 512 transition");
    near(*length(profiles,1024).boundary_db_hz,23.46002544127475,"512 to 1024 transition");
    near(selected.seconds,128./1800,"default duration");near(selected.bits_per_second,14.0625,"default raw bit rate");
    for(const auto target:{80.,65.,60.,59.,40.,30.,29.,26.,23.,8.,-20.}) {
        const auto config=tuning::resolve(3600,target,tuning::PatternMode::auto_pattern,false,1500).config;
        const auto rows=reference::build(config,target,tuning::PatternMode::auto_pattern,false);
        check(std::count_if(rows.rows.begin(),rows.rows.end(),[](const auto& row){return row.active;})==1,"exactly one row is selected");
        const auto& active=*std::find_if(rows.rows.begin(),rows.rows.end(),[](const auto& row){return row.active;});
        check(active.nominal_chips==config.spreading_factor && active.extended==(config.integration_seconds>0),"selection disagrees with tuner");
        near(active.seconds,modem::symbol_seconds(config),"selected duration disagrees with tuner");
    }
    for(const auto chips:{32U,64U,128U,256U,512U,1024U,2048U,4096U,8192U,16384U}) {
        const auto edge=*length(profiles,chips).boundary_db_hz;
        const auto above=tuning::resolve(3600,edge+1e-7,tuning::PatternMode::auto_pattern,false,1500).config;
        const auto below=tuning::resolve(3600,edge-1e-7,tuning::PatternMode::auto_pattern,false,1500).config;
        check(above.spreading_factor<chips && below.spreading_factor==chips,"displayed boundary must separate its two adjacent lengths");
    }
}
void actual_geometry() {
    near(*length(model(12000,80,9000),32).boundary_db_hz,70.79181246047625,"rate-specific short-pattern floor");
    check(model(600,80,1500).rows.front().nominal_chips==64,"public long-sample geometry floor");
    check(model(600,80,1500,true).rows.front().nominal_chips==32,"private orthogonal geometry floor");
    check(model(600,80,400,true).rows.front().nominal_chips==16,"custom carrier and clock must determine floor");
    check(model(3200,80,1600,true).rows.front().nominal_chips==32,"orthogonal private floor");
    auto clock=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,false,1500).config;
    clock.sample_rate=48000;
    const auto profiles=reference::build(clock,80,tuning::PatternMode::auto_pattern,false);
    check(profiles.rows.front().nominal_chips==64,"caller sample clock must survive profile lookup");
    for(const auto& row:profiles.rows)if(!row.extended) {
        auto expected=clock;expected.spreading_factor=row.nominal_chips;
        check(row.chips==modem::pattern_chips_per_symbol(expected),"complex chip count must include actual sample quantization");
    }
    const auto tones=model(1200,80,1500,false,tuning::PatternMode::auto_tone);
    check(tones.rows.front().nominal_chips==64,"automatic tones retain their floor");
}
void fixed_and_extended() {
    const auto fixed=model(1200,-40,1500,true,tuning::PatternMode::pattern_3);
    check(fixed.rows.size()==1 && fixed.rows.front().active && !fixed.rows.front().boundary_db_hz &&
          fixed.rows.front().label=="fixed 3complex 5ms 200bit/s","forced modes must not imply automatic transitions");
    const auto weak_fixed=model(1200,-250,1500,false,tuning::PatternMode::pattern_8);
    check(weak_fixed.rows.size()==1 && weak_fixed.rows.front().active &&
          weak_fixed.rows.front().nominal_chips==8 && !weak_fixed.rows.front().extended,
          "finite forced targets below the receive-list range must remain valid");
    const auto extended=model(3600,-20,1500);
    const auto& row=extended.rows.back();
    check(row.active && row.extended && row.chips>16384,"extended integration must show actual chip count");
    near(row.seconds,6309.57344480193,"extended duration");
    check(row.label.find("0bit/s")==std::string::npos && row.label.find("bit/s")!=std::string::npos,"extended rate must remain visible");
    const auto extreme=model(1,-130,1500);
    check(extreme.rows.back().active && extreme.rows.back().bits_per_second>0,"long valid integration must stay bounded and nonzero");
    check(model(3600,4000,1500).rows.front().active,"strong finite targets must retain the automatic floor");
}
}
int main() {
    try {default_boundaries();actual_geometry();fixed_and_extended();std::cout<<"profile reference tests passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
