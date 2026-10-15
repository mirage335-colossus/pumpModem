#include "scheduled_cold_colored_certificate.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
namespace t=d::scheduled_acquisition_tile;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void contains(c::Interval value,double x,const char* message){check(value.lo<=x && x<=value.hi,message);}
g::FitIntervals exact(const d::CorrelationFit& f,bool zero=false){return g::enclose(f,0,0,0,zero);}

void observation_mask_lattice() {
    check(g::observation_mask_lattice(.5L,0,.001L),"interior physical/source lattice rejected");
    check(!g::observation_mask_lattice(.5L,0,.5L),"unbounded displacement acquired exact mask");
    check(!g::observation_mask_lattice(.125L,0,.125L),"equal mask edge margin acquired proof");
    check(g::observation_mask_lattice(.125L,0,.124L),"strict mask interior lost proof");
    check(!g::observation_mask_lattice(.5L,.5L,1e-8L),"source edge hidden by physical margin");
    check(!g::observation_mask_lattice(.5L,0,.001L,.001L),"FHSS equal edge margin acquired proof");
    check(g::observation_mask_lattice(.5L,0,.001L,.002L),"strict FHSS margin lost proof");
    check(!g::observation_mask_lattice(0,0,0),"integral arithmetic inferred from zero displacement");
    check(!g::observation_mask_lattice(.5L,0,-1),"negative displacement acquired mask proof");
    const auto nan=std::numeric_limits<long double>::quiet_NaN();
    check(!g::observation_mask_lattice(nan,0,.001L),"nonfinite physical lattice acquired proof");
    check(!g::observation_mask_lattice(.5L,0,.001L,nan),"unknown FHSS margin acquired proof");
}

void exact_mask_raw_certificate() {
    // A global max-PCM mask debit can erase a noise-like window's energy bound.
    // Exact mask proof restores the SAME original raw upper, without needing a
    // colored-excess predicate. It must never turn proxy count equality alone
    // into proof or change the prior fallback when any producer fact is absent.
    d::CorrelationFit whole;whole.count=128;whole.energy=128;whole.cc=whole.ss=64;
    whole.xc=4;whole.xs=1;
    c::FitCertificate tile;tile.valid=true;tile.dot_radius={0,0};tile.gram_radius={0,0};
    tile.energy_lower=tile.energy_upper=whole.energy;
    const c::ReferenceErrors original{0,0,0,1e-10,true};
    g::Summary chips;chips.count=whole.count;chips.owner_set_certified=true;chips.rank=32;
    chips.projected=c::Interval::point(64);
    auto stripped=tile;stripped.energy_lower=0;
    auto inflated=whole;inflated.count+=16;
    const auto old=c::score_upper(inflated,stripped,0,original);
    check(!std::isfinite(old),"mask-debit fixture ceased exercising original refinement");
    const auto unchanged=g::rejection_upper(old,whole,tile,0,original,chips,40,{1e-10,true});
    check(!std::isfinite(unchanged),"fixture unexpectedly obtained a colored rejection");
    const auto tight=g::exact_mask_raw_upper(old,whole,tile,0,original,chips,{true,whole.count});
    check(std::isfinite(tight) && tight<1 && tight>=whole.score(),"exact-mask raw bound missed or excluded original score");
    check(g::exact_mask_raw_upper(old,whole,tile,0,original,chips)==old,"proxy count equality forged original-mask proof");
    check(g::exact_mask_raw_upper(old,whole,tile,0,original,chips,{true,whole.count+1})==old,
          "different original count acquired a zero-strip raw bound");
    auto wrong=chips;--wrong.count;
    check(g::exact_mask_raw_upper(old,whole,tile,0,original,wrong,{true,whole.count})==old,
          "incomplete chip count acquired exact raw evidence");
    wrong=chips;wrong.owner_set_certified=false;
    check(g::exact_mask_raw_upper(old,whole,tile,0,original,wrong,{true,whole.count})==old,
          "unknown owner identity acquired exact raw evidence");
    wrong=chips;wrong.valid=false;
    check(g::exact_mask_raw_upper(old,whole,tile,0,original,wrong,{true,whole.count})==old,
          "invalid chip summary bypassed original fallback");
    check(g::exact_mask_raw_upper(old,whole,stripped,0,original,chips,{true,whole.count})==old,
          "nonpositive certified observed energy bypassed original fallback");
}

