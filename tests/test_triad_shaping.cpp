#include "triad_crypto_fixture.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/pattern_pulse.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
Format noncommensurate_fixture(){auto f=eme();f.inner_rate_hz=1000;f.ramp_seconds=2./3600;f.bit_seconds=40;return f;}
const ShapingPlan& noncommensurate_plan(){
    static const auto selected=plan_shaping(noncommensurate_fixture());return selected;
}
void pulse_and_legacy_checks(){
    double energy=0;
    for(unsigned j=0;j<=65536;++j){const auto t=-8+j/4096.;
        check(shaping_pulse(PulseShape::LegacyRrc,t)==datapump::modem::pattern_pulse(t),"legacy pulse changed");
        const auto v=shaping_pulse(PulseShape::TaperedRrc,t);energy+=v*v*(j==0||j==65536?.5:1)/4096;}
    check(std::abs(energy-1)<2e-5,"tapered pulse lost unit energy");
    check(shaping_pulse(PulseShape::TaperedRrc,8)==0&&shaping_pulse(PulseShape::TaperedRrc,8.01)==0,
        "tapered pulse finite tail changed");
    for(auto f:{Format{},eme()})for(unsigned burst:{0U,3U}){
        f.burst_wait_tiles=burst;f.bit_seconds=80;const auto g=geometry(f);
        for(unsigned j=0;j<10007;++j){const long double t=(g.bit_seconds+.2L)*j/10006-.1L;
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
void adaptive_phase_checks(){
    ShapingOptions limited;limited.maximum_phase_samples=64;
    const auto input=noncommensurate_fixture();
    const auto fallback=plan_shaping(input,limited);
    check(fallback.fallback&&fallback.spectrum.placement_bandwidth_hz>4000,
        "explicit64-phase cap did not retain conservative fallback");
    const auto& selected=noncommensurate_plan();
    check(!selected.fallback&&selected.spectrum.converged&&selected.spectrum.placement_bandwidth_hz<=800,
        "bounded adaptive phase refinement failed valid noncommensurate waveform");
    check(selected.spectrum.phase_samples==256&&selected.spectrum.phase_refinement_attempts==3,
        "noncommensurate regression did not exercise phase refinement ladder");
    check(selected.geometry.payload_fine_cells_per_tile==600&&std::abs(selected.geometry.tile_seconds-8.8)<1e-12,
        "adaptive phase refinement changed protected allocation or tile duration");
    check(selected.spectrum.total_kernel_ffts>=selected.spectrum.kernel_ffts&&
        selected.total_kernel_ffts>=selected.spectrum.total_kernel_ffts,
        "adaptive work counters hide preceding looks or other candidates");
    std::cout<<"adaptive_rate1000 width_hz="<<selected.spectrum.placement_bandwidth_hz
        <<" phases="<<selected.spectrum.phase_samples<<" attempts="<<selected.spectrum.phase_refinement_attempts
        <<" certificate_kernel_ffts="<<selected.spectrum.kernel_ffts
        <<" selected_candidate_total_kernel_ffts="<<selected.spectrum.total_kernel_ffts
        <<" entire_planner_kernel_ffts="<<selected.total_kernel_ffts<<'\n';
}
datapump::Crypto source_key(unsigned seed){
    // Fixed independent fixture roots, unrelated to the spectral planner.
    std::array<std::uint8_t,32> key{};
    std::uint32_t state=seed;
    for(auto& value:key){state=1664525U*state+1013904223U;value=static_cast<std::uint8_t>(state>>24);}
    return triad_test_key(key);
}
struct Evidence {
    double e0=0,e1=0,difference=0;
    Complex cross{};
    Evidence& operator+=(const Evidence& other){e0+=other.e0;e1+=other.e1;
        difference+=other.difference;cross+=other.cross;return *this;}
    double phase_invariant_distance()const{return std::max(0.,e0+e1-2*std::abs(cross));}
};
struct RenderedPair {
    Geometry geometry;
    Evidence total;
    std::vector<Evidence> dwell;
};
using Deadline=std::chrono::steady_clock::time_point;
RenderedPair pair_evidence(const Format& f,const datapump::Crypto& key,const Deadline* deadline=nullptr){
    PatternSource zero(f,key,0),one(f,key,1);
    const auto& g=zero.geometry();
    RenderedPair result{g,{},std::vector<Evidence>(static_cast<std::size_t>(g.dwells))};
    const auto fs=effective_sample_rate(f);
    for(std::uint64_t sample=0;sample<g.source_samples;++sample){
        if(deadline&&(sample%4096==0)&&std::chrono::steady_clock::now()>*deadline)
            throw std::runtime_error("extended shaping source qualification exceeded60-second wall budget; coverage incomplete");
        const auto x0=zero.sample_at(sample),x1=one.sample_at(sample);
        const Evidence row{std::norm(x0)/fs,std::norm(x1)/fs,std::norm(x1-x0)/fs,std::conj(x0)*x1/fs};
        result.total+=row;
        const auto t=(static_cast<long double>(sample)-g.calendar_origin_sample)/fs;
        if(t>=0&&t<g.bit_seconds){const auto dwell=static_cast<std::uint64_t>(std::floor(t/f.dwell_seconds));
            if(dwell<result.dwell.size())result.dwell[static_cast<std::size_t>(dwell)]+=row;}
    }
    return result;
}
double db_ratio(double a,double b){check(a>0&&b>0,"zero comparison evidence");return 10*std::log10(a/b);}
void rendered_energy_checks(bool extended=false){
    Format hf;hf.sample_rate=18000;
    auto hopped=eme();
    auto burst=hopped;burst.burst_wait_tiles=3;burst.bit_seconds=geometry(burst).tile_seconds+.001;
    std::vector<std::pair<const char*,Format>> cases{{"hf",hf},{"fhss",hopped},{"burst3",burst}};
    if(extended){
        auto added100=hf;added100.inner_rate_hz=3.6;added100.added_symbols=100;
        auto added1000=hf;added1000.inner_rate_hz=.36;added1000.added_symbols=1000;
        auto added100hop=hopped;added100hop.inner_rate_hz=3.6;added100hop.added_symbols=100;
        cases={{"added100-continuous",added100},{"added1000-continuous",added1000},{"added100-fhss",added100hop}};
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    const auto key=source_key(0x43e1872U);
    for(const auto& item:cases){
        auto legacy=item.second;const auto selected=plan_shaping(legacy);
        check(!selected.fallback,"rendered acceptance fixture fell back from spectral selection");
        auto shaped=selected.format;legacy.sample_rate=shaped.sample_rate=960;
        const auto before=pair_evidence(legacy,key,extended?&deadline:nullptr),
            after=pair_evidence(shaped,key,extended?&deadline:nullptr);
        check(before.geometry.source_samples==after.geometry.source_samples&&
            before.geometry.fine_coefficients==after.geometry.fine_coefficients&&
            before.geometry.tile_seconds==after.geometry.tile_seconds,
            "rendered shape comparison changed calendar or protected allocation");
        if(legacy.burst_wait_tiles)check(before.geometry.tile_count==2,"Symbol Burst fixture did not realize two active tiles");
        const auto total_db=db_ratio(after.total.e0+after.total.e1,before.total.e0+before.total.e1);
        std::vector<std::size_t> occupied;
        double maximum_dwell=0,min_dwell_db=0;
        for(const auto& d:before.dwell)maximum_dwell=std::max(maximum_dwell,d.difference);
        for(std::size_t i=0;i<before.dwell.size();++i)if(before.dwell[i].difference>maximum_dwell*1e-6){
            occupied.push_back(i);min_dwell_db=std::min(min_dwell_db,db_ratio(after.dwell[i].difference,before.dwell[i].difference));}
        std::cout<<"rendered="<<item.first<<" full_source_energy_change_db="<<total_db
            <<" min_informative_dwell_change_db="<<min_dwell_db<<" informative_dwells="<<occupied.size()<<'\n';
        if(extended)std::cout<<"extended_geometry="<<item.first<<" fine_rate_hz="<<before.geometry.fine_chip_rate_hz
            <<" added="<<legacy.added_symbols<<" inner_coefficients="<<before.geometry.inner_coefficients
            <<" fine_coefficients="<<before.geometry.fine_coefficients<<" calendar_dwells="<<before.geometry.dwells
            <<" source_samples="<<before.geometry.source_samples<<" duration_seconds="<<before.geometry.bit_seconds<<'\n';
        check(std::abs(total_db)<=.1,"shaping changed actual paired source energy by more than0.1dB");
        check(occupied.size()>=20,"rendered bit does not span twenty informative dwells");
        check(min_dwell_db>=-.2,"shaping disproportionately lost per-dwell bit discrimination");
        for(unsigned mask=0;mask<4;++mask){
            std::vector<bool> erase(before.dwell.size());
            if(mask==1){erase[occupied.front()]=true;erase[occupied.back()]=true;}
            if(mask==2){erase[occupied[occupied.size()/2-1]]=true;erase[occupied[occupied.size()/2]]=true;}
            if(mask==3)for(unsigned j=0;j<4;++j)erase[occupied[(5+17*j)%occupied.size()]]=true;
            Evidence a,b;
            for(std::size_t i=0;i<erase.size();++i)if(!erase[i]){a+=before.dwell[i];b+=after.dwell[i];}
            const auto useful_db=db_ratio(b.difference,a.difference);
            const auto phase_db=db_ratio(b.phase_invariant_distance(),a.phase_invariant_distance());
            std::cout<<"rendered="<<item.first<<" mask="<<mask<<" useful_energy_change_db="<<useful_db
                <<" phase_invariant_distance_change_db="<<phase_db<<" retained_difference_fraction="
                <<b.difference/after.total.difference<<'\n';
            check(std::abs(useful_db)<=.1,"shaping/missed-hop mask changed useful difference energy by more than0.1dB");
            check(phase_db>=-.2,"shaping/missed-hop mask lost more than0.2dB phase-invariant pattern distance");
        }
    }
}
double rendered_spectral_tail(const Format& f,const datapump::Crypto& key,unsigned bit,double width){
    PatternSource source(f,key,static_cast<std::uint8_t>(bit));
    const auto length=source.geometry().source_samples;
    const auto n=std::bit_ceil(static_cast<std::size_t>(length));
    std::vector<Complex> values(n);
    source.render(0,std::span(values).first(static_cast<std::size_t>(length)));
    const datapump::modem::detail::search_fft::DoublePlan fft(n);
    datapump::modem::detail::search_fft::fft(values,false,fft);
    const auto fs=effective_sample_rate(f);
    double total=0,tail=0;
    for(std::size_t j=0;j<n;++j){const auto bin=j<=n/2?static_cast<double>(j):static_cast<double>(j)-static_cast<double>(n);
        const auto frequency=bin*fs/static_cast<double>(n);const auto power=std::norm(values[j]);
        total+=power;if(std::abs(frequency)>width/2)tail+=power;}
    check(total>0,"independent rendered spectrum has zero energy");return tail/total;
}
void rendered_spectrum_checks(){
    auto hopped=eme();auto burst=hopped;burst.burst_wait_tiles=3;burst.bit_seconds=geometry(burst).tile_seconds+.001;
    struct Case {const char* name;Format format;unsigned sample_rate,seeds;};
    const std::array<Case,3> cases{{{"fhss",hopped,960,4},{"burst3",burst,960,4},
        {"rate1000-noncommensurate",noncommensurate_fixture(),4000,2}}};
    for(const auto& item:cases){
        const auto selected=item.sample_rate==4000?noncommensurate_plan():plan_shaping(item.format);
        check(!selected.fallback,"independent spectrum fixture unexpectedly fell back");
        const auto width=selected.spectrum.placement_bandwidth_hz;
        std::array<double,2> means{};double maximum=0,refinement=0;
        for(unsigned seed=0;seed<item.seeds;++seed)for(unsigned bit=0;bit<2;++bit){
            const auto key=source_key(0x81b53U+seed*0x147bU);
            std::array<double,2> tails{};
            for(unsigned level=0;level<2;++level){auto f=selected.format;f.sample_rate=item.sample_rate<<level;
                tails[level]=rendered_spectral_tail(f,key,bit,width);means[level]+=tails[level]/(2*item.seeds);
                maximum=std::max(maximum,tails[level]);}
            refinement=std::max(refinement,std::abs(tails[0]-tails[1]));
        }
        // Fixed finite-ensemble engineering checks: not a key-population or
        // stochastic-channel confidence bound. At0.0015 excluded energy the
        // full-waveform collection penalty is only0.0066dB.
        std::cout<<"rendered_spectrum="<<item.name<<" expected_tail="<<selected.spectrum.measured_tail_fraction
            <<" low_sample_rate_hz="<<item.sample_rate<<" seed_count="<<item.seeds
            <<" mean_tail_low_rate="<<means[0]<<" mean_tail_high_rate="<<means[1]<<" max_tail="<<maximum
            <<" max_sample_refinement_error="<<refinement<<" width_hz="<<width<<'\n';
        check(maximum<=.0015,"actual rendered finite-waveform tail exceeds0.15percent acceptance envelope");
        check(refinement<=.0001,"actual rendered spectrum does not agree under doubled sample rate");
    }
}
}
int main(int argc,char** argv){
    const bool extended=argc==2&&std::string_view(argv[1])=="--extended";
    if(argc>1&&!extended){std::cerr<<"usage: test_triad_shaping [--extended]\n";return 2;}
    try{const auto start=std::chrono::steady_clock::now();pulse_and_legacy_checks();planner_checks();
    adaptive_phase_checks();rendered_energy_checks();rendered_spectrum_checks();
    if(extended)rendered_energy_checks(true);
    std::cout<<"triad shaping passed; wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
