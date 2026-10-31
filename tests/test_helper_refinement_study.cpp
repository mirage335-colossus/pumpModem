#include "datapump/helper_aided_phase.hpp"
#include "datapump/helper_relative_clock.hpp"
#include "datapump/helper_joint_estimator.hpp"
#include "datapump/helper_dual_pilot_phase.hpp"
#include "datapump/helper_stream_conditioner.hpp"
#include "helper_tracking_fixture.hpp"

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
#include <span>
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
    bool phase_from_aided=false;
    h::HelperAidedPhaseResult last_aided;
    double frequency_error2=0,frequency_sigma2=0,frequency_error_max=0;
    unsigned frequency_errors=0,frequency_outside_three_sigma=0;
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
        phase={};measurement={};phase_from_aided=false;last_aided={};
        if(missing){if(index==89){const auto s=snapshot();if(s.valid)frozen=s;}observed_cpu+=cpu()-observed_started;if(index==89)cpu36=total_cpu();return;}
        const auto data=std::span<const S>(d.samples.data()+settle,aperture);
        const double start=d.first_seconds+settle/fs;
        bool acquired=false;
        auto prior=snapshot();
        if(aided&&prior.valid){
            const auto predict_started=cpu();
            const auto ph=fit.predict(2400,d.shift_hz,start+(aperture-1)/(2*fs),aperture/fs);
            const auto pl=fit.predict(550,d.shift_hz,start+(aperture-1)/(2*fs),aperture/fs);
            h::HelperAidedPhasePrediction prediction{ph.valid&&pl.valid,ph.frequency_hz,pl.frequency_hz,
                ph.mean_sigma_hz+ph.aperture_process_sigma_hz,pl.mean_sigma_hz+pl.aperture_process_sigma_hz};
            fit_cpu+=cpu()-predict_started;
            if(supplied&&supplied->valid)prediction=*supplied;
            const auto a=cpu();auto result=h::estimate_helper_aided_phase(data,aided_config,prediction,d.shift_hz,start);
            tracking_cpu+=cpu()-a;++aided_calls;
            if(result.valid){
                const auto f=cpu();require(fit.ingest(result.measurement),"aided measurement intake failed");fit_cpu+=cpu()-f;
                measurement=result.measurement;last_aided=result;
                phase={true,result.center_time_seconds,result.high_center_phase_radians,result.low_center_phase_radians,
                    result.high_center_phase_sigma_radians+result.common_center_phase_sigma_radians,
                    result.phase_difference_sigma_radians};
                phase_from_aided=true;
                acquired=true;
            }else{
                ++fallbacks;
                if(result.failure==h::HelperAidedPhaseFailure::residual_frequency)++residual_rejections;
                if(result.failure==h::HelperAidedPhaseFailure::phase_evolution)++evolution_rejections;
                if(result.failure==h::HelperAidedPhaseFailure::insufficient_signal)++signal_rejections;
            }
        }
        if(!acquired){const auto a=cpu();measurement=fit.observe(data,d.shift_hz,start);
            const double elapsed=cpu()-a;acquisition_cpu+=elapsed;if(aided_calls)fallback_cpu+=elapsed;else startup_cpu+=elapsed;++broad_calls;phase=broad_phase(measurement);}
        if(measurement.valid){++valid;recent.push_back(measurement);if(recent.size()>8)recent.erase(recent.begin());}
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
    void evaluate_phase(const ht::Source& source,const hp::Dwell& d,unsigned index){
        if(measurement.valid){
            const double frequency_date=phase_from_aided?last_aided.center_time_seconds:measurement.time_seconds;
            const double truth_high=2400*source.audio_scale_at(frequency_date)+source.shift_error_hz(d.shift_hz,frequency_date);
            const double truth_low=550*source.audio_scale_at(frequency_date)+source.shift_error_hz(d.shift_hz,frequency_date);
            const double errors[]{measurement.high_frequency_hz-truth_high,measurement.low_frequency_hz-truth_low};
            const double sigmas[]{std::hypot(measurement.high_sigma_hz,measurement.common_phase_frequency_sigma_hz),std::hypot(measurement.low_sigma_hz,measurement.common_phase_frequency_sigma_hz)};
            for(unsigned j=0;j<2;++j){++frequency_errors;frequency_error2+=errors[j]*errors[j];frequency_sigma2+=sigmas[j]*sigmas[j];frequency_error_max=std::max(frequency_error_max,std::abs(errors[j]));if(std::abs(errors[j])>3*sigmas[j])++frequency_outside_three_sigma;}
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
    double cpu_seconds=0,cpu36=0,reference_input=0,gauge=0;
    unsigned anchors=0,timing_rejections=0,model_rejections=0;
    unsigned heldout_anchor_mask=0;
    std::uint64_t supported_samples=0,output_samples=0,input_samples=0;
    double timing_error_max=0,timing_error2=0,first_timing_error=0,last_timing_error=0;
    unsigned timing_observations=0,cycle_slips=0,cycle_error_observations=0;
    long long previous_cycle_error=0;
    bool initialized=false;double first_input_seconds=0;
    void initialize(std::uint64_t first,Coeff c,double source,double reference,const ht::Source& simulator){
        const auto initialized_started=cpu();first_input_seconds=static_cast<double>(first)/fs;
        const auto cfg=stream_config(first,c.audio,source);
        noisy=std::make_unique<h::HelperStreamConditioner>(cfg,h::HelperStreamConditioner::Correction{c.audio,c.offset,.001});
        const auto clean_started=cpu();clean=std::make_unique<h::HelperStreamConditioner>(cfg,h::HelperStreamConditioner::Correction{c.audio,c.offset,.001});
        const double clean_elapsed=cpu()-clean_started;
        reference_input=reference;
        cpu_seconds+=cpu()-initialized_started-clean_elapsed;
        // Exactly one EVALUATOR-ONLY clock gauge; no probe fit or receiver input.
        gauge=reference-simulator.tx_time(reference);initialized=true;
    }
    void feed(const hp::Dwell& d,Coeff c,const Phase& phase,bool model_valid,const ht::Source& simulator,
              std::optional<h::HelperRelativeClock::Estimate> joint={}){
        const auto feed_started=cpu();double excluded_cpu=0;
        require(initialized,"uninitialized stream");
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        noisy->update({c.audio,c.offset,.001});noisy->clear_phase();
        const auto clean_update=cpu();clean->update({c.audio,c.offset,.001});clean->clear_phase();excluded_cpu+=cpu()-clean_update;
        bool allow=model_valid&&phase.valid&&std::isfinite(phase.high_sigma)&&phase.high_sigma*3<.5;
        if(!model_valid)++model_rejections;
        const long double anchor=phase.date*fs;
        if(allow&&joint){
            const double clock=static_cast<double>(noisy->clock_time_at_input(anchor));
            const double mismatch=std::abs(clock-joint->source_seconds);
            if(!joint->valid||!joint->reference_preserved||mismatch>joint->source_half_width_seconds){allow=false;++timing_rejections;}
        }
        if(allow){
            const double clock=static_cast<double>(noisy->clock_time_at_input(anchor));
            const h::HelperStreamConditioner::LocalPhase p{anchor,wrap(tau*2400*clock-phase.high),
                first+settle+phase_margin,first+settle+aperture-phase_margin,first+hp::dwell_samples};
            const bool accepted=noisy->set_phase(p);
            const auto clean_phase=cpu();require(accepted==clean->set_phase(p),"paired local phase acceptance mismatch");excluded_cpu+=cpu()-clean_phase;
            if(accepted&&d.first_seconds>=38){++anchors;
                const auto dwell=static_cast<unsigned>(std::llround(d.first_seconds/.4));
                if(dwell>=95&&dwell<100)heldout_anchor_mask|=1U<<(dwell-95);}
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
                    const bool support=noisy->phase_supported_at(dates[j])&&noisy->timing_supported_at(dates[j],p.output_first_sample+j);
                    if(trace.noisy.empty())trace.first=p.output_first_sample;
                    require(p.output_first_sample+j==trace.first+trace.noisy.size(),"noncontiguous output trace");
                    trace.noisy.push_back(x[j]);trace.supported.push_back(static_cast<unsigned char>(support));
                    ++output_samples;if(support)++supported_samples;
                }
                const auto clean_copy=cpu();trace.clean.insert(trace.clean.end(),y.begin(),y.begin()+static_cast<std::ptrdiff_t>(q.produced));excluded_cpu+=cpu()-clean_copy;
                pos+=p.consumed;input_samples+=p.consumed;
            }
        }
        cpu_seconds+=cpu()-feed_started-excluded_cpu;
    }
};

