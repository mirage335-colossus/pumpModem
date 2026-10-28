#pragma once
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_phase_precision.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace datapump::helper {
// Noise-only references. Independent phase apertures contribute sum(T_i^3),
// not the cube of wall-clock time. These are not operational confidence bounds.
inline double segmented_tone_three_sigma(double pilot_cn0_hz,double sum_aperture_cubes) noexcept {
    return pilot_cn0_hz>0 && sum_aperture_cubes>0?
        3*std::sqrt(3/(2*std::numbers::pi*std::numbers::pi*pilot_cn0_hz*sum_aperture_cubes)):
        std::numeric_limits<double>::infinity();
}
namespace segmented_detail {
class Pilot {
    using C=std::complex<double>;
    using F=std::complex<float>;
    static constexpr double tau=2*std::numbers::pi;
    static constexpr std::size_t maximum_fft=32768,points=10,records=768,phase_records=8192;
    struct Point {double phase=0,variance=0;std::uint64_t sample=0;};
    struct Row {C value{},lower{},upper{};};
    struct FrequencyFit {
        bool valid=false;double hz=0,variance=0,aperture=0;
        bool fallback=false;unsigned gap=0;double aperture_cube=0;
        bool dated=false;std::uint64_t sample=0;double sample_fraction=0;
    };
    static constexpr std::size_t profile_points=251;
    struct Record {double hz=0,variance=0,aperture=0;std::uint64_t first=0,last=0,sample=0;double sample_fraction=-1;};
    using Matrix=std::array<std::array<double,3>,3>;
    struct PhaseObservation {std::uint64_t end=0;double phase=0,variance=0;};
    struct PhaseState {std::array<double,3> x{};Matrix p{};std::uint64_t sample=0;bool valid=false;};
    // Past frequency/rate marginals and their cross covariance with the current
    // phase/frequency/rate state. This bounded joint Kalman update needs no
    // historical PCM, inversion or additional phase hypothesis.
    struct LagNode {
        std::array<double,2> mean{};
        std::array<std::array<double,2>,2> p{};
        std::array<std::array<double,3>,2> b{};
        std::uint64_t sample=0,first=0,end=0,evidence_first=0;
        double inflate=1,q=0;
        bool active=false,qualified=false;
    };
    struct FullPoint {double phase=0,variance=0,fraction=0;std::uint64_t sample=0;double window_sum=0;};
    std::array<float,maximum_fft> discovery_pcm_{};
    std::array<FullPoint,32> full_points_{};
    bool full_mode_=false,full_good_=true,full_closed_=false;
    std::uint64_t full_end_=0,full_first_=0;
    std::size_t full_count_=0,full_block_length_=0;
    double full_ws_=0,full_w2_=0;
    ToneFrequencyObservation frequency_kernel;
    double full_independent_variance_hz2=0,full_residual_variance_hz2=0;
    ToneTrackerConfig c_;
    std::array<F,maximum_fft> raw_{};
    std::array<double,128> background_{};
    std::array<Point,points> p_{};
    std::array<Row,points> rows_{};
    std::array<std::array<double,profile_points>,points> profile_bic_{};
    std::array<Record,records> r_{};
    std::array<PhaseObservation,phase_records> phase_rows_{};
    PhaseState continuous_;
    std::array<LagNode,64> lag_nodes_{};
    std::size_t lag_head_=0,lag_count_=0;
    std::size_t phase_head_=0,phase_count_=0,phase_since_refit_=0,phase_updates_=0;
    std::uint64_t phase_origin_=0,phase_support_first_=0,phase_frequency_support_first_=0;
    double phase_reference_hz_=0,phase_chisq_=0,phase_lag_=0,phase_lag2_=0,phase_process_i1_=0,phase_process_i2_=0;
    phase_detail::FrequencyProcessProjection frequency_process_projection_;
    double phase_change_positive_=0,phase_change_negative_=0;
    std::uint64_t next_=0,last_good_=0,context_first_=0,settled_first_=0;
    std::size_t fft_n_=0,ordinary_fft_n_=0,local_fft_n_=0,raw_used_=0,block_n_=0,block_used_=0,pn_=0,rn_=0;
    double low_=0,high_=0,center_=0,scale_=1,noise_=0,power_=0,ws_=0,w2_=0;
    double previous_phase_=0,previous_variance_=0,unwrapped_=0;
    C nco_{1,0},step_{1,0},side_{1,0},side_step_{1,0},sum_{},lower_{},upper_{};
    bool acquired_=false,previous_=false,straight_context_=true,local_aperture_=false;
public:
    ToneTrackerSnapshot result;
    ToneFrequencyInterval frequency_interval(std::uint64_t first,std::uint64_t end)const noexcept {
        ToneFrequencyInterval out;
        if(first>=end || end>next_)return out;
        const auto middle=first+(end-first-1)/2;
        const auto fraction=.5*((end-first-1)%2);
        const auto a=result.frequency_at(first),b=result.frequency_at(end-1),m=result.frequency_at(middle,fraction);
        if(a.valid && b.valid && m.valid) {
            out={true,false,false,m.frequency_hz,std::nextafter(std::max(
                a.uncertainty_hz+std::abs(a.frequency_hz-m.frequency_hz),
                b.uncertainty_hz+std::abs(b.frequency_hz-m.frequency_hz)),INFINITY),
                first,end,result.sample,result.first_sample,result.sample+1,next_};
        }
        // Independent frequency evidence has its own actual support. A phase
        // ambiguity or an older fine publication cannot revoke this aperture.
        // Walk the bounded numeric records backwards: the existing400ms PCM
        // stage can need a record preceding the latest200ms context.
        for(std::size_t i=rn_;i>0;--i) {
            const auto& record=r_[i-1];
            if(record.last<first)break;
            // Retained common-coordinate records may still inform the
            // explicitly mapped oscillator model. Their old raw apertures do
            // not become observed certificates in the new retune geometry.
            if(record.first<settled_first_)break;
            if(record.first>first || record.last==UINT64_MAX || end>record.last+1 ||
               record.first>record.last || record.last>=next_ ||
               !std::isfinite(record.hz) || !std::isfinite(record.variance) || record.variance<0)continue;
            const auto frequency=c_.expected_hz+scale_*record.hz;
            const auto duration=static_cast<double>(record.last-record.first+1)/c_.sample_rate;
            // The effective mean of a gap fit can be asymmetric. Enclose its
            // entire observed support with the existing local bounded-slew
            // contract, separately from the continuous filter's process Q.
            // This is frequency-only evidence, never a future or phase lease.
            const auto width=std::nextafter(3*std::abs(scale_)*std::sqrt(record.variance)+
                c_.maximum_tracking_slew_hz_per_second*duration,INFINITY);
            if(std::isfinite(frequency) && frequency>=200 && frequency<=c_.sample_rate/2.-200 &&
               std::isfinite(width) && (!out.valid || width<out.uncertainty_hz))
                out={true,false,true,frequency,width,first,end,record.first,record.first,record.last+1,next_};
            break; // Disjoint apertures: never join fits across a boundary.
        }
        if(!lag_count_ || first<lag_nodes_[lag_head_].sample ||
           end-1>lag_nodes_[(lag_head_+lag_count_-1)%lag_nodes_.size()].sample)return out;
        const LagNode* selected=nullptr;long double distance=std::numeric_limits<long double>::infinity();
        for(std::size_t i=0;i<lag_count_;++i) {
            const auto& node=lag_nodes_[(lag_head_+i)%lag_nodes_.size()];
            if(!node.qualified || first<node.first || end>node.end || node.end>next_)continue;
            const auto d=std::abs(static_cast<long double>(middle)-node.sample+fraction);
            if(d<distance){selected=&node;distance=d;}
        }
        if(!selected)return out;
        const auto& node=*selected;
        const auto time=[&](std::uint64_t at,double f=0.) {
            return static_cast<double>((static_cast<long double>(at)-node.sample+f)/c_.sample_rate);
        };
        const auto dt=time(middle,fraction),frequency=std::fma(node.mean[1],dt,node.mean[0]);
        double width=0;
        for(const auto at:std::array{first,end-1}) {
            const auto t=time(at);
            const auto variance=node.inflate*std::fma(t,std::fma(t,node.p[1][1],2*node.p[0][1]),node.p[0][0]);
            // The nearby point's process error and smoothed marginal need not
            // be independent. Their RMS sum is conservative in either time
            // direction, including gaps between accepted phase observations.
            const auto process=node.q*std::abs(t)*t*t/3;
            if(!(variance>=0) || !(process>=0) || !std::isfinite(variance) || !std::isfinite(process))return out;
            width=std::max(width,3*(std::sqrt(variance)+std::sqrt(process))+
                std::abs(std::fma(node.mean[1],t,node.mean[0])-frequency));
        }
        width=std::nextafter(width,INFINITY);
        if(std::isfinite(frequency) && std::isfinite(width) && (!out.valid || width<out.uncertainty_hz))
            out={true,true,false,frequency,width,first,end,node.sample,node.evidence_first,node.end,next_};
        return out;
    }
    explicit Pilot(ToneTrackerConfig c,std::uint64_t first):c_(c) {
        fft_n_=1024;while(fft_n_<c.sample_rate*.16 && fft_n_<maximum_fft)fft_n_*=2;
        ordinary_fft_n_=fft_n_;
        local_fft_n_=1024;while(local_fft_n_<c.sample_rate*.08 && local_fft_n_<maximum_fft)local_fft_n_*=2;
        block_n_=std::max<std::size_t>(16,std::lround(c.sample_rate*.02));
        for(std::size_t i=0;i<block_n_;++i){const auto w=.5-.5*std::cos(tau*i/(block_n_-1));ws_+=w;w2_+=w*w;}
        double cumulative=0;
        for(std::size_t i=0;i<block_n_;++i) {
            const auto w=.5-.5*std::cos(tau*i/(block_n_-1)),fraction=cumulative/ws_;
            const auto lag=static_cast<double>(block_n_-1-i)/c.sample_rate;
            phase_lag_+=w*lag/ws_;phase_lag2_+=w*lag*lag/ws_;
            phase_process_i1_+=fraction/c.sample_rate;phase_process_i2_+=fraction*fraction/c.sample_rate;
            cumulative+=w;
        }
        frequency_process_projection_=phase_detail::hann_frequency_process(block_n_,c.sample_rate);
        side_step_=std::polar(1.,-tau*200/c.sample_rate);geometry();reset(first);
    }
    void geometry() noexcept {low_=std::max(20.,c_.expected_hz-c_.search_half_width_hz);high_=std::min(c_.sample_rate/2.-20.,c_.expected_hz+c_.search_half_width_hz);}
    void reset(std::uint64_t first) noexcept {
        next_=last_good_=settled_first_=first;raw_used_=block_used_=pn_=rn_=0;acquired_=previous_=false;straight_context_=true;
        local_aperture_=false;fft_n_=ordinary_fft_n_;
        scale_=1;noise_=power_=0;nco_=side_={1,0};sum_=lower_=upper_={};
        result={};result.sample_rate=c_.sample_rate;result.captured_end_sample=first;
        full_mode_=false;full_good_=true;full_closed_=false;full_count_=full_block_length_=0;frequency_kernel={};
        clear_continuous(false);
        lag_head_=lag_count_=0;
        result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=scale_;
    }
    void begin(std::uint64_t first,double expected,double width,bool preserve,double scale,std::uint64_t settled_first=0,
               bool qualify_local_aperture=false,std::uint64_t settled_end=0) {
        if(first!=next_ || !std::isfinite(expected) || !std::isfinite(width) || width<0 ||
           !std::isfinite(scale) || scale==0 ||
           (settled_first && settled_first<first) ||
           (settled_end && (settled_end<=(settled_first?settled_first:first) ||
               settled_end-(settled_first?settled_first:first)>31*block_n_)) ||
           (qualify_local_aperture && (preserve || scale!=1)) ||
           std::max(20.,expected-width)>std::min(c_.sample_rate/2.-20.,expected+width))
            throw Error("invalid dated pilot segment geometry");
        // A caller retaining frequency records asserts the common residual model.
        // No phase, sample frontier or measured support is carried across retune.
        const auto keep_frequency=preserve && result.frequency_knowledge_valid;
        const auto keep_qualified=keep_frequency && result.qualified;
        const auto keep_refined=keep_frequency && result.frequency_precision_refined;
        const auto old_common=(result.frequency_hz-c_.expected_hz)/scale_;
        const auto old_uncertainty=result.uncertainty_hz/std::abs(scale_);
        const auto old_rate=result.frequency_rate_hz_per_second/scale_;
        c_.expected_hz=expected;c_.search_half_width_hz=width;scale_=scale;geometry();
        local_aperture_=qualify_local_aperture;fft_n_=local_aperture_?local_fft_n_:ordinary_fft_n_;
        clear_continuous(); // Unknown retune phase is never a coherent bridge.
        // The scalar query API has the new RF mapping. The separate Live joint
        // RF/audio model owns frequency transport across actual retunes.
        lag_head_=lag_count_=0;
        settled_first_=settled_first?settled_first:first;
        if(!preserve){rn_=0;acquired_=false;raw_used_=0;noise_=power_=0;reject_frequency_knowledge();}
        else if(acquired_ || keep_frequency){
            result.frequency_hz=expected+old_common*scale_;
            result.uncertainty_hz=old_uncertainty*std::abs(scale_);
            result.frequency_rate_hz_per_second=old_rate*scale_;
            const auto ratio=scale_/result.frequency_model_residual_scale;
            result.frequency_variance_hz2*=ratio*ratio;
            result.frequency_rate_covariance_hz2_per_second*=ratio*ratio;
            result.frequency_rate_variance_hz2_per_second2*=ratio*ratio;
            result.frequency_acceleration_psd_hz2_per_second3*=ratio*ratio;
            if(acquired_)set_center(std::clamp(result.frequency_hz,low_,high_));
        }
        if(!acquired_)raw_used_=0; // An FFT never crosses a known independent hop phase.
        block_used_=pn_=0;previous_=false;straight_context_=true;sum_=lower_=upper_={};nco_=side_={1,0};
        result.qualified=keep_qualified;result.phase_qualified=result.projection_phase_valid=false;
        invalidate_phase_fit();result.present=false;result.frequency_precision_refined=keep_refined;
        result.frequency_local_aperture=local_aperture_;
        result.frequency_model_expected_hz=expected;result.frequency_model_residual_scale=scale_;
        if(!preserve || !acquired_)last_good_=first;
        // Known aperture dates do not assert any continuity through a retune.
        // Compatible scalar frequency records survive only the caller's
        // explicit preserve/scale contract above.
        full_mode_=settled_end!=0;frequency_kernel={};result.frequency_observation={};
        full_independent_variance_hz2=full_residual_variance_hz2=0;
        full_end_=settled_end;full_first_=settled_first_;full_count_=full_block_length_=0;
        full_good_=true;full_closed_=false;
    }
    void push(std::span<const float> pcm,std::uint64_t first) {
        if(first!=next_ || pcm.size()>UINT64_MAX-first)throw Error("pilot tracker requires contiguous raw samples");
        for(const auto x:pcm)if(!std::isfinite(x))throw Error("pilot tracker received nonfinite PCM");
        for(const auto x:pcm){
            ++next_;
            if(next_-1<settled_first_)continue;
            if(full_mode_ && next_-1>=full_end_)continue;
            if(!acquired_){
                if(full_mode_)discovery_pcm_[raw_used_]=x;
                raw_[raw_used_++]={x,0};
                if(raw_used_==fft_n_){
                    discover();raw_used_=0;
                    if(full_mode_ && acquired_) {
                        const auto captured=next_,first=captured-fft_n_;
                        if(first!=full_first_)full_good_=false;
                        next_=first;nco_=side_={1,0};
                        for(std::size_t i=0;i<fft_n_;++i){++next_;project_full(discovery_pcm_[i]);}
                        next_=captured;
                    }
                }
                continue;
            }
            if(full_mode_){project_full(x);continue;}
            const auto w=.5-.5*std::cos(tau*block_used_/(block_n_-1));
            const auto z=static_cast<double>(x)*nco_*w;sum_+=z;lower_+=z*side_;upper_+=z*std::conj(side_);
            nco_*=step_;side_*=side_step_;
            if(++block_used_==block_n_){nco_/=std::max(std::abs(nco_),1e-30);finish_block();}
        }
        result.captured_end_sample=next_;
        const auto age=static_cast<double>(next_-last_good_)/c_.sample_rate;
        result.observation_age_seconds=result.sample<next_?static_cast<double>(next_-1-result.sample)/c_.sample_rate:0;
        if(age>.4){result.qualified=result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();result.frequency_precision_refined=false;result.frequency_observation.valid=false;}
        if(age>.55)result.present=false;
        if(result.phase_sample<next_ && next_-1-result.phase_sample>static_cast<std::uint64_t>(.04*c_.sample_rate))result.phase_qualified=result.projection_phase_valid=false;
        if(age>1. && acquired_){acquired_=false;raw_used_=pn_=0;previous_=false;result.qualified=result.present=result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();}
    }
private:
    void project_full(float x) noexcept {
        if(full_closed_ || next_>full_end_)return;
        if(!block_used_) {
            const auto remaining=full_end_-(next_-1);
            // Merge the last partial context into a final20..40ms Hann row.
            // Every usable sample is included; there is no wide4ms row whose
            // noise projections would overlap the pilot main lobe.
            full_block_length_=remaining<2*block_n_?static_cast<std::size_t>(remaining):block_n_;
            full_ws_=full_w2_=0;
        }
        const auto w=full_block_length_>1?.5-.5*std::cos(tau*block_used_/(full_block_length_-1)):1.;
        full_ws_+=w;full_w2_+=w*w;
        const auto z=static_cast<double>(x)*nco_*w;sum_+=z;lower_+=z*side_;upper_+=z*std::conj(side_);
        nco_*=step_;side_*=side_step_;
        if(++block_used_!=full_block_length_)return;
        nco_/=std::max(std::abs(nco_),1e-30);
        const auto bandwidth=c_.sample_rate*full_w2_/(full_ws_*full_ws_);
        const auto measured_noise=(std::norm(lower_)+std::norm(upper_))/(c_.sample_rate*full_w2_);
        if(std::isfinite(measured_noise) && measured_noise>0)noise_=.9*noise_+.1*measured_noise;
        noise_=std::max(noise_,1e-30);
        const auto measured_power=std::max(0.,2*std::norm(sum_)/(full_ws_*full_ws_)-noise_*bandwidth);
        const auto good=measured_power>noise_*bandwidth && (!power_ || measured_power>.12*power_);
        power_=.95*power_+.05*measured_power;
        result.carrier_to_noise_db_hz=10*std::log10(std::max(measured_power/noise_,1e-20));
        const auto phase=std::arg(sum_),variance=good?noise_*bandwidth/(2*measured_power):0.;
        const auto increment=std::remainder(phase-previous_phase_,tau);
        const auto jump=previous_ && std::abs(increment)>std::min(2.5,std::max(.65,5*std::sqrt(variance+previous_variance_)));
        full_good_ &= good && !jump;
        if(!previous_)unwrapped_=phase;else unwrapped_+=increment;
        previous_=true;previous_phase_=phase;previous_variance_=variance;
        const auto date=next_-full_block_length_+(full_block_length_-1)/2;
        const auto fraction=.5*((full_block_length_-1)%2);
        if(full_count_<full_points_.size())full_points_[full_count_++]={unwrapped_,variance,fraction,date,full_ws_};
        else full_good_=false;
        sum_=lower_=upper_={};side_={1,0};block_used_=0;
        if(next_==full_end_) {
            full_closed_=true;context_first_=full_first_;
            if(full_good_ && full_count_>=4) {
                const auto fit=full_fit();
                if(fit.valid) {
                    accept_frequency(fit);
                    // This current raw observation owns its own dates/Hz/R.
                    // Pooled qualification and the independent phase gate
                    // cannot lend authority or stale geometry to this record.
                    frequency_kernel.valid=frequency_kernel.valid &&
                        frequency_kernel.first_sample==full_first_ && frequency_kernel.end_sample==next_ &&
                        frequency_kernel.state_endpoint_sample+1==next_ &&
                        result.phase_fit_first_sample==full_first_ && result.phase_fit_end_sample==next_ &&
                        std::isfinite(full_independent_variance_hz2) && full_independent_variance_hz2>0 &&
                        std::isfinite(full_residual_variance_hz2) && full_residual_variance_hz2>=0;
                    if(frequency_kernel.valid) {
                        frequency_kernel.frequency_hz=fit.hz;
                        frequency_kernel.independent_variance_hz2=full_independent_variance_hz2;
                        frequency_kernel.residual_variance_hz2=full_residual_variance_hz2;
                        result.frequency_observation=frequency_kernel;
                    }
                }
            } else {if(!result.frequency_knowledge_valid)result.qualified=false;invalidate_phase_fit();}
        }
    }
    FrequencyFit full_fit() noexcept {
        invalidate_phase_fit();const auto count=static_cast<double>(full_count_);
        const auto mid=full_points_[full_count_/2].sample;
        const auto sample_delta=[](std::uint64_t a,std::uint64_t b) {
            return a>=b?static_cast<double>(a-b):-static_cast<double>(b-a);
        };
        const auto time=[&](const FullPoint& p){return (sample_delta(p.sample,mid)+p.fraction)/c_.sample_rate;};
        double mt=0,mp=0;for(std::size_t i=0;i<full_count_;++i){mt+=time(full_points_[i]);mp+=full_points_[i].phase;}
        mt/=count;mp/=count;double tt=0,tp=0;
        for(std::size_t i=0;i<full_count_;++i){const auto& p=full_points_[i];const auto t=time(p)-mt;tt+=t*t;tp+=t*(p.phase-mp);}
        // Actual current-dwell linearized phase-observation kernel, not an
        // uninterrupted-tone approximation or a guessed midpoint measurement.
        frequency_kernel={};frequency_kernel.first_sample=full_first_;frequency_kernel.end_sample=full_end_;frequency_kernel.state_endpoint_sample=full_end_-1;
        double sum_u=0;for(std::size_t i=0;i<full_count_;++i)sum_u+=time(full_points_[i])-mt;
        const auto h=1./c_.sample_rate,h2=h*h,h3=h2*h,h4=h3*h,h5=h4*h;
        double cumulative_q=0,k=0,k_prime=0;std::uint64_t cursor=full_first_;
        for(std::size_t i=0;i<full_count_;++i) {
            const auto remaining=full_end_-cursor;
            const auto length=remaining<2*block_n_?static_cast<std::size_t>(remaining):block_n_;
            const auto& point=full_points_[i];
            const auto B=(time(point)-mt-sum_u/count)/tt;
            for(std::size_t j=0;j<length;++j,++cursor) {
                const auto hann=length>1?.5-.5*std::cos(tau*j/(length-1)):1.;
                cumulative_q+=B*hann/point.window_sum;
                if(cursor==frequency_kernel.state_endpoint_sample)continue;
                const auto w=-cumulative_q,r=static_cast<double>(frequency_kernel.state_endpoint_sample-cursor)/c_.sample_rate;
                // Between adjacent ADC dates w is constant and k quadratic.
                const auto ik=k*h+k_prime*h2/2+w*h3/6;
                frequency_kernel.mass+=w*h;
                frequency_kernel.mu_seconds+=w*(r*h-h2/2);
                frequency_kernel.integral_k_seconds2+=ik;
                frequency_kernel.integral_b_minus_s_k_seconds3+=r*ik-(k*h2/2+k_prime*h3/3+w*h4/8);
                frequency_kernel.integral_k_squared_seconds3+=k*k*h+k*k_prime*h2+
                    (k_prime*k_prime+k*w)*h3/3+k_prime*w*h4/4+w*w*h5/20;
                k+=k_prime*h+w*h2/2;k_prime+=w*h;
            }
        }
        frequency_kernel.k_at_end_seconds=k;frequency_kernel.k_prime_at_end=k_prime;
        frequency_kernel.valid=cursor==full_end_ && std::isfinite(k) &&
            std::abs(frequency_kernel.mass-1)<1e-10 && std::abs(k_prime-1)<1e-10 &&
            std::abs(k-frequency_kernel.mu_seconds)<1e-10 && frequency_kernel.integral_k_squared_seconds3>=0;
        const auto slope=tp/tt;double residual=0,thermal=0,expected=0,maximum_residual=0,phase_thermal=0;
        const auto& last=full_points_[full_count_-1];const auto latest_t=time(last)-mt;
        for(std::size_t i=0;i<full_count_;++i){const auto& p=full_points_[i];const auto t=time(p)-mt;
            const auto e=p.phase-mp-slope*t;residual+=e*e;maximum_residual=std::max(maximum_residual,std::abs(e));
            const auto phase_weight=1/count+latest_t*t/tt;phase_thermal+=phase_weight*phase_weight*p.variance;
            thermal+=t*t*p.variance/(tt*tt);expected+=p.variance*(1-1/count-t*t/tt);}
        if(residual>std::max(.35,9*expected))return {};
        const auto aperture=static_cast<double>(full_end_-full_first_)/c_.sample_rate;
        const auto process=2*c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second/aperture;
        const auto excess=std::max(0.,(residual-expected)/(count-2));
        const auto f=center_+slope/tau,var=(thermal+excess/tt+process)/(tau*tau);
        if(!std::isfinite(var) || f<low_ || f>high_ || std::abs(slope/tau)>20)return {};
        full_independent_variance_hz2=(thermal+process)/(tau*tau);
        full_residual_variance_hz2=excess/(tt*tau*tau);
        const auto mixer_phase=std::arg(nco_)+tau*center_*(static_cast<double>(full_end_-last.sample)-last.fraction)/c_.sample_rate;
        const auto phase_process=2*c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second*aperture;
        result.phase_fit_cycles=std::remainder((mp+slope*latest_t-mixer_phase)/tau,1.);
        result.phase_fit_uncertainty_cycles=(3*std::sqrt(phase_thermal+phase_process)+maximum_residual)/tau;
        result.phase_fit_frequency_hz=f;result.phase_fit_frequency_uncertainty_hz=3*std::sqrt(var);
        result.phase_fit_sample=last.sample;result.phase_fit_sample_fraction=last.fraction;
        result.phase_fit_first_sample=full_first_;result.phase_fit_end_sample=full_end_;
        double support_uncertainty=0;
        for(const auto endpoint:std::array{full_first_,full_end_-1}) {
            const auto distance=(sample_delta(endpoint,last.sample)-last.fraction)/c_.sample_rate;
            const auto at=latest_t+distance;double prediction_variance=0;
            for(std::size_t i=0;i<full_count_;++i){const auto& p=full_points_[i];const auto t=time(p)-mt;
                const auto weight=1/count+at*t/tt;prediction_variance+=weight*weight*(p.variance+excess);}
            const auto bound=(3*std::sqrt(prediction_variance+phase_process)+maximum_residual+3*std::sqrt(process)*std::abs(distance))/tau;
            support_uncertainty=std::max(support_uncertainty,bound);
        }
        result.phase_fit_qualified=std::isfinite(result.phase_fit_uncertainty_cycles) && std::isfinite(support_uncertainty);
        if(result.phase_fit_qualified)result.phase_fit_support_uncertainty_cycles=support_uncertainty;
        FrequencyFit fit{true,f,var,aperture,false,0,aperture*aperture*aperture};
        // Subtract a small integer/fractional lag, never a large absolute
        // ADC date in double precision (including dates beyond2^53).
        const auto lag_samples=frequency_kernel.mu_seconds*c_.sample_rate;
        if(!frequency_kernel.valid || !(lag_samples>=0) ||
           lag_samples>static_cast<double>(full_end_-full_first_-1))return {};
        const auto whole=static_cast<std::uint64_t>(std::floor(lag_samples));
        const auto fraction=lag_samples-whole;
        fit.dated=true;fit.sample=frequency_kernel.state_endpoint_sample-whole;
        if(fraction>0){--fit.sample;fit.sample_fraction=1-fraction;}
        frequency_kernel.mean_sample=fit.sample;frequency_kernel.mean_sample_fraction=fit.sample_fraction;
        return fit;
    }
    void clear_continuous(bool count=true) noexcept {
        if(count && continuous_.valid)++result.continuous_phase_resets;
        // Phase ambiguity rejects phase coherence, not its dated frequency
        // marginal. Only explicit model contradiction/reset rejects that.
        result.frequency_continuous_phase=false;
        continuous_={};phase_head_=phase_count_=phase_since_refit_=phase_updates_=0;phase_chisq_=0;
        phase_change_positive_=phase_change_negative_=0;
        // An independent new phase has no derived cross covariance with these
        // old states. Freeze already qualified historical frequency marginals
        // inside their actual support instead of discarding their precision.
        for(std::size_t i=0;i<lag_count_;++i)lag_nodes_[(lag_head_+i)%lag_nodes_.size()].active=false;
    }
    void reject_frequency_knowledge() noexcept {
        result.frequency_knowledge_valid=result.frequency_covariance_valid=false;
        result.qualified=result.frequency_precision_refined=result.frequency_continuous_phase=false;
        lag_head_=lag_count_=0;
    }
    double phase_process_density()const noexcept {
        const auto sigma=c_.phase_noise_radians_per_sqrt_second/tau;return sigma*sigma;
    }
    double frequency_process_density()const noexcept {
        return c_.frequency_acceleration_psd_hz2_per_second3*scale_*scale_;
    }
    double phase_measurement_variance(const PhaseObservation& row)const noexcept {
        return row.variance+phase_process_density()*phase_process_i2_+
            frequency_process_density()*frequency_process_projection_.variance;
    }
    std::array<double,3> phase_measurement_cross()const noexcept {
        auto cross=frequency_process_projection_.cross;
        for(auto& value:cross)value*=frequency_process_density();
        cross[0]-=phase_process_density()*phase_process_i1_;return cross;
    }
    PhaseState initial_phase_state(const PhaseObservation& row,double frequency_variance)const noexcept {
        PhaseState state;state.sample=row.end;state.valid=true;state.x[0]=row.phase;
        const auto rate_variance=c_.maximum_tracking_slew_hz_per_second*c_.maximum_tracking_slew_hz_per_second;
        state.p[1][1]=frequency_variance;state.p[2][2]=rate_variance;
        if(frequency_process_density()>0) {
            // This fresh prior starts before the aperture's process noise.
            // In particular, S=0 does not mean the endpoint rate has zero
            // variance when positive acceleration acts during the aperture.
            const auto t=static_cast<double>(block_n_-1)/c_.sample_rate;
            const auto process=phase_detail::frequency_process_covariance(frequency_process_density(),t);
            state.p[1][1]+=t*t*rate_variance+process[1][1];
            state.p[1][2]=state.p[2][1]=t*rate_variance+process[1][2];
            state.p[2][2]+=process[2][2];
        }
        const auto vf=state.p[1][1],vfr=state.p[1][2],vr=state.p[2][2];
        const auto a=phase_lag_,b=-.5*phase_lag2_;
        const auto cross=phase_measurement_cross();
        state.p[0][1]=state.p[1][0]=a*vf+b*vfr-cross[1];
        state.p[0][2]=state.p[2][0]=a*vfr+b*vr-cross[2];
        state.p[0][0]=phase_measurement_variance(row)+
            a*a*vf+2*a*b*vfr+b*b*vr-2*(a*cross[1]+b*cross[2]);
        return state;
    }
    bool seed_independent_phase_frequency(PhaseState& state,const PhaseObservation& row) noexcept {
        if(!result.frequency_knowledge_valid || !result.frequency_covariance_valid || row.end<result.sample)return false;
        if(frequency_process_density()>0&&result.sample>row.end+1-block_n_)return false;
        const auto dt=static_cast<double>(row.end-result.sample)/c_.sample_rate;
        const auto rate=result.frequency_rate_hz_per_second;
        const auto old_vrr=result.frequency_rate_variance_hz2_per_second2>0?
            result.frequency_rate_variance_hz2_per_second2:
            c_.maximum_tracking_slew_hz_per_second*c_.maximum_tracking_slew_hz_per_second;
        const auto process=phase_detail::frequency_process_covariance(result.frequency_acceleration_psd_hz2_per_second3,dt);
        const auto vrr=old_vrr+process[2][2];
        const auto cross=result.frequency_rate_covariance_hz2_per_second+dt*old_vrr+process[1][2];
        const auto variance=std::fma(dt,std::fma(dt,old_vrr,2*result.frequency_rate_covariance_hz2_per_second),
            result.frequency_variance_hz2)+process[1][1];
        const auto frequency=std::fma(rate,dt,result.frequency_hz);
        // Propagate the dated frequency marginal and its declared process;
        // no guessed phase is carried. A broad
        // stale marginal need not displace the new causal FFT frequency prior.
        if(!(variance>=0) || !std::isfinite(variance) || !std::isfinite(cross) || !std::isfinite(vrr) ||
           variance>state.p[1][1] || frequency<low_ || frequency>high_)return false;
        state.x[1]=frequency-phase_reference_hz_;state.x[2]=rate;
        state.p[1][1]=variance;state.p[1][2]=state.p[2][1]=cross;state.p[2][2]=vrr;
        const auto a=phase_lag_,b=-.5*phase_lag2_;
        const auto measurement_cross=phase_measurement_cross();
        state.x[0]=row.phase+a*state.x[1]+b*state.x[2];
        state.p[0][1]=state.p[1][0]=a*variance+b*cross-measurement_cross[1];
        state.p[0][2]=state.p[2][0]=a*cross+b*vrr-measurement_cross[2];
        state.p[0][0]=phase_measurement_variance(row)+
            a*a*variance+2*a*b*cross+b*b*vrr-2*(a*measurement_cross[1]+b*measurement_cross[2]);
        phase_frequency_support_first_=result.first_sample;
        return true; // This first independent phase row leaves P(f,rate) unchanged.
    }
    static bool valid_lag_node(const LagNode& node) noexcept {
        if(!std::isfinite(node.mean[0]) || !std::isfinite(node.mean[1]))return false;
        double scale=0;
        for(const auto& row:node.p)for(const auto value:row) {
            if(!std::isfinite(value))return false;scale=std::max(scale,std::abs(value));
        }
        for(const auto& row:node.b)for(const auto value:row)if(!std::isfinite(value))return false;
        if(node.p[0][0]<0 || node.p[1][1]<0)return false;
        if(!scale)return true;
        const auto product=(node.p[0][0]/scale)*(node.p[1][1]/scale);
        const auto square=(node.p[0][1]/scale)*(node.p[1][0]/scale);
        const auto epsilon=64*std::numeric_limits<double>::epsilon();
        return std::abs(node.p[0][1]-node.p[1][0])/scale<=epsilon &&
            product-square>=-epsilon*std::max({std::abs(product),std::abs(square),1e-300});
    }
    void append_lag_node(const PhaseState& state) noexcept {
        if(lag_count_==lag_nodes_.size()){lag_head_=(lag_head_+1)%lag_nodes_.size();--lag_count_;}
        auto& node=lag_nodes_[(lag_head_+lag_count_++)%lag_nodes_.size()];node={};
        node.sample=state.sample;node.active=true;
        for(std::size_t i=0;i<2;++i) {
            node.mean[i]=state.x[i+1]+(i==0?phase_reference_hz_:0);
            for(std::size_t j=0;j<2;++j)node.p[i][j]=state.p[i+1][j+1];
            for(std::size_t j=0;j<3;++j)node.b[i][j]=state.p[i+1][j];
        }
        node.active=valid_lag_node(node);
    }
    void update_lag_nodes(const Matrix& transition,const std::array<double,3>& h,
            const std::array<double,3>& e,double variance,double innovation,
            const PhaseState& current,std::uint64_t measurement_first) noexcept {
        for(std::size_t n=0;n<lag_count_;++n) {
            auto& node=lag_nodes_[(lag_head_+n)%lag_nodes_.size()];if(!node.active)continue;
            node.qualified=false;
            // New Hann process/noise is independent of these older endpoints.
            // Its correlation with the CURRENT endpoint belongs in e, not d.
            if(node.sample>=measurement_first){node.active=false;continue;}
            std::array<std::array<double,3>,2> prior{};std::array<double,2> d{};
            for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<3;++j) {
                for(std::size_t k=0;k<3;++k)prior[i][j]+=node.b[i][k]*transition[j][k];
                d[i]+=prior[i][j]*h[j];
            }
            for(std::size_t i=0;i<2;++i) {
                node.mean[i]+=d[i]*innovation/variance;
                for(std::size_t j=0;j<2;++j)node.p[i][j]-=d[i]*d[j]/variance;
                for(std::size_t j=0;j<3;++j)node.b[i][j]=prior[i][j]-d[i]*e[j]/variance;
            }
            if(!valid_lag_node(node))node.active=false;
        }
        append_lag_node(current);
    }
    bool phase_update(PhaseState& state,const PhaseObservation& row,bool unwrap,double& measured,double& nis,
                      double* whitened=nullptr,bool* uninformative=nullptr,bool smooth=false) noexcept {
        if(uninformative)*uninformative=false;
        if(!state.valid || row.end<=state.sample)return false;
        const auto dt=static_cast<double>(row.end-state.sample)/c_.sample_rate;
        const Matrix transition{{{{1,dt,.5*dt*dt}},{{0,1,dt}},{{0,0,1}}}};
        std::array<double,3> predicted{};Matrix prior{},temporary{};
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j) {
            predicted[i]+=transition[i][j]*state.x[j];
            for(std::size_t k=0;k<3;++k)temporary[i][j]+=transition[i][k]*state.p[k][j];
        }
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j)
            for(std::size_t k=0;k<3;++k)prior[i][j]+=temporary[i][k]*transition[j][k];
        const auto density=phase_process_density();prior[0][0]+=density*dt;
        const auto frequency_process=phase_detail::frequency_process_covariance(frequency_process_density(),dt);
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j)prior[i][j]+=frequency_process[i][j];
        // The projection is a Hann-weighted phase average, not an instantaneous
        // endpoint sample. Brownian phase inside it shares process increments
        // with the endpoint state, hence the explicit negative cross covariance.
        const std::array<double,3> h{1,-phase_lag_,.5*phase_lag2_};
        const auto cross=phase_measurement_cross();
        const auto measurement_variance=phase_measurement_variance(row);
        std::array<double,3> ph{},gain{};double prediction=0,innovation_variance=measurement_variance;
        for(std::size_t i=0;i<3;++i) {
            prediction+=h[i]*predicted[i];
            for(std::size_t j=0;j<3;++j)ph[i]+=prior[i][j]*h[j];
            innovation_variance+=2*h[i]*cross[i];
        }
        for(std::size_t i=0;i<3;++i)innovation_variance+=h[i]*ph[i];
        if(!(innovation_variance>0) || !std::isfinite(innovation_variance))return false;
        auto innovation=row.phase-prediction;
        if(unwrap)innovation=std::remainder(innovation,1.);
        const auto sigma=std::sqrt(innovation_variance);
        // Never guess a cycle across an unresolved phase prediction or repair
        // an inconsistent observation by a historical search.
        if(unwrap && (3*sigma>=.5 || std::abs(innovation)>std::min(.5-3*sigma,5*sigma))) {
            // A noisy row need not erase a previously resolved phase model.
            // Skip only when its prior prediction still resolves the cycle
            // and the observation is statistically consistent with that prior.
            // No state, observation date or numeric history advances on skip.
            const auto predicted_variance=innovation_variance-row.variance;
            if(uninformative)*uninformative=predicted_variance>=0 &&
                3*std::sqrt(predicted_variance)<.5 && std::abs(innovation)<=5*sigma;
            return false;
        }
        measured=prediction+innovation;nis=innovation*innovation/innovation_variance;
        if(whitened)*whitened=innovation/sigma;
        for(std::size_t i=0;i<3;++i){gain[i]=(ph[i]+cross[i])/innovation_variance;state.x[i]=predicted[i]+gain[i]*innovation;}
        Matrix a{},ap{},posterior{};
        std::array<double,3> ac{};
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j) {
            a[i][j]=(i==j?1.:0)-gain[i]*h[j];ac[i]+=a[i][j]*cross[j];
        }
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j)
            for(std::size_t k=0;k<3;++k)ap[i][j]+=a[i][k]*prior[k][j];
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j) {
            for(std::size_t k=0;k<3;++k)posterior[i][j]+=ap[i][k]*a[j][k];
            posterior[i][j]+=gain[i]*measurement_variance*gain[j]-ac[i]*gain[j]-gain[i]*ac[j];
        }
        for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j) {
            state.p[i][j]=.5*(posterior[i][j]+posterior[j][i]);
            if(!std::isfinite(state.p[i][j]) || (i==j && state.p[i][j]<0))return false;
        }
        state.sample=row.end;
        if(smooth) {
            std::array<double,3> e{};for(std::size_t i=0;i<3;++i)e[i]=ph[i]+cross[i];
            update_lag_nodes(transition,h,e,innovation_variance,innovation,state,row.end+1-block_n_);
        }
        return true;
    }
    void retain_phase_observation(const PhaseObservation& row) noexcept {
        if(phase_count_==phase_records){phase_head_=(phase_head_+1)%phase_records;--phase_count_;}
        phase_rows_[(phase_head_+phase_count_)%phase_records]=row;++phase_count_;++phase_since_refit_;
        const auto keep=128ULL*c_.sample_rate,oldest=row.end>keep?row.end-keep:0;
        while(phase_count_>1 && phase_rows_[phase_head_].end<oldest){phase_head_=(phase_head_+1)%phase_records;--phase_count_;}
        // Refit only bounded numeric observations, about once per audio second
        // after the128s window fills. No historical PCM is retained or searched.
        if(phase_since_refit_<50 || phase_rows_[phase_head_].end<=phase_support_first_+block_n_)return;
        auto state=initial_phase_state(phase_rows_[phase_head_],1e8);
        // Truncating the128s numeric prior changes all cross covariances.
        // Reconstruct only the final bounded lag, using the same refit states.
        lag_head_=lag_count_=0;
        const auto lag_first=phase_count_>lag_nodes_.size()?phase_count_-lag_nodes_.size():0;
        if(!lag_first)append_lag_node(state);
        double chisq=0;std::size_t updates=0;
        for(std::size_t i=1;i<phase_count_;++i) {
            const auto& item=phase_rows_[(phase_head_+i)%phase_records];double measured=0,nis=0;
            if(i==lag_first && lag_first)append_lag_node(state);
            if(!phase_update(state,item,false,measured,nis,nullptr,nullptr,i>=lag_first)){clear_continuous();return;}
            chisq+=nis;++updates;
        }
        continuous_=state;phase_chisq_=chisq;phase_updates_=updates;phase_since_refit_=0;
        phase_support_first_=phase_rows_[phase_head_].end+1-block_n_;
        phase_frequency_support_first_=phase_support_first_;
    }
    bool current_frequency_witness()const noexcept {
        return next_>=last_good_&&static_cast<double>(next_-last_good_)/c_.sample_rate<=.4;
    }
    void observe_continuous(double variance) noexcept {
        // A weak phase projection alone is not a new tone-presence witness.
        // The coarse acquisition or accepted independent frequency aperture
        // owns that existing lease, evaluated here at the actual row date.
        if(!c_.continuous_pilot || local_aperture_ || !current_frequency_witness())return;
        const auto end=next_-1;
        // A first independent phase cannot consume overlapping frequency
        // evidence as an independent prior. Retain that marginal and wait for
        // the next disjoint projection; this does not withdraw frequency.
        if(!continuous_.valid&&frequency_process_density()>0&&result.frequency_knowledge_valid&&
           result.frequency_covariance_valid&&result.sample>next_-block_n_)return;
        // Elapsed covariance and cycle ambiguity decide phase continuity.
        // Sparse accepted observations are not missing ADC samples.
        if(!continuous_.valid) {
            phase_origin_=end;phase_reference_hz_=center_;phase_support_first_=next_-block_n_;
            phase_frequency_support_first_=phase_support_first_;
        }
        const auto date=static_cast<long double>(result.phase_sample)-phase_origin_+result.phase_sample_fraction;
        PhaseObservation row{end,std::remainder(result.phase_cycles-
            static_cast<double>(std::remainder(phase_reference_hz_*date/c_.sample_rate,1.L)),1.),variance/(tau*tau)};
        if(!continuous_.valid) {
            const auto prior=2.*c_.sample_rate/fft_n_/3;
            continuous_=initial_phase_state(row,prior*prior);
            seed_independent_phase_frequency(continuous_,row);
            append_lag_node(continuous_);
            retain_phase_observation(row);return;
        }
        double measured=0,nis=0,whitened=0;bool uninformative=false;
        if(!phase_update(continuous_,row,true,measured,nis,&whitened,&uninformative,true)) {
            if(!uninformative)clear_continuous();
            return; // A skipped row grants no new support or phase authority.
        }
        // Conventional two-sided Page CUSUM for a sustained one-standard-
        // deviation innovation mean change. This invalidates the constant-
        // drift model; it does not retroactively qualify its detection latency.
        // The acquired tone/NCO and independent local phase certificates stay.
        phase_change_positive_=std::max(0.,phase_change_positive_+whitened-.5);
        phase_change_negative_=std::max(0.,phase_change_negative_-whitened-.5);
        if(std::max(phase_change_positive_,phase_change_negative_)>10) {
            ++result.continuous_model_resets;result.continuous_last_model_reset_sample=end;
            rn_=0; // The fallback shares the constant-drift model just rejected.
            reject_frequency_knowledge();clear_continuous();return;
        }
        row.phase=measured;phase_chisq_+=nis;++phase_updates_;retain_phase_observation(row);
    }
    bool publish_continuous() noexcept {
        if(!continuous_.valid || continuous_.sample!=next_-1 || phase_updates_<19 || !current_frequency_witness())return false;
        const auto span=static_cast<double>(continuous_.sample-phase_support_first_+1)/c_.sample_rate;
        if(span<.38)return false;
        const auto inflate=std::max(1.,phase_chisq_/std::max<std::size_t>(1,phase_updates_-2));
        const auto f=phase_reference_hz_+continuous_.x[1],rate=continuous_.x[2];
        const auto variance=inflate*continuous_.p[1][1],rate_variance=inflate*continuous_.p[2][2];
        if(!std::isfinite(f) || !(variance>=0) || !std::isfinite(variance) || f<200 || f>c_.sample_rate/2.-200 ||
           std::abs(rate)>c_.maximum_tracking_slew_hz_per_second+3*std::sqrt(rate_variance))return false;
        result.frequency_hz=f;result.frequency_rate_hz_per_second=rate;result.uncertainty_hz=3*std::sqrt(variance);
        result.frequency_variance_hz2=variance;
        result.frequency_rate_covariance_hz2_per_second=inflate*continuous_.p[1][2];
        result.frequency_rate_variance_hz2_per_second2=rate_variance;
        result.frequency_acceleration_psd_hz2_per_second3=frequency_process_density();
        result.sample=continuous_.sample;result.first_sample=phase_frequency_support_first_;
        result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=scale_;
        result.frequency_knowledge_valid=result.frequency_covariance_valid=result.frequency_precision_refined=result.qualified=result.present=true;
        result.frequency_continuous_phase=true;result.frequency_local_aperture=false;
        result.frequency_used_fallback=false;result.frequency_gap_index=0;
        result.frequency_context_first_sample=phase_support_first_;result.frequency_context_end_sample=next_;
        result.frequency_aperture_cube_seconds3=span*span*span;
        result.coherent_aperture_seconds=span;
        result.frequency_support_seconds=static_cast<double>(result.sample-result.first_sample+1)/c_.sample_rate;
        for(std::size_t i=0;i<lag_count_;++i) {
            auto& node=lag_nodes_[(lag_head_+i)%lag_nodes_.size()];if(!node.active)continue;
            node.first=phase_support_first_;node.end=continuous_.sample+1;
            node.evidence_first=phase_frequency_support_first_;node.inflate=inflate;
            node.q=frequency_process_density();node.qualified=true;
        }
        return true;
    }
    void invalidate_phase_fit() noexcept {result.phase_fit_qualified=false;result.phase_fit_support_uncertainty_cycles.reset();}
    void set_center(double f) noexcept {center_=f;step_=std::polar(1.,-tau*f/c_.sample_rate);}
    void discover() noexcept {
        double ws=0,w2=0;
        for(std::size_t i=0;i<fft_n_;++i){const auto w=.5-.5*std::cos(tau*i/(fft_n_-1));raw_[i]*=static_cast<float>(w);ws+=w;w2+=w*w;}
        for(std::size_t i=1,j=0;i<fft_n_;++i){auto bit=fft_n_>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(raw_[i],raw_[j]);}
        for(std::size_t n=2;n<=fft_n_;n*=2){const auto step=std::polar(1.F,static_cast<float>(-tau/n));
            for(std::size_t first=0;first<fft_n_;first+=n){F w{1,0};for(std::size_t j=0;j<n/2;++j){const auto u=raw_[first+j],v=raw_[first+j+n/2]*w;raw_[first+j]=u+v;raw_[first+j+n/2]=u-v;w*=step;}}}
        const auto df=static_cast<double>(c_.sample_rate)/fft_n_;
        const auto lo=std::clamp<std::size_t>(static_cast<std::size_t>(std::floor(low_/df)),1,fft_n_/2-1);
        const auto hi=std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(high_/df)),lo,fft_n_/2-1);
        auto peak=lo;for(auto i=lo;i<=hi;++i)if(std::norm(raw_[i])>std::norm(raw_[peak]))peak=i;
        // Broad physical noise samples, excluding the strongest lobe; no auxiliary decoder or competitor veto.
        std::size_t used=0;for(std::size_t j=0;j<background_.size();++j){const auto k=1+(fft_n_/2-2)*(2*j+1)/(2*background_.size());if(k+4<peak || k>peak+4)background_[used++]=std::norm(raw_[k]);}
        if(used<16)return;
        std::nth_element(background_.begin(),background_.begin()+used/2,background_.begin()+used);
        const auto floor=std::max(background_[used/2]/std::numbers::ln2,1e-30),peak_power=static_cast<double>(std::norm(raw_[peak]));
        if(peak_power<16*floor)return;
        const auto a=std::log(std::max(static_cast<double>(std::norm(raw_[peak-1])),1e-30));
        const auto b=std::log(std::max(peak_power,1e-30)),d=std::log(std::max(static_cast<double>(std::norm(raw_[peak+1])),1e-30));
        const auto denominator=a-2*b+d,fraction=denominator<0?std::clamp(.5*(a-d)/denominator,-.5,.5):0.;
        set_center(std::clamp((peak+fraction)*df,low_,high_));
        noise_=2*floor/(w2*c_.sample_rate);power_=2*peak_power/(ws*ws);acquired_=true;previous_=false;straight_context_=true;pn_=block_used_=0;
        nco_=side_={1,0};sum_=lower_=upper_={};last_good_=next_;
        if(!result.frequency_knowledge_valid){result.frequency_hz=center_;result.uncertainty_hz=2*df;}
    }
    void finish_block() noexcept {
        const auto bandwidth=c_.sample_rate*w2_/(ws_*ws_);
        const auto measured_noise=(std::norm(lower_)+std::norm(upper_))/(c_.sample_rate*w2_);
        if(std::isfinite(measured_noise) && measured_noise>0)noise_=.9*noise_+.1*measured_noise;
        noise_=std::max(noise_,1e-30);
        const auto measured_power=std::max(0.,2*std::norm(sum_)/(ws_*ws_)-noise_*bandwidth);
        // This gate qualifies a row only for the separate straight phase fit.
        // Frequency fallback retains all ten current complex observations.
        const auto good=measured_power>noise_*bandwidth && (!power_ || measured_power>.12*power_);
        power_=.95*power_+.05*measured_power;result.carrier_to_noise_db_hz=10*std::log10(std::max(measured_power/noise_,1e-20));
        const auto phase=std::arg(sum_),variance=good?noise_*bandwidth/(2*measured_power):0.;
        const auto sample=next_-(block_n_+1)/2;
        if(!pn_){context_first_=next_-block_n_;straight_context_=true;previous_=false;}
        rows_[pn_]={sum_/ws_,lower_/ws_,upper_/ws_};
        if(good){
            const auto increment=std::remainder(phase-previous_phase_,tau);
            const auto jump=previous_ && std::abs(increment)>std::min(2.5,std::max(.65,5*std::sqrt(variance+previous_variance_)));
            if(!previous_ || jump){unwrapped_=phase;if(jump){straight_context_=false;invalidate_phase_fit();}}
            else unwrapped_+=increment;
            p_[pn_]={unwrapped_,variance,sample};previous_=true;previous_phase_=phase;previous_variance_=variance;
            result.phase_sample=next_-block_n_+(block_n_-1)/2;
            result.phase_sample_fraction=.5*((block_n_-1)%2);
            const auto distance=static_cast<double>(next_-result.phase_sample)-result.phase_sample_fraction;
            const auto mixer_phase=std::arg(nco_)+tau*center_*distance/c_.sample_rate;
            result.phase_cycles=std::remainder((phase-mixer_phase)/tau,1.);
            const auto phase_process=c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second*block_n_/c_.sample_rate;
            result.phase_uncertainty_cycles=3*std::sqrt(variance+phase_process)/tau;
            result.projection_phase_valid=true;result.phase_qualified=false;
            observe_continuous(variance);
        } else {
            straight_context_=false;previous_=false;
            result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();
        }
        const auto context_end=++pn_==points;
        if(context_end) {
            // Exactly one disjoint frequency record per fixed raw context.
            // The NCO basis stays constant until BOTH fits have finished.
            auto fit=straight_context_?straight_fit():FrequencyFit{};
            if(!straight_context_)invalidate_phase_fit();
            if(!fit.valid)fit=gap_fit();
            if(fit.valid)accept_frequency(fit);
            pn_=0;previous_=false;straight_context_=true;
        }
        if(publish_continuous() && context_end)set_center(std::clamp(result.frequency_hz,low_,high_));
        sum_=lower_=upper_={};side_={1,0};block_used_=0;
    }
    FrequencyFit straight_fit() noexcept {
        invalidate_phase_fit();
        const auto mid=p_[points/2].sample;double mt=0,mp=0;
        for(const auto& p:p_){mt+=static_cast<double>(static_cast<long double>(p.sample)-mid)/c_.sample_rate;mp+=p.phase;}
        mt/=points;mp/=points;double tt=0,tp=0;
        for(const auto& p:p_){const auto t=static_cast<double>(static_cast<long double>(p.sample)-mid)/c_.sample_rate-mt;tt+=t*t;tp+=t*(p.phase-mp);}
        const auto slope=tp/tt;double residual=0,thermal=0,expected=0,maximum_residual=0,phase_thermal=0;
        const auto latest_t=static_cast<double>(p_.back().sample-mid)/c_.sample_rate-mt;
        for(const auto& p:p_){const auto t=static_cast<double>(static_cast<long double>(p.sample)-mid)/c_.sample_rate-mt;
            const auto e=p.phase-mp-slope*t;residual+=e*e;maximum_residual=std::max(maximum_residual,std::abs(e));
            const auto phase_weight=1./points+latest_t*t/tt;phase_thermal+=phase_weight*phase_weight*p.variance;
            thermal+=t*t*p.variance/(tt*tt);expected+=p.variance*(1-1./points-t*t/tt);}
        // Explicitly reject contexts inconsistent with a straight tone. No
        // cycle lifting or historical PCM replay repairs a rejected context.
        if(residual>std::max(.35,9*expected))return {};
        const auto aperture=static_cast<double>(points*block_n_)/c_.sample_rate;
        const auto process=2*c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second/aperture;
        const auto excess=std::max(0.,(residual-expected)/(points-2));
        const auto f=center_+slope/tau,var=(thermal+excess/tt+process)/(tau*tau);
        if(!std::isfinite(var) || f<low_ || f>high_ || std::abs(slope/tau)>20)return {};
        // Absolute cosine phase at the latest actually observed projection
        // center. The mixer increment was constant throughout this context.
        // The local residual slope determines phase transport; pooled numeric
        // oscillator history has no phase authority here.
        const auto phase_date=next_-block_n_+(block_n_-1)/2;
        const auto phase_fraction=.5*((block_n_-1)%2);
        const auto mixer_phase=std::arg(nco_)+tau*center_*(static_cast<double>(next_-phase_date)-phase_fraction)/c_.sample_rate;
        const auto fitted_phase=mp+slope*latest_t;
        const auto phase_process=2*c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second*aperture;
        result.phase_fit_cycles=std::remainder((fitted_phase-mixer_phase)/tau,1.);
        result.phase_fit_uncertainty_cycles=(3*std::sqrt(phase_thermal+phase_process)+maximum_residual)/tau;
        result.phase_fit_frequency_hz=f;result.phase_fit_frequency_uncertainty_hz=3*std::sqrt(var);
        result.phase_fit_sample=phase_date;result.phase_fit_sample_fraction=phase_fraction;
        result.phase_fit_first_sample=context_first_;result.phase_fit_end_sample=next_;
        // The anchor phase already contains fitted slope uncertainty. Predict
        // each support endpoint jointly, retaining its covariance cancellation
        // toward earlier samples instead of adding an independent slope bound.
        // The prediction variance and process-distance allowance are convex,
        // so their maximum on the supported interval is at an endpoint.
        double support_uncertainty=0;
        for(const auto endpoint:std::array{context_first_,next_-1}) {
            const auto distance=static_cast<double>((static_cast<long double>(endpoint)-phase_date-phase_fraction)/c_.sample_rate);
            const auto at=latest_t+distance;double prediction_variance=0;
            for(const auto& p:p_) {
                const auto t=static_cast<double>(static_cast<long double>(p.sample)-mid)/c_.sample_rate-mt;
                const auto weight=1./points+at*t/tt;
                prediction_variance+=weight*weight*(p.variance+excess);
            }
            // Preserve both existing process allowances conservatively. The
            // independent distance term here contains process noise only.
            const auto bound=(3*std::sqrt(prediction_variance+phase_process)+maximum_residual+
                3*std::sqrt(process)*std::abs(distance))/tau;
            support_uncertainty=std::max(support_uncertainty,bound);
        }
        result.phase_fit_qualified=std::isfinite(result.phase_fit_uncertainty_cycles) && std::isfinite(support_uncertainty);
        if(result.phase_fit_qualified)result.phase_fit_support_uncertainty_cycles=support_uncertainty;
        return {true,f,var,aperture,false,0,aperture*aperture*aperture};
    }
    FrequencyFit gap_fit() noexcept {
        // Current-context profile only. Each gap has independent unknown
        // complex amplitudes on its two sides and one common frequency.
        // Nothing here publishes phase or replays historical PCM.
        struct Model {double frequency=0,score=0,bic=0,variance=0,chi=0,snr=0;std::size_t index=0;};
        std::array<Model,points> models{};
        double noise=0,energy=0;
        for(const auto& row:rows_){noise+=std::norm(row.lower)+std::norm(row.upper);energy+=std::norm(row.value);}
        noise=std::max(noise/(2*points),1e-30);
        const auto dt=static_cast<double>(block_n_)/c_.sample_rate;
        const auto radius=2.*c_.sample_rate/fft_n_;
        constexpr double grid=.1;
        const auto steps=static_cast<std::size_t>(2*radius/grid);
        if(steps+1>profile_points || !std::isfinite(energy) || !std::isfinite(noise))return {};
        const auto score=[&](std::size_t gap,double f) {
            C left{},right{};
            for(std::size_t i=0;i<points;++i) {
                const auto t=(static_cast<double>(i)-.5*(points-1))*dt;
                const auto value=rows_[i].value*std::polar(1.,-tau*f*t);
                if(!gap || i<gap)left+=value;else right+=value;
            }
            return !gap?std::norm(left)/points:
                std::norm(left)/gap+std::norm(right)/(points-gap);
        };
        const auto bic=[&](std::size_t gap,double f) {
            const auto penalty=gap?2*std::log(9.):0.;
            return 2*std::max(0.,energy-score(gap,f))/noise+(gap?5:3)*std::log(20.)+penalty;
        };
        std::size_t best_model=0;
        for(std::size_t gap=0;gap<points;++gap) {
            auto& model=models[gap];double best=-1;
            for(std::size_t i=0;i<=steps;++i) {
                const auto f=-radius+i*grid,value=score(gap,f);
                profile_bic_[gap][i]=2*std::max(0.,energy-value)/noise+
                    (gap?5:3)*std::log(20.)+(gap?2*std::log(9.):0.);
                if(value>best){best=value;model.index=i;}
            }
            auto f=-radius+model.index*grid;
            const auto left=score(gap,f-grid),right=score(gap,f+grid);
            const auto curvature=(left-2*best+right)/(grid*grid);
            if(curvature<0)f+=std::clamp(.5*(left-right)/(grid*curvature),-grid,grid);
            model.frequency=f;model.score=score(gap,f);model.bic=bic(gap,f);
            constexpr double h=.01;
            const auto refined_curvature=(score(gap,f-h)-2*model.score+score(gap,f+h))/(h*h);
            model.variance=refined_curvature<0?noise/(-refined_curvature):std::numeric_limits<double>::infinity();
            model.chi=std::max(0.,(energy-model.score)/noise);
            model.snr=std::max(0.,model.score/noise-(gap?2:1));
            if(model.bic<models[best_model].bic)best_model=gap;
        }
        const auto& best=models[best_model];
        const auto limit=best.bic+9;
        double lower=radius,upper=-radius,aperture=points*dt;
        bool clipped=false;
        // Preserve the COMPLETE model/frequency envelope, not only winning
        // curvature. Plausible unknown phase gaps reduce claimed coherence.
        for(std::size_t gap=0;gap<points;++gap) {
            const auto& model=models[gap];if(model.bic>limit)continue;
            lower=std::min(lower,model.frequency);upper=std::max(upper,model.frequency);
            if(gap)aperture=std::min(aperture,std::max(gap,points-gap)*dt);
            for(std::size_t i=0;i<=steps;++i)if(profile_bic_[gap][i]<=limit) {
                const auto f=-radius+i*grid;lower=std::min(lower,f);upper=std::max(upper,f);
                clipped|=i==0 || i==steps;
            }
        }
        double side_best=0;
        for(std::size_t i=0;i<=steps;++i) {
            const auto f=-radius+i*grid;
            if(std::abs(f-best.frequency)>5)side_best=std::max(side_best,score(best_model,f));
        }
        const auto f=center_+best.frequency;
        const auto uncertainty=std::max({best.frequency-lower+grid,upper-best.frequency+grid,3*std::sqrt(best.variance)});
        const auto process=2*c_.phase_noise_radians_per_sqrt_second*c_.phase_noise_radians_per_sqrt_second/(tau*tau*aperture);
        const auto variance=uncertainty*uncertainty/9+process;
        if(clipped || !best.index || best.index==steps || best.snr<16 || best.chi>25 ||
           !(best.score>1.5*side_best) || !std::isfinite(variance) || f<low_ || f>high_)return {};
        const auto selected_aperture=best_model?std::max(best_model,points-best_model)*dt:points*dt;
        const auto left=best_model*dt,right=(points-best_model)*dt;
        const auto cube=best_model?left*left*left+right*right*right:selected_aperture*selected_aperture*selected_aperture;
        return {true,f,variance,selected_aperture,true,static_cast<unsigned>(best_model),cube};
    }
    void accept_frequency(const FrequencyFit& fit) noexcept {
        // A model-change detector can fire inside this disjoint context. Its
        // frequency summary must not reintroduce support from the rejected
        // model; the independent local phase certificate remains available.
        if(!local_aperture_ && result.continuous_model_resets &&
           context_first_<result.continuous_last_model_reset_sample)return;
        const auto f=fit.hz,var=fit.variance,aperture=fit.aperture;
        auto retain_publication=!local_aperture_ && !full_mode_ && c_.continuous_pilot &&
            result.frequency_knowledge_valid;
        if(!retain_publication) {
            result.frequency_continuous_phase=false;
            result.frequency_used_fallback=fit.fallback;result.frequency_gap_index=fit.gap;
            result.frequency_aperture_cube_seconds3=fit.aperture_cube;
            result.frequency_context_first_sample=context_first_;result.frequency_context_end_sample=next_;
        }
        if(local_aperture_ || (full_mode_&&frequency_process_density()>0)) {
            // This mode is explicitly selected at a known independent retune.
            // A valid frequency fit does not require a usable phase anchor.
            const auto qualified=fit.valid && std::isfinite(var) && var>=0;
            // Frequency validity is independent of phase-correction eligibility.
            // Its slew enclosure spans all actual support, including a valid
            // frequency-only unknown-phase-gap fit.
            const auto support_duration=static_cast<double>(next_-context_first_)/c_.sample_rate;
            const auto thermal=std::sqrt(std::max(0.,var));
            // S is the configured operational tracking allowance, not a drift
            // inferred from ppm accuracy or measured by this one aperture.
            // Publish its worst-endpoint enclosure over observed support only:
            // Straight fits use the midpoint enclosure S*T/2. A gap fit's
            // independently phased sides can have unequal information and an
            // off-center effective date; S*T conservatively covers both ends.
            // Neither enclosure grants an extrapolation lease.
            auto drift_distance=fit.fallback?support_duration:support_duration/2;
            if(full_mode_) {
                // The final merged Hann row can move the frequency kernel's
                // mean away from the geometric midpoint. Enclose both actual
                // endpoints using that dated kernel, never a guessed center.
                const auto observed=static_cast<double>(next_-1-context_first_)/c_.sample_rate;
                const auto mu=frequency_kernel.mu_seconds;
                drift_distance=frequency_kernel.valid&&mu>=0&&mu<=observed?
                    std::max(mu,observed-mu):observed;
            }
            const auto sigma=thermal+c_.maximum_tracking_slew_hz_per_second*drift_distance/3;
            result.sample=next_-1;result.first_sample=context_first_;
            result.frequency_hz=f;result.uncertainty_hz=3*sigma;
            result.frequency_rate_hz_per_second=0;
            result.frequency_variance_hz2=sigma*sigma;
            result.frequency_rate_covariance_hz2_per_second=0;
            result.frequency_rate_variance_hz2_per_second2=0;
            result.frequency_acceleration_psd_hz2_per_second3=0;
            result.frequency_knowledge_valid=result.frequency_covariance_valid=qualified && std::isfinite(result.frequency_variance_hz2);
            result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=scale_;
            result.frequency_local_aperture=true;result.coherent_aperture_seconds=aperture;
            result.frequency_support_seconds=aperture;result.frequency_precision_refined=qualified;
            result.qualified=result.frequency_covariance_valid && f>=200 && f<=c_.sample_rate/2.-200;
            last_good_=next_;set_center(std::clamp(f,low_,high_));result.present=true;
            return; // Never enter numeric cross-aperture pooling in this mode.
        }
        const auto common=(f-c_.expected_hz)/scale_;
        // Reject old frequency knowledge only with independent frequency
        // evidence, not a phase jump. Compare both at this fit's actual mean
        // date, with the conditional rate covariance propagated over elapsed
        // time. A supported constant drift is already represented by that rate.
        if(result.frequency_knowledge_valid && result.frequency_covariance_valid) {
            const auto date=fit.dated?fit.sample:context_first_+(points*block_n_-1)/2;
            const auto delta=date>=result.sample?static_cast<double>(date-result.sample):-static_cast<double>(result.sample-date);
            const auto dt=(delta+(fit.dated?fit.sample_fraction:0))/c_.sample_rate;
            const auto old_variance=std::fma(dt,std::fma(dt,result.frequency_rate_variance_hz2_per_second2,
                2*result.frequency_rate_covariance_hz2_per_second),result.frequency_variance_hz2);
            const auto prediction=std::fma(result.frequency_rate_hz_per_second,dt,result.frequency_hz);
            const auto process_sigma=std::sqrt(result.frequency_acceleration_psd_hz2_per_second3*std::pow(std::abs(dt),3)/3);
            if(old_variance>=0 && std::isfinite(old_variance) &&
               std::abs(f-prediction)>3*(std::sqrt(old_variance)+process_sigma)+6*std::sqrt(var)) {
                rn_=0;reject_frequency_knowledge();clear_continuous();
                retain_publication=false;
                result.frequency_used_fallback=fit.fallback;result.frequency_gap_index=fit.gap;
                result.frequency_aperture_cube_seconds3=fit.aperture_cube;
                result.frequency_context_first_sample=context_first_;result.frequency_context_end_sample=next_;
            }
        }
        const auto keep=(c_.continuous_pilot?128ULL:36ULL)*c_.sample_rate,oldest=next_>keep?next_-keep:0;std::size_t retained=0;
        for(std::size_t i=0;i<rn_;++i)if(r_[i].first>=oldest)r_[retained++]=r_[i];
        rn_=retained;
        if(rn_==(c_.continuous_pilot?records:192)){std::move(r_.begin()+1,r_.begin()+rn_,r_.begin());--rn_;}
        const auto date=fit.dated?fit.sample:context_first_+(points*block_n_-1)/2;
        r_[rn_++]={common,std::max(var/(scale_*scale_),1e-24),aperture,context_first_,next_-1,date,fit.dated?fit.sample_fraction:-1};
        last_good_=next_;
        // A context ending on an uninformative row may add independent dated
        // frequency evidence without replacing a still-qualified continuous
        // publication. Retain all its original support/covariance dates; a
        // later actual phase row, genuine reset or expiry decides its future.
        if(!retain_publication)pool();
        // Increment changes only at an independent context boundary.
        set_center(std::clamp(f,low_,high_));result.present=true;
    }
    double record_time(const Record& r,std::uint64_t now)const noexcept {
        if(r.sample_fraction>=0) {
            const auto delta=r.sample>=now?static_cast<double>(r.sample-now):-static_cast<double>(now-r.sample);
            return (delta+r.sample_fraction)/c_.sample_rate;
        }
        // Preserve the ordinary/end0 R5 arithmetic exactly. New full-dated
        // records subtract integer ADC dates before adding their fraction.
        return static_cast<double>(static_cast<long double>(r.sample)-now)/c_.sample_rate;
    }
    void pool() noexcept {
        if(!rn_)return;
        // This fallback fits a constant drift to independent windows. It
        // remains available for that model; positive acceleration needs the
        // continuous filter's process covariance instead of this LS interval.
        if(c_.continuous_pilot&&frequency_process_density()>0)return;
        // Evaluate the local frequency model at its actual last inclusive
        // support endpoint. Internal record dates remain aperture centers.
        const auto now=r_[rn_-1].last;double weight=0,mt=0,mf=0;
        for(std::size_t i=0;i<rn_;++i){const auto w=1/r_[i].variance,t=record_time(r_[i],now);weight+=w;mt+=w*t;mf+=w*r_[i].hz;}
        mt/=weight;mf/=weight;double tt=0,tf=0;
        for(std::size_t i=0;i<rn_;++i){const auto w=1/r_[i].variance,t=record_time(r_[i],now)-mt;tt+=w*t*t;tf+=w*t*(r_[i].hz-mf);}
        const bool linear=rn_>=4 && tt>0;const auto slope=linear?tf/tt:0.,frequency=mf-slope*mt;
        double chisq=0;
        for(std::size_t i=0;i<rn_;++i){const auto t=record_time(r_[i],now);
            const auto error=r_[i].hz-frequency-slope*t;chisq+=error*error/r_[i].variance;
}
        const auto thermal=1/weight+(linear?mt*mt/tt:0);
        // Between-aperture variation is measured, rather than reinterpreted as
        // uninterrupted phase coherence. The reported interval is conditional
        // on this locally linear oscillator model, not a future drift lease.
        const auto inflate=std::max(1.,chisq/std::max<std::size_t>(1,rn_-(linear?2:1)));
        const auto uncertainty=3*std::sqrt(thermal*inflate)*std::abs(scale_);
        const auto span=static_cast<double>(r_[rn_-1].last-r_[0].first+1)/c_.sample_rate;
        result.sample=now;result.first_sample=r_[0].first;result.frequency_hz=c_.expected_hz+frequency*scale_;
        result.frequency_rate_hz_per_second=slope*scale_;result.uncertainty_hz=uncertainty;
        const auto covariance_scale=inflate*scale_*scale_;
        result.frequency_variance_hz2=covariance_scale*thermal;
        result.frequency_rate_covariance_hz2_per_second=linear?covariance_scale*(-mt/tt):0;
        result.frequency_rate_variance_hz2_per_second2=linear?covariance_scale/tt:0;
        result.frequency_acceleration_psd_hz2_per_second3=0;
        result.frequency_covariance_valid=linear && std::isfinite(result.frequency_variance_hz2) &&
            std::isfinite(result.frequency_rate_covariance_hz2_per_second) &&
            std::isfinite(result.frequency_rate_variance_hz2_per_second2);
        result.frequency_knowledge_valid=result.frequency_covariance_valid;
        result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=scale_;
        result.coherent_aperture_seconds=r_[rn_-1].aperture;result.frequency_support_seconds=span;
        result.frequency_precision_refined=rn_>=4;
        result.qualified=rn_>=4 && std::isfinite(uncertainty) &&
            std::abs(slope*scale_)<=c_.maximum_tracking_slew_hz_per_second+3*std::abs(scale_)/std::sqrt(std::max(tt,1e-30)) &&
            result.frequency_hz>=200 && result.frequency_hz<=c_.sample_rate/2.-200;
    }
};
}
}
