#include "datapump/helper_aided_phase.hpp"
#include "datapump/helper_relative_clock.hpp"
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
    double acquisition_cpu=0,tracking_cpu=0,fit_cpu=0,cpu36=0,observed_cpu=0,external_cpu=0;
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
    void observe(const hp::Dwell& d,bool missing,unsigned index){
        const auto observed_started=cpu();
        phase={};measurement={};phase_from_aided=false;
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
            const auto a=cpu();auto result=h::estimate_helper_aided_phase(data,aided_config,prediction,d.shift_hz,start);
            tracking_cpu+=cpu()-a;++aided_calls;
            if(result.valid){
                const auto f=cpu();require(fit.ingest(result.measurement),"aided measurement intake failed");fit_cpu+=cpu()-f;
                measurement=result.measurement;
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
            acquisition_cpu+=cpu()-a;++broad_calls;phase=broad_phase(measurement);}
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
    bool initialized=false;
    void initialize(std::uint64_t first,Coeff c,double source,double reference,const ht::Source& simulator){
        const auto initialized_started=cpu();
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

void print_metrics(const std::string& label,const std::string& mode,Receiver& r,Stream& stream,const ht::Source& source,double generation,double metrology,const Metrics& m,const Evm& evm,double joint_cpu=0,double joint_cpu36=0){
    std::cout<<std::setprecision(9)<<"helper_tracking method="<<label<<" mode="<<mode<<" seed="<<source.config().seed<<" helper_w="<<source.config().helper_w
        <<" probe_w="<<source.config().probe_w<<" loss_db="<<source.config().path_loss_db
        <<" probe="<<(source.config().probe==hp::Probe::tone?"tone":"apsk64")
        <<" hop_schedule="<<schedule(source.config())
        <<" probe_cn0_db_hz="<<source.base().probe_cn0_db_hz()<<" probe_snr360_db="<<source.base().probe_cn0_db_hz()-10*std::log10(360.)
        <<" high_cn0_db_hz="<<source.base().high_cn0_db_hz()<<" temperature_K="<<source.config().temperature_kelvin
        <<" noise_figure_db="<<source.config().noise_figure_db<<" rf_center_hz="<<source.config().shift_center_hz
        <<" broad_calls="<<r.broad_calls<<" aided_calls="<<r.aided_calls<<" fallback_calls="<<r.fallbacks<<" valid_dwells="<<r.valid
        <<" residual_rejections="<<r.residual_rejections<<" evolution_rejections="<<r.evolution_rejections<<" signal_rejections="<<r.signal_rejections
        <<" frequency_model_restarts="<<r.model_restarts<<" snapshot36="<<bool(r.frozen)
        <<" acquisition_cpu_s="<<r.acquisition_cpu<<" tracking_cpu_s="<<r.tracking_cpu<<" fit_cpu_s="<<r.fit_cpu
        <<" receiver_cpu36_s="<<(r.cpu36+joint_cpu36)<<" conditioning_cpu_s="<<stream.cpu_seconds<<" conditioned_signal_seconds="<<static_cast<double>(stream.input_samples)/fs
        <<" relative_clock_cpu_s="<<joint_cpu<<" receiver_cpu40_s="<<(r.total_cpu()+stream.cpu_seconds+joint_cpu)
        <<" generation_cpu_s="<<generation<<" metrology_cpu_s="<<metrology
        <<" phase_measurements="<<r.phase_count<<" phase_rms_deg="<<(r.phase_count?std::sqrt(r.phase_error2/r.phase_count)*180/std::numbers::pi:0)
        <<" phase_max_deg="<<r.phase_max*180/std::numbers::pi<<" heldout_phase_measurements="<<r.phase_heldout
        <<" heldout_phase_rms_deg="<<(r.phase_heldout?std::sqrt(r.heldout_error2/r.phase_heldout)*180/std::numbers::pi:0)
        <<" heldout_phase_max_deg="<<r.heldout_max*180/std::numbers::pi<<" phase_anchors="<<stream.anchors<<" phase_aperture_margin_s="<<phase_margin/fs
        <<" heldout_anchor_mask="<<stream.heldout_anchor_mask
        <<" supported_samples="<<stream.supported_samples<<" output_samples="<<stream.output_samples
        <<" timing_rejections="<<stream.timing_rejections<<" model_rejections="<<stream.model_rejections
        <<" relative_reference_input_s="<<stream.reference_input<<" evaluator_only_fixed_gauge_s="<<stream.gauge
        <<" remaining_acquisition_ambiguity=one_constant_time_and_phase"
        <<" conditioning_frequency=continued_helper_model"
        <<" timing_error_max_us="<<stream.timing_error_max*1e6<<" timing_error_change_us="<<(stream.last_timing_error-stream.first_timing_error)*1e6
        <<" cycle_slips="<<stream.cycle_slips<<" cycle_error_observations="<<stream.cycle_error_observations
        <<" tone_windows="<<m.windows<<" full_tone_windows="<<m.full_windows<<" twin_frequency_rms_hz="<<m.frequency_rms<<" twin_frequency_max_hz="<<m.frequency_max
        <<" twin_phase_rms_deg="<<m.phase_rms*180/std::numbers::pi<<" twin_phase_max_deg="<<m.phase_max*180/std::numbers::pi
        <<" admitted_probe_samples="<<m.sample_count<<" admitted_sample_phase_rms_deg="<<m.sample_phase_rms*180/std::numbers::pi
        <<" admitted_sample_phase_max_deg="<<m.sample_phase_max*180/std::numbers::pi
        <<" noisy_tone_windows="<<m.noisy_windows<<" noisy_frequency_rms_hz="<<m.noisy_frequency_rms<<" noisy_frequency_fit_se_hz="<<m.noisy_fit_se
        <<" apsk_symbols="<<evm.symbols<<" noisy_apsk_evm="<<evm.noisy<<" twin_apsk_evm="<<evm.clean;
    if(r.frozen){const auto c=coefficient(*r.frozen,source.config().shift_center_hz,36);
        const double dt=36-r.frozen->time_seconds;
        const double v[]{1600e-6,source.config().shift_center_hz*1e-6,1600e-6*dt,source.config().shift_center_hz*1e-6*dt};double variance=0;
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)variance+=v[i]*r.frozen->covariance[i][j]*v[j];
        const double truth=source.audio_scale_at(36)*1600+source.shift_error_hz(source.config().shift_center_hz,36);
        std::cout<<" frequency36_error_hz="<<(c.audio*1600+c.offset-truth)<<" frequency36_3sigma_hz="<<3*std::sqrt(std::max(0.,variance));}
    std::cout<<'\n';
}

struct Result {bool frozen=false;unsigned aided_anchors=0,joint_anchors=0,aided_windows=0,joint_windows=0;unsigned fallbacks=0;bool reference_preserved=false,clock36=false;double clock_reference=0,aided_sample_phase_max=0,joint_sample_phase_max=0;unsigned aided_anchor_mask=0,joint_anchor_mask=0;double joint_frequency_max=0;unsigned joint_cycle_slips=0,clock_truth_outside=0;};
Result scenario(hp::Config config,ht::Impairments impairments,bool required,const std::string& mode="clean"){
    ht::Source source(config,impairments);
    const double max_rf=config.shift_center_hz>1e9?(config.microwave_eu20?5868.4e6:5846.8e6):31.225e6;
    const double shift_ppm=config.shift_center_hz>1e9?.0001:1;
    const double width=3*(max_rf*shift_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        config.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    const auto broad_constructed=cpu();Receiver broad("broad",false,mc);broad.external_cpu+=cpu()-broad_constructed;
    const auto aided_constructed=cpu();Receiver aided("aided",true,mc);aided.external_cpu+=cpu()-aided_constructed;
    h::HelperRelativeClock::Config clock_config;
    clock_config.differential_phase_half_width_radians=.02;
    const auto clock_constructed=cpu();h::HelperRelativeClock clock(clock_config);
    double clock_cpu=cpu()-clock_constructed,clock_cpu36=0,generation_cpu=0;
    std::optional<h::HelperRelativeClock::Estimate> clock36;
    struct Narrowing {bool valid=false;double frequency=0,half_width=0,high_half=0,clock_half=0;} joint36;
    h::HelperRelativeClock::Estimate last_clock;
    unsigned clock_qualified=0,clock_truth_outside=0;
    double clock_truth_error_max=0,clock_truth_radius_max=0;
    Stream broad_stream,aided_stream,joint_stream;
    double reference=.02+(aperture-1)/(2*fs);
    for(unsigned i=0;i<100;++i){
        const auto a=cpu();auto d=source.generate(i);generation_cpu+=cpu()-a;
        const bool missing=source.missing(i);
        broad.observe(d,missing,i);aided.observe(d,missing,i);
        broad.evaluate_phase(source,d,i);aided.evaluate_phase(source,d,i);
        h::HelperRelativeClock::Input input;
        // Only the aided phases share a common Hann weighting and direct
        // center-date uncertainty. Broad acquisition remains frequency-only
        // for this clock: it does not choose a phase reference with transported
        // noisy slopes that can generate artificial initial branch explosion.
        input.valid=aided.phase.valid&&aided.phase_from_aided&&!missing;input.time_seconds=d.first_seconds+reference;
        if(input.valid){input.time_seconds=aided.phase.date;input.high_phase_radians=aided.phase.high;input.low_phase_radians=aided.phase.low;input.phase_difference_sigma_radians=aided.phase.difference_sigma;}
        const auto cstart=cpu();auto cs=clock.observe(input);last_clock=cs;clock_cpu+=cpu()-cstart;
        if(cs.valid){
            // Evaluation only: every claimed interval retains the same
            // initial clock reference. No probe sample supplies this gauge.
            const double error=cs.source_seconds-source.tx_time(cs.input_seconds)-
                (cs.reference_input_seconds-source.tx_time(cs.reference_input_seconds));
            ++clock_qualified;if(std::abs(error)>cs.source_half_width_seconds)++clock_truth_outside;
            clock_truth_error_max=std::max(clock_truth_error_max,std::abs(error));
            clock_truth_radius_max=std::max(clock_truth_radius_max,cs.source_half_width_seconds);
        }
        if(i==89){
            const auto now=cpu();const auto at36=clock.predict(36);if(at36.valid)clock36=at36;
            if(clock36&&aided.frozen){
                const auto global=coefficient(*aided.frozen,config.shift_center_hz,36);
                const double dt=36-aided.frozen->time_seconds;
                const double row[]{2400e-6,config.shift_center_hz*1e-6,2400e-6*dt,config.shift_center_hz*1e-6*dt};
                double variance=0;for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)variance+=row[j]*aided.frozen->covariance[j][k]*row[k];
                joint36.valid=true;joint36.high_half=3*std::sqrt(std::max(0.,variance));
                joint36.clock_half=800e-6*clock36->audio_half_width_ppm;
                joint36.half_width=joint36.high_half+joint36.clock_half;
                joint36.frequency=2400*global.audio+global.offset-800*clock36->audio_scale;
            }
            clock_cpu+=cpu()-now;clock_cpu36=clock_cpu;
        }
        if(i<94)continue;
        const auto first=static_cast<std::uint64_t>(std::llround(d.first_seconds*fs));
        const double midpoint=d.first_seconds+.2;
        const auto bs=broad.snapshot(true),as=aided.snapshot(true);
        const auto bcoef_start=cpu();const auto bc=bs.valid?coefficient(bs,d.shift_hz,midpoint):(broad.frozen?coefficient(*broad.frozen,d.shift_hz,midpoint):Coeff{});broad.external_cpu+=cpu()-bcoef_start;
        const auto acoef_start=cpu();const auto ac=as.valid?coefficient(as,d.shift_hz,midpoint):(aided.frozen?coefficient(*aided.frozen,d.shift_hz,midpoint):Coeff{});aided.external_cpu+=cpu()-acoef_start;
        if(i==94){
            if(broad.frozen){const auto start=cpu();const double origin=36+elapsed_clock(*broad.frozen,36,d.first_seconds);broad.external_cpu+=cpu()-start;broad_stream.initialize(first,bc,origin,36,source);}
            if(aided.frozen){const auto start=cpu();const double origin=36+elapsed_clock(*aided.frozen,36,d.first_seconds);aided.external_cpu+=cpu()-start;aided_stream.initialize(first,ac,origin,36,source);}
        }
        // Creation may be delayed by an unresolved gap or model failure. Its
        // one original clock reference is retained; an existing stream is
        // never reinitialized when later evidence returns.
        if(!joint_stream.initialized&&aided.frozen&&as.valid){
            const auto start=cpu();const auto jc=clock.predict(d.first_seconds);
            if(jc.valid&&jc.reference_preserved){const auto old=coefficient(as,d.shift_hz,d.first_seconds);Coeff correction{jc.audio_scale,old.offset+2400*(old.audio-jc.audio_scale)};
                clock_cpu+=cpu()-start;
                joint_stream.initialize(first,correction,jc.source_seconds,jc.reference_input_seconds,source);
            }else clock_cpu+=cpu()-start;
        }
        if(broad_stream.initialized)broad_stream.feed(d,bc,broad.phase,bs.valid&&!missing,source);
        if(aided_stream.initialized)aided_stream.feed(d,ac,aided.phase,as.valid&&!missing,source);
        if(joint_stream.initialized){
            const auto start=cpu();const auto jc=clock.predict(midpoint),anchor=clock.predict(aided.phase.date);clock_cpu+=cpu()-start;
            const Coeff correction{jc.valid?jc.audio_scale:ac.audio,ac.offset+2400*(ac.audio-(jc.valid?jc.audio_scale:ac.audio))};
            joint_stream.feed(d,correction,aided.phase,as.valid&&!missing&&jc.valid,source,anchor);
        }
    }
    const auto metrology_start=cpu();
    const auto bm=config.probe==hp::Probe::tone?tone_metrics(broad_stream.trace,broad_stream.gauge):Metrics{};
    const auto am=config.probe==hp::Probe::tone?tone_metrics(aided_stream.trace,aided_stream.gauge):Metrics{};
    const auto jm=config.probe==hp::Probe::tone?tone_metrics(joint_stream.trace,joint_stream.gauge):Metrics{};
    const auto be=config.probe==hp::Probe::apsk64?evm_metrics(broad_stream.trace,source.base(),broad_stream.gauge):Evm{};
    const auto ae=config.probe==hp::Probe::apsk64?evm_metrics(aided_stream.trace,source.base(),aided_stream.gauge):Evm{};
    const auto je=config.probe==hp::Probe::apsk64?evm_metrics(joint_stream.trace,source.base(),joint_stream.gauge):Evm{};
    const double metrology_cpu=cpu()-metrology_start;
    print_metrics("broad",mode,broad,broad_stream,source,generation_cpu,metrology_cpu,bm,be);
    print_metrics("aided",mode,aided,aided_stream,source,generation_cpu,metrology_cpu,am,ae);
    print_metrics("joint",mode,aided,joint_stream,source,generation_cpu,metrology_cpu,jm,je,clock_cpu,clock_cpu36);
    std::cout<<"helper_tracking_clock mode="<<mode<<" seed="<<config.seed<<" loss_db="<<config.path_loss_db<<" hop_schedule="<<schedule(config)<<" valid36="<<bool(clock36)
        <<" clock_cpu36_s="<<clock_cpu36<<" receiver_joint_cpu36_s="<<(aided.cpu36+clock_cpu36)
        <<" reference_input_s="<<last_clock.reference_input_seconds<<" reference_preserved="<<last_clock.reference_preserved
        <<" alternatives="<<last_clock.alternatives<<" accepted="<<last_clock.accepted_observations<<" rejected="<<last_clock.rejected_observations
        <<" ambiguity_limited="<<last_clock.ambiguity_limited<<" work_limited="<<last_clock.work_limited<<" model_consistent="<<last_clock.model_consistent
        <<" differential_phase_bound_rad="<<clock_config.differential_phase_half_width_radians
        <<" qualified_intervals="<<clock_qualified<<" truth_outside_intervals="<<clock_truth_outside
        <<" truth_error_max_us="<<clock_truth_error_max*1e6<<" interval_radius_max_us="<<clock_truth_radius_max*1e6<<'\n';
    if(joint36.valid){
        const double truth=1600*source.audio_scale_at(36)+source.shift_error_hz(config.shift_center_hz,36);
        std::cout<<"helper_tracking_joint36 mode="<<mode<<" seed="<<config.seed<<" loss_db="<<config.path_loss_db
            <<" hop_schedule="<<schedule(config)
            <<" frequency_error_hz="<<joint36.frequency-truth<<" conditional_half_width_hz="<<joint36.half_width
            <<" high_global_three_sigma_hz="<<joint36.high_half<<" clock_frequency_half_width_hz="<<joint36.clock_half
            <<" bound=correlation_agnostic_sum_of_conditional_model_intervals\n";
    }
    if(required){require(broad.frozen&&aided.frozen,"comparison failed frequency acquisition");
        if(config.probe==hp::Probe::tone){require(am.full_windows>=4,"aided correction lacked supported heldout tone");
            require(am.frequency_rms<.1,"aided tone correction frequency inaccurate");}
        else require(ae.symbols>=250&&je.symbols>=250,"aided/joint APSK correction lacked supported symbols");}
    return {bool(aided.frozen),aided_stream.anchors,joint_stream.anchors,am.full_windows,jm.full_windows,aided.fallbacks,last_clock.reference_preserved,
        bool(clock36),last_clock.reference_input_seconds,am.sample_phase_max,jm.sample_phase_max,aided_stream.heldout_anchor_mask,joint_stream.heldout_anchor_mask,jm.frequency_max,joint_stream.cycle_slips,clock_truth_outside};
}

void robustness(){
    hp::Config config;config.path_loss_db=180;config.seed=901;
    ht::Impairments problem;problem.noise_only=true;
    const auto noise=scenario(config,problem,false,"noise");
    require(!noise.frozen&&noise.aided_anchors==0&&noise.joint_anchors==0,"noise-only input falsely qualified Helper correction");
    problem={};problem.missing_first=91;problem.missing_count=2;
    const auto gap=scenario(config,problem,false,"gap");
    require(gap.reference_preserved&&gap.aided_windows==5&&gap.joint_windows==5,"short gap did not recover within the original reference");
    require(std::abs(gap.clock_reference-(2.1999791666666666))<1e-8,"short gap selected a new clock origin");
    problem={};problem.missing_first=70;problem.missing_count=24;
    const auto long_gap=scenario(config,problem,false,"long-gap");
    require(!long_gap.clock36&&long_gap.reference_preserved,"long gap falsely claimed 36-second timing or reset its reference");
    require(std::abs(long_gap.clock_reference-gap.clock_reference)<1e-12,"long gap selected a new clock origin");
    require(long_gap.joint_windows>=1,"long gap never recovered useful tracking in its preserved reference");
    problem={};problem.frequency_step_hz=1.2;
    const auto step=scenario(config,problem,false,"step");
    require(step.fallbacks>0&&step.joint_windows>=1&&step.joint_windows<5,"frequency step did not withdraw then partially recover");
    require(step.reference_preserved&&step.joint_cycle_slips==0&&step.joint_sample_phase_max<.75&&step.joint_frequency_max<.5,
        "frequency step recovery lost its reference or admitted unbounded correction error");
    problem={};problem.late_retune_dwell=95;problem.late_retune_seconds=.12;
    const auto retune=scenario(config,problem,false,"retune-mid");
    require((retune.aided_anchor_mask&1U)==0&&(retune.joint_anchor_mask&1U)==0,"in-aperture retune falsely qualified its phase");
    require(retune.aided_sample_phase_max<.75&&retune.joint_sample_phase_max<.75,"retune recovery admitted a large supported phase error");
    config.path_loss_db=160;problem={};problem.differential_first=91;problem.changing_low_phase_radians=.8;
    const auto differential=scenario(config,problem,false,"differential-large");
    // This seeded changing channel is near zero on95/97/99, but approximately
    // -.800/+.798rad on96/98. Only those strong contradictions must withdraw;
    // finite-SNR near-zero values cannot prove the channel bound universally.
    const unsigned contradicted=(1U<<1)|(1U<<3),later=(1U<<2)|(1U<<4);
    require(differential.clock36&&(differential.joint_anchor_mask&contradicted)==0,
        "strong inconsistent differential phase falsely qualified a contradicted hop");
    require((differential.joint_anchor_mask&later)!=0&&differential.reference_preserved&&
        differential.joint_cycle_slips==0&&differential.clock_truth_outside==0,
        "differential phase recovery lost its fixed reference or conditional coverage");
    std::cout<<"helper_tracking_robustness checked=noise_short_gap_long_gap_step_retune_differential passed=1\n";
}

void short_probe(){
    std::array<h::HelperDwellMeasurement,6> support{};
    for(std::size_t i=0;i<support.size();++i)support[i].time_seconds=.2+.4*i;
    require(recent_frequency_support(support,support.back().time_seconds),"consecutive recovery evidence was rejected");
    require(!recent_frequency_support(support,support.back().time_seconds+.4),"invalid current dwell recycled stale recovery history");
    support.back().time_seconds+=.4;
    require(recent_frequency_support(support,support.back().time_seconds),"one omitted recovery dwell was rejected");
    support.back().time_seconds+=1.2;
    require(!recent_frequency_support(support,support.back().time_seconds),"stale pre-gap measurements qualified a recovery candidate");
    hp::Config c;c.seed=901;c.path_loss_db=180;ht::Source source(c);
    h::HelperMeasurement acquisition({fs,550,2400,2300,2500,300,.05*std::numbers::pi/180});
    for(unsigned i=0;i<2;++i){auto d=source.generate(i);const auto m=acquisition.observe(std::span<const S>(d.samples.data()+settle,aperture),d.shift_hz,d.first_seconds+settle/fs);
        require(m.valid,"short 40W pilot acquisition failed");}
    require(!acquisition.snapshot().valid,"short probe manufactured 36-second narrowing");
    std::cout<<"helper_tracking_short seconds=.8 helper_w=40 loss_db=180 phase_acquisition=available clock_fit=unavailable\n";
}
}

