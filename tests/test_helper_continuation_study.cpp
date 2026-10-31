#include "datapump/helper_aided_phase.hpp"
#include "datapump/helper_joint_dwell.hpp"
#include "datapump/helper_soft_joint_dwell.hpp"
#include "datapump/helper_evolving_clock.hpp"
#include "datapump/helper_piecewise_dwell.hpp"
#include "datapump/helper_relative_clock.hpp"
#include "datapump/helper_joint_estimator.hpp"
#include "datapump/helper_dual_pilot_phase.hpp"
#include "datapump/helper_frequency_recovery.hpp"
#include "datapump/helper_stream_conditioner.hpp"
#include "helper_tracking_fixture.hpp"
#include "helper_transient_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace hp=helper_live_probe;
namespace ht=helper_tracking;
namespace h=datapump::helper;
using C=std::complex<double>;
using S=std::complex<float>;
constexpr double fs=hp::sample_rate,tau=hp::tau;
constexpr std::size_t settle=480,aperture=8640;
// Explicit bounded settling guard. Center phase does not qualify low-weight
// Hann edges that may still contain a retune transient.
constexpr std::size_t phase_margin=960;
// An isolated simulator-only transient control. The underlying actual Helper
// waveform, seeded noise and random hop schedule remain identical. A short
// common-Hz excursion has continuous integrated phase and then returns to its
// old frequency; it must not be mistaken for a persistent frequency epoch.
class ContinuationSource : public ht::Source {
public:
    ContinuationSource(hp::Config config,ht::Impairments impairment,bool pulse=false,bool scaled_step=false,
                       double scaled_step_begin=36.8,double audio_evolution=0,double audio_jump=0,bool second_step=false,unsigned erased_dwell=100,
                       std::string_view transient_profile={})
        :ht::Source(std::move(config),impairment),pulse_(pulse),scaled_step_(scaled_step),scaled_begin_(scaled_step_begin),
         audio_evolution_(audio_evolution),audio_jump_(audio_jump),second_step_(second_step),erased_dwell_(erased_dwell),
         scaled_hops_(scaled_step?make_hops(this->config()):helper_measurement_hops::Profile{}){
        if(!std::isfinite(scaled_begin_)||scaled_begin_<0)throw std::invalid_argument("invalid scaled oscillator step date");
        if(!transient_profile.empty())for(const unsigned dwell:{25U,50U,75U,95U})
            transients_.push_back(std::make_unique<helper_transient::Model>(this->config(),helper_transient::named(transient_profile,dwell)));
        if(audio_evolution_!=0||audio_jump_!=0||erased_dwell_!=100){
            h::Options options;options.power_w=this->config().helper_w;options.data_power_w=1;options.callsign=this->config().callsign;
            audio_generator_=std::make_unique<h::Generator>(options,h::Plan{2400,.4,0,this->config().helper_w,950,550,this->config().hopping},this->config().epoch,true);
        }
    }
    hp::Dwell generate(unsigned dwell){
        auto out=ht::Source::generate(dwell);
        if(audio_evolution_!=0||audio_jump_!=0)for(std::size_t i=0;i<out.samples.size();++i){
            const double time=out.first_seconds+static_cast<double>(i)/fs;
            const double old_time=ht::Source::tx_time(time),new_time=tx_time(time);
            const auto old_helper=audio_generator_->mix_analytic({},old_time,fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            const auto new_helper=audio_generator_->mix_analytic({},new_time,fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            const C common(out.common_phasor[i]);
            const C new_probe=base().probe_amplitude()*base().probe_baseband(new_time)*std::polar(1.,tau*std::remainder(1600*new_time,1.))*common;
            const C received=C(out.samples[i])+(new_helper-old_helper)*common+new_probe-C(out.clean_probe[i]);
            out.samples[i]={static_cast<float>(received.real()),static_cast<float>(received.imag())};
            out.clean_probe[i]={static_cast<float>(new_probe.real()),static_cast<float>(new_probe.imag())};
        }
        if(dwell==erased_dwell_)for(std::size_t i=0;i<out.samples.size();++i){
            const double time=out.first_seconds+static_cast<double>(i)/fs;
            const auto helper=audio_generator_->mix_analytic({},tx_time(time),fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            const C received=C(out.samples[i])-helper*C(out.common_phasor[i])-C(out.clean_probe[i]);
            out.samples[i]={static_cast<float>(received.real()),static_cast<float>(received.imag())};out.clean_probe[i]={};
        }
        if(pulse_||scaled_step_||second_step_)for(std::size_t i=0;i<out.samples.size();++i){
            const double time=out.first_seconds+static_cast<double>(i)/fs;
            double phase=0;
            if(pulse_){const double elapsed=std::clamp(time-pulse_begin_,0.,pulse_end_-pulse_begin_);phase+=tau*pulse_hz_*elapsed;}
            if(scaled_step_)phase+=advance_scaled_phase(time);
            if(second_step_)phase+=tau*1.2*std::max(0.,time-39.2);
            const C rotation=std::polar(1.,phase);
            const C noisy=C(out.samples[i])*rotation,clean=C(out.clean_probe[i])*rotation,common=C(out.common_phasor[i])*rotation;
            out.samples[i]={static_cast<float>(noisy.real()),static_cast<float>(noisy.imag())};
            out.clean_probe[i]={static_cast<float>(clean.real()),static_cast<float>(clean.imag())};
            out.common_phasor[i]={static_cast<float>(common.real()),static_cast<float>(common.imag())};
        }
        if(std::any_of(transients_.begin(),transients_.end(),[&](const auto& t){return t->applies(dwell);}))
            out.settled_common_phasor=out.common_phasor;
        for(const auto& transient:transients_)transient->apply(out,base(),dwell);
        return out;
    }
    double shift_error_hz(double rf,double time)const noexcept{
        const double scaled=scaled_step_&&time>=scaled_begin_?scaled_step_hz_*rf/config().shift_center_hz/base().rx_scale():0;
        double transient_hz=0;
        if(time>=0){const auto dwell=static_cast<unsigned>(time/hp::dwell_seconds);
            const double local=time-dwell*hp::dwell_seconds;
            for(const auto& transient:transients_)transient_hz+=transient->frequency_hz(dwell,local);}
        return ht::Source::shift_error_hz(rf,time)+(pulse_&&time>=pulse_begin_&&time<pulse_end_?pulse_hz_:0)+scaled+(second_step_&&time>=39.2?1.2:0)+transient_hz;
    }
    // Controlled independent baseband waveform-clock stress. RF keyed retune
    // dates retain the underlying clock calendar; audio evolution shifts the
    // actual Helper envelope/tones and probe together. No evaluator time enters
    // any receiver. This is not a model of an RF scheduler on the same clock.
    double tx_time(double time)const noexcept{const double dt=std::max(0.,time-28);return ht::Source::tx_time(time)+.5e-6*audio_evolution_*dt*dt+(time>=32?audio_jump_:0);}
    double audio_scale_at(double time)const noexcept{return ht::Source::audio_scale_at(time)+1e-6*audio_evolution_*std::max(0.,time-28);}
    double audio_evolution()const noexcept{return audio_evolution_;}
    double audio_jump()const noexcept{return audio_jump_;}
    bool has_scaled_step()const noexcept{return scaled_step_;}
    bool has_transients()const noexcept{return !transients_.empty();}
    // Evaluator-only input-time mask; receiver code never consumes it.
    bool physical_disturbance(long double input_sample,long double halo)const noexcept{
        if(input_sample+halo<0)return false;
        const double first=static_cast<double>((input_sample-halo)/fs);
        const double last=static_cast<double>((input_sample+halo)/fs);
        // Fixed bounded set of simulated transient dwells. This information is
        // used only after receiver processing to classify evaluator samples.
        for(unsigned dwell:{25U,50U,75U,95U})for(const auto& t:transients_)
            if(t->disturbed_during(dwell,first-dwell*hp::dwell_seconds,last-dwell*hp::dwell_seconds))return true;
        return false;
    }
    double scaled_step_begin_seconds()const noexcept{return scaled_begin_;}
    double scaled_step_reference_hz()const noexcept{return config().shift_center_hz;}
    double scaled_step_physical_hz()const noexcept{return scaled_step_hz_;}
private:
    static helper_measurement_hops::Profile make_hops(const hp::Config& c){
        if(!c.hopping)return {};
        if(c.shift_center_hz>1e9)return c.microwave_eu20?helper_measurement_hops::eu20(c.seed,c.expanded_schedule_key):
            helper_measurement_hops::fcc100(c.seed,c.expanded_schedule_key);
        return helper_measurement_hops::hf50(c.seed,c.expanded_schedule_key);
    }
    double physical_retune(int hop)const{
        const auto& c=config();const double boundary=static_cast<double>(hop)*hp::dwell_seconds;
        const double rate=1+c.tx_audio_ppm*1e-6;
        const double discriminant=rate*rate+2e-6*c.tx_audio_drift_ppm_per_second*boundary;
        if(!(discriminant>0))throw std::invalid_argument("invalid scaled-step TX retune geometry");
        return c.arrival_offset_seconds+(hop==0?0:2*boundary/(rate+std::sqrt(discriminant)));
    }
    double active_rf(int hop)const{
        return !config().hopping||hop<0?config().shift_center_hz:scaled_hops_.frequency(static_cast<unsigned>(hop));
    }
    double advance_scaled_phase(double nominal){
        const auto& c=config();const double physical=(nominal+c.fractional_input_sample/fs)/base().rx_scale();
        const double tx=tx_time(nominal);const int hop=tx<0?-1:std::min(99,static_cast<int>(std::floor(tx/hp::dwell_seconds)));
        const double begin=(scaled_begin_+c.fractional_input_sample/fs)/base().rx_scale();
        const auto integrate=[&](double rf,double from,double to){
            const double left=std::max(from,begin);if(to>left)scaled_phase_+=tau*(scaled_step_hz_/c.shift_center_hz)*rf*(to-left);
        };
        if(!scaled_have_previous_){scaled_have_previous_=true;scaled_active_hop_=hop;scaled_active_rf_=active_rf(hop);scaled_previous_physical_=physical;}
        else if(hop!=scaled_active_hop_){
            if(hop!=scaled_active_hop_+1)throw std::invalid_argument("scaled oscillator step skipped an actual RF hop");
            const double boundary=std::clamp(physical_retune(hop),scaled_previous_physical_,physical);
            integrate(scaled_active_rf_,scaled_previous_physical_,boundary);
            scaled_active_rf_=active_rf(hop);integrate(scaled_active_rf_,boundary,physical);scaled_active_hop_=hop;
        }else integrate(scaled_active_rf_,scaled_previous_physical_,physical);
        scaled_previous_physical_=physical;return scaled_phase_;
    }
    bool pulse_=false,scaled_step_=false,scaled_have_previous_=false;
    double scaled_begin_=36.8,scaled_previous_physical_=0,scaled_active_rf_=0,scaled_phase_=0;
    double audio_evolution_=0,audio_jump_=0;bool second_step_=false;unsigned erased_dwell_=100;
    std::unique_ptr<h::Generator> audio_generator_;
    std::vector<std::unique_ptr<helper_transient::Model>> transients_;
    int scaled_active_hop_=-1;
    helper_measurement_hops::Profile scaled_hops_;
    static constexpr double pulse_begin_=36.8,pulse_end_=37.2,pulse_hz_=1.2,scaled_step_hz_=1.2;
};
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
double cpu(){const auto c=std::clock();require(c!=static_cast<std::clock_t>(-1),"CPU clock unavailable");return static_cast<double>(c)/CLOCKS_PER_SEC;}
double wrap(double p){return std::remainder(p,tau);}
const char* schedule(const hp::Config& c){return c.shift_center_hz>1e9?(c.microwave_eu20?"keyed_eu20":"keyed_fcc100"):"keyed_hf50";}
struct Coeff {double audio=1,offset=0;};
Coeff coefficient(const h::HelperMeasurementSnapshot& s,double nominal_rf,double t){
    const double dt=t-s.time_seconds;
    return {1+(s.audio_ppm+dt*s.audio_drift_ppm_per_second)*1e-6,
        nominal_rf*(s.shift_ppm+dt*s.shift_drift_ppm_per_second)*1e-6};
}
double elapsed_clock(const h::HelperMeasurementSnapshot& s,double begin,double end){
    const double dt=end-begin;
    return dt+1e-6*(s.audio_ppm*dt+.5*s.audio_drift_ppm_per_second*
        ((end-s.time_seconds)*(end-s.time_seconds)-(begin-s.time_seconds)*(begin-s.time_seconds)));
}

struct Phase {
    bool valid=false;
    double date=0,high=0,low=0,high_sigma=0,difference_sigma=0;
    // Optional receiver-selected contiguous input support relative to dwell.
    // Zero/zero preserves the existing full-aperture policy.
    std::size_t support_first=0,support_end=0;
    bool transport_budget_valid=false;
    double transport_rate=0,diffusion_q=0;
    bool local_frequency_valid=false;double local_frequency_hz=0;
};
bool recent_frequency_support(std::span<const h::HelperDwellMeasurement> history,double current_date){
    if(history.size()<6)return false;
    // A failed current acquisition cannot recycle six clustered observations
    // from before a gap as fresh recovery evidence.
    if(!std::isfinite(current_date)||std::abs(current_date-history.back().time_seconds)>1e-9)return false;
    const auto first=history.size()-6;
    if(history.back().time_seconds-history[first].time_seconds>2.5)return false;
    for(std::size_t i=first+1;i<history.size();++i)
        if(history[i].time_seconds-history[i-1].time_seconds>.800000001)return false;
    return true;
}
Phase broad_phase(const h::HelperDwellMeasurement& m){
    if(!m.valid)return {};
    const double dt=m.time_seconds-m.first_sample_time_seconds;
    const double duration=m.end_sample_time_seconds-m.first_sample_time_seconds;
    const double hs=std::sqrt(2/(std::pow(10.,m.high_cn0_db_hz/10)*duration));
    const double ls=std::sqrt(2/(std::pow(10.,m.low_cn0_db_hz/10)*duration));
    // Symmetric constant-tone LS makes center phase and slope first-order
    // orthogonal. The first-sample phase already carries the opposite slope
    // covariance; transporting it back to center does not add that error
    // independently. Keep the half-sample date offset and common diffusion.
    const double common=tau*duration*m.common_phase_frequency_sigma_hz;
    return {true,m.time_seconds,wrap(m.high_phase_radians+tau*m.high_frequency_hz*dt),
        wrap(m.low_phase_radians+tau*m.low_frequency_hz*dt),
        hs+tau*.5/fs*m.high_sigma_hz+common,hs+ls+tau*.5/fs*(m.high_sigma_hz+m.low_sigma_hz)+2*common};
}

struct Receiver {
    std::string name;
    bool aided=false;
    h::HelperMeasurementConfig config;
    h::HelperAidedPhaseConfig aided_config;
    h::HelperMeasurement fit;
    std::optional<h::HelperMeasurementSnapshot> frozen;
    h::HelperDwellMeasurement measurement;
    Phase phase;
    bool phase_from_aided=false,last_global_prior_valid=false,last_supplied_prior_valid=false;
    bool use_joint_dwell=false,use_transient_phase=false,use_piecewise=false,use_uncached_joint=false,transient_phase_selected=false,transient_phase_withheld=false;
    unsigned piecewise_attempts=0,piecewise_detected=0,piecewise_selected=0,piecewise_withheld=0;
    double piecewise_cpu=0;
    bool last_joint_dwell_selected=false;
    double joint_phase_model_half_width=0,joint_difference_model_half_width=0;
    unsigned joint_dwell_attempts=0,joint_dwell_accepted=0,joint_dwell_fallbacks=0;
    unsigned transient_attempts=0,transient_detections=0,transient_selected_count=0,transient_withheld_count=0;
    double joint_dwell_cpu=0,transient_cpu=0;
    h::HelperAidedPhaseResult last_aided;
    std::optional<h::HelperJointDwellResult> last_joint_fresh;
    double frequency_error2=0,frequency_sigma2=0,frequency_error_max=0;
    unsigned frequency_errors=0,frequency_outside_three_sigma=0;
    unsigned covariance_frequency_errors=0,covariance_frequency_outside_three_sigma=0;
    double covariance_frequency_error2=0,covariance_frequency_sigma2=0;
    double acquisition_cpu=0,tracking_cpu=0,fit_cpu=0,cpu36=0,observed_cpu=0,external_cpu=0,startup_cpu=0,fallback_cpu=0;
    unsigned broad_calls=0,aided_calls=0,fallbacks=0,valid=0,model_restarts=0;
    unsigned residual_rejections=0,evolution_rejections=0,signal_rejections=0;
    unsigned phase_count=0,phase_heldout=0;
    double phase_error2=0,phase_max=0,heldout_error2=0,heldout_max=0;
    std::vector<h::HelperDwellMeasurement> recent;
    explicit Receiver(std::string label,bool use_aided,h::HelperMeasurementConfig c)
        :name(std::move(label)),aided(use_aided),config(c),fit(c){
        aided_config.sample_rate=fs;
        aided_config.phase_noise_radians_per_sqrt_second=c.phase_noise_radians_per_sqrt_second;
    }
    h::HelperMeasurementSnapshot snapshot(bool external=false){const auto a=cpu();auto s=fit.snapshot();const double elapsed=cpu()-a;fit_cpu+=elapsed;if(external)external_cpu+=elapsed;return s;}
    void observe(const hp::Dwell& d,bool missing,unsigned index,std::optional<h::HelperAidedPhasePrediction> supplied={}){
        const auto observed_started=cpu();
        phase={};measurement={};phase_from_aided=false;last_aided={};transient_phase_selected=false;transient_phase_withheld=false;
        last_joint_dwell_selected=false;last_joint_fresh.reset();joint_phase_model_half_width=0;joint_difference_model_half_width=0;
        std::optional<h::HelperDwellMeasurement> global_observation;
        if(missing){if(index==89){const auto s=snapshot();if(s.valid)frozen=s;}observed_cpu+=cpu()-observed_started;if(index==89)cpu36=total_cpu();return;}
        const auto data=std::span<const S>(d.samples.data()+settle,aperture);
        const double start=d.first_seconds+settle/fs;
        bool acquired=false;
        auto prior=snapshot();
        last_global_prior_valid=prior.valid;last_supplied_prior_valid=supplied&&supplied->valid;
        if(aided&&(prior.valid||last_supplied_prior_valid)){
            const auto predict_started=cpu();
            h::HelperAidedPhasePrediction prediction;
            if(prior.valid){
                const auto ph=fit.predict(2400,d.shift_hz,start+(aperture-1)/(2*fs),aperture/fs);
                const auto pl=fit.predict(550,d.shift_hz,start+(aperture-1)/(2*fs),aperture/fs);
                prediction={ph.valid&&pl.valid,ph.frequency_hz,pl.frequency_hz,
                    ph.mean_sigma_hz+ph.aperture_process_sigma_hz,pl.mean_sigma_hz+pl.aperture_process_sigma_hz};
            }
            fit_cpu+=cpu()-predict_started;
            // A separately qualified Helper model can locate this local
            // likelihood even while the auxiliary global fit is contradicted.
            // It supplies coordinates only; current samples still establish
            // frequency/phase, covariance and every residual/retune gate.
            if(last_supplied_prior_valid)prediction=*supplied;
            const auto a=cpu();auto result=h::estimate_helper_aided_phase(data,aided_config,prediction,d.shift_hz,start);
            if(result.valid)global_observation=result.measurement;
            if(use_joint_dwell&&result.valid&&last_supplied_prior_valid){
                ++joint_dwell_attempts;
                h::HelperJointDwellConfig candidate_config;
                candidate_config.sample_rate=fs;
                candidate_config.use_static_window_cache=!use_uncached_joint;
                candidate_config.phase_noise_radians_per_sqrt_second=config.phase_noise_radians_per_sqrt_second;
                candidate_config.frequency_drift_bound_hz_per_second=aided_config.slope_frequency_drift_bound_hz_per_second;
                const auto joint_started=cpu();
                const auto candidate=h::estimate_helper_joint_dwell(data,candidate_config,prediction,result);
                joint_dwell_cpu+=cpu()-joint_started;
                if(candidate.valid){result=candidate.observation;last_joint_fresh=candidate;last_joint_dwell_selected=true;
                    joint_phase_model_half_width=candidate.common_phase_model_half_width_radians;
                    joint_difference_model_half_width=candidate.phase_difference_model_half_width_radians;
                    ++joint_dwell_accepted;}
                else ++joint_dwell_fallbacks;
            }
            tracking_cpu+=cpu()-a;++aided_calls;
            bool transient_handled=false;
            if(use_transient_phase&&result.valid&&last_supplied_prior_valid){
                ++transient_attempts;const h::HelperTransientPhaseConfig candidate_config{};
                const auto transient_started=cpu();
                const auto candidate=h::estimate_helper_transient_phase(data,aided_config,candidate_config,prediction,result,start);
                const double transient_elapsed=cpu()-transient_started;
                transient_cpu+=transient_elapsed;tracking_cpu+=transient_elapsed;
                if(candidate.disturbance_detected){
                    ++transient_detections;transient_handled=true;acquired=true;
                    // A detected contaminated full aperture must not be
                    // ingested or requalified by broad acquisition. A
                    // selected phase uses the prior-only clock below.
                    if(candidate.valid&&candidate.support_end_sample>candidate.support_first_sample&&
                       candidate.support_end_sample<=data.size()){
                        phase={true,candidate.anchor_time_seconds,candidate.high_phase_radians,candidate.low_phase_radians,
                            candidate.high_sigma_radians+candidate.model_half_width_radians/3,
                            candidate.high_sigma_radians+candidate.low_sigma_radians+2*candidate.model_half_width_radians/3,
                            settle+candidate.support_first_sample,settle+candidate.support_end_sample};
                        transient_phase_selected=true;++transient_selected_count;
                    }else{transient_phase_withheld=true;++transient_withheld_count;}
                }
            }
            if(use_piecewise&&result.valid&&last_supplied_prior_valid&&!transient_handled){
                ++piecewise_attempts;const auto piecewise_started=cpu();
                const auto candidate=h::estimate_helper_piecewise_dwell(data,aided_config,{},prediction,result,start);
                const double elapsed=cpu()-piecewise_started;piecewise_cpu+=elapsed;tracking_cpu+=elapsed;
                if(candidate.disturbance_detected){
                    ++piecewise_detected;transient_handled=true;acquired=true;
                    if(candidate.valid&&candidate.support_end_sample>candidate.support_first_sample&&
                       candidate.support_end_sample<=data.size()){
                        phase={true,candidate.anchor_time_seconds,candidate.high_phase_radians,candidate.low_phase_radians,
                            candidate.high_sigma_radians+candidate.model_half_width_radians/3,
                            candidate.high_sigma_radians+candidate.low_sigma_radians+2*candidate.model_half_width_radians/3,
                            settle+candidate.support_first_sample,settle+candidate.support_end_sample};
                        transient_phase_selected=true;++piecewise_selected;
                    }else{transient_phase_withheld=true;++piecewise_withheld;}
                }
            }
            if(result.valid&&!transient_handled){
                // The independent legacy acquisition model keeps its original
                // gated raw measurement and scalar process model. The joint
                // history receives only the fresh joint likelihood below;
                // these two fallback models are never fused as independent
                // observations of the same dwell.
                const auto f=cpu();require(global_observation&&fit.ingest(*global_observation),"aided measurement intake failed");fit_cpu+=cpu()-f;
                measurement=result.measurement;last_aided=result;
                phase={true,result.center_time_seconds,result.high_center_phase_radians,result.low_center_phase_radians,
                    result.high_center_phase_sigma_radians+result.common_center_phase_sigma_radians+joint_phase_model_half_width/3,
                    result.phase_difference_sigma_radians};
                phase_from_aided=true;
                acquired=true;
            }else if(!result.valid){
                ++fallbacks;
                if(result.failure==h::HelperAidedPhaseFailure::residual_frequency)++residual_rejections;
                if(result.failure==h::HelperAidedPhaseFailure::phase_evolution)++evolution_rejections;
                if(result.failure==h::HelperAidedPhaseFailure::insufficient_signal)++signal_rejections;
            }
        }
        if(!acquired){const auto a=cpu();measurement=fit.observe(data,d.shift_hz,start);
            const double elapsed=cpu()-a;acquisition_cpu+=elapsed;if(aided_calls)fallback_cpu+=elapsed;else startup_cpu+=elapsed;++broad_calls;phase=broad_phase(measurement);}
        if(measurement.valid){++valid;recent.push_back(global_observation.value_or(measurement));if(recent.size()>8)recent.erase(recent.begin());}
        const auto state=snapshot();
        // Frequency reacquisition may replace a contradicted frequency model,
        // while the independent relative source reference is never reset.
        if(aided&&measurement.valid&&!state.valid&&state.dwell_count>=12&&recent_frequency_support(recent,measurement.time_seconds)){
            // Only replace the frequency fit after six recent independently
            // acquired/validated observations form a model-consistent fit.
            // A single contradictory sample cannot restart either the model
            // or the separate, permanent phase-clock reference.
            const auto a=cpu();h::HelperMeasurement candidate(config);
            for(std::size_t i=recent.size()-6;i<recent.size();++i)require(candidate.ingest(recent[i]),"frequency recovery intake failed");
            const auto recovered=candidate.snapshot();
            if(recovered.valid){fit=std::move(candidate);++model_restarts;}
            fit_cpu+=cpu()-a;
        }
        if(index==89){const auto s=snapshot();if(s.valid)frozen=s;}
        observed_cpu+=cpu()-observed_started;
        if(index==89)cpu36=total_cpu();
    }
    void evaluate_phase(const ContinuationSource& source,const hp::Dwell& d,unsigned index){
        if(measurement.valid){
            const double high_date=phase_from_aided&&last_aided.thermal_covariance_valid?last_aided.high_frequency_time_seconds:
                (phase_from_aided?last_aided.center_time_seconds:measurement.time_seconds);
            const double low_date=phase_from_aided&&last_aided.thermal_covariance_valid?last_aided.low_frequency_time_seconds:
                (phase_from_aided?last_aided.center_time_seconds:measurement.time_seconds);
            const double truth_high=2400*source.audio_scale_at(high_date)+source.shift_error_hz(d.shift_hz,high_date);
            const double truth_low=550*source.audio_scale_at(low_date)+source.shift_error_hz(d.shift_hz,low_date);
            const double errors[]{measurement.high_frequency_hz-truth_high,measurement.low_frequency_hz-truth_low};
            const double sigmas[]{std::hypot(measurement.high_sigma_hz,measurement.common_phase_frequency_sigma_hz),std::hypot(measurement.low_sigma_hz,measurement.common_phase_frequency_sigma_hz)};
            for(unsigned j=0;j<2;++j){++frequency_errors;frequency_error2+=errors[j]*errors[j];frequency_sigma2+=sigmas[j]*sigmas[j];frequency_error_max=std::max(frequency_error_max,std::abs(errors[j]));if(std::abs(errors[j])>3*sigmas[j])++frequency_outside_three_sigma;}
            if(phase_from_aided&&last_aided.thermal_covariance_valid){
                for(unsigned j=0;j<2;++j){
                    const double process=last_aided.process_covariance_valid?last_aided.process_covariance[j][j]:
                        measurement.common_phase_frequency_sigma_hz*measurement.common_phase_frequency_sigma_hz;
                    const double variance=last_aided.thermal_covariance[j][j]+process;
                    if(variance>0&&std::isfinite(variance)){
                        ++covariance_frequency_errors;covariance_frequency_error2+=errors[j]*errors[j];covariance_frequency_sigma2+=variance;
                        if(std::abs(errors[j])>3*std::sqrt(variance))++covariance_frequency_outside_three_sigma;
                    }
                }
            }
        }
        if(!phase.valid)return;
        const double sample=(phase.date-d.first_seconds)*fs;
        const auto i=static_cast<std::size_t>(std::floor(sample));
        require(i+1<d.common_phasor.size(),"phase truth date outside capture");
        const double f=sample-static_cast<double>(i);
        const double common=std::arg(C(d.common_phasor[i]))+f*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
        const double truth=wrap(tau*std::remainder(2400*source.tx_time(phase.date),1.)+common);
        const double error=wrap(phase.high-truth);
        ++phase_count;phase_error2+=error*error;phase_max=std::max(phase_max,std::abs(error));
        if(index>=95){++phase_heldout;heldout_error2+=error*error;heldout_max=std::max(heldout_max,std::abs(error));}
    }
    double total_cpu()const{return observed_cpu+external_cpu;}
};

struct Trace {
    std::uint64_t first=0;
    std::vector<S> noisy,clean;
    std::vector<unsigned char> supported;
    // Evaluator-only physical channel disturbance mask on the output grid.
    std::vector<unsigned char> physically_disturbed;
    bool contains(std::uint64_t n)const{return n>=first&&n-first<noisy.size();}
    C at(std::uint64_t n,bool twin)const{return C((twin?clean:noisy).at(static_cast<std::size_t>(n-first)));}
    bool support(std::uint64_t n)const{return contains(n)&&supported[static_cast<std::size_t>(n-first)];}
};
h::HelperStreamConditioner::Config stream_config(std::uint64_t input,double audio,long double source){
    const long double output=std::ceil(source*fs);
    require(output>0&&output<static_cast<long double>(UINT64_MAX),"invalid relative source coordinate");
    double fraction=static_cast<double>((output-source*fs)/audio);
    if(fraction>=1){const auto shift=static_cast<std::uint64_t>(std::floor(fraction));input+=shift;source+=audio*shift/fs;fraction-=shift;}
    require(fraction>=0&&fraction<1,"invalid fractional relative origin");
    return {hp::sample_rate,input,static_cast<std::uint64_t>(output),source,fraction};
}
struct Stream {
    std::unique_ptr<h::HelperStreamConditioner> noisy,clean;
    Trace trace;
    double cpu_seconds=0,cpu36=0,reference_input=0,gauge=0,evaluator_mask_cpu_seconds=0,evaluator_stream_cpu_seconds=0;
    unsigned anchors=0,timing_rejections=0,model_rejections=0,guard_rejections=0,guard_shortened=0;
    bool conditional_guard=false;
    unsigned heldout_anchor_mask=0;
    std::uint64_t supported_samples=0,output_samples=0,input_samples=0;
    std::optional<long double> first_supported_input,last_supported_input;
    std::array<std::uint64_t,100> supported_by_dwell{};
    std::array<long double,100> first_supported_by_dwell{},last_supported_by_dwell{};
    double timing_error_max=0,timing_error2=0,first_timing_error=0,last_timing_error=0;
    unsigned timing_observations=0,cycle_slips=0,cycle_error_observations=0;
    long long previous_cycle_error=0;
    bool initialized=false;double first_input_seconds=0;
    std::string heldout_support_dates()const{
        std::ostringstream out;out<<std::setprecision(10);
        for(unsigned i=95;i<100;++i){
            if(i!=95)out<<',';
            out<<i<<':'<<supported_by_dwell[i];
            if(supported_by_dwell[i])out<<':'<<static_cast<double>(first_supported_by_dwell[i]/fs)
                                        <<':'<<static_cast<double>(last_supported_by_dwell[i]/fs);
        }
        return out.str();
    }
    void initialize(std::uint64_t first,Coeff c,double source,double reference,const ContinuationSource& simulator){
        const auto initialized_started=cpu();first_input_seconds=static_cast<double>(first)/fs;
        const auto cfg=stream_config(first,c.audio,source);
        noisy=std::make_unique<h::HelperStreamConditioner>(cfg,h::HelperStreamConditioner::Correction{c.audio,c.offset,.001});
        const auto clean_started=cpu();clean=std::make_unique<h::HelperStreamConditioner>(cfg,h::HelperStreamConditioner::Correction{c.audio,c.offset,.001});
        const double clean_elapsed=cpu()-clean_started;
        evaluator_stream_cpu_seconds+=clean_elapsed;
        reference_input=reference;
        cpu_seconds+=cpu()-initialized_started-clean_elapsed;
        // Exactly one EVALUATOR-ONLY clock gauge; no probe fit or receiver input.
        gauge=reference-simulator.tx_time(reference);initialized=true;
    }
    void feed(const hp::Dwell& d,Coeff c,const Phase& phase,bool model_valid,const ContinuationSource& simulator,
              std::optional<h::HelperRelativeClock::Estimate> joint={}){
        const auto feed_started=cpu();double excluded_cpu=0;
        require(initialized,"uninitialized stream");
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        noisy->update({c.audio,c.offset,.001});noisy->clear_phase();
        const auto clean_update=cpu();clean->update({c.audio,c.offset,.001});clean->clear_phase();excluded_cpu+=cpu()-clean_update;
        bool allow=model_valid&&phase.valid&&std::isfinite(phase.high_sigma)&&phase.high_sigma*3<.5;
        double guard_anchor_budget=0;
        if(!model_valid)++model_rejections;
        const long double anchor=phase.date*fs;
        if(allow&&joint){
            const double clock=static_cast<double>(noisy->clock_time_at_input(anchor));
            const double mismatch=std::abs(clock-joint->source_seconds);
            if(!joint->valid||!joint->reference_preserved||mismatch>joint->source_half_width_seconds){allow=false;++timing_rejections;}
        }
        if(allow){
            const double clock=static_cast<double>(noisy->clock_time_at_input(anchor));
            const bool selected=phase.support_end>phase.support_first;
            auto support_first=selected?first+phase.support_first:first+settle+phase_margin;
            auto support_end=selected?first+phase.support_end:first+settle+aperture-phase_margin;
            if(selected&&(phase.support_end>d.samples.size()||phase.support_first>=phase.support_end))
                throw std::invalid_argument("invalid receiver-selected phase support");
            if(conditional_guard){
                if(!joint||!joint->valid||!phase.transport_budget_valid||anchor<support_first||anchor>=support_end){
                    allow=false;
                }else{
                    // Conditional input-point budget. The conditioner separately erodes
                    // support by its actual interpolation halo; this is not an FIR
                    // output-error guarantee for arbitrary modulated signals.
                    const double b0=3*phase.high_sigma+tau*980*(std::abs(clock-joint->source_seconds)+joint->source_half_width_seconds);
                    guard_anchor_budget=b0;
                    const double max_d=std::max(static_cast<double>(anchor-support_first),static_cast<double>(support_end-anchor))/fs;
                    double lo=0,hi=max_d;
                    if(!(b0<.5))allow=false;
                    else{
                        for(int i=0;i<28;++i){const double mid=(lo+hi)/2;
                            if(b0+phase.transport_rate*mid+4.5*phase.diffusion_q*std::sqrt(mid)<.5)lo=mid;else hi=mid;}
                        const auto begin=static_cast<std::uint64_t>(std::max<long double>(0,std::ceil(anchor-lo*fs)));
                        const auto end=static_cast<std::uint64_t>(std::max<long double>(0,std::floor(anchor+lo*fs)));
                        const auto old_first=support_first,old_end=support_end;
                        support_first=std::max(support_first,begin);support_end=std::min(support_end,end);
                        if(support_first!=old_first||support_end!=old_end)++guard_shortened;
                        if(support_first>=support_end||anchor<support_first||anchor>=support_end)allow=false;
                    }
                }
                if(!allow)++guard_rejections;
            }
            if(allow){
            const h::HelperStreamConditioner::LocalPhase p{anchor,wrap(tau*2400*clock-phase.high),
                support_first,support_end,first+hp::dwell_samples};
            const bool accepted=noisy->set_phase(p);
            const auto clean_phase=cpu();require(accepted==clean->set_phase(p),"paired local phase acceptance mismatch");excluded_cpu+=cpu()-clean_phase;
            if(accepted&&d.first_seconds>=38){++anchors;
                const auto dwell=static_cast<unsigned>(std::llround(d.first_seconds/.4));
                if(dwell>=95&&dwell<100)heldout_anchor_mask|=1U<<(dwell-95);}
            if(accepted){
            const auto evaluation_start=cpu();
            const double error=clock-simulator.tx_time(phase.date)-gauge;
            if(!timing_observations)first_timing_error=error;
            last_timing_error=error;
            timing_error_max=std::max(timing_error_max,std::abs(error));timing_error2+=error*error;++timing_observations;
            const auto cycle=static_cast<long long>(std::llround(error*1850));
            if(cycle)++cycle_error_observations;
            if(cycle!=previous_cycle_error)++cycle_slips;
            previous_cycle_error=cycle;excluded_cpu+=cpu()-evaluation_start;
            }
            }
        }
        constexpr std::array<std::size_t,5> sizes{91,223,17,251,73};
        std::size_t pos=static_cast<std::size_t>(noisy->next_input_sample()-first),chunk=0;
        while(pos<d.samples.size()){
            const auto end=std::min(d.samples.size(),pos+sizes[chunk++%sizes.size()]);
            while(pos<end){
                std::array<S,256>x{},y{};std::array<long double,256>dates{},other{};
                auto p=noisy->process(std::span<const S>(d.samples.data()+pos,end-pos),x,first+pos,false,dates);
                const auto clean_process=cpu();auto q=clean->process(std::span<const S>(d.clean_probe.data()+pos,end-pos),y,first+pos,false,other);excluded_cpu+=cpu()-clean_process;
                require(p.consumed==q.consumed&&p.produced==q.produced&&p.output_first_sample==q.output_first_sample,"paired stream map mismatch");
                require(p.consumed||p.produced,"stream conditioner stalled");
                for(std::size_t j=0;j<p.produced;++j){
                    bool support=noisy->phase_supported_at(dates[j])&&noisy->timing_supported_at(dates[j],p.output_first_sample+j);
                    if(support&&conditional_guard){
                        const auto map_error=noisy->timing_error_at(dates[j],p.output_first_sample+j);
                        const double dt=(std::abs(static_cast<double>(dates[j]-anchor))+std::ceil(noisy->lookahead_seconds()*fs))/fs;
                        support=allow&&map_error&&guard_anchor_budget+phase.transport_rate*dt+
                            4.5*phase.diffusion_q*std::sqrt(dt)+tau*1780*std::abs(static_cast<double>(*map_error))<.5;
                    }
                    if(trace.noisy.empty())trace.first=p.output_first_sample;
                    require(p.output_first_sample+j==trace.first+trace.noisy.size(),"noncontiguous output trace");
                    trace.noisy.push_back(x[j]);trace.supported.push_back(static_cast<unsigned char>(support));
                    ++output_samples;if(support){++supported_samples;
                        if(!first_supported_input)first_supported_input=dates[j];
                        last_supported_input=dates[j];
                        const auto dwell=static_cast<std::size_t>(dates[j]/(fs*hp::dwell_seconds));
                        if(dwell<supported_by_dwell.size()){
                            if(!supported_by_dwell[dwell])first_supported_by_dwell[dwell]=dates[j];
                            last_supported_by_dwell[dwell]=dates[j];++supported_by_dwell[dwell];
                        }}
                }
                const auto truth_started=cpu();
                for(std::size_t j=0;j<p.produced;++j)
                    trace.physically_disturbed.push_back(static_cast<unsigned char>(simulator.has_transients()&&simulator.physical_disturbance(dates[j],std::ceil(noisy->lookahead_seconds()*fs))));
                const double truth_elapsed=cpu()-truth_started;
                excluded_cpu+=truth_elapsed;evaluator_mask_cpu_seconds+=truth_elapsed;
                const auto clean_copy=cpu();trace.clean.insert(trace.clean.end(),y.begin(),y.begin()+static_cast<std::ptrdiff_t>(q.produced));excluded_cpu+=cpu()-clean_copy;
                pos+=p.consumed;input_samples+=p.consumed;
            }
        }
        cpu_seconds+=cpu()-feed_started-excluded_cpu;
        evaluator_stream_cpu_seconds+=excluded_cpu;
    }
};

struct Metrics {unsigned windows=0,noisy_windows=0,full_windows=0;std::uint64_t sample_count=0,physically_disturbed_supported=0,physically_clean_supported=0,physically_disturbed_withheld=0,physically_clean_withheld=0;double frequency_rms=0,frequency_max=0,phase_rms=0,phase_max=0,noisy_frequency_rms=0,noisy_fit_se=0,sample_phase_rms=0,sample_phase_max=0;};
Metrics tone_metrics(const Trace& t,double gauge,const Trace* other=nullptr){
    Metrics out;constexpr std::uint64_t lag=128;
    const auto supported=[&](std::uint64_t n){return t.support(n)&&(!other||other->support(n));};
    const double fixed_phase=wrap(tau*(2400-hp::probe_frequency_hz)*gauge);
    for(unsigned d=95;d<100;++d){
        const auto lo=static_cast<std::uint64_t>(std::llround((d*.4+.08+gauge)*fs));
        const auto hi=lo+5760;
        if(!t.contains(lo)||!t.contains(hi-1))continue;
        bool full=true;for(auto i=lo;i<hi;++i)if(!supported(i)){full=false;break;}
        C phase{},frequency{};unsigned n=0;
        for(auto i=lo;i+lag<hi;++i){
            if(!supported(i)||!supported(i+lag))continue;
            phase+=t.at(i,true)*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*i/fs,1.)-fixed_phase);
            frequency+=t.at(i+lag,true)*std::conj(t.at(i,true))*std::polar(1.,-tau*hp::probe_frequency_hz*lag/fs);++n;
        }
        if(n<1000)continue;
        if(full)++out.full_windows;
        const double f=std::arg(frequency)*fs/(tau*lag),p=std::arg(phase);
        ++out.windows;out.frequency_rms+=f*f;out.frequency_max=std::max(out.frequency_max,std::abs(f));out.phase_rms+=p*p;out.phase_max=std::max(out.phase_max,std::abs(p));
        std::array<double,24>angles{};bool valid=true;
        for(unsigned b=0;b<24;++b){C z{};
            for(std::uint64_t j=0;j<240;++j){const auto i=lo+b*240+j;if(!supported(i)){valid=false;break;}
                z+=t.at(i,false)*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*i/fs,1.)-fixed_phase);}
            if(!valid)break;
            const double angle=std::arg(z);angles[b]=b?angles[b-1]+wrap(angle-angles[b-1]):angle;
        }
        if(valid){double mean=0;for(double a:angles)mean+=a/24;double ss=0,slope=0;
            for(unsigned b=0;b<24;++b){const double dt=b*.01-.115;ss+=dt*dt;slope+=dt*(angles[b]-mean);}slope/=ss;
            double residual=0;for(unsigned b=0;b<24;++b)residual+=std::pow(angles[b]-mean-slope*(b*.01-.115),2);
            ++out.noisy_windows;out.noisy_frequency_rms+=std::pow(slope/tau,2);out.noisy_fit_se+=residual/22/ss/(tau*tau);}
    }
    if(out.windows){out.frequency_rms=std::sqrt(out.frequency_rms/out.windows);out.phase_rms=std::sqrt(out.phase_rms/out.windows);}
    if(out.noisy_windows){out.noisy_frequency_rms=std::sqrt(out.noisy_frequency_rms/out.noisy_windows);out.noisy_fit_se=std::sqrt(out.noisy_fit_se/out.noisy_windows);}
    // Expose errors anywhere the receiver admitted support, including a late
    // retune transient that could be hidden by central 240ms window summaries.
    for(std::size_t i=0;i<t.noisy.size();++i){
        const auto n=t.first+i;const double source_date=static_cast<double>(n)/fs-gauge;
        if(source_date<38.02||source_date>39.98)continue;
        const bool disturbed=i<t.physically_disturbed.size()&&t.physically_disturbed[i];
        if(!supported(n)){
            if(disturbed)++out.physically_disturbed_withheld;else ++out.physically_clean_withheld;
            continue;
        }
        if(disturbed)++out.physically_disturbed_supported;else ++out.physically_clean_supported;
        const auto value=t.at(n,true);if(std::norm(value)<1e-12)continue;
        const double error=std::arg(value*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-fixed_phase));
        ++out.sample_count;out.sample_phase_rms+=error*error;out.sample_phase_max=std::max(out.sample_phase_max,std::abs(error));
    }
    if(out.sample_count)out.sample_phase_rms=std::sqrt(out.sample_phase_rms/static_cast<double>(out.sample_count));
    return out;
}

