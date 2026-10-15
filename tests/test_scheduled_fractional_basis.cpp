#include "scheduled_fractional_basis.hpp"
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace basis=d::scheduled_fractional_basis;
namespace tile=d::scheduled_acquisition_tile;
namespace cert=d::scheduled_acquisition_certificate;
using C=std::complex<double>;
using L=std::complex<long double>;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
constexpr std::size_t budget=32*1024*1024;

struct ExactFit {long double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;std::uint64_t count=0;};
L wide(C z){return {z.real(),z.imag()};}
void add_exact(ExactFit& f,long double x,L q) {
    f.xc+=x*q.real();f.xs+=x*q.imag();f.cc+=q.real()*q.real();
    f.ss+=q.imag()*q.imag();f.cs+=q.real()*q.imag();f.energy+=x*x;++f.count;
}
void encloses(const tile::Value& value,const std::array<ExactFit,2>& exact) {
    CHECK(value.certificate.valid);
    for(unsigned bit=0;bit<2;++bit) {
        const auto& f=value.fits[bit];const auto& x=exact[bit];
        const auto dr=value.certificate.dot_radius[bit],gr=value.certificate.gram_radius[bit];
        CHECK(f.count==x.count);
        CHECK(std::abs(static_cast<long double>(f.xc)-x.xc)<=dr);
        CHECK(std::abs(static_cast<long double>(f.xs)-x.xs)<=dr);
        CHECK(std::abs(static_cast<long double>(f.cc)-x.cc)<=gr);
        CHECK(std::abs(static_cast<long double>(f.ss)-x.ss)<=gr);
        CHECK(std::abs(static_cast<long double>(f.cs)-x.cs)<=gr);
        CHECK(x.energy>=value.certificate.energy_lower && x.energy<=value.certificate.energy_upper);
    }
}
tile::Value value_of(const std::array<ExactFit,2>& fields) {
    tile::Value result;result.physical_origin=123;
    result.certificate.energy_lower=cert::down(static_cast<double>(fields[0].energy));
    result.certificate.energy_upper=cert::up(static_cast<double>(fields[0].energy));
    result.certificate.valid=true;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& x=fields[bit];auto& f=result.fits[bit];
        f.xc=x.xc;f.xs=x.xs;f.cc=x.cc;f.ss=x.ss;f.cs=x.cs;f.energy=x.energy;f.count=x.count;
        result.certificate.dot_radius[bit]=std::max(cert::ulp(f.xc),cert::ulp(f.xs));
        result.certificate.gram_radius[bit]=std::max({cert::ulp(f.cc),cert::ulp(f.ss),cert::ulp(f.cs)});
    }
    return result;
}
void algebra_and_failure_cases() {
    const std::array<basis::Row,4> a{
        basis::Row{C{1,.25},C{.5,-.75}},basis::Row{C{.125,-1},C{-.25,.75}},
        basis::Row{C{-.75,.5},C{1,.25}},basis::Row{C{.25,.75},C{-.5,-1}}};
    auto b=a;
    b[0][0]={std::ldexp(1.,-54),-.5};b[1][1]={1.25,.5};b[2][0]={.75,-.25};
    basis::DifferenceCertificate difference;
    std::array<basis::Row,4> delta;
    std::array<ExactFit,2> fa{},fb{},fd{};
    const std::array<C,4> oscillator{{{1.25,.5},{-.5,1.5},{.75,-1},{1,-.25}}};
    const std::array<double,4> pcm{{.25,-.5,.125,.75}};
    for(std::size_t i=0;i<a.size();++i) {
        delta[i]=difference.subtract(a[i],b[i]);
        for(unsigned bit=0;bit<2;++bit) {
            add_exact(fa[bit],pcm[i],wide(a[i][bit])*wide(oscillator[i]));
            add_exact(fb[bit],pcm[i],wide(b[i][bit])*wide(oscillator[i]));
            add_exact(fd[bit],pcm[i],wide(delta[i][bit])*wide(oscillator[i]));
        }
    }
    CHECK(difference.valid());CHECK(difference.rows()==a.size());
    CHECK(difference.residual_norm_upper(0,2)>0);
    const auto av=value_of(fa),bv=value_of(fb),dv=value_of(fd);
    for(const double t:{0.,-0.,std::numeric_limits<double>::denorm_min(),std::ldexp(1.,-54),
                        .125,.5,.875,std::nextafter(1.,0.),1.}) {
        tile::Value mixed;CHECK(basis::combine(av,bv,dv,t,difference,2,mixed));
        std::array<ExactFit,2> expected{};
        for(std::size_t i=0;i<a.size();++i)for(unsigned bit=0;bit<2;++bit)
            add_exact(expected[bit],pcm[i],((1-static_cast<long double>(t))*wide(a[i][bit])+
                static_cast<long double>(t)*wide(b[i][bit]))*wide(oscillator[i]));
        encloses(mixed,expected);
        CHECK(mixed.fits[0].count==4);CHECK(mixed.fits[0].energy==av.fits[0].energy);
    }
    for(unsigned relation=0;relation<3;++relation) {
        basis::DifferenceCertificate proof;
        std::array<ExactFit,2> right_fields{},difference_fields{},expected{};
        for(std::size_t i=0;i<a.size();++i) {
            basis::Row right;
            for(unsigned bit=0;bit<2;++bit)right[bit]=relation==0?a[i][bit]:(relation==1?C{}:-a[i][bit]);
            const auto row=proof.subtract(a[i],right);
            for(unsigned bit=0;bit<2;++bit) {
                add_exact(right_fields[bit],pcm[i],wide(right[bit])*wide(oscillator[i]));
                add_exact(difference_fields[bit],pcm[i],wide(row[bit])*wide(oscillator[i]));
                add_exact(expected[bit],pcm[i],(.5L*wide(a[i][bit])+.5L*wide(right[bit]))*wide(oscillator[i]));
            }
        }
        auto difference_value=value_of(difference_fields);difference_value.requires_refinement={true,true};
        tile::Value mixed;
        CHECK(basis::combine(av,value_of(right_fields),difference_value,.5,proof,2,mixed));
        encloses(mixed,expected);CHECK(mixed.fits[0].count==4);
        if(relation==0)CHECK(!mixed.requires_refinement[0] && !mixed.requires_refinement[1]);
        if(relation==2)CHECK(mixed.requires_refinement[0] && mixed.requires_refinement[1]);
        basis::DifferenceCertificate joined;joined.append(proof);
        CHECK(joined.rows()==proof.rows());
        CHECK(joined.residual_norm_upper(0,2)>=proof.residual_norm_upper(0,2));
    }
    // Force the association correction to carry the proof: observed D is
    // deliberately zero although A-B=2A. Other field intervals are tiny.
    basis::DifferenceCertificate displaced_difference;
    std::array<ExactFit,2> negative_fields{},zero_fields{},cancelled_fields{};
    for(std::size_t i=0;i<a.size();++i) {
        const basis::Row negative{-a[i][0],-a[i][1]};
        displaced_difference.observe(a[i],negative,basis::Row{});
        for(unsigned bit=0;bit<2;++bit) {
            add_exact(negative_fields[bit],pcm[i],wide(negative[bit])*wide(oscillator[i]));
            add_exact(zero_fields[bit],pcm[i],L{});
            add_exact(cancelled_fields[bit],pcm[i],L{});
        }
    }
    tile::Value displaced;
    CHECK(basis::combine(av,value_of(negative_fields),value_of(zero_fields),.5,displaced_difference,2,displaced));
    encloses(displaced,cancelled_fields);
    CHECK(displaced.fits[0].cc>1 && displaced.certificate.gram_radius[0]>=displaced.fits[0].cc);
    tile::Value sentinel=av;sentinel.physical_origin=999;
    auto absent_proof=sentinel;
    CHECK(!basis::combine(av,bv,dv,.5,{},2,absent_proof));CHECK(absent_proof.physical_origin==999);
    auto unchanged=[&](const tile::Value& aa,const tile::Value& bb,const tile::Value& dd,double t,double o=2.) {
        auto output=sentinel;CHECK(!basis::combine(aa,bb,dd,t,difference,o,output));
        CHECK(output.physical_origin==999);
    };
    unchanged(av,bv,dv,-.1);unchanged(av,bv,dv,1.1);
    unchanged(av,bv,dv,std::numeric_limits<double>::quiet_NaN());
    unchanged(av,bv,dv,.5,-1);unchanged(av,bv,dv,.5,cert::infinity);
    auto bad=bv;bad.physical_origin++;unchanged(av,bad,dv,.5);
    bad=bv;bad.fits[0].count++;unchanged(av,bad,dv,.5);
    bad=bv;bad.fits[0].energy=std::nextafter(bad.fits[0].energy,cert::infinity);unchanged(av,bad,dv,.5);
    bad=bv;bad.certificate.energy_lower=-1;unchanged(av,bad,dv,.5);
    bad=bv;bad.certificate.dot_radius[0]=-1;unchanged(av,bad,dv,.5);
    bad=bv;bad.certificate.gram_radius[1]=cert::infinity;unchanged(av,bad,dv,.5);
    // Endpoints explicitly ignore the unused basis/difference and preserve
    // original error/refinement metadata exactly.
    auto endpoint_basis=av;endpoint_basis.requires_refinement={true,false};
    endpoint_basis.dot_roundoff={3,7};endpoint_basis.gram_roundoff={11,13};
    tile::Value endpoint;CHECK(basis::combine(endpoint_basis,{}, {},0,{},cert::infinity,endpoint));
    CHECK(endpoint.certificate.dot_radius==endpoint_basis.certificate.dot_radius);
    CHECK(endpoint.requires_refinement==endpoint_basis.requires_refinement);
    CHECK(endpoint.dot_roundoff==endpoint_basis.dot_roundoff && endpoint.gram_roundoff==endpoint_basis.gram_roundoff);
    CHECK(basis::combine({},bv,{},1,{},cert::infinity,endpoint));
    CHECK(endpoint.certificate.gram_radius==bv.certificate.gram_radius);
    basis::DifferenceCertificate invalid;
    auto not_finite=a[0];not_finite[0]={std::numeric_limits<double>::quiet_NaN(),0};
    invalid.observe(not_finite,a[0],a[0]);CHECK(!invalid.valid());
    invalid.subtract(a[0],a[0]);CHECK(!invalid.valid());
    basis::DifferenceCertificate sticky;sticky.append(invalid);CHECK(!sticky.valid());
    const auto saved=std::fegetround();CHECK(std::fesetround(FE_UPWARD)==0);
    CHECK(!basis::combine(av,bv,dv,.5,difference,2,endpoint));CHECK(std::fesetround(saved)==0);
}

