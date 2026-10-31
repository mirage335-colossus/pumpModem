#include "datapump/helper_aided_phase.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace h=datapump::helper;
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr double fs=24000;
constexpr std::size_t count=8640;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double wrap(double x){return std::remainder(x,tau);}

template<std::size_t N>
bool finite_covariance(const std::array<std::array<double,N>,N>& covariance,bool positive) {
    std::array<std::array<double,N>,N> lower{};
    for(std::size_t i=0;i<N;++i)for(std::size_t j=0;j<=i;++j) {
        if(!std::isfinite(covariance[i][j])||
           std::abs(covariance[i][j]-covariance[j][i])>1e-12)return false;
        double value=covariance[i][j];
        for(std::size_t k=0;k<j;++k)value-=lower[i][k]*lower[j][k];
        if(i==j) {
            if(positive&&!(value>0))return false;
            if(value<-1e-12)return false;
            lower[i][j]=std::sqrt(std::max(value,1e-30));
        } else lower[i][j]=value/lower[j][j];
    }
    return true;
}
std::vector<std::complex<float>> signal(unsigned seed,double cn0,double offset=0,
    bool noise_only=false,double bright_power=0,double curvature=0) {
    std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
    const double common=std::uniform_real_distribution<double>(-std::numbers::pi,std::numbers::pi)(rng);
    const double sigma=std::sqrt(fs/(2*std::pow(10.,cn0/10)));
    std::vector<std::complex<float>> out(count);
    for(std::size_t i=0;i<count;++i) {
        const double t=static_cast<double>(i)/fs;
        const double q=curvature*std::pow(t-.18,3);
        const auto x=noise_only?std::complex<double>{}:
            std::polar(1.,tau*(2424.12+offset)*t+.37+common+q)+
            std::polar(std::sqrt(.5),tau*(574.0275+offset)*t-.81+common+q)+
            std::polar(std::sqrt(bright_power),tau*1624.08*t+1.2);
        const auto z=x+std::complex<double>(sigma*normal(rng),sigma*normal(rng));
        out[i]={static_cast<float>(z.real()),static_cast<float>(z.imag())};
    }
    return out;
}

// A numerical regression fixture, independent of implementation-defined
// random-library sequences. The sensitivity studies below use thermal noise;
// this explicitly indexed uniform perturbation only exercises the Gram algebra.
std::vector<std::complex<float>> covariance_reference_signal() {
    const auto perturbation=[](std::uint32_t index) {
        index^=index>>16;index*=0x7feb352dU;
        index^=index>>15;index*=0x846ca68bU;index^=index>>16;
        return (static_cast<double>(index)+.5)/2147483648.-1;
    };
    const double sigma=std::sqrt(3*fs/(2*std::pow(10.,2.8)));
    std::vector<std::complex<float>> out(count);
    for(std::size_t i=0;i<count;++i) {
        const double t=static_cast<double>(i)/fs;
        const auto index=static_cast<std::uint32_t>(2*i);
        const auto x=std::polar(1.,tau*2423.94*t+.77)+
            std::polar(std::sqrt(.5),tau*573.8475*t-.41)+
            sigma*std::complex<double>(perturbation(index),perturbation(index+1));
        out[i]=std::complex<float>(x);
    }
    return out;
}

