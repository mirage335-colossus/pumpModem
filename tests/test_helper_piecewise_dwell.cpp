#include "datapump/helper_piecewise_dwell.hpp"

#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace h=datapump::helper;
namespace {
constexpr double fs=24000,tau=2*std::numbers::pi;
constexpr std::size_t count=8640;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
std::vector<std::complex<float>> data(unsigned seed,double snr,double onset,double duration,double pulse,int scenario=0){
    std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
    std::vector<std::complex<float>> result(count);
    const double sigma=std::sqrt(fs/(2*std::pow(10.,snr/10)));
    for(std::size_t i=0;i<count;++i){const double t=static_cast<double>(i)/fs;
        const bool active=t>=onset&&t<onset+duration;
        const double p=active&&scenario==0?pulse:0;
        auto signal=std::polar(1.,tau*2424.12*t+.37+p)+std::polar(.7,tau*574.0275*t-.81+p);
        if(active&&scenario==1)signal*=.35;
        if(active&&scenario==2)signal+=std::polar(1.,tau*2424.12*t+.37+std::numbers::pi/2);
        result[i]={static_cast<float>(signal.real()+sigma*normal(rng)),static_cast<float>(signal.imag()+sigma*normal(rng))};}
    return result;
}
}
int main(){try{
    h::HelperAidedPhaseConfig c;c.sample_rate=fs;c.compute_slope_covariance=true;
    const h::HelperAidedPhasePrediction p{true,2424.12,574.0275,.10,.10};
    const h::HelperPiecewiseDwellConfig pc;
    // The frequencies here are fixture constants, not channel truth supplied
    // to a receiver. Preserve exactly the same noise for every paired case.
    unsigned clean=0,flags=0,pulse=0,pulse_flags=0,long_flags=0,long_count=0,selected=0;
    unsigned old_clean=0,old_pulse=0,old_long=0;const h::HelperTransientPhaseConfig old_config;
    unsigned gain_count=0,gain_detected=0,gain_v5=0,add_count=0,add_detected=0,add_v5=0;
    for(unsigned seed=1;seed<=32;++seed){
        for(int mode=0;mode<5;++mode){
            const int kind=mode==3?1:mode==4?2:0;
            auto x=data(1700000+seed,28.07,.085,mode==1?.015:mode==3?.080:.048,mode==0?0:(mode==1?-.85:1.57),kind);
            const auto base=h::estimate_helper_aided_phase(x,c,p,30e6,.02);
            if(!base.valid)continue;
            const auto candidate=h::estimate_helper_piecewise_dwell(x,c,pc,p,base,.02);
            require(candidate.evaluated,"piecewise witness did not evaluate a valid baseline");
            const auto old=h::estimate_helper_transient_phase(x,c,old_config,p,base,.02);
            require(old.evaluated,"v5 witness did not evaluate same baseline");
            if(mode==0){++clean;flags+=candidate.disturbance_detected;old_clean+=old.disturbance_detected;}
            else if(mode==1){++pulse;pulse_flags+=candidate.disturbance_detected;old_pulse+=old.disturbance_detected;}
            else if(mode==2){++long_count;long_flags+=candidate.disturbance_detected;old_long+=old.disturbance_detected;}
            else if(mode==3){++gain_count;gain_detected+=candidate.disturbance_detected;gain_v5+=old.disturbance_detected;}
            else {++add_count;add_detected+=candidate.disturbance_detected;add_v5+=old.disturbance_detected;}
            if(candidate.valid){++selected;require(candidate.disturbance_detected&&candidate.support_first_sample<candidate.support_end_sample&&
                candidate.support_end_sample<=count&&candidate.anchor_time_seconds>=.02+static_cast<double>(candidate.support_first_sample)/fs&&
                candidate.anchor_time_seconds<.02+static_cast<double>(candidate.support_end_sample)/fs&&candidate.high_sigma_radians>0&&candidate.low_sigma_radians>0,
                "selected phase support and uncertainty invalid");}
        }
    }
    require(clean>=25&&pulse>=20&&long_count>=20,"too few valid short synthetic checks");
    auto x=data(1901,100,.09,.048,1.57);
    h::HelperAidedPhaseResult b;b.valid=true;b.measurement.valid=true;
    b.center_time_seconds=.02+(static_cast<double>(count)-1)/(2*fs);
    b.measurement.first_sample_time_seconds=.02;
    b.measurement.end_sample_time_seconds=.02+static_cast<double>(count)/fs;
    b.high_raw_noise_density=b.low_raw_noise_density=1e-5;
    const h::HelperAidedPhasePrediction bright_prior{true,2424.12,574.0275,.001,.001};
    auto r=h::estimate_helper_piecewise_dwell(x,c,pc,bright_prior,b,.02);
    std::cout<<"piecewise_bright baseline="<<b.valid<<" evaluated="<<r.evaluated<<" detected="<<r.disturbance_detected<<" score="<<r.maximum_score<<"\n";
    require(b.valid&&r.evaluated&&r.disturbance_detected&&r.valid,"bright phase pulse was not detected with valid support");
    const double t=r.anchor_time_seconds-.02;
    require(std::abs(std::remainder(r.high_phase_radians-(tau*2424.12*t+.37),tau))<.02&&
            std::abs(std::remainder(r.low_phase_radians-(tau*574.0275*t-.81),tau))<.02,
            "piecewise phase sign or anchor date disagrees with known tones");
    auto unresolved=pc;unresolved.minimum_support_seconds=.30;
    const auto no_support=h::estimate_helper_piecewise_dwell(x,c,unresolved,bright_prior,b,.02);
    require(no_support.evaluated&&no_support.disturbance_detected&&!no_support.valid,
            "detected but unresolved disturbance exposed phase authority");
    auto stale=b;stale.measurement.first_sample_time_seconds+=.01;
    require(!h::estimate_helper_piecewise_dwell(x,c,pc,bright_prior,stale,.02).evaluated,
            "mismatched baseline supplied candidate authority");
    stale=b;stale.center_time_seconds+=.01;
    require(!h::estimate_helper_piecewise_dwell(x,c,pc,bright_prior,stale,.02).evaluated,
            "mismatched baseline anchor supplied candidate authority");
    const auto uneven=std::span<const std::complex<float>>(x.data(),x.size()-1);
    require(!h::estimate_helper_piecewise_dwell(uneven,c,pc,bright_prior,b,.02).evaluated,
        "unscanned trailing samples supplied candidate support");
    auto unsafe=b;unsafe.noise_psd_safety_factor=.5;
    require(!h::estimate_helper_piecewise_dwell(x,c,pc,bright_prior,unsafe,.02).evaluated,
            "subunit PSD safety factor supplied candidate authority");
    auto huge_c=c;huge_c.sample_rate=1e100;
    auto huge_b=b;huge_b.center_time_seconds=.02;
    huge_b.measurement.first_sample_time_seconds=.02;huge_b.measurement.end_sample_time_seconds=.02;
    huge_b.high_raw_noise_density=huge_b.low_raw_noise_density=1e-200;
    std::vector<std::complex<float>> huge(count,{1,0});
    for(std::size_t i=count/4;i<count/4+count/12;++i)huge[i]={0,1};
    const auto huge_result=h::estimate_helper_piecewise_dwell(huge,huge_c,pc,bright_prior,huge_b,.02);
    require(huge_result.evaluated&&huge_result.disturbance_detected&&!huge_result.valid,
            "oversized retune guard exposed support or failed the overflow control");
    auto bad=pc;bad.boundary_guard_segments=0;
    require(!h::estimate_helper_piecewise_dwell(x,c,bad,p,b,.02).evaluated,"invalid boundary guard evaluated");
    std::cout<<"piecewise_short synthetic_cn0_db_hz=28.07 clean="<<clean<<" clean_flags="<<flags
        <<" phase15="<<pulse<<" phase15_flags="<<pulse_flags<<" phase48="<<long_count
        <<" phase48_flags="<<long_flags<<" selected="<<selected
        <<" v5_clean_flags="<<old_clean<<" v5_phase15_flags="<<old_pulse<<" v5_phase48_flags="<<old_long
        <<" gain_count="<<gain_count<<" gain_v6="<<gain_detected<<" gain_v5="<<gain_v5
        <<" add_count="<<add_count<<" add_v6="<<add_detected<<" add_v5="<<add_v5<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