template<class T> void native_accumulators(bool native_float) {
    const tile::Shape shape{5,17};tile::Plan<T> plan(shape);
    const std::array<std::size_t,3> starts{0,2,4};
    constexpr std::uint64_t origin=1000;
    tile::Accumulator<T> aa(plan,origin,starts,budget,{},false,native_float);
    tile::Accumulator<T> bb(plan,origin,starts,budget,{},false,native_float);
    tile::Accumulator<T> dd(plan,origin,starts,budget,{},false,native_float);
    basis::DifferenceCertificate difference;
    const std::array<double,5> weights{0,.125,.5,.875,1};
    std::array<std::array<std::array<ExactFit,2>,3>,5> expected{};
    for(unsigned fragment=0;fragment<3;++fragment) {
        const std::size_t length=fragment==1?11:17,physical=fragment*24;
        std::vector<float> pcm(21);std::vector<C> oscillator(21);
        std::vector<std::uint8_t> observed(21),mask(length);
        std::vector<basis::Row> a(length),b(length),delta(length);
        for(std::size_t i=0;i<pcm.size();++i) {
            pcm[i]=static_cast<float>((static_cast<int>((i*13+fragment*7)%31)-15)/16.);
            oscillator[i]={(static_cast<int>((i*7+fragment)%13)-6)/8.,
                           (static_cast<int>((i*11+fragment)%17)-8)/8.};
            observed[i]=(i+fragment)%7!=0;
        }
        for(std::size_t i=0;i<length;++i) {
            mask[i]=(i+fragment)%5!=0;
            for(unsigned bit=0;bit<2;++bit) {
                a[i][bit]={(static_cast<int>((i*3+bit+fragment)%17)-8)/8.,
                           (static_cast<int>((i*7+bit+fragment)%19)-9)/8.};
                b[i][bit]={(static_cast<int>((i*5+bit+fragment)%23)-11)/8.,
                           (static_cast<int>((i*11+bit+fragment)%29)-14)/8.};
            }
            // Include an active private-zero row: it still contributes raw E/N.
            if(i==3)a[i]=b[i]={};
            delta[i]=difference.subtract(a[i],b[i]);
        }
        const tile::Input input{pcm,oscillator,observed,origin+physical};
        tile::PreparedInput<T> prepared_input(plan,input,budget);
        const tile::Fragment af{a,mask,physical},bf{b,mask,physical},df{delta,mask,physical};
        tile::PreparedFragment<T> pa(plan,af,budget),pb(plan,bf,budget),pd(plan,df,budget);
        aa.add(prepared_input,pa);bb.add(prepared_input,pb);dd.add(prepared_input,pd);
        for(std::size_t at=0;at<starts.size();++at)for(std::size_t i=0;i<length;++i) {
            const auto n=starts[at]+i;if(!observed[n] || !mask[i])continue;
            for(std::size_t w=0;w<weights.size();++w)for(unsigned bit=0;bit<2;++bit) {
                const auto t=static_cast<long double>(weights[w]);
                const auto q=((1-t)*wide(a[i][bit])+t*wide(b[i][bit]))*wide(oscillator[n]);
                add_exact(expected[w][at][bit],pcm[n],q);
            }
        }
    }
    const auto av=aa.finish(),bv=bb.finish(),dv=dd.finish();
    for(std::size_t at=0;at<starts.size();++at)for(std::size_t w=0;w<weights.size();++w) {
        tile::Value mixed;CHECK(basis::combine(av[at],bv[at],dv[at],weights[w],difference,2,mixed));
        encloses(mixed,expected[w][at]);
        CHECK(mixed.physical_origin==origin+starts[at]);
        CHECK(mixed.fits[0].energy==av[at].fits[0].energy);
        CHECK(mixed.fits[0].count==av[at].fits[0].count);
    }
}
int main() {
    try {
        algebra_and_failure_cases();native_accumulators<double>(false);native_accumulators<float>(false);
        if(d::scheduled_float_spectral_accumulator::environment())native_accumulators<float>(true);
        std::cout<<"scheduled fractional basis tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