void slope_covariance_study() {
    constexpr unsigned seeds=128;
    const h::HelperAidedPhasePrediction prediction{true,2424.12,574.0275,.1,.1};
    h::HelperAidedPhaseConfig plain;plain.sample_rate=fs;
    auto configured=plain;configured.compute_slope_covariance=true;
    // Lower triangles captured from the original per-sample scaled Gram
    // implementation. Keep the tiny differential-process entries separately:
    // reconstructing them by subtraction loses useful precision.
    auto reference_config=configured;reference_config.phase_noise_radians_per_sqrt_second=.03;
    const auto reference=h::estimate_helper_aided_phase(covariance_reference_signal(),reference_config,prediction,30e6,.02);
    constexpr std::array<double,10> expected_thermal{
        .016652002503463249,6.5293738804654318e-13,.029479313790492248,
        4.7107279308835725e-5,3.7124626300069401e-11,.0057784755162264443,
        6.1751557609075429e-12,-.00019077046934372845,-6.2444461620952963e-18,.015018001850811733};
    constexpr std::array<double,10> expected_process{
        9.6096045632053362e-5,7.8185390821677332e-5,6.3635561550204269e-5,
        7.88796282672776e-5,6.303168132496357e-5,.00012858305415961902,
        7.887961719503068e-5,6.3031696246441997e-5,.00012858184822361267,.00012858065644686428};
    constexpr std::array<double,6> expected_reduced_process{
        9.6096045632053362e-5,7.8185390821677332e-5,6.3635561550204269e-5,
        1.1072246685843182e-11,-1.4921478682449905e-11,1.4159257230181779e-11};
    const auto check_reference=[](const auto& matrix,const auto& expected) {
        std::size_t entry=0;
        for(std::size_t i=0;i<matrix.size();++i)for(std::size_t j=0;j<=i;++j) {
            const double scale=std::sqrt(std::abs(matrix[i][i]*matrix[j][j]));
            require(std::isfinite(matrix[i][j])&&std::abs(matrix[i][j]-expected[entry++])<=3e-11*scale,
                "slope Gram scaling changed numerical covariance");
        }
    };
    require(reference.thermal_covariance_valid&&reference.process_covariance_valid,
        "slope covariance reference unavailable");
    check_reference(reference.pilot_thermal_covariance,expected_thermal);
    check_reference(reference.pilot_process_covariance,expected_process);
    check_reference(reference.process_covariance,expected_reduced_process);
    for(const int exponent:{-100,100}) {
        auto scaled=covariance_reference_signal();
        for(auto& sample:scaled)sample*=std::ldexp(1.f,exponent);
        const auto result=h::estimate_helper_aided_phase(scaled,reference_config,prediction,30e6,.02);
        require(result.thermal_covariance_valid&&result.process_covariance_valid,
            "finite amplitude scaling lost slope covariance");
        check_reference(result.pilot_thermal_covariance,expected_thermal);
        check_reference(result.pilot_process_covariance,expected_process);
        check_reference(result.process_covariance,expected_reduced_process);
    }
    for(const double cn0:{28.,48.}) {
        unsigned accepted=0,frequency_misses=0,phase_misses=0;
        std::array<double,4> error_square{},reported_variance{};
        double largest_correlation=0,date_separation=0,cpu_seconds=0,plain_cpu=0;
        for(unsigned seed=0;seed<seeds;++seed) {
            const unsigned key=5000+seed;
            const double offset=(seed%2?1.:-1.)*.18;
            const auto input=signal(key,cn0,offset);
            auto begin=std::clock();const auto old=h::estimate_helper_aided_phase(input,plain,prediction,30e6,.02);
            plain_cpu+=static_cast<double>(std::clock()-begin)/CLOCKS_PER_SEC;
            begin=std::clock();const auto result=h::estimate_helper_aided_phase(input,configured,prediction,30e6,.02);
            cpu_seconds+=static_cast<double>(std::clock()-begin)/CLOCKS_PER_SEC;
            require(old.valid==result.valid,"covariance instrumentation changed original slope admission");
            if(!result.valid)continue;
            require(old.measurement.high_frequency_hz==result.measurement.high_frequency_hz&&
                old.measurement.low_frequency_hz==result.measurement.low_frequency_hz&&
                old.high_center_phase_radians==result.high_center_phase_radians&&
                old.low_center_phase_radians==result.low_center_phase_radians&&
                old.measurement.high_sigma_hz==result.measurement.high_sigma_hz&&
                old.measurement.low_sigma_hz==result.measurement.low_sigma_hz,
                "optional slope covariance changed points or legacy marginal errors");
            require(!old.thermal_covariance_valid&&!old.process_covariance_valid&&!old.frequency_dates_valid,
                "default original slope silently enabled covariance work");
            require(result.thermal_covariance_valid&&result.process_covariance_valid&&result.frequency_dates_valid,
                "admitted original slope lacked opt-in covariance");
            require(finite_covariance(result.pilot_thermal_covariance,true)&&
                finite_covariance(result.thermal_covariance,true)&&
                finite_covariance(result.pilot_process_covariance,false)&&
                finite_covariance(result.process_covariance,false),"slope covariance is not finite symmetric PSD");
            require(result.work_sample_products<=64*count,"slope covariance exceeded work bound");
            ++accepted;
            std::mt19937_64 phase_rng(key);
            const double common=std::uniform_real_distribution<double>(-std::numbers::pi,std::numbers::pi)(phase_rng);
            const double center=static_cast<double>(count-1)/(2*fs);
            const std::array<double,4> errors{
                result.measurement.high_frequency_hz-(2424.12+offset),
                result.measurement.low_frequency_hz-(574.0275+offset),
                wrap(result.high_center_phase_radians-tau*(2424.12+offset)*center-.37-common),
                wrap(result.low_center_phase_radians-tau*(574.0275+offset)*center+.81-common)};
            for(std::size_t i=0;i<4;++i) {
                error_square[i]+=errors[i]*errors[i];reported_variance[i]+=result.pilot_thermal_covariance[i][i];
                if(std::abs(errors[i])>3*std::sqrt(result.pilot_thermal_covariance[i][i])) {
                    if(i<2)++frequency_misses;else ++phase_misses;
                }
            }
            const double correlation=result.pilot_thermal_covariance[1][3]/
                std::sqrt(result.pilot_thermal_covariance[1][1]*result.pilot_thermal_covariance[3][3]);
            largest_correlation=std::max(largest_correlation,std::abs(correlation));
            date_separation=std::max(date_separation,std::abs(result.high_frequency_time_seconds-result.low_frequency_time_seconds));
        }
        require(accepted>=120&&frequency_misses<=8&&phase_misses<=8,"slope covariance sampled coverage failed");
        require(largest_correlation>.01&&date_separation>1e-5,"adaptive quarter asymmetry/covariance was erased");
        std::cout<<"helper_slope_covariance cn0_db_hz="<<cn0<<" accepted="<<accepted<<"/"<<seeds
            <<" high_frequency_rms_hz="<<std::sqrt(error_square[0]/accepted)
            <<" high_frequency_sigma_rms_hz="<<std::sqrt(reported_variance[0]/accepted)
            <<" low_frequency_rms_hz="<<std::sqrt(error_square[1]/accepted)
            <<" low_frequency_sigma_rms_hz="<<std::sqrt(reported_variance[1]/accepted)
            <<" high_phase_rms_deg="<<std::sqrt(error_square[2]/accepted)*180/std::numbers::pi
            <<" high_phase_sigma_rms_deg="<<std::sqrt(reported_variance[2]/accepted)*180/std::numbers::pi
            <<" frequency_three_sigma_misses="<<frequency_misses<<"/"<<(2*accepted)
            <<" phase_three_sigma_misses="<<phase_misses<<"/"<<(2*accepted)
            <<" max_abs_low_frequency_phase_correlation="<<largest_correlation
            <<" max_high_low_date_separation_s="<<date_separation
            <<" receiver_cpu_s="<<cpu_seconds<<" original_cpu_s="<<plain_cpu
            <<" sampled_seconds="<<(seeds*count/fs)<<'\n';
    }
    configured.phase_noise_radians_per_sqrt_second=.03;
    double error_square=0,reported_variance=0,max_differential_process=0;unsigned accepted=0,misses=0;
    for(unsigned seed=0;seed<128;++seed) {
        auto input=signal(6000+seed,100);
        std::mt19937_64 rng(7000+seed);std::normal_distribution<double> normal;double phase=0;
        for(auto& x:input){phase+=configured.phase_noise_radians_per_sqrt_second/std::sqrt(fs)*normal(rng);
            x*=std::complex<float>(std::polar(1.,phase));}
        const auto result=h::estimate_helper_aided_phase(input,configured,prediction,30e6,.02);
        if(!result.valid)continue;
        require(result.thermal_covariance_valid&&result.process_covariance_valid,"Brownian slope covariance unavailable");
        const double error=result.measurement.high_frequency_hz-2424.12;
        const double variance=result.thermal_covariance[0][0]+result.process_covariance[0][0];
        error_square+=error*error;reported_variance+=variance;++accepted;
        misses+=std::abs(error)>3*std::sqrt(variance);
        const double differential=std::max(0.,result.process_covariance[0][0]+result.process_covariance[1][1]-
            2*result.process_covariance[0][1]);
        max_differential_process=std::max(max_differential_process,std::sqrt(differential));
    }
    require(accepted>=120&&misses<=8&&max_differential_process>1e-7,"adaptive common-process covariance failed");
    std::cout<<"helper_slope_covariance_process q_rad_per_sqrt_s="<<configured.phase_noise_radians_per_sqrt_second
        <<" accepted="<<accepted<<"/128 high_frequency_rms_hz="<<std::sqrt(error_square/accepted)
        <<" total_sigma_rms_hz="<<std::sqrt(reported_variance/accepted)<<" three_sigma_misses="<<misses
        <<" max_high_minus_low_process_sigma_hz="<<max_differential_process<<'\n';

    configured.phase_noise_radians_per_sqrt_second=0;
    unsigned colored_accepted=0,colored_misses=0,bright_accepted=0,bright_misses=0;
    unsigned noise_accepted=0,stale_accepted=0,alias_accepted=0,retune_accepted=0;
    double colored_error2=0,colored_variance=0;
    for(unsigned seed=0;seed<64;++seed) {
        auto input=signal(8000+seed,std::numeric_limits<double>::infinity());
        std::mt19937_64 rng(9000+seed);std::normal_distribution<double> normal;
        constexpr double rho=.4;
        const double sigma=std::sqrt(fs/(2*std::pow(10.,28./10)));
        std::complex<double> state(sigma*normal(rng),sigma*normal(rng));
        for(auto& x:input) {
            state=rho*state+sigma*std::sqrt(1-rho*rho)*std::complex<double>(normal(rng),normal(rng));
            x+=std::complex<float>(state);
        }
        const auto result=h::estimate_helper_aided_phase(input,configured,prediction,30e6,.02);
        if(result.valid) {
            require(result.thermal_covariance_valid,"colored admitted slope lacked covariance");
            ++colored_accepted;
            const double error=result.measurement.high_frequency_hz-2424.12;
            const double variance=result.thermal_covariance[0][0];
            colored_error2+=error*error;colored_variance+=variance;
            colored_misses+=std::abs(error)>3*std::sqrt(variance);
        }
        if(seed<16) {
            const auto bright=h::estimate_helper_aided_phase(signal(10000+seed,28,0,false,156),configured,prediction,30e6,.02);
            if(bright.valid) {
                require(bright.thermal_covariance_valid,"bright admitted slope lacked covariance");
                ++bright_accepted;
                bright_misses+=std::abs(bright.measurement.high_frequency_hz-2424.12)>
                    3*std::sqrt(bright.thermal_covariance[0][0]);
            }
            noise_accepted+=h::estimate_helper_aided_phase(signal(11000+seed,28,0,true),configured,prediction,30e6,.02).valid;
            stale_accepted+=h::estimate_helper_aided_phase(signal(12000+seed,48,3),configured,prediction,30e6,.02).valid;
            auto retune=signal(13000+seed,80);
            for(std::size_t i=count/3;i<count;++i)retune[i]*=std::complex<float>(std::polar(1.,1.2));
            retune_accepted+=h::estimate_helper_aided_phase(retune,configured,prediction,30e6,.02).valid;
        }
    }
    const double alias_step=fs/static_cast<double>(count/4);
    for(const double multiple:{.98,1.,1.02})for(unsigned seed=0;seed<8;++seed)
        alias_accepted+=h::estimate_helper_aided_phase(signal(14000+seed,48,multiple*alias_step),configured,prediction,30e6,.02).valid;
    std::cout<<"helper_slope_covariance_controls colored_ar_rho=.4 colored_accepted="<<colored_accepted
        <<"/64 colored_high_frequency_rms_hz="<<std::sqrt(colored_error2/colored_accepted)
        <<" colored_sigma_rms_hz="<<std::sqrt(colored_variance/colored_accepted)
        <<" colored_three_sigma_misses="<<colored_misses<<" bright="<<bright_accepted
        <<"/16 bright_three_sigma_misses="<<bright_misses<<" noise="<<noise_accepted
        <<"/16 stale="<<stale_accepted<<"/16 aliases="<<alias_accepted<<"/24 retune="<<retune_accepted<<"/16\n";
    // This AR stress is outside the stationary-white likelihood. Extra noise
    // near the low pilot legitimately declines some apertures; check admitted
    // errors with a nonvacuous denominator, not an invented availability rate.
    require(colored_accepted>=16&&colored_misses<=4,"colored local-noise stress gave false precision or no useful support");
    require(bright_accepted>=14&&bright_misses<=2,"bright-probe slope covariance failed");
    require(noise_accepted==0&&stale_accepted==0&&alias_accepted==0&&retune_accepted==0,
        "optional covariance bypassed original fallback guards");

    auto chirp_config=configured;chirp_config.phase_noise_radians_per_sqrt_second=.03;
    chirp_config.slope_frequency_drift_bound_hz_per_second=.2;
    const double center=static_cast<double>(count-1)/(2*fs),rate=.2;
    const h::HelperAidedPhasePrediction chirp_prediction{true,2424.12+rate*center,574.0275+rate*center,.1,.1};
    unsigned chirp_accepted=0;double maximum_error=0,maximum_date_separation=0;
    for(unsigned seed=0;seed<16;++seed) {
        std::mt19937_64 rng(15000+seed);
        const double common=std::uniform_real_distribution<double>(-std::numbers::pi,std::numbers::pi)(rng);
        const std::array<double,4> high_gain{1,.7,1.3,.85},low_gain{1.4,.65,1,.8};
        std::vector<std::complex<float>> input(count);
        for(std::size_t i=0;i<count;++i) {
            const double t=static_cast<double>(i)/fs;
            const auto quarter=std::min<std::size_t>(3,i/(count/4));
            const auto x=std::polar(high_gain[quarter],tau*(2424.12*t+.5*rate*t*t)+.37+common)+
                std::polar(std::sqrt(.5)*low_gain[quarter],tau*(574.0275*t+.5*rate*t*t)-.81+common);
            input[i]=std::complex<float>(x);
        }
        const auto result=h::estimate_helper_aided_phase(input,chirp_config,chirp_prediction,30e6,.02);
        if(!result.valid)continue;
        require(result.thermal_covariance_valid&&result.frequency_dates_valid,"chirp covariance/date unavailable");
        ++chirp_accepted;
        const double high_date=result.high_frequency_time_seconds-.02,low_date=result.low_frequency_time_seconds-.02;
        const double high_error=std::abs(result.measurement.high_frequency_hz-(2424.12+rate*high_date));
        const double low_error=std::abs(result.measurement.low_frequency_hz-(574.0275+rate*low_date));
        maximum_error=std::max({maximum_error,high_error,low_error});
        maximum_date_separation=std::max(maximum_date_separation,std::abs(high_date-low_date));
        require(high_date>=0&&high_date<count/fs&&low_date>=0&&low_date<count/fs,"chirp date outside support");
        require(result.high_frequency_model_half_width_hz>=rate*count/fs&&
            result.low_frequency_model_half_width_hz>=rate*count/fs,"random-date drift model radius missing");
    }
    require(chirp_accepted==16&&maximum_error<.001&&maximum_date_separation>.001,
        "asymmetric quarter/chirp effective date check failed");
    auto invalid_bound=configured;invalid_bound.slope_frequency_drift_bound_hz_per_second=-1;
    require(!h::estimate_helper_aided_phase(signal(16000,48),invalid_bound,prediction,30e6,.02).valid,
        "negative declared drift bound qualified");
    std::cout<<"helper_slope_covariance_dates chirp_hz_per_s="<<rate<<" accepted="<<chirp_accepted
        <<"/16 max_frequency_error_at_effective_date_hz="<<maximum_error
        <<" high_low_date_separation_s="<<maximum_date_separation
        <<" declared_date_model_half_width_hz="<<(rate*count/fs)<<'\n';
}

