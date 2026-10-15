#include "scheduled_gain_chord_certificate.hpp"
#include <iostream>
#include <stdexcept>
namespace m=datapump::modem;
namespace d=m::detail;
namespace g=d::scheduled_gain_chord;
namespace c=d::scheduled_acquisition_certificate;
namespace p=d::scheduled_pulse_chord;
using C=std::complex<double>;
using W=std::complex<long double>;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
W wide(C x){return {x.real(),x.imag()};}
C coefficient(std::uint64_t j,unsigned bit) {
    constexpr C q[]={{1,.5},{-.75,1},{.25,-1},{-1,-.5}};
    return q[(j+bit*3)%4];
}
p::Pair original(double x,std::uint64_t symbol,std::uint64_t chip) {
    p::Pair q;for(unsigned bit=0;bit<2;++bit)
        q[bit]=m::pattern_pulse_sum(x,symbol,chip,[&](std::uint64_t j){return coefficient(j,bit);});
    return q;
}
void local_envelope_against_native_sum() {
    for(const auto chip:{64ULL,127ULL})for(const auto tail:{0ULL,17ULL}) {
        const auto symbol=128*chip+tail;
        for(const double center:{0.,.5,8.,16.,63.,127.5})for(unsigned k=0;k<=256;++k) {
            const auto x=std::min(std::nextafter(static_cast<double>(symbol),0.),center*chip+chip*k/256.);
            if(x<0)continue;
            for(const auto at:{std::nextafter(x,0.),x,std::nextafter(x,static_cast<double>(symbol))}) {
                const auto point=std::clamp(at,0.,static_cast<double>(symbol));
                const auto bound=g::raw_norm_upper(point,point,symbol,chip,2,true);
                CHECK(std::isfinite(bound));for(const auto q:original(point,symbol,chip))CHECK(c::norm(q)<=bound);
            }
        }
        // The exact partial-chip support endpoints and their native neighbors.
        if(tail){const auto mid=static_cast<double>(symbol-tail)+tail/2.;
            for(const auto x:{mid-8*chip,mid+8*chip})if(x>=0 && x<=symbol)
                for(const auto at:{std::nextafter(x,0.),x,std::nextafter(x,static_cast<double>(symbol))})
                    for(const auto q:original(at,symbol,chip))CHECK(c::norm(q)<=g::raw_norm_upper(at,at,symbol,chip,2,true));}
    }
    CHECK(!std::isfinite(g::limited_gain_norm_upper(8,std::numeric_limits<double>::max(),1)));
}
void scheduled_ramp_product_against_native_target() {
    m::SchedHopLocation location;location.active=true;location.physical_begin=50;location.physical_end=950;
    location.rise_begin=100;location.flat_begin=200;location.flat_end=800;location.fall_end=900;
    m::FhssPowerShape disabled;
    constexpr double amplitude=.1;constexpr std::uint64_t symbol=8192,chip=64;
    for(const auto physical:{100.25L,125.L,199.75L,400.L,799.75L,875.L,899.75L}) {
        const m::SchedHopCoordinate center{static_cast<std::uint64_t>(physical),physical-std::floor(physical)};
        const auto first=3000.,last=3001.01;
        p::Request request{first,last,amplitude,1,2,symbol,chip,2048,p::reservation(2048),true,true};
        const auto left=center.add_signed(-.505L),right=center.add_signed(.505L);
        const auto proof=g::certify_row(request,[&](double x,std::stop_token){return original(x,symbol,chip);},
            location,disabled,center,.505L,1e-12L,0,48000,
            m::SchedHopSourceMap::gain(location,left),m::SchedHopSourceMap::gain(location,right),false,true);
        CHECK(proof.value.valid);
        for(unsigned k=0;k<=256;++k){const auto t=static_cast<long double>(k)/256;
            const auto physical_target=center.add_signed((2*t-1)*.505L);
            const auto gain=m::SchedHopSourceMap::gain(location,physical_target);
            const auto q=original(static_cast<double>(first+t*(last-first)),symbol,chip);
            for(unsigned bit=0;bit<2;++bit){const auto actual=gain*m::pattern_limit_pcm(amplitude*q[bit])/amplitude;
                const auto mix=(1-t)*wide(proof.value.left[bit])+t*wide(proof.value.right[bit]);
                CHECK(std::abs(wide(actual)-mix)<=proof.value.residual[bit]);}
        }
    }
    const m::SchedHopCoordinate edge{50,.25L};
    CHECK(!g::active_interval(location,edge,.5L,1e-12));
}
void fhss_flat_cell_and_fail_closed() {
    m::FhssPowerShape shape;shape.enabled=true;shape.smooth=true;shape.quiet=100;shape.support=100;
    shape.period=19200;constexpr std::uint32_t rate=48000;
    CHECK(g::fhss_flat_interior(shape,{5000,.25L},.505L,1e-8L,rate,true));
    CHECK(!g::fhss_flat_interior(shape,{19200,0},.505L,1e-8L,rate,true));
    CHECK(!g::fhss_flat_interior(shape,{5000,.25L},.505L,1e-8L,rate,false));
    m::SchedHopLocation l;l.active=l.continuous=true;
    CHECK(!g::gain_proof(l,shape,1.01,1e-8,1e-8,true).valid);
}
int main() {
    if(!c::coordinate_environment()){std::cerr<<"unsupported coordinate environment\n";return 77;}
    local_envelope_against_native_sum();scheduled_ramp_product_against_native_target();fhss_flat_cell_and_fail_closed();
    std::cout<<"gain-chord proposal checks passed\n";
}
