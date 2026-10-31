#include "datapump/helper_carrier.hpp"
#include "datapump/helper_frequency_measurement.hpp"
#include "datapump/helper_measurement.hpp"
#include "helper_measurement_hops.hpp"
#include "helper_measurement_sim.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Independent EME link study, not an ordinary Robust receiver. Power means
// active-mean composite Helper power, not high-pilot power. The actual-waveform
// source and simplified source both model random RF retune phase, separate
// endpoint clocks, physical-time Brownian phase, and thermal AWGN. Known RF
// commands, never oscillator truth, enter the receiver.
namespace {
namespace h=datapump::helper;
namespace sim=helper_measurement_sim;
constexpr double fs=24000,tau=2*std::numbers::pi;
constexpr double rf_hz=5.8e9,rf_ppm=.00008,rf_bound_ppm=.0001,q_deg=.005;
constexpr double tx_audio_ppm=37,rx_audio_ppm=-13;
struct Case {
 double power_w,loss_db;unsigned seed;bool actual,signal=true,disciplined_audio=false;
 // Extra RF error rate at 5.8 GHz, for explicitly out-of-model controls.
 double residual_rate_hz_per_second=0;
 double residual_acceleration_hz_per_second2=0;
 // Deliberately simple flat two-path model: second-path voltage relative to
 // first and an independently specified differential Doppler. It is not a
 // measured lunar scattering spectrum or a complete multipath model.
 double multipath_amplitude=0,multipath_doppler_hz=0;
 double noise_figure_db=10;
};
double tx_ppm(const Case& c){return c.disciplined_audio?.00004:tx_audio_ppm;}
double rx_ppm(const Case& c){return c.disciplined_audio?-.00003:rx_audio_ppm;}
double tx_scale(const Case& c){return 1+tx_ppm(c)*1e-6;}
double rx_scale(const Case& c){return 1+rx_ppm(c)*1e-6;}
struct Dwell {std::vector<std::complex<float>> samples;double first,rf;};
double n0_dbm_hz(const Case& c){return 10*std::log10(1.380649e-23*290*1000)+c.noise_figure_db;}
double high_fraction() {
    // Use the same actual Generator options, epoch, 36-second active calendar
    // and 4ms pilot dwell gating. mean_power accepts at most 2s per call, so
    // average eighteen exact 2s reference windows. The generator is evaluated
    // on the transmitter clock; the tiny audio clock correction to 36s is
    // negligible here and is not treated as an output-power measurement.
    static const double fraction=[] {
       h::Options o;o.power_w=15000;o.data_power_w=1;o.callsign="N0CALL";
       double total=0;
       for(unsigned i=0;i<18;i++) {
          h::PowerReference ref;ref.epoch=1800000000;ref.begin_seconds=2*i;ref.end_seconds=2*(i+1);
          total+=h::mean_power(o,true,&ref).total;
       }
       return h::pilot_power_fraction/(total/18);
    }();
    return fraction;
}
double high_cn0(const Case& c){return 10*std::log10(c.power_w*1000*high_fraction())-c.loss_db-n0_dbm_hz(c);}
double now(){auto t=std::clock();if(t==static_cast<std::clock_t>(-1))throw std::runtime_error("CPU clock unavailable");return double(t)/CLOCKS_PER_SEC;}

class PhysicalSource {
public:
 explicit PhysicalSource(Case c):c_(c),rng_(c.seed),hops_(helper_measurement_hops::fcc100(c.seed)),generator_(make_generator()) {
    cn0_=std::pow(10.,high_cn0(c)/10);
    sigma_=std::sqrt(fs*rx_scale(c_)/(2*cn0_));
 }
 Dwell generate(unsigned n) {
    if(n!=next_++)throw std::logic_error("nonsequential simulation");
    Dwell d;d.first=n*.4+.02;d.rf=hops_.frequency(n);d.samples.resize(8640);
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
    if(n)rf_phase_+=uniform(rng_);
    constexpr double q=q_deg*std::numbers::pi/180;
    brown_+=q*std::sqrt(.02/rx_scale(c_))*normal(rng_);
    const double step=q/std::sqrt(fs*rx_scale(c_)),shift=d.rf*rf_ppm*1e-6;
    for(std::size_t i=0;i<d.samples.size();++i) {
       const double physical=(d.first+double(i)/fs)/rx_scale(c_);
       const auto actual=generator_.mix_analytic({},physical*tx_scale(c_),static_cast<std::uint32_t>(fs));
       // Normalize production pilot amplitude to unity, keeping the actual
       // metadata auxiliary and its transmitter timing intact.
       const double begin=n*.4/rx_scale(c_);
       const double residual_cycles=c_.residual_rate_hz_per_second*(d.rf/rf_hz)*
          .5*(physical*physical-begin*begin)+c_.residual_acceleration_hz_per_second2*(d.rf/rf_hz)*
          (physical*physical*physical-begin*begin*begin)/6;
       auto z=(c_.signal?actual/(.4*h::pilot_amplitude_fraction):std::complex<double>{})*
            std::polar(1.,rf_phase_+tau*(shift*(.02+double(i)/fs)/rx_scale(c_)+residual_cycles)+brown_);
       if(c_.multipath_amplitude!=0)
          z*=(1.+c_.multipath_amplitude*std::polar(1.,tau*c_.multipath_doppler_hz*physical+.83))/
             std::sqrt(1+c_.multipath_amplitude*c_.multipath_amplitude);
       d.samples[i]={float(z.real()+sigma_*normal(rng_)),float(z.imag()+sigma_*normal(rng_))};
       brown_+=step*normal(rng_);
    }
    brown_+=q*std::sqrt(.02/rx_scale(c_))*normal(rng_);
    rf_phase_+=tau*shift*.4/rx_scale(c_);
    const double t0=n*.4/rx_scale(c_),t1=(n+1)*.4/rx_scale(c_);
    rf_phase_+=tau*c_.residual_rate_hz_per_second*(d.rf/rf_hz)*.5*(t1*t1-t0*t0);
    rf_phase_+=tau*c_.residual_acceleration_hz_per_second2*(d.rf/rf_hz)*(t1*t1*t1-t0*t0*t0)/6;
    return d;
 }
private:
 h::Generator make_generator(){h::Options o;o.power_w=c_.power_w;o.data_power_w=1;o.callsign="N0CALL";
    h::Plan p{2400,.4,0,c_.power_w,950,550,true};return h::Generator(o,p,1800000000,true);}
 Case c_;std::mt19937_64 rng_;helper_measurement_hops::Profile hops_;h::Generator generator_;
 double cn0_=0,sigma_=0,brown_=0,rf_phase_=.31;unsigned next_=0;
};
struct Row {
 bool signal=false,valid=false,unc=false,boundary=false,limited=false,accurate=false,covered=false,narrow=false;
 double error=std::numeric_limits<double>::quiet_NaN(),half=std::numeric_limits<double>::quiet_NaN();
 double audio_error=std::numeric_limits<double>::quiet_NaN(),cpu=0,heldout_model=std::numeric_limits<double>::quiet_NaN();
 double heldout_observed=std::numeric_limits<double>::quiet_NaN();bool heldout_edge=false;
};
std::vector<Dwell> capture(const Case& c,unsigned count) {
 std::vector<Dwell> dwells;dwells.reserve(count+5);
 if(c.actual) {PhysicalSource s(c);for(unsigned i=0;i<count+(count==90?5:0);++i)dwells.push_back(s.generate(i));}
 else {
    if(c.residual_acceleration_hz_per_second2!=0 || c.multipath_amplitude!=0)
       throw std::invalid_argument("synthetic source lacks acceleration or multipath");
    sim::Link link;link.path_loss_db=c.loss_db;link.transmit_high_dbm=10*std::log10(c.power_w*1000*high_fraction());
    link.noise_figure_db=c.noise_figure_db;
    link.shift_center_hz=rf_hz;link.tx_shift_ppm=rf_ppm;link.rx_shift_ppm=0;
    link.tx_audio_ppm=tx_ppm(c);link.rx_audio_ppm=rx_ppm(c);link.phase_noise_degrees_per_sqrt_s=q_deg;
    link.shift_drift_ppm_per_s=c.residual_rate_hz_per_second*1e6/rf_hz;
    link.hopping=true;link.signal_enabled=c.signal;link.seed=c.seed;
    sim::Source source(link);for(unsigned i=0;i<count+(count==90?5:0);++i){auto z=source.generate(i);dwells.push_back({std::move(z.samples),z.first_seconds,z.shift_hz});}
 }
 return dwells;
}
Row run(const Case& c,unsigned count) {
 auto dwells=capture(c,count);
 h::HelperPooledConfig cfg;const double high_prior=3*(5868.4e6*rf_bound_ppm+2400*100)*1e-6;
 cfg.measurement={fs,550,2400,2400-high_prior,2400+high_prior,300,q_deg*std::numbers::pi/180/std::sqrt(rx_scale(c))};
 Row r;r.signal=c.signal;double start=now();h::HelperPooledMeasurement rx(cfg);r.cpu+=now()-start;
 for(unsigned i=0;i<count;++i) {
    start=now();const bool accepted=rx.observe(dwells[i].samples,dwells[i].rf,dwells[i].first);r.cpu+=now()-start;
    if(!accepted)throw std::runtime_error("dwell geometry rejected");
    start=now();(void)rx.snapshot((i+1)*.4);r.cpu+=now()-start;
 }
 start=now();const auto state=rx.snapshot(count*.4);r.cpu+=now()-start;
 r.valid=state.valid;r.unc=state.uncertainty_valid;r.boundary=state.boundary_limited;r.limited=state.work_limited;
 if(count<90 && r.valid)throw std::runtime_error("short probe incorrectly completed observation");
 if(!r.valid || !c.signal || count<90)return r;
 const auto& s=state.clock;const double at_rf=dwells[count-1].rf;
 const double truth_audio=(tx_scale(c)/rx_scale(c)-1)*1e6,truth_shift=rf_ppm/rx_scale(c);
 r.audio_error=s.audio_ppm-truth_audio;
 r.error=(1500*r.audio_error+at_rf*(s.shift_ppm-truth_shift))*1e-6;
 r.accurate=std::abs(r.error)<=.1;
 if(r.unc) {
    const double v[]={1500e-6,at_rf*1e-6,0,0};double var=0;
    for(int i=0;i<4;i++)for(int j=0;j<4;j++)var+=v[i]*s.covariance[i][j]*v[j];
    if(!s.valid || !std::isfinite(var) || var<0)throw std::runtime_error("invalid claimed covariance");
    r.half=3*std::sqrt(var);r.covered=std::abs(r.error)<=r.half;r.narrow=r.half<=.1;
 } else if(r.boundary && (s.valid || !std::isnan(s.audio_sigma_ppm) || !std::isnan(s.shift_sigma_ppm)))
    throw std::runtime_error("boundary fit must not claim covariance");
 // Five further dwells to t=38 s, independently sampled. Search a small
 // residual grid around receiver prediction, without truth-centering. An edge
 // is reported explicitly; it is not a qualified precision interval.
 double best=-1,best_offset=0;
 for(int k=-20;k<=20;k++) {
    const double offset=k*.05;double energy=0;
    for(unsigned i=count;i<count+5;i++) {
       const auto& d=dwells[i];const double dt=d.first+.18-s.time_seconds;
       const double f=2400*(1+(s.audio_ppm+dt*s.audio_drift_ppm_per_second)*1e-6)+
          d.rf*(s.shift_ppm+dt*s.shift_drift_ppm_per_second)*1e-6+offset;
       std::complex<double> accum{},rot=1,step=std::polar(1.,-tau*f/fs);
       for(const auto value:d.samples){accum+=std::complex<double>(value)*rot;rot*=step;}
       energy+=std::norm(accum);
    }
    if(energy>best){best=energy;best_offset=offset;}
 }
 const auto& last=dwells[count+4];const double dt=last.first+.18-s.time_seconds;
 const double predicted=2400*(1+(s.audio_ppm+dt*s.audio_drift_ppm_per_second)*1e-6)+
     last.rf*(s.shift_ppm+dt*s.shift_drift_ppm_per_second)*1e-6;
 const double truth=(2400*tx_scale(c)+last.rf*rf_ppm*1e-6)/rx_scale(c);
 r.heldout_model=predicted-truth;r.heldout_observed=best_offset;r.heldout_edge=std::abs(best_offset)>=1;
 return r;
}
void summarize(const Case& c,unsigned seeds,unsigned count) {
 unsigned detected=0,unc=0,narrow=0,accurate=0,both=0,cover=0,boundary=0,limited=0,edge=0;
 double worst=0,worst_audio=0,worst_held=0,max_cpu=0;std::vector<double> cpu;
 for(unsigned i=0;i<seeds;i++) {
    auto x=c;x.seed+=i*37;const auto r=run(x,count);
    detected+=r.valid;unc+=r.unc;boundary+=r.boundary;limited+=r.limited;accurate+=r.accurate;narrow+=r.narrow;
    both+=(r.narrow && r.covered && r.accurate);cover+=r.covered;edge+=r.heldout_edge;
    if(r.valid&&c.signal&&count==90){worst=std::max(worst,std::abs(r.error));worst_audio=std::max(worst_audio,std::abs(r.audio_error));worst_held=std::max(worst_held,std::abs(r.heldout_model));}
    cpu.push_back(r.cpu);max_cpu=std::max(max_cpu,r.cpu);
 }
 std::sort(cpu.begin(),cpu.end());
 std::cout<<std::setprecision(8)<<"eme source="<<(c.actual?"actual":"synthetic")<<" signal="<<c.signal
 <<" power_w="<<c.power_w<<" path_loss_db="<<c.loss_db<<" high_pilot_cn0_db_hz="<<high_cn0(c)
 <<" high_pilot_fraction="<<high_fraction()<<" seconds="<<count*.4<<" trials="<<seeds
 <<" detected="<<detected<<" boundary="<<boundary<<" work_limited="<<limited<<" uncertainty="<<unc
 <<" accurate_0p1="<<accurate<<" intervals_0p1="<<narrow<<" interval_covered="<<cover
 <<" narrow_accurate_covered="<<both;
 if(detected && c.signal && count==90)
    std::cout<<" max_abs_data_error_hz="<<worst<<" max_abs_audio_error_ppm="<<worst_audio
             <<" heldout_max_model_error_hz="<<worst_held<<" heldout_observed_search_edges="<<edge;
 else std::cout<<" max_abs_data_error_hz=unavailable max_abs_audio_error_ppm=unavailable"
              <<" heldout_max_model_error_hz=unavailable heldout_observed_search_edges=unavailable";
 std::cout
 <<" receiver_cpu_median_s="<<cpu[cpu.size()/2]<<" receiver_cpu_max_s="<<max_cpu<<'\n';
}

// The reduced receiver explicitly relies on external audio calibration. Its
// constant and linear RF models are different experiments; neither implies
// that a guessed residual evolution is safe for real lunar Doppler.
struct FrequencyRow {
 bool detected=false,unc=false,valid=false,accurate=false,covered=false,narrow=false,boundary=false,limited=false;
 bool conditional=false,conditional_narrow=false,conditional_covered=false;
 double error=std::numeric_limits<double>::quiet_NaN(),width=std::numeric_limits<double>::quiet_NaN();
 double conditional_width=std::numeric_limits<double>::quiet_NaN();
 double statistical=0,model_bound=0,cpu=0,holdout_error=std::numeric_limits<double>::quiet_NaN();
};
FrequencyRow run_frequency(const Case& c,unsigned count,bool linear,double assumed_bound) {
 if(!c.disciplined_audio)throw std::invalid_argument("reduced mode requires disciplined audio model");
 auto dwells=capture(c,count);
 h::HelperFrequencyConfig cfg;const double rf_prior=3*5868.4e6*rf_bound_ppm*1e-6;
 cfg.measurement={fs,550,2400,2400-rf_prior-.001,2400+rf_prior+.001,.0001,
    q_deg*std::numbers::pi/180/std::sqrt(rx_scale(c))};
 cfg.audio_center_ppm=0;cfg.reference_shift_hz=rf_hz;
 cfg.fit_frequency_rate=linear;cfg.frequency_rate_search_half_hz_per_second=.02;
 if(linear)cfg.maximum_residual_acceleration_hz_per_second2=assumed_bound;
 else cfg.maximum_residual_rate_hz_per_second=assumed_bound;
 FrequencyRow r;double start=now();h::HelperFrequencyMeasurement receiver(cfg);r.cpu+=now()-start;
 for(unsigned i=0;i<count;i++) {
    start=now();bool ok=receiver.observe(dwells[i].samples,dwells[i].rf,dwells[i].first);r.cpu+=now()-start;
    if(!ok)throw std::runtime_error("reduced frequency aperture rejected");
    start=now();(void)receiver.snapshot((i+1)*.4);r.cpu+=now()-start;
 }
 start=now();const auto s=receiver.snapshot(count*.4);r.cpu+=now()-start;
 r.detected=s.detected;r.unc=s.uncertainty_valid;r.boundary=s.boundary_limited;r.limited=s.work_limited;
 if(count<90 && r.detected)throw std::runtime_error("reduced mode admitted short observation");
 if(count<90 || !c.signal || !s.detected)return r;
 const auto& last=dwells[count-1];const double date=last.first+.18;
 start=now();const auto prediction=receiver.predict(1500,last.rf,date,count*.4);r.cpu+=now()-start;
 if(s.detected && !s.model_consistent && prediction.conditional_valid)
    throw std::runtime_error("contradicted model exposed conditional prediction");
 r.valid=prediction.valid;r.conditional=prediction.conditional_valid;
 if(!r.conditional)return r;
 const double t=date/rx_scale(c);
 const double truth=(1500*tx_scale(c)+last.rf*rf_ppm*1e-6+
    (last.rf/rf_hz)*(c.residual_rate_hz_per_second*t+.5*c.residual_acceleration_hz_per_second2*t*t))/rx_scale(c);
 r.error=prediction.frequency_hz-truth;r.width=prediction.half_width_hz;
 r.conditional_width=prediction.conditional_half_width_hz;
 r.statistical=prediction.statistical_3sigma_hz;r.model_bound=prediction.bounded_model_error_hz;
 r.accurate=std::abs(r.error)<=.1;
 r.conditional_covered=std::abs(r.error)<=r.conditional_width;r.conditional_narrow=r.conditional_width<=.1;
 r.covered=r.valid&&std::abs(r.error)<=r.width;r.narrow=r.valid&&r.width<=.1;
 const auto& held=dwells[count+4];const double ht=held.first+.18;
 start=now();const auto hp=receiver.predict(2400,held.rf,ht,count*.4);r.cpu+=now()-start;
 if(hp.conditional_valid){const double pt=ht/rx_scale(c);
    const double htruth=(2400*tx_scale(c)+held.rf*rf_ppm*1e-6+
       (held.rf/rf_hz)*(c.residual_rate_hz_per_second*pt+.5*c.residual_acceleration_hz_per_second2*pt*pt))/rx_scale(c);
    r.holdout_error=hp.frequency_hz-htruth;
 }
 return r;
}
void summarize_frequency(const Case& c,unsigned seeds,unsigned count,bool linear,double bound) {
 unsigned det=0,unc=0,valid=0,narrow=0,accurate=0,covered=0,both=0,miss=0,boundary=0,limited=0;
 unsigned conditional=0,cond_narrow=0,cond_covered=0,cond_miss=0;
 double worst=0,worstheld=0,minwidth=1e100,maxwidth=0,mincond=1e100,maxcond=0,maxstat=0,maxmodel=0;std::vector<double> cpu;
 for(unsigned i=0;i<seeds;i++) {
    auto x=c;x.seed+=i*37;const auto r=run_frequency(x,count,linear,bound);
    det+=r.detected;unc+=r.unc;valid+=r.valid;boundary+=r.boundary;limited+=r.limited;
    conditional+=r.conditional;cond_narrow+=r.conditional_narrow;cond_covered+=r.conditional_covered;
    cond_miss+=(r.conditional&&!r.conditional_covered);
    narrow+=r.narrow;accurate+=r.accurate;covered+=r.covered;both+=(r.narrow&&r.accurate&&r.covered);
    miss+=(r.valid&&!r.covered);
    if(r.conditional){worst=std::max(worst,std::abs(r.error));mincond=std::min(mincond,r.conditional_width);maxcond=std::max(maxcond,r.conditional_width);
       maxstat=std::max(maxstat,r.statistical);maxmodel=std::max(maxmodel,r.model_bound);
       if(std::isfinite(r.holdout_error))worstheld=std::max(worstheld,std::abs(r.holdout_error));}
    if(r.valid){minwidth=std::min(minwidth,r.width);maxwidth=std::max(maxwidth,r.width);}
    cpu.push_back(r.cpu);
 }
 std::sort(cpu.begin(),cpu.end());
 std::cout<<std::setprecision(8)<<"eme_reduced source="<<(c.actual?"actual":"synthetic")
   <<" signal="<<c.signal<<" mode="<<(linear?"linear":"constant")<<" power_w="<<c.power_w
   <<" path_loss_db="<<c.loss_db<<" noise_figure_db="<<c.noise_figure_db
   <<" nominal_pre_multipath_high_pilot_cn0_db_hz="<<high_cn0(c)
   <<" relative_audio_truth_ppm="<<(tx_scale(c)/rx_scale(c)-1)*1e6<<" relative_audio_bound_ppm=.0001"
   <<" true_residual_rate_hz_per_second="<<c.residual_rate_hz_per_second
   <<" true_residual_acceleration_hz_s2="<<c.residual_acceleration_hz_per_second2
   <<" second_path_amplitude="<<c.multipath_amplitude<<" second_path_doppler_hz="<<c.multipath_doppler_hz
   <<" direct_pilot_cn0_db_hz="<<(high_cn0(c)-10*std::log10(1+c.multipath_amplitude*c.multipath_amplitude))
   <<(linear?" assumed_acceleration_bound_hz_s2=":" assumed_rate_bound_hz_s=")<<bound
   <<" seconds="<<count*.4<<" trials="<<seeds<<" detected="<<det<<" boundary="<<boundary
   <<" work_limited="<<limited<<" uncertainty="<<unc<<" valid_prediction="<<valid
   <<" conditional_valid="<<conditional<<" conditional_intervals_0p1="<<cond_narrow
   <<" conditional_interval_covered="<<cond_covered<<" conditional_interval_misses="<<cond_miss
   <<" accurate_0p1="<<accurate<<" intervals_0p1="<<narrow<<" interval_covered="<<covered
   <<" narrow_accurate_covered="<<both<<" interval_misses="<<miss;
 if(conditional)std::cout<<" conditional_min_half_hz="<<mincond<<" conditional_max_half_hz="<<maxcond
    <<" max_statistical_3sigma_hz="<<maxstat<<" max_model_bound_hz="<<maxmodel
    <<" max_abs_data_error_hz="<<worst<<" heldout_max_model_error_hz="<<worstheld;
 else std::cout<<" conditional_min_half_hz=unavailable conditional_max_half_hz=unavailable max_abs_data_error_hz=unavailable";
 if(valid)std::cout<<" guarded_min_half_hz="<<minwidth<<" guarded_max_half_hz="<<maxwidth;
 else std::cout<<" guarded_min_half_hz=unavailable guarded_max_half_hz=unavailable";
 std::cout<<" receiver_cpu_median_s="<<cpu[cpu.size()/2]<<" receiver_cpu_max_s="<<cpu.back()<<'\n';
}
void frequency_regression() {
 // Small deterministic check, not a calibrated false alarm or coverage test.
 for(unsigned i=0;i<2;i++) {
    Case noise{15000.,220.,1901+i*37,true,false,true};
    const auto short_case=run_frequency(noise,2,false,.0005);
    const auto empty=run_frequency(noise,90,false,.0005);
    if(short_case.detected || empty.detected)throw std::runtime_error("reduced mode admitted short/noise-only input");
    Case clean{15000.,220.,1901+i*37,true,true,true};
    const auto fit=run_frequency(clean,90,false,.0005);
    if(!fit.detected || !fit.conditional || !fit.valid || !fit.covered ||
       fit.width<fit.conditional_width || !std::isfinite(fit.error))
       throw std::runtime_error("clean EME frequency guard failed");
 }
 unsigned curved_valid=0;
 for(unsigned i=0;i<8;i++) {
    Case accelerated{15000.,218.,2801+i*37,true,true,true,0,.0007};
    const auto fit=run_frequency(accelerated,90,false,.0005);
    curved_valid+=fit.valid;
    if(fit.valid && (!fit.covered || (fit.narrow && !fit.accurate)))
       throw std::runtime_error("EME curvature escaped guarded precision");
 }
 if(!curved_valid)throw std::runtime_error("curvature guard test was vacuously all invalid");
 std::cout<<"EME reduced 36s clean/noise/curvature guard regression PASS\n";
}
}
int main(int argc,char** argv) {
 try {
    const std::string mode=argc>1?argv[1]:"--probe";
    if(mode=="--probe") {
       for(double w:{15000.,1500.})for(bool signal:{false,true})summarize({w,220,501,true,signal},1,2);
    } else if(mode=="--sweep") {
       // Same absolute received C/N0 at losses separated by 10 dB; both total
       // powers are explicitly tested, without treating pilot watts as total.
       for(bool actual:{false,true})for(double w:{15000.,1500.}) {
          const double adjustment=w==15000?0:-10;
          for(double base:{220.,215.,210.,205.,203.,202.,201.,200.})
             summarize({w,base+adjustment,801,actual,true},4,90);
       }
       // The exact user-requested 1.5kW/220dB point is below the equivalent
       // 15kW grid and is run explicitly.
       for(bool actual:{false,true})summarize({1500.,220.,1001,actual,true},4,90);
       for(bool actual:{false,true})summarize({15000.,203.,1401,actual,false},4,90);
    } else if(mode=="--frequency-probe") {
       for(double w:{15000.,1500.})for(bool signal:{false,true})
          summarize_frequency({w,220,1901,true,signal,true},1,2,false,.0005);
    } else if(mode=="--frequency-edge") {
       for(bool linear:{false,true})for(bool actual:{false,true})for(double w:{15000.,1500.})
          summarize_frequency({w,220,1901,actual,true,true},8,90,linear,linear?.00005:.0005);
    } else if(mode=="--frequency-transition") {
       for(bool actual:{false,true})for(double w:{15000.,1500.}) {
          const double adjustment=w==15000?0:-10;
          for(double loss:{206.,205.5,205.,204.5,204.})
             summarize_frequency({w,loss+adjustment,2001,actual,true,true},4,90,false,.0005);
       }
    } else if(mode=="--frequency-sweep" || mode=="--frequency-linear") {
       const bool linear=mode=="--frequency-linear";
       const double bound=linear?.00005:.0005;
       for(bool actual:{false,true})for(double w:{15000.,1500.}) {
          const double adjustment=w==15000?0:-10;
          for(double loss:{220.,219.5,219.,218.5,218.,217.,216.,214.,212.,210.,208.,206.,204.,202.,200.})
             summarize_frequency({w,loss+adjustment,2001,actual,true,true},4,90,linear,bound);
       }
       for(bool actual:{false,true})summarize_frequency({1500.,220.,2401,actual,true,true},4,90,linear,bound);
       for(bool actual:{false,true})summarize_frequency({15000.,220.,2501,actual,false,true},4,90,linear,bound);
    } else if(mode=="--frequency-drift") {
       for(bool linear:{false,true}) {
          for(double rate:{0.,.0005,.005})
             summarize_frequency({15000.,218.,2701,true,true,true,rate},8,90,linear,linear?.00005:.0005);
          for(double acceleration:{.00005,.0007})
             summarize_frequency({15000.,218.,2801,true,true,true,0,acceleration},8,90,linear,linear?.00005:.0005);
       }
    } else if(mode=="--frequency-multipath") {
       for(bool linear:{false,true})for(double doppler:{0.,.1,.25,.5})
          summarize_frequency({15000.,218.,2901,true,true,true,0,0,.5,doppler},8,90,linear,linear?.00005:.0005);
    } else if(mode=="--noise-figure") {
       for(bool linear:{false,true})for(double nf:{0.,3.,10.}) {
          Case c{1500.,220.,3101,true,true,true};c.noise_figure_db=nf;
          summarize_frequency(c,8,90,linear,linear?.00005:.0005);
       }
       for(bool linear:{false,true}) {
          Case c{15000.,220.,3201,true,true,true};c.noise_figure_db=3;
          summarize_frequency(c,8,90,linear,linear?.00005:.0005);
       }
    } else if(mode=="--frequency-regression") {
       frequency_regression();
    } else throw std::invalid_argument("unknown EME study mode");
    return 0;
 }catch(const std::exception& e){std::cerr<<"EME study: "<<e.what()<<'\n';return 1;}
}