void guard_certificate() {
    d::CorrelationFit whole;whole.count=87040;whole.energy=87040;whole.cc=whole.ss=43520;
    whole.xc=std::sqrt(120*whole.cc);
    g::Summary summary;summary.projected=c::Interval::point(69632);summary.rank=5120;summary.count=whole.count;
    summary.owner_set_certified=true;
    d::CorrelationChipEvidence original;original.projected_energy=summary.projected.lo;original.projected_rank=summary.rank;
    const g::MathContract math{1e-10,true};
    const auto bound=g::guarded_upper(exact(whole),summary,40,math);
    check(whole.score()>40,"fixture no longer needs colored guard");
    check(original.colored_excess(whole,40),"original colored fixture not active");
    check(bound.colored && bound.score_upper<40,"original colored rejection not certified");
    check(bound.score_upper>=original.score(whole),"guarded original score escaped upper");
    summary.owner_set_certified=false;
    check(!g::guarded_upper(exact(whole),summary,40,math).colored,"missing owner-set proof certified rank");
    summary.owner_set_certified=true;
    check(!g::guarded_upper(exact(whole),summary,40,{}).colored,"unknown libm contract certified");
    check(!g::guarded_upper(exact(whole),summary,40,{1e-10,false}).colored,"unknown sqrt contract certified");
    const auto count_proxy=g::enclose(whole,1e-8,1e-8,1e-8,false,16);
    const auto proxy_bound=g::guarded_upper(count_proxy,summary,40,math);
    check(proxy_bound.colored && proxy_bound.score_upper<40,"bounded whole count rejected proxy opportunity");
    for(int delta=-16;delta<=16;++delta){auto actual=whole;actual.count+=delta;
        check(original.colored_excess(actual,40),"original proxy-count guard changed");
        check(proxy_bound.score_upper>=original.score(actual),"count proxy escaped guarded score upper");}
    summary.nested=false;check(!g::guarded_upper(exact(whole),summary,40,math).colored,"non-nested evidence certified");
    summary.nested=true;summary.projected=c::Interval::point(120);
    check(!g::guarded_upper(exact(whole),summary,40,math).colored,"coherent signal claimed colored excess");
    summary.projected=c::Interval::point(69632);summary.rank=whole.count-100;
    check(!g::guarded_upper(exact(whole),summary,40,math).colored,"residual lower gate weakened");
}

void retention_rejection() {
    // This cold event is already below its ORIGINAL charged admission T,
    // but its raw score would require deferred exact history at retain=5.
    // The existing colored proof may resolve that history only if its guarded
    // upper is also below five; five never replaces T in colored_excess.
    constexpr double threshold=40,retention=5;
    d::CorrelationFit whole;whole.count=87040;whole.energy=87040;whole.cc=whole.ss=43520;
    whole.xc=std::sqrt(60*whole.cc);
    g::Summary summary;summary.projected=c::Interval::point(69632);summary.rank=5120;summary.count=whole.count;
    summary.owner_set_certified=true;
    d::CorrelationChipEvidence original;original.projected_energy=summary.projected.lo;original.projected_rank=summary.rank;
    const auto raw=whole.score();
    check(raw>retention && raw<threshold,"retention fixture left the raw-rejected history band");
    c::FitCertificate tile;tile.valid=true;tile.dot_radius={0,0};tile.gram_radius={0,0};tile.energy_lower=tile.energy_upper=whole.energy;
    const c::ReferenceErrors reference{0,0,0,1e-10,true};
    const auto upper=g::rejection_upper(raw,whole,tile,0,reference,summary,threshold,{1e-10,true});
    check(original.colored_excess(whole,threshold),"retention fixture Original guard inactive at charged T");
    check(upper<retention && upper>=original.score(whole),"charged-T guard did not safely resolve nonretained history");
    summary.owner_set_certified=false;
    check(g::rejection_upper(raw,whole,tile,0,reference,summary,threshold,{1e-10,true})==raw,
        "missing owner proof resolved deferred history");
    summary.owner_set_certified=true;summary.projected=c::Interval::point(60);
    check(g::rejection_upper(raw,whole,tile,0,reference,summary,threshold,{1e-10,true})==raw,
        "coherent-only evidence resolved deferred history through colored guard");
}

