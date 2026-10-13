#include "../src/scheduled_pulse_moments.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace datapump::modem;
using namespace datapump::modem::detail;
using C=std::complex<double>;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool near(double a,double b){return std::abs(a-b)<=2e-10*std::max({1.,std::abs(a),std::abs(b)});}
bool near(C a,C b){return near(a.real(),b.real())&&near(a.imag(),b.imag());}
ScheduledSpanMoments moments(const std::vector<double>& x,const std::vector<C>& o) {
    ScheduledSpanMoments m;m.count=x.size();
    for(std::size_t n=0;n<x.size();++n) {
        const double t=static_cast<double>(n);
        m.signal[0]+=x[n]*o[n];m.signal[1]+=t*x[n]*o[n];
        double p=1;
        for(unsigned k=0;k<3;++k){m.square[k]+=p*o[n]*o[n];m.norm[k]+=p*std::norm(o[n]);p*=t;}
        m.energy+=x[n]*x[n];
    }
    return m;
}
void prefix_origin_and_cache_identity() {
    struct Power {C signal1{},square1{},square2{};double norm1=0,norm2=0;};
    std::vector<CorrelationProjection> p(97);
    std::vector<Power> powers(p.size());
    std::vector<double> x(96);std::vector<C> o(96);
    for(std::size_t n=0;n<x.size();++n) {
        x[n]=std::sin(.317*n);o[n]=std::polar(1.+1e-7*n,.123*n+.7);
        const auto c=o[n].real(),s=o[n].imag(),t=static_cast<double>(n),t2=t*t;
        auto q=p[n];q.xc+=x[n]*c;q.xs+=x[n]*s;q.cc+=c*c;
        q.ss+=s*s;q.cs+=c*s;q.energy+=x[n]*x[n];p[n+1]=q;
        auto a=powers[n];a.signal1+=t*x[n]*o[n];a.square1+=t*o[n]*o[n];
        a.square2+=t2*o[n]*o[n];a.norm1+=t*std::norm(o[n]);
        a.norm2+=t2*std::norm(o[n]);powers[n+1]=a;
    }
    ScheduledSpanMoments extracted;
    check(scheduled_span_moments(std::span<const CorrelationProjection>{p},
        std::span<const Power>{powers},17,63,extracted),"valid physical span rejected");
    const std::vector<double> clipped_x(x.begin()+17,x.begin()+80);
    const std::vector<C> clipped_o(o.begin()+17,o.begin()+80);
    const auto direct=moments(clipped_x,clipped_o);
    for(unsigned k=0;k<2;++k)check(near(extracted.signal[k],direct.signal[k]),"signal moment origin changed");
    for(unsigned k=0;k<3;++k)check(near(extracted.square[k],direct.square[k])&&
        near(extracted.norm[k],direct.norm[k]),"square/norm moment origin changed");
    check(near(extracted.energy,direct.energy)&&extracted.count==63,"raw energy/count changed");
    check(scheduled_span_moments(std::span<const CorrelationProjection>{p}.first(81),
        std::span<const Power>{powers},17,63,extracted),"partial shared block rejected full-capacity powers");
    check(!scheduled_span_moments(std::span<const CorrelationProjection>{p},
        std::span<const Power>{powers},80,17,extracted),"physical span passed input endpoint");
    ScheduledMomentTile<1> cache;unsigned builds=0;
    const auto build=[&](ScheduledSpanMoments& m){++builds;m=direct;return true;};
    check(cache.get(1,17,63,build)!=nullptr&&builds==1,"cache first construction failed");
    check(cache.get(1,17,63,build)!=nullptr&&builds==1&&cache.hits==1,"exact physical span missed");
    check(cache.get(2,17,63,build)!=nullptr&&builds==2,"frequency identity was ignored");
    check(cache.get(2,18,63,build)!=nullptr&&builds==3,"physical left was ignored");
    cache.reset();check(cache.get(2,18,63,build)!=nullptr&&builds==4,"capture reset retained stale span");
}
void pair_fit_and_atomic_fallback() {
    std::mt19937_64 random(6013);std::uniform_real_distribution<double> u(-1,1);
    for(unsigned c=0;c<128;++c) {
        const std::size_t n=3+c%62;std::vector<double> x(n);std::vector<C> o(n);
        for(std::size_t j=0;j<n;++j){x[j]=u(random);o[j]=std::polar(1.+1e-7*j,.127*static_cast<double>(j)+.3);}
        const std::array<C,2> value{{{.37,.19},{-.23,.41}}},slope{{{.001,-.002},{-.002,.003}}};
        auto m=moments(x,o);std::array<CorrelationFit,2> actual;
        check(scheduled_affine_pair(m,value,slope,actual),"regular Gram rejected");
        for(unsigned bit=0;bit<2;++bit) {
            CorrelationFit direct;
            for(std::size_t j=0;j<n;++j) {
                const auto z=o[j];CorrelationProjection p{x[j]*z.real(),x[j]*z.imag(),
                    z.real()*z.real(),z.imag()*z.imag(),z.real()*z.imag(),x[j]*x[j]};
                direct.add(p,value[bit]+static_cast<double>(j)*slope[bit],1);
            }
            const auto& a=actual[bit];
            check(near(a.xc,direct.xc)&&near(a.xs,direct.xs)&&near(a.cc,direct.cc)&&
                near(a.ss,direct.ss)&&near(a.cs,direct.cs)&&near(a.energy,direct.energy)&&
                a.count==direct.count,"affine pair changed real I/Q fit or raw energy/count");
        }
    }
    ScheduledSpanMoments singular;singular.count=32;singular.norm[0]=32;singular.square[0]=32;
    std::array<CorrelationFit,2> sentinel;sentinel[0].energy=17;sentinel[1].energy=29;
    const std::array<C,2> v{{{1,0},{0,1}}},s{};
    check(!scheduled_affine_pair(singular,v,s,sentinel)&&sentinel[0].energy==17&&
        sentinel[1].energy==29,"singular pair partially mutated output");
    singular.norm[0]=std::numeric_limits<double>::infinity();
    check(!scheduled_affine_pair(singular,v,s,sentinel),"nonfinite Gram accepted");
}
void outer_pulses_and_partial_tail() {
    std::mt19937_64 random(17);std::uniform_real_distribution<double> u(-1,1);
    const auto coefficients=[](std::uint64_t j){return std::array<C,2>{
        std::polar(.5+.03*(j%7),.713*static_cast<double>(j+1)),
        std::polar(.4+.02*(j%5),1.117*static_cast<double>(j+3))};};
    for(const auto chip:{1024ULL,8192ULL})for(const auto tail:{0ULL,137ULL})
      for(const auto rate:{.99999L,1.L,1.00001L})
       for(const auto local:{0ULL,7ULL,8ULL,12ULL,20ULL,21ULL})
        for(const auto fraction:{0.L,.25L,.499999999L,.5L,.500000001L,.75L,.999999999L}) {
            const auto symbol=21*chip+tail;const auto within=(local+fraction)*chip;
            if(within>=symbol)continue;
            const auto endpoint=std::min<long double>(symbol,(local+1)*chip);
            const auto maximum=static_cast<std::uint64_t>(std::min(32.L,
                std::max(1.L,std::ceil((endpoint-within)/rate))));
            const auto pulse=scheduled_pulse_span(within,rate,chip,symbol,maximum);
            std::vector<double> x(pulse.count);std::vector<C> o(pulse.count);
            for(std::size_t n=0;n<x.size();++n){x[n]=u(random);o[n]=std::polar(1.,.137*static_cast<double>(n)+.2);}
            auto m=moments(x,o);CorrelationOuterEvidence grouped,direct;
            scheduled_outer_piece(m,pulse,grouped,coefficients);
            for(std::size_t n=0;n<x.size();++n)
                pattern_pulse_each(static_cast<double>(within+n*rate),symbol,chip,
                    [&](std::uint64_t j,double p){direct.add(j,p*x[n]*o[n],coefficients);});
            grouped.finish(coefficients);direct.finish(coefficients);
            for(unsigned bit=0;bit<2;++bit) {
                check(near(C{static_cast<double>(grouped.dot[bit].real()),static_cast<double>(grouped.dot[bit].imag())},
                    C{static_cast<double>(direct.dot[bit].real()),static_cast<double>(direct.dot[bit].imag())}),"outer complete-pulse dot changed");
                check(near(static_cast<double>(grouped.variance[bit]),static_cast<double>(direct.variance[bit]))&&
                    grouped.contributing_terms[bit]==direct.contributing_terms[bit],"outer complete-pulse variance/term count changed");
            }
        }
}
} // namespace

int main() {
    prefix_origin_and_cache_identity();pair_fit_and_atomic_fallback();
    outer_pulses_and_partial_tail();
    std::cout<<"shared mapped affine/outer pulse algebra passed\n";
}
