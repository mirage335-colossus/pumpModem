// Evaluator-only pilot efficiency comparison.  The ideal path uses simulation
// truth and must never feed a receiver, acquisition gate or branch choice.
#include "datapump/helper_aided_phase.hpp"
#include "helper_tracking_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace hp=helper_live_probe;
namespace ht=helper_tracking;
namespace h=datapump::helper;
using C=std::complex<double>;
using S=std::complex<float>;
constexpr double fs=hp::sample_rate,tau=2*std::numbers::pi;
constexpr std::size_t guard=480,count=8640;

struct Moment { C sum{},first{},second{}; };
Moment moments(std::span<const C> x,double f) {
    Moment m;
    const double center=static_cast<double>(x.size()-1)/2;
    for(std::size_t i=0;i<x.size();++i) {
        const double time=(static_cast<double>(i)-center)/fs;
        const C z=x[i]*std::polar(1.,-tau*f*time);
        const C derivative(0,-tau*time);
        m.sum+=z;m.first+=z*derivative;m.second+=z*derivative*derivative;
    }
    return m;
}
// Separate bounded high-pilot local likelihood.  Its initial coordinate is
// truth+0.2 Hz: this is deliberately idealized prior information, not a
// receiver input. It is not the phase benchmark which uses known frequency.
double oracle_frequency(std::span<const C> x,double initial) {
    double f=initial;
    for(unsigned n=0;n<6;++n) {
        const auto m=moments(x,f);
        const double gradient=2*std::real(std::conj(m.sum)*m.first);
        const double curvature=2*(std::norm(m.first)+std::real(std::conj(m.sum)*m.second));
        if(!(curvature<0)||!std::isfinite(curvature))break;
        const double step=std::clamp(-gradient/curvature,-.25,.25);
        f=std::clamp(f+step,initial-.75,initial+.75);
        if(std::abs(step)<1e-5)break;
    }
    return f;
}
struct Stats {unsigned n=0;double e2=0,max=0;void add(double x){++n;e2+=x*x;max=std::max(max,std::abs(x));} double rms()const{return n?std::sqrt(e2/n):0;}};