void rank_and_membership() {
    d::CorrelationFit cell;cell.count=34;cell.cc=cell.ss=17;cell.xc=12;cell.xs=-8;cell.energy=34;
    const auto native=exact(cell);const auto projection=g::projection(native);
    const auto original=d::CorrelationChipEvidence::projection(cell);
    check(projection.valid && projection.nested && projection.rank==original.second,"ordinary full-rank cell unknown");
    contains(projection.energy,original.first,"same-order cell projection not enclosed");
    const auto a=g::sample_allowance(.5f,{.6,.2},{1,0},.001,1e-10);
    const auto b=g::sample_allowance(.2f,{.1,-.2},{0,1},.001,1e-10);
    const auto edge=g::one_edge(a,b),both=g::two_edges(edge,edge);
    check(edge.count==1 && both.count==2,"adjacent union double-counted a boundary");
    const c::ReferenceErrors fields{1e-10,1e-10,1e-10,1e-10,true};
    const auto uncertain=g::membership_enclose(cell,fields,both);
    check(uncertain.count_lower==32 && uncertain.count_upper==36,"local membership count interval incorrect");
    check(g::projection(uncertain).valid,"bounded adjacent samples disabled well-conditioned chip");
    // Enumerate all two-edge membership choices against the field interval.
    for(int left=-1;left<=1;++left)for(int right=-1;right<=1;++right){auto f=cell;
        const auto alter=[&](int sign,float x,std::complex<double> q,std::complex<double> o){
            const auto v=q*o;f.xc+=sign*x*v.real();f.xs+=sign*x*v.imag();
            f.cc+=sign*v.real()*v.real();f.ss+=sign*v.imag()*v.imag();f.cs+=sign*v.real()*v.imag();
            f.energy+=sign*double(x)*x;f.count+=sign;};
        alter(left,.5f,{.6,.2},{1,0});alter(right,.5f,{.6,.2},{1,0});
        contains(uncertain.xc,f.xc,"membership dot not enclosed");contains(uncertain.cc,f.cc,"membership Gram not enclosed");
        contains(uncertain.energy,f.energy,"membership energy not enclosed");
        check(f.count>=uncertain.count_lower && f.count<=uncertain.count_upper,"membership count escaped");
        const auto p=d::CorrelationChipEvidence::projection(f);
        const auto bound=g::projection(uncertain);check(p.second==bound.rank,"membership rank proof failed");
        contains(bound.energy,p.first,"membership projected energy escaped");
    }
    const auto owner=g::owner_range({33.999,34.001},34,87040);
    check(owner.valid && owner.lower==0 && owner.upper==1,"fractional source boundary union failed");
    check(!g::owner_range({-1e-4,1e-4},34,87040).valid,"symbol-edge uncertainty silently accepted");
    check(!g::owner_range({1,200},34,87040).valid,"wide clock displacement silently accepted");
    cell.count=1;
    const auto singleton=g::projection(exact(cell));
    check(singleton.valid && singleton.nested && singleton.rank==1 &&
        singleton.energy.lo==0 && singleton.energy.hi>=cell.energy,
        "bounded singleton claimed useful libm energy beyond its full [0,E] clamp");
    contains(singleton.energy,d::CorrelationChipEvidence::projection(cell).first,
        "bounded singleton excluded the original rank-one result");
    auto unbounded=cell;unbounded.xc=1e101;
    check(!g::projection(exact(unbounded)).valid,"unbounded rank-one libm branch accepted");
    cell={};cell.count=34;cell.energy=34;
    const auto zero=g::projection(exact(cell,true));check(zero.valid && zero.nested && zero.rank==0,"exact absent pulse not retained");
    check(!g::projection(g::enclose(cell,1e-5,1e-5,1e-5)).valid,"zero proxy claimed original zero basis");
}

