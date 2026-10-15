#include "scheduled_default_point_observation.hpp"
#include "scheduled_default_point_piece_cache.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace obs=d::scheduled_default_point_observation;
namespace runtime=d::scheduled_default_linear_runtime;
namespace original=d::scheduled_original_partition;
using namespace datapump::modem;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
struct Map {
    bool ramp=false;
    SchedHopLocation locate(SchedHopCoordinate tick)const {
        SchedHopLocation l;l.active=true;l.continuous=!ramp;l.source=tick;
        l.physical_end=l.source_end=UINT64_MAX;l.rise_begin=0;l.flat_begin=1;l.flat_end=1000000;l.fall_end=1000001;return l;
    }
    SchedHopCoordinate source_to_physical(SchedHopCoordinate p,bool)const{return p;}
};
struct Fence {std::uint64_t original_first=0,original_count=0;};
auto single_fence(std::uint64_t end){return [end](auto,auto,auto emit){emit(Fence{0,end});};}
FhssPowerShape shape(std::uint32_t fs) {
    FhssPlan p;p.channels=2;p.shifts[0]=30000000;p.shifts[1]=31000000;p.dwell_seconds=.4;
    p.power_ramps=p.mandatory_ramps=p.smooth_ramps=true;p.clock_error_seconds=p.peer_clock_error_seconds=.01;
    p.audio_error_seconds=.05;p.ramp_seconds=.006;return FhssPowerShape(p,fs,64,12240);
}
original::Event event(std::uint32_t fs=6121,long double rate=1,long double origin=0) {
    original::Event e;e.identity={1,2,3,1,0,0,1,1};e.sample_rate=fs;e.epoch_sample=194ULL*fs;
    e.anchor={e.epoch_sample,0};e.symbol_samples=2ULL*fs-(2ULL*fs)%17;e.chip_samples=17;e.rate=rate;e.origin=origin;
    e.begin=e.native_begin=static_cast<std::uint64_t>(std::max(0.L,std::ceil(origin)));
    e.end=e.native_end=e.capture_end=static_cast<std::uint64_t>(std::ceil(origin+e.symbol_samples/rate));
    e.left_guard=e.right_guard=1;e.fhss=shape(fs);return e;
}
SchedHopCoordinate coordinate(long double x){const auto i=std::floor(x);return {static_cast<std::uint64_t>(i),x-i};}
struct Raw {bool current=false;long double within=0;double gain=0;};
Raw scalar(const original::Event& e,const original::Piece& p,std::uint64_t row) {
    Raw r;if(!p.location0.active)return r;
    r.within=p.source0+static_cast<long double>(row-p.anchor_first)*p.rate;
    if(r.within<0||r.within>=e.symbol_samples)return r;
    const auto physical=p.tick0.add_signed(static_cast<long double>(row-p.anchor_first)*p.rate);
    r.gain=SchedHopSourceMap::gain(p.location0,physical);
    r.gain*=e.fhss.gain(0,physical.sample%(2ULL*e.sample_rate)+physical.fraction,e.sample_rate);
    r.current=r.gain>0;return r;
}
void exact_runs(long double rate,long double origin,std::uint32_t fs) {
    auto e=event(fs,rate,origin);Map map;const auto fences=single_fence(e.end);runtime::Context context(1,0);context.add(0,e,map,fences);
    const auto pieces=context.pieces(0);check(pieces.size()==1,"long FHSS fixture lost one-Piece witness");
    const auto measured=obs::measure(e,pieces);check(measured.valid&&measured.runs>pieces.size(),"FHSS run bound still limited by processing Piece count");
    check(measured.fhss_flat_spans&&measured.fhss_zero_spans&&measured.fhss_rows,"FHSS fixture lost flat/quiet/ramp recursive coverage");
    const auto selected=obs::build(e,pieces,measured,obs::Selection::reservation(measured.runs));
    check(selected.valid&&selected.probes<=262144,"FHSS selection exceeded qualified probe cap");
    check(selected.runs.capacity()<=2*measured.runs,"retained FHSS intervals grew with processing Piece count");
    obs::PieceCache cache;std::uint64_t count=0,quiet=0;
    for(auto row=e.begin;row<e.end;++row) {
        const auto* p=original::find_piece(pieces,row);check(p,"scalar FHSS Piece missing");const auto expected=scalar(e,*p,row);
        check(selected.contains(row)==expected.current,"recursive FHSS selection differs from actual combined-gain row");
        const auto actual=cache.evaluate(e,row,map,fences);check(actual.valid()&&actual.current==expected.current,"FHSS direct cache current mask differs");
        if(expected.current)check(actual.within==expected.within&&actual.gain==expected.gain,"FHSS direct cache changed source/product gain");
        count+=expected.current;quiet+=!expected.current;
    }
    check(count==selected.count&&count&&quiet,"FHSS selected count or nonvacuous mask changed");
    auto unavailable=e;unavailable.history_first=selected.runs.front().first;
    const auto quiet_history=obs::build(unavailable,pieces,obs::Selection::reservation(measured.runs));
    check(quiet_history.valid&&quiet_history.count==selected.count,"FHSS-proved quiet prefix incorrectly required PCM history");
    ++unavailable.history_first;
    const auto positive_history=obs::build(unavailable,pieces,obs::Selection::reservation(measured.runs));
    check(!positive_history.valid&&positive_history.failure==obs::Failure::history&&positive_history.runs.empty(),
        "missing positive FHSS history was declared unobserved");
    obs::Limits capped;capped.maximum_probes=1;
    const auto fail=obs::build(e,pieces,obs::Selection::reservation(measured.runs),capped);
    check(!fail.valid&&fail.count==0&&fail.runs.empty(),"FHSS probe-cap abort published partial support");
    capped={};capped.maximum_runs=1;
    check(!obs::measure(e,pieces,capped).valid,"FHSS merged-run cap ignored");
    const auto partial=obs::build(e,pieces,measured,obs::Selection::reservation(measured.runs),capped);
    check(!partial.valid&&partial.count==0&&partial.runs.empty(),"FHSS second-pass failure retained partial selected support");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)obs::measure(e,pieces,{},stop.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled,"cancelled FHSS selection published");
}
void zero_branch_association() {
    auto e=event();e.fhss.lag_center=0;const auto two=2.L*e.sample_rate,period=two/5;
    const auto quiet=e.fhss.quiet;
    const auto early_a=coordinate(quiet-1),early_b=coordinate(quiet);
    const auto late_a=coordinate(period-quiet),late_b=coordinate(period-quiet+.5L);
    check(obs::implementation::fhss_zero_span(e,early_a,early_b),"exact early edge==0 declined zero proof");
    check(obs::implementation::fhss_zero_span(e,late_a,late_b),"exact late edge==0 declined zero proof");
    check(e.fhss.gain(0,early_b.sample+early_b.fraction,e.sample_rate)==0&&
          e.fhss.gain(0,late_a.sample+late_a.fraction,e.sample_rate)==0,"zero-edge fixture no longer executes zero branch");
    check(!obs::implementation::fhss_zero_span(e,early_a,late_b),"zero endpoints hid positive half-hop interior");
    check(!obs::implementation::fhss_zero_span(e,coordinate(two-1),coordinate(two+.5L)),"2Fs wrap falsely certified zero");
    check(!obs::implementation::fhss_zero_span(e,coordinate(0),coordinate(period+1)),"hop ordinal crossing falsely certified zero");
    e.fhss.lag_center=two-10;
    check(!obs::implementation::fhss_zero_span(e,coordinate(5),coordinate(20)),"raw lag wrap falsely certified zero");
}
void combined_underflow_and_source_before_gain() {
    auto e=event();e.epoch_sample=0;e.anchor={0,0};e.origin=-1e-107L;e.symbol_samples=34;e.chip_samples=17;
    e.begin=e.native_begin=0;e.end=e.native_end=e.capture_end=34;
    e.fhss.period=2.L*e.sample_rate/5;e.fhss.quiet=0;e.fhss.support=1;e.fhss.lag_center=0;
    Map map;map.ramp=true;runtime::Context context(1,0);context.add(0,e,map,single_fence(e.end));const auto pieces=context.pieces(0);
    const auto physical=pieces[0].tick0;
    const auto sched_gain=SchedHopSourceMap::gain(pieces[0].location0,physical);
    const auto fhss_gain=e.fhss.gain(0,physical.sample%(2ULL*e.sample_rate)+physical.fraction,e.sample_rate);
    check(sched_gain>0&&fhss_gain>0&&sched_gain*fhss_gain==0,"combined product-underflow witness became vacuous");
    const auto selected=obs::build(e,pieces,obs::Selection::reservation(8));
    check(selected.valid&&!selected.contains(0)&&selected.contains(1),"FHSS selection inferred positive product from positive factors");
    auto exhausted=e;exhausted.end=exhausted.native_end=exhausted.capture_end=200;
    exhausted.origin=0;exhausted.fhss.quiet=1000;exhausted.fhss.support=1;Map continuous;
    runtime::Context invalid(1,0);invalid.add(0,exhausted,continuous,single_fence(exhausted.end));
    const auto result=obs::build(exhausted,invalid.pieces(0),obs::Selection::reservation(8));
    check(!result.valid&&result.failure==obs::Failure::source_coverage,"FHSS zero mask hid Original source exhaustion");
}
}
int main(){try{if(!d::scheduled_sparse_owned_spans::environment())return 77;
    exact_runs(1,0,6121);exact_runs(.9999997L,.000716L,6121);exact_runs(1.0000003L,.999716L,48000);
    zero_branch_association();combined_underflow_and_source_before_gain();std::cout<<"Default point FHSS source cases passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
