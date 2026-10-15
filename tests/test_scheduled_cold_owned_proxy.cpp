#include "scheduled_cold_owned_proxy.hpp"
#include "scheduled_cold_colored_source_rows.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
using Z=std::complex<double>;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void contains(c::Interval a,double b){check(a.bounded() && a.lo<=b && b<=a.hi,"Original escaped owned whole interval");}
static void paired(bool forward,bool crop) {
    constexpr std::size_t N=204,C=34;
    std::vector<float> pcm(N);std::vector<Z> oscillator(N);
    std::vector<std::array<Z,2>> rows(N),original_rows(N);
    std::vector<std::array<double,2>> norms(N);
    std::vector<std::uint8_t> mask(N,1),present(N,1);
    std::vector<g::ProxyRow> geometry(N);
    std::array<d::CorrelationChipEvidence,2> exact{};std::array<d::CorrelationFit,2> whole{};
    double energy=0;
    for(std::size_t i=0;i<N;++i){pcm[i]=static_cast<float>(.3*std::sin(.17*i)+.2*std::cos(.31*i));
        oscillator[i]=std::polar(1.,.731*i);energy+=double(pcm[i])*pcm[i];
        rows[i]={std::polar(.7,.011*i),std::polar(.8,-.023*double(i))};
        original_rows[i]={rows[i][0]+Z{1e-9,-2e-9},rows[i][1]+Z{-2e-9,1e-9}};
        norms[i]={c::norm(rows[i][0]),c::norm(rows[i][1])};
        // First/last physical samples can move in either direction across
        // source-chip edges. Exact Original ownership is supplied, with no
        // count-radius or transfer-flag substitution.
        const auto source=static_cast<long double>(i)+(forward?.9999999L:.0000001L)+(forward?1e-8L:-1e-8L)*i;
        if(source<0 || source>=N || (crop && (i<7 || i>=N-9)) || (i>=55 && i<61))mask[i]=0;
        if(mask[i])geometry[i]={static_cast<std::uint64_t>(source/C),0,1e-8};
    }
    g::OwnedProxy proxy(C+2,c::up(2*energy),1e-6,2,512);
    std::vector<d::CorrelationProjection> prefix(N+1);
    for(std::size_t i=0;i<N;++i){const auto x=double(pcm[i]),a=oscillator[i].real(),b=oscillator[i].imag();
        auto p=prefix[i];p.xc+=x*a;p.xs+=x*b;p.cc+=a*a;p.ss+=b*b;p.cs+=a*b;p.energy+=x*x;prefix[i+1]=p;
        if(mask[i])for(unsigned bit=0;bit<2;++bit){const auto p=prefix[i+1]-prefix[i];d::CorrelationFit f;f.add(p,original_rows[i][bit],1);
            exact[bit].add_fit(f,geometry[i].owner);whole[bit].add(p,original_rows[i][bit],1);}}
    for(std::size_t first=0;first<N;){const auto n=std::min<std::size_t>(47,N-first);
        proxy.add({std::span<const float>(pcm).subspan(first,n),std::span<const Z>(oscillator).subspan(first,n),
                std::span<const std::uint8_t>(present).subspan(first,n),first},
            {std::span<const std::array<Z,2>>(rows).subspan(first,n),std::span<const std::uint8_t>(mask).subspan(first,n),first},
            std::span<const g::ProxyRow>(geometry).subspan(first,n),std::span<const std::array<double,2>>(norms).subspan(first,n));first+=n;}
    const auto result=proxy.finish(true);check(result.valid,"owned native corrected partition unknown");
    check(proxy.chip_pairs()==proxy.whole_pairs() && proxy.whole_pairs()==whole[0].count,"duplicate domain pair accounting changed");
    for(unsigned bit=0;bit<2;++bit){const auto tail=d::CorrelationChipEvidence::projection(exact[bit].cell);
        const auto& a=result.whole[bit];const auto& b=whole[bit];
        contains(a.xc,b.xc);contains(a.xs,b.xs);contains(a.cc,b.cc);contains(a.ss,b.ss);contains(a.cs,b.cs);contains(a.energy,b.energy);
        check(a.count==b.count && a.count_lower==b.count && a.count_upper==b.count,"whole count is not exact Original count");
        check(result.chips[bit].count==b.count && result.chips[bit].rank==exact[bit].projected_rank+tail.second,"Original chip count/rank changed");
        contains(result.chips[bit].projected,exact[bit].projected_energy+tail.first);
        for(double threshold:{0.,5.,100.,10000.}){const auto guard=g::guarded_upper(a,result.chips[bit],threshold,{0,true});
            if(guard.colored){check(exact[bit].colored_excess(b,threshold),"certified a different colored predicate");
                check(exact[bit].score(b)<=guard.score_upper,"Original guarded score escaped upper");}}
    }
    bool twice=false;try{(void)proxy.finish(true);}catch(const std::exception&){twice=true;}check(twice,"owned terminal cell counted twice");
}
static void failures() {
    const std::array<float,2> pcm{.25f,-.5f};const std::array<Z,2> carrier{Z{1,0},Z{0,1}};
    const std::array<std::array<Z,2>,2> rows{{{Z{.7,.1},Z{.8,-.2}},{Z{.7,.1},Z{.8,-.2}}}};
    const std::array<std::array<double,2>,2> norms{{{1,1},{1,1}}};const std::array<std::uint8_t,2> mask{1,1};
    const std::array<g::ProxyRow,2> geometry{{{0,0,0},{0,0,0}}};
    g::OwnedProxy no_closure(34,1,1e-6,2,512);no_closure.add({pcm,carrier,{},0},{rows,mask,0},geometry,norms);
    check(!no_closure.finish(false).valid,"missing support-closure proof accepted");
    g::OwnedProxy missing(34,1,1e-6,2,512);const std::array<std::uint8_t,2> absent{1,0};
    missing.add({pcm,carrier,absent,0},{rows,mask,0},geometry,norms);check(!missing.finish(true).valid,"missing Original observation accepted");
    g::OwnedProxy stopped(34,1,1e-6,2,512);std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{stopped.add({pcm,carrier,{},0},{rows,mask,0},geometry,norms,stop.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled && !stopped.selected_rows() && !stopped.chip_pairs(),"pre-cancelled owned add changed state");
}
static void strong_colored() {
    constexpr std::size_t C=34,N=256*C;std::vector<float> pcm(N);std::vector<Z> carrier(N);
    std::vector<std::array<Z,2>> rows(N);std::vector<std::array<double,2>> norms(N);
    std::vector<std::uint8_t> mask(N,1);std::vector<g::ProxyRow> geometry(N);
    std::array<d::CorrelationChipEvidence,2> exact{};std::array<d::CorrelationFit,2> whole{};double energy=0;
    for(std::size_t i=0;i<N;++i){const auto physical=i<4000?i:i+10000;carrier[i]=std::polar(1.,.731*physical);
        pcm[i]=static_cast<float>(.4*carrier[i].real());energy+=double(pcm[i])*pcm[i];
        const auto owner=i/C;rows[i]={std::polar(.7,.731*owner),std::polar(.8,.931*owner)};
        norms[i]={c::norm(rows[i][0]),c::norm(rows[i][1])};geometry[i]={owner,0,1e-12};
        const auto x=double(pcm[i]),a=carrier[i].real(),b=carrier[i].imag();
        const d::CorrelationProjection p{x*a,x*b,a*a,b*b,a*b,x*x};
        for(unsigned bit=0;bit<2;++bit){d::CorrelationFit f;f.add(p,rows[i][bit],1);exact[bit].add_fit(f,owner);whole[bit].add(p,rows[i][bit],1);}}
    g::OwnedProxy proxy(C,c::up(2*energy),1e-6,2,512);
    for(std::size_t first=0;first<N;){const auto end=std::min({N,first+128,first<4000?std::size_t(4000):N});const auto n=end-first;
        const auto physical=first<4000?first:first+10000;
        proxy.add({std::span<const float>(pcm).subspan(first,n),std::span<const Z>(carrier).subspan(first,n),{},physical},
            {std::span<const std::array<Z,2>>(rows).subspan(first,n),std::span<const std::uint8_t>(mask).subspan(first,n),physical},
            std::span<const g::ProxyRow>(geometry).subspan(first,n),std::span<const std::array<double,2>>(norms).subspan(first,n));first=end;}
    const auto result=proxy.finish(true);check(result.valid,"strong colored owned proof unknown");
    for(unsigned bit=0;bit<2;++bit){const auto guard=g::guarded_upper(result.whole[bit],result.chips[bit],100,{1e-12,true});
        check(guard.colored && exact[bit].colored_excess(whole[bit],100),"strong Original colored predicate was not certified");
        check(guard.score_upper<5 && exact[bit].score(whole[bit])<=guard.score_upper,"strong Original guard did not bound its score");}
}
static void rounded_gain() {
    g::SourceRows rows({},0,32);bool overshoot=false;unsigned i=0;
    for(double delta:{1e-5,1e-6,1e-7,1e-8,1e-9,1e-10,1e-11,1e-12}) {
        const double t=1-delta,gain=t*t*t*(10+t*(-15+6*t));
        overshoot|=gain>1;
        check(gain<=g::smooth_gain_upper() && gain*gain<=g::smooth_gain_upper(),"rounded quintic escaped its derived cap");
        rows.append(i++,{Z{gain,0},Z{0,gain}},0,gain,1);
        check(rows.gain.back()==gain,"source gain was clamped");
    }
    check(overshoot,"fixture missed the actual quintic rounding overshoot");
    bool rejected=false;try{rows.append(i,{Z{1,0},Z{0,1}},0,1.000001,1);}catch(const std::exception&){rejected=true;}
    check(rejected,"source gain admitted values outside the arithmetic bound");
}
int main(){try{if(!c::environment<double>())return 77;rounded_gain();paired(false,false);paired(true,false);paired(false,true);paired(true,true);failures();strong_colored();
    std::cout<<"owned corrected support whole/chip enclosure checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
