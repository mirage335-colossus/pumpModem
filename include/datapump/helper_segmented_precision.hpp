#pragma once
#include "datapump/helper_tone_tracker.hpp"
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
    static constexpr std::size_t maximum_fft=32768,points=10,records=192;
    struct Point {double phase=0,variance=0;std::uint64_t sample=0;};
    struct Row {C value{},lower{},upper{};};
    struct FrequencyFit {
        bool valid=false;double hz=0,variance=0,aperture=0;
        bool fallback=false;unsigned gap=0;double aperture_cube=0;
    };
    static constexpr std::size_t profile_points=251;
    struct Record {double hz=0,variance=0,aperture=0;std::uint64_t first=0,last=0,sample=0;};
    ToneTrackerConfig c_;
    std::array<F,maximum_fft> raw_{};
    std::array<double,128> background_{};
    std::array<Point,points> p_{};
    std::array<Row,points> rows_{};
    std::array<std::array<double,profile_points>,points> profile_bic_{};
    std::array<Record,records> r_{};
    std::uint64_t next_=0,last_good_=0,context_first_=0,settled_first_=0;
    std::size_t fft_n_=0,ordinary_fft_n_=0,local_fft_n_=0,raw_used_=0,block_n_=0,block_used_=0,pn_=0,rn_=0;
    double low_=0,high_=0,center_=0,scale_=1,noise_=0,power_=0,ws_=0,w2_=0;
    double previous_phase_=0,previous_variance_=0,unwrapped_=0;
    C nco_{1,0},step_{1,0},side_{1,0},side_step_{1,0},sum_{},lower_{},upper_{};
    bool acquired_=false,previous_=false,straight_context_=true,local_aperture_=false;
