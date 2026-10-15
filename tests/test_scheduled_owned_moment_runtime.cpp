#include "scheduled_owned_moment_runtime.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace o=d::scheduled_owned_moment_runtime;namespace s=d::scheduled_sparse_owned_spans;
namespace p=d::scheduled_original_partition;namespace c=d::scheduled_owned_moment_correction;
namespace poly=d::scheduled_polynomial_chip;namespace cert=d::scheduled_acquisition_certificate;
using Complex=std::complex<double>;
using datapump::modem::SchedHopCoordinate;using datapump::modem::SchedHopLocation;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
struct Map {SchedHopLocation locate(SchedHopCoordinate t)const{SchedHopLocation l;l.active=l.continuous=true;l.source=t;
    l.physical_end=std::numeric_limits<std::uint64_t>::max();return l;}
    SchedHopCoordinate source_to_physical(SchedHopCoordinate t,bool)const{return t;}};
std::array<Complex,2> q(long double source,std::uint64_t n){const auto t=static_cast<double>(source/n);
    return {Complex{.2+.3*t,.1-.17*t},Complex{-.23+.13*t,.16+.22*t}};}
std::vector<p::Piece> pieces(const p::Event& e){Map map;std::vector<p::Piece> out;auto at=e.begin;std::uint64_t id=1;
    while(at<e.end){const auto end=std::min(e.end,at+17);p::ProcessingFence fence{id++,77,at,end};
        while(at<end){auto part=s::make_piece(e,fence,at,map);at=part.end;out.push_back(part);}}
    return out;}
void inside(cert::Interval i,double x){check(i.bounded() && i.lo<=x && x<=i.hi,"Original field escaped sparse moments");}
void compare(const d::scheduled_cold_colored::FitIntervals& f,const d::CorrelationFit& r){check(f.count==r.count,"Original count changed");
    inside(f.xc,r.xc);inside(f.xs,r.xs);inside(f.cc,r.cc);inside(f.ss,r.ss);inside(f.cs,r.cs);inside(f.energy,r.energy);}
