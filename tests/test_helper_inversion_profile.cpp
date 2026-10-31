#include "datapump/helper_inversion_profile.hpp"
#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>
namespace {
namespace h=datapump::helper;using P=h::HelperInversionProfile;using C=std::complex<double>;
constexpr std::uint32_t fs=24000;constexpr std::size_t dwell_samples=9600;
constexpr double tau=2*std::numbers::pi,a=1.00005,arrival=.00321,b=24.;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
struct Fixture {
    h::Options options;h::Generator generator;
    std::mt19937_64 rng;std::normal_distribution<double> normal;
    bool auxiliary=true,noise_only=false;
    Fixture(std::uint64_t seed,bool punctuate=true,bool aux=true,bool noise=false)
      :options([]{h::Options o;o.power_w=40;o.timecode=false;return o;}()),
       generator(options,{2400,.4,0,40,aux?950.:0,550,false},1800000000,punctuate),rng(seed),auxiliary(aux),noise_only(noise){}
    std::vector<std::complex<float>> capture(unsigned index,double cn0=std::numeric_limits<double>::infinity()){
        std::vector<std::complex<float>> x(dwell_samples);
        const auto phase=normal(rng)*4;
        const double amp=.4*h::auxiliary_amplitude_fraction;
        const double sigma=std::isfinite(cn0)?amp*std::sqrt(fs/(2*std::pow(10.,cn0/10))):0;
        for(std::size_t j=0;j<x.size();++j){const double t=(index*dwell_samples+j)/double(fs),tx=a*t-arrival;
            const auto z=noise_only?C{}:generator.mix_analytic({},tx,fs)*std::polar(1.,phase+tau*b*t);
            x[j]=std::complex<float>(z+C(sigma*normal(rng),sigma*normal(rng)));}
        return x;
    }
};
P::Input input(std::span<const std::complex<float>> x,unsigned index,double error=0){
    P::Input in;in.samples=x;in.first_sample=index*dwell_samples;in.capture_frontier_end_sample=(index+1)*dwell_samples;in.dwell=index;
    in.predicted_marker_sample=(index*.4+.16)*fs;in.nominal_marker_source_seconds=index*.4+.16;
    in.audio_scale=a;in.audio_half_width_ppm=.2;
    in.auxiliary_frequency_hz=950*a+b+error;in.frequency_half_width_hz=.2;
    in.nuisance_frequency_hz={550*a+b,2400*a+b};return in;
}
P::ClockModel clock(unsigned count){P::ClockModel c;c.available=c.reference_preserved=true;c.reference_input_sample=0;
    c.anchor_input_sample=0;c.source_seconds_at_anchor=0;c.audio_scale=a;c.source_half_width_seconds=2e-6;c.audio_half_width_ppm=.02;
    c.latest_observation_end_sample=count*dwell_samples;return c;}
void strong(){
    P p({});Fixture f(11);
    for(unsigned i=0;i<4;++i){const auto raw=f.capture(i);const auto o=p.observe(input(raw,i,.13));
        check(o.stored&&o.dwell.qualified,"strong marker not qualified");
        const auto truth=(i*.4+.16+arrival)/a*fs;
        check(std::abs(o.dwell.marker_input_sample-truth)/fs<=o.dwell.conditional_half_width_seconds,"strong marker outside conditional support");
        check(std::abs(o.dwell.marker_input_sample-truth)/fs<.0002,"wrong production inversion shape/date");
        check(o.dwell.end_sample<=o.dwell.latest_observation_end_sample&&o.dwell.end_sample<=(i+1)*dwell_samples,"future marker support");
    }
    auto q=p.estimate(clock(4),1.36,4*dwell_samples);
    check(q.qualified&&std::abs(q.offset_seconds-arrival)<.0002,"pooled strong timing unavailable");
    check(std::abs(q.dwell_start_input_sample-(1.2+arrival)/a*fs)/fs<.0002,"dwell boundary audio sign wrong");
    auto changed=clock(4);changed.reference_input_sample=1;
    check(p.estimate(changed,1.36,4*dwell_samples).reason==P::Reason::reference_mismatch,"reference silently changed");
    check(p.estimate(clock(4),1.36,4*dwell_samples).reason==P::Reason::reference_mismatch,"reference mismatch silently recovered");
    std::cout<<"strong offset_ms="<<q.offset_seconds*1e3<<" radius_ms="<<q.conditional_half_width_seconds*1e3<<" score="<<q.likelihood_score<<" threshold="<<q.detection_threshold<<'\n';
}
void controls(){
    for(unsigned kind=0;kind<3;++kind){P p({});Fixture f(100+kind,kind!=0,kind!=1,kind==2);
        for(unsigned i=0;i<8;++i){auto raw=f.capture(i,kind==2?20:60);const auto o=p.observe(input(raw,i));check(o.stored||o.reason==P::Reason::frequency_mismatch,"unexpected control rejection");check(!o.dwell.qualified,"control falsely qualified per dwell");}
        const auto q=p.estimate(clock(8),2.96,8*dwell_samples);check(!q.qualified,"straight/absent/noise falsely qualified pooled marker");}
    P p({});Fixture f(90);auto raw=f.capture(0);auto in=input(raw,0);
    check(p.observe(in).stored,"first observation missing");check(p.observe(in).reason==P::Reason::overlapping_observation,"same data counted twice");
    auto bad=in;bad.first_sample=100;bad.capture_frontier_end_sample=100;check(!p.observe(bad).stored,"future data accepted");
    P mismatch({});const auto rejected=mismatch.observe(input(raw,0,3));
    check(rejected.reason==P::Reason::frequency_mismatch,"inconsistent frequency not rejected");
    check(rejected.raw_samples_processed>0,"rejected frequency witness omitted processed aperture work");
    P short_input({});in.samples=in.samples.first(200);check(short_input.observe(in).reason==P::Reason::insufficient_samples,"short aperture accepted");
}

void clock_contradiction(){
    P p({});Fixture f(77);
    for(unsigned i=0;i<32;++i){auto raw=f.capture(i,60);check(p.observe(input(raw,i)).stored,"clock control profile missing");}
    auto wrong=clock(32);wrong.audio_scale=a+.00015;
    const auto rejected=p.estimate(wrong,12.56,32*dwell_samples);
    check(rejected.available&&!rejected.qualified&&!rejected.clock_model_consistent&&rejected.reason==P::Reason::clock_map_mismatch,
        "inconsistent clock evolution admitted a narrow pooled timing");
    const auto recovered=p.estimate(clock(32),12.56,32*dwell_samples);
    check(recovered.qualified&&recovered.clock_model_consistent&&std::abs(recovered.offset_seconds-arrival)<.0002,
        "corrected clock model did not recover original reference/data");
    std::cout<<"clock_contradiction first_ms="<<rejected.first_half_offset_seconds*1e3<<" second_ms="<<rejected.second_half_offset_seconds*1e3<<'\n';
}

void weak_and_gaps(){
    P p({});Fixture f(300);unsigned individual=0;
    for(unsigned i=0;i<90;++i){if(i==20||i==41)continue;auto raw=f.capture(i,18.88);auto o=p.observe(input(raw,i));check(o.stored,"weak segment required independent detection");individual+=o.dwell.qualified;}
    const auto c=clock(90);auto q=p.estimate(c,35.76,90*dwell_samples);
    std::cout<<"weak independent_dwell_qualified="<<individual<<" profiles="<<q.observations<<" available="<<q.available<<" qualified="<<q.qualified
        <<" offset_ms="<<q.offset_seconds*1e3<<" radius_ms="<<q.conditional_half_width_seconds*1e3<<" score="<<q.likelihood_score<<" threshold="<<q.detection_threshold<<'\n';
    check(q.observations==88,"gaps manufactured profiles");
    check(q.qualified,"pooled weak evidence unavailable in fixed feasibility case");
    check(std::abs(q.offset_seconds-arrival)<=q.conditional_half_width_seconds,"weak timing outside conditional support");
    check(q.conditional_half_width_seconds<.008,"prior mistaken for new timing information");
    auto unavailable=c;unavailable.reference_preserved=false;
    check(p.estimate(unavailable,35.76,90*dwell_samples).reason==P::Reason::unavailable_clock,"gap/reference ambiguity ignored");
    auto future=c;future.latest_observation_end_sample=91*dwell_samples;
    check(p.estimate(future,35.76,90*dwell_samples).reason==P::Reason::invalid_input,"future clock prediction used");
    auto broad=c;broad.source_half_width_seconds=.010;
    check(!p.estimate(broad,35.76,90*dwell_samples).qualified,"10ms shared uncertainty became sub8ms authority");
}
}
int main(){try{strong();controls();clock_contradiction();weak_and_gaps();std::cout<<"Helper inversion profile checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