struct Evm {unsigned symbols=0;double noisy=0,clean=0;};
Evm evm_metrics(const Trace& trace,const hp::Source& source,double gauge,const Trace* other=nullptr){
    constexpr int span=640,sps=80;
    std::array<double,2*span+1> taps{};double energy=0;
    for(int j=-span;j<=span;++j){const double v=datapump::fast::root_raised_cosine(static_cast<double>(j)/sps,.2);taps[static_cast<std::size_t>(j+span)]=v;energy+=v*v;}
    const double scale=std::sqrt(energy/sps)/energy;
    const double phase=wrap(tau*(2400-hp::probe_frequency_hz)*gauge);
    const auto supported=[&](std::uint64_t n){return trace.support(n)&&(!other||other->support(n));};
    Evm out;double reference_power=0;
    for(unsigned k=8;k<592;++k){
        // One fixed evaluator clock gauge, derived once from simulator clock
        // truth. Neither symbol samples nor residual errors choose this date.
        const double center=(38+gauge)*fs+k*sps;
        const auto integer=static_cast<std::uint64_t>(std::floor(center));
        const double fraction=center-std::floor(center);
        if(integer<span||!trace.contains(integer-span)||!trace.contains(integer+span+1))continue;
        const double within=std::fmod(center/fs-gauge,.4);if(within<.08||within>.32)continue;
        C noisy{},clean{};bool accepted=true;
        for(int j=-span;j<=span;++j){const auto n=static_cast<std::uint64_t>(static_cast<std::int64_t>(integer)+j);
            if(!supported(n)||(fraction>0&&!supported(n+1))){accepted=false;break;}
            const auto mix=std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-phase);
            C a=trace.at(n,false)*mix,b=trace.at(n,true)*mix;
            if(fraction>0){const auto next=std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*(n+1)/fs,1.)-phase);
                a=(1-fraction)*a+fraction*trace.at(n+1,false)*next;b=(1-fraction)*b+fraction*trace.at(n+1,true)*next;}
            noisy+=a*taps[static_cast<std::size_t>(j+span)];clean+=b*taps[static_cast<std::size_t>(j+span)];}
        if(!accepted)continue;
        const C expected=source.probe_amplitude()*source.symbol(k);
        reference_power+=std::norm(expected);out.noisy+=std::norm(noisy*scale-expected);out.clean+=std::norm(clean*scale-expected);++out.symbols;
    }
    if(reference_power>0){out.noisy=std::sqrt(out.noisy/reference_power);out.clean=std::sqrt(out.clean/reference_power);}return out;
}

