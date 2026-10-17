#include "scheduled_default_point_observation.hpp"
#include "scheduled_default_point_piece_cache.hpp"
#include "scheduled_default_linear_runtime.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace obs=d::scheduled_default_point_observation;
namespace rt=d::scheduled_default_linear_runtime;
namespace original=d::scheduled_original_partition;
namespace sparse=d::scheduled_sparse_owned_spans;
using datapump::modem::SchedHopCoordinate;
using datapump::modem::SchedHopLocation;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Map {
    SchedHopLocation locate(SchedHopCoordinate p)const {
        SchedHopLocation l;l.frame_index=p.sample<1200?0:1;
        if(p.sample>=1100&&p.sample<1200){l.source={1100,0};return l;}
        l.active=true;const auto base=p.sample<1100?1000ULL:1200ULL;
        l.source=p.sample<1100?p:p.add_signed(-100);
        l.physical_begin=base;l.physical_end=base+100;l.source_end=p.sample<1100?1100:1200;
        l.rise_begin=base+1;l.flat_begin=base+20;l.flat_end=base+80;l.fall_end=base+99;return l;
    }
    SchedHopCoordinate source_to_physical(SchedHopCoordinate p,bool)const{return p.sample>=1100?p.add_samples(100):p;}
};
original::Event event(long double rate=1,long double fraction=0) {
    original::Event e;e.identity={1,2,3,1,0,0,1,1};e.epoch_sample=1000;e.anchor={1000,0};
    e.origin=-fraction;e.rate=rate;e.symbol_samples=136;e.chip_samples=34;e.sample_rate=48000;
    e.begin=e.native_begin=0;e.end=e.native_end=e.capture_end=250;e.left_guard=e.right_guard=1;return e;
}
struct Fence {std::uint64_t original_first=0,original_count=0;};
const auto fences=[](auto first,auto end,auto consume){for(std::uint64_t at=0;at<250;at+=17)
    if(at<end&&at+17>first)consume(Fence{at,std::min<std::uint64_t>(17,250-at)});};
void unsupported_selection(const original::Event& e,Map& map) {
    check(!rt::valid_event(e),"unsupported sparse observation event accepted");
    const auto measured=obs::measure(e,{});
    check(!measured.valid&&measured.failure==obs::Failure::event&&!measured.count&&!measured.runs,"unsupported observation measurement published");
    const auto bytes=obs::Selection::reservation(0);
    const auto selection=obs::build(e,{},bytes);
    check(!selection.valid&&selection.failure==obs::Failure::event&&!selection.count&&selection.runs.empty()&&!selection.contains(e.begin),
        "unsupported observation selection published");
    check(selection.working_bytes()<=bytes,"refused selection exceeded precharge");
    obs::PieceCache cache;unsigned fence_calls=0;bool denied=false;
    const auto counted_fences=[&](auto first,auto end,auto consume){++fence_calls;fences(first,end,consume);};
    try{(void)cache.evaluate(e,e.begin,map,counted_fences);}catch(const datapump::Error&){denied=true;}
    check(denied&&!fence_calls&&!cache.fence_lookups&&!cache.piece_builds,"unsupported direct cache visited processing metadata");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)obs::build(e,{},bytes,{},stop.get_token());}catch(const datapump::Error&){cancelled=true;}
    check(cancelled,"cancelled selection was published");
}
void scalar_oracle(long double rate,long double fraction) {
    auto e=event(rate,fraction);Map map;
    if(!sparse::environment()){unsupported_selection(e,map);return;}
    rt::Context context(1,0);context.add(0,e,map,fences);
    const auto pieces=context.pieces(0);auto selection=obs::build(e,pieces,obs::Selection::reservation(pieces.size()));
    check(selection.valid,"exact sparse observation selection unavailable");
    obs::PieceCache cache;std::uint64_t n=0;
    for(auto row=e.begin;row<e.end;++row) {
        const auto* piece=original::find_piece(pieces,row);check(piece!=nullptr,"oracle piece missing");
        const auto exact=sparse::evaluate(e,*piece,row);check(exact.valid(),"oracle metadata invalid");
        check(selection.contains(row)==exact.current,"sparse selected membership differs from Original row");
        const auto direct=cache.evaluate(e,row,map,fences);
        check(direct.valid()&&direct.current==exact.current&&direct.within==exact.within&&direct.gain==exact.gain&&direct.owner==exact.owner,
            "one-Piece direct cache changed processing restart/source/gain/owner");n+=exact.current;
    }
    check(n==selection.count&&selection.runs.size()==2,"sparse selection count/group merging changed");
    check(selection.working_bytes()<=obs::Selection::reservation(pieces.size()),"selected runs exceeded precharge");
    auto absent=e;absent.history_first=selection.runs.front().first+1;
    const auto missing=obs::build(absent,pieces,obs::Selection::reservation(pieces.size()));
    check(!missing.valid&&missing.failure==obs::Failure::history,"missing positive history was declared unobserved");
    obs::Limits limited;limited.maximum_runs=1;
    const auto capacity=obs::build(e,pieces,obs::Selection::reservation(pieces.size()),limited);
    check(!capacity.valid&&capacity.failure==obs::Failure::capacity,"selection grew past bounded run capacity");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)obs::build(e,pieces,obs::Selection::reservation(pieces.size()),{},stop.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled,"cancelled selection was published");
    auto broken=std::vector<original::Piece>(pieces.begin(),pieces.end());++broken.front().first;
    check(!obs::build(e,broken,obs::Selection::reservation(broken.size())).valid,"processing coverage gap accepted");
}
}
int main(){try{scalar_oracle(1,0);scalar_oracle(.9999997L,.000716L);scalar_oracle(1.0000003L,.999716L);
    if(!sparse::environment())std::cout<<"unsupported sparse arithmetic refusal verified\n";
    std::cout<<"Default point observation source cases passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
