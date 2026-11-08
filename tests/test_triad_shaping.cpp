#include "datapump/triad_shaping.hpp"
#include "datapump/triad_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace datapump::triad::v3;
void check(bool good,const char* why){if(!good)throw std::runtime_error(why);}
template<class F> bool rejects(F&& fn){try{fn();return false;}catch(const datapump::Error&){return true;}}
template<class F> std::string rejection(F&& fn){try{fn();return {};}catch(const datapump::Error& e){return e.what();}}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
double old_envelope(const Format& f,const Geometry& g,long double t){
    if(t<0||t>=g.bit_seconds)return 0;
    const long double edge=std::max(f.ramp_seconds,8/g.fine_chip_rate_hz);
    double result=smooth(static_cast<double>(t/edge))*smooth(static_cast<double>((g.bit_seconds-t)/edge));
    if(f.burst_wait_tiles){const auto id=static_cast<std::uint64_t>(std::floor(t/g.tile_seconds));
        if(id%(f.burst_wait_tiles+1)!=0||id/(f.burst_wait_tiles+1)>=g.tile_count)return 0;
        const auto within=t-id*static_cast<long double>(g.tile_seconds);
        result*=smooth(static_cast<double>(within/edge))*smooth(static_cast<double>((g.tile_seconds-within)/edge));}
    if(f.fhss){const auto within=t-std::floor(t/f.dwell_seconds)*f.dwell_seconds;
        const auto distance=std::min(within,f.dwell_seconds-within);
        result*=smooth(static_cast<double>((distance-f.quiet_seconds)/f.ramp_seconds));}
    return result;
}
Format eme(){Format f;f.inner_rate_hz=360;f.added_symbols=1;f.sample_rate=18000;
    f.bit_seconds=16;f.fhss=true;f.fhss_center_aligned=true;f.quiet_seconds=.055;f.ramp_seconds=.006;return f;}
void pulse_and_envelope_checks(){
    double energy=0;
    for(unsigned j=0;j<=4096;++j){const auto t=-8+j/256.;
        check(shaping_pulse(PulseShape::Rrc,t)==datapump::triad::v3::rrc_pulse(t),"legacy pulse changed");
        const auto v=shaping_pulse(PulseShape::TaperedRrc,t);energy+=v*v*(j==0||j==4096?.5:1)/256;}
    check(std::abs(energy-1)<2e-5,"tapered pulse lost unit energy");
    check(shaping_pulse(PulseShape::TaperedRrc,8)==0&&shaping_pulse(PulseShape::TaperedRrc,8.01)==0,
        "tapered pulse finite tail changed");
    for(auto f:{Format{},eme()})for(unsigned burst:{0U,3U}){
        f.burst_wait_tiles=burst;f.bit_seconds=80;const auto g=geometry(f);
        for(unsigned j=0;j<33;++j){const long double t=(g.bit_seconds+.2L)*j/32-.1L;
            check(shaping_envelope(f,g,t)==old_envelope(f,g,t),"legacy source envelope changed");}}
}
void planner_checks(){
    const auto before=eme();const auto original=geometry(before);
    const auto plan=plan_shaping(before);
    check(!plan.fallback&&plan.spectrum.converged,"EME spectral model did not qualify");
    check(plan.spectrum.phase_samples==24&&plan.spectrum.phase_refinement_attempts==1,
        "small exact phase bank repeated unnecessary adaptive work");
    check(plan.geometry.fine_coefficients==original.fine_coefficients&&plan.geometry.tile_seconds==original.tile_seconds&&
        plan.geometry.source_samples==original.source_samples,"planner changed protected allocation/calendar");
    check(plan.format.ramp_seconds>=before.ramp_seconds&&plan.format.ramp_seconds<.006667,
        "ramp choice lost protected center");
    check(plan.spectrum.placement_bandwidth_hz>=225&&plan.spectrum.placement_bandwidth_hz<310,
        "measured gated spectrum retained unnecessary full2/ramp allowance");
    check(plan.spectrum.measured_tail_fraction<=.0011&&std::abs(plan.spectrum.normalized_pulse_energy-1)<1e-5,
        "spectrum certificate omitted pulse/tail energy");
    check(plan.format.pulse_shape==PulseShape::TaperedRrc,"far-spill selection missed smooth finite pulse");
    ShapingOptions cap;cap.maximum_fft_size=4096;
    const auto fallback=plan_shaping(before,cap);
    check(fallback.fallback&&!fallback.spectrum.converged&&fallback.spectrum.placement_bandwidth_hz==225+2/before.ramp_seconds,
        "work cap silently reduced the planning band");
    check(fallback.total_kernel_ffts==0,"ineligible FFT work cap repeated spectral work");
    cap.maximum_fft_size=16384;
    check(plan_shaping(before,cap).total_kernel_ffts==0,
        "ineligible refinement FFT cap unnecessarily executed coarse profiles");
    auto slow=before;slow.inner_rate_hz=.001;
    check(rejects([&]{(void)plan_shaping(slow);}),"infeasible low-rate FHSS was relabeled as supported");
    auto capped=before;capped.inner_rate_hz=.36;capped.added_symbols=1000;
    const auto geometry_error=rejection([&]{(void)geometry(capped);});
    check(geometry_error=="triad v3 protected tile exceeds bounded source workspace"&&
        rejection([&]{(void)plan_shaping(capped);})==geometry_error,
        "maximal-Added tile cap was misreported as a physical protected-pulse failure");
    auto other=before;other.inner_rate_hz=36;other.added_symbols=10;
    const auto equivalent=shaping_spectrum(other);
    check(equivalent.placement_bandwidth_hz>0&&equivalent.phase_samples==24,
        "Rate/Added equivalent fine rate spectrum failed");
    auto nohop=before;nohop.fhss=nohop.fhss_center_aligned=false;nohop.quiet_seconds=0;
    const auto nohop_spectrum=shaping_spectrum(nohop);
    check(nohop_spectrum.phase_samples==1&&nohop_spectrum.placement_bandwidth_hz<250,
        "FHSS-off received a periodic dwell gate");
    nohop.burst_wait_tiles=3;nohop.bit_seconds=80;
    check(shaping_spectrum(nohop).converged,"finite Symbol Burst endpoint spectrum missing");
    std::cout<<"EME width="<<plan.spectrum.placement_bandwidth_hz<<" ramp="<<plan.format.ramp_seconds
        <<" tail="<<plan.spectrum.measured_tail_fraction<<" kernels="<<plan.spectrum.kernel_ffts<<'\n';
}
} // namespace
int main(){
    try{pulse_and_envelope_checks();planner_checks();
        std::cout<<"bounded shaping arithmetic and planning invariants passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
