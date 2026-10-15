#include "../src/scheduled_zero_guard.hpp"
#include "../src/scheduled_original_boundary_partition.hpp"
#include <iostream>
using namespace datapump;
namespace z=modem::detail::scheduled_zero_guard;
namespace original=modem::detail::scheduled_original_partition;
void check(bool value,const char* message){if(!value)throw Error(message);}

original::Evaluation evaluated(original::Event e,std::uint64_t row,std::uint64_t fence_size,
        modem::SchedHopSourceMap& map) {
    const auto first=row/fence_size*fence_size;
    original::ProcessingFence fence{first+1,1,first,std::min(e.end,first+fence_size)};
    auto at=first;
    while(at<fence.end) {
        const auto piece=original::make_piece(e,fence,at,map);
        if(row<piece.end) {
            const auto result=original::evaluate(e,piece,row);
            check(result.valid(),"actual Original source/carry validity failed");
            if(piece.location0.active) {
                const auto physical=piece.tick0.add_signed(static_cast<long double>(row-piece.anchor_first)*piece.rate);
                check(modem::SchedHopSourceMap::gain(piece.location0,physical)==0,
                    "proved guard had positive executed Original gain");
            }
            return result;
        }
        at=piece.end;
    }
    throw Error("Original fence did not contain requested row");
}
void run(unsigned multiplier,std::uint32_t rate,long double clock) {
    modem::SchedHopPlan plan;plan.time_multiplier=multiplier;plan.group_hops=1;
    plan.clock_error_seconds=plan.peer_clock_error_seconds=.01;plan.audio_error_seconds=0;
    plan.region_seconds=plan.offset_seconds=0;plan.ramp_seconds=.006;
    Crypto key(Bytes(32,static_cast<std::uint8_t>(0x61+multiplier)));
    modem::SchedHopSourceMap map(plan,key,rate);
    const modem::SchedHopCoordinate phase{8312ULL*rate,0};
    constexpr long double origin=.375L;
    const auto end=6ULL*rate+1024;
    const auto anchor=map.locate(phase).source;
    const auto magnitude=2*(1.L+origin+3*end+65536+8192);
    const auto radius=z::radius(.5L*clock,.0001L,magnitude,clock);
    check(std::isfinite(radius) && radius>2,"zero guard native reserve missing");
    std::uint64_t proven=0,replayed=0;
    for(std::uint64_t p=17;p<6ULL*rate;p+=(rate<1000?1:17)) {
        const auto center=phase.add_signed((static_cast<long double>(p)-origin)*clock);
        const auto location=map.locate(center);
        const auto proof=z::prove(location,center,radius);
        if(!proof)continue;
        check(location.active && modem::SchedHopSourceMap::gain(location,center)==0,
            "zero guard did not describe active literal-zero power");
        ++proven;
        for(const auto shift:{0ULL,513ULL})for(const auto delta:{-.5L,-.125L,0.L,.5L}) {
            original::Event e;e.identity={1,1,1,1,0,0,1,1};e.epoch_sample=phase.sample;e.anchor=anchor;
            e.origin=origin+shift+delta;e.rate=clock;e.symbol_samples=65536;e.chip_samples=34;
            e.sample_rate=rate;e.begin=e.native_begin=e.history_first=0;e.end=e.native_end=e.capture_end=end;
            e.left_guard=e.right_guard=1;
            check(original::valid(e),"actual Original guard fixture invalid");
            for(const auto fence:{13ULL,512ULL}) {
                const auto result=evaluated(e,p+shift,fence,map);++replayed;
                check(!result.current && !result.carry && result.gain==0,
                    "zero guard manufactured an absent/valid observation");
            }
        }
        check(!z::prove(location,{location.rise_begin,0},radius),"rise boundary incorrectly proved zero");
        check(!z::prove(location,{location.flat_begin,0},radius),"full-power boundary incorrectly proved zero");
        check(!z::prove(location,{location.fall_end,0},radius),"fall boundary incorrectly proved zero");
        check(!z::prove(location,{location.physical_begin,0},radius),"unproved group edge accepted");
        check(!z::prove(location,center,1.),"missing native-piece reserve accepted");
        check(!z::prove_current(location,center,0,65536,radius),"source start validation hidden by quiet proof");
        check(!z::prove_current(location,center,65536,65536,radius),"carry validation hidden by quiet proof");
        check(static_cast<bool>(z::prove_current(location,center,32768,65536,radius)),"current source interior lost zero proof");
    }
    check(proven>0 && replayed==16*proven,"actual zero guard coverage missing");
    std::cout<<"zero guard M="<<multiplier<<" Fs="<<rate<<" rate="<<static_cast<double>(clock)
        <<" proven="<<proven<<" actual_piece_rows="<<replayed<<'\n';
}
int main(){try {
    if(!modem::detail::scheduled_acquisition_certificate::coordinate_environment())return 77;
    for(const auto multiplier:{3U,5U})for(const auto rate:{512U,6120U})
        for(const auto clock:{.9997L,1.L,1.0003L})run(multiplier,rate,clock);
    check(!std::isfinite(z::radius(.5L,0,0x1p53L,1)),"unbounded coordinate magnitude qualified");
    check(!std::isfinite(z::radius(.5L,0,100,1.02L)),"unsupported physical rate qualified");
    modem::SchedHopLocation continuous;continuous.active=continuous.continuous=true;
    check(!z::prove(continuous,{100,0},3.),"continuous limit invented a scheduled gap");
    std::cout<<"structural zero guards preserve actual Original fences and source ownership\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
