#include "datapump/helper_inversion_timing.hpp"
#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
namespace h=datapump::helper;
namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
constexpr unsigned rate=24000;
constexpr double tau=2*std::numbers::pi;
void waveform(){
    h::Options o;o.power_w=40;o.timecode=false;
    for(const double hz:{550.,2400.}){
        h::Generator g(o,{hz,.4,0,40,0,0,false},1800000000,true);
        for(const long double t:{.001L,.396L,.398L,.4L,.402L,.404L,.799L,.8L,.801L}){
            const auto expected=.4*h::pilot_amplitude_fraction*std::polar(1.,tau*static_cast<double>(std::remainder(hz*t,1.L)));
            check(std::abs(g.mix_analytic({},t,rate)-expected)<1e-11,"non-FHSS straight pilot ramped or reset");
        }
    }
    for(const bool hopping:{false,true}){
        h::Generator full(o,{2400,.4,0,40,950,550,hopping},1800000000,true);
        h::Generator pilots(o,{2400,.4,0,40,0,550,hopping},1800000000,false);
        for(const auto [t,sign]:std::array<std::pair<long double,double>,3>{{{.140L,1},{.160L,0},{.180L,-1}}}){
            const auto aux=(full.mix_analytic({},t,rate)-pilots.mix_analytic({},t,rate))*std::polar(1.,-tau*static_cast<double>(std::remainder(950*t,1.L)));
            check(std::abs(aux-.4*h::auxiliary_amplitude_fraction*sign)<1e-11,"auxiliary inversion absent or wrong sign");
        }
    }
}
void observe(bool hopping,bool punctuate){
    h::Options o;o.power_w=40;o.timecode=false;
    h::Generator generator(o,{2400,.4,0,40,950,550,hopping},1800000000,punctuate);
    h::HelperInversionTiming observer({rate,950,2400,550});
    constexpr long double scale=1.0001L,offset=.00321L;
    std::array<std::complex<float>,113> input;
    std::uint64_t count=0,previous=0;double square=0,worst=0;
    for(std::uint64_t first=0;first<6ULL*rate;){
        const auto n=std::min<std::uint64_t>(input.size(),6ULL*rate-first);
        for(std::size_t j=0;j<n;++j){
            const auto t=(first+j)*scale/rate-offset;
            const auto slot=static_cast<long long>(std::floor(t/.4L));
            // Independent known unit-test hop phases; the receiver does not
            // receive these values. Full simulation uses keyed RF schedules.
            const auto phase=hopping?.37+std::remainder(slot*1.731,6.28):.37;
            input[j]=static_cast<std::complex<float>>(generator.mix_analytic({},t,rate,{},0.L)*std::polar(1.,phase));
        }
        observer.observe(std::span(input).first(n),first);first+=n;
        const auto p=observer.take_update();if(!p.known)continue;
        check(p.last_sample<first&&p.first_sample<=p.sample&&p.sample<=p.last_sample,"inversion support/dating invalid");
        check(!count||p.sample>previous,"inversion observation duplicated/backdated");
        const double error=static_cast<double>(std::remainder((p.sample+p.fraction)*scale/rate-offset-.160L,.4L)/scale);
        check(std::abs(error)*rate<=p.uncertainty_samples,"noiseless marker outside reported radius");
        square+=error*error;worst=std::max(worst,std::abs(error));previous=p.sample;++count;
    }
    std::cout<<"inversion hopping="<<hopping<<" enabled="<<punctuate<<" markers="<<count<<" rms_us="<<(count?std::sqrt(square/count)*1e6:0)
        <<" worst_us="<<worst*1e6<<" workspace_bytes="<<observer.workspace_bytes()<<'\n';
    if(punctuate){check(count>=6,"strong composite inversion timing unavailable");check(worst<.0001,"noiseless composite inversion error exceeds 100us");}
    else check(count==0,"straight auxiliary falsely identified as inversion");
    check(!observer.take_update().known,"carried marker manufactured new evidence");
    observer.missing(8ULL*rate);check(!observer.take_update().known,"gap retained stale pending marker");
}
}
int main(){try{waveform();observe(false,true);observe(true,true);observe(false,false);return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