struct Frozen {
    bool valid=false;
    double frequency=0,thermal_sigma=0,model_half=0,half_width=0;
    double high_frequency=0,low_frequency=0,audio=1,source=0,reference=0;
    double audio_drift=0,shift_ppm=0,shift_drift=0;
    double process_sigma=0,stochastic_sigma=0,enclosure_extra=0;
    bool thermal_available=false,recent_guard=false;
};
struct Errors {
    unsigned count=0,outside=0;
    double square=0,maximum=0,sigma_square=0;
    void add(double error,double sigma){++count;square+=error*error;sigma_square+=sigma*sigma;maximum=std::max(maximum,std::abs(error));if(std::abs(error)>3*sigma)++outside;}
    double rms()const{return count?std::sqrt(square/count):0;}
    double sigma_rms()const{return count?std::sqrt(sigma_square/count):0;}
};
bool advance_recovery(bool& suspended,unsigned& streak,bool accepted,bool innovation_available,bool innovation_consistent){
    if(!suspended)return false;
    if(accepted&&innovation_available&&innovation_consistent)++streak;else streak=0;
    if(streak>=3){suspended=false;streak=0;return true;}
    return false;
}
bool internal_model_conflict(bool current_valid,bool model_established,bool current_accepted,const h::HelperJointEstimator::Estimate& updated){
    return current_valid&&model_established&&!current_accepted&&!updated.model_consistent&&
        !updated.ambiguity_limited&&!updated.work_limited;
}
std::optional<h::HelperJointEstimator::Input> frequency_epoch_raw(
    h::HelperJointEstimator::Input raw,bool specifically_frequency_notice){
    // A new frequency model may explain a notification that the OLD
    // frequency model was contradicted. It cannot erase independent channel,
    // phase or other external evidence merely by choosing a step model.
    if(raw.model_contradicted&&!specifically_frequency_notice)return {};
    raw.model_contradicted=false;return raw;
}

// A retained old-model coordinate used ONLY by the change detector. Eight
// corners capture the exact bilinear frequency mean of the declared linear
// audio/RF model. Nonnegative interpolation combines support radii by L1;
// no corner is ingested as new evidence and it never grants phase authority.
struct DiagnosticPrior {
    struct Value {double mean=0,sigma=0,model=0;};
    bool valid=false;double begin=0,end=0,endpoint=0,minimum_rf=0,maximum_rf=0,reference=0,last_observation=0;
    double curvature_bound_hz_per_second2=0;
    std::array<std::array<std::array<Value,2>,2>,2> values{};
    void capture(const h::HelperJointEstimator& joint,double date,double last,double low_rf,double high_rf,bool extended=false){
        DiagnosticPrior next;next.begin=date;next.endpoint=date+2.0;next.end=date+(extended?3.0:2.0);
        // An explicit experimental model assumption, not a measured curvature
        // or a reduced uncertainty guard. It applies over the entire retained
        // interval. Unmodeled jumps and larger curvature violate this prior.
        next.curvature_bound_hz_per_second2=extended?.002:0;
        next.minimum_rf=low_rf;next.maximum_rf=high_rf;next.last_observation=last;
        for(unsigned pilot=0;pilot<2;++pilot)for(unsigned rf=0;rf<2;++rf)for(unsigned time=0;time<2;++time){
            const auto e=joint.predict(pilot?550:2400,rf?high_rf:low_rf,time?next.endpoint:date);
            if(!e.valid||!e.reference_preserved)return;
            if(!pilot&&!rf&&!time)next.reference=e.reference_input_seconds;
            else if(e.reference_input_seconds!=next.reference)return;
            next.values[pilot][rf][time]={e.frequency_hz,e.stochastic_sigma_hz,e.model_half_width_hz};
        }
        next.valid=true;*this=next;
    }
    std::optional<Value> predict(unsigned pilot,double rf,double date)const{
        if(!valid||pilot>1||!std::isfinite(rf)||!std::isfinite(date)||date<begin-1e-8||date>end+1e-8||rf<minimum_rf||rf>maximum_rf)return {};
        const double u=(rf-minimum_rf)/(maximum_rf-minimum_rf),v=(date-begin)/(endpoint-begin);
        Value out;
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){
            const double w=(a?u:1-u)*(b?v:1-v);const auto& x=values[pilot][a][b];
            out.mean+=w*x.mean;out.sigma+=std::abs(w)*x.sigma;out.model+=std::abs(w)*x.model;
        }
        out.model+=.5*curvature_bound_hz_per_second2*std::abs((date-begin)*(date-endpoint));
        return out;
    }
};

