#include "scheduled_cold_colored_proxy_runtime.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
namespace t=d::scheduled_acquisition_tile;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void contains(c::Interval v,double x,const char* m){check(v.lo<=x && x<=v.hi,m);}
void multistart_domains(){
    constexpr std::size_t N=133,C=34;const std::array<std::size_t,4> starts{0,7,17,29};
    const std::array<g::RuntimeProxy::Domain,4> crops{{{5,34},{35,125},{70,128},{90,90}}};
    std::vector<float> pcm(N+starts.back());std::vector<std::complex<double>> oscillator(pcm.size());
    std::vector<std::uint8_t> observed(pcm.size(),1),mask(N,1);
    const std::array<std::complex<double>,2> q{std::complex<double>{.5,.25},std::complex<double>{-.5,.75}};
    std::vector<std::array<std::complex<double>,2>> rows(N,q);
    std::vector<std::array<double,2>> norms(N,{c::norm(rows[0][0]),c::norm(rows[0][1])});
    std::vector<g::ProxyRow> geometry(N);double energy=0;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=static_cast<float>(std::sin(.31*i)+.1*std::cos(.11*i));
        oscillator[i]=std::polar(1.,.731*i);energy+=double(pcm[i])*pcm[i];}
    for(std::size_t i=0;i<N;++i)geometry[i]={i/C,0,1e-12};
    const auto union_energy=c::up(energy/c::down(1-c::gamma(pcm.size(),std::numeric_limits<double>::epsilon()/2)));
    const auto batch=[&](bool fast=true) {
        g::RuntimeProxy runtime(starts.size(),C,union_energy,1e-6,2,512,0,fast);
        for(std::size_t first=0;first<N;) {
            const auto n=std::min<std::size_t>(47,N-first),length=n+starts.back();
            std::array<g::RuntimeProxy::Domain,4> domains{};
            for(std::size_t j=0;j<starts.size();++j)domains[j]={
                crops[j].first>first?std::min(n,crops[j].first-first):0,
                crops[j].end>first?std::min(n,crops[j].end-first):0};
            runtime.add({std::span<const float>(pcm).subspan(first,length),
                std::span<const std::complex<double>>(oscillator).subspan(first,length),
                std::span<const std::uint8_t>(observed).subspan(first,length),1000+first},
                {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,n),
                 std::span<const std::uint8_t>(mask).subspan(first,n),1000+first},starts,
                std::span<const g::ProxyRow>(geometry).subspan(first,n),{},
                std::span<const std::array<double,2>>(norms).subspan(first,n),domains);
            first+=n;
        }
        auto result=runtime.finish();
        if(observed[50])check(runtime.sample_pairs()==177,"domain sample-pair count includes excluded rows");
        else check(runtime.sample_pairs()==176,"missing inside sample did not remove one native pair");
        bool repeated=false;try{(void)runtime.finish();}catch(const std::exception&){repeated=true;}
        check(repeated,"domain terminal cell was finalized twice");return result;
    };
    const auto scalar=[&](std::size_t j,bool fast) {
        g::RuntimeProxy runtime(1,C,union_energy,1e-6,2,512,0,fast);const std::array<std::size_t,1> zero{0};
        for(std::size_t first=0;first<N;first+=std::min<std::size_t>(47,N-first)) {
            const auto lo=std::max(first,crops[j].first),hi=std::min(first+std::min<std::size_t>(47,N-first),crops[j].end);
            if(lo>=hi)continue;const auto n=hi-lo,at=lo+starts[j];
            runtime.add({std::span<const float>(pcm).subspan(at,n),
                std::span<const std::complex<double>>(oscillator).subspan(at,n),
                std::span<const std::uint8_t>(observed).subspan(at,n),1000+lo},
                {std::span<const std::array<std::complex<double>,2>>(rows).subspan(lo,n),
                 std::span<const std::uint8_t>(mask).subspan(lo,n),1000+lo},zero,
                std::span<const g::ProxyRow>(geometry).subspan(lo,n),{},
                std::span<const std::array<double,2>>(norms).subspan(lo,n));
        }
        return runtime.finish().front();
    };
    const auto compare=[&](bool fast) {
        const auto result=batch(fast);
        for(std::size_t j=0;j<starts.size();++j){const auto independent=scalar(j,fast);
            for(unsigned bit=0;bit<2;++bit){const auto& a=result[j][bit];const auto& b=independent[bit];
                check(a.valid==b.valid && a.nested==b.nested && a.owner_set_certified==b.owner_set_certified,
                    "domain lane validity differs from independent crop");
                if(a.valid)check(a.count==b.count && a.rank==b.rank &&
                    a.projected.lo==b.projected.lo && a.projected.hi==b.projected.hi,
                    "domain lane differs from independent terminal/cell reductions");}}
        return result;
    };
    for(const auto fast:{false,true}) {
        auto result=compare(fast);const std::array<std::uint64_t,4> counts{29,90,58,0},ranks{2,6,4,0};
        for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit)
            check(result[j][bit].valid && result[j][bit].count==counts[j] && result[j][bit].rank==ranks[j],
                "domain count/rank changed");
        observed[1]=0;result=compare(fast); // unavailable for every in-domain lane
        for(const auto& pair:result)for(const auto& value:pair)check(value.valid,"outside missing row invalidated a lane");
        observed[50]=0;result=compare(fast);
        for(unsigned bit=0;bit<2;++bit){check(!result[1][bit].valid,"inside missing row was certified");
            check(result[0][bit].valid && result[2][bit].valid && result[3][bit].valid,"inside missing row invalidated other domains");}
        observed[1]=observed[50]=1;
    }
    // A transfer row outside every lane must not invalidate an empty old cell
    // or count an unavailable row. A transfer inside a lane keeps old behavior.
    geometry[34].membership=g::ProxyRow::transfer;geometry[68].membership=g::ProxyRow::transfer;
    observed[34]=0; // physical row34: all lane source coordinates are outside crops
    (void)compare(true);observed[34]=1;

    // Stop before a fragment and retry it: the failed add must not
    // advance fragment order, counts, error terms or terminal ownership.
    g::RuntimeProxy cancelled_runtime(1,C,union_energy,1e-6,2,512,0,true);
    const std::array<std::size_t,1> zero{0};const std::array<g::RuntimeProxy::Domain,1> full{{{0,34}}};
    const auto input=t::Input{std::span<const float>(pcm).first(34),std::span<const std::complex<double>>(oscillator).first(34),{},2000};
    const auto fragment=t::Fragment{std::span<const std::array<std::complex<double>,2>>(rows).first(34),std::span<const std::uint8_t>(mask).first(34),2000};
    std::stop_source stopped;stopped.request_stop();bool cancelled=false;
    try{cancelled_runtime.add(input,fragment,zero,std::span<const g::ProxyRow>(geometry).first(34),stopped.get_token(),
        std::span<const std::array<double,2>>(norms).first(34),full);}catch(const std::exception&){cancelled=true;}
    check(cancelled,"domain add ignored cancellation");
    bool bad_domain=false;const std::array<g::RuntimeProxy::Domain,1> bad{{{1,35}}};
    try{cancelled_runtime.add(input,fragment,zero,std::span<const g::ProxyRow>(geometry).first(34),{},
        std::span<const std::array<double,2>>(norms).first(34),bad);}catch(const std::exception&){bad_domain=true;}
    check(bad_domain,"domain add accepted an out-of-fragment interval");
    cancelled_runtime.add(input,fragment,zero,std::span<const g::ProxyRow>(geometry).first(34),{},
        std::span<const std::array<double,2>>(norms).first(34),full);
    const auto completed=cancelled_runtime.finish();check(completed[0][0].valid && completed[0][0].count==34,
        "cancelled or invalid domain add changed retry state");
}

