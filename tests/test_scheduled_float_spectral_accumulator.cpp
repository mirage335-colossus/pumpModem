#include "scheduled_float_spectral_accumulator.hpp"
#include <bit>
#include <cfenv>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace f=datapump::modem::detail::scheduled_float_spectral_accumulator;
namespace c=datapump::modem::detail::scheduled_acquisition_certificate;
using C=std::complex<float>;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);} while(false)
template<class Fn>void rejects(Fn fn){bool rejected=false;try{fn();}catch(const datapump::Error&){rejected=true;}CHECK(rejected);}

// Exact integer units of 2^-149. Finite binary32 <=277 bits; <=65536
// terms <=293 bits. No long-double exactness or additional library is assumed.
struct UInt {
    std::array<std::uint64_t,5> limb{};
    void part(std::size_t at,std::uint64_t value) {
        while(value) {
            CHECK(at<limb.size());const auto before=limb[at];limb[at]+=value;
            value=limb[at]<before?1:0;++at;
        }
    }
    void shifted(std::uint64_t value,int shift) {
        if(shift<0){value=(-shift>=64)?0:value>>static_cast<unsigned>(-shift);shift=0;}
        const auto at=static_cast<std::size_t>(shift)/64;const auto within=static_cast<unsigned>(shift)%64;
        if(!value)return;part(at,value<<within);
        if(within)part(at+1,value>>(64-within));
    }
    static int compare(const UInt& a,const UInt& b) {
        for(std::size_t i=a.limb.size();i--;)if(a.limb[i]!=b.limb[i])return a.limb[i]<b.limb[i]?-1:1;
        return 0;
    }
    static UInt subtract(UInt a,const UInt& b) {
        CHECK(compare(a,b)>=0);std::uint64_t borrow=0;
        for(std::size_t i=0;i<a.limb.size();++i) {
            const auto before=a.limb[i],after=before-b.limb[i];const bool first=before<b.limb[i];
            a.limb[i]=after-borrow;borrow=static_cast<std::uint64_t>(first || after<borrow);
        }
        CHECK(!borrow);return a;
    }
    static UInt floor_radius(double radius) {
        CHECK(radius>=0 && std::isfinite(radius));UInt result;
        const auto bits=std::bit_cast<std::uint64_t>(radius);const auto exponent=static_cast<unsigned>((bits>>52)&2047);
        const auto mantissa=(bits&0xfffffffffffffULL)|(exponent?1ULL<<52:0);
        const int shift=exponent?static_cast<int>(exponent)-926:-925;
        // A larger bound exceeds every possible exact test sum.
        if(shift>267){result.limb.fill(~std::uint64_t{});return result;}
        result.shifted(mantissa,shift);return result;
    }
};
struct ExactSum {
    UInt positive,negative;
    void add(float x) {
        CHECK(std::isfinite(x));const auto bits=std::bit_cast<std::uint32_t>(x);
        const auto e=(bits>>23)&255,m=(bits&0x7fffffU)|(e?1U<<23:0);
        (bits>>31?negative:positive).shifted(m,e?static_cast<int>(e)-1:0);
    }
    bool enclosed(float output,double radius)const {
        if(std::isinf(radius))return true;
        auto p=positive,n=negative;ExactSum subtractor;subtractor.add(-output);
        for(std::size_t i=0;i<5;++i){p.part(i,subtractor.positive.limb[i]);n.part(i,subtractor.negative.limb[i]);}
        const auto error=UInt::compare(p,n)>=0?UInt::subtract(p,n):UInt::subtract(n,p);
        // Error has integral 2^-149 units; exact floor of a decoded FP64
        // radius is sufficient and avoids rounding it to float or long double.
        return UInt::compare(error,UInt::floor_radius(radius))<=0;
    }
};
C rounded_product(C a,C b) {
    volatile float ac=a.real()*b.real(),bd=a.imag()*b.imag(),ad=a.real()*b.imag(),bc=a.imag()*b.real();
    volatile float re=ac-bd,im=ad+bc;return {re,im};
}
bool bit_equal(C a,C b) {
    return std::bit_cast<std::uint32_t>(a.real())==std::bit_cast<std::uint32_t>(b.real()) &&
           std::bit_cast<std::uint32_t>(a.imag())==std::bit_cast<std::uint32_t>(b.imag());
}
void exact_oracle_checks() {
    ExactSum s;s.add(1);s.add(std::numeric_limits<float>::denorm_min());s.add(-1);
    CHECK(s.enclosed(std::numeric_limits<float>::denorm_min(),0));
    CHECK(!s.enclosed(0,0));CHECK(s.enclosed(0,std::numeric_limits<float>::denorm_min()));
    CHECK(!s.enclosed(0,.5*static_cast<double>(std::numeric_limits<float>::denorm_min())));
    ExactSum longsum;for(unsigned n=0;n<65536;++n)longsum.add(std::numeric_limits<float>::max());
    CHECK(!longsum.enclosed(0,0));CHECK(longsum.enclosed(0,std::numeric_limits<double>::max()));
}
void reductions(std::size_t bins,std::size_t fragments,unsigned mode) {
    std::vector<C> a(bins),b(bins),scalar(bins),sc(bins),native(bins),nc(bins);
    std::vector<std::array<ExactSum,2>> exact(bins);std::mt19937 rng(92183+mode);
    double scalar_mass=0,native_mass=0;
    f::Kernel executed=f::Kernel::scalar;
    for(std::size_t block=0;block<fragments;++block) {
        for(std::size_t i=0;i<bins;++i) {
            if(mode==0) {
                const float large=std::ldexp(1.F,block%4==0?100:block%4==2?100:-100);
                const float sign=block%4==2?-1.F:1.F;
                a[i]={large*sign,large*sign*.5F};b[i]={.5F,static_cast<float>((i%3)-1.)*.125F};
            } else if(mode==1) {
                const auto small=std::numeric_limits<float>::denorm_min();
                a[i]={static_cast<float>(1+i%9)*small,static_cast<float>(1+i%7)*small};
                b[i]={block%2?-1.F:1.F,block%3==0?.5F:0.F};
            } else {
                a[i]={static_cast<float>(static_cast<int>(rng()%2001)-1000)*.03125F,
                      static_cast<float>(static_cast<int>(rng()%2001)-1000)*.015625F};
                b[i]={static_cast<float>(static_cast<int>(rng()%2001)-1000)*.0078125F,
                      static_cast<float>(static_cast<int>(rng()%2001)-1000)*.00390625F};
            }
            const auto p=rounded_product(a[i],b[i]);exact[i][0].add(p.real());exact[i][1].add(p.imag());
        }
        const auto x=f::accumulate(a,b,scalar,sc,{},{f::Kernel::scalar,true});
        const auto y=f::accumulate(a,b,native,nc);
        CHECK(x.certificate_valid && y.certificate_valid);executed=y.executed;
        scalar_mass=c::add(scalar_mass,x.residual_mass_upper);native_mass=c::add(native_mass,y.residual_mass_upper);
        CHECK(x.residual_mass_upper==y.residual_mass_upper);
        for(std::size_t i=0;i<bins;++i){CHECK(bit_equal(scalar[i],native[i]));CHECK(bit_equal(sc[i],nc[i]));}
    }
    double final_mass=0;
    for(std::size_t i=0;i<bins;++i)final_mass=c::add(final_mass,f::finish(scalar[i],sc[i]).radius_l1);
    const auto radius=f::accumulation_radius(fragments,bins,scalar_mass,final_mass);
    CHECK(std::isfinite(radius));CHECK(native_mass==scalar_mass);
    for(std::size_t i=0;i<bins;++i) {
        const auto v=f::finish(scalar[i],sc[i]);CHECK(exact[i][0].enclosed(v.value.real(),radius));CHECK(exact[i][1].enclosed(v.value.imag(),radius));
    }
    if(bins>=4 && f::spectral::avx2_available()) {
#if !defined(DP_FLOATACC_PORTABLE_ONLY) && !defined(DP_SPECTRAL_PORTABLE_ONLY)
        CHECK(executed==f::Kernel::avx2);
#endif
    }
}
void failures() {
    std::vector<C> a(9,C{1,1}),b(9,C{1,1}),s(9),correction(9);
    rejects([&]{f::accumulate(a,b,s,s);});
    rejects([&]{f::accumulate(a,b,a,correction);});
    std::stop_source stop;stop.request_stop();rejects([&]{f::accumulate(a,b,s,correction,stop.get_token());});
    for(auto x:s)CHECK(x==C{});for(auto x:correction)CHECK(x==C{});
    a[0]={std::numeric_limits<float>::max(),0};b[0]={2,0};rejects([&]{f::accumulate(a,b,s,correction);});
    rejects([&]{f::finish({std::numeric_limits<float>::max(),0},{std::numeric_limits<float>::max(),0});});
    CHECK(std::isinf(f::accumulation_radius(65537,1,0,0)));
    CHECK(std::isinf(f::accumulation_radius(1,f::maximum_transform+1,0,0)));
    CHECK(std::isinf(f::accumulation_radius(1,1,-1,0)));
    const auto rounding=std::fegetround();CHECK(std::fesetround(FE_UPWARD)==0);
    a.assign(9,{1,0});b.assign(9,{1,0});s.assign(9,{});correction.assign(9,{});
    const auto invalid=f::accumulate(a,b,s,correction);CHECK(!invalid.certificate_valid && std::isinf(invalid.residual_mass_upper));
    CHECK(std::isinf(f::finish({1,0},{0,0},true).radius_l1));
    CHECK(std::fesetround(rounding)==0);
#if defined(__SSE2__)
    const auto csr=_mm_getcsr();
    _mm_setcsr((csr&~(3U<<13))|(2U<<13)); // Change SIMD rounding only.
    CHECK(!f::environment());CHECK(!c::environment<float>() && !c::environment<double>());
    const auto directed=f::accumulate(a,b,s,correction);CHECK(!directed.certificate_valid && std::isinf(directed.residual_mass_upper));
    CHECK(std::isinf(f::finish({1,0},{0,0},true).radius_l1));_mm_setcsr(csr);
    _mm_setcsr(csr|(1U<<15)|(1U<<6));
    const auto flushed=f::accumulate(a,b,s,correction);CHECK(!flushed.certificate_valid && std::isinf(flushed.residual_mass_upper));
    CHECK(std::isinf(f::finish({1,0},{0,0},true).radius_l1));_mm_setcsr(csr);
#endif
    const auto join=f::finish({1,0},{std::ldexp(1.F,-24),0});CHECK(join.value.real()==1);CHECK(join.radius_l1>=std::ldexp(1.,-24));
}
int main()try {
    CHECK(f::environment());exact_oracle_checks();
    reductions(9,65536,0);reductions(257,769,1);reductions(16384,33,2);reductions(65536,3,3);
    failures();std::cout<<"native FP32 compensated spectra: exact dyadic long-reduction, boundary, environment and dispatch checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