struct Chain {
    std::string name;
    double reference_rf=30e6;
    bool use_joint=false,use_dual=false,use_recovery=false,use_extended_recovery=false,use_gap_propagation=false;
    h::HelperJointEstimator::FrequencyStepModel selected_step_model=h::HelperJointEstimator::FrequencyStepModel::common_hz;
    Receiver receiver;
    h::HelperRelativeClock clock;
    h::HelperJointEstimator joint;
    h::HelperDualPilotPhase dual;
    DiagnosticPrior detector_prior;
    h::HelperFrequencyRecovery recovery;
    h::HelperFrequencyRecovery::Result recovery_state;
    std::vector<h::HelperJointEstimator::Input> raw_replay;
    unsigned recovery_epoch_requests=0,recovery_epoch_accepted=0,recovery_external_rejections=0;
    unsigned diagnostic_extrapolations=0;
    bool recovery_epoch_active=false;
    Stream stream;
    Frozen frozen;
    Coeff retained_correction;double retained_rf=0;bool have_retained_correction=false;
    h::HelperRelativeClock::Estimate old_clock;
    h::HelperJointEstimator::Estimate joint_state;
    double extra_cpu=0,extra_cpu36=0,evaluator_cpu=0,latest_joint_input=-std::numeric_limits<double>::infinity();
    bool joint_suspended=false,current_joint_accepted=false,current_phase_only=false,joint_ever_established=false;
    unsigned joint_recovery_streak=0,joint_contradictions=0,joint_model_contradictions=0,joint_authority_rejections=0;
    unsigned phase_only_count=0,soft_attempts=0,soft_accepted=0,soft_fallbacks=0;
    bool use_soft_joint=false,use_soft_rate=false,use_evolving_frequency=false;
    unsigned evolving_attempts=0,evolving_accepted=0,evolving_fallbacks=0;
    unsigned latest_accepted_count=0,joint_inputs=0,joint_qualified=0,clock_outside=0,dual_combined=0,dual_fallback=0,dual_inconsistent=0;
    std::array<unsigned,10> dual_failure_counts{};
    unsigned frozen_frequency_misses=0,heldout_count=0;
    double frozen_error=0,frozen_heldout_square=0,frozen_heldout_max=0,clock_error_max=0;
    Errors current_phase,heldout_phase,difference,settled_phase;
    std::vector<double> phase_errors;
    std::array<double,100> evaluator_phase_error{};
    std::array<bool,100> evaluator_phase_available{};
    std::array<bool,100> admitted_dwells{},fresh_joint_dwells{};
    explicit Chain(std::string label,h::HelperAidedFrequencyMethod method,bool fitted,bool blended,
                   const h::HelperMeasurementConfig& mc,double rf,bool slope_covariance=false,bool recovering=false,
                   h::HelperJointEstimator::FrequencyStepModel step_model=h::HelperJointEstimator::FrequencyStepModel::common_hz,
                   bool extended_recovery=false,bool gap_propagation=false)
        :name(std::move(label)),reference_rf(rf),use_joint(fitted),use_dual(blended),use_recovery(recovering),use_extended_recovery(extended_recovery),use_gap_propagation(gap_propagation),selected_step_model(step_model),receiver(name,true,mc),
         clock([]{h::HelperRelativeClock::Config c;c.differential_phase_half_width_radians=.02;return c;}()),
         joint([&]{h::HelperJointEstimator::Config c;c.reference_shift_hz=rf;c.allow_frequency_step=recovering;
             if(gap_propagation){c.maximum_phase_branch_gap_seconds=12;c.phase_branch_audio_evolution_ppm_per_second=.2;
                 c.minimum_post_gap_observations=3;c.minimum_post_gap_span_seconds=.8-1e-8;}return c;}()),dual({}),recovery({}) {
        receiver.aided_config.frequency_method=method;
        receiver.aided_config.compute_slope_covariance=slope_covariance;
        // Declared experiment prior, applied to clean and perturbed captures
        // alike. It covers |audio drift|<=.04 ppm/s and |RF drift|<=.00002
        // ppm/s over the commanded HF or microwave band. It is never selected
        // from the simulator's actual realization or probe measurements.
        if(slope_covariance)receiver.aided_config.slope_frequency_drift_bound_hz_per_second=rf>1e9?.125:.001;
    }
    std::optional<h::HelperAidedPhasePrediction> prior_for(const hp::Dwell& d){
        if(!use_joint)return {};
        const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
        if(use_recovery&&joint_suspended&&!recovery_epoch_active&&recovery_state.provisional){
            const auto hi=detector_prior.predict(0,d.shift_hz,date),lo=detector_prior.predict(1,d.shift_hz,date);
            if(hi&&lo){if(date>detector_prior.endpoint+1e-8)++diagnostic_extrapolations;return h::HelperAidedPhasePrediction{true,
                hi->mean+recovery_state.additive_correction_hz,lo->mean+recovery_state.additive_correction_hz,
                hi->sigma+hi->model+recovery_state.conditional_half_width_hz,
                lo->sigma+lo->model+recovery_state.conditional_half_width_hz};}
        }
        // A confirmed epoch's prediction may locate the local acquisition
        // likelihood during the recovery run; it does not itself grant phase.
        if(joint_suspended&&!recovery_epoch_active)return {};
        const auto hi=joint.predict(2400,d.shift_hz,date),lo=joint.predict(550,d.shift_hz,date);
        if(!hi.valid||!lo.valid)return {};
        return h::HelperAidedPhasePrediction{true,hi.frequency_hz,lo.frequency_hz,
            hi.stochastic_sigma_hz+hi.model_half_width_hz,lo.stochastic_sigma_hz+lo.model_half_width_hz};
    }
    void observe_recovery(const hp::Dwell& d,bool missing,const h::HelperJointEstimator::Estimate& previous){
        if(!use_recovery||recovery_epoch_active)return;
        const auto& m=receiver.measurement;const auto& r=receiver.last_aided;
        const bool fresh_raw_support=!missing&&r.valid&&r.thermal_covariance_valid&&r.frequency_dates_valid&&
            (!receiver.aided_config.compute_slope_covariance||r.process_covariance_valid);
        // A confirmed proposal is held only inside the old-model diagnostic
        // bracket while enough full-covariance RAW points are acquired. No
        // proposal mean/radius ever becomes a joint likelihood observation.
        if(recovery_state.epoch_requested&&(!use_extended_recovery||fresh_raw_support)&&
            d.first_seconds+settle/fs+(aperture-1)/(2*fs)<=detector_prior.end+1e-8)return;
        const bool proposal_broken=use_extended_recovery&&recovery_state.epoch_requested&&!fresh_raw_support;
        if(proposal_broken)raw_replay.clear();
        h::HelperFrequencyRecovery::Input x;x.time_seconds=d.first_seconds+settle/fs+(aperture-1)/(2*fs);
        x.reference_status_known=previous.reference_initialized;
        x.reference_preserved=previous.reference_preserved;x.reference_input_seconds=previous.reference_input_seconds;
        x.valid=!missing&&m.valid&&!proposal_broken;x.first_sample_seconds=m.first_sample_time_seconds;x.end_sample_seconds=m.end_sample_time_seconds;
        x.high_frequency_hz=m.high_frequency_hz;x.low_frequency_hz=m.low_frequency_hz;
        if(x.valid){
            x.frequency_dates_valid=true;
            if(r.valid&&r.thermal_covariance_valid&&r.frequency_dates_valid){
                x.high_frequency_time_seconds=r.high_frequency_time_seconds;x.low_frequency_time_seconds=r.low_frequency_time_seconds;
                for(unsigned i=0;i<2;++i)for(unsigned j=0;j<2;++j){
                    x.thermal_covariance[i][j]=r.thermal_covariance[i][j];x.process_covariance[i][j]=r.process_covariance[i][j];}
                x.process_covariance_valid=r.process_covariance_valid;x.common_frequency_sigma_hz=m.common_phase_frequency_sigma_hz;
                x.current_frequency_model_half_width_hz=std::max(r.high_frequency_model_half_width_hz,r.low_frequency_model_half_width_hz);
            }else{
                // Broad acquisition supplies marginal errors only. Twice the
                // diagonal marginal covariance dominates every possible 2x2
                // covariance with these marginals; it is used for detection,
                // NEVER replayed as a fabricated full joint phase summary.
                x.high_frequency_time_seconds=x.low_frequency_time_seconds=m.time_seconds;
                x.thermal_covariance={{{2*m.high_sigma_hz*m.high_sigma_hz,0},{0,2*m.low_sigma_hz*m.low_sigma_hz}}};
                x.common_frequency_sigma_hz=m.common_phase_frequency_sigma_hz;
                x.current_frequency_model_half_width_hz=receiver.aided_config.slope_frequency_drift_bound_hz_per_second*
                    (m.end_sample_time_seconds-m.first_sample_time_seconds)+10e-6;
            }
            const auto high=detector_prior.predict(0,d.shift_hz,x.high_frequency_time_seconds);
            const auto low=detector_prior.predict(1,d.shift_hz,x.low_frequency_time_seconds);
            if(high&&low){x.prediction_valid=true;x.predicted_high_hz=high->mean;x.predicted_low_hz=low->mean;
                x.prediction_high_sigma_hz=high->sigma;x.prediction_low_sigma_hz=low->sigma;
                x.prediction_high_model_half_width_hz=high->model;x.prediction_low_model_half_width_hz=low->model;
                x.prediction_last_observation_seconds=detector_prior.last_observation;}
        }
        recovery_state=recovery.observe(x);
    }
    h::HelperRelativeClock::Estimate clock_at(double time,double rf)const{
        if(!use_joint)return clock.predict(time);
        const auto j=joint.predict(1600,rf,time);
        h::HelperRelativeClock::Estimate c;c.valid=j.valid&&!joint_suspended&&(current_joint_accepted||current_phase_only);c.reference_preserved=j.reference_preserved;
        c.reference_initialized=j.reference_initialized;c.reference_input_seconds=j.reference_input_seconds;
        c.input_seconds=time;c.source_seconds=j.source_seconds;c.audio_scale=j.audio_scale;
        c.source_half_width_seconds=3*j.source_stochastic_sigma_seconds+j.source_model_half_width_seconds;
        c.audio_half_width_ppm=3*j.audio_stochastic_sigma_ppm+j.audio_model_half_width_ppm;
        c.audio_drift_ppm_per_second=j.audio_drift_ppm_per_second;return c;
    }
    void observe(const hp::Dwell& d,bool missing,unsigned index){
        current_phase_only=false;
        const auto before=cpu();auto prior=prior_for(d);extra_cpu+=cpu()-before;
        receiver.observe(d,missing,index,prior);
        const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
        const auto started=cpu();
        bool epoch_committed_current=false;
        const auto previous=joint.predict(1600,d.shift_hz,date);
        joint_ever_established=joint_ever_established||previous.valid;
        const double high_frequency_date=receiver.phase_from_aided&&receiver.last_aided.thermal_covariance_valid?
            receiver.last_aided.high_frequency_time_seconds:(receiver.phase_from_aided?receiver.last_aided.center_time_seconds:receiver.measurement.time_seconds);
        const double low_frequency_date=receiver.phase_from_aided&&receiver.last_aided.thermal_covariance_valid?
            receiver.last_aided.low_frequency_time_seconds:(receiver.phase_from_aided?receiver.last_aided.center_time_seconds:receiver.measurement.time_seconds);
        const auto previous_high=joint.predict(2400,d.shift_hz,high_frequency_date);
        const auto previous_low=joint.predict(550,d.shift_hz,low_frequency_date);
        bool innovation_available=receiver.measurement.valid&&previous_high.valid&&previous_low.valid;
        bool innovation_consistent=true;
        if(innovation_available){
            const auto& m=receiver.measurement;
            const double current_model=receiver.last_aided.valid?std::max(receiver.last_aided.high_frequency_model_half_width_hz,receiver.last_aided.low_frequency_model_half_width_hz):0;
            const double radii[]{5*(previous_high.stochastic_sigma_hz+m.high_sigma_hz+m.common_phase_frequency_sigma_hz)+previous_high.model_half_width_hz+current_model,
                                5*(previous_low.stochastic_sigma_hz+m.low_sigma_hz+m.common_phase_frequency_sigma_hz)+previous_low.model_half_width_hz+current_model};
            innovation_consistent=std::abs(m.high_frequency_hz-previous_high.frequency_hz)<=radii[0]&&
                std::abs(m.low_frequency_hz-previous_low.frequency_hz)<=radii[1];
        }
        observe_recovery(d,missing,previous);
        const bool frequency_contradiction=use_joint&&innovation_available&&!innovation_consistent;
        if(frequency_contradiction){joint_suspended=true;joint_recovery_streak=0;++joint_contradictions;}
        const double previous_input=latest_joint_input;
        h::HelperEvolvingFrequencyResult evolving;
        if(use_evolving_frequency&&receiver.last_joint_dwell_selected&&receiver.last_aided.valid&&previous_high.valid){
            ++evolving_attempts;
            const auto& r=receiver.last_aided;h::HelperEvolvingFrequencyInput e;
            // Fixed 0.4 s calendar and 0.36 s apertures: require the
            // previous accepted full-aperture end before this aperture.
            e.valid=r.thermal_covariance_valid&&r.process_covariance_valid&&std::isfinite(previous_input)&&
                previous_input+static_cast<double>(aperture+1)/(2*fs)<=r.measurement.first_sample_time_seconds&&
                std::isfinite(r.pilot_thermal_covariance[0][0])&&r.pilot_thermal_covariance[0][0]>=0&&
                std::isfinite(r.pilot_process_covariance[0][0])&&r.pilot_process_covariance[0][0]>=0;
            e.reference_preserved=previous_high.reference_preserved;e.previous_only=true;
            e.time_seconds=r.high_frequency_time_seconds;e.previous_observation_seconds=previous_input;
            e.forecast_hz=previous_high.frequency_hz;e.forecast_thermal_sigma_hz=previous_high.thermal_sigma_hz;
            e.forecast_process_sigma_hz=previous_high.process_sigma_hz;e.forecast_model_half_width_hz=previous_high.model_half_width_hz;
            e.forecast_conditional_half_width_hz=previous_high.conditional_half_width_hz;
            e.observed_hz=r.measurement.high_frequency_hz;
            e.observed_thermal_sigma_hz=std::sqrt(std::max(0.,r.pilot_thermal_covariance[0][0]));
            e.observed_process_sigma_hz=std::sqrt(std::max(0.,r.pilot_process_covariance[0][0]));
            e.observed_model_half_width_hz=r.high_frequency_model_half_width_hz;
            evolving=h::refine_helper_evolving_frequency(e);
            if(!evolving.valid)++evolving_fallbacks;
        }
        h::HelperSoftJointResult soft;
        if(use_soft_joint&&receiver.last_joint_fresh){
            ++soft_attempts;h::HelperSoftJointPrediction p;
            p.valid=previous.valid&&previous.model_consistent;p.previous_observations_only=true;
            p.reference_preserved=previous.reference_preserved;p.alternatives=static_cast<unsigned>(previous.alternatives);
            p.time_seconds=date;p.latest_observation_seconds=previous_input;p.reference_input_seconds=previous.reference_input_seconds;
            p.phase_difference_radians=previous.phase_difference_radians;p.frequency_difference_hz=1850*previous.audio_scale;
            const double k=1850e-6,pt=previous.phase_difference_thermal_sigma_radians,pp=previous.phase_difference_process_sigma_radians;
            const double ft=k*previous.audio_thermal_sigma_ppm;
            const double fp=k*std::sqrt(std::max(0.,previous.audio_stochastic_sigma_ppm*previous.audio_stochastic_sigma_ppm-previous.audio_thermal_sigma_ppm*previous.audio_thermal_sigma_ppm));
            p.thermal_covariance={{{2*pt*pt,0},{0,2*ft*ft}}};p.process_covariance={{{2*pp*pp,0},{0,2*fp*fp}}};
            p.phase_model_half_width_radians=previous.phase_difference_model_half_width_radians+.02;
            p.frequency_model_half_width_hz=k*previous.audio_model_half_width_ppm;
            soft=h::condition_helper_joint_dwell(*receiver.last_joint_fresh,p);
            if(!soft.valid)++soft_fallbacks;
        }
        if(use_joint){
            h::HelperJointEstimator::Input x;x.time_seconds=date;x.frequency_time_seconds=date;x.shift_hz=d.shift_hz;x.model_contradicted=frequency_contradiction;
            const auto& r=receiver.last_aided;
            x.valid=!missing&&r.valid&&r.thermal_covariance_valid&&r.frequency_dates_valid&&
                (!receiver.aided_config.compute_slope_covariance||r.process_covariance_valid);
            if(x.valid){x.time_seconds=r.center_time_seconds;x.frequency_time_seconds=r.high_frequency_time_seconds;
                x.separate_low_frequency_time=true;x.low_frequency_time_seconds=r.low_frequency_time_seconds;
                x.high_frequency_hz=r.measurement.high_frequency_hz;x.low_frequency_hz=r.measurement.low_frequency_hz;
                x.high_center_phase_radians=r.high_center_phase_radians;x.low_center_phase_radians=r.low_center_phase_radians;
                x.thermal_covariance=r.thermal_covariance;x.common_frequency_sigma_hz=r.measurement.common_phase_frequency_sigma_hz;
                x.process_covariance_valid=r.process_covariance_valid;
                if(x.process_covariance_valid)x.process_covariance=r.process_covariance;
                x.frequency_model_half_width_hz=std::max(r.high_frequency_model_half_width_hz,r.low_frequency_model_half_width_hz);
                x.phase_model_half_width_radians=receiver.joint_difference_model_half_width;
                x.sample_support_valid=true;x.first_sample_time_seconds=r.measurement.first_sample_time_seconds;
                x.end_sample_time_seconds=r.measurement.end_sample_time_seconds;
                ++joint_inputs;}
            if(use_recovery&&x.valid){
                if(const auto raw=frequency_epoch_raw(x,frequency_contradiction)){
                    raw_replay.push_back(*raw);if(raw_replay.size()>8)raw_replay.erase(raw_replay.begin());
                }else ++recovery_external_rejections;
            }
            auto updated=joint.observe(x);
            if(use_recovery&&!recovery_epoch_active&&!recovery_external_rejections&&recovery_state.epoch_requested){
                std::vector<h::HelperJointEstimator::Input> definite_post;
                for(const auto& raw:raw_replay)if(raw.sample_support_valid&&
                    raw.first_sample_time_seconds>=recovery_state.change_upper_seconds-1e-9)definite_post.push_back(raw);
                // Detector selection and joint likelihood use the same raw
                // samples as selection/evidence, not as independent copies.
                // The joint transaction deduplicates dates and omits every
                // whole summary intersecting the OUTER transition bracket.
                if(definite_post.size()>=3){++recovery_epoch_requests;
                    const double original_reference=updated.reference_input_seconds;
                    if(joint.begin_frequency_step(selected_step_model,
                        recovery_state.change_lower_seconds,recovery_state.change_upper_seconds,definite_post)){
                        const auto candidate=joint.predict(1600,d.shift_hz,date);
                        require(candidate.reference_initialized&&std::abs(candidate.reference_input_seconds-original_reference)<1e-12,
                            "frequency epoch changed the original source reference");
                        if(candidate.valid&&candidate.frequency_step_confirmed&&candidate.post_step_observations>=3){
                            recovery_epoch_active=true;epoch_committed_current=true;++recovery_epoch_accepted;joint_suspended=false;joint_recovery_streak=0;
                            updated=candidate;latest_accepted_count=static_cast<unsigned>(candidate.accepted_observations);
                            latest_joint_input=x.time_seconds;
                        }
                    }
                }
            }
            current_joint_accepted=x.valid&&(updated.accepted_observations>latest_accepted_count||
                (recovery_epoch_active&&updated.valid&&updated.frequency_step_confirmed));
            if(current_joint_accepted){latest_accepted_count=static_cast<unsigned>(updated.accepted_observations);latest_joint_input=x.time_seconds;}
            if(!frequency_contradiction&&internal_model_conflict(x.valid,joint_ever_established,current_joint_accepted,updated)){
                joint_suspended=true;joint_recovery_streak=0;++joint_model_contradictions;
            }
            const auto prediction_now=joint.predict(1600,d.shift_hz,date);
            joint_ever_established=joint_ever_established||prediction_now.valid;
            advance_recovery(joint_suspended,joint_recovery_streak,current_joint_accepted&&prediction_now.valid,innovation_available,innovation_consistent);
            joint_state=prediction_now;
            // Fresh selected phase may use only an already qualified previous
            // clock; it does not create a frequency/history observation.
            current_phase_only=receiver.transient_phase_selected&&!missing&&!joint_suspended&&
                !frequency_contradiction&&previous.valid&&previous.reference_preserved&&prediction_now.valid;
            if(current_phase_only)++phase_only_count;
            if(joint_suspended||(!current_joint_accepted&&!current_phase_only)){joint_state.valid=false;++joint_authority_rejections;}
            admitted_dwells[index]=joint_state.valid;fresh_joint_dwells[index]=current_joint_accepted;
            if(use_recovery&&!recovery_epoch_active&&!joint_suspended&&current_joint_accepted&&prediction_now.valid){
                const double low_rf=reference_rf>1e9?5725e6:28.775e6,high_rf=reference_rf>1e9?5875e6:31.225e6;
                detector_prior.capture(joint,date,latest_joint_input,low_rf,high_rf,use_extended_recovery);
            }
        }else{
            h::HelperRelativeClock::Input x;x.valid=!missing&&receiver.phase_from_aided&&receiver.phase.valid;
            x.time_seconds=date;
            if(x.valid){x.time_seconds=receiver.phase.date;x.high_phase_radians=receiver.phase.high;x.low_phase_radians=receiver.phase.low;x.phase_difference_sigma_radians=receiver.phase.difference_sigma;}
            old_clock=clock.observe(x);
        }
        if(evolving.valid&&current_joint_accepted&&!joint_suspended){
            receiver.phase.local_frequency_valid=true;receiver.phase.local_frequency_hz=evolving.frequency_hz;++evolving_accepted;
        }else if(evolving.valid)++evolving_fallbacks;
        if(soft.valid&&current_joint_accepted&&!joint_suspended){
            receiver.phase.high=soft.value[2];receiver.phase.low=soft.value[3];
            if(use_soft_rate){receiver.phase.local_frequency_valid=true;receiver.phase.local_frequency_hz=soft.value[0];}
            receiver.phase.high_sigma=soft.thermal_sigma[2]+soft.process_sigma[2]+soft.model_half_width[2]/3;
            receiver.phase.difference_sigma=soft.thermal_sigma[2]+soft.thermal_sigma[3]+soft.process_sigma[2]+soft.process_sigma[3]+(soft.model_half_width[2]+soft.model_half_width[3])/3;
            ++soft_accepted;
        }else if(soft.valid)++soft_fallbacks;
        if(use_dual&&!(soft.valid&&current_joint_accepted&&!joint_suspended)&&!epoch_committed_current&&!joint_suspended&&current_joint_accepted&&receiver.phase.valid&&receiver.last_aided.thermal_covariance_valid){
            const auto& r=receiver.last_aided;
            h::HelperDualPilotPhaseInput in;in.valid=true;in.time_seconds=r.center_time_seconds;
            in.high_phase_radians=r.high_center_phase_radians;in.low_phase_radians=r.low_center_phase_radians;
            in.thermal_covariance={{{r.pilot_thermal_covariance[2][2],r.pilot_thermal_covariance[2][3]},
                                    {r.pilot_thermal_covariance[3][2],r.pilot_thermal_covariance[3][3]}}};
            in.common_phase_sigma_radians=r.common_center_phase_sigma_radians;
            in.high_model_half_width_radians=receiver.joint_phase_model_half_width;
            // Predictor model radius covers PAST deviations. The NEW hop may
            // independently vary low-minus-high transfer about that unknown
            // constant by the declared bound; it is not averaged away.
            in.low_model_half_width_radians=.02;
            if(receiver.last_joint_dwell_selected&&r.process_covariance_valid){
                const auto& p=r.pilot_process_covariance;
                const double difference_process=std::max(0.,p[2][2]+p[3][3]-2*p[2][3]);
                // The dual gate's common term cancels exactly. Preserve the
                // candidate's noncommon process difference as a separate L1
                // five-sigma allowance rather than silently cancelling it.
                in.low_model_half_width_radians+=5*std::sqrt(difference_process);
            }
            h::HelperDualPilotPhasePrediction prediction;
            prediction.valid=previous.valid;prediction.previous_observations_only=true;prediction.reference_preserved=previous.reference_preserved;
            prediction.alternatives=static_cast<unsigned>(previous.alternatives);prediction.time_seconds=in.time_seconds;
            prediction.latest_observation_seconds=previous_input;prediction.reference_input_seconds=previous.reference_input_seconds;
            prediction.high_minus_low_radians=previous.phase_difference_radians;
            prediction.thermal_sigma_radians=previous.phase_difference_thermal_sigma_radians;
            prediction.process_sigma_radians=previous.phase_difference_process_sigma_radians;
            prediction.model_half_width_radians=previous.phase_difference_model_half_width_radians;
            // Current/prior white-noise samples occupy disjoint dwells. This
            // zero THERMAL cross covariance is conditional on that model; the
            // common oscillator term above is retained and cannot average out.
            prediction.current_prediction_covariance={0,0};
            const auto result=dual.observe(in,prediction);
            if(result.valid&&result.combined){receiver.phase.high=result.high_gauge_phase_radians;
                receiver.phase.high_sigma=result.thermal_sigma_radians+result.common_sigma_radians+result.prediction_process_sigma_radians+result.model_half_width_radians/3;
                ++dual_combined;
            }else{++dual_fallback;
                const auto failure=static_cast<std::size_t>(result.failure);
                if(failure<dual_failure_counts.size())++dual_failure_counts[failure];
                if(result.failure==h::HelperDualPilotPhaseFailure::inconsistent_difference)++dual_inconsistent;}
        }
        if(index==89){
            if(use_joint){const auto j=joint.predict(1600,reference_rf,36);const auto high=joint.predict(2400,reference_rf,36),low=joint.predict(550,reference_rf,36);
                if(!joint_suspended&&current_joint_accepted&&j.valid&&high.valid&&low.valid)frozen={true,j.frequency_hz,j.thermal_sigma_hz,j.model_half_width_hz,j.conditional_half_width_hz,
                    high.frequency_hz,low.frequency_hz,j.audio_scale,j.source_seconds,j.reference_input_seconds};
                if(frozen.valid){frozen.thermal_available=true;frozen.process_sigma=j.process_sigma_hz;frozen.stochastic_sigma=j.stochastic_sigma_hz;
                    frozen.recent_guard=j.recent_guard_available;frozen.enclosure_extra=std::max(0.,j.conditional_half_width_hz-(3*j.stochastic_sigma_hz+j.model_half_width_hz));
                    frozen.audio_drift=j.audio_drift_ppm_per_second;
                    frozen.shift_ppm=(j.frequency_hz-1600*j.audio_scale)/reference_rf*1e6;
                    const auto future=joint.predict(1600,reference_rf,36.5);
                    if(future.valid)frozen.shift_drift=2*((future.frequency_hz-1600*(j.audio_scale+.5e-6*frozen.audio_drift))/reference_rf*1e6-frozen.shift_ppm);}
            }else if(receiver.frozen){const auto model=*receiver.frozen;const auto f=coefficient(model,reference_rf,36);
                const double dt=36-model.time_seconds,row[]{1600e-6,reference_rf*1e-6,1600e-6*dt,reference_rf*1e-6*dt};double variance=0;
                for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)variance+=row[a]*model.covariance[a][b]*row[b];
                frozen={true,1600*f.audio+f.offset,std::sqrt(std::max(0.,variance)),0,3*std::sqrt(std::max(0.,variance)),
                    2400*f.audio+f.offset,550*f.audio+f.offset,f.audio,36,36};
                frozen.audio_drift=model.audio_drift_ppm_per_second;frozen.shift_ppm=model.shift_ppm+(36-model.time_seconds)*model.shift_drift_ppm_per_second;frozen.shift_drift=model.shift_drift_ppm_per_second;}
        }
        extra_cpu+=cpu()-started;
        if(index==89)extra_cpu36=extra_cpu;
    }
    void evaluate(const ContinuationSource& source,const hp::Dwell& d,unsigned index){
        receiver.evaluate_phase(source,d,index);
        if(receiver.phase.valid){
            const double sample=(receiver.phase.date-d.first_seconds)*fs;
            const auto i=static_cast<std::size_t>(std::floor(sample));const double f=sample-static_cast<double>(i);
            const double common=std::arg(C(d.common_phasor[i]))+f*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
            const double true_h=wrap(tau*std::remainder(2400*source.tx_time(receiver.phase.date),1.)+common);
            const double error=wrap(receiver.phase.high-true_h);
            const auto& settled=d.settled_common_phasor.empty()?d.common_phasor:d.settled_common_phasor;
            const double settled_common=std::arg(C(settled[i]))+f*wrap(std::arg(C(settled[i+1]))-std::arg(C(settled[i])));
            const double settled_truth=wrap(tau*std::remainder(2400*source.tx_time(receiver.phase.date),1.)+settled_common);
            settled_phase.add(wrap(receiver.phase.high-settled_truth),receiver.phase.high_sigma);
            evaluator_phase_error[index]=error;evaluator_phase_available[index]=true;
            current_phase.add(error,receiver.phase.high_sigma);phase_errors.push_back(std::abs(error));
            if(index>=95)heldout_phase.add(error,receiver.phase.high_sigma);
            const double true_diff=wrap(tau*std::remainder(1850*source.tx_time(receiver.phase.date),1.));
            const auto& raw=receiver.last_aided;
            if(raw.valid)difference.add(wrap(raw.high_center_phase_radians-raw.low_center_phase_radians-true_diff),raw.phase_difference_sigma_radians);
        }
        if(index==89&&frozen.valid){const double truth=1600*source.audio_scale_at(36)+source.shift_error_hz(reference_rf,36);frozen_error=frozen.frequency-truth;}
        if(index>=95&&frozen.valid){
            // Predict later RF using ONLY the frozen model and held RF command.
            const double date=d.first_seconds+.2,dt=date-36;
            const double prediction=1600*(frozen.audio+1e-6*frozen.audio_drift*dt)+
                d.shift_hz*(frozen.shift_ppm+frozen.shift_drift*dt)*1e-6;
            const double truth=1600*source.audio_scale_at(d.first_seconds+.2)+source.shift_error_hz(d.shift_hz,d.first_seconds+.2);
            const double error=prediction-truth;++heldout_count;frozen_heldout_square+=error*error;frozen_heldout_max=std::max(frozen_heldout_max,std::abs(error));
        }
        if(!use_joint&&old_clock.valid){
            const double gauge=old_clock.reference_input_seconds-source.tx_time(old_clock.reference_input_seconds);
            const double error=old_clock.source_seconds-source.tx_time(old_clock.input_seconds)-gauge;
            ++joint_qualified;if(std::abs(error)>old_clock.source_half_width_seconds)++clock_outside;clock_error_max=std::max(clock_error_max,std::abs(error));
        }
        if(use_joint&&joint_state.valid){
            const double gauge=joint_state.reference_input_seconds-source.tx_time(joint_state.reference_input_seconds);
            const double error=joint_state.source_seconds-source.tx_time(joint_state.time_seconds)-gauge;
            const double radius=3*joint_state.source_stochastic_sigma_seconds+joint_state.source_model_half_width_seconds;
            ++joint_qualified;if(std::abs(error)>radius)++clock_outside;clock_error_max=std::max(clock_error_max,std::abs(error));
        }
    }
    void condition(const hp::Dwell& d,bool missing,unsigned index,const ContinuationSource& source){
        if(index<94)return;
        const auto state=receiver.snapshot(true);const auto now=cpu();
        Coeff c;h::HelperRelativeClock::Estimate coordinate;
        if(use_joint){const auto high=joint.predict(2400,d.shift_hz,d.first_seconds+.2);coordinate=clock_at(d.first_seconds+.2,d.shift_hz);
            c={coordinate.audio_scale,high.frequency_hz-2400*coordinate.audio_scale};
        }else{coordinate=clock.predict(d.first_seconds+.2);c=state.valid?coefficient(state,d.shift_hz,d.first_seconds+.2):Coeff{};
            if(coordinate.valid)c={coordinate.audio_scale,c.offset+2400*(c.audio-coordinate.audio_scale)};}
        if(receiver.phase.local_frequency_valid)c.offset=receiver.phase.local_frequency_hz-2400*c.audio;
        const auto anchor=clock_at(receiver.phase.date,d.shift_hz);
        const auto initial=clock_at(d.first_seconds,d.shift_hz);
        const bool usable_coefficients=coordinate.valid&&(use_joint?joint_state.valid:state.valid);
        if(usable_coefficients){retained_correction=c;retained_rf=d.shift_hz;have_retained_correction=true;}
        else if(have_retained_correction){c=retained_correction;c.offset*=d.shift_hz/retained_rf;}
        else c={};
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        extra_cpu+=cpu()-now;
        if(!stream.initialized&&usable_coefficients&&!missing&&receiver.phase.valid&&initial.valid&&initial.reference_preserved&&(use_joint?joint_state.valid:state.valid))
            stream.initialize(first,c,initial.source_seconds,initial.reference_input_seconds,source);
        const auto guard_started=cpu();
        Phase corrected_phase=receiver.phase;
        if(stream.conditional_guard&&use_joint&&usable_coefficients&&corrected_phase.valid){
            const bool selected=corrected_phase.support_end>corrected_phase.support_first;
            const double start=d.first_seconds+static_cast<double>(selected?corrected_phase.support_first:settle+phase_margin)/fs;
            const double end=d.first_seconds+static_cast<double>(selected?corrected_phase.support_end:settle+aperture-phase_margin)/fs;
            double rf=0,audio=0;bool valid=true;
            for(double t:{start,corrected_phase.date,end}){
                const auto j=joint.predict(2400,d.shift_hz,t);
                if(!j.valid||!j.reference_preserved||j.frequency_step_active){valid=false;break;}
                rf=std::max(rf,std::abs(j.frequency_hz-(2400*c.audio+c.offset))+j.conditional_half_width_hz);
                audio=std::max(audio,std::abs(j.audio_scale-c.audio)+1e-6*(3*j.audio_stochastic_sigma_ppm+j.audio_model_half_width_ppm));
            }
            corrected_phase.transport_budget_valid=valid&&std::isfinite(rf)&&std::isfinite(audio);
            corrected_phase.transport_rate=tau*(rf+980*audio);
            corrected_phase.diffusion_q=receiver.aided_config.phase_noise_radians_per_sqrt_second;
        }
        extra_cpu+=cpu()-guard_started;
        if(stream.initialized)stream.feed(d,c,corrected_phase,(use_joint?joint_state.valid:state.valid)&&!missing&&coordinate.valid,source,anchor);
    }
};

