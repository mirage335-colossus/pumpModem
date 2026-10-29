#include "../src/live_helper_paired_dwell.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>

using namespace datapump;
namespace {
void check(bool ok,const char* message) {if(!ok)throw Error(message);}
void trial(double cn0,unsigned seed,int side,bool wide=false) {
    constexpr std::uint32_t rate=32000,dwell=rate*2/5;
    constexpr double pi=std::numbers::pi,high=2400,low=550;
    std::mt19937 random(seed);
    std::normal_distribution<double> noise(0,std::sqrt(rate/(4*std::pow(10.,cn0/10))));
    std::uniform_real_distribution<double> phase(0,2*pi);
    helper::ToneTrackerConfig config{rate,high,10000,0,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    live::detail::HelperPairedDwell pair(rate,0,high,.0003,0);
    check(pair.workspace_bytes()<=live::detail::HelperPairedDwell::reservation_bytes(rate,0),
          "paired dwell exceeded its prepaid workspace");
    std::vector<float> input(dwell);unsigned high_fits=0,low_fits=0,escapes=0,wrong=0,clock_escapes=0;
    double max_error=0,max_u=0,max_clock_error=0;
    for(unsigned hop=0;hop<90;++hop) {
        const auto first=static_cast<std::uint64_t>(hop)*dwell;
        const auto high_phase=phase(random),low_phase=phase(random),aux_phase=phase(random),data_phase=phase(random);
        const auto rf=std::array{7e6,21e6,35e6,49e6}[hop%4];
        const auto common=wide?3000+.001*hop:side*7.3*rf/7e6;
        for(unsigned i=0;i<dwell;++i) {
            const auto t=double(i)/rate;
            input[i]=std::cos(2*pi*(high*1.0001+common)*t+high_phase)+
                std::sqrt(.5)*std::cos(2*pi*(low*1.0001+common)*t+low_phase)+
                std::sqrt(.125892541)*std::cos(2*pi*(950*1.0001+common)*t+aux_phase)+
                .25*std::cos(2*pi*(1500*1.0001+common)*t+data_phase)+noise(random);
        }
        tracker.begin_pilot_segment(first,high,10000,false,1,first,true,first+dwell);
        tracker.push(input,first);pair.push(input,first);
        const auto h=tracker.snapshot().frequency_observation;
        if(!h.valid)continue;
        ++high_fits;
        if(!pair.submit(h,hop+1,rf))continue;
        while(!pair.complete())check(pair.advance(4096),"paired worker stalled");
        const auto measured=pair.take();if(!measured.usable)continue;
        const auto* l=&measured.low;
        ++low_fits;
        check(l->first_sample==h.first_sample && l->end_sample==h.end_sample &&
              l->mean_sample==h.mean_sample && l->mean_sample_fraction==h.mean_sample_fraction &&
              l->mu_seconds==h.mu_seconds && l->integral_k_squared_seconds3==h.integral_k_squared_seconds3,
              "paired frequency support or process kernel changed");
        const auto error=std::abs(l->frequency_hz-(low*1.0001+common));
        const auto u=3*std::sqrt(l->independent_variance_hz2+l->residual_variance_hz2);
        const auto scale=((h.frequency_hz-l->frequency_hz)/(high-low)-1)*1e6;
        const auto scale_u=3e6*std::sqrt(2*(h.independent_variance_hz2+h.residual_variance_hz2+
            l->independent_variance_hz2+l->residual_variance_hz2))/(high-low);
        const auto scale_error=std::abs(scale-100);
        max_clock_error=std::max(max_clock_error,scale_error);clock_escapes+=scale_error>scale_u;
        max_error=std::max(max_error,error);max_u=std::max(max_u,u);
        escapes+=error>u;wrong+=error>50;
    }
    check(high_fits>=80,"wide-offset high discovery did not find the pilot");
    check(low_fits>=(cn0>=40?75u:20u),"too few bounded low-pilot fits");
    check(!wrong && escapes<=6 && clock_escapes<=6,"paired fit chose a wrong tone or had excessive conditional uncertainty escapes");
    std::cout<<"paired dwell cn0="<<cn0<<" seed="<<seed<<" side="<<side<<" wide="<<wide<<" high="<<high_fits<<" low="<<low_fits
             <<" escapes="<<escapes<<" clock_escapes="<<clock_escapes<<" wrong="<<wrong<<" max_error_hz="<<max_error
             <<" max_U_hz="<<max_u<<" max_clock_error_ppm="<<max_clock_error<<'\n';
}
void worker_handoff() {
    constexpr std::uint32_t rate=32000,dwell=rate*2/5;
    helper::ToneTrackerConfig config{rate,2400,60,0,.5,true};
    helper::ToneTracker high(config,helper::ToneTracker::workspace_required(config));
    live::detail::HelperPairedDwell pair(rate,0,2400,.0003,0);
    std::vector<float> input(dwell);
    for(std::size_t i=0;i<input.size();++i) {
        const auto t=static_cast<double>(i)/rate;
        input[i]=static_cast<float>(std::cos(2*std::numbers::pi*2401*t+.7)+
            .7*std::cos(2*std::numbers::pi*551*t+.2));
    }
    high.begin_pilot_segment(0,2400,60,false,1,0,true,dwell);
    high.push(input,0);pair.push(input,0);
    auto observation=high.snapshot().frequency_observation;
    check(observation.valid,"handoff high fit missing");
    auto edge=observation;edge.frequency_hz=2400-350;
    check(!pair.submit(edge,1,7e6),"paired worker accepted noise band across DC");
    edge.frequency_hz=2400+15300;
    check(!pair.submit(edge,1,7e6),"paired worker accepted noise band across Nyquist");
    auto altered=observation;altered.integral_k_seconds2+=.001;
    check(pair.submit(altered,1,7e6),"handoff rejected valid captured aperture");
    check(!pair.submit(observation,1,7e6),"handoff overwrote an active worker snapshot");
    while(!pair.complete())check(pair.advance(4096),"handoff worker stalled");
    const auto mismatch=pair.take();
    check(mismatch.low.valid && !mismatch.kernel_match && !mismatch.usable,
          "different high/low kernels supplied a paired observation");
    check(pair.submit(observation,1,7e6),"handoff did not accept the next snapshot");
    pair.reset(dwell+17); // capture reset while the immutable worker owns PCM
    while(!pair.complete())check(pair.advance(4096),"stale handoff worker stalled");
    const auto stale=pair.take();
    check(stale.stale && !stale.usable,"old capture generation committed a paired observation");
    constexpr std::uint64_t next=dwell+17;
    high.reset(next);high.begin_pilot_segment(next,2400,60,false,1,next,true,next+dwell);
    high.push(input,next);pair.push(input,next);
    const auto recovered=high.snapshot().frequency_observation;
    check(recovered.valid && pair.submit(recovered,2,7e6),"fresh aperture was not accepted after stale work");
    while(!pair.complete())check(pair.advance(4096),"fresh worker stalled after stale work");
    check(pair.take().usable,"fresh paired measurement did not recover after invalidation");
}
}
int main()try {worker_handoff();for(double cn0:{30.,40.})for(unsigned seed=1;seed<=3;++seed)
    for(int side:{-1,1})trial(cn0,seed,side);trial(40,1,1,true);}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