void run(std::uint64_t n,long double native_rate,long double rate,bool crop,bool degrade=false) {
    p::Event e;e.identity={101,202,303,1,0,0,7,9};e.epoch_sample=1000;e.anchor={1000,0};
    e.origin=-.0625L;e.rate=rate;e.symbol_samples=n;e.chip_samples=34;e.sample_rate=6000;
    e.displacement=.25L;e.left_guard=e.right_guard=1;e.begin=e.native_begin=crop?3:0;
    e.end=e.native_end=e.capture_end=n;e.history_first=0;
    auto native=e;native.rate=native_rate;native.origin=.0625L;
    const d::scheduled_cold_colored::SourceRows::Key key{native.origin,0,0,0,1000,native_rate};
    d::scheduled_cold_colored::SourceRows source(key,512,n+4);source.window_halo=true;Map map;
    for(std::uint64_t row=0;row<n+1;++row){const auto tick=p::tick(native,row);const auto z=map.locate(tick).source.distance(native.anchor);
        source.append(row+512,q(z,n),z,1,z>=0 && z<n);}
    source.seal();poly::SourceModels models(source,n,poly::SourceModels::NativeTemplateTag{});
    const auto all=pieces(e);
    const s::NativeInventoryView<poly::SourceModels::NativeOwnerSpan> inventory{s::source_identity(e.identity),native_rate,n,34,models.native_spans,true};
    const auto proof=s::partition(e,all,inventory,-512);check(proof.complete,"cross-clock sparse partition failed");
    check(proof.edit_rows>0,"fixture has no membership edits");
    std::vector<c::RowSample> rows(n);double energy=0;
    Complex carrier;const auto step=std::polar(1.,.71);
    for(std::size_t i=0;i<n;++i){if(i%512==0)carrier=std::polar(1.,.71*(i-i%512));
        rows[i].pcm=static_cast<float>(.4*std::sin(.27*i)+.15*std::cos(.77*i));rows[i].oscillator=carrier;
        energy+=static_cast<double>(rows[i].pcm)*rows[i].pcm;carrier*=step;}
    const auto sample=[&](std::uint64_t row)->std::optional<c::RowSample>{if(row>=rows.size())return {};return rows[row];};
    std::uint64_t fetches=0;
    const auto fetch=[&](const poly::SourceModels::Cell& cell,std::uint64_t first){++fetches;
        std::array<float,34> pcm;std::array<Complex,34> osc;
        check(first+34<=n,"native crop proof failed");
        for(std::size_t j=0;j<34;++j){pcm[j]=rows[first+j].pcm;osc[j]=rows[first+j].oscillator;}
        auto result=poly::contract_math(cell.model,poly::build_moments(pcm,osc));
        // A valid but unusably wide second-bit native Gram enclosure models a
        // conservative FFT cell. It still encloses the actual native fields.
        if(degrade && cell.owner==1)result.intervals[1].cc={-100,100};
        return result;};
    const c::Identity id{1,2,3,4,5,6};const o::Bounds bounds{3,16./n,.01,cert::up(energy+1),0,2,512};
    o::Counters work;const auto result=o::contract(source,models,e,all,proof,-512,id,bounds,fetch,sample,work);
    check(result.valid && work.polynomial_cells>0 && fetches>0,"sparse moment producer did not complete");
    check(work.direct_rows<n && work.edit_rows==proof.edit_rows,"sparse producer work accounting mismatch");
    if(degrade) {
        check(work.polynomial_refinements==1 && result.first_refinement.owner==1 && result.first_refinement.bit==1 &&
            result.first_refinement.polynomial && result.first_refinement.failure==o::Failure::projection_rank,
            "failed polynomial owner was not refined or accurately diagnosed");
        o::Counters diagnostic_work;
        const auto diagnostic=o::contract(source,models,e,all,proof,-512,id,bounds,fetch,sample,diagnostic_work,{},false);
        check(!diagnostic.valid && diagnostic.failure==o::Failure::projection_rank && diagnostic.failure_owner==1 &&
            diagnostic.terminal.owner==1 && diagnostic.terminal.bit==1,"diagnostic-only first failure lost its exact predicate");
        check(diagnostic.chips[0].count==34 && diagnostic.chips[1].count==34,
            "failed second bit committed first-bit or whole partial state");
    }
    std::array<d::CorrelationFit,2> whole;std::map<std::uint64_t,std::array<d::CorrelationFit,2>> owner_fits;
    // Independently execute Original per-call prefix subtraction and row q.
    for(const auto& part:all){d::CorrelationProjection prefix;
        for(auto row=part.first;row<part.end;++row){const auto x=static_cast<double>(rows[row].pcm),a=rows[row].oscillator.real(),b=rows[row].oscillator.imag();
            const auto old=prefix;prefix.xc+=x*a;prefix.xs+=x*b;prefix.cc+=a*a;prefix.ss+=b*b;prefix.cs+=a*b;prefix.energy+=x*x;
            const auto value=s::evaluate(e,part,row);check(value.valid(),"Original oracle invalid");if(!value.current)continue;
            const auto values=q(value.within,n);const auto projection=prefix-old;
            for(unsigned bit=0;bit<2;++bit){whole[bit].add(projection,values[bit],1);owner_fits[value.owner][bit].add(projection,values[bit],1);}
        }}
    for(unsigned bit=0;bit<2;++bit){compare(result.whole[bit],whole[bit]);double projected=0;std::uint64_t rank=0,count=0;
        for(const auto& [owner,fits]:owner_fits){(void)owner;const auto expected=d::CorrelationChipEvidence::projection(fits[bit]);
            projected+=expected.first;rank+=expected.second;count+=fits[bit].count;}
        check(result.chips[bit].count==count && result.chips[bit].rank==rank,"Original owner rank/count changed");
        inside(result.chips[bit].projected,projected);}
    auto wrong=proof;wrong.native_rate=std::nextafter(native_rate,2.L);o::Counters ignored;
    check(!o::contract(source,models,e,all,wrong,-512,id,bounds,fetch,sample,ignored).valid,"stale actual source rate accepted");
    std::stop_source stop;stop.request_stop();bool stopped=false;
    try{(void)o::contract(source,models,e,all,proof,-512,id,bounds,fetch,sample,ignored,stop.get_token());}catch(const std::exception&){stopped=true;}
    check(stopped,"sparse moment cancellation ignored");
}
int main(){try{if(!s::environment()){std::cout<<"qualified LD64 environment unavailable\n";return 77;}
    run(136,1,1,false);run(136,1,1,false,true);run(8704,1,1+1e-6L,true);run(8704,1+1e-6L,1-1e-6L,false);
    std::cout<<"sparse corrected moments Original field/count/rank/clock/crop/cancellation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