void refinement_study() {
    constexpr unsigned seeds=64;
    const h::HelperAidedPhasePrediction prediction{true,2424.12,574.0275,.1,.1};
    for(double cn0:{28.,48.}) {
        for(const auto method:{h::HelperAidedFrequencyMethod::subaperture_slope,
                               h::HelperAidedFrequencyMethod::whole_hann,
                               h::HelperAidedFrequencyMethod::partitioned_hann}) {
            h::HelperAidedPhaseConfig c;c.sample_rate=fs;c.frequency_method=method;
            unsigned accepted=0,frequency_misses=0,phase_misses=0;
            double frequency_error2=0,phase_error2=0,reported_variance=0,phase_variance=0;
            double worst_frequency=0,worst_phase=0,elapsed=0;
            for(unsigned seed=0;seed<seeds;++seed) {
                const unsigned key=1000+seed;
                const double offset=(seed%2?1.:-1.)*.18;
                const auto input=signal(key,cn0,offset);
                const auto begin=std::clock();
                const auto m=h::estimate_helper_aided_phase(input,c,prediction,30e6,.02);
                elapsed+=static_cast<double>(std::clock()-begin)/CLOCKS_PER_SEC;
                if(!m.valid)continue;
                ++accepted;
                const double error=m.measurement.high_frequency_hz-(2424.12+offset);
                std::mt19937_64 phase_rng(key);
                const double common=std::uniform_real_distribution<double>(-std::numbers::pi,std::numbers::pi)(phase_rng);
                const double center=static_cast<double>(count-1)/(2*fs);
                const double phase=wrap(m.high_center_phase_radians-tau*(2424.12+offset)*center-.37-common);
                frequency_error2+=error*error;phase_error2+=phase*phase;
                reported_variance+=m.measurement.high_sigma_hz*m.measurement.high_sigma_hz;
                phase_variance+=m.high_center_phase_sigma_radians*m.high_center_phase_sigma_radians;
                worst_frequency=std::max(worst_frequency,std::abs(error));worst_phase=std::max(worst_phase,std::abs(phase));
                frequency_misses+=std::abs(error)>3*m.measurement.high_sigma_hz;
                phase_misses+=std::abs(phase)>3*m.high_center_phase_sigma_radians;
                const bool refined=method!=h::HelperAidedFrequencyMethod::subaperture_slope;
                require(m.work_sample_products<=(refined?72:20)*count,"frequency refinement work exceeded explicit bound");
                require(m.thermal_covariance_valid==refined,"refinement thermal covariance availability changed");
                if(refined) {
                    for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j)
                        require(std::isfinite(m.thermal_covariance[i][j])&&
                            std::abs(m.thermal_covariance[i][j]-m.thermal_covariance[j][i])<1e-12,
                            "refinement covariance is nonfinite or asymmetric");
                    require(m.thermal_covariance[0][0]>0&&m.thermal_covariance[1][1]>0&&
                        m.thermal_covariance[2][2]>0,"refinement covariance lost a marginal");
                }
            }
            require(accepted>=56,"refinement scarcely available in matched local branch");
            require(frequency_misses<=3&&phase_misses<=3,"refinement sampled 3-sigma coverage failed");
            const char* name=method==h::HelperAidedFrequencyMethod::subaperture_slope?"slope":
                method==h::HelperAidedFrequencyMethod::whole_hann?"whole-hann":"partitioned-hann";
            std::cout<<"helper_aided_refinement method="<<name<<" cn0_db_hz="<<cn0<<" accepted="<<accepted
                <<"/"<<seeds<<" frequency_rms_hz="<<std::sqrt(frequency_error2/accepted)
                <<" reported_frequency_sigma_rms_hz="<<std::sqrt(reported_variance/accepted)
                <<" frequency_3sigma_misses="<<frequency_misses<<" worst_frequency_hz="<<worst_frequency
                <<" phase_rms_deg="<<std::sqrt(phase_error2/accepted)*180/std::numbers::pi
                <<" reported_phase_sigma_rms_deg="<<std::sqrt(phase_variance/accepted)*180/std::numbers::pi
                <<" phase_3sigma_misses="<<phase_misses<<" worst_phase_deg="<<worst_phase*180/std::numbers::pi
                <<" receiver_cpu_s="<<elapsed<<" sampled_seconds="<<(seeds*count/fs)<<'\n';
        }
    }
    for(const auto method:{h::HelperAidedFrequencyMethod::whole_hann,h::HelperAidedFrequencyMethod::partitioned_hann}) {
        h::HelperAidedPhaseConfig c;c.sample_rate=fs;c.frequency_method=method;
        unsigned noise=0,stale=0,bright=0,aliases=0,retune=0;
        const double alias_step=fs/static_cast<double>(count/4);
        for(unsigned seed=0;seed<16;++seed) {
            noise+=h::estimate_helper_aided_phase(signal(2000+seed,28,0,true),c,prediction,30e6,.02).valid;
            stale+=h::estimate_helper_aided_phase(signal(2100+seed,48,3),c,prediction,30e6,.02).valid;
            bright+=h::estimate_helper_aided_phase(signal(2200+seed,28,0,false,156),c,prediction,30e6,.02).valid;
            auto step=signal(2300+seed,80);
            for(std::size_t i=count/3;i<count;++i)step[i]*=std::complex<float>(std::polar(1.,1.2));
            retune+=h::estimate_helper_aided_phase(step,c,prediction,30e6,.02).valid;
        }
        for(double cn0:{28.,48.,80.})for(double multiple:{.49,.98,1.,1.02,1.98})
            for(unsigned seed=0;seed<8;++seed)
                aliases+=h::estimate_helper_aided_phase(signal(2400+seed,cn0,multiple*alias_step),c,prediction,30e6,.02).valid;
        require(noise==0&&stale==0&&aliases==0&&retune==0,"refined likelihood admitted falsifying control");
        require(bright>=14,"refined likelihood lost distant bright-probe support");
        std::cout<<"helper_aided_refinement_controls method="<<(method==h::HelperAidedFrequencyMethod::whole_hann?"whole-hann":"partitioned-hann")
            <<" noise="<<noise<<"/16 stale="<<stale<<"/16 bright="<<bright<<"/16 aliases="<<aliases
            <<"/120 retune="<<retune<<"/16\n";
    }
    for(const auto method:{h::HelperAidedFrequencyMethod::whole_hann,h::HelperAidedFrequencyMethod::partitioned_hann}) {
        h::HelperAidedPhaseConfig c;c.sample_rate=fs;c.frequency_method=method;
        c.phase_noise_radians_per_sqrt_second=.03;
        double error2=0,variance=0,factor=0;unsigned accepted=0,misses=0;
        for(unsigned seed=0;seed<seeds;++seed) {
            auto input=signal(3000+seed,100);
            std::mt19937_64 rng(4000+seed);std::normal_distribution<double> normal;
            double phase=0;
            for(auto& x:input) {
                phase+=c.phase_noise_radians_per_sqrt_second/std::sqrt(fs)*normal(rng);
                x*=std::complex<float>(std::polar(1.,phase));
            }
            const auto m=h::estimate_helper_aided_phase(input,c,prediction,30e6,.02);
            if(!m.valid)continue;
            ++accepted;const double error=m.measurement.high_frequency_hz-2424.12;
            const double v=m.measurement.high_sigma_hz*m.measurement.high_sigma_hz+
                m.measurement.common_phase_frequency_sigma_hz*m.measurement.common_phase_frequency_sigma_hz;
            error2+=error*error;variance+=v;factor=m.common_frequency_process_variance_factor;
            misses+=std::abs(error)>3*std::sqrt(v);
        }
        require(accepted>=60&&misses<=3,"taper-specific Brownian process allowance failed");
        const double expected=method==h::HelperAidedFrequencyMethod::whole_hann?.04267609:.03201301;
        require(std::abs(factor*(count/fs)-expected)<2e-5,"Brownian taper transfer reused rectangular coefficient");
        std::cout<<"helper_aided_refinement_process method="<<(method==h::HelperAidedFrequencyMethod::whole_hann?"whole-hann":"partitioned-hann")
            <<" q_rad_per_sqrt_s="<<c.phase_noise_radians_per_sqrt_second<<" accepted="<<accepted<<"/"<<seeds
            <<" frequency_rms_hz="<<std::sqrt(error2/accepted)<<" reported_total_sigma_rms_hz="<<std::sqrt(variance/accepted)
            <<" process_factor="<<factor<<" three_sigma_misses="<<misses<<'\n';
    }
}

