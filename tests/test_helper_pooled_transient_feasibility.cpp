// Offline falsification of a pooled short-correlation transient witness.
// This is not a receiver or correction: its local frequency coordinate is
// simulator truth plus a fixed offset, and it does not process the probe.
// The longest unflagged interval is merely a provisional sample span, with
// no conditioner halo, selection covariance, or qualified model uncertainty.
#include "helper_live_probe_fixture.hpp"
#include "helper_transient_fixture.hpp"
#include "datapump/helper_aided_phase.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>

namespace {
namespace hp = helper_live_probe;
namespace h = datapump::helper;
using C = std::complex<double>;
constexpr double pi = std::numbers::pi, tau = 2*pi, fs = hp::sample_rate;
constexpr std::size_t guard = 480, aperture = 8640, coarse_n = 8, coarse_size = 1080;
constexpr std::size_t fine_n = 36, fine_size = 240;
double wrap(double x) { return std::remainder(x, tau); }

struct RobustFit { double phase = 0, frequency_hz = 0, cost = std::numeric_limits<double>::infinity(); };
RobustFit fit_high(const std::array<C,coarse_n>& z, double prediction, double huber) {
    RobustFit result;
    for (int grid=-15; grid<=15; ++grid) {
        const double residual=.05*grid;
        std::array<double,coarse_n> angles{};
        C initial{};
        for (std::size_t j=0;j<coarse_n;++j) {
            const double dt=(static_cast<double>(j*coarse_size)+539.5-4319.5)/fs;
            angles[j]=wrap(std::arg(z[j])-tau*residual*dt);
            initial+=std::polar(1.,angles[j]);
        }
        double mean=std::arg(initial);
        for(int iteration=0;iteration<3;++iteration) {
            C sum{};
            for (double angle:angles) {
                const double error=wrap(angle-mean);
                const double weight=std::min(1.,huber/std::max(std::abs(error),1e-9));
                sum+=weight*std::polar(1.,angle);
            }
            mean=std::arg(sum);
        }
        double cost=0;
        for(double angle:angles) {
            const double error=std::abs(wrap(angle-mean));
            cost+=error<=huber?error*error:2*huber*error-huber*huber;
        }
        if(cost<result.cost)result={mean,prediction+residual,cost};
    }
    return result;
}

double fit_low_phase(const std::array<C,coarse_n>& z,double residual) {
    C initial{};
    std::array<double,coarse_n> angles{};
    for(std::size_t j=0;j<coarse_n;++j) {
        const double dt=(static_cast<double>(j*coarse_size)+539.5-4319.5)/fs;
        angles[j]=wrap(std::arg(z[j])-tau*residual*dt);
        initial+=std::polar(1.,angles[j]);
    }
    double mean=std::arg(initial);
    for(int iteration=0;iteration<3;++iteration) {
        C sum{};
        for(double angle:angles) {
            const double error=wrap(angle-mean);
            sum+=std::min(1.,.20/std::max(std::abs(error),1e-9))*std::polar(1.,error);
        }
        mean=wrap(mean+std::arg(sum));
    }
    return mean;
}

struct Totals {
    unsigned attempted=0, admitted=0, rejected=0, flagged=0, nonempty_spans=0;
    std::uint64_t span_samples=0, error_samples=0, disturbed_samples=0;
    double baseline_phase2=0, candidate_phase2=0, frequency2=0;
    double baseline_max=0, candidate_max=0, sample_max=0;
};

void run(unsigned first_seed, unsigned count, const std::string& mode, double gate) {
    if(count==0 || count>64 || first_seed>UINT32_MAX-count || !std::isfinite(gate) || gate<=0)
        throw std::invalid_argument("invalid count, seed or threshold");
    Totals stats;
    for(unsigned seed=first_seed;seed<first_seed+count;++seed) {
        hp::Config config;
        config.seed=seed;
        config.expanded_schedule_key=true;
        hp::Source source(config);
        for(unsigned dwell=0;dwell<6;++dwell) {
            auto data=source.generate(dwell);
            if(dwell<2)continue;
            ++stats.attempted;
            const double center=data.first_seconds+(static_cast<double>(guard)+4319.5)/fs;
            const double high_frequency=2400*source.audio_scale_at(center)+source.shift_error_hz(data.shift_hz,center);
            const double low_frequency=550*source.audio_scale_at(center)+source.shift_error_hz(data.shift_hz,center);
            const C original_common=(C(data.common_phasor[guard+4319])+C(data.common_phasor[guard+4320]))*.5;
            // This settled-reference phase is evaluator truth, never an input to
            // the pooled estimator or its provisional support selector.
            const double settled_phase=wrap(std::arg(original_common)+tau*std::remainder(2400*source.tx_time(center),1.));
            helper_transient::Profile profile{};
            std::unique_ptr<helper_transient::Model> model;
            if(mode!="clean") {
                profile=helper_transient::named(mode,dwell);
                model=std::make_unique<helper_transient::Model>(config,profile);
                model->apply(data,source,dwell);
            }
            const auto input=std::span<const std::complex<float>>(data.samples.data()+guard,aperture);
            const double offset=((seed+dwell)&1U)? .15:-.15;
            h::HelperAidedPhaseConfig aided_config;
            aided_config.sample_rate=fs;
            const h::HelperAidedPhasePrediction coordinate{true,high_frequency+offset,low_frequency+offset,.2,.2};
            const auto base=h::estimate_helper_aided_phase(input,aided_config,coordinate,data.shift_hz,data.first_seconds+guard/fs);
            if(!base.valid) { ++stats.rejected; continue; }
            ++stats.admitted;
            std::array<C,coarse_n> high{},low{};
            for(std::size_t j=0;j<coarse_n;++j) {
                const double start_dt=(static_cast<double>(j*coarse_size)-4319.5)/fs;
                C oh=std::polar(1.,-tau*coordinate.high_frequency_hz*start_dt);
                C ol=std::polar(1.,-tau*coordinate.low_frequency_hz*start_dt);
                const C sh=std::polar(1.,-tau*coordinate.high_frequency_hz/fs);
                const C sl=std::polar(1.,-tau*coordinate.low_frequency_hz/fs);
                double sum_weights=0;
                for(std::size_t k=0;k<coarse_size;++k) {
                    const double w=.5-.5*std::cos(tau*static_cast<double>(k)/(coarse_size-1));
                    high[j]+=C(input[j*coarse_size+k])*oh*w;
                    low[j]+=C(input[j*coarse_size+k])*ol*w;
                    oh*=sh;ol*=sl;sum_weights+=w;
                }
                high[j]/=sum_weights;low[j]/=sum_weights;
            }
            const auto fit=fit_high(high,coordinate.high_frequency_hz,.20);
            const double residual=fit.frequency_hz-coordinate.high_frequency_hz;
            const double low_phase=fit_low_phase(low,residual);
            double high_amp=0,low_amp=0;
            for(std::size_t j=0;j<coarse_n;++j){high_amp+=std::abs(high[j])/coarse_n;low_amp+=std::abs(low[j])/coarse_n;}
            const double effective=(fine_size-1)/(1.5*fs);
            const double wh=high_amp*high_amp*effective/base.high_raw_noise_density;
            const double wl=low_amp*low_amp*effective/base.low_raw_noise_density;
            if(!(wh>0&&wl>0&&std::isfinite(wh)&&std::isfinite(wl)))throw std::runtime_error("invalid local noise estimate");
            const double sigma_h=1/std::sqrt(wh),sigma_l=1/std::sqrt(wl),sigma_common=1/std::sqrt(wh+wl);
            std::array<double,fine_n> rh{},rl{},common{};
            for(std::size_t j=0;j<fine_n;++j) {
                const double dt=(static_cast<double>(j*fine_size)-4319.5)/fs;
                C oh=std::polar(1.,-tau*fit.frequency_hz*dt-fit.phase);
                C ol=std::polar(1.,-tau*(coordinate.low_frequency_hz+residual)*dt-low_phase);
                const C sh=std::polar(1.,-tau*fit.frequency_hz/fs);
                const C sl=std::polar(1.,-tau*(coordinate.low_frequency_hz+residual)/fs);
                C zh{},zl{};double sum_weights=0;
                for(std::size_t k=0;k<fine_size;++k) {
                    const double w=.5-.5*std::cos(tau*static_cast<double>(k)/(fine_size-1));
                    zh+=C(input[j*fine_size+k])*oh*w;zl+=C(input[j*fine_size+k])*ol*w;
                    oh*=sh;ol*=sl;sum_weights+=w;
                }
                rh[j]=zh.imag()/(sum_weights*high_amp);
                rl[j]=zl.imag()/(sum_weights*low_amp);
                common[j]=(wh*rh[j]+wl*rl[j])/(wh+wl);
            }
            // Fixed, bounded 20/30/60 ms windows. The maximum over overlapping
            // windows is not a calibrated likelihood ratio or a safe gate.
            std::array<bool,fine_n> suspect{};
            for(std::size_t length:{2U,3U,6U})for(std::size_t start=0;start+length<=fine_n;++start) {
                double sum_h=0,sum_l=0,sum_common=0;
                for(std::size_t j=start;j<start+length;++j){sum_h+=rh[j];sum_l+=rl[j];sum_common+=common[j];}
                const double norm=std::sqrt(static_cast<double>(length));
                const double score=std::max({std::abs(sum_h)/(sigma_h*norm),std::abs(sum_l)/(sigma_l*norm),
                                             std::abs(sum_common)/(sigma_common*norm)});
                if(score>gate)for(std::size_t j=start;j<start+length;++j)suspect[j]=true;
            }
            if(std::any_of(suspect.begin(),suspect.end(),[](bool value){return value;}))++stats.flagged;
            std::size_t first=0,last=0,begin=0;
            for(std::size_t j=0;j<=fine_n;++j) {
                if(j<fine_n&&!suspect[j])continue;
                if(j-begin>last-first){first=begin;last=j;}
                begin=j+1;
            }
            if(last>first) {
                ++stats.nonempty_spans;
                for(std::size_t i=first*fine_size;i<last*fine_size;++i) {
                    const double local=(guard+static_cast<double>(i))/fs;
                    const double date=data.first_seconds+local;
                    const double actual=wrap(std::arg(C(data.common_phasor[guard+i]))+
                        tau*std::remainder(2400*source.tx_time(date),1.));
                    const double estimate=wrap(fit.phase+tau*fit.frequency_hz*(static_cast<double>(i)-4319.5)/fs);
                    const double error=std::abs(wrap(estimate-actual));
                    ++stats.span_samples;
                    if(error>.2)++stats.error_samples;
                    stats.sample_max=std::max(stats.sample_max,error);
                    // Evaluator-only: the fixture's frequency excursion can
                    // retain integrated phase after its active frequency pulse.
                    if(model && model->disturbed(dwell,local))++stats.disturbed_samples;
                }
            }
            const double baseline_error=wrap(base.high_center_phase_radians-settled_phase);
            const double candidate_error=wrap(fit.phase-settled_phase);
            stats.baseline_phase2+=baseline_error*baseline_error;
            stats.candidate_phase2+=candidate_error*candidate_error;
            stats.frequency2+=std::pow(fit.frequency_hz-high_frequency,2);
            stats.baseline_max=std::max(stats.baseline_max,std::abs(baseline_error));
            stats.candidate_max=std::max(stats.candidate_max,std::abs(candidate_error));
        }
    }
    if(!stats.admitted)throw std::runtime_error("no admitted reference observations");
    const double n=stats.admitted;
    std::cout<<std::setprecision(10)<<"pooled_transient mode="<<mode<<" seed_first="<<first_seed<<" seeds="<<count
        <<" key_namespace=expanded_public_fixture temperature_K=290 noise_figure_db=10 helper_w=40 loss_db=180"
        <<" dwell_samples="<<aperture<<" baseline_attempted="<<stats.attempted<<" baseline_admitted="<<stats.admitted
        <<" baseline_rejected="<<stats.rejected<<" provisional_flagged_dwells="<<stats.flagged
        <<" baseline_phase_rms_deg="<<std::sqrt(stats.baseline_phase2/n)*180/pi
        <<" candidate_phase_rms_deg="<<std::sqrt(stats.candidate_phase2/n)*180/pi
        <<" baseline_phase_max_deg="<<stats.baseline_max*180/pi<<" candidate_phase_max_deg="<<stats.candidate_max*180/pi
        <<" candidate_frequency_rms_hz="<<std::sqrt(stats.frequency2/n)
        <<" provisional_nonempty_spans="<<stats.nonempty_spans<<" provisional_span_samples="<<stats.span_samples
        <<" provisional_error_gt_0p2rad_samples="<<stats.error_samples<<" provisional_disturbed_samples="<<stats.disturbed_samples
        <<" provisional_sample_max_deg="<<stats.sample_max*180/pi<<" scan_threshold="<<gate
        <<" coordinate=truth_plus_or_minus_0p15Hz_not_a_receiver reference=undisturbed_center_evaluator_only"
        <<" frequency_reference=undisturbed_center_nonstationary_apertures_have_no_single_truth"
        <<" high_additive_reference=undisturbed_desired_pilot_not_total_waveform"
        <<" probe_absent_at_test_dwells=1 probe_used=0 correction_or_covariance_qualified=0\n";
}
}
int main(int argc,char** argv) try {
    if(argc>5)throw std::invalid_argument("usage: [first_seed] [count] [clean|transient-profile] [scan_threshold]");
    const auto seed_arg=argc>1?std::stoull(argv[1]):1666973697ULL;
    const auto count_arg=argc>2?std::stoull(argv[2]):8ULL;
    if(seed_arg>UINT32_MAX || count_arg>UINT32_MAX)throw std::invalid_argument("seed or count out of range");
    const auto seed=static_cast<unsigned>(seed_arg);
    const auto count=static_cast<unsigned>(count_arg);
    const std::string mode=argc>3?argv[3]:"clean";
    const double gate=argc>4?std::stod(argv[4]):3.5;
    run(seed,count,mode,gate);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
