#include "scheduled_pulse_chord_certificate.hpp"
#include <cfenv>
#include <iostream>
#include <stdexcept>

namespace m=datapump::modem;
namespace h=m::detail::scheduled_pulse_chord;
namespace c=m::detail::scheduled_acquisition_certificate;
static std::size_t predicates=0;
#define CHECK(...) do {++predicates;if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(false)
using Wide=std::complex<long double>;
struct Fixture {
    std::uint64_t symbol,chip;
    std::vector<h::Pair> coefficients;
    Fixture(std::uint64_t s,std::uint64_t q):symbol(s),chip(q),coefficients((s+q-1)/q) {
        for(std::size_t j=0;j<coefficients.size();++j)coefficients[j]={
            std::polar(.5+.2*std::sin(.17*j),.31*j),std::polar(.6+.1*std::cos(.11*j),-.23*double(j))};
    }
    h::Pair operator()(double sample,std::stop_token stop={})const {
        h::check_stop(stop);h::Pair result{};
        m::pattern_pulse_each(sample,symbol,chip,[&](std::uint64_t local,double p) {
            result[0]+=p*coefficients[local][0];result[1]+=p*coefficients[local][1];
        });return result;
    }
};
static h::Request request(const Fixture& f,double first,double last) {
    h::Request r;r.first=first;r.last=last;r.amplitude=.05;r.flat_gain=.83;r.coefficient_norm_upper=2;
    r.symbol_samples=f.symbol;r.chip_samples=f.chip;r.original_math_qualified=true;r.flat_gain_proven=true;
    r.workspace=h::reservation(r.maximum_points);return r;
}
static void paired(std::uint64_t chip,std::uint64_t tail,double first,double last,bool expect_jump) {
    Fixture f(40*chip+tail,chip);const auto r=request(f,first,last);
    const auto result=h::certify(r,f);
    CHECK(result.valid && result.failure==h::Failure::none);
    CHECK(result.evaluations==result.points && result.points>=2);
    CHECK(result.peak_temporary_bytes<=r.workspace);
    CHECK((result.support_edge_terms>0)==expect_jump);
    for(unsigned bit=0;bit<2;++bit) {
        const auto a=f(first)[bit],b=f(last)[bit];
        CHECK(result.left[bit]==r.flat_gain*m::pattern_limit_pcm(r.amplitude*a)/r.amplitude);
        CHECK(result.right[bit]==r.flat_gain*m::pattern_limit_pcm(r.amplitude*b)/r.amplitude);
    }
    // Dense deterministic points test the ORIGINAL executed producer against
    // the endpoint chord, including physical/source fractions near knots.
    const auto check_source=[&](double source) {
        const auto t=(static_cast<long double>(source)-first)/(static_cast<long double>(last)-first);
        const auto original=f(source);
        for(unsigned bit=0;bit<2;++bit) {
            const auto value=r.flat_gain*m::pattern_limit_pcm(r.amplitude*original[bit])/r.amplitude;
            const auto a=result.left[bit],b=result.right[bit];
            const Wide chord=(1-t)*Wide(a.real(),a.imag())+t*Wide(b.real(),b.imag());
            const auto error=std::abs(Wide(value.real(),value.imag())-chord);
            CHECK(error<=result.residual[bit]);
            CHECK(std::abs(Wide(value.real(),value.imag()))<=result.maximum_norm[bit]);
        }
    };
    for(unsigned i=0;i<=513;++i)check_source(first+(last-first)*(double(i)/513.));
    check_source(std::nextafter(first,last));check_source(std::nextafter(last,first));
    for(std::size_t j=0;j<f.coefficients.size();++j) {
        const auto duration=std::min(chip,f.symbol-j*chip);
        const auto center=double(j*chip)+.5*double(duration);
        for(const auto edge:{center-8.*chip,center+8.*chip}) {
            if(edge>first && edge<last) {
                check_source(edge);check_source(std::nextafter(edge,first));
                check_source(std::nextafter(edge,last));
            }
        }
    }
    // A closed finite-support edge is not made continuous by interpolation.
    if(expect_jump)CHECK(result.jump_radius>0);
    std::cout<<"C="<<chip<<" tail="<<tail<<" points="<<result.points<<" edges="<<result.support_edge_terms
        <<" residual0="<<result.residual[0]<<" residual1="<<result.residual[1]<<'\n';
}
int main(){try {
    // Unsupported coordinate environments must retain runtime fallback rather
    // than make this optional local certificate a platform requirement.
    if(!c::coordinate_environment()) {
        Fixture f(320,8);CHECK(!h::certify(request(f,70.1,70.9),f).valid);
        std::cout<<"unsupported chord coordinate producer; rejection checked\n";return 0;
    }
    paired(8,0,70.1,70.9,false);
    paired(8,0,67.5,68.5,true); // regular closed support edge at8.5C
    paired(17,3,545.0,546.0,true); // final partial's left support edge:680+1.5−136=545.5
    paired(17,4,545.5,546.5,true); // even partial center adds its own translated grid
    paired(1,0,9.1,10.09,true);
    constexpr auto large_chip=std::uint64_t{1}<<34;
    paired(large_chip,3,8.5*large_chip-.25,8.5*large_chip+.25,true);
    Fixture f(320,8);auto r=request(f,70.1,70.9);
    r.flat_gain_proven=false;CHECK(!h::certify(r,f).valid);r.flat_gain_proven=true;
    r.original_math_qualified=false;CHECK(!h::certify(r,f).valid);r.original_math_qualified=true;
    r.last=320;CHECK(!h::certify(r,f).valid);r.last=70.9;
    r.workspace=1;CHECK(h::certify(r,f).failure==h::Failure::workspace);r.workspace=h::reservation(r.maximum_points);
    r.maximum_points=2;CHECK(h::certify(r,f).failure==h::Failure::knot_cap);r.maximum_points=2048;
    r.amplitude=1000;CHECK(h::certify(r,f).failure==h::Failure::clipping);r.amplitude=.05;
    r.amplitude=std::numeric_limits<double>::denorm_min();
    const auto tiny=h::certify(r,f);
    if(tiny.valid) {
        const auto source=.5*(r.first+r.last);
        const long double weight=(static_cast<long double>(source)-r.first)/(static_cast<long double>(r.last)-r.first);
        const auto actual=f(source);
        for(unsigned bit=0;bit<2;++bit) {
            const auto q=r.flat_gain*m::pattern_limit_pcm(r.amplitude*actual[bit])/r.amplitude;
            const Wide chord=(1-weight)*Wide(tiny.left[bit].real(),tiny.left[bit].imag())
                +weight*Wide(tiny.right[bit].real(),tiny.right[bit].imag());
            CHECK(std::abs(Wide(q.real(),q.imag())-chord)<=tiny.residual[bit]);
        }
    } else CHECK(tiny.failure==h::Failure::arithmetic || tiny.failure==h::Failure::clipping);
    r.amplitude=.05;
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)h::certify(r,f,stop.get_token());}catch(const std::exception&){cancelled=true;}CHECK(cancelled);
    const auto mode=std::fegetround();std::fesetround(FE_UPWARD);
    const auto refused=!h::certify(r,f).valid;std::fesetround(mode);CHECK(refused);
    std::cout<<"pulse chord proposed predicates="<<predicates<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