void transient_phase_study() {
    h::HelperAidedPhaseConfig c;c.sample_rate=fs;c.compute_slope_covariance=true;
    const h::HelperAidedPhasePrediction p{true,2424.12,574.0275,.1,.1};
    const h::HelperTransientPhaseConfig tc;
    unsigned clean=0,clean_flags=0,impaired=0,flags=0,selected=0;
    for(unsigned seed=0;seed<32;++seed){
        auto x=signal(5000+seed,28);
        auto b=h::estimate_helper_aided_phase(x,c,p,30e6,.02);
        if(b.valid){++clean;const auto t=h::estimate_helper_transient_phase(x,c,tc,p,b,.02);
            require(t.evaluated,"valid baseline failed transient witness");clean_flags+=t.disturbance_detected;
            if(t.valid)require(t.disturbance_detected&&t.support_first_sample<t.support_end_sample,
                "transient result lacked its conditional support");}
        // An added high-pilot component changes only the received input. The
        // baseline and transient witness see exactly the same impaired data.
        for(std::size_t i=0;i<1152;++i){const double time=static_cast<double>(i)/fs;
            x[i]+=std::complex<float>(std::polar(1.,tau*2424.12*time+std::numbers::pi/2));}
        b=h::estimate_helper_aided_phase(x,c,p,30e6,.02);
        if(!b.valid)continue;
        ++impaired;const auto t=h::estimate_helper_transient_phase(x,c,tc,p,b,.02);
        require(t.evaluated,"impaired valid baseline failed witness");flags+=t.disturbance_detected;
        if(t.valid){++selected;
            require(t.disturbance_detected&&t.support_first_sample<t.support_end_sample&&
                t.support_end_sample<=x.size()&&t.support_first_sample>=static_cast<std::size_t>(.04*fs)&&
                t.support_end_sample<=x.size()-static_cast<std::size_t>(.04*fs),
                "conditional selected support escaped the retune guard");
            const double lo=.02+static_cast<double>(t.support_first_sample)/fs;
            const double hi=.02+static_cast<double>(t.support_end_sample)/fs;
            require(t.anchor_time_seconds>=lo&&t.anchor_time_seconds<hi&&
                t.high_sigma_radians>0&&t.low_sigma_radians>0&&
                std::isfinite(t.high_phase_radians)&&std::isfinite(t.low_phase_radians)&&
                t.model_half_width_radians==tc.model_half_width_radians,
                "selected phase lacks finite dated uncertainty");
        }
    }
    auto invalid=tc;invalid.detection_sigma=0;
    const auto base=h::estimate_helper_aided_phase(signal(5010,28),c,p,30e6,.02);
    const auto unavailable=h::estimate_helper_transient_phase(signal(5010,28),c,invalid,p,base,.02);
    require(!unavailable.evaluated&&!unavailable.valid,"invalid transient config qualified");
    require(clean>=24&&impaired>=20,"transient fixture lacked baseline support");
    // Strong deterministic geometry control independent of any selection
    // frequency fit. The explicitly supplied previous coordinate and local
    // noise allowance let us verify correlation phase sign and exact date.
    auto deterministic=[](bool alternating){
        std::vector<std::complex<float>> x(count);
        for(std::size_t i=0;i<count;++i){
            const double t=static_cast<double>(i)/fs;
            auto y=std::polar(1.,tau*2424.12*t+.37)+std::polar(.7,tau*574.0275*t-.81);
            const std::size_t segment=i/(count/8);
            if((alternating&&(segment%2)==1)||(!alternating&&segment==1))y*=std::polar(1.,1.2);
            x[i]=std::complex<float>(y);
        }
        return x;
    };
    h::HelperAidedPhaseResult synthetic;synthetic.valid=true;synthetic.measurement.valid=true;
    synthetic.measurement.high_frequency_hz=p.high_frequency_hz;synthetic.measurement.low_frequency_hz=p.low_frequency_hz;
    synthetic.measurement.high_sigma_hz=.01;synthetic.measurement.low_sigma_hz=.01;
    synthetic.center_time_seconds=.02+static_cast<double>(count-1)/(2*fs);
    synthetic.high_raw_noise_density=synthetic.low_raw_noise_density=1e-5;
    synthetic.high_center_phase_sigma_radians=synthetic.low_center_phase_sigma_radians=.01;
    const auto resolved=h::estimate_helper_transient_phase(deterministic(false),c,tc,p,synthetic,.02);
    require(resolved.evaluated&&resolved.disturbance_detected&&resolved.valid,
        "deterministic transient failed to expose a supported side");
    const double dt=resolved.anchor_time_seconds-.02;
    require(std::abs(wrap(resolved.high_phase_radians-(tau*p.high_frequency_hz*dt+.37)))<.002&&
        std::abs(wrap(resolved.low_phase_radians-(tau*p.low_frequency_hz*dt-.81)))<.002,
        "selected phase sign/date disagrees with known tones");
    const auto unresolved=h::estimate_helper_transient_phase(deterministic(true),c,tc,p,synthetic,.02);
    require(unresolved.evaluated&&unresolved.disturbance_detected&&!unresolved.valid,
        "ambiguous alternating disturbances supplied conditional support");
    std::cout<<"helper_transient_phase clean="<<clean<<" clean_flags="<<clean_flags
        <<" impaired="<<impaired<<" flagged="<<flags<<" selected="<<selected
        <<" interpretation=conditional_no_detection_guarantee\n";
}
}
int main() {
    try {
        h::HelperAidedPhaseConfig c;c.sample_rate=fs;
        const h::HelperAidedPhasePrediction prediction{true,2424.12,574.0275,.1,.1};
        unsigned accepted=0,noise_accepted=0,stale_accepted=0,bright_accepted=0;
        double largest_phase=0,largest_frequency=0;
        for(unsigned seed=0;seed<16;++seed) {
            const auto x=signal(100+seed,28);
            const auto m=h::estimate_helper_aided_phase(x,c,prediction,30e6,.02);
            if(m.valid) {
                ++accepted;
                const double center=static_cast<double>(count-1)/(2*fs);
                std::mt19937_64 phase_rng(100+seed);
                const double common=std::uniform_real_distribution<double>(-std::numbers::pi,std::numbers::pi)(phase_rng);
                const double error=std::abs(wrap(m.high_center_phase_radians-tau*2424.12*center-.37-common));
                largest_phase=std::max(largest_phase,error);
                largest_frequency=std::max(largest_frequency,std::abs(m.measurement.high_frequency_hz-2424.12));
                require(error<6*m.high_center_phase_sigma_radians,"admitted high phase missed engineering allowance");
                require(m.work_sample_products<=20*count,"aided work was not bounded");
                require(std::abs(wrap(m.measurement.high_phase_radians+
                    tau*m.measurement.high_frequency_hz*center-m.high_center_phase_radians))<1e-8,
                    "first/center phase transport changed reference");
            }
            noise_accepted+=h::estimate_helper_aided_phase(signal(200+seed,28,0,true),c,prediction,30e6,.02).valid;
            stale_accepted+=h::estimate_helper_aided_phase(signal(300+seed,48,3),c,prediction,30e6,.02).valid;
            bright_accepted+=h::estimate_helper_aided_phase(signal(400+seed,28,0,false,156),c,prediction,30e6,.02).valid;
        }
        require(accepted>=14,"weak matched aided phase scarcely available");
        require(noise_accepted==0,"noise-only qualified aided phase");
        require(stale_accepted==0,"stale prediction qualified aided phase");
        require(bright_accepted>=14,"distant bright probe defeated matched tapered correlation");
        unsigned alias_accepted=0;
        const double alias_step=fs/static_cast<double>(count/4);
        for(double cn0:{28.,48.,80.})for(double multiple:{.49,.98,1.,1.02,1.98})
            for(unsigned seed=0;seed<8;++seed)
                alias_accepted+=h::estimate_helper_aided_phase(
                    signal(500+seed,cn0,multiple*alias_step),c,prediction,30e6,.02).valid;
        require(alias_accepted==0,"subwindow frequency alias qualified");
        auto curvature=h::estimate_helper_aided_phase(signal(502,80,0,false,0,4000),c,prediction,30e6,.02);
        require(!curvature.valid && curvature.fallback_required,"curved phase qualified constant frequency");
        auto bad=signal(503,48);bad[3]={std::numeric_limits<float>::infinity(),0};
        require(!h::estimate_helper_aided_phase(bad,c,prediction,30e6,.02).valid,"nonfinite sample qualified");
        require(!h::estimate_helper_aided_phase({},c,prediction,30e6,.02).valid,"empty aperture qualified");
        auto over=c;over.maximum_samples=count-1;
        require(h::estimate_helper_aided_phase(signal(504,48),over,prediction,30e6,.02).failure==
            h::HelperAidedPhaseFailure::work_limit,"oversized aperture bypassed bound");
        auto absent=prediction;absent.valid=false;
        require(h::estimate_helper_aided_phase(signal(505,48),c,absent,30e6,.02).fallback_required,
            "missing prediction did not request fallback");
        std::cout<<"helper_aided_phase accepted="<<accepted<<"/16 noise="<<noise_accepted
            <<"/16 stale="<<stale_accepted<<"/16 bright="<<bright_accepted
            <<"/16 alias="<<alias_accepted<<"/120 worst_center_phase_deg="<<largest_phase*180/std::numbers::pi
            <<" worst_frequency_hz="<<largest_frequency<<'\n';
        refinement_study();
        slope_covariance_study();
        transient_phase_study();
        return 0;
    }catch(const std::exception& e){std::cerr<<"Helper aided phase failure: "<<e.what()<<'\n';return 1;}
}