void direct_fragments() {
    constexpr std::size_t samples=238,chip=34;const std::array<std::size_t,3> starts{0,7,17};
    std::vector<float> pcm(samples+starts.back());std::vector<std::complex<double>> oscillator(pcm.size());
    std::vector<std::uint8_t> observed(pcm.size(),1);observed[4]=0;
    std::vector<std::array<std::complex<double>,2>> q(samples);std::vector<std::uint8_t> mask(samples,1);
    std::vector<std::uint64_t> owners(samples);
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=static_cast<float>(.4*std::sin(.173*i)+.3*std::cos(.07*i));
        oscillator[i]=std::polar(1.,.93*i);}
    for(std::size_t i=0;i<samples;++i){owners[i]=i/chip;const auto gain=i<11?double(i)/11:1.;
        q[i]={gain*std::polar(.7,.21*i),gain*std::polar(.5,-.31*i)};
        if(i>=48 && i<65)mask[i]=0; // null aperture splits a retained chip
        if(i>=136 && i<170)q[i]={}; // zero basis still counts physical noise
    }
    std::array<g::Direct::Errors,starts.size()> errors;
    for(auto& lane:errors)for(auto& bit:lane)bit={1e-8,1e-8,1e-8,1e-10,true};
    g::Direct direct(starts.size(),true,true);
    // Fragment and caller-like cuts do not choose chip evidence boundaries.
    for(std::size_t begin=0;begin<samples;){const auto end=std::min(samples,begin+47);
        direct.add({std::span<const float>(pcm).subspan(begin,end-begin+starts.back()),
            std::span<const std::complex<double>>(oscillator).subspan(begin,end-begin+starts.back()),
            std::span<const std::uint8_t>(observed).subspan(begin,end-begin+starts.back()),begin},
            {std::span<const std::array<std::complex<double>,2>>(q).subspan(begin,end-begin),
             std::span<const std::uint8_t>(mask).subspan(begin,end-begin),begin},starts,
             std::span<const std::uint64_t>(owners).subspan(begin,end-begin),errors);begin=end;}
    const auto summaries=direct.finish(errors);
    for(std::size_t j=0;j<starts.size();++j){std::array<d::CorrelationFit,2> whole{};
        std::array<d::CorrelationChipEvidence,2> evidence{};
        for(std::size_t i=0;i<samples;++i){const auto at=i+starts[j];if(!mask[i] || !observed[at])continue;
            const auto x=double(pcm[at]),a=oscillator[at].real(),b=oscillator[at].imag();
            const d::CorrelationProjection p{x*a,x*b,a*a,b*b,a*b,x*x};
            for(unsigned bit=0;bit<2;++bit){whole[bit].add(p,q[i][bit],1);evidence[bit].add(p,q[i][bit],1,owners[i]);}}
        for(unsigned bit=0;bit<2;++bit){const auto tail=d::CorrelationChipEvidence::projection(evidence[bit].cell);
            const auto rank=evidence[bit].projected_rank+tail.second;
            const auto energy=evidence[bit].projected_energy+tail.first;
            const auto& bound=summaries[j][bit];check(bound.valid && bound.nested,"direct split-chip evidence unknown");
            check(bound.count==whole[bit].count && bound.rank==rank,"direct changed original count/rank");
            contains(bound.projected,energy,"original cumulative chip energy escaped direct bound");}
    }
    bool stopped=false;try{direct.add({}, {},starts,{},errors);}catch(const datapump::Error&){stopped=true;}
    check(stopped,"continued finalized chip created second projection");
    g::Direct unproved(starts.size(),false);unproved.add({pcm,oscillator,observed,0},{q,mask,0},starts,owners,errors);
    for(const auto& lane:unproved.finish(errors))for(const auto& bit:lane)check(!bit.valid,"unproved source ownership produced valid evidence");
}

int main() {
    try {check(c::environment<double>(),"strict FP64 environment required");
        observation_mask_lattice();exact_mask_raw_certificate();guard_certificate();retention_rejection();rank_and_membership();direct_fragments();
        std::cout<<"scheduled cold colored certificate checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
