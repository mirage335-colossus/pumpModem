#include "datapump/helper_relative_clock.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace {
namespace h=datapump::helper;
constexpr double tau=2*std::numbers::pi,separation=1850;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
double wrap(double p) {return std::remainder(p,tau);}
h::HelperRelativeClock::Input observation(double t,double ppm,double drift,double common,double sigma=0,
                                         double differential=0,double noise=0) {
    const double source=t*(1+ppm*1e-6)+.5e-6*drift*t*t-.003197;
    return {true,t,wrap(tau*2400*source+common+differential+noise/2),
        wrap(tau*550*source+common-noise/2),sigma,0};
}
void clean_and_noise() {
    for(const double ppm:{50.,-50.})for(const double drift:{0.,.04}) {
        h::HelperRelativeClock clock({});std::mt19937_64 rng(731);
        std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
        std::normal_distribution<double> normal;
        h::HelperRelativeClock::Estimate e;double max_error=0;
        constexpr double reference=.2;
        for(unsigned i=0;i<100;++i) {
            const double t=reference+.4*i;
            e=clock.observe(observation(t,ppm,drift,phase(rng),.01,0,.01*normal(rng)));
            if(!e.valid)continue;
            const double truth=t+ppm*1e-6*(t-reference)+.5e-6*drift*(t*t-reference*reference);
            const double error=std::abs(e.source_seconds-truth);
            max_error=std::max(max_error,error);
            require(error<=e.source_half_width_seconds,"conservative fixed-reference interval missed noisy clock");
            const auto origin=clock.predict(reference);
            require(origin.valid&&std::abs(origin.source_seconds-reference)<1e-14&&origin.source_half_width_seconds<1e-14,
                    "relative reference changed with later pilot observations");
        }
        require(e.valid&&e.accepted_observations==100&&e.alternatives==1,"random common hop phases broke clock tracking");
        require(std::abs(e.audio_scale-(1+(ppm+drift*e.input_seconds)*1e-6))<2e-6,"relative audio rate inaccurate");
        std::cout<<"relative_clock ppm="<<ppm<<" drift="<<drift<<" max_error_us="<<max_error*1e6
                 <<" final_support_us="<<e.source_half_width_seconds*1e6<<'\n';
    }
}
void missing_and_ambiguity() {
    h::HelperRelativeClock clock({});h::HelperRelativeClock::Estimate e;
    for(unsigned i=0;i<20;++i)e=clock.observe(observation(.2+.4*i,50,0,.37));
    require(e.valid,"gap control failed initial fit");
    auto absent=observation(8.2,50,0,1.7);absent.valid=false;
    require(!clock.observe(absent).valid,"missing pilot manufactured fresh clock authority");
    e=clock.observe(observation(8.6,50,0,-2.8));
    require(e.valid&&e.reference_input_seconds==.2,"short gap did not preserve old reference");

    h::HelperRelativeClock ambiguous({});
    (void)ambiguous.observe(observation(.2,280,0,.2));
    (void)ambiguous.observe(observation(.6,280,0,-2));
    e=ambiguous.observe(observation(1.6,280,0,1));
    require(e.reference_preserved&&!e.valid&&e.alternatives>=2,"timing alternatives silently collapsed after early gap");
    for(unsigned i=0;i<8;++i)e=ambiguous.observe(observation(2+.4*i,280,0,-1.2));
    require(e.valid&&e.alternatives==1&&e.reference_input_seconds==.2,"retained phase alternatives did not recover original reference");

    h::HelperRelativeClock lost({});
    (void)lost.observe(observation(.2,50,0,.2));
    e=lost.observe(observation(20,50,0,.7));
    require(!e.valid&&e.ambiguity_limited&&!e.reference_preserved,"excess ambiguity did not report lost bounded coverage");
    e=lost.observe(observation(20.4,50,0,-2));
    require(!e.valid&&e.reference_input_seconds==.2,"ambiguous gap silently selected a fresh reference");
}
void differential_channel() {
    h::HelperRelativeClock::Config config;config.differential_phase_half_width_radians=.1;
    h::HelperRelativeClock bounded(config);h::HelperRelativeClock::Estimate e;
    for(unsigned i=0;i<90;++i) {
        const double t=.2+.4*i,change=.08*std::sin(.37*i);
        e=bounded.observe(observation(t,50,0,1.4,.001,change));
        if(e.valid) {
            const double truth=t+50e-6*(t-.2);
            require(std::abs(e.source_seconds-truth)<=e.source_half_width_seconds,
                    "differential-channel allowance missing from relative clock support");
        }
    }
    require(e.valid,"bounded differential channel was unnecessarily lost");
    e=bounded.observe(observation(36.2,50,0,-1.4,.001,1.2));
    require(!e.valid&&!e.model_consistent,"large differential phase step falsely qualified clock");
    require(!bounded.predict(36.3).valid,"explicit prediction restored contradicted clock authority");
    e=bounded.observe(observation(36.6,50,0,.7,.001,0));
    require(e.valid&&e.reference_input_seconds==.2,"rejected differential outlier prevented fixed-reference recovery");
    // A slow differential phase change that stays within the declared bound
    // cannot be distinguished from clock drift. It must remain in support.
    h::HelperRelativeClock smooth(config);
    for(unsigned i=0;i<90;++i) {
        const double t=.2+.4*i,change=.09*(2*static_cast<double>(i)/89-1);
        e=smooth.observe(observation(t,50,0,2.4,.001,change));
    }
    const double truth=e.input_seconds+50e-6*(e.input_seconds-.2);
    require(e.valid&&std::abs(e.source_seconds-truth)<=e.source_half_width_seconds,
            "smooth channel/clock degeneracy falsely claimed precision");
}
void bad_input_and_bounds() {
    h::HelperRelativeClock clock({});auto weak=observation(.2,0,0,0,4);
    require(!clock.observe(weak).reference_initialized,"weak initial phase chose a reference");
    auto invalid=observation(.6,0,0,0);invalid.phase_difference_sigma_radians=-1;
    require(!clock.observe(invalid).reference_initialized,"negative phase sigma accepted");
    for(unsigned i=0;i<128;++i)(void)clock.observe(observation(1+.4*i,0,0,.7));
    require(!clock.observe(observation(1,0,0,.7)).valid,"backward phase date manufactured fresh authority");
    require(!clock.observe(observation(51.8,0,0,.7)).valid,"duplicate phase date manufactured fresh authority");
    require(!clock.predict(100).valid,"unbounded future clock prediction qualified");
    const auto full=clock.observe(observation(52.2,0,0,.2));
    require(!full.valid&&full.work_limited,"observation work bound not enforced");
    clock.reset();require(!clock.predict(1).reference_initialized,"explicit reset retained history");
}
}
int main() {
    try {clean_and_noise();missing_and_ambiguity();differential_channel();bad_input_and_bounds();
        std::cout<<"Helper relative clock tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Helper relative clock failure: "<<e.what()<<'\n';return 1;}
}
