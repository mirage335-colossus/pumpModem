#include "scheduled_gain_chord_certificate.hpp"
#include <iostream>
#include <stdexcept>
namespace m=datapump::modem;
namespace d=m::detail;
namespace g=d::scheduled_gain_chord;
namespace p=d::scheduled_pulse_chord;
namespace derivative=d::scheduled_pulse_derivative_chord;
using C=std::complex<double>;using W=std::complex<long double>;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
W wide(C x){return {x.real(),x.imag()};}
p::Pair original(double source,std::uint64_t symbol,std::uint64_t chip) {
    p::Pair result;
    constexpr C coefficients[]={{1,.5},{-.5,-1},{.75,-.25},{-.25,.75}};
    for(unsigned bit=0;bit<2;++bit)result[bit]=m::pattern_pulse_sum(source,symbol,chip,[&](std::uint64_t j){return coefficients[(j+bit)%4];});
    return result;
}
int main() {
    if(!d::scheduled_acquisition_certificate::coordinate_environment())return 77;
    constexpr std::uint64_t chip=64,symbol=8192+17;constexpr double amplitude=.1;
    derivative::DerivativeTable table(chip,derivative::DerivativeTable::reservation());CHECK(table.valid());
    m::SchedHopLocation location;location.active=true;location.physical_begin=50;location.physical_end=950;
    location.rise_begin=100;location.flat_begin=200;location.flat_end=800;location.fall_end=900;
    const m::SchedHopCoordinate center{125,.125L};m::FhssPowerShape disabled;
    const auto left=center.add_signed(-.505L),right=center.add_signed(.505L);
    const auto gl=m::SchedHopSourceMap::gain(location,left),gr=m::SchedHopSourceMap::gain(location,right);
    for(const double source:{2048.125,3000.25,8190.125}) {
        p::Request request{source,source+1.01,amplitude,1,2,symbol,chip,2048,table.working_bytes()+8192,true,true};
        std::size_t callbacks=0;
        const auto proof=g::certify_row(request,[&](double x,std::stop_token){++callbacks;return original(x,symbol,chip);},
            location,disabled,center,.505L,1e-12L,0,48000,gl,gr,false,false,{},&table);
        CHECK(proof.value.valid);CHECK(callbacks==2);CHECK(proof.evaluations==2);
        for(unsigned k=0;k<=256;++k) {
            const auto t=static_cast<long double>(k)/256;
            const auto gain=m::SchedHopSourceMap::gain(location,center.add_signed((2*t-1)*.505L));
            const auto q=original(static_cast<double>(request.first+t*(request.last-request.first)),symbol,chip);
            for(unsigned bit=0;bit<2;++bit) {
                const auto actual=gain*m::pattern_limit_pcm(amplitude*q[bit])/amplitude;
                const auto mix=(1-t)*wide(proof.value.left[bit])+t*wide(proof.value.right[bit]);
                CHECK(std::abs(wide(actual)-mix)<=proof.value.residual[bit]);
            }
        }
        request.workspace=table.working_bytes()+8191;callbacks=0;
        const auto short_workspace=g::certify_row(request,[&](double x,std::stop_token){++callbacks;return original(x,symbol,chip);},
            location,disabled,center,.505L,1e-12L,0,48000,gl,gr,false,false,{},&table);
        CHECK(!short_workspace.value.valid);CHECK(callbacks==0);
    }
    // Omission of the optional pointer remains source-compatible and uses the
    // existing knot certificate; a different C fails before private callbacks.
    derivative::DerivativeTable mismatch(127,derivative::DerivativeTable::reservation());CHECK(mismatch.valid());
    p::Request request{3000,3001.01,amplitude,1,2,symbol,chip,2048,table.working_bytes()+8192,true,true};
    std::size_t callbacks=0;
    const auto bad=g::certify_row(request,[&](double x,std::stop_token){++callbacks;return original(x,symbol,chip);},
        location,disabled,center,.505L,1e-12L,0,48000,gl,gr,false,false,{},&mismatch);
    CHECK(!bad.value.valid);CHECK(callbacks==0);
    request.workspace=p::reservation(2048);
    const auto ordinary=g::certify_row(request,[&](double x,std::stop_token){return original(x,symbol,chip);},
        location,disabled,center,.505L,1e-12L,0,48000,gl,gr,false,false);
    CHECK(ordinary.value.valid);
    std::cout<<"gain derivative hook proposal checks passed\n";
}