public:
    ToneTrackerSnapshot result;
    explicit Pilot(ToneTrackerConfig c,std::uint64_t first):c_(c) {
        fft_n_=1024;while(fft_n_<c.sample_rate*.16 && fft_n_<maximum_fft)fft_n_*=2;
        ordinary_fft_n_=fft_n_;
        local_fft_n_=1024;while(local_fft_n_<c.sample_rate*.08 && local_fft_n_<maximum_fft)local_fft_n_*=2;
        block_n_=std::max<std::size_t>(16,std::lround(c.sample_rate*.02));
        for(std::size_t i=0;i<block_n_;++i){const auto w=.5-.5*std::cos(tau*i/(block_n_-1));ws_+=w;w2_+=w*w;}
        side_step_=std::polar(1.,-tau*200/c.sample_rate);geometry();reset(first);
    }
    void geometry() noexcept {low_=std::max(20.,c_.expected_hz-c_.search_half_width_hz);high_=std::min(c_.sample_rate/2.-20.,c_.expected_hz+c_.search_half_width_hz);}
    void reset(std::uint64_t first) noexcept {
        next_=last_good_=settled_first_=first;raw_used_=block_used_=pn_=rn_=0;acquired_=previous_=false;straight_context_=true;
        local_aperture_=false;fft_n_=ordinary_fft_n_;
        scale_=1;noise_=power_=0;nco_=side_={1,0};sum_=lower_=upper_={};
        result={};result.sample_rate=c_.sample_rate;result.captured_end_sample=first;
        result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=scale_;
    }
    void begin(std::uint64_t first,double expected,double width,bool preserve,double scale,std::uint64_t settled_first=0,
               bool qualify_local_aperture=false) {
        if(first!=next_ || !std::isfinite(expected) || !std::isfinite(width) || width<0 ||
           !std::isfinite(scale) || scale==0 ||
           (settled_first && settled_first<first) ||
           (qualify_local_aperture && (preserve || scale!=1)) ||
           std::max(20.,expected-width)>std::min(c_.sample_rate/2.-20.,expected+width))
            throw Error("invalid dated pilot segment geometry");
        // A caller retaining frequency records asserts the common residual model.
        // No phase, sample frontier or measured support is carried across retune.
        const auto old_common=(result.frequency_hz-c_.expected_hz)/scale_;
        const auto old_uncertainty=result.uncertainty_hz/std::abs(scale_);
        const auto old_rate=result.frequency_rate_hz_per_second/scale_;
        c_.expected_hz=expected;c_.search_half_width_hz=width;scale_=scale;geometry();
        local_aperture_=qualify_local_aperture;fft_n_=local_aperture_?local_fft_n_:ordinary_fft_n_;
        settled_first_=settled_first?settled_first:first;
        if(!preserve){rn_=0;acquired_=false;raw_used_=0;noise_=power_=0;}
        else if(acquired_){
            result.frequency_hz=expected+old_common*scale_;
            result.uncertainty_hz=old_uncertainty*std::abs(scale_);
            result.frequency_rate_hz_per_second=old_rate*scale_;
            const auto ratio=scale_/result.frequency_model_residual_scale;
            result.frequency_variance_hz2*=ratio*ratio;
            result.frequency_rate_covariance_hz2_per_second*=ratio*ratio;
            result.frequency_rate_variance_hz2_per_second2*=ratio*ratio;
            set_center(std::clamp(result.frequency_hz,low_,high_));
        }
        else raw_used_=0; // An FFT never crosses a known independent hop phase.
        block_used_=pn_=0;previous_=false;straight_context_=true;sum_=lower_=upper_={};nco_=side_={1,0};
        result.qualified=result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();result.present=false;
        result.frequency_precision_refined=false;
        result.frequency_local_aperture=local_aperture_;
        result.frequency_model_expected_hz=expected;result.frequency_model_residual_scale=scale_;
        if(!preserve || !acquired_)last_good_=first;
    }
    void push(std::span<const float> pcm,std::uint64_t first) {
        if(first!=next_ || pcm.size()>UINT64_MAX-first)throw Error("pilot tracker requires contiguous raw samples");
        for(const auto x:pcm)if(!std::isfinite(x))throw Error("pilot tracker received nonfinite PCM");
        for(const auto x:pcm){
            ++next_;
            if(next_-1<settled_first_)continue;
            if(!acquired_){raw_[raw_used_++]={x,0};if(raw_used_==fft_n_){discover();raw_used_=0;}continue;}
            const auto w=.5-.5*std::cos(tau*block_used_/(block_n_-1));
            const auto z=static_cast<double>(x)*nco_*w;sum_+=z;lower_+=z*side_;upper_+=z*std::conj(side_);
            nco_*=step_;side_*=side_step_;
            if(++block_used_==block_n_){nco_/=std::max(std::abs(nco_),1e-30);finish_block();}
        }
        result.captured_end_sample=next_;
        const auto age=static_cast<double>(next_-last_good_)/c_.sample_rate;
        result.observation_age_seconds=result.sample<next_?static_cast<double>(next_-1-result.sample)/c_.sample_rate:0;
        if(age>.4){result.qualified=result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();result.frequency_precision_refined=false;}
        if(age>.55)result.present=false;
        if(result.phase_sample<next_ && next_-1-result.phase_sample>static_cast<std::uint64_t>(.04*c_.sample_rate))result.phase_qualified=result.projection_phase_valid=false;
        if(age>1. && acquired_){acquired_=false;raw_used_=rn_=pn_=0;previous_=false;result.qualified=result.present=result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();}
    }