struct Metrics {unsigned windows=0,noisy_windows=0,full_windows=0;std::uint64_t sample_count=0;double frequency_rms=0,frequency_max=0,phase_rms=0,phase_max=0,noisy_frequency_rms=0,noisy_fit_se=0,sample_phase_rms=0,sample_phase_max=0;};
Metrics tone_metrics(const Trace& t,double gauge){
    Metrics out;constexpr std::uint64_t lag=128;
    const double fixed_phase=wrap(tau*(2400-hp::probe_frequency_hz)*gauge);
    for(unsigned d=95;d<100;++d){
        const auto lo=static_cast<std::uint64_t>(std::llround((d*.4+.08+gauge)*fs));
        const auto hi=lo+5760;
        if(!t.contains(lo)||!t.contains(hi-1))continue;
        bool full=true;for(auto i=lo;i<hi;++i)if(!t.support(i)){full=false;break;}
        C phase{},frequency{};unsigned n=0;
        for(auto i=lo;i+lag<hi;++i){
            if(!t.support(i)||!t.support(i+lag))continue;
            phase+=t.at(i,true)*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*i/fs,1.)-fixed_phase);
            frequency+=t.at(i+lag,true)*std::conj(t.at(i,true))*std::polar(1.,-tau*hp::probe_frequency_hz*lag/fs);++n;
        }
        if(n<1000)continue;
        if(full)++out.full_windows;
        const double f=std::arg(frequency)*fs/(tau*lag),p=std::arg(phase);
        ++out.windows;out.frequency_rms+=f*f;out.frequency_max=std::max(out.frequency_max,std::abs(f));out.phase_rms+=p*p;out.phase_max=std::max(out.phase_max,std::abs(p));
        std::array<double,24>angles{};bool valid=true;
        for(unsigned b=0;b<24;++b){C z{};
            for(std::uint64_t j=0;j<240;++j){const auto i=lo+b*240+j;if(!t.support(i)){valid=false;break;}
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
        if(source_date<38.02||source_date>39.98||!t.support(n))continue;
        const auto value=t.at(n,true);if(std::norm(value)<1e-12)continue;
        const double error=std::arg(value*std::polar(1.,-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-fixed_phase));
        ++out.sample_count;out.sample_phase_rms+=error*error;out.sample_phase_max=std::max(out.sample_phase_max,std::abs(error));
    }
    if(out.sample_count)out.sample_phase_rms=std::sqrt(out.sample_phase_rms/static_cast<double>(out.sample_count));
    return out;
}

