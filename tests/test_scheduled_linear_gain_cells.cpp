#include "scheduled_linear_gain_cells.hpp"
#include "coarse_pulse_cells.hpp"
#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include <iostream>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace gains=d::scheduled_linear_gain_cells;
namespace coarse=d::scheduled_coarse_pulse_cells;
#define CHECK(c) do{if(!(c))throw Error("check failed: " #c);}while(false)
static double H(double x){const auto t=std::clamp(x,0.,1.);return t*t*t*(10+t*(-15+6*t));}
static double g(double x,double s,double h,bool falling){return H(x/s)*H((falling?55-x:10+x)/h);}
static double upper(){const auto u=std::numeric_limits<double>::epsilon()/2;const auto z=1+31*d::scheduled_acquisition_certificate::gamma(8,u);return std::nextafter(z*z*(1+d::scheduled_acquisition_certificate::gamma(1,u)),INFINITY);}
void geometry(){d::scheduled_linear_grid::Cell c;c.supported=c.observed=true;c.left={std::complex<double>{1,.3},std::complex<double>{-.2,.7}};c.right={std::complex<double>{-1,.2},std::complex<double>{.8,.1}};c.residual={0,0};
    gains::Request r;r.left_gain=.2;r.right_gain=.8;r.width=4.25;r.gain_upper=upper();r.amplitude=.3;r.scheduled_ramp=61;r.fhss_ramp=35;r.qualified=true;
    CHECK(gains::apply(c,r).supported);r.qualified=false;CHECK(!gains::apply(c,r).supported);r.qualified=true;
    r.left_gain=0;CHECK(!gains::apply(c,r).supported);r.left_gain=1;r.right_gain=1;r.scheduled_ramp=r.fhss_ramp=0;
    const auto flat=gains::apply(c,r);CHECK(flat.supported&&flat.left==c.left&&flat.right==c.right&&flat.residual==c.residual);
    r.width=INFINITY;CHECK(!gains::apply(c,r).supported);r.width=4.25;r.coordinate_radius=.01;CHECK(!gains::apply(c,r).supported);
}
void actual(){for(const bool encrypted:{false,true})for(const unsigned Q:{2U,4U,8U,16U}){
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,encrypted,1500,encrypted?10:1,
        OuterDsssVersion::interleaved_v2,SearchArithmetic::default_mode).config;c.sample_rate=6120;
    if(encrypted){transfer::Options o;o.modem=c;o.key.emplace(Bytes(32,0x67));c=transfer::seeded_config(o,1800000000);}
    PatternCode pattern(c,1800000000);pattern.set_stream_phase_samples(136);pattern.prepare_symbol(0);
    const auto C=pattern.chip_samples();const double width=double(C)/Q,amplitude=.3;
    for(unsigned phase=0;phase<Q;++phase){const auto k=std::uint64_t(100)*Q+phase;const auto first=double(k)*C/Q;
        coarse::Request request{pattern.symbol_samples(),C,k,Q,amplitude,0,true};
        const auto raw=coarse::certify(request,[&](auto j){return pattern.values(j);},[&](double x){return pattern.shaped_values(0,x);});CHECK(raw.cell.supported);
        for(const bool falling:{false,true})for(const double begin:{3.,17.,32.}){
            gains::Request r;r.left_gain=g(begin,61,35,falling);r.right_gain=g(begin+width,61,35,falling);
            r.width=std::nextafter(width,INFINITY);r.coordinate_radius=1e-7;r.gain_upper=upper();r.amplitude=.3;r.scheduled_ramp=61;r.fhss_ramp=35;r.qualified=true;
            if(!(r.left_gain>0&&r.right_gain>0))continue;const auto bounded=gains::apply(raw.cell,r);CHECK(bounded.supported);
            for(unsigned i=0;i<=128;++i){const auto t=double(i)/128;const auto q=pattern.shaped_values(0,first+t*width);const auto gain=g(begin+t*width,61,35,falling);
                for(unsigned bit=0;bit<2;++bit){const auto actual=gain*pattern_limit_pcm(amplitude*q[bit])/amplitude;
                    const auto line=(1-t)*bounded.left[bit]+t*bounded.right[bit];CHECK(std::abs(actual-line)<=bounded.residual[bit]);}}
        }
    }
}}
int main(){try{geometry();actual();std::cout<<"native gain product chords preserve actual public/private independent candidate enclosures\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