double quantile(std::vector<double> values,double p){
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    const auto n=static_cast<std::size_t>(std::ceil(p*static_cast<double>(values.size())));
    return values[std::min(values.size()-1,n?n-1:0)];
}
std::string dwell_mask(const std::array<bool,100>& flags){std::string out;out.reserve(flags.size());for(bool x:flags)out+=x?'1':'0';return out;}
void print_chain(Chain& chain,const ContinuationSource& source,const std::string& impairment,
                 double generation,double generation_setup,double metrology,const Metrics& m,const Evm& evm){
    const auto& r=chain.receiver;const auto& stream=chain.stream;
    std::cout<<std::setprecision(10)<<"helper_continuation method="<<chain.name<<" mode="<<impairment
        <<" seed="<<source.config().seed<<" loss_db="<<source.config().path_loss_db<<" schedule="<<schedule(source.config())
        <<" schedule_key_namespace="<<(source.config().expanded_schedule_key?"expanded_v3":"legacy")
        <<" helper_w="<<source.config().helper_w<<" probe_w="<<source.config().probe_w
        <<" probe="<<(source.config().probe==hp::Probe::tone?"tone":"apsk64")
        <<" temperature_K="<<source.config().temperature_kelvin<<" noise_figure_db="<<source.config().noise_figure_db
        <<" high_cn0_db_hz="<<source.base().high_cn0_db_hz()<<" probe_cn0_db_hz="<<source.base().probe_cn0_db_hz()
        <<" simulator_scaled_step="<<source.has_scaled_step()
        <<" simulator_step_begin_seconds="<<source.scaled_step_begin_seconds()
        <<" simulator_step_reference_hz="<<source.scaled_step_reference_hz()
        <<" simulator_physical_step_hz_at_reference="<<source.scaled_step_physical_hz()
        <<" simulator_audio_evolution_ppm_per_second="<<source.audio_evolution()
        <<" simulator_audio_jump_seconds="<<source.audio_jump()
        <<" simulator_audio_stress_rf_calendar=independent_retained_key_calendar"
        <<" local_frequency_truth="<<(impairment.starts_with("transient-")?"instantaneous_not_aperture_average_for_disturbed_dwells":"modeled_instantaneous")
        <<" observation_seconds=36 lead_seconds=38 capture_seconds=40 buffering_latency_seconds=.4"
        <<" broad_calls="<<r.broad_calls<<" aided_calls="<<r.aided_calls<<" fallback_calls="<<r.fallbacks
        <<" joint_window_uncached="<<r.use_uncached_joint
        <<" joint_dwell_enabled="<<r.use_joint_dwell<<" joint_dwell_attempts="<<r.joint_dwell_attempts
        <<" joint_dwell_accepted="<<r.joint_dwell_accepted<<" joint_dwell_fallbacks="<<r.joint_dwell_fallbacks
        <<" piecewise_enabled="<<r.use_piecewise<<" piecewise_attempts="<<r.piecewise_attempts
        <<" piecewise_detected="<<r.piecewise_detected<<" piecewise_selected="<<r.piecewise_selected
        <<" piecewise_withheld="<<r.piecewise_withheld<<" piecewise_cpu_seconds="<<r.piecewise_cpu
        <<" transient_phase_enabled="<<r.use_transient_phase<<" transient_attempts="<<r.transient_attempts
        <<" transient_detections="<<r.transient_detections<<" transient_selected="<<r.transient_selected_count
        <<" transient_withheld="<<r.transient_withheld_count
        <<" evolving_frequency_enabled="<<chain.use_evolving_frequency<<" evolving_attempts="<<chain.evolving_attempts
        <<" evolving_accepted="<<chain.evolving_accepted<<" evolving_fallbacks="<<chain.evolving_fallbacks
        <<" soft_joint_enabled="<<chain.use_soft_joint<<" soft_rate_enabled="<<chain.use_soft_rate<<" soft_joint_attempts="<<chain.soft_attempts
        <<" soft_joint_accepted="<<chain.soft_accepted<<" soft_joint_fallbacks="<<chain.soft_fallbacks
        <<" transient_phase_only_authorized="<<chain.phase_only_count
        <<" conditional_band_guard="<<stream.conditional_guard<<" band_guard_rejections="<<stream.guard_rejections
        <<" band_guard_shortened="<<stream.guard_shortened
        <<" joint_dwell_cpu_seconds="<<r.joint_dwell_cpu<<" transient_cpu_seconds="<<r.transient_cpu
        <<" valid_dwells="<<r.valid<<" residual_rejections="<<r.residual_rejections
        <<" phase_evolution_rejections="<<r.evolution_rejections<<" signal_rejections="<<r.signal_rejections
        <<" frequency_restarts="<<r.model_restarts
        <<" slope_covariance_enabled="<<r.aided_config.compute_slope_covariance
        <<" covariance_frequency_errors="<<r.covariance_frequency_errors
        <<" covariance_frequency_rms_hz="<<(r.covariance_frequency_errors?std::sqrt(r.covariance_frequency_error2/r.covariance_frequency_errors):0)
        <<" covariance_stochastic_sigma_rms_hz="<<(r.covariance_frequency_errors?std::sqrt(r.covariance_frequency_sigma2/r.covariance_frequency_errors):0)
        <<" covariance_frequency_outside3sigma="<<r.covariance_frequency_outside_three_sigma
        <<" frequency36_available="<<chain.frozen.valid;
    if(chain.frozen.valid){
        std::cout<<" frequency36_error_hz="<<chain.frozen_error
            <<" frequency36_covariance_sigma_hz="<<chain.frozen.thermal_sigma
            <<" frequency36_thermal_sigma_available="<<chain.frozen.thermal_available;
        if(chain.frozen.thermal_available)std::cout<<" frequency36_thermal_sigma_hz="<<chain.frozen.thermal_sigma
            <<" frequency36_process_sigma_hz="<<chain.frozen.process_sigma
            <<" frequency36_stochastic_sigma_hz="<<chain.frozen.stochastic_sigma
            <<" frequency36_recent_guard="<<chain.frozen.recent_guard
            <<" frequency36_recent_enclosure_extra_hz="<<chain.frozen.enclosure_extra;
        if(chain.frozen.thermal_available)std::cout<<" frequency36_model_half_width_hz="<<chain.frozen.model_half;
        else std::cout<<" frequency36_model_half_width_available=0";
        std::cout<<" frequency36_conditional_half_width_hz="<<chain.frozen.half_width
            <<" frequency36_covered="<<(std::abs(chain.frozen_error)<=chain.frozen.half_width);
    }
    std::cout<<" dwell_frequency_errors="<<r.frequency_errors
        <<" dwell_frequency_rms_hz="<<(r.frequency_errors?std::sqrt(r.frequency_error2/r.frequency_errors):0)
        <<" dwell_frequency_max_hz="<<r.frequency_error_max
        <<" dwell_frequency_reported_sigma_rms_hz="<<(r.frequency_errors?std::sqrt(r.frequency_sigma2/r.frequency_errors):0)
        <<" dwell_frequency_outside3sigma="<<r.frequency_outside_three_sigma
        <<" hop_phase_count="<<chain.current_phase.count<<" hop_phase_rms_deg="<<chain.current_phase.rms()*180/std::numbers::pi
        <<" hop_phase_p90_deg="<<quantile(chain.phase_errors,.90)*180/std::numbers::pi
        <<" hop_phase_p99_deg="<<quantile(chain.phase_errors,.99)*180/std::numbers::pi
        <<" hop_phase_max_deg="<<chain.current_phase.maximum*180/std::numbers::pi
        <<" heldout_hop_phase_count="<<chain.heldout_phase.count
        <<" heldout_hop_phase_rms_deg="<<chain.heldout_phase.rms()*180/std::numbers::pi
        <<" heldout_hop_phase_max_deg="<<chain.heldout_phase.maximum*180/std::numbers::pi
        <<" evaluator_counterfactual_phase_count="<<chain.settled_phase.count
        <<" evaluator_counterfactual_phase_rms_deg="<<chain.settled_phase.rms()*180/std::numbers::pi
        <<" evaluator_counterfactual_phase_max_deg="<<chain.settled_phase.maximum*180/std::numbers::pi
        <<" phase_difference_rms_deg="<<chain.difference.rms()*180/std::numbers::pi
        <<" phase_difference_sigma_rms_deg="<<chain.difference.sigma_rms()*180/std::numbers::pi
        <<" clock_reference_initialized="<<(chain.use_joint?chain.joint_state.reference_initialized:chain.old_clock.reference_initialized)
        <<" clock_reference_preserved="<<(chain.use_joint?chain.joint_state.reference_preserved:chain.old_clock.reference_preserved)
        <<" clock_reference_input_seconds="<<(chain.use_joint?chain.joint_state.reference_input_seconds:chain.old_clock.reference_input_seconds)
        <<" clock_alternatives="<<(chain.use_joint?chain.joint_state.alternatives:chain.old_clock.alternatives)
        <<" clock_ambiguity_limited="<<(chain.use_joint?chain.joint_state.ambiguity_limited:chain.old_clock.ambiguity_limited)
        <<" clock_work_limited="<<(chain.use_joint?chain.joint_state.work_limited:chain.old_clock.work_limited)
        <<" clock_model_consistent="<<(chain.use_joint?chain.joint_state.model_consistent:chain.old_clock.model_consistent)
        <<" clock_accepted_observations="<<(chain.use_joint?chain.joint_state.accepted_observations:chain.old_clock.accepted_observations)
        <<" clock_rejected_observations="<<(chain.use_joint?chain.joint_state.rejected_observations:chain.old_clock.rejected_observations)
        <<" clock_qualified="<<chain.joint_qualified<<" clock_interval_misses="<<chain.clock_outside
        <<" clock_error_max_us="<<chain.clock_error_max*1e6
        <<" joint_current_observation_accepted="<<chain.current_joint_accepted
        <<" joint_ever_established="<<chain.joint_ever_established
        <<" joint_suspended="<<chain.joint_suspended<<" joint_frequency_contradictions="<<chain.joint_contradictions
        <<" joint_internal_model_contradictions="<<chain.joint_model_contradictions
        <<" joint_authority_rejections="<<chain.joint_authority_rejections
        <<" admitted_dwell_mask="<<dwell_mask(chain.admitted_dwells)
        <<" fresh_joint_dwell_mask="<<dwell_mask(chain.fresh_joint_dwells)
        <<" frequency_recovery_enabled="<<chain.use_recovery
        <<" diagnostic_prior_horizon_seconds="<<(chain.use_extended_recovery?3:2)
        <<" diagnostic_prior_curvature_bound_hz_per_second2="<<(chain.use_extended_recovery?.002:0)
        <<" diagnostic_coordinate_extrapolations="<<chain.diagnostic_extrapolations
        <<" phase_branch_propagation_enabled="<<chain.use_gap_propagation
        <<" phase_branch_propagation_used="<<chain.joint_state.phase_branch_propagation_used
        <<" phase_branch_gap_seconds="<<chain.joint_state.phase_branch_gap_seconds
        <<" post_gap_qualification_pending="<<chain.joint_state.post_gap_qualification_pending
        <<" post_gap_observations="<<chain.joint_state.post_gap_observations
        <<" recovery_candidate_observations="<<chain.recovery_state.candidate_observations
        <<" recovery_provisional="<<chain.recovery_state.provisional
        <<" recovery_epoch_proposed="<<chain.recovery_state.epoch_requested
        <<" recovery_epoch_requests="<<chain.recovery_epoch_requests<<" recovery_epoch_accepted="<<chain.recovery_epoch_accepted
        <<" recovery_external_rejections="<<chain.recovery_external_rejections
        <<" recovery_epoch_active="<<chain.recovery_epoch_active
        <<" recovery_change_lower_seconds="<<chain.recovery_state.change_lower_seconds
        <<" recovery_change_upper_seconds="<<chain.recovery_state.change_upper_seconds
        <<" recovery_detector_delta_hz="<<chain.recovery_state.additive_correction_hz
        <<" recovery_selection=conditional_predeclared_step_model"
        <<" recovery_step_model="<<(chain.selected_step_model==h::HelperJointEstimator::FrequencyStepModel::rf_scaled?"rf_scaled":"common_hz")
        <<" recovery_proposal_model=additive_common_hz_compatible"
        <<" dual_combined="<<chain.dual_combined<<" dual_fallback="<<chain.dual_fallback<<" dual_difference_rejections="<<chain.dual_inconsistent
        <<" dual_cycle_ambiguity="<<chain.dual_failure_counts[5]
        <<" dual_no_benefit="<<chain.dual_failure_counts[7]
        <<" dual_uncertainty_too_large="<<chain.dual_failure_counts[8]
        <<" phase_anchors="<<stream.anchors<<" heldout_anchor_mask="<<stream.heldout_anchor_mask
        <<" supported_samples="<<stream.supported_samples<<" output_samples="<<stream.output_samples
        <<" supported_input_span_first_seconds="<<(stream.first_supported_input?static_cast<double>(*stream.first_supported_input/fs):0)
        <<" supported_input_span_last_seconds="<<(stream.last_supported_input?static_cast<double>(*stream.last_supported_input/fs):0)
        <<" heldout_support_input_count_first_last_by_dwell="<<stream.heldout_support_dates()
        <<" conditioning_first_input_seconds="<<stream.first_input_seconds
        <<" reference_input_seconds="<<stream.reference_input<<" evaluator_constant_gauge_seconds="<<stream.gauge
        <<" remaining_ambiguity=one_constant_unknown_time_and_phase"
        <<" timing_max_us="<<stream.timing_error_max*1e6<<" timing_change_us="<<(stream.last_timing_error-stream.first_timing_error)*1e6
        <<" cycle_slips="<<stream.cycle_slips<<" cycle_error_observations="<<stream.cycle_error_observations
        <<" tone_windows="<<m.windows<<" full_tone_windows="<<m.full_windows
        <<" twin_frequency_rms_hz="<<m.frequency_rms<<" twin_frequency_max_hz="<<m.frequency_max
        <<" twin_phase_rms_deg="<<m.phase_rms*180/std::numbers::pi<<" twin_phase_max_deg="<<m.phase_max*180/std::numbers::pi
        <<" admitted_tone_samples="<<m.sample_count<<" admitted_sample_phase_max_deg="<<m.sample_phase_max*180/std::numbers::pi
        <<" evaluator_support_counter_scope="<<(source.config().probe==hp::Probe::tone?"produced_tone_trace_38.02_to_39.98_only":"unavailable_for_apsk")
        <<" evaluator_physical_disturbance_supported_samples="<<m.physically_disturbed_supported
        <<" evaluator_physical_clean_supported_samples="<<m.physically_clean_supported
        <<" evaluator_physical_disturbance_withheld_samples="<<m.physically_disturbed_withheld
        <<" evaluator_physical_clean_withheld_samples="<<m.physically_clean_withheld
        <<" noisy_tone_windows="<<m.noisy_windows<<" noisy_frequency_rms_hz="<<m.noisy_frequency_rms<<" noisy_frequency_fit_se_hz="<<m.noisy_fit_se
        <<" apsk_symbols="<<evm.symbols<<" noisy_apsk_evm="<<evm.noisy<<" twin_apsk_evm="<<evm.clean
        <<" frozen_model_heldout_count="<<chain.heldout_count
        <<" frozen_model_heldout_frequency_rms_hz="<<(chain.heldout_count?std::sqrt(chain.frozen_heldout_square/chain.heldout_count):0)
        <<" frozen_model_heldout_frequency_max_hz="<<chain.frozen_heldout_max
        <<" conditioning_frequency=continued_helper_samples"
        <<" receiver_cpu36_seconds="<<(r.cpu36+chain.extra_cpu36)
        <<" receiver_cpu40_seconds="<<(r.total_cpu()+chain.extra_cpu+stream.cpu_seconds)
        <<" startup_cpu_seconds="<<r.startup_cpu<<" fallback_cpu_seconds="<<r.fallback_cpu
        <<" correlations_cpu_seconds="<<r.tracking_cpu<<" global_fit_cpu_seconds="<<r.fit_cpu
        <<" clock_joint_dual_cpu_seconds="<<chain.extra_cpu
        <<" conditioning_cpu_seconds="<<stream.cpu_seconds<<" conditioning_signal_seconds="<<stream.input_samples/fs
        <<" generation_cpu_seconds="<<generation<<" metrology_cpu_seconds="<<metrology
        <<" simulation_setup_cpu_seconds="<<generation_setup
        <<" evaluator_phase_cpu_seconds="<<chain.evaluator_cpu
        <<" evaluator_disturbance_mask_cpu_seconds="<<stream.evaluator_mask_cpu_seconds
        <<" evaluator_stream_total_cpu_seconds="<<stream.evaluator_stream_cpu_seconds<<'\n';
}