private:
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
        result.frequency_hz=center_;result.uncertainty_hz=2*df;
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
        } else {
            straight_context_=false;previous_=false;
            result.phase_qualified=result.projection_phase_valid=false;invalidate_phase_fit();
        }
        if(++pn_==points) {
            // Exactly one disjoint frequency record per fixed raw context.
            // The NCO basis stays constant until BOTH fits have finished.
            auto fit=straight_context_?straight_fit():FrequencyFit{};
            if(!straight_context_)invalidate_phase_fit();
            if(!fit.valid)fit=gap_fit();
            if(fit.valid)accept_frequency(fit);
            pn_=0;previous_=false;straight_context_=true;
        }
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
        const auto f=fit.hz,var=fit.variance,aperture=fit.aperture;
        result.frequency_used_fallback=fit.fallback;result.frequency_gap_index=fit.gap;
        result.frequency_aperture_cube_seconds3=fit.aperture_cube;
        result.frequency_context_first_sample=context_first_;result.frequency_context_end_sample=next_;
        if(local_aperture_) {
            // This mode is explicitly selected at a known independent retune.
            // A successful gap fit can remain diagnostic but cannot qualify
            // one local frequency aperture or manufacture a phase anchor.
            const auto support=result.phase_fit_support_uncertainty_cycles;
            const auto qualified=!fit.fallback && result.phase_fit_qualified && support &&
                std::isfinite(*support) && *support>=0 && *support<.125;
            const auto thermal=std::sqrt(std::max(0.,var));
            // S is the configured operational tracking allowance, not a drift
            // inferred from ppm accuracy or measured by this one aperture.
            // Publish its worst-endpoint enclosure over observed support only:
            // U = 3 sqrt(var) + S*T/2. No extrapolation lease follows.
            const auto sigma=thermal+c_.maximum_tracking_slew_hz_per_second*aperture/6;
            result.sample=next_-1;result.first_sample=context_first_;
            result.frequency_hz=f;result.uncertainty_hz=3*sigma;
            result.frequency_rate_hz_per_second=0;
            result.frequency_variance_hz2=sigma*sigma;
            result.frequency_rate_covariance_hz2_per_second=0;
            result.frequency_rate_variance_hz2_per_second2=0;
            result.frequency_covariance_valid=qualified && std::isfinite(result.frequency_variance_hz2);
            result.frequency_model_expected_hz=c_.expected_hz;result.frequency_model_residual_scale=1;
            result.frequency_local_aperture=true;result.coherent_aperture_seconds=aperture;
            result.frequency_support_seconds=aperture;result.frequency_precision_refined=qualified;
            result.qualified=result.frequency_covariance_valid && f>=200 && f<=c_.sample_rate/2.-200;
            last_good_=next_;set_center(std::clamp(f,low_,high_));result.present=true;
            return; // Never enter numeric cross-aperture pooling in this mode.
        }
        const auto common=(f-c_.expected_hz)/scale_;
        // A new incompatible frequency level starts new dated evidence. This
        // does not reject a larger initial RF error or manufacture continuity.
        if(rn_ && result.qualified && std::abs(f-result.frequency_hz)>result.uncertainty_hz+6*std::sqrt(var)+
            c_.maximum_tracking_slew_hz_per_second*(static_cast<double>(next_-last_good_)/c_.sample_rate+.2))rn_=0;
        const auto keep=36ULL*c_.sample_rate,oldest=next_>keep?next_-keep:0;std::size_t retained=0;
        for(std::size_t i=0;i<rn_;++i)if(r_[i].first>=oldest)r_[retained++]=r_[i];
        rn_=retained;
        if(rn_==records){std::move(r_.begin()+1,r_.end(),r_.begin());--rn_;}
        const auto date=context_first_+(points*block_n_-1)/2;
        r_[rn_++]={common,std::max(var/(scale_*scale_),1e-24),aperture,context_first_,next_-1,date};
        last_good_=next_;pool();
        // Increment changes only at an independent context boundary.
        set_center(std::clamp(f,low_,high_));result.present=true;
    }
    void pool() noexcept {
        if(!rn_)return;
        // Evaluate the local frequency model at its actual last inclusive
        // support endpoint. Internal record dates remain aperture centers.
        const auto now=r_[rn_-1].last;double weight=0,mt=0,mf=0;
        for(std::size_t i=0;i<rn_;++i){const auto w=1/r_[i].variance,t=static_cast<double>(static_cast<long double>(r_[i].sample)-now)/c_.sample_rate;weight+=w;mt+=w*t;mf+=w*r_[i].hz;}
        mt/=weight;mf/=weight;double tt=0,tf=0;
        for(std::size_t i=0;i<rn_;++i){const auto w=1/r_[i].variance,t=static_cast<double>(static_cast<long double>(r_[i].sample)-now)/c_.sample_rate-mt;tt+=w*t*t;tf+=w*t*(r_[i].hz-mf);}
        const bool linear=rn_>=4 && tt>0;const auto slope=linear?tf/tt:0.,frequency=mf-slope*mt;
        double chisq=0;
        for(std::size_t i=0;i<rn_;++i){const auto t=static_cast<double>(static_cast<long double>(r_[i].sample)-now)/c_.sample_rate;
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
        result.frequency_covariance_valid=linear && std::isfinite(result.frequency_variance_hz2) &&
            std::isfinite(result.frequency_rate_covariance_hz2_per_second) &&
            std::isfinite(result.frequency_rate_variance_hz2_per_second2);
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