struct Evm {unsigned symbols=0;double noisy=0,clean=0;};
Evm evm_metrics(const Trace& trace,const hp::Source& source,double gauge){
    constexpr int span=640,sps=80;
    std::array<double,2*span+1> taps{};double energy=0;
    for(int j=-span;j<=span;++j){const double v=datapump::fast::root_raised_cosine(static_cast<double>(j)/sps,.2);taps[static_cast<std::size_t>(j+span)]=v;energy+=v*v;}
    const double scale=std::sqrt(energy/sps)/energy;
    const double phase=wrap(tau*(2400-hp::probe_frequency_hz)*gauge);
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
            if(!trace.support(n)||(fraction>0&&!trace.support(n+1))){accepted=false;break;}
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
struct Chain {
    std::string name;
    double reference_rf=30e6;
    bool use_joint=false,use_dual=false;
    Receiver receiver;
    h::HelperRelativeClock clock;
    h::HelperJointEstimator joint;
    h::HelperDualPilotPhase dual;
    Stream stream;
    Frozen frozen;
    Coeff retained_correction;double retained_rf=0;bool have_retained_correction=false;
    h::HelperRelativeClock::Estimate old_clock;
    h::HelperJointEstimator::Estimate joint_state;
    double extra_cpu=0,extra_cpu36=0,latest_joint_input=-std::numeric_limits<double>::infinity();
    bool joint_suspended=false,current_joint_accepted=false,joint_ever_established=false;
    unsigned joint_recovery_streak=0,joint_contradictions=0,joint_model_contradictions=0,joint_authority_rejections=0;
    unsigned latest_accepted_count=0,joint_inputs=0,joint_qualified=0,clock_outside=0,dual_combined=0,dual_fallback=0,dual_inconsistent=0;
    unsigned frozen_frequency_misses=0,heldout_count=0;
    double frozen_error=0,frozen_heldout_square=0,frozen_heldout_max=0,clock_error_max=0;
    Errors current_phase,heldout_phase,difference;
    std::vector<double> phase_errors;
    explicit Chain(std::string label,h::HelperAidedFrequencyMethod method,bool fitted,bool blended,
                   const h::HelperMeasurementConfig& mc,double rf)
        :name(std::move(label)),reference_rf(rf),use_joint(fitted),use_dual(blended),receiver(name,true,mc),
         clock([]{h::HelperRelativeClock::Config c;c.differential_phase_half_width_radians=.02;return c;}()),
         joint([&]{h::HelperJointEstimator::Config c;c.reference_shift_hz=rf;return c;}()),dual({}) {
        receiver.aided_config.frequency_method=method;
    }
    std::optional<h::HelperAidedPhasePrediction> prior_for(const hp::Dwell& d){
        if(!use_joint||joint_suspended)return {};
        const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
        const auto hi=joint.predict(2400,d.shift_hz,date),lo=joint.predict(550,d.shift_hz,date);
        if(!hi.valid||!lo.valid)return {};
        return h::HelperAidedPhasePrediction{true,hi.frequency_hz,lo.frequency_hz,
            hi.stochastic_sigma_hz+hi.model_half_width_hz,lo.stochastic_sigma_hz+lo.model_half_width_hz};
    }
    h::HelperRelativeClock::Estimate clock_at(double time,double rf)const{
        if(!use_joint)return clock.predict(time);
        const auto j=joint.predict(1600,rf,time);
        h::HelperRelativeClock::Estimate c;c.valid=j.valid&&!joint_suspended&&current_joint_accepted;c.reference_preserved=j.reference_preserved;
        c.reference_initialized=j.reference_initialized;c.reference_input_seconds=j.reference_input_seconds;
        c.input_seconds=time;c.source_seconds=j.source_seconds;c.audio_scale=j.audio_scale;
        c.source_half_width_seconds=3*j.source_stochastic_sigma_seconds+j.source_model_half_width_seconds;
        c.audio_half_width_ppm=3*j.audio_stochastic_sigma_ppm+j.audio_model_half_width_ppm;
        c.audio_drift_ppm_per_second=j.audio_drift_ppm_per_second;return c;
    }
    void observe(const hp::Dwell& d,bool missing,unsigned index){
        const auto before=cpu();auto prior=prior_for(d);extra_cpu+=cpu()-before;
        receiver.observe(d,missing,index,prior);
        const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
        const auto started=cpu();
        const auto previous=joint.predict(1600,d.shift_hz,date);
        joint_ever_established=joint_ever_established||previous.valid;
        const double frequency_date=receiver.phase_from_aided?receiver.last_aided.center_time_seconds:receiver.measurement.time_seconds;
        const auto previous_high=joint.predict(2400,d.shift_hz,frequency_date);
        const auto previous_low=joint.predict(550,d.shift_hz,frequency_date);
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
        const bool frequency_contradiction=use_joint&&innovation_available&&!innovation_consistent;
        if(frequency_contradiction){joint_suspended=true;joint_recovery_streak=0;++joint_contradictions;}
        const double previous_input=latest_joint_input;
        if(use_joint){
            h::HelperJointEstimator::Input x;x.time_seconds=date;x.frequency_time_seconds=date;x.shift_hz=d.shift_hz;x.model_contradicted=frequency_contradiction;
            const auto& r=receiver.last_aided;
            x.valid=!missing&&r.valid&&r.thermal_covariance_valid;
            if(x.valid){x.time_seconds=r.center_time_seconds;x.frequency_time_seconds=r.center_time_seconds;
                x.high_frequency_hz=r.measurement.high_frequency_hz;x.low_frequency_hz=r.measurement.low_frequency_hz;
                x.high_center_phase_radians=r.high_center_phase_radians;x.low_center_phase_radians=r.low_center_phase_radians;
                x.thermal_covariance=r.thermal_covariance;x.common_frequency_sigma_hz=r.measurement.common_phase_frequency_sigma_hz;
                x.frequency_model_half_width_hz=std::max(r.high_frequency_model_half_width_hz,r.low_frequency_model_half_width_hz);
                ++joint_inputs;}
            const auto updated=joint.observe(x);
            current_joint_accepted=x.valid&&updated.accepted_observations>latest_accepted_count;
            if(current_joint_accepted){latest_accepted_count=static_cast<unsigned>(updated.accepted_observations);latest_joint_input=x.time_seconds;}
            if(!frequency_contradiction&&internal_model_conflict(x.valid,joint_ever_established,current_joint_accepted,updated)){
                joint_suspended=true;joint_recovery_streak=0;++joint_model_contradictions;
            }
            const auto prediction_now=joint.predict(1600,d.shift_hz,date);
            joint_ever_established=joint_ever_established||prediction_now.valid;
            advance_recovery(joint_suspended,joint_recovery_streak,current_joint_accepted&&prediction_now.valid,innovation_available,innovation_consistent);
            joint_state=prediction_now;
            if(joint_suspended||!current_joint_accepted){joint_state.valid=false;++joint_authority_rejections;}
        }else{
            h::HelperRelativeClock::Input x;x.valid=!missing&&receiver.phase_from_aided&&receiver.phase.valid;
            x.time_seconds=date;
            if(x.valid){x.time_seconds=receiver.phase.date;x.high_phase_radians=receiver.phase.high;x.low_phase_radians=receiver.phase.low;x.phase_difference_sigma_radians=receiver.phase.difference_sigma;}
            old_clock=clock.observe(x);
        }
        if(use_dual&&!joint_suspended&&current_joint_accepted&&receiver.phase.valid&&receiver.last_aided.thermal_covariance_valid){
            const auto& r=receiver.last_aided;
            h::HelperDualPilotPhaseInput in;in.valid=true;in.time_seconds=r.center_time_seconds;
            in.high_phase_radians=r.high_center_phase_radians;in.low_phase_radians=r.low_center_phase_radians;
            in.thermal_covariance={{{r.pilot_thermal_covariance[2][2],r.pilot_thermal_covariance[2][3]},
                                    {r.pilot_thermal_covariance[3][2],r.pilot_thermal_covariance[3][3]}}};
            in.common_phase_sigma_radians=r.common_center_phase_sigma_radians;
            // Predictor model radius covers PAST deviations. The NEW hop may
            // independently vary low-minus-high transfer about that unknown
            // constant by the declared bound; it is not averaged away.
            in.low_model_half_width_radians=.02;
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
            }else{++dual_fallback;if(result.failure==h::HelperDualPilotPhaseFailure::inconsistent_difference)++dual_inconsistent;}
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
    void evaluate(const ht::Source& source,const hp::Dwell& d,unsigned index){
        receiver.evaluate_phase(source,d,index);
        if(receiver.phase.valid){
            const double sample=(receiver.phase.date-d.first_seconds)*fs;
            const auto i=static_cast<std::size_t>(std::floor(sample));const double f=sample-static_cast<double>(i);
            const double common=std::arg(C(d.common_phasor[i]))+f*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
            const double true_h=wrap(tau*std::remainder(2400*source.tx_time(receiver.phase.date),1.)+common);
            const double error=wrap(receiver.phase.high-true_h);
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
    void condition(const hp::Dwell& d,bool missing,unsigned index,const ht::Source& source){
        if(index<94)return;
        const auto state=receiver.snapshot(true);const auto now=cpu();
        Coeff c;h::HelperRelativeClock::Estimate coordinate;
        if(use_joint){const auto high=joint.predict(2400,d.shift_hz,d.first_seconds+.2);coordinate=clock_at(d.first_seconds+.2,d.shift_hz);
            c={coordinate.audio_scale,high.frequency_hz-2400*coordinate.audio_scale};
        }else{coordinate=clock.predict(d.first_seconds+.2);c=state.valid?coefficient(state,d.shift_hz,d.first_seconds+.2):Coeff{};
            if(coordinate.valid)c={coordinate.audio_scale,c.offset+2400*(c.audio-coordinate.audio_scale)};}
        const auto anchor=clock_at(receiver.phase.date,d.shift_hz);
        const auto initial=clock_at(d.first_seconds,d.shift_hz);
        const bool usable_coefficients=coordinate.valid&&(use_joint?joint_state.valid:state.valid);
        if(usable_coefficients){retained_correction=c;retained_rf=d.shift_hz;have_retained_correction=true;}
        else if(have_retained_correction){c=retained_correction;c.offset*=d.shift_hz/retained_rf;}
        else c={};
        extra_cpu+=cpu()-now;
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        if(!stream.initialized&&usable_coefficients&&!missing&&receiver.phase.valid&&initial.valid&&initial.reference_preserved&&(use_joint?joint_state.valid:state.valid))
            stream.initialize(first,c,initial.source_seconds,initial.reference_input_seconds,source);
        if(stream.initialized)stream.feed(d,c,receiver.phase,(use_joint?joint_state.valid:state.valid)&&!missing&&coordinate.valid,source,anchor);
    }
};

double quantile(std::vector<double> values,double p){
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    const auto n=static_cast<std::size_t>(std::ceil(p*static_cast<double>(values.size())));
    return values[std::min(values.size()-1,n?n-1:0)];
}
void print_chain(Chain& chain,const ht::Source& source,const std::string& impairment,
                 double generation,double metrology,const Metrics& m,const Evm& evm){
    const auto& r=chain.receiver;const auto& stream=chain.stream;
    std::cout<<std::setprecision(10)<<"helper_refinement method="<<chain.name<<" mode="<<impairment
        <<" seed="<<source.config().seed<<" loss_db="<<source.config().path_loss_db<<" schedule="<<schedule(source.config())
        <<" helper_w="<<source.config().helper_w<<" probe_w="<<source.config().probe_w
        <<" probe="<<(source.config().probe==hp::Probe::tone?"tone":"apsk64")
        <<" temperature_K="<<source.config().temperature_kelvin<<" noise_figure_db="<<source.config().noise_figure_db
        <<" high_cn0_db_hz="<<source.base().high_cn0_db_hz()<<" probe_cn0_db_hz="<<source.base().probe_cn0_db_hz()
        <<" observation_seconds=36 lead_seconds=38 capture_seconds=40 buffering_latency_seconds=.4"
        <<" broad_calls="<<r.broad_calls<<" aided_calls="<<r.aided_calls<<" fallback_calls="<<r.fallbacks
        <<" valid_dwells="<<r.valid<<" residual_rejections="<<r.residual_rejections
        <<" phase_evolution_rejections="<<r.evolution_rejections<<" signal_rejections="<<r.signal_rejections
        <<" frequency_restarts="<<r.model_restarts
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
        <<" dual_combined="<<chain.dual_combined<<" dual_fallback="<<chain.dual_fallback<<" dual_difference_rejections="<<chain.dual_inconsistent
        <<" phase_anchors="<<stream.anchors<<" heldout_anchor_mask="<<stream.heldout_anchor_mask
        <<" supported_samples="<<stream.supported_samples<<" output_samples="<<stream.output_samples
        <<" conditioning_first_input_seconds="<<stream.first_input_seconds
        <<" reference_input_seconds="<<stream.reference_input<<" evaluator_constant_gauge_seconds="<<stream.gauge
        <<" remaining_ambiguity=one_constant_unknown_time_and_phase"
        <<" timing_max_us="<<stream.timing_error_max*1e6<<" timing_change_us="<<(stream.last_timing_error-stream.first_timing_error)*1e6
        <<" cycle_slips="<<stream.cycle_slips<<" cycle_error_observations="<<stream.cycle_error_observations
        <<" tone_windows="<<m.windows<<" full_tone_windows="<<m.full_windows
        <<" twin_frequency_rms_hz="<<m.frequency_rms<<" twin_frequency_max_hz="<<m.frequency_max
        <<" twin_phase_rms_deg="<<m.phase_rms*180/std::numbers::pi<<" twin_phase_max_deg="<<m.phase_max*180/std::numbers::pi
        <<" admitted_tone_samples="<<m.sample_count<<" admitted_sample_phase_max_deg="<<m.sample_phase_max*180/std::numbers::pi
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
        <<" generation_cpu_seconds="<<generation<<" metrology_cpu_seconds="<<metrology<<'\n';
}

struct MethodOutcome {
    std::string name;bool frozen=false,reference_initialized=false,reference_preserved=false,suspended=false;
    unsigned windows=0,anchors=0,mask=0,cycle_slips=0,contradictions=0,clock_misses=0,authority_rejections=0;
    double reference=0,frequency_max=0,phase_max=0,first_input_seconds=0;
};
struct Outcome {unsigned qualified36=0,window_count=0,cycle_slips=0;double maximum_admitted_phase=0;std::vector<MethodOutcome> methods;
    const MethodOutcome& at(const std::string& name)const{for(const auto& x:methods)if(x.name==name)return x;throw std::runtime_error("missing method outcome");}
};
Outcome scenario(hp::Config config,ht::Impairments impairment,const std::string& mode,const std::string& methods,bool required=false){
    ht::Source source(config,impairment);
    const double max_rf=config.shift_center_hz>1e9?(config.microwave_eu20?5868.4e6:5846.8e6):31.225e6;
    const double prior_ppm=config.shift_center_hz>1e9?.0001:1;
    const double width=3*(max_rf*prior_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        config.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    using Method=h::HelperAidedFrequencyMethod;
    std::vector<std::unique_ptr<Chain>> chains;
    const auto add=[&](const std::string& name,Method method,bool joint,bool dual){
        const auto before=cpu();auto c=std::make_unique<Chain>(name,method,joint,dual,mc,config.shift_center_hz);
        c->extra_cpu+=cpu()-before;chains.push_back(std::move(c));};
    if(methods=="all"||methods=="frequency"||methods=="finalists"||methods=="legacy")add("legacy-clock",Method::subaperture_slope,false,false);
    if(methods=="all"||methods=="frequency"||methods=="whole")add("whole-clock",Method::whole_hann,false,false);
    if(methods=="all"||methods=="frequency"||methods=="finalists"||methods=="partitioned")add("partitioned-clock",Method::partitioned_hann,false,false);
    if(methods=="all"||methods=="joint"||methods=="joint-whole")add("whole-joint",Method::whole_hann,true,false);
    if(methods=="all"||methods=="joint"||methods=="finalists"||methods=="joint-partitioned")add("partitioned-joint",Method::partitioned_hann,true,false);
    if(methods=="all"||methods=="finalists"||methods=="dual")add("partitioned-joint-dual",Method::partitioned_hann,true,true);
    require(!chains.empty(),"unknown method set");
    double generation=0,oracle_cpu=0;Stream oracle;
    for(unsigned dwell=0;dwell<100;++dwell){
        const auto before=cpu();const auto d=source.generate(dwell);generation+=cpu()-before;
        const bool missing=source.missing(dwell);
        for(auto& chain:chains){chain->observe(d,missing,dwell);chain->evaluate(source,d,dwell);chain->condition(d,missing,dwell,source);}
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
    std::cout<<std::setprecision(10)<<"helper_refinement_oracle seed="<<config.seed<<" loss_db="<<config.path_loss_db<<" mode="<<mode
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
        print_chain(*chain,source,mode,generation,metrology,tone,evm);
        result.qualified36+=chain->frozen.valid;result.window_count+=tone.full_windows;result.cycle_slips+=chain->stream.cycle_slips;
        result.maximum_admitted_phase=std::max(result.maximum_admitted_phase,tone.sample_phase_max);
        result.methods.push_back({chain->name,chain->frozen.valid,
            chain->use_joint?chain->joint_state.reference_initialized:chain->old_clock.reference_initialized,
            chain->use_joint?chain->joint_state.reference_preserved:chain->old_clock.reference_preserved,
            chain->joint_suspended,tone.full_windows,chain->stream.anchors,chain->stream.heldout_anchor_mask,
            chain->stream.cycle_slips,chain->joint_contradictions,chain->clock_outside,chain->joint_authority_rejections,
            chain->use_joint?chain->joint_state.reference_input_seconds:chain->old_clock.reference_input_seconds,
            tone.frequency_max,tone.sample_phase_max,chain->stream.first_input_seconds});
        if(required){require(chain->frozen.valid,"clean matched chain lacked 36-second snapshot");
            if(config.probe==hp::Probe::tone)require(tone.full_windows>=4,"clean matched chain lacked supported tone");
            else require(evm.symbols>=250,"clean matched chain lacked supported APSK");}
    }
    return result;
}

ht::Impairments impairment_named(const std::string& mode){
    ht::Impairments p;
    if(mode=="gap"){p.missing_first=91;p.missing_count=2;}
    else if(mode=="gap-init"){p.missing_first=90;p.missing_count=5;}
    else if(mode=="long-gap"){p.missing_first=70;p.missing_count=24;}
    else if(mode=="step")p.frequency_step_hz=1.2;
    else if(mode=="curvature"){p.frequency_change_seconds=18;p.frequency_curvature_hz_per_second2=.003;}
    else if(mode=="differential")p.changing_low_phase_radians=.02;
    else if(mode=="differential-large"){p.differential_first=91;p.changing_low_phase_radians=.8;}
    else if(mode=="retune")p.late_retune_dwell=95;
    else if(mode=="retune-mid"){p.late_retune_dwell=95;p.late_retune_seconds=.12;}
    else if(mode=="noise")p.noise_only=true;
    else if(mode!="clean"&&mode!="drift"&&mode!="negative-clock")throw std::invalid_argument("unknown impairment");
    return p;
}
void short_probe(){
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
    hp::Config config;config.seed=901;ht::Source source(config);
    h::HelperMeasurementConfig mc{fs,550,2400,2300,2500,300,.05*std::numbers::pi/180};
    using Method=h::HelperAidedFrequencyMethod;
    std::vector<std::unique_ptr<Chain>> chains;
    chains.push_back(std::make_unique<Chain>("legacy-clock",Method::subaperture_slope,false,false,mc,30e6));
    chains.push_back(std::make_unique<Chain>("whole-clock",Method::whole_hann,false,false,mc,30e6));
    chains.push_back(std::make_unique<Chain>("partitioned-clock",Method::partitioned_hann,false,false,mc,30e6));
    for(unsigned i=0;i<8;++i){const auto d=source.generate(i);for(auto& c:chains){c->observe(d,false,i);c->evaluate(source,d,i);}}
    for(auto& c:chains){require(c->receiver.valid>=6,"short refinement probe failed pilot acquisition");
        std::cout<<"helper_refinement_short method="<<c->name<<" capture_seconds=3.2 helper_w=40 loss_db=180 valid_dwells="<<c->receiver.valid
            <<" snapshot36=unavailable frequency_rms_hz="<<std::sqrt(c->receiver.frequency_error2/c->receiver.frequency_errors)
            <<" reported_sigma_rms_hz="<<std::sqrt(c->receiver.frequency_sigma2/c->receiver.frequency_errors)
            <<" phase_rms_deg="<<c->current_phase.rms()*180/std::numbers::pi<<'\n';}
}
void robustness(){
    hp::Config c;c.path_loss_db=180;c.seed=901;
    const auto clean=scenario(c,{},"clean","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"}){
        const auto& x=clean.at(method);require(x.frozen&&x.windows==5&&x.anchors==5&&x.cycle_slips==0,"clean finalist did not establish nonvacuous support");
    }
    auto problem=impairment_named("noise");const auto noise=scenario(c,problem,"noise","finalists");
    for(const auto& x:noise.methods)require(!x.frozen&&x.anchors==0&&x.windows==0,"noise-only input falsely qualified refinement");
    problem=impairment_named("step");const auto step=scenario(c,problem,"step","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"}){
        const auto& x=step.at(method);require(x.frozen&&x.contradictions>0&&x.authority_rejections>0,"frequency step lacked a fresh contradiction witness");
        require((x.mask&1U)==0,"first stale post-step frequency model falsely authorized a hop");
        require(x.cycle_slips==0&&x.phase_max<.75&&x.frequency_max<.5,"step correction admitted large residual errors");
        require(x.reference_initialized&&std::abs(x.reference-clean.at(method).reference)<1e-12,"step selected a new time reference");
    }
    problem=impairment_named("gap");const auto gap=scenario(c,problem,"gap","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"}){
        const auto& x=gap.at(method);require(x.reference_preserved&&x.windows==5&&x.cycle_slips==0,"short pilot gap did not preserve useful reference");
        require(std::abs(x.reference-clean.at(method).reference)<1e-12,"pilot gap silently selected a new origin");
    }
    problem=impairment_named("gap-init");const auto init_gap=scenario(c,problem,"gap-init","finalists");
    for(const auto& x:init_gap.methods)require(x.first_input_seconds==0||x.first_input_seconds>=38,"expired midpoint prediction initialized a conditioner with default coefficients");
    problem=impairment_named("long-gap");const auto long_gap=scenario(c,problem,"long-gap","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"}){
        const auto& x=long_gap.at(method);require(!x.frozen&&x.reference_initialized&&std::abs(x.reference-clean.at(method).reference)<1e-12,"long gap claimed unavailable 36-second fit or reseeded");
        require(x.windows==0||x.reference_preserved,"long gap admitted output without reference coverage");
    }
    problem=impairment_named("curvature");const auto curvature=scenario(c,problem,"curvature","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"})require(!curvature.at(method).frozen,"curved frequency model falsely qualified 36-second narrowing");
    problem=impairment_named("retune-mid");const auto retune=scenario(c,problem,"retune-mid","finalists");
    for(const auto* method:{"partitioned-joint","partitioned-joint-dual"}){
        const auto& x=retune.at(method);require((x.mask&1U)==0&&x.phase_max<.75,"settling transient was hidden inside admitted support");
    }
    std::cout<<"helper_refinement_robustness passed=1 checks=clean_noise_step_gap_init_gap_long_gap_curvature_retune\n";
}

}
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--probe"){short_probe();return 0;}
    if(argc>1&&std::string(argv[1])=="--robustness"){short_probe();robustness();return 0;}
    hp::Config config;std::string mode="clean",methods="all";
    if(argc>1&&std::string(argv[1])=="--case"){
        if(argc<4||argc>9)throw std::invalid_argument("--case LOSS SEED [hf|microwave|microwave-eu] [MODE] [tone|apsk] [PROBE_W] [METHODS]");
        config.path_loss_db=std::stod(argv[2]);config.seed=std::stoull(argv[3]);
        if(argc>4){const std::string profile=argv[4];if(profile=="microwave"||profile=="microwave-eu"){
            config.shift_center_hz=5.8e9;config.relative_shift_ppm=.00008;config.phase_noise_degrees_per_sqrt_second=.005;config.microwave_eu20=profile=="microwave-eu";
        }else if(profile!="hf")throw std::invalid_argument("unknown RF profile");}
        if(argc>5)mode=argv[5];
        if(argc>6){const std::string probe=argv[6];if(probe=="apsk")config.probe=hp::Probe::apsk64;else if(probe!="tone")throw std::invalid_argument("unknown probe");}
        if(argc>7)config.probe_w=std::stod(argv[7]);
        if(argc>8)methods=argv[8];
        if(mode=="drift"){config.tx_audio_drift_ppm_per_second=.04;config.relative_shift_drift_ppm_per_second=.00002;}
        if(mode=="negative-clock"){config.tx_audio_ppm=-37;config.rx_audio_ppm=13;}
        scenario(config,impairment_named(mode),mode,methods);return 0;
    }
    if(argc>1&&std::string(argv[1])!="--suite")throw std::invalid_argument("expected --probe, --case or --suite");
    short_probe();config.path_loss_db=160;scenario(config,{},"clean","frequency",true);
    config.path_loss_db=180;scenario(config,{},"clean","frequency",true);return 0;
}catch(const std::exception& e){std::cerr<<"Helper refinement study failure: "<<e.what()<<'\n';return 1;}}