struct MethodOutcome {
    std::string name;bool frozen=false,reference_initialized=false,reference_preserved=false,suspended=false;
    unsigned windows=0,anchors=0,mask=0,cycle_slips=0,contradictions=0,clock_misses=0,authority_rejections=0;
    double reference=0,frequency_max=0,phase_max=0,first_input_seconds=0;
    bool epoch_active=false;unsigned epochs_accepted=0,epoch_requests=0;
    double frozen_frequency=0,frozen_half_width=0;bool selected_scaled_step=false;
    std::array<bool,100> admitted_dwells{};
    double clock_error_max=0;
};
struct Outcome {unsigned qualified36=0,window_count=0,cycle_slips=0;double maximum_admitted_phase=0;std::vector<MethodOutcome> methods;
    const MethodOutcome& at(const std::string& name)const{for(const auto& x:methods)if(x.name==name)return x;throw std::runtime_error("missing method outcome");}
};
Outcome scenario(hp::Config config,ht::Impairments impairment,const std::string& mode,const std::string& methods,bool required=false){
    const auto generation_setup_started=cpu();
    ContinuationSource source(config,impairment,mode=="frequency-pulse",mode=="scaled-step",36.8,
        mode=="gap-evolution"?.04:(mode=="gap-evolution-outside"?.6:0),
        mode=="gap-clock-cycle"?1./1850:0,mode=="second-step",mode=="step-invalid"?95:100,
        mode.starts_with("transient-")?std::string_view(mode).substr(10):std::string_view{});
    const double generation_setup=cpu()-generation_setup_started;
    const double max_rf=config.shift_center_hz>1e9?(config.microwave_eu20?5868.4e6:5846.8e6):31.225e6;
    const double prior_ppm=config.shift_center_hz>1e9?.0001:1;
    const double width=3*(max_rf*prior_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        config.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    using Method=h::HelperAidedFrequencyMethod;
    std::vector<std::unique_ptr<Chain>> chains;
    const auto add=[&](const std::string& name,Method method,bool fitted,bool dual,bool covariance=false,bool recovering=false,
                       bool extended_recovery=false,bool gap_propagation=false,bool joint_dwell=false,bool transient_phase=false){
        const auto before=cpu();auto c=std::make_unique<Chain>(name,method,fitted,dual,mc,config.shift_center_hz,covariance,recovering,
            mode=="scaled-step"?h::HelperJointEstimator::FrequencyStepModel::rf_scaled:h::HelperJointEstimator::FrequencyStepModel::common_hz,
            extended_recovery,gap_propagation);
        c->receiver.use_joint_dwell=joint_dwell||((methods=="soft-v5"||methods=="soft-rate-v5")&&name=="soft-joint-v5");
        c->use_soft_joint=(methods=="soft-v5"||methods=="soft-rate-v5")&&name=="soft-joint-v5";
        c->use_evolving_frequency=methods=="evolve-v6"&&name=="evolving-v6";
        c->use_soft_rate=methods=="soft-rate-v5"&&name=="soft-joint-v5";
        c->receiver.use_transient_phase=transient_phase;
        c->receiver.use_uncached_joint=methods=="cache-pair-v6"&&name=="joint-uncached-v6";
        c->receiver.use_piecewise=methods=="piecewise-v6"&&name=="piecewise-v6";
        c->stream.conditional_guard=methods=="joint-v5"||methods=="joint-v5-only"||methods=="transient-v5"||methods=="transient-v5-only"||methods=="soft-v5"||methods=="soft-rate-v5"||methods=="evolve-v6"||methods=="piecewise-v6"||methods=="cache-pair-v6";
        c->extra_cpu+=cpu()-before;chains.push_back(std::move(c));};
    if(methods=="all"||methods=="finalists"||methods=="legacy")add("legacy-clock",Method::subaperture_slope,false,false);
    if(methods=="all"||methods=="covariance")add("legacy-cov-clock",Method::subaperture_slope,false,false,true);
    if(methods=="all"||methods=="finalists"||methods=="joint"||methods=="phase-pair")add("legacy-joint",Method::subaperture_slope,true,false,true);
    if(methods=="all"||methods=="finalists"||methods=="dual"||methods=="phase-pair")add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    if(methods=="soft-v5"||methods=="soft-rate-v5"){add("soft-joint-v5",Method::subaperture_slope,true,true,true);add("legacy-joint-dual",Method::subaperture_slope,true,true,true);}
    if(methods=="cache-pair-v6"){
        add("joint-cached-v6",Method::subaperture_slope,true,true,true,false,false,false,true);
        add("joint-uncached-v6",Method::subaperture_slope,true,true,true,false,false,false,true);
    }
    if(methods=="evolve-v6"){
        add("evolving-v6",Method::subaperture_slope,true,true,true,false,false,false,true);
        add("joint-dwell-v5",Method::subaperture_slope,true,true,true,false,false,false,true);
        add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    }
    if(methods=="joint-v5"||methods=="joint-v5-only")add("joint-dwell-v5",Method::subaperture_slope,true,true,true,false,false,false,true);
    if(methods=="joint-v5")add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    if(methods=="piecewise-v6"){
        add("piecewise-v6",Method::subaperture_slope,true,true,true);
        add("transient-phase-v5",Method::subaperture_slope,true,true,true,false,false,false,false,true);
        add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    }
    if(methods=="transient-v5"||methods=="transient-v5-only")add("transient-phase-v5",Method::subaperture_slope,true,true,true,false,false,false,false,true);
    if(methods=="transient-v5")add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    if(methods=="all"||methods=="finalists"||methods=="partitioned")add("partitioned-joint-dual",Method::partitioned_hann,true,true);
    if(methods=="recovery"||methods=="recovery-only")add("legacy-joint-dual-recovery",Method::subaperture_slope,true,true,true,true);
    if(methods=="recovery"){
        add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
        add("legacy-clock",Method::subaperture_slope,false,false);
    }
    if(methods=="gap"||methods=="gap-only")add("legacy-joint-dual-gap",Method::subaperture_slope,true,true,true,false,false,true);
    if(methods=="gap")add("legacy-joint-dual",Method::subaperture_slope,true,true,true);
    if(methods=="recovery-extended"||methods=="recovery-extended-only")add("legacy-joint-dual-recovery-extended",Method::subaperture_slope,true,true,true,true,true);
    if(methods=="recovery-extended")add("legacy-joint-dual-recovery",Method::subaperture_slope,true,true,true,true);
    require(!chains.empty(),"unknown method set");
    double generation=0,oracle_cpu=0;Stream oracle;
    for(unsigned dwell=0;dwell<100;++dwell){
        const auto before=cpu();const auto d=source.generate(dwell);generation+=cpu()-before;
        const bool missing=source.missing(dwell);
        for(auto& chain:chains){chain->observe(d,missing,dwell);const auto evaluation_started=cpu();
            chain->evaluate(source,d,dwell);chain->evaluator_cpu+=cpu()-evaluation_started;
            chain->condition(d,missing,dwell,source);}
        if(mode=="step-debug"&&dwell>=89){for(const auto& chain:chains)if(chain->use_recovery){
            const auto& r=chain->receiver;
            std::cout<<std::setprecision(10)<<"helper_continuation_debug dwell="<<dwell<<" method="<<chain->name
                <<" global_prior="<<r.last_global_prior_valid<<" supplied_prior="<<r.last_supplied_prior_valid
                <<" measurement="<<r.measurement.valid<<" aided_phase="<<r.phase_from_aided
                <<" thermal="<<r.last_aided.thermal_covariance_valid<<" process="<<r.last_aided.process_covariance_valid
                <<" dates="<<r.last_aided.frequency_dates_valid<<" joint_current="<<chain->current_joint_accepted
                <<" joint_valid="<<chain->joint_state.valid<<" joint_model="<<chain->joint_state.model_consistent
                <<" joint_accepted="<<chain->joint_state.accepted_observations<<" suspended="<<chain->joint_suspended
                <<" recovery_count="<<chain->recovery_state.candidate_observations<<" provisional="<<chain->recovery_state.provisional
                <<" epoch_proposed="<<chain->recovery_state.epoch_requested<<" epoch_active="<<chain->recovery_epoch_active
                <<" epoch_accepted="<<chain->recovery_epoch_accepted<<" mask="<<chain->stream.heldout_anchor_mask<<'\n';
        }}
        // Entirely evaluator-only ideal-reference conditioner on the SAME
        // noisy samples and probe twin. No truth or oracle output reaches any
        // receiver above. It has its own one fixed coordinate gauge at36.
        if(dwell>=94&&!impairment.noise_only){const auto started=cpu();
            const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
            const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
            const Coeff truth{source.audio_scale_at(date),source.shift_error_hz(d.shift_hz,date)};
            const double gauge=36-source.tx_time(36);
            if(!oracle.initialized)oracle.initialize(first,truth,source.tx_time(d.first_seconds)+gauge,36,source);
            const double sample=(date-d.first_seconds)*fs;
            const auto i=static_cast<std::size_t>(std::floor(sample));const double f=sample-static_cast<double>(i);
            const double common=std::arg(C(d.common_phasor[i]))+f*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
            Phase phase{true,date,wrap(tau*std::remainder(2400*source.tx_time(date),1.)+common),0,0,0};
            oracle.feed(d,truth,phase,true,source);oracle_cpu+=cpu()-started;
        }
    }
    const auto oracle_metrology_started=cpu();
    const auto oracle_tone=config.probe==hp::Probe::tone?tone_metrics(oracle.trace,oracle.gauge):Metrics{};
    const auto oracle_evm=config.probe==hp::Probe::apsk64?evm_metrics(oracle.trace,source.base(),oracle.gauge):Evm{};
    std::cout<<std::setprecision(10)<<"helper_continuation_oracle seed="<<config.seed<<" loss_db="<<config.path_loss_db<<" mode="<<mode
        <<" scope=evaluator_only_same_capture_fixed36_gauge_midpoint_model oracle_cpu_seconds="<<oracle_cpu
        <<" oracle_metrology_cpu_seconds="<<cpu()-oracle_metrology_started
        <<" conditioning_signal_seconds="<<oracle.input_samples/fs<<" supported_samples="<<oracle.supported_samples
        <<" tone_windows="<<oracle_tone.full_windows<<" twin_frequency_rms_hz="<<oracle_tone.frequency_rms
        <<" twin_phase_rms_deg="<<oracle_tone.phase_rms*180/std::numbers::pi
        <<" noisy_frequency_rms_hz="<<oracle_tone.noisy_frequency_rms<<" apsk_symbols="<<oracle_evm.symbols
        <<" noisy_apsk_evm="<<oracle_evm.noisy<<" twin_apsk_evm="<<oracle_evm.clean<<'\n';
    Outcome result;
    for(auto& chain:chains){
        const auto before=cpu();const auto tone=config.probe==hp::Probe::tone?tone_metrics(chain->stream.trace,chain->stream.gauge):Metrics{};
        const auto evm=config.probe==hp::Probe::apsk64?evm_metrics(chain->stream.trace,source.base(),chain->stream.gauge):Evm{};
        const double metrology=cpu()-before;
        print_chain(*chain,source,mode,generation,generation_setup,metrology,tone,evm);
        result.qualified36+=chain->frozen.valid;result.window_count+=tone.full_windows;result.cycle_slips+=chain->stream.cycle_slips;
        result.maximum_admitted_phase=std::max(result.maximum_admitted_phase,tone.sample_phase_max);
        result.methods.push_back({chain->name,chain->frozen.valid,
            chain->use_joint?chain->joint_state.reference_initialized:chain->old_clock.reference_initialized,
            chain->use_joint?chain->joint_state.reference_preserved:chain->old_clock.reference_preserved,
            chain->joint_suspended,tone.full_windows,chain->stream.anchors,chain->stream.heldout_anchor_mask,
            chain->stream.cycle_slips,chain->joint_contradictions,chain->clock_outside,chain->joint_authority_rejections,
            chain->use_joint?chain->joint_state.reference_input_seconds:chain->old_clock.reference_input_seconds,
            tone.frequency_max,tone.sample_phase_max,chain->stream.first_input_seconds,
            chain->recovery_epoch_active,chain->recovery_epoch_accepted,chain->recovery_epoch_requests,
            chain->frozen.frequency,chain->frozen.half_width,
            chain->selected_step_model==h::HelperJointEstimator::FrequencyStepModel::rf_scaled,
            chain->admitted_dwells,chain->clock_error_max});
        if(required){require(chain->frozen.valid,"clean matched chain lacked 36-second snapshot");
            if(config.probe==hp::Probe::tone)require(tone.full_windows>=4,"clean matched chain lacked supported tone");
            else require(evm.symbols>=250,"clean matched chain lacked supported APSK");}
    }
    // Evaluator-only pairwise intersection. A different fixed reference changes
    // the output time grid, so do not pretend equal indices are equal source
    // times in that case. Neither support mask feeds either receiver.
    for(std::size_t i=0;i<chains.size();++i)for(std::size_t j=i+1;j<chains.size();++j){
        const double metrology_start=cpu();
        const auto& a=chains[i]->stream;const auto& b=chains[j]->stream;
        const bool comparable=a.initialized&&b.initialized&&
            std::abs(a.reference_input-b.reference_input)<1e-12&&std::abs(a.gauge-b.gauge)<1e-12;
        std::cout<<"helper_continuation_common_support first="<<chains[i]->name<<" second="<<chains[j]->name
            <<" seed="<<config.seed<<" mode="<<mode<<" conditioning_comparable="<<comparable;
        unsigned phase_count=0;double phase_a=0,phase_b=0,phase_max_a=0,phase_max_b=0;
        for(unsigned d=0;d<100;++d)if(chains[i]->evaluator_phase_available[d]&&chains[j]->evaluator_phase_available[d]){
            const double x=chains[i]->evaluator_phase_error[d],y=chains[j]->evaluator_phase_error[d];
            ++phase_count;phase_a+=x*x;phase_b+=y*y;phase_max_a=std::max(phase_max_a,std::abs(x));phase_max_b=std::max(phase_max_b,std::abs(y));
        }
        std::cout<<" common_phase_dwells="<<phase_count
            <<" first_phase_rms_deg="<<(phase_count?std::sqrt(phase_a/phase_count)*180/std::numbers::pi:0)
            <<" second_phase_rms_deg="<<(phase_count?std::sqrt(phase_b/phase_count)*180/std::numbers::pi:0)
            <<" first_phase_max_deg="<<phase_max_a*180/std::numbers::pi
            <<" second_phase_max_deg="<<phase_max_b*180/std::numbers::pi;
        if(comparable){
            const auto ta=config.probe==hp::Probe::tone?tone_metrics(a.trace,a.gauge,&b.trace):Metrics{};
            const auto tb=config.probe==hp::Probe::tone?tone_metrics(b.trace,b.gauge,&a.trace):Metrics{};
            const auto ea=config.probe==hp::Probe::apsk64?evm_metrics(a.trace,source.base(),a.gauge,&b.trace):Evm{};
            const auto eb=config.probe==hp::Probe::apsk64?evm_metrics(b.trace,source.base(),b.gauge,&a.trace):Evm{};
            require(ta.sample_count==tb.sample_count&&ta.windows==tb.windows&&ta.full_windows==tb.full_windows&&
                ta.noisy_windows==tb.noisy_windows&&ea.symbols==eb.symbols,"common-support evaluation counts differ");
            std::cout<<" common_tone_samples="<<ta.sample_count<<" common_tone_windows="<<ta.full_windows
                <<" first_evaluator_disturbed_samples="<<ta.physically_disturbed_supported
                <<" second_evaluator_disturbed_samples="<<tb.physically_disturbed_supported
                <<" first_twin_phase_rms_deg="<<ta.sample_phase_rms*180/std::numbers::pi
                <<" second_twin_phase_rms_deg="<<tb.sample_phase_rms*180/std::numbers::pi
                <<" first_twin_frequency_rms_hz="<<ta.frequency_rms<<" second_twin_frequency_rms_hz="<<tb.frequency_rms
                <<" common_apsk_symbols="<<ea.symbols<<" first_noisy_evm="<<ea.noisy<<" second_noisy_evm="<<eb.noisy
                <<" first_twin_evm="<<ea.clean<<" second_twin_evm="<<eb.clean;
        }
        std::cout<<" pair_metrology_cpu_seconds="<<cpu()-metrology_start<<'\n';
    }
    return result;
}

ht::Impairments impairment_named(const std::string& mode){
    ht::Impairments p;
    if(mode=="gap"){p.missing_first=91;p.missing_count=2;}
    else if(mode=="gap-init"){p.missing_first=90;p.missing_count=5;}
    else if(mode=="long-gap"||mode=="gap-evolution"||mode=="gap-evolution-outside"||mode=="gap-clock-cycle"||mode=="gap-step"){
        p.missing_first=70;p.missing_count=24;if(mode=="gap-step"){p.frequency_change_seconds=32;p.frequency_step_hz=1.2;}}
    else if(mode=="step"||mode=="step-debug"||mode=="second-step"||mode=="step-invalid"||mode=="step-missing"){
        p.frequency_step_hz=1.2;if(mode=="step-missing"){p.missing_first=95;p.missing_count=1;}}
    else if(mode=="curvature"){p.frequency_change_seconds=18;p.frequency_curvature_hz_per_second2=.003;}
    else if(mode=="differential")p.changing_low_phase_radians=.02;
    else if(mode=="differential-large"){p.differential_first=91;p.changing_low_phase_radians=.8;}
    else if(mode=="retune")p.late_retune_dwell=95;
    else if(mode=="retune-mid"){p.late_retune_dwell=95;p.late_retune_seconds=.12;}
    else if(mode=="noise")p.noise_only=true;
    else if(mode!="clean"&&mode!="drift"&&mode!="negative-clock"&&mode!="frequency-pulse"&&mode!="scaled-step"&&!mode.starts_with("transient-"))throw std::invalid_argument("unknown impairment");
    return p;
}
void scaled_step_fixture_control(){
    // Independent reference: find TX-calendar retunes by bisection in nominal
    // receiver time, then integrate a piecewise constant nominal frequency.
    // Production generation instead advances in physical time using a closed
    // form inverse and splits the sampled phase at each physical retune.
    for(unsigned profile=0;profile<3;++profile)for(const double onset:{.37,.61})for(const bool expanded:{false,true}){
        hp::Config c;c.seed=expanded?1666973697U:917U;c.expanded_schedule_key=expanded;c.path_loss_db=180;c.tx_audio_drift_ppm_per_second=.04;
        if(profile){c.shift_center_hz=5.8e9;c.relative_shift_ppm=.00008;c.phase_noise_degrees_per_sqrt_second=.005;c.microwave_eu20=profile==2;}
        ContinuationSource baseline(c,{}),scaled(c,{},false,true,onset);
        const auto hops=profile?(profile==2?helper_measurement_hops::eu20(c.seed,expanded):
            helper_measurement_hops::fcc100(c.seed,expanded)):helper_measurement_hops::hf50(c.seed,expanded);
        std::array<double,5> boundary{};
        for(unsigned hop=0;hop<boundary.size();++hop){
            double left=0,right=2;
            for(unsigned iteration=0;iteration<70;++iteration){const double midpoint=(left+right)/2;
                if(baseline.tx_time(midpoint)<hop*.4)left=midpoint;else right=midpoint;}
            boundary[hop]=(left+right)/2;
        }
        const auto expected=[&](double time){
            double cycles=0;
            for(unsigned hop=0;hop+1<boundary.size();++hop){
                const double left=std::max(onset,boundary[hop]),right=std::min(time,boundary[hop+1]);
                if(right>left)cycles+=(right-left)*1.2*hops.frequency(hop)/c.shift_center_hz/baseline.base().rx_scale();
            }
            return tau*cycles;
        };
        double maximum_phase_error=0;unsigned checked=0;
        for(unsigned dwell=0;dwell<3;++dwell){
            const auto ordinary=baseline.generate(dwell),changed=scaled.generate(dwell);
            require(ordinary.shift_hz==changed.shift_hz,"scaled-step fixture changed the random RF commands");
            for(std::size_t sample=0;sample<changed.common_phasor.size();sample+=37){
                const double time=changed.first_seconds+static_cast<double>(sample)/fs;
                const C ratio=C(changed.common_phasor[sample])*std::conj(C(ordinary.common_phasor[sample]));
                maximum_phase_error=std::max(maximum_phase_error,std::abs(wrap(std::arg(ratio)-expected(time))));++checked;
                if(time>=onset&&time>changed.first_seconds+.02&&time<changed.first_seconds+.38){
                    const double observed=scaled.shift_error_hz(changed.shift_hz,time)-baseline.shift_error_hz(changed.shift_hz,time);
                    const double reference=1.2*changed.shift_hz/c.shift_center_hz/baseline.base().rx_scale();
                    require(std::abs(observed-reference)<1e-10,"scaled-step frequency truth disagreed with physical oscillator units");
                }
            }
        }
        require(checked>700&&maximum_phase_error<2e-6,"scaled-step sampled phase disagreed with independent piecewise integral");
        std::cout<<"helper_continuation_scaled_fixture profile="<<(profile==0?"hf50":profile==1?"fcc100":"eu20")
            <<" schedule_key_namespace="<<(expanded?"expanded_v3":"legacy")
            <<" capture_seconds=1.2 onset_seconds="<<onset<<" reference_rf_hz="<<c.shift_center_hz
            <<" physical_step_hz_at_reference=1.2 checked="<<checked<<" maximum_phase_error_rad="<<maximum_phase_error<<'\n';
    }
}

void supplied_coordinate_control(){
    hp::Config config;config.seed=901;config.path_loss_db=180;ContinuationSource source(config,{});
    h::HelperMeasurementConfig mc{fs,550,2400,2300,2500,300,.05*std::numbers::pi/180};
    h::HelperMeasurement earlier(mc);
    for(unsigned i=0;i<5;++i){const auto d=source.generate(i);
        (void)earlier.observe(std::span<const S>(d.samples.data()+settle,aperture),d.shift_hz,d.first_seconds+settle/fs);}
    const auto d=source.generate(5);const double date=d.first_seconds+settle/fs+(aperture-1)/(2*fs);
    const auto hi=earlier.predict(2400,d.shift_hz,date,aperture/fs),lo=earlier.predict(550,d.shift_hz,date,aperture/fs);
    require(hi.valid&&lo.valid,"earlier Helper captures did not establish supplied-coordinate premise");
    h::HelperAidedPhasePrediction prior{true,hi.frequency_hz,lo.frequency_hz,
        hi.mean_sigma_hz+hi.aperture_process_sigma_hz,lo.mean_sigma_hz+lo.aperture_process_sigma_hz};
    Receiver supplied("supplied-coordinate-control",true,mc);
    supplied.observe(d,false,5,prior);
    require(!supplied.last_global_prior_valid&&supplied.last_supplied_prior_valid&&supplied.phase_from_aided&&
        supplied.aided_calls==1&&supplied.broad_calls==0,"qualified separate Helper coordinates were blocked by an empty unrelated global fit");
    Receiver absent("absent-coordinate-control",true,mc);absent.observe(d,false,5);
    require(!absent.last_global_prior_valid&&!absent.last_supplied_prior_valid&&!absent.phase_from_aided&&
        absent.aided_calls==0&&absent.broad_calls==1,"unavailable Helper coordinates falsely started local aiding");
}
void diagnostic_prior_control(){
    DiagnosticPrior p;p.valid=true;p.begin=10;p.endpoint=12;p.end=13;p.minimum_rf=29e6;p.maximum_rf=31e6;
    p.curvature_bound_hz_per_second2=.002;
    for(unsigned pilot=0;pilot<2;++pilot)for(unsigned rf=0;rf<2;++rf)for(unsigned time=0;time<2;++time)
        p.values[pilot][rf][time]={100.+10*pilot+2*rf+4*time,.01+.01*time,.02+.01*time};
    const auto x=p.predict(0,30e6,13);
    require(x&&std::abs(x->mean-107)<1e-12,"detector coordinate clamped its affine extrapolation mean");
    require(std::abs(x->sigma-.035)<1e-12&&std::abs(x->model-.058)<1e-12,
        "detector coordinate treated correlated endpoint errors as independent or omitted curvature remainder");
    require(!p.predict(0,30e6,13.0001)&&!p.predict(0,28e6,12),"detector coordinate extrapolated beyond its declared time/RF bounds");
}

void short_probe(){
    scaled_step_fixture_control();
    supplied_coordinate_control();
    diagnostic_prior_control();
    // The fixture key repeats every 256 seeds. Verify byte-level separation
    // across the tuning, held-out HF and two wide-hop sets rather than assume
    // distinct numeric seeds imply distinct cryptographic test keys.
    constexpr std::array<unsigned,56> reserved{
        16777250,16777251,16777252,16777253,16777254,16777255,16777257,16777258,
        16777264,16777265,16777266,16777267,16777268,16777269,16777271,16777272,
        16777274,16777276,16777283,16777284,16777290,16777291,16777292,16777301,
        16777302,16777304,16777305,16777306,16777307,16777308,16777309,16777311,
        16777312,16777313,16777314,16777315,16777316,16777413,16777414,16777415,
        16777449,16777450,16777452,16777453,16777454,16777455,16777456,16777460,
        16777461,16777462,16777463,16777470,16777471,16777259,16777260,16777261};
    const auto key_array=[](std::uint64_t seed,bool expanded=false){
        const auto bytes=helper_measurement_hops::fixture_key(seed,expanded);
        require(bytes.size()==32,"invalid Helper fixture key size");
        std::array<std::uint8_t,32> result{};std::copy(bytes.begin(),bytes.end(),result.begin());return result;
    };
    std::set<std::array<std::uint8_t,32>> keys;
    for(const auto seed:reserved)require(keys.insert(key_array(seed)).second,"reserved Helper schedule keys collide");
    for(const auto range:std::array<std::pair<unsigned,unsigned>,5>{{{0,32},{77,84},{101,132},{133,196},{201,232}}})
        for(unsigned seed=range.first;seed<=range.second;++seed)
            require(!keys.contains(key_array(seed)),"reserved Helper key collides with recorded fixtures");
    std::set<std::array<std::uint8_t,32>> all_keys;
    for(unsigned seed=0;seed<256;++seed){
        const auto old_key=key_array(seed);
        require(all_keys.insert(old_key).second&&old_key==key_array(seed+256),
            "legacy Helper fixture key behavior changed");
    }
    const auto verify_namespace=[&](unsigned first,unsigned count){
        for(unsigned seed=first;seed<first+count;++seed)
            require(all_keys.insert(key_array(seed,true)).second,
                "expanded Helper fixture key collision");
    };
    verify_namespace(1666973697U,8);   // tuning only
    verify_namespace(1667039233U,32);  // HF held-out
    verify_namespace(1667104769U,8);   // FCC held-out
    verify_namespace(1667170305U,8);   // EU held-out
    verify_namespace(1680000001U,8);   // v4 tuning only
    verify_namespace(1680100001U,32);  // v4 HF held-out
    verify_namespace(1680200001U,8);   // v4 FCC held-out
    verify_namespace(1680300001U,8);   // v4 EU held-out
    verify_namespace(1690000001U,8);   // v5 tuning only
    verify_namespace(1690100001U,32);  // v5 HF held-out
    verify_namespace(1690200001U,8);   // v5 FCC held-out
    verify_namespace(1690300001U,8);   // v5 EU held-out
    verify_namespace(1700000001U,8);   // v6 tuning only
    verify_namespace(1700100001U,32);  // v6 HF held-out
    verify_namespace(1700200001U,8);   // v6 FCC held-out
    verify_namespace(1700300001U,8);   // v6 EU held-out
    const auto legacy_schedule=helper_measurement_hops::hf50(901);
    const auto legacy_repeat=helper_measurement_hops::hf50(1157);
    require(legacy_schedule.order==legacy_repeat.order,"legacy keyed Helper schedule changed");
    const auto expanded_schedule=helper_measurement_hops::hf50(1666973697U,true);
    require(expanded_schedule.order==helper_measurement_hops::hf50(1666973697U,true).order,
        "expanded Helper schedule is not deterministic");
    // Evaluator-only support intersection must discard samples unavailable to
    // either method; it must not manufacture a per-window alignment.
    Trace a,b;a.first=b.first=38*static_cast<std::uint64_t>(fs);
    for(std::uint64_t i=0;i<2*static_cast<std::uint64_t>(fs);++i){
        const S sample=static_cast<S>(std::polar(1.,tau*std::remainder(hp::probe_frequency_hz*static_cast<double>(a.first+i)/fs,1.)));
        a.noisy.push_back(sample);a.clean.push_back(sample);a.supported.push_back(1);
        b.noisy.push_back(sample);b.clean.push_back(sample);b.supported.push_back(i>=static_cast<std::uint64_t>(fs));
    }
    const auto native=tone_metrics(a,0),shared_a=tone_metrics(a,0,&b),shared_b=tone_metrics(b,0,&a);
    require(shared_a.sample_count>0&&shared_a.sample_count<native.sample_count&&shared_a.sample_count==shared_b.sample_count,
        "common-support tone metrology did not intersect masks symmetrically");
    require(shared_a.physically_clean_withheld>0&&native.physically_clean_withheld==0,
        "evaluator did not count clean samples withheld by support intersection");
    require(shared_a.full_windows==shared_b.full_windows&&shared_a.phase_rms<1e-6&&shared_b.phase_rms<1e-6,
        "common-support tone metrology changed the fixed phase reference");
    h::HelperJointEstimator::Input raw;raw.valid=true;raw.model_contradicted=true;
    require(!frequency_epoch_raw(raw,false),"frequency epoch erased an unrelated external contradiction");
    const auto labeled_frequency=frequency_epoch_raw(raw,true);
    require(labeled_frequency&&!labeled_frequency->model_contradicted,"raw frequency-only notice could not be separated from unchanged observations");
    bool suspended=true;unsigned streak=0;
    require(!advance_recovery(suspended,streak,true,true,true),"one new observation silently restored joint authority");
    require(!advance_recovery(suspended,streak,true,true,true),"two new observations silently restored joint authority");
    require(!advance_recovery(suspended,streak,false,false,true)&&suspended&&streak==0,"missing dwell preserved a partial recovery streak");
    require(!advance_recovery(suspended,streak,true,true,true)&&!advance_recovery(suspended,streak,true,true,true)&&suspended,"gap-interleaved observations restored joint authority");
    require(advance_recovery(suspended,streak,true,true,true)&&!suspended,"three consecutive consistent observations did not restore authority");
    h::HelperJointEstimator joint({});
    h::HelperJointEstimator::Input input;input.valid=true;
    input.thermal_covariance={{{.0001,0,0},{0,.000144,0},{0,0,.0001}}};
    h::HelperJointEstimator::Estimate prior,updated;
    for(unsigned i=0;i<20;++i){input.time_seconds=.2+.4*i;input.frequency_time_seconds=input.time_seconds;input.shift_hz=30e6+50000*(static_cast<double>(i%5)-2);
        const double source_time=1.00005*input.time_seconds;
        input.high_frequency_hz=2400*1.00005+.8e-6*input.shift_hz;input.low_frequency_hz=550*1.00005+.8e-6*input.shift_hz;
        input.high_center_phase_radians=wrap(tau*2400*source_time+.37*i);input.low_center_phase_radians=wrap(tau*550*source_time+.37*i);
        updated=joint.observe(input);
    }
    input.time_seconds+=.4;input.frequency_time_seconds=input.time_seconds;const double source_time=1.00005*input.time_seconds;
    input.high_center_phase_radians=wrap(tau*2400*source_time+.37*20);input.low_center_phase_radians=wrap(tau*550*source_time+.37*20+1.5);
    prior=joint.predict(1600,input.shift_hz,input.time_seconds);const auto accepted_before=prior.accepted_observations;
    updated=joint.observe(input);
    require(prior.valid&&internal_model_conflict(true,prior.valid,updated.accepted_observations>accepted_before,updated),"fresh internal phase contradiction did not engage recovery policy");
    require(updated.reference_initialized&&updated.reference_input_seconds==.2,"internal contradiction silently selected a new reference");
    input.valid=false;input.time_seconds+=.4;input.frequency_time_seconds=input.time_seconds;
    const auto missing_after_conflict=joint.observe(input);
    require(!missing_after_conflict.valid&&missing_after_conflict.reference_initialized&&missing_after_conflict.reference_input_seconds==.2,
        "missing input cleared a fresh contradiction or re-established a phase reference");
    input.valid=true;
    h::HelperJointEstimator after_gap({});
    for(unsigned i=0;i<20;++i){input.time_seconds=.2+.4*i;input.frequency_time_seconds=input.time_seconds;input.shift_hz=30e6+50000*(static_cast<double>(i%5)-2);
        const double t=1.00005*input.time_seconds;
        input.high_frequency_hz=2400*1.00005+.8e-6*input.shift_hz;input.low_frequency_hz=550*1.00005+.8e-6*input.shift_hz;
        input.high_center_phase_radians=wrap(tau*2400*t+.37*i);input.low_center_phase_radians=wrap(tau*550*t+.37*i);
        updated=after_gap.observe(input);
    }
    const auto established=after_gap.predict(1600,input.shift_hz,input.time_seconds);
    input.time_seconds+=2.0001;input.frequency_time_seconds=input.time_seconds;const double gap_source=1.00005*input.time_seconds;
    input.high_center_phase_radians=wrap(tau*2400*gap_source+.37*20);input.low_center_phase_radians=wrap(tau*550*gap_source+.37*20+1.5);
    const auto stale=after_gap.predict(1600,input.shift_hz,input.time_seconds);const auto count=stale.accepted_observations;
    updated=after_gap.observe(input);
    require(established.valid&&!stale.valid&&internal_model_conflict(true,established.valid,updated.accepted_observations>count,updated),"expired prediction hid a preserved-model phase contradiction");
    require(updated.reference_initialized&&updated.reference_input_seconds==.2,"post-gap phase contradiction silently reinitialized reference");
    hp::Config config;config.seed=901;ContinuationSource source(config,{});
    h::HelperMeasurementConfig mc{fs,550,2400,2300,2500,300,.05*std::numbers::pi/180};
    using Method=h::HelperAidedFrequencyMethod;
    std::vector<std::unique_ptr<Chain>> chains;
    chains.push_back(std::make_unique<Chain>("legacy-clock",Method::subaperture_slope,false,false,mc,30e6));
    chains.push_back(std::make_unique<Chain>("legacy-cov-clock",Method::subaperture_slope,false,false,mc,30e6,true));
    chains.push_back(std::make_unique<Chain>("partitioned-joint-dual",Method::partitioned_hann,true,true,mc,30e6));
    for(unsigned i=0;i<8;++i){const auto d=source.generate(i);for(auto& c:chains){c->observe(d,false,i);c->evaluate(source,d,i);}}
    for(auto& c:chains){require(c->receiver.valid>=6,"short refinement probe failed pilot acquisition");
        std::cout<<"helper_continuation_short method="<<c->name<<" capture_seconds=3.2 helper_w=40 loss_db=180 valid_dwells="<<c->receiver.valid
            <<" snapshot36=unavailable frequency_rms_hz="<<std::sqrt(c->receiver.frequency_error2/c->receiver.frequency_errors)
            <<" reported_sigma_rms_hz="<<std::sqrt(c->receiver.frequency_sigma2/c->receiver.frequency_errors)
            <<" phase_rms_deg="<<c->current_phase.rms()*180/std::numbers::pi<<'\n';}
}
void robustness(){
    hp::Config c;c.path_loss_db=180;c.seed=901;
    const auto clean=scenario(c,{},"clean","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"}){
        const auto& x=clean.at(method);require(x.frozen&&x.windows==5&&x.anchors==5&&x.cycle_slips==0,"clean finalist did not establish nonvacuous support");
    }
    auto problem=impairment_named("noise");const auto noise=scenario(c,problem,"noise","finalists");
    for(const auto& x:noise.methods)require(!x.frozen&&x.anchors==0&&x.windows==0,"noise-only input falsely qualified refinement");
    problem=impairment_named("step");const auto step=scenario(c,problem,"step","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"}){
        const auto& x=step.at(method);require(x.frozen&&x.contradictions>0&&x.authority_rejections>0,"frequency step lacked a fresh contradiction witness");
        require((x.mask&1U)==0,"first stale post-step frequency model falsely authorized a hop");
        require(x.cycle_slips==0&&x.phase_max<.75&&x.frequency_max<.5,"step correction admitted large residual errors");
        require(x.reference_initialized&&std::abs(x.reference-clean.at(method).reference)<1e-12,"step selected a new time reference");
    }
    problem=impairment_named("gap");const auto gap=scenario(c,problem,"gap","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"}){
        const auto& x=gap.at(method);require(x.reference_preserved&&x.windows==5&&x.cycle_slips==0,"short pilot gap did not preserve useful reference");
        require(std::abs(x.reference-clean.at(method).reference)<1e-12,"pilot gap silently selected a new origin");
    }
    problem=impairment_named("gap-init");const auto init_gap=scenario(c,problem,"gap-init","finalists");
    for(const auto& x:init_gap.methods)require(x.first_input_seconds==0||x.first_input_seconds>=38,"expired midpoint prediction initialized a conditioner with default coefficients");
    problem=impairment_named("long-gap");const auto long_gap=scenario(c,problem,"long-gap","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"}){
        const auto& x=long_gap.at(method);require(!x.frozen&&x.reference_initialized&&std::abs(x.reference-clean.at(method).reference)<1e-12,"long gap claimed unavailable 36-second fit or reseeded");
        require(x.windows==0||x.reference_preserved,"long gap admitted output without reference coverage");
    }
    problem=impairment_named("curvature");const auto curvature=scenario(c,problem,"curvature","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"})require(!curvature.at(method).frozen,"curved frequency model falsely qualified 36-second narrowing");
    problem=impairment_named("retune-mid");const auto retune=scenario(c,problem,"retune-mid","finalists");
    for(const auto* method:{"legacy-joint","legacy-joint-dual","partitioned-joint-dual"}){
        const auto& x=retune.at(method);require((x.mask&1U)==0&&x.phase_max<.75,"settling transient was hidden inside admitted support");
    }
    std::cout<<"helper_continuation_robustness passed=1 checks=clean_noise_step_gap_init_gap_long_gap_curvature_retune\n";
}

