#include "../src/live_helper_paired_dwell.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>

using namespace datapump;
namespace {
void check(bool ok,const char* message) {if(!ok)throw Error(message);}
void trial(double cn0,unsigned seed) {
    constexpr std::uint32_t rate=32000,dwell=rate*2/5;
    constexpr double pi=std::numbers::pi,high=2400,low=550;
    std::mt19937 random(seed);
    std::normal_distribution<double> noise(0,std::sqrt(rate/(4*std::pow(10.,cn0/10))));
    std::uniform_real_distribution<double> phase(0,2*pi);
    helper::ToneTrackerConfig config{rate,high,10000,0,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    live::detail::HelperPairedDwell pair(rate,0,high,.0003,0);
    std::vector<float> input(dwell);unsigned high_fits=0,low_fits=0,escapes=0,wrong=0;
    double max_error=0,max_u=0;
    for(unsigned hop=0;hop<90;++hop) {
        const auto first=static_cast<std::uint64_t>(hop)*dwell;
        const auto high_phase=phase(random),low_phase=phase(random),aux_phase=phase(random),data_phase=phase(random);
        const auto common=3000+.001*hop;
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
        auto l=pair.measure(h);if(!l)continue;
        ++low_fits;
        check(l->first_sample==h.first_sample && l->end_sample==h.end_sample &&
              l->mean_sample==h.mean_sample && l->mean_sample_fraction==h.mean_sample_fraction &&
              l->mu_seconds==h.mu_seconds && l->integral_k_squared_seconds3==h.integral_k_squared_seconds3,
              "paired frequency support or process kernel changed");
        const auto error=std::abs(l->frequency_hz-(low*1.0001+common));
        const auto u=3*std::sqrt(l->independent_variance_hz2+l->residual_variance_hz2);
        max_error=std::max(max_error,error);max_u=std::max(max_u,u);
        escapes+=error>u;wrong+=error>50;
        check(!pair.measure(h),"reused one aperture as a new measurement");
    }
    check(high_fits>=80,"wide-offset high discovery did not find the pilot");
    check(low_fits>=(cn0>=40?75u:20u),"too few bounded low-pilot fits");
    check(!wrong && escapes<=6,"low-pilot fit chose a wrong tone or had excessive uncertainty escapes");
    std::cout<<"paired dwell cn0="<<cn0<<" seed="<<seed<<" high="<<high_fits<<" low="<<low_fits
             <<" escapes="<<escapes<<" wrong="<<wrong<<" max_error_hz="<<max_error<<" max_U_hz="<<max_u<<'\n';
}
}
int main()try {for(double cn0:{30.,40.})for(unsigned seed=1;seed<=3;++seed)trial(cn0,seed);}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