int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--probe"){short_probe();return 0;}
    if(argc>1&&std::string(argv[1])=="--robustness"){robustness();return 0;}
    hp::Config config;ht::Impairments impairments;
    if(argc>1&&std::string(argv[1])=="--case"){
        if(argc<4||argc>8)throw std::invalid_argument("--case LOSS_DB SEED [hf|microwave] [IMPAIRMENT] [tone|apsk] [PROBE_W]");
        config.path_loss_db=std::stod(argv[2]);config.seed=std::stoull(argv[3]);
        if(argc>4&&(std::string(argv[4])=="microwave"||std::string(argv[4])=="microwave-eu")){config.shift_center_hz=5.8e9;config.relative_shift_ppm=.00008;config.phase_noise_degrees_per_sqrt_second=.005;config.microwave_eu20=std::string(argv[4])=="microwave-eu";}
        if(argc>5){const std::string mode=argv[5];
            if(mode=="gap"){impairments.missing_first=91;impairments.missing_count=2;}
            else if(mode=="long-gap"){impairments.missing_first=70;impairments.missing_count=24;}
            else if(mode=="step")impairments.frequency_step_hz=1.2;
            else if(mode=="curvature"){impairments.frequency_change_seconds=18;impairments.frequency_curvature_hz_per_second2=.003;}
            else if(mode=="differential")impairments.changing_low_phase_radians=.02;
            else if(mode=="differential-large"){impairments.differential_first=91;impairments.changing_low_phase_radians=.8;}
            else if(mode=="retune")impairments.late_retune_dwell=95;
            else if(mode=="retune-mid"){impairments.late_retune_dwell=95;impairments.late_retune_seconds=.12;}
            else if(mode=="noise")impairments.noise_only=true;
            else if(mode!="clean")throw std::invalid_argument("unknown impairment");}
        if(argc>6){const std::string probe=argv[6];if(probe=="apsk")config.probe=hp::Probe::apsk64;else if(probe!="tone")throw std::invalid_argument("unknown probe");}
        if(argc>7)config.probe_w=std::stod(argv[7]);
        scenario(config,impairments,false,argc>5?argv[5]:"clean");return 0;
    }
    if(argc>1&&std::string(argv[1])!="--suite")throw std::invalid_argument("expected --probe, --suite, --robustness or --case");
    short_probe();config.path_loss_db=160;scenario(config,{},true);
    config.path_loss_db=180;scenario(config,{},true);return 0;
}catch(const std::exception& e){std::cerr<<"Helper tracking failure: "<<e.what()<<'\n';return 1;}}