void recovery_robustness(){
    hp::Config c;c.path_loss_db=180;c.seed=901;
    const auto clean=scenario(c,{},"clean","recovery");
    const auto& clean_recovered=clean.at("legacy-joint-dual-recovery");
    require(clean_recovered.frozen&&clean_recovered.windows==5&&!clean_recovered.epoch_active&&clean_recovered.epochs_accepted==0,
        "unchanged signal selected a spurious frequency epoch");
    const auto step=scenario(c,impairment_named("step"),"step","recovery");
    const auto& recovered=step.at("legacy-joint-dual-recovery");
    const auto& withdrawn=step.at("legacy-joint-dual");
    require(recovered.epoch_active&&recovered.epochs_accepted==1&&recovered.windows>=1&&recovered.anchors>=1,
        "confirmed common frequency step did not produce nonvacuous recovery");
    require(withdrawn.windows==0,"no-epoch comparison unexpectedly supplied post-step joint correction");
    require(recovered.reference_initialized&&recovered.reference_preserved&&
        std::abs(recovered.reference-clean_recovered.reference)<1e-12&&recovered.cycle_slips==0,
        "frequency recovery changed or lost the original source reference");
    require(recovered.frequency_max<.1&&recovered.phase_max<.5&&recovered.clock_misses==0,
        "frequency recovery admitted inaccurate supported samples");
    require(recovered.frozen==clean_recovered.frozen&&recovered.frozen_frequency==clean_recovered.frozen_frequency&&
        recovered.frozen_half_width==clean_recovered.frozen_half_width,
        "post-36-second frequency recovery changed the frozen observation result");
    const auto scaled_step=scenario(c,{},"scaled-step","recovery");
    const auto& scaled=scaled_step.at("legacy-joint-dual-recovery");
    require(scaled.selected_scaled_step&&scaled.epoch_active&&scaled.epochs_accepted==1&&scaled.windows>=3,
        "predeclared RF-scaled oscillator step lacked nonvacuous recovery");
    require(scaled.reference_initialized&&scaled.reference_preserved&&scaled.cycle_slips==0&&
        std::abs(scaled.reference-clean_recovered.reference)<1e-12&&scaled.frequency_max<.1&&scaled.phase_max<.2&&scaled.clock_misses==0,
        "RF-scaled oscillator recovery changed timing reference or admitted inaccurate samples");
    require(scaled.frozen==clean_recovered.frozen&&scaled.frozen_frequency==clean_recovered.frozen_frequency&&
        scaled.frozen_half_width==clean_recovered.frozen_half_width,
        "later scaled oscillator step changed the frozen 36-second result");
    require(scaled_step.at("legacy-joint-dual").windows==0,
        "no-epoch comparison unexpectedly recovered the scaled oscillator step");
    const auto pulse=scenario(c,{},"frequency-pulse","recovery");
    for(const auto& x:pulse.methods)if(x.name=="legacy-joint-dual-recovery"){
        require(!x.epoch_active&&x.epochs_accepted==0&&x.cycle_slips==0,
            "a one-dwell frequency excursion falsely established a persistent epoch");
        require(x.windows>=3&&x.frequency_max<.1&&x.phase_max<.2&&x.reference_preserved&&
            std::abs(x.reference-clean_recovered.reference)<1e-12,
            "frequency pulse lacked accurate nonvacuous recovery on the original reference");
    }
    const auto noise=scenario(c,impairment_named("noise"),"noise","recovery");
    for(const auto& x:noise.methods)require(!x.frozen&&x.windows==0&&x.epochs_accepted==0,
        "noise-only input qualified a frequency epoch or corrected waveform");
    const auto curvature=scenario(c,impairment_named("curvature"),"curvature","recovery");
    const auto& curve=curvature.at("legacy-joint-dual-recovery");
    require(!curve.frozen&&!curve.epoch_active&&curve.epochs_accepted==0&&curve.windows==0,
        "smooth curvature was hidden by a selected frequency step");
    const auto retune=scenario(c,impairment_named("retune-mid"),"retune-mid","recovery");
    const auto& transient=retune.at("legacy-joint-dual-recovery");
    require(!transient.epoch_active&&transient.epochs_accepted==0&&(transient.mask&1U)==0&&transient.phase_max<.75,
        "retune transient selected a spurious epoch or hid an admitted phase error");
    const auto long_gap=scenario(c,impairment_named("long-gap"),"long-gap","recovery");
    const auto& gap=long_gap.at("legacy-joint-dual-recovery");
    require(!gap.frozen&&gap.reference_initialized&&std::abs(gap.reference-clean_recovered.reference)<1e-12&&
        gap.epochs_accepted==0&&(!gap.windows||gap.reference_preserved),
        "missing pilots fabricated a frequency epoch or silently reinitialized timing");
    std::cout<<"helper_continuation_recovery_robustness passed=1 checks=clean_confirmed_step_scaled_step_pulse_noise_curvature_retune_long_gap\n";
}
void gap_robustness(){
    hp::Config c;c.path_loss_db=180;c.seed=901;
    const auto clean=scenario(c,{},"clean","gap");
    const auto& original=clean.at("legacy-joint-dual-gap");
    require(original.frozen&&original.windows==5&&original.cycle_slips==0,"gap experiment lacked supported clean control");
    const auto short_gap=scenario(c,impairment_named("gap"),"gap","gap");
    const auto& short_result=short_gap.at("legacy-joint-dual-gap");
    require(short_result.windows==5&&short_result.reference==original.reference&&short_result.cycle_slips==0,
        "bounded branch propagation damaged short-gap tracking");
    const auto long_gap=scenario(c,impairment_named("long-gap"),"long-gap","gap");
    const auto& result=long_gap.at("legacy-joint-dual-gap");
    require(!result.frozen&&result.reference_initialized&&result.reference==original.reference&&result.windows>=3&&result.cycle_slips==0,
        "long-gap tracking lacked qualified fresh evidence on its original reference");
    require(result.frequency_max<.1&&result.phase_max<.3&&result.clock_misses==0,"long-gap tracking admitted inaccurate samples");
    require(long_gap.at("legacy-joint-dual").windows==0,"baseline long-gap control unexpectedly resumed");
    for(unsigned i=70;i<96;++i)require(!result.admitted_dwells[i],"missing or unqualified post-gap observations supplied authority");
    for(const auto* mode:{"gap-step","noise"}){
        const auto trial=scenario(c,impairment_named(mode),mode,"gap");
        for(const auto& x:trial.methods)require(!x.frozen&&x.windows==0&&x.epochs_accepted==0,
            "gap branch propagation hid an unsupported frequency change or qualified noise");
    }
    // These waveform-clock controls deliberately separate the RF key calendar
    // from the audio waveform clock. The cycle-sized jump is OUTSIDE the
    // declared bounded evolution prior and can be unobservable to two pilot
    // phases. Report admitted errors; do not mislabel a conditional model as
    // arbitrary-jump detection. None may choose a fresh time origin.
    for(const auto* mode:{"gap-evolution","gap-evolution-outside","gap-clock-cycle"}){
        const auto trial=scenario(c,impairment_named(mode),mode,"gap");
        for(const auto& x:trial.methods)require(!x.frozen&&x.reference_initialized&&x.reference==original.reference,
            "audio-clock stress changed the reference or backdated later observations to36");
    }
    std::cout<<"helper_continuation_gap_robustness passed=1 checks=clean_short_long_missing_pending_gap_step_noise_clock_evolution_clock_cycle_conditional\n";
}
void extended_recovery_robustness(){
    hp::Config c;c.path_loss_db=180;c.seed=901;
    const auto clean=scenario(c,{},"clean","recovery-extended");
    const auto& original=clean.at("legacy-joint-dual-recovery-extended");
    require(original.frozen&&original.windows==5&&original.epochs_accepted==0,"extended detector lacked supported clean control");
    for(const auto* mode:{"step","step-invalid","step-missing","second-step","curvature","noise","long-gap"}){
        const auto trial=scenario(c,impairment_named(mode),mode,"recovery-extended");
        const auto& x=trial.at("legacy-joint-dual-recovery-extended");
        if(std::string(mode)=="noise"){require(!x.frozen&&x.windows==0&&x.epochs_accepted==0,"extended detector falsely qualified noise");continue;}
        require(x.reference_initialized&&x.reference==original.reference,"extended detector changed the original reference");
        if(std::string(mode)=="step")require(x.epochs_accepted==1&&x.windows>=3&&x.cycle_slips==0&&x.phase_max<.3&&x.frequency_max<.1,
            "extended detector failed the supported frequency-step control");
        if(std::string(mode)=="step-invalid"||std::string(mode)=="step-missing"){
            require(!x.admitted_dwells[95]&&x.epochs_accepted==0,"an unusable dwell bridged a pending detector proposal/replay");
        }
        if(std::string(mode)=="second-step")require(x.epochs_accepted==1&&!x.admitted_dwells[98]&&!x.admitted_dwells[99],
            "one-epoch model hid a second independent frequency step");
        if(std::string(mode)=="curvature"||std::string(mode)=="long-gap")require(!x.frozen&&x.windows==0&&x.epochs_accepted==0,
            "extended detector hid curvature or used unbounded stale prior");
        if(std::string(mode)!="curvature"&&std::string(mode)!="long-gap")require(x.frozen==original.frozen&&x.frozen_frequency==original.frozen_frequency&&x.frozen_half_width==original.frozen_half_width,
            "post36 detector experiment changed the frozen36 result");
    }
    std::cout<<"helper_continuation_extended_recovery_robustness passed=1 checks=clean_step_missing_invalid_second_step_curvature_noise_longgap\n";
}

}
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--probe"){short_probe();return 0;}
    if(argc>1&&std::string(argv[1])=="--robustness"){short_probe();robustness();return 0;}
    if(argc>1&&std::string(argv[1])=="--recovery-robustness"){short_probe();recovery_robustness();return 0;}
    if(argc>1&&std::string(argv[1])=="--gap-robustness"){short_probe();gap_robustness();return 0;}
    if(argc>1&&std::string(argv[1])=="--extended-recovery-robustness"){short_probe();extended_recovery_robustness();return 0;}
    hp::Config config;std::string mode="clean",methods="all";
    if(argc>1&&std::string(argv[1])=="--case"){
        if(argc<4||argc>9)throw std::invalid_argument("--case LOSS SEED [hf|microwave|microwave-eu, optional -v3 key suffix] [MODE] [tone|apsk] [PROBE_W] [METHODS]");
        config.path_loss_db=std::stod(argv[2]);config.seed=std::stoull(argv[3]);
        if(argc>4){const std::string profile=argv[4];config.expanded_schedule_key=profile.ends_with("-v3");
            const auto rf=config.expanded_schedule_key?profile.substr(0,profile.size()-3):profile;
            if(rf=="microwave"||rf=="microwave-eu"){
                config.shift_center_hz=5.8e9;config.relative_shift_ppm=.00008;config.phase_noise_degrees_per_sqrt_second=.005;config.microwave_eu20=rf=="microwave-eu";
            }else if(rf!="hf")throw std::invalid_argument("unknown RF profile");}
        if(argc>5)mode=argv[5];
        if(argc>6){const std::string probe=argv[6];if(probe=="apsk")config.probe=hp::Probe::apsk64;else if(probe!="tone")throw std::invalid_argument("unknown probe");}
        if(argc>7)config.probe_w=std::stod(argv[7]);
        if(argc>8)methods=argv[8];
        if(mode=="drift"){config.tx_audio_drift_ppm_per_second=.04;config.relative_shift_drift_ppm_per_second=.00002;}
        if(mode=="negative-clock"){config.tx_audio_ppm=-37;config.rx_audio_ppm=13;}
        scenario(config,impairment_named(mode),mode,methods);return 0;
    }
    if(argc>1&&std::string(argv[1])!="--suite")throw std::invalid_argument("expected --probe, --case, --robustness, --recovery-robustness, --gap-robustness, --extended-recovery-robustness or --suite");
    short_probe();config.path_loss_db=160;scenario(config,{},"clean","all",true);
    config.path_loss_db=180;scenario(config,{},"clean","all",true);return 0;
}catch(const std::exception& e){std::cerr<<"Helper continuation study failure: "<<e.what()<<'\n';return 1;}}
