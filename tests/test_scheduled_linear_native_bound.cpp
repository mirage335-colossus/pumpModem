#include "scheduled_linear_native_bound.hpp"
#include "coarse_pulse_cells.hpp"
#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include <iostream>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace native=d::scheduled_linear_native_bound;
namespace coarse=d::scheduled_coarse_pulse_cells;
#define CHECK(c) do{if(!(c))throw Error("check failed: " #c);}while(false)
void metadata(){native::Request r;r.symbol=87040;r.chip=34;r.groups=40;r.fhss_dwells_per_group=3;
    r.rate=1.000051L;r.magnitude=2000000;r.qmax=3.6;r.source_q_upper=.7;r.amplitude=.3;r.gain_upper=1+1024*std::numeric_limits<double>::epsilon();
    r.gate_slope=.1;r.pulse_peak=1.1;r.pulse_slope=2;r.pulse_jump=.006;r.scheduled_ramp=61;r.fhss_support=35;r.fhss=true;r.qualified=true;
    const auto b=native::build(r);const auto cap=r.qmax;
    if(d::scheduled_acquisition_certificate::coordinate_environment()) {
        CHECK(b.valid&&b.mask_count==656);CHECK(b.coordinate>0&&b.coordinate<.001);CHECK(b.edge_radius>2&&b.edge_radius<2.001);
        CHECK(b.scheduled_gain>0&&b.scheduled_gain<.001&&b.fhss_gain<.003);CHECK(b.square>0&&b.l1>0&&b.gram>0);
        r.source_q_upper=r.qmax;const auto full=native::build(r);CHECK(full.valid&&full.square>b.square&&full.l1>b.l1);
    } else CHECK(!b.valid&&b.mask_count==0&&std::isinf(b.coordinate)&&std::isinf(b.edge_radius));
    const auto edge_radius=b.valid?b.edge_radius:2.0001;
    const auto upper=native::edge_gain(edge_radius,61,r.gain_upper);
    for(unsigned i=0;i<=10000;++i){const auto t=double(i)/10000*double(edge_radius/61);const auto gain=t*t*t*(10+t*(-15+6*t));CHECK(gain<=upper);}
    r.qualified=false;CHECK(!native::build(r).valid);r.qualified=true;r.rate=1.02;CHECK(!native::build(r).valid);r.rate=1;
    r.groups=UINT64_MAX;CHECK(!native::build(r).valid);r.groups=40;r.magnitude=0x1p53L;CHECK(!native::build(r).valid);r.magnitude=2000000;
    r.source_q_upper=cap+1;CHECK(!native::build(r).valid);CHECK(!std::isfinite(native::edge_gain(2,0,1)));
}
void raw_cutoff(){auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
    OuterDsssVersion::interleaved_v2,SearchArithmetic::default_mode).config;c.sample_rate=6120;
    transfer::Options o;o.modem=c;o.key.emplace(Bytes(32,0x67));c=transfer::seeded_config(o,1800000000);
    PatternCode pattern(c,1800000000);pattern.set_stream_phase_samples(136);pattern.prepare_symbol(0);CHECK(pattern.chip_samples()==34);
    const auto amplitude=.3;
    for(const auto Q:{2U,4U,8U,16U})for(const auto last:{false,true}){
        const auto count=pattern.symbol_samples()/pattern.chip_samples()*Q;const auto cell=last?count-1:0;
        coarse::Request r{pattern.symbol_samples(),pattern.chip_samples(),cell,Q,amplitude,0,true};
        std::size_t coefficient_calls=0,endpoint_calls=0;
        const auto coeff=[&](auto k){++coefficient_calls;return pattern.values(k);};
        const auto raw=[&](double x){++endpoint_calls;return pattern.shaped_values(0,x);};
        CHECK(!coarse::certify(r,coeff,raw).cell.supported);r.raw_endpoint_bound=true;
        const auto b=coarse::certify(r,coeff,raw);
        if(!d::scheduled_acquisition_certificate::coordinate_environment()) {
            CHECK(!b.cell.supported&&b.coefficient_reads==0&&b.endpoint_reads==0&&
                coefficient_calls==0&&endpoint_calls==0&&std::isinf(b.execution_radius));
            continue;
        }
        CHECK(b.cell.supported);const auto first=double(cell)*pattern.chip_samples()/Q,width=double(pattern.chip_samples())/Q;
        for(unsigned i=0;i<=64;++i){const auto t=double(i)/64;const auto q=pattern.shaped_values(0,first+t*width);
            for(unsigned bit=0;bit<2;++bit){const auto actual=pattern_limit_pcm(amplitude*q[bit])/amplitude;
                const auto line=(1-t)*b.cell.left[bit]+t*b.cell.right[bit];CHECK(std::abs(actual-line)<=b.cell.residual[bit]);
                CHECK(std::abs(actual)<=std::max(std::abs(b.cell.left[bit]),std::abs(b.cell.right[bit]))+b.cell.residual[bit]);}}
    }
}
int main(){try{metadata();raw_cutoff();std::cout<<"native piece mask categories, outward ramp caps and actual raw source endpoint bounds passed\n";}
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