int main(){try{
    multistart_domains();
    constexpr std::size_t N=408,C=34;const std::array<std::size_t,3> starts{0,7,17};
    std::vector<float> pcm(N+starts.back());std::vector<std::complex<double>> oscillator(pcm.size());
    std::vector<std::array<std::complex<double>,2>> rows(N);std::vector<std::uint8_t> mask(N,1);
    std::vector<std::uint64_t> owners(N);std::vector<d::CorrelationProjection> prefix(pcm.size()+1);
    double energy=0;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=i%17==0?1.f:static_cast<float>(std::ldexp(std::sin(.7*i),-30));
        oscillator[i]=std::polar(1.,.731*i);const double x=pcm[i],a=oscillator[i].real(),b=oscillator[i].imag();
        auto p=prefix[i];p.xc+=x*a;p.xs+=x*b;p.cc+=a*a;p.ss+=b*b;p.cs+=a*b;p.energy+=x*x;prefix[i+1]=p;
        energy+=x*x;}
    const auto union_energy=c::up(energy/c::down(1-c::gamma(pcm.size(),std::numeric_limits<double>::epsilon()/2)));
    for(std::size_t i=0;i<N;++i){owners[i]=i/C;const auto ramp=std::min(1.,std::pow(double(i+1)/90,3));
        rows[i]={ramp*std::polar(.7,.211*i),ramp*std::polar(.8,-.17*i)};
        if(i>=55 && i<69)mask[i]=0;if(i>=136 && i<170)rows[i]={};}
    g::RuntimeDirect runtime(starts.size(),C,union_energy,1e-6,2,512);
    for(std::size_t first=0;first<N;){const auto end=std::min(N,first+47),length=end-first+starts.back();
        runtime.add({std::span<const float>(pcm).subspan(first,length),std::span<const std::complex<double>>(oscillator).subspan(first,length),{},first},
            {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,end-first),
             std::span<const std::uint8_t>(mask).subspan(first,end-first),first},starts,
             std::span<const std::uint64_t>(owners).subspan(first,end-first));first=end;}
    const auto result=runtime.finish();check(runtime.sample_pairs()>0,"runtime did no direct work");
    for(std::size_t j=0;j<starts.size();++j){std::array<d::CorrelationChipEvidence,2> original{};std::array<d::CorrelationFit,2> whole{};
        for(std::size_t i=0;i<N;++i)if(mask[i]){const auto at=i+starts[j];const auto p=prefix[at+1]-prefix[at];
            for(unsigned bit=0;bit<2;++bit){d::CorrelationFit value;value.add(p,rows[i][bit],1);
                original[bit].add_fit(value,owners[i]);whole[bit].add(p,rows[i][bit],1);}}
        for(unsigned bit=0;bit<2;++bit){const auto tail=d::CorrelationChipEvidence::projection(original[bit].cell);
            const auto& bound=result[j][bit];check(bound.valid && bound.nested && bound.owner_set_certified,"runtime prefix/ramp rank proof unknown");
            check(bound.count==whole[bit].count && bound.rank==original[bit].projected_rank+tail.second,"runtime changed original count/rank");
            contains(bound.projected,original[bit].projected_energy+tail.first,"original prefix-difference projected energy escaped");}}
    std::cout<<"scheduled cold colored runtime checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
