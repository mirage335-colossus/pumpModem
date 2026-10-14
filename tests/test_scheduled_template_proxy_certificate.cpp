#include "scheduled_template_proxy_certificate.hpp"
#include <stdexcept>
// These predicates must execute in Release builds as well as Debug.
static std::size_t checked_predicates=0;
#define CHECK(...) do { ++checked_predicates; if(!(__VA_ARGS__)) throw std::runtime_error(#__VA_ARGS__); } while(false)
#include <cfenv>
#include <iostream>

namespace proxy=datapump::modem::detail::scheduled_template_proxy_certificate;
namespace cert=datapump::modem::detail::scheduled_acquisition_certificate;
using datapump::modem::detail::CorrelationFit;
using Wide=std::complex<long double>;
struct Moments {
    long double xc=0,xs=0,cc=0,ss=0,cs=0;
    void add(long double x,Wide q) {xc+=x*q.real();xs+=x*q.imag();
        cc+=q.real()*q.real();ss+=q.imag()*q.imag();cs+=q.real()*q.imag();}
};

static void paired(unsigned seed) {
    constexpr unsigned n=4096;
    std::array<Moments,2> original{},represented{};
    proxy::ResidualSquares residual;
    long double selected_energy=0,union_energy=0;
    double maximum=0,residual_l1=0,residual_gram=0;
    std::uint64_t strips=0;
    for(unsigned i=0;i<n;++i) {
        const double x=.17*std::sin((i+3)*(seed+1)*.071)+.04*std::cos(i*.173);
        const bool strip=i%257==0;
        const double e=strip?.035:(i%503==0?.008:1e-7*(1+i%7));
        const double phase=.019*(i+seed),magnitude=.97+.02*std::sin(i*.13);
        const std::complex<double> oscillator=std::polar(magnitude,phase);
        maximum=std::max(maximum,std::abs(x));union_energy+=static_cast<long double>(x)*x;
        if(strip)++strips;else selected_energy+=static_cast<long double>(x)*x;
        for(unsigned bit=0;bit<2;++bit) {
            const std::complex<double> q=strip?std::complex<double>{}:
                std::polar((bit?.21:.67)*(1.2+.4*std::sin(i*(bit?.071:.053))),i*(bit?.109:.137));
            const auto delta=std::polar(.87*e,i*(bit?.151:.193));
            const Wide qo=Wide(q.real(),q.imag())*Wide(oscillator.real(),oscillator.imag());
            const Wide qt=(Wide(q.real(),q.imag())+Wide(delta.real(),delta.imag()))*
                Wide(oscillator.real(),oscillator.imag());
            represented[bit].add(x,qo);original[bit].add(x,qt);
        }
        residual.observe(e);residual_l1=cert::add(residual_l1,e);
        residual_gram=cert::add(residual_gram,cert::add(cert::mul(2*1.2,e),cert::mul(e,e)));
    }
    cert::FitCertificate certificate;certificate.valid=true;
    certificate.energy_upper=cert::up(static_cast<double>(selected_energy));
    certificate.energy_lower=std::max(0.,cert::down(static_cast<double>(selected_energy)));
    for(unsigned bit=0;bit<2;++bit) {
        const auto& p=represented[bit];const auto& q=original[bit];
        CorrelationFit fit;fit.cc=static_cast<double>(p.cc);fit.ss=static_cast<double>(p.ss);fit.cs=static_cast<double>(p.cs);
        // The separately owned FFT certificate would enclose these casts.
        certificate.gram_radius[bit]=1e-10;
        const auto bound=proxy::errors(fit,certificate,bit,residual.upper,1.0,strips,cert::up(maximum),residual_l1,residual_gram);
        CHECK(bound.valid);
        const auto dot=std::hypot(q.xc-p.xc,q.xs-p.xs);
        const auto a=q.cc-p.cc,b=q.cs-p.cs,c=q.ss-p.ss;
        const auto gram=.5L*(std::abs(a+c)+std::hypot(a-c,2*b));
        CHECK(dot<=bound.dot);CHECK(gram<=bound.gram);
        CHECK(proxy::union_energy_upper(certificate.energy_upper,strips,cert::up(maximum))>=union_energy);
        CHECK(bound.dot<2*maximum*residual_l1);
        std::cout<<"seed="<<seed<<" bit="<<bit<<" dot="<<static_cast<double>(dot)
            <<" bound="<<bound.dot<<" gram="<<static_cast<double>(gram)<<" bound="<<bound.gram<<'\n';
    }
}

int main() {
    CHECK(cert::environment<double>());
    for(unsigned seed=0;seed<5;++seed)paired(seed);
    CorrelationFit empty;cert::FitCertificate c;c.valid=true;c.energy_upper=0;c.gram_radius={0,0};
    const auto exact=proxy::errors(empty,c,0,0,2,0,0);
    CHECK(exact.valid && exact.dot==0 && exact.gram==0);
    proxy::ResidualSquares invalid;invalid.observe(-1);CHECK(!invalid.valid);
    CHECK(!proxy::errors(empty,c,0,invalid.upper,2,0,0).valid);
    invalid={};invalid.observe(std::numeric_limits<double>::max());CHECK(!invalid.valid);
    CHECK(!proxy::errors(empty,c,2,0,2,0,0).valid);
    CHECK(!proxy::errors(empty,c,0,0,2,0x20000000000000ULL,0).valid);
    c.valid=false;CHECK(!proxy::errors(empty,c,0,0,2,0,0).valid);c.valid=true;
    const auto mode=std::fegetround();std::fesetround(FE_UPWARD);
    CHECK(!proxy::errors(empty,c,0,0,2,0,0).valid);std::fesetround(mode);
    // Zero proxy / nonzero original support and aligned perturbations exercise
    // the mandatory R^2 term independently of the trace cross term.
    c.energy_upper=4;c.gram_radius={0,0};
    const auto added=proxy::errors(empty,c,0,1,2,0,1,1,1);
    CHECK(added.valid && added.dot>=2 && added.gram>=4);
    CorrelationFit aligned;aligned.cc=16;
    const auto aligned_bound=proxy::errors(aligned,c,0,.25,2,0,1,1,2.25);
    CHECK(aligned_bound.valid && aligned_bound.dot>=2 && aligned_bound.gram>=9);
    proxy::ResidualSquares tiny;tiny.observe(std::numeric_limits<double>::denorm_min());
    CHECK(tiny.valid && tiny.upper>0);
    CHECK(proxy::errors(empty,c,0,tiny.upper,2,0,1).valid);
    CHECK(!proxy::errors(empty,c,0,0,2,0,1,-1,0).valid);
    std::cout<<"scheduled template L2 proxy certificate passed\n";
    std::cout<<"checked_predicates="<<checked_predicates<<'\n';
}