void run(double loss,unsigned seed,unsigned dwells,hp::Probe probe,bool noise,bool expanded_key) {
    hp::Config c;c.seed=seed;c.path_loss_db=loss;c.probe=probe;c.noise=noise;c.expanded_schedule_key=expanded_key;
    ht::Source source(c,{});
    h::Options options;options.power_w=c.helper_w;options.data_power_w=1;options.callsign=c.callsign;
    h::Generator generator(options,h::Plan{2400,.4,0,c.helper_w,950,550,c.hopping},c.epoch,true);
    const double max_rf=31.225e6, width=3*(max_rf+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    h::HelperMeasurement receiver(mc);
    h::HelperAidedPhaseConfig ac;ac.sample_rate=fs;ac.phase_noise_radians_per_sqrt_second=mc.phase_noise_radians_per_sqrt_second;
    ac.compute_slope_covariance=true;ac.slope_frequency_drift_bound_hz_per_second=.001;
    unsigned aided_count=0,broad_count=0,rejected=0;
    Stats aided_phase,ideal_phase,ideal_dual_phase,aided_frequency,ideal_frequency;
    Stats aided_phase_heldout,ideal_phase_heldout,ideal_dual_phase_heldout;
    double noiseless_subtraction_max=0;
    for(unsigned dwell=0;dwell<dwells;++dwell){
        const auto d=source.generate(dwell);
        const auto data=std::span<const S>(d.samples.data()+guard,count);
        const double start=d.first_seconds+guard/fs;
        const double center=start+static_cast<double>(count-1)/(2*fs);
        const auto prior=receiver.snapshot();
        h::HelperAidedPhaseResult a;
        if(prior.valid){
            const auto high=receiver.predict(2400,d.shift_hz,center,count/fs);
            const auto low=receiver.predict(550,d.shift_hz,center,count/fs);
            h::HelperAidedPhasePrediction p{high.valid&&low.valid,high.frequency_hz,low.frequency_hz,
                high.mean_sigma_hz+high.aperture_process_sigma_hz,low.mean_sigma_hz+low.aperture_process_sigma_hz};
            a=h::estimate_helper_aided_phase(data,ac,p,d.shift_hz,start);
            if(a.valid){if(!receiver.ingest(a.measurement))throw std::runtime_error("receiver intake failure");++aided_count;}
            else ++rejected;
        }
        if(!a.valid){receiver.observe(data,d.shift_hz,start);++broad_count;}

        // Everything below this point is evaluator-only. It removes the exact
        // nuisance Helper (low pilot and time-varying auxiliary) and clean
        // independent probe, using the simulated common complex path, while
        // retaining the identical thermal noise and high-pilot waveform.
        std::vector<C> pure(count),low_pure(count);
        const double low_amplitude=h::low_pilot_amplitude_fraction/h::pilot_amplitude_fraction;
        for(std::size_t j=0;j<count;++j){
            const auto i=guard+j;const double date=d.first_seconds+static_cast<double>(i)/fs;
            const double tx=source.tx_time(date);const C common(d.common_phasor[i]);
            const C helper=generator.mix_analytic({},tx,fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            const C high=std::polar(1.,tau*std::remainder(2400*tx,1.));
            const C low=low_amplitude*std::polar(1.,tau*std::remainder(550*tx,1.));
            pure[j]=C(d.samples[i])-(helper-high)*common-C(d.clean_probe[i]);
            low_pure[j]=C(d.samples[i])-(helper-low)*common-C(d.clean_probe[i]);
            if(!noise)noiseless_subtraction_max=std::max(noiseless_subtraction_max,std::abs(pure[j]-high*common));
            if(!noise)noiseless_subtraction_max=std::max(noiseless_subtraction_max,std::abs(low_pure[j]-low*common));
        }
        if(!a.valid)continue;
        const auto i=guard+(count-1)/2;const double frac=.5;
        const double common=std::arg(C(d.common_phasor[i]))+frac*std::remainder(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])),tau);
        const double phase=std::remainder(tau*std::remainder(2400*source.tx_time(center),1.)+common,tau);
        const double f=2400*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center);
        const auto high_moment=moments(pure,f);
        const double ideal_phi=std::arg(high_moment.sum);
        const double low_f=550*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center);
        const C low_moment=moments(low_pure,low_f).sum;
        const double known_difference=tau*std::remainder((2400-550)*source.tx_time(center),1.);
        const double ideal_dual_phi=std::arg(high_moment.sum+low_amplitude*low_moment*std::polar(1.,known_difference));
        const double ideal_f=oracle_frequency(pure,f+.2);
        aided_phase.add(std::remainder(a.high_center_phase_radians-phase,tau));
        ideal_phase.add(std::remainder(ideal_phi-phase,tau));
        ideal_dual_phase.add(std::remainder(ideal_dual_phi-phase,tau));
        if(dwell>=95){
            aided_phase_heldout.add(std::remainder(a.high_center_phase_radians-phase,tau));
            ideal_phase_heldout.add(std::remainder(ideal_phi-phase,tau));
            ideal_dual_phase_heldout.add(std::remainder(ideal_dual_phi-phase,tau));
        }
        aided_frequency.add(a.measurement.high_frequency_hz-f);
        ideal_frequency.add(ideal_f-f);
    }
    std::cout<<std::setprecision(10)<<"helper_pilot_oracle seed="<<seed<<" loss_db="<<loss<<" helper_w="<<c.helper_w
        <<" probe_w="<<c.probe_w<<" probe="<<(probe==hp::Probe::tone?"tone":"apsk")<<" schedule=keyed_hf50"
        <<" schedule_key_namespace="<<(expanded_key?"expanded_v3":"legacy")
        <<" temperature_K="<<c.temperature_kelvin<<" noise_figure_db="<<c.noise_figure_db
        <<" probe_active_start_seconds=38 probe_absent_before_38=1"
        <<" dwell_count="<<dwells<<" aperture_samples="<<count<<" aperture_seconds="<<count/fs
        <<" aided_accepted="<<aided_count<<" aided_rejected="<<rejected<<" broad_calls="<<broad_count
        <<" common_supported_dwells="<<ideal_phase.n<<" common_support=aided_valid_dwells"
        <<" aided_phase_rms_deg="<<aided_phase.rms()*180/std::numbers::pi
        <<" ideal_known_frequency_phase_rms_deg="<<ideal_phase.rms()*180/std::numbers::pi
        <<" ideal_known_difference_dual_phase_rms_deg="<<ideal_dual_phase.rms()*180/std::numbers::pi
        <<" aided_phase_worst_deg="<<aided_phase.max*180/std::numbers::pi
        <<" ideal_known_frequency_phase_worst_deg="<<ideal_phase.max*180/std::numbers::pi
        <<" aided_frequency_rms_hz="<<aided_frequency.rms()<<" ideal_profile_frequency_rms_hz="<<ideal_frequency.rms()
        <<" aided_frequency_worst_hz="<<aided_frequency.max<<" ideal_profile_frequency_worst_hz="<<ideal_frequency.max
        <<" heldout_supported_dwells="<<aided_phase_heldout.n
        <<" aided_heldout_phase_rms_deg="<<aided_phase_heldout.rms()*180/std::numbers::pi
        <<" ideal_heldout_phase_rms_deg="<<ideal_phase_heldout.rms()*180/std::numbers::pi
        <<" ideal_dual_heldout_phase_rms_deg="<<ideal_dual_phase_heldout.rms()*180/std::numbers::pi
        <<" noise="<<noise<<" noiseless_subtraction_max="<<noiseless_subtraction_max
        <<" comparator=local_high_pilot_aided_with_helper_global_prediction"
        <<" oracle_knows=exact_nuisance_waveforms_per_sample_complex_path_pilot_frequencies_and_true_differential_phase"
        <<" oracle_frequency_initial_coordinate=truth_plus_0.2_hz"
        <<" oracle_phase_diffusion=retained oracle_feeds_receiver=0\n";
    if(aided_count!=ideal_phase.n || ideal_phase.n<3)throw std::runtime_error("insufficient common supported pilot observations");
    if(!noise&&noiseless_subtraction_max>2e-5)throw std::runtime_error("oracle nuisance subtraction differs from generated waveform");
}
int main(int argc,char** argv) try {
    const double loss=argc>1?std::stod(argv[1]):180;
    const unsigned seed=argc>2?static_cast<unsigned>(std::stoul(argv[2])):901;
    const unsigned dwells=argc>3?static_cast<unsigned>(std::stoul(argv[3])):8;
    const std::string probe=argc>4?argv[4]:"tone";
    const bool expanded_key=probe.ends_with("-v3");
    const std::string waveform=expanded_key?probe.substr(0,probe.size()-3):probe;
    const bool apsk=waveform=="apsk";
    const bool noise=argc<=5||std::string(argv[5])!="no-noise";
    if(argc>6||dwells>100||dwells<4||loss<0||(waveform!="apsk"&&waveform!="tone")||
       (argc>5&&std::string(argv[5])!="no-noise"))throw std::invalid_argument("usage: [loss_db] [seed] [4..100 dwells] [tone|apsk, optional -v3 key suffix] [no-noise]");
    run(loss,seed,dwells,apsk?hp::Probe::apsk64:hp::Probe::tone,noise,expanded_key);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
