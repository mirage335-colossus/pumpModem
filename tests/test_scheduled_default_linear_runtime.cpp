#include "scheduled_default_linear_runtime.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace runtime=d::scheduled_default_linear_runtime;
namespace orig=d::scheduled_original_partition;
using datapump::modem::SchedHopCoordinate;
using datapump::modem::SchedHopLocation;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Map {
    bool ramp=false;
    SchedHopLocation locate(SchedHopCoordinate tick)const {
        SchedHopLocation p;p.active=true;p.continuous=!ramp;p.source=tick;
        p.physical_end=p.source_end=136;p.rise_begin=0;p.flat_begin=34;p.flat_end=102;p.fall_end=136;
        return p;
    }
    SchedHopCoordinate source_to_physical(SchedHopCoordinate source,bool)const{return source;}
};
orig::Event event(std::uint64_t begin=0,std::uint64_t end=136) {
    orig::Event e;e.identity={1,2,3,1,0,0,1,1};e.symbol_samples=136;e.chip_samples=34;
    e.sample_rate=48000;e.begin=e.native_begin=begin;e.end=e.native_end=end;
    e.history_first=0;e.capture_end=136;e.left_guard=e.right_guard=1;return e;
}
struct Fence {std::uint64_t original_first,original_count;};
const auto fences=[](auto first,auto end,auto consume) {
    for(const Fence f:{Fence{0,64},Fence{64,64},Fence{128,8}})
        if(f.original_first<end && f.original_first+f.original_count>first)consume(f);
};
void exact_piece_partitions_and_crops() {
    Map map;
    check(runtime::Context::piece_count(event(),map,fences)==3,"frozen processing Piece prepass changed");
    check(runtime::Context::reservation(3,1)<runtime::Context::reservation(runtime::maximum_pieces,1),"Piece reservation still uses unconditional maximum");
    runtime::Context c(4,17);c.add(0,event(),map,fences);
    const auto chip=c.classify(0,0,17);check(chip.valid&&chip.observed&&chip.owner==0&&chip.section==0&&chip.window==0,"first exact partition changed");
    check(!c.classify(0,16,18).valid,"differential split classified as one window");
    check(!c.classify(0,33,35).valid,"chip/quarter split classified as one owner");
    runtime::Context across(1,0);across.add(0,event(),map,fences);
    const auto joint=across.classify(0,50,65);check(joint.valid&&joint.observed&&joint.owner==1,"processing-fence partition rejected exact owner");
    runtime::Context cropped(1,0);cropped.add(7,event(9,121),map,fences);
    check(cropped.classify(7,0,9).valid&&!cropped.classify(7,0,9).observed,"outside Original crop became observed");
    check(!cropped.classify(7,8,10).valid,"partial Original crop classified whole");
    unsigned shapes=0;
    const auto row=cropped.row(7,10,false,[&](auto,double){++shapes;return d::scheduled_linear_grid::Pair{};});
    check(row.observed&&row.source==10&&row.owner==0&&shapes==0,"metadata fallback shaped private q or changed source");
    const auto missing=cropped.row(7,8,true,[&](auto,double){++shapes;return d::scheduled_linear_grid::Pair{};});
    check(!missing.observed&&shapes==0,"unobserved crop row shaped private q");
}
void ramp_gain_and_cancel_retry() {
    Map map;map.ramp=true;runtime::Context c(1,0);c.add(0,event(),map,fences);
    const auto row=c.row(0,3,true,[](auto,double gain){return d::scheduled_linear_grid::Pair{std::complex<double>{gain,0},std::complex<double>{0,gain}};});
    check(row.observed&&row.gain>0&&row.gain<1&&row.q[0].real()==row.gain,"Original fallback outer gain lost");
    check(!c.classify(0,0,2).valid,"zero/positive ramp edge certified as entirely usable");
    const auto ramp=c.classify(0,1,10);check(ramp.valid&&ramp.observed,"positive ramp partition not proved");
    runtime::Context retry(1,0);std::stop_source stop;stop.request_stop();bool cancelled=false;
    try {retry.add(0,event(),map,fences,stop.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled,"pre-cancelled Original piece build completed");retry.add(0,event(),map,fences);
    check(retry.row(0,3,false,[](auto,double){return d::scheduled_linear_grid::Pair{};}).observed,"cancelled unpublished lane poisoned retry");
    runtime::Context partial(1,0);bool failed=false;
    try {partial.add(0,event(),map,[](auto,auto,auto consume){consume(Fence{0,64});throw std::runtime_error("injected stopped fence walk");},{},3);}
    catch(const std::exception&){failed=true;}
    check(failed,"injected partial Piece build completed");partial.add(0,event(),map,fences,{},3);
    check(partial.classify(0,1,10).valid,"unpublished Piece fragments survived retry");
    runtime::Cache cache;cache.key.outputs=2;cache.observed_source_begin={3,7};cache.bind();
    check(cache.geometry.observed_source_begin.size()==2&&cache.geometry.observed_source_begin[1]==7,"retained cache lost observation-origin ownership");
}
}
int main(){try {exact_piece_partitions_and_crops();ramp_gain_and_cancel_retry();std::cout<<"Default linear runtime source cases passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
