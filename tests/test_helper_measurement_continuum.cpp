#include "datapump/helper_carrier.hpp"
#include "datapump/helper_measurement.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <ctime>
#include <vector>

// Independent physical-time simulation of actual Helper waveforms and a
// cheap synthetic reference. Only commanded hops and declared prior reach the
// receiver. Truth is retained separately for the evaluation after snapshot.
namespace {
namespace h = datapump::helper;
namespace hops = helper_measurement_hops;
constexpr double fs=24000, tau=2*std::numbers::pi, rx_ppm=-13, tx_ppm=37;
constexpr double rx=1+rx_ppm*1e-6;
struct Scenario {
    double cn0_db_hz=20, rf_ppm=.00008, rf_center_hz=5.8e9, prior_ppm=.0001, q_degrees=.005;
    bool hopping=true, actual=false, signal=true, strong_path=false;
    double audio_acceleration_ppm_s2=0, rf_wander_hz=0, rf_acceleration_hz_s2=0, timing_jitter_s=0;
    double fading_db=0, declared_q_degrees=.005;
    double prior_halfwidth_hz=0, audio_bound_ppm=300, max_audio_drift=1, max_shift_drift=.01;
    unsigned missing_dwells=0; bool variable_aperture=false, dispersed_missing=false;
    unsigned seed=0;
    const hops::Profile* schedule=nullptr;
};
struct Dwell {std::vector<std::complex<float>> samples;double first=0, actual_first=0,rf=0, extra_hz=0;};
class Source {
public:
    explicit Source(const Scenario& c):c_(c),rng_(c.seed),generator_(make_generator()) {
        normal_sigma_=std::sqrt(fs*rx/(2*std::pow(10.,c_.cn0_db_hz/10)));
    }
    double rf(unsigned i) const {return c_.hopping?c_.schedule->frequency(i):c_.rf_center_hz;}
    Dwell next(unsigned i) {
        if(i!=next_++)throw std::logic_error("out-of-order simulation");
        std::normal_distribution<double> norm;
        std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
        const double dt=.4/rx, front=.02/rx, r=rf(i);
        const double offset=r*c_.rf_ppm*1e-6;
        const double wander=c_.rf_wander_hz*std::sin(.31*i);
        if(c_.hopping && i)rf_phase_+=uniform(rng_);
        const double q=c_.q_degrees*std::numbers::pi/180;
        brown_+=q*std::sqrt(front)*norm(rng_);
        const double phase_step=q/std::sqrt(fs*rx);
        Dwell d;d.first=i*.4+.02;d.actual_first=d.first;d.rf=r;d.extra_hz=wander;
        // Generate the full .36s first; a variable aperture discards samples at
        // its end without changing the shared physical phase trajectory.
        d.samples.resize(8640);
        const double t0=i*.4/rx;
        const double gain=(c_.fading_db!=0 && i%17==3)?std::pow(10.,-c_.fading_db/20):1;
        for(std::size_t k=0;k<d.samples.size();++k) {
            const double nominal=d.first+static_cast<double>(k)/fs;
            const double t=nominal/rx;
            const double tx_time=t*(1+tx_ppm*1e-6)+c_.audio_acceleration_ppm_s2*1e-6*t*t*t/6;
            const double lo=rf_phase_+tau*((offset+wander)*(t-t0)+
                c_.rf_acceleration_hz_s2*(std::pow(t-18.,3)-std::pow(t0-18.,3))/6)+brown_;
            std::complex<double> sig{};
            if(c_.signal) {
                if(c_.actual) sig=generator_.mix_analytic({},tx_time,static_cast<std::uint32_t>(fs))/(.4*h::pilot_amplitude_fraction);
                else {
                    sig=std::polar(1.,tau*2400*tx_time+.71)+std::polar(std::pow(10.,-3./20),tau*550*tx_time-.42);
                    // Independent auxiliary interference, not a decoded payload.
                    const double local=.02+static_cast<double>(k)/fs;
                    const double rev=local<.16?1.:-1.;
                    const double mod=local<.212?1.:((i+static_cast<unsigned>((local-.212)/.02))%2?-.75:1.);
                    sig+=rev*mod*std::polar(std::pow(10.,-9./20),tau*950*tx_time+.2);
                }
                sig*=gain*std::polar(1.,lo);
            }
            d.samples[k]={static_cast<float>(sig.real()+normal_sigma_*norm(rng_)),static_cast<float>(sig.imag()+normal_sigma_*norm(rng_))};
            brown_+=phase_step*norm(rng_);
        }
        brown_+=q*std::sqrt(front)*norm(rng_);
        rf_phase_+=tau*((offset+wander)*dt+c_.rf_acceleration_hz_s2*
            (std::pow(t0+dt-18.,3)-std::pow(t0-18.,3))/6);
        if(c_.variable_aperture) d.samples.resize(6720+(i%5)*480);
        // The reported timestamp can have an unknown retune/timing error.
        d.first+=c_.timing_jitter_s*std::sin(.71*i);
        return d;
    }
    double truth_frequency(double nominal,double rf,double time,double extra) const {
        const double t=time/rx;
        const double tx_rate=1+tx_ppm*1e-6+.5*c_.audio_acceleration_ppm_s2*1e-6*t*t;
        return (nominal*tx_rate+rf*c_.rf_ppm*1e-6+extra+
            .5*c_.rf_acceleration_hz_s2*(t-18.)*(t-18.))/rx;
    }
private:
    h::Generator make_generator() {
        h::Options options;options.power_w=15000;options.data_power_w=1;options.callsign="N0CALL";
        h::Plan plan{2400,.4,0,15000,950,550,c_.hopping};
        return h::Generator(options,plan,1800000000,true);
    }
    Scenario c_;std::mt19937_64 rng_;h::Generator generator_;
    double normal_sigma_=0,rf_phase_=.37,brown_=0;unsigned next_=0;
};
// One receiver and one simulation thread in this process: std::clock counts
// process CPU across constructor, observe and snapshot; generation and truth
// evaluation occur outside the measured intervals. No wall time is substituted.
double cpu_now() {
    const auto t=std::clock();
    if(t==static_cast<std::clock_t>(-1))throw std::runtime_error("process CPU clock unavailable");
    return static_cast<double>(t)/static_cast<double>(CLOCKS_PER_SEC);
}
struct Result {
    bool detected=false,uncertainty=false,boundary=false,stored_all=true,polling_ok=true,model_rejected=false,work_limited=false;
    double error=std::numeric_limits<double>::quiet_NaN(),half=std::numeric_limits<double>::quiet_NaN();
    double cpu=0,sampled_seconds=0,modeled_seconds=0,largest_gap_seconds=0,heldout_observed=0;
    std::size_t projected_sample_products=0;bool heldout_edge=false;
    double heldout_error=std::numeric_limits<double>::quiet_NaN(),audio_error_ppm=std::numeric_limits<double>::quiet_NaN();
};
Result trial(const Scenario& c,unsigned count=90) {
    Source source(c);std::vector<Dwell> input;input.reserve(count+1);
    for(unsigned i=0;i<=count;++i)input.push_back(source.next(i));
    h::HelperPooledConfig cfg;
    // The frequency window is derived only from the declared 0.0001ppm RF and
    // 100ppm audio priors and maximum RF channel; it never uses signal truth.
    const double maxrf=*std::max_element(c.schedule->centers_hz.begin(),c.schedule->centers_hz.end());
    const double width=c.prior_halfwidth_hz>0 ? c.prior_halfwidth_hz : 3*(maxrf*c.prior_ppm+2400*100)*1e-6;
    cfg.measurement={fs,550,2400,2400-width,2400+width,c.audio_bound_ppm,c.declared_q_degrees*std::numbers::pi/180/std::sqrt(rx)};
    cfg.max_audio_drift_ppm_per_second=c.max_audio_drift;cfg.max_shift_drift_hz_per_second=c.max_shift_drift;
    Result out;double t=cpu_now();
    std::optional<h::HelperPooledMeasurement> pooled;
    std::optional<h::HelperMeasurement> strong;
    if(c.strong_path)strong.emplace(cfg.measurement);else pooled.emplace(cfg);
    out.cpu+=cpu_now()-t;
    for(unsigned i=0;i<count;++i) {
        const bool missing=c.missing_dwells && (c.dispersed_missing ?
            (i>=3 && (i-3)%8==0 && (i-3)/8<c.missing_dwells):
            (i>=20 && i<20+c.missing_dwells));
        if(!missing) {
            t=cpu_now();
            const bool admitted=c.strong_path?strong->observe(input[i].samples,input[i].rf,input[i].first).valid:
                pooled->observe(input[i].samples,input[i].rf,input[i].first);
            out.cpu+=cpu_now()-t;
            out.stored_all &= admitted;
        }
        // Model the ordinary 400ms polling cadence rather than timing only a
        // single final snapshot. A missing dwell still advances the timeline.
        if(i+1<count) {
            t=cpu_now();
            if(c.strong_path)(void)strong->snapshot();else (void)pooled->snapshot((i+1)*.4);
            out.cpu+=cpu_now()-t;
        }
    }
    t=cpu_now();h::HelperPooledSnapshot s;
    if(c.strong_path) {
        s.clock=strong->snapshot();s.valid=s.clock.valid;s.uncertainty_valid=s.clock.valid;
        double previous_end=0;
        for(unsigned i=0;i<count;++i) {
            const bool missing=c.missing_dwells && (c.dispersed_missing ?
                (i>=3 && (i-3)%8==0 && (i-3)/8<c.missing_dwells):
                (i>=20 && i<20+c.missing_dwells));
            if(missing)continue;
            s.sampled_seconds+=static_cast<double>(input[i].samples.size())/fs;
            s.largest_unobserved_seconds=std::max(s.largest_unobserved_seconds,input[i].first-previous_end);
            previous_end=input[i].first+static_cast<double>(input[i].samples.size())/fs;
        }
        s.largest_unobserved_seconds=std::max(s.largest_unobserved_seconds,count*.4-previous_end);
        s.modeled_seconds=s.sampled_seconds;
    }
    else {
        s=pooled->snapshot(count*.4);
        if(count==90) {
            for(double d:{.1,.2,.3}) {
                const auto later=pooled->snapshot(36.+d);
                const auto& last=input[count-1];
                const double gap=std::max(s.largest_unobserved_seconds,
                    36.+d-(last.first+static_cast<double>(last.samples.size())/fs));
                const bool enough=s.sampled_seconds+1e-8>=cfg.minimum_sampled_fraction*(36.+d) &&
                    gap<=cfg.maximum_unobserved_seconds;
                const bool expected_valid=s.valid && enough;
                out.polling_ok &= later.valid==expected_valid &&
                    later.uncertainty_valid==(expected_valid && s.uncertainty_valid);
                if(expected_valid && later.valid)
                    out.polling_ok &= std::abs(later.clock.audio_ppm-s.clock.audio_ppm)<1e-9 &&
                                      std::abs(later.clock.shift_ppm-s.clock.shift_ppm)<1e-9;
            }
            const auto stale=pooled->snapshot(36.9);
            const auto early=pooled->snapshot(35.99);
            out.polling_ok &= !stale.valid && !early.valid;
        }
    }
    out.cpu+=cpu_now()-t;
    out.detected=s.valid;out.uncertainty=s.uncertainty_valid;out.boundary=s.boundary_limited;
    out.model_rejected=s.clock.dwell_count>=3 && !s.clock.model_consistent;
    out.work_limited=s.work_limited;out.projected_sample_products=s.projected_sample_products;
    out.sampled_seconds=s.sampled_seconds;out.modeled_seconds=s.modeled_seconds;out.largest_gap_seconds=s.largest_unobserved_seconds;
    if(!s.valid)return out;
    const auto& state=s.clock;const auto& last=input[count-1];const double date=state.time_seconds;
    const double modeled=1500*(1+state.audio_ppm*1e-6)+last.rf*state.shift_ppm*1e-6;
    const double last_true_time=last.actual_first+static_cast<double>(last.samples.size())/(2*fs);
    out.error=modeled-source.truth_frequency(1500,last.rf,last_true_time,last.extra_hz);
    const double tr=(1+tx_ppm*1e-6+.5*c.audio_acceleration_ppm_s2*1e-6*std::pow(date/rx,2))/rx;
    out.audio_error_ppm=state.audio_ppm-(tr-1)*1e6;
    if(s.uncertainty_valid) {
        const double v[]{1500e-6,last.rf*1e-6,0,0};double variance=0;
        for(int i=0;i<4;i++)for(int j=0;j<4;j++)variance+=v[i]*state.covariance[i][j]*v[j];
        if(variance>=0 && std::isfinite(variance))out.half=3*std::sqrt(variance);
    }
    const auto& held=input[count];const double ht=held.first+static_cast<double>(held.samples.size())/(2*fs),dt=ht-date;
    const double predicted=2400*(1+(state.audio_ppm+dt*state.audio_drift_ppm_per_second)*1e-6)+held.rf*(state.shift_ppm+dt*state.shift_drift_ppm_per_second)*1e-6;
    out.heldout_error=predicted-source.truth_frequency(2400,held.rf,
        held.actual_first+static_cast<double>(held.samples.size())/(2*fs),held.extra_hz);
    // Independent, truth-blind one-dwell high-pilot diagnostic. This noisy
    // 0.1Hz-grid peak is not a ±0.1Hz confidence interval; an endpoint peak
    // is explicitly marked unresolved. It is evaluation, not receiver CPU.
    double best=-1;int best_k=0;
    for(int k=-10;k<=10;++k) {
        const double f=predicted+.1*k;
        const auto step=std::polar(1.,-tau*f/fs);std::complex<double> sum{},rot{1,0};
        for(const auto sample:held.samples) {sum+=std::complex<double>(sample)*rot;rot*=step;}
        if(std::norm(sum)>best) {best=std::norm(sum);best_k=k;}
    }
    out.heldout_observed=.1*best_k;out.heldout_edge=std::abs(best_k)==10;
    return out;
}
void print_results(const std::string& label,double cn0,const std::vector<Result>& results) {
    unsigned det=0,unc=0,bound=0,accurate=0,narrow=0,covered=0,joint=0,false_precision=0,storage=0,heldout_edges=0,poll_fail=0,model_rejected=0,work_limited=0;std::size_t max_products=0;double worst=0,held=0,held_obs=0,audio=0,min_sampled=1e9,min_modeled=1e9,max_gap=0,min_half=1e9,max_half=0;
    std::vector<double> cpus;for(const auto& r:results) {
        cpus.push_back(r.cpu);storage+=r.stored_all;poll_fail+=!r.polling_ok;model_rejected+=r.model_rejected;work_limited+=r.work_limited;
        max_products=std::max(max_products,r.projected_sample_products);
        min_sampled=std::min(min_sampled,r.sampled_seconds);min_modeled=std::min(min_modeled,r.modeled_seconds);max_gap=std::max(max_gap,r.largest_gap_seconds);det+=r.detected;unc+=r.uncertainty;bound+=r.boundary;
        if(r.detected) {accurate+=std::abs(r.error)<=.1;worst=std::max(worst,std::abs(r.error));held=std::max(held,std::abs(r.heldout_error));audio=std::max(audio,std::abs(r.audio_error_ppm));held_obs=std::max(held_obs,std::abs(r.heldout_observed));heldout_edges+=r.heldout_edge;}
        if(r.uncertainty && std::isfinite(r.half)) {min_half=std::min(min_half,r.half);max_half=std::max(max_half,r.half);narrow+=r.half<=.1;covered+=std::abs(r.error)<=r.half;joint+=(r.half<=.1 && std::abs(r.error)<=.1);false_precision+=(r.half<=.1 && std::abs(r.error)>.1);}
    }
    std::sort(cpus.begin(),cpus.end());
    std::cout<<std::setprecision(7)<<"continuum profile="<<label<<" cn0_db_hz="<<cn0<<" trials="<<results.size()
             <<" stored="<<storage<<" detected="<<det<<" boundary="<<bound<<" uncertainty="<<unc<<" model_rejected="<<model_rejected
             <<" work_limited="<<work_limited<<" max_projected_sample_products="<<max_products
             <<" abs_error_le_0.1="<<accurate<<" halfwidth_le_0.1="<<narrow<<" coverage="<<covered<<"/"<<unc
             <<" narrow_and_accurate="<<joint<<" false_0.1hz_precision="<<false_precision
             <<" min_3sigma_hz="<<(unc?std::to_string(min_half):std::string("unavailable"))
             <<" max_3sigma_hz="<<(unc?std::to_string(max_half):std::string("unavailable"))
             <<" pilot_snr_2500hz_db="<<(cn0-10*std::log10(2500.))
             <<" min_raw_sampled_s="<<min_sampled<<" min_modeled_s="<<min_modeled<<" max_unobserved_gap_s="<<max_gap
             <<" max_abs_data_error_hz="<<(det?std::to_string(worst):std::string("unavailable"))
             <<" max_heldout_model_error_hz="<<(det?std::to_string(held):std::string("unavailable"))
             <<" max_abs_audio_error_ppm="<<(det?std::to_string(audio):std::string("unavailable"))
             <<" heldout_observed_peak_max_hz="<<(det?std::to_string(held_obs):std::string("unavailable"))
             <<" heldout_search_edge="<<heldout_edges<<" polling_failures="<<poll_fail
             <<" receiver_cpu_median_s="<<cpus[cpus.size()/2]<<" receiver_cpu_max_s="<<cpus.back()<<'\n';
}
void probe() {
    const auto schedule=hops::fcc100();Scenario c;c.schedule=&schedule;c.seed=1001;
    for(bool signal:{false,true}) {c.signal=signal;const auto r=trial(c,2);
        std::cout<<"short_probe signal="<<signal<<" detected="<<r.detected<<" stored="<<r.stored_all<<" cpu_s="<<r.cpu<<'\n';
        if(r.detected || !r.stored_all)throw std::runtime_error("short observation incorrectly acquired or rejected sample geometry");}
}
void sweep(const std::string& mode) {
    // One-dB grid around the candidate .1Hz region plus both extreme ends.
    const std::vector<double> levels{5,10,14,17,18,19,20,21,22,23,24,25,26,27,28,30,31,32,33,34,35,36,38,40,45};
    const unsigned seeds=mode=="--smoke"?2:8;
    std::vector<hops::Profile> fcc,eu,hf;
    for(unsigned i=0;i<seeds;++i){fcc.push_back(hops::fcc100(i));eu.push_back(hops::eu20(i));hf.push_back(hops::hf50(i));}
    const auto execute=[&](const std::vector<hops::Profile>& schedules,const std::string& label,bool hopping,bool actual,const std::vector<double>& grid,bool strong=false,bool hf_case=false) {
        for(double cn0:grid) {
            std::vector<Result> rows;rows.reserve(seeds);
            for(unsigned seed=0;seed<seeds;++seed) {
                Scenario c;c.schedule=&schedules[seed];c.cn0_db_hz=cn0;c.hopping=hopping;c.actual=actual;c.strong_path=strong;c.seed=5101+seed*37;
                if(hf_case){c.rf_center_hz=30e6;c.rf_ppm=.8;c.prior_ppm=1;c.q_degrees=.05;c.declared_q_degrees=.05;}
                rows.push_back(trial(c));
            }
            print_results(label,cn0,rows);
            if(mode=="--smoke" && (cn0==30 || cn0==45))for(const auto& r:rows)
                if(!r.stored_all || !r.detected || !r.uncertainty || !r.polling_ok ||
                   std::abs(r.error)>.1 || std::abs(r.error)>r.half || (cn0==45 && r.half>.1))
                    throw std::runtime_error("strong deterministic continuum control failed");
        }
    };
    if(mode=="--smoke") {
        execute(fcc,"fcc100_hopped_synthetic",true,false,{10,20,30,45});
        for(unsigned i=0;i<2;++i) {
            Scenario c;c.schedule=&fcc[i];c.signal=false;c.seed=9101+i*37;const auto r=trial(c);
            if(!r.stored_all || r.detected || !r.polling_ok)throw std::runtime_error("noise-only continuum control failed");
        }
    }
    else if(mode=="--strong-cost") {
        execute(fcc,"fcc_hopped_strong_path",true,false,{20,24,28,30,35},true);
        execute(hf,"hf_50x50k_hopped_strong_path",true,false,{25,30,35},true,true);
    }
    else {
        execute(fcc,"fcc100_hopped_synthetic",true,false,levels);
        execute(fcc,"fixed_synthetic",false,false,{10,15,20,21,22,23,24,25,26,27,28,30,31,32,33,34,35,36,38,40,45});
        execute(eu,"eu20_hopped_synthetic",true,false,{10,15,20,21,22,23,24,25,26,27,28,30,31,32,33,34,35,36,38,40,45});
        execute(fcc,"fcc_hopped_actual",true,true,{10,15,20,22,24,26,28,30,31,32,33,34,35,36,38,40,45});
        execute(fcc,"fcc_hopped_strong_path",true,false,{20,24,28,30,35},true);
        execute(hf,"hf_50x50k_hopped_strong_path",true,false,{25,30,35},true,true);
        for(bool hop:{false,true}) {
            std::vector<Result> controls;
            for(unsigned i=0;i<seeds;++i){Scenario c;c.schedule=&fcc[i];c.signal=false;c.hopping=hop;c.seed=9101+i*37;controls.push_back(trial(c));}
            print_results(hop?"fcc100_noise_only":"fixed_noise_only",20,controls);
        }
    }
}
void wide_cost() {
    const auto schedule=hops::fcc100(17);
    Scenario a;a.schedule=&schedule;a.seed=6101;a.cn0_db_hz=30;a.prior_halfwidth_hz=32;
    auto normal=trial(a);print_results("wide64_default_drift",30,{normal});
    if(!normal.stored_all || !normal.detected || normal.work_limited || !normal.polling_ok)
        throw std::runtime_error("supported 64Hz prior was improperly declined");
    Scenario b=a;b.seed=6102;b.audio_bound_ppm=1000;b.max_shift_drift=.314;
    auto extreme=trial(b);print_results("wide64_near_grid_cap",30,{extreme});
    if(!extreme.stored_all || extreme.detected || !extreme.work_limited || !extreme.polling_ok)
        throw std::runtime_error("near-cap workload was not explicitly declined");
    std::cout<<"wide_cost default_audio_prior_ppm=300 default_shift_drift_hz_s=.01"
             <<" extreme_audio_prior_ppm=1000 extreme_shift_drift_hz_s=.314"
             <<" max_audio_drift_ppm_s=1 grid_cap=100000; timed full receiver, including repeated polls\n";
}
void robustness_regression() {
    // Independent reproduction of the centered-q=0.0007 Hz/s² false-precision
    // case that passed the old global linear fit. The guard must either widen
    // or withdraw an estimate; evidence-only boundary fits are not precision.
    for(bool hf:{false,true}) {
        unsigned reported=0,detected=0;
        for(unsigned seed=0;seed<(hf?4U:8U);++seed) {
            const auto schedule=hf?hops::hf50(seed):hops::fcc100(seed);
            Scenario c;c.schedule=&schedule;c.rf_acceleration_hz_s2=.0014;
            c.seed=(hf?8101:7101)+seed*37;c.cn0_db_hz=hf?30:24;
            if(hf){c.rf_center_hz=30e6;c.rf_ppm=.8;c.prior_ppm=1;c.q_degrees=.05;
                   c.declared_q_degrees=.05;c.strong_path=true;}
            const auto r=trial(c);detected+=r.detected;
            if(r.uncertainty) {
                ++reported;
                if(!std::isfinite(r.half) || std::abs(r.error)>r.half ||
                   (r.half<=.1 && std::abs(r.error)>.1))
                    throw std::runtime_error("curvature produced false precision");
            }
        }
        std::cout<<"robustness centered_curvature hf="<<hf<<" detected="<<detected
                 <<" covariance_available="<<reported<<'\n';
        if(!detected || !reported)throw std::runtime_error("curvature check had no usable frequency evidence");
    }
    const auto schedule=hops::fcc100(0);
    for(bool dispersed:{false,true}) {
        Scenario c;c.schedule=&schedule;c.cn0_db_hz=24;c.seed=7101;
        c.missing_dwells=5;c.dispersed_missing=dispersed;
        const auto r=trial(c);
        std::cout<<"robustness five_missing dispersed="<<dispersed<<" detected="<<r.detected<<'\n';
        if(!r.stored_all || !r.polling_ok || r.detected!=dispersed)
            throw std::runtime_error("missing-dwell support coverage failure");
    }
}
void mismatches() {
    std::vector<hops::Profile> schedules;for(unsigned i=0;i<8;++i)schedules.push_back(hops::fcc100(i));
    struct Variant {const char* name;double audio_accel,rf_accel,wander,jitter,fade,q;unsigned missing;bool variable;bool dispersed=false;};
    const Variant variants[]={{"baseline",0,0,0,0,0,.005,0,false},
        {"audio_accel",.02,0,0,0,0,.005,0,false},{"rf_curvature",0,.0014,0,0,0,.005,0,false},
        {"hop_wander",0,0,.1,0,0,.005,0,false},{"timing_error",0,0,0,.003,0,.005,0,false},
        {"periodic_fade",0,0,0,0,12,.005,0,false},{"phase_noise_x10",0,0,0,0,0,.05,0,false},
        {"variable_aperture",0,0,0,0,0,.005,0,true},{"missing_1",0,0,0,0,0,.005,1,false},
        {"missing_5",0,0,0,0,0,.005,5,false},{"missing_10",0,0,0,0,0,.005,10,false},
        {"missing_1_dispersed",0,0,0,0,0,.005,1,false,true},
        {"missing_5_dispersed",0,0,0,0,0,.005,5,false,true},
        {"missing_10_dispersed",0,0,0,0,0,.005,10,false,true}};
    for(const auto& v:variants) {
        std::vector<Result> rows;
        for(unsigned seed=0;seed<8;++seed){Scenario c;c.schedule=&schedules[seed];c.cn0_db_hz=24;c.seed=7101+seed*37;
            c.audio_acceleration_ppm_s2=v.audio_accel;c.rf_acceleration_hz_s2=v.rf_accel;
            c.rf_wander_hz=v.wander;c.timing_jitter_s=v.jitter;c.fading_db=v.fade;
            c.q_degrees=v.q;c.missing_dwells=v.missing;c.variable_aperture=v.variable;
            c.dispersed_missing=v.dispersed;rows.push_back(trial(c));}
        print_results(v.name,24,rows);
    }
    // Independent 30MHz wide-prior strong-path sensitivity to the same
    // departures. Fixed geometry is a simulation, not HF authorization.
    std::vector<hops::Profile> hf;for(unsigned i=0;i<4;++i)hf.push_back(hops::hf50(i));
    for(const auto& v:variants) {
        const std::string name=v.name;
        if(name!="baseline" && name!="rf_curvature" && name!="hop_wander" &&
           name!="periodic_fade" && name!="missing_1" && name!="missing_5")continue;
        std::vector<Result> rows;
        for(unsigned seed=0;seed<4;++seed) {
            Scenario c;c.schedule=&hf[seed];c.rf_center_hz=30e6;c.rf_ppm=.8;c.prior_ppm=1;
            c.cn0_db_hz=30;c.seed=8101+seed*37;c.q_degrees=.05;c.declared_q_degrees=.05;
            c.strong_path=true;c.rf_acceleration_hz_s2=v.rf_accel;c.rf_wander_hz=v.wander;
            c.fading_db=v.fade;c.missing_dwells=v.missing;rows.push_back(trial(c));
        }
        print_results("hf50_strong_"+name,30,rows);
    }
    std::vector<Result> hf_noise;
    for(unsigned seed=0;seed<4;++seed) {
        Scenario c;c.schedule=&hf[seed];c.rf_center_hz=30e6;c.rf_ppm=.8;c.prior_ppm=1;
        c.cn0_db_hz=30;c.seed=9901+seed*37;c.q_degrees=.05;c.declared_q_degrees=.05;
        c.strong_path=true;c.signal=false;hf_noise.push_back(trial(c));
    }
    print_results("hf50_strong_noise_only",30,hf_noise);
}
}
int main(int argc,char**argv) {
    try {
        const std::string mode=argc>1?argv[1]:"--smoke";
        if(mode=="--probe")probe();else if(mode=="--smoke") {probe();sweep(mode);}
        else if(mode=="--sweep" || mode=="--strong-cost")sweep(mode);
        else if(mode=="--mismatch")mismatches();else if(mode=="--wide-cost")wide_cost();
        else if(mode=="--robustness")robustness_regression();
        else throw std::invalid_argument("expected --probe, --smoke, --sweep, --strong-cost, --mismatch, --wide-cost or --robustness");
        return 0;
    }catch(const std::exception& e){std::cerr<<"Helper continuum: "<<e.what()<<'\n';return 1;}
}
