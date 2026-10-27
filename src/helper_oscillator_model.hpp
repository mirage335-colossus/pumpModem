#pragma once
#include "helper_link_state.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::helper {
// Conditional Gaussian frequency model only: no PCM, phase, UTC, receiver
// admission or completion authority. Prior sigmas and process densities are
// explicit assumptions, never derived from configured static ppm accuracy.
struct OscillatorFrequencyModelConfig {
    LinkTopology topology=LinkTopology::independent_audio;
    // These zero-mean priors are dated at the FIRST aperture's START. They
    // are propagated to each observed inclusive endpoint before its update.
    double rf_prior_sigma_ppm=0,audio_prior_sigma_ppm=0;
    double rf_drift_prior_sigma_ppm_per_second=0,audio_drift_prior_sigma_ppm_per_second=0;
    // Independent white acceleration PSD, ppm^2/s^3. Missing means unknown
    // evolution and reset declines. Explicit zero is constant fractional drift.
    std::optional<double> rf_acceleration_psd_ppm2_per_second3;
    std::optional<double> audio_acceleration_psd_ppm2_per_second3;
    double maximum_prediction_age_seconds=0;
    // Declared unexplained retune-frequency variance. Zero assumes none
    // initially; learning cannot calibrate first visits or predict IID errors.
    double retune_variance_floor_hz2=0;
    bool learn_repeat_visit_scatter=true;
};
// Producer certificate for its nonnegative normalized effective FREQUENCY kernel w(t),
// supported on [a,b], a=first/Fs, b=(end-1)/Fs (last observed sample). Define
// k(s)=integral[a..s] w(t)(s-t)dt. This model never guesses w from the span or
// assumes an unequal-row phase-slope fit is dated at the span midpoint.
struct FrequencyApertureProcessMoments {
    bool valid=false;
    std::uint64_t first_sample=0,end_sample=0,state_endpoint_sample=0;
    double mass=0,mu_seconds=0; // integral w=1; mu=integral(b-t)w(t)dt
    double integral_k_seconds2=0;
    double integral_b_minus_s_k_seconds3=0;
    double integral_k_squared_seconds3=0;
    double k_at_end_seconds=0,k_prime_at_end=0;
};
// One integrated independent fit per increasing actual phase segment. Several
// fits from the same retune share its frequency error and must be combined by
// their owner first. Pooled snapshots/repeated polls are not measurements.
struct IndependentFrequencyAperture {
    LinkMapping mapping;
    std::uint64_t first_sample=0,end_sample=0,sample=0;
    // sample+fraction is the actual effective frequency date b-mu.
    double sample_fraction=0,frequency_hz=0;
    // Independent measurement variance (AWGN and independently modelled phase
    // diffusion). EXCLUDES this model's acceleration process contribution.
    // A producer's empirical residual may contain that same acceleration;
    // keep such residual diagnostics separate rather than counting it twice.
    double variance_hz2=0,coherent_aperture_seconds=0;
    // Mandatory for positive acceleration PSD. Q0 accepts an existing dated
    // frequency mean without kernel process moments; C and Rprocess are zero.
    std::optional<FrequencyApertureProcessMoments> process_moments;
};
enum class OscillatorModelReason { accepted,invalid_configuration,invalid_mapping,
    invalid_measurement,overlapping_support,reused_phase_segment,timebase_changed,reference_changed,
    prediction_too_old,no_observation,numeric_failure };
struct OscillatorFrequencyPrediction {
    // Numeric/provenance validity under assumptions, not empirical qualification.
    bool valid=false;OscillatorModelReason reason=OscillatorModelReason::no_observation;
    double helper_residual_hz=0,data_residual_hz=0;
    double helper_variance_hz2=0,data_variance_hz2=0,retune_variance_hz2=0;
    double rf_fraction=0,audio_fraction=0,rf_fraction_sigma=0,audio_fraction_sigma=0;
    double rf_drift_fraction_per_second=0,audio_drift_fraction_per_second=0;
    // RF fraction, audio fraction, RF fraction/s, audio fraction/s.
    std::array<std::array<double,4>,4> fractional_covariance{};
    std::uint64_t sample=0,evidence_first_sample=0,evidence_end_sample=0;
    double observation_age_seconds=0,usable_aperture_seconds=0,aperture_cube_seconds3=0;
    std::size_t measurements=0,retained_measurements=0,repeat_visits=0;
    double helper_three_sigma_hz()const noexcept{return 3*std::sqrt(helper_variance_hz2);}
    double data_three_sigma_hz()const noexcept{return 3*std::sqrt(data_variance_hz2);}
};
class OscillatorFrequencyModel {
    using Vector=std::array<double,4>;
    using Matrix=std::array<Vector,4>;
    struct Visit {double rf=0;int sideband=1;std::size_t count=0;};
    struct NumericAperture {
        Vector row{};double observed=0,variance=0,aperture=0;
        std::uint64_t first=0,end=0;
        FrequencyApertureProcessMoments moments{};
    };
    struct ObservationNoise {Vector cross{};double process_variance=0;};
    static constexpr std::size_t history_capacity=256;
public:
    bool reset(const OscillatorFrequencyModelConfig& config)noexcept {
        *this=OscillatorFrequencyModel{};config_=config;
        if((config.topology!=LinkTopology::independent_audio&&config.topology!=LinkTopology::shared_radio)||
            !nonnegative(config.rf_prior_sigma_ppm)||!nonnegative(config.audio_prior_sigma_ppm)||
            !nonnegative(config.rf_drift_prior_sigma_ppm_per_second)||!nonnegative(config.audio_drift_prior_sigma_ppm_per_second)||
            !config.rf_acceleration_psd_ppm2_per_second3||!nonnegative(*config.rf_acceleration_psd_ppm2_per_second3)||
            (config.topology==LinkTopology::independent_audio&&(!config.audio_acceleration_psd_ppm2_per_second3||
                !nonnegative(*config.audio_acceleration_psd_ppm2_per_second3)))||
            !nonnegative(config.maximum_prediction_age_seconds)||!nonnegative(config.retune_variance_floor_hz2))return false;
        covariance_=initial_covariance();valid_=finite(state_,covariance_);return valid_;
    }
    OscillatorModelReason observe(const IndependentFrequencyAperture& input,std::uint64_t captured_end_sample)noexcept {
        if(!valid_)return OscillatorModelReason::invalid_configuration;
        if(!mapping_valid(input.mapping))return OscillatorModelReason::invalid_mapping;
        const auto& m=input.mapping;
        if(input.first_sample>=input.end_sample||input.first_sample<m.settled_first_sample||input.end_sample>m.settled_end_sample||
            input.end_sample>captured_end_sample||input.sample<input.first_sample||input.sample>=input.end_sample||
            !std::isfinite(input.sample_fraction)||input.sample_fraction<0||input.sample_fraction>=1||
            !std::isfinite(input.frequency_hz)||!std::isfinite(input.variance_hz2)||input.variance_hz2<=0||
            !std::isfinite(input.coherent_aperture_seconds)||input.coherent_aperture_seconds<=0||
            input.coherent_aperture_seconds>static_cast<double>(input.end_sample-input.first_sample)/m.sample_rate+1e-12)
            return OscillatorModelReason::invalid_measurement;
        FrequencyApertureProcessMoments moments;
        if(!measurement_moments(input,moments))return OscillatorModelReason::invalid_measurement;
        if(have_) {
            if(m.capture_generation!=mapping_.capture_generation||m.timebase_identity!=mapping_.timebase_identity||m.sample_rate!=mapping_.sample_rate)
                return OscillatorModelReason::timebase_changed;
            if(m.reference_identity!=mapping_.reference_identity)return OscillatorModelReason::reference_changed;
            if(input.first_sample<last_end_)return OscillatorModelReason::overlapping_support;
            if(m.phase_segment<=mapping_.phase_segment)return OscillatorModelReason::reused_phase_segment;
            if(!transition_valid(m))return OscillatorModelReason::invalid_mapping;
        }
        const auto endpoint=input.end_sample-1;
        auto state=state_;auto covariance=covariance_;
        // The initial prior is at the FIRST aperture's START. Advancing to its
        // observed endpoint supplies the very process shared by C and Rprocess.
        const auto prior_sample=have_?sample_:input.first_sample;
        if(!advance(state,covariance,seconds(endpoint,prior_sample,m.sample_rate)))return OscillatorModelReason::numeric_failure;
        auto h=row(m,m.helper_audio_hz);h[2]=-h[0]*moments.mu_seconds;h[3]=-h[1]*moments.mu_seconds;
        const auto noise=observation_noise(h,moments);
        const auto ph=multiply(covariance,h);
        const auto model_variance=dot(h,ph)+2*dot(h,noise.cross)+noise.process_variance;
        const auto observed=m.mono_image_sign*input.frequency_hz-m.helper_audio_hz;
        const auto innovation=observed-dot(h,state);
        auto visits=visits_;auto repeats=repeat_visits_;auto scatter=excess_scatter_estimate_hz2_;
        const auto slot=visit_slot(visits,m.actual_rf_shift_hz,m.sideband_sign);
        if(visits[slot].count&&have_&&m.phase_segment!=mapping_.phase_segment) {
            ++repeats;
            if(config_.learn_repeat_visit_scatter) {
                const auto excess=innovation*innovation-model_variance-input.variance_hz2-config_.retune_variance_floor_hz2;
                const auto weight=1./static_cast<double>(std::min<std::size_t>(repeats,32));
                scatter=std::fma(weight,excess-scatter,scatter);
            }
        }
        ++visits[slot].count;
        const auto retune=config_.retune_variance_floor_hz2+std::max(0.,scatter);
        const NumericAperture newest{h,observed,input.variance_hz2,input.coherent_aperture_seconds,input.first_sample,input.end_sample,moments};
        // Reweight bounded NUMERIC rows when unexplained variance increases;
        // no PCM replay, frequency search, or36-second knowledge expiry.
        const bool drop_oldest=history_count_==history_capacity;
        if(drop_oldest||retune>config_.retune_variance_floor_hz2+std::max(0.,excess_scatter_estimate_hz2_)) {
            state={};covariance=initial_covariance();auto previous=have_?origin_sample_:input.first_sample;
            for(std::size_t n=drop_oldest?1:0;n<history_count_;++n) {
                const auto& saved=history_[(history_head_+n)%history_capacity];
                if(!advance(state,covariance,seconds(saved.end-1,previous,m.sample_rate))||
                    !update(state,covariance,saved.row,saved.observed,saved.variance+retune,observation_noise(saved.row,saved.moments)))
                    return OscillatorModelReason::numeric_failure;
                previous=saved.end-1;
            }
            if(!advance(state,covariance,seconds(endpoint,previous,m.sample_rate))||
                !update(state,covariance,h,observed,input.variance_hz2+retune,noise))return OscillatorModelReason::numeric_failure;
        } else if(!update(state,covariance,h,observed,input.variance_hz2+retune,noise))return OscillatorModelReason::numeric_failure;
        if(!have_)origin_sample_=input.first_sample;
        if(drop_oldest){history_head_=(history_head_+1)%history_capacity;--history_count_;}
        history_[(history_head_+history_count_)%history_capacity]=newest;++history_count_;
        first_=history_[history_head_].first;aperture_=0;aperture_cube_=0;
        for(std::size_t n=0;n<history_count_;++n){const auto t=history_[(history_head_+n)%history_capacity].aperture;aperture_+=t;aperture_cube_+=t*t*t;}
        state_=state;covariance_=covariance;sample_=endpoint;
        last_end_=input.end_sample;mapping_=m;have_=true;visits_=visits;repeat_visits_=repeats;excess_scatter_estimate_hz2_=scatter;++measurements_;
        return OscillatorModelReason::accepted;
    }
    OscillatorFrequencyPrediction predict(const LinkMapping& m,std::uint64_t at_sample)const noexcept {
        OscillatorFrequencyPrediction out;
        if(!valid_){out.reason=OscillatorModelReason::invalid_configuration;return out;}
        if(!have_)return out;
        if(!mapping_valid(m)||at_sample<m.settled_first_sample||at_sample>=m.settled_end_sample){out.reason=OscillatorModelReason::invalid_mapping;return out;}
        if(m.capture_generation!=mapping_.capture_generation||m.timebase_identity!=mapping_.timebase_identity||m.sample_rate!=mapping_.sample_rate){out.reason=OscillatorModelReason::timebase_changed;return out;}
        if(m.reference_identity!=mapping_.reference_identity){out.reason=OscillatorModelReason::reference_changed;return out;}
        if(!transition_valid(m)){out.reason=OscillatorModelReason::invalid_mapping;return out;}
        const auto dt=seconds(at_sample,sample_,m.sample_rate);
        if(dt<0||dt>config_.maximum_prediction_age_seconds){out.reason=OscillatorModelReason::prediction_too_old;return out;}
        auto state=state_;auto covariance=covariance_;
        if(!advance(state,covariance,dt)){out.reason=OscillatorModelReason::numeric_failure;return out;}
        const auto helper=row(m,m.helper_audio_hz),data=row(m,m.data_audio_hz);
        out.helper_residual_hz=dot(helper,state);out.data_residual_hz=dot(data,state);
        out.retune_variance_hz2=config_.retune_variance_floor_hz2+std::max(0.,excess_scatter_estimate_hz2_);
        out.helper_variance_hz2=dot(helper,multiply(covariance,helper))+out.retune_variance_hz2;
        out.data_variance_hz2=dot(data,multiply(covariance,data))+out.retune_variance_hz2;
        out.rf_fraction=state[0]*1e-6;out.rf_fraction_sigma=std::sqrt(std::max(0.,covariance[0][0]))*1e-6;
        out.audio_fraction=state[1]*1e-6;out.audio_fraction_sigma=std::sqrt(std::max(0.,covariance[1][1]))*1e-6;
        out.rf_drift_fraction_per_second=state[2]*1e-6;out.audio_drift_fraction_per_second=state[3]*1e-6;
        if(config_.topology==LinkTopology::shared_radio){out.audio_fraction=out.rf_fraction;out.audio_fraction_sigma=out.rf_fraction_sigma;out.audio_drift_fraction_per_second=out.rf_drift_fraction_per_second;}
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){const auto a=config_.topology==LinkTopology::shared_radio?(i==1?0:i==3?2:i):i;const auto b=config_.topology==LinkTopology::shared_radio?(j==1?0:j==3?2:j):j;out.fractional_covariance[i][j]=covariance[a][b]*1e-12;}
        out.sample=at_sample;out.evidence_first_sample=first_;out.evidence_end_sample=last_end_;out.observation_age_seconds=dt;
        out.usable_aperture_seconds=aperture_;out.aperture_cube_seconds3=aperture_cube_;out.measurements=measurements_;out.retained_measurements=history_count_;out.repeat_visits=repeat_visits_;
        out.valid=std::isfinite(out.helper_residual_hz)&&std::isfinite(out.data_residual_hz)&&out.helper_variance_hz2>=0&&out.data_variance_hz2>=0&&std::isfinite(out.helper_variance_hz2)&&std::isfinite(out.data_variance_hz2);
        out.reason=out.valid?OscillatorModelReason::accepted:OscillatorModelReason::numeric_failure;return out;
    }
    static constexpr std::size_t workspace_bytes()noexcept{return sizeof(OscillatorFrequencyModel);}
private:
    static bool nonnegative(double x)noexcept{return std::isfinite(x)&&x>=0;}
    static double seconds(std::uint64_t a,std::uint64_t b,std::uint32_t rate)noexcept{return (a>=b?static_cast<double>(a-b):-static_cast<double>(b-a))/rate;}
    double q(std::size_t coordinate)const noexcept{return coordinate==0?*config_.rf_acceleration_psd_ppm2_per_second3:config_.topology==LinkTopology::shared_radio?0:*config_.audio_acceleration_psd_ppm2_per_second3;}
    bool measurement_moments(const IndependentFrequencyAperture& in,FrequencyApertureProcessMoments& out)const noexcept {
        const auto rate=in.mapping.sample_rate;const auto endpoint=in.end_sample-1;
        const auto duration=seconds(endpoint,in.first_sample,rate);
        const auto lag=seconds(endpoint,in.sample,rate)-in.sample_fraction/rate;
        if(!(duration>0)||lag<0||lag>duration)return false;
        if(!in.process_moments){if(q(0)>0||q(1)>0)return false;out={true,in.first_sample,in.end_sample,endpoint,1,lag,0,0,0,lag,1};return true;}
        out=*in.process_moments;
        if(!out.valid||out.first_sample!=in.first_sample||out.end_sample!=in.end_sample||out.state_endpoint_sample!=endpoint||
            !nonnegative(out.mass)||!nonnegative(out.mu_seconds)||!nonnegative(out.integral_k_seconds2)||
            !nonnegative(out.integral_b_minus_s_k_seconds3)||!nonnegative(out.integral_k_squared_seconds3)||
            !nonnegative(out.k_at_end_seconds)||!nonnegative(out.k_prime_at_end))return false;
        constexpr auto epsilon=512*std::numeric_limits<double>::epsilon();
        if(std::abs(out.mass-1)>epsilon||std::abs(out.k_prime_at_end-1)>epsilon||std::abs(out.mu_seconds-lag)>epsilon*(1+duration)||
            std::abs(out.k_at_end_seconds-out.mu_seconds)>epsilon*(1+duration)||out.mu_seconds>duration)return false;
        const auto t2=duration*duration,t3=t2*duration;
        const auto i1=out.integral_k_seconds2/t2,i2=out.integral_b_minus_s_k_seconds3/t3,r=out.integral_k_squared_seconds3/t3;
        // Necessary moment bounds and joint process/measurement PSD. Actual
        // weighting-kernel provenance remains the producer's responsibility.
        const auto lower=12*i2*i2-12*i1*i2+4*i1*i1;
        return std::isfinite(lower)&&i1<=.5+epsilon&&i2<=1./6+epsilon&&r<=1./3+epsilon&&r+epsilon>=lower;
    }
    ObservationNoise observation_noise(const Vector& h,const FrequencyApertureProcessMoments& k)const noexcept {
        ObservationNoise out;
        for(std::size_t i=0;i<2;++i){const auto density=q(i);out.cross[i]=h[i]*density*k.integral_b_minus_s_k_seconds3;out.cross[i+2]=h[i]*density*k.integral_k_seconds2;out.process_variance+=h[i]*h[i]*density*k.integral_k_squared_seconds3;}return out;
    }
    bool transition_valid(const LinkMapping& m)const noexcept {
        const bool same=m.phase_segment==mapping_.phase_segment&&m.actual_rf_shift_hz==mapping_.actual_rf_shift_hz&&m.helper_audio_hz==mapping_.helper_audio_hz&&m.data_audio_hz==mapping_.data_audio_hz&&m.sideband_sign==mapping_.sideband_sign&&m.mono_image_sign==mapping_.mono_image_sign;
        return same||(m.phase_segment>mapping_.phase_segment&&m.settled_first_sample>=mapping_.settled_end_sample&&(m.provenance==LinkMappingProvenance::modelled_physical_capture||m.provenance==LinkMappingProvenance::confirmed_adapter));
    }
    bool mapping_valid(const LinkMapping& m)const noexcept {
        const auto rf=m.sideband_sign*m.actual_rf_shift_hz;
        const bool nonzero=config_.topology==LinkTopology::shared_radio?std::isfinite(rf+m.helper_audio_hz)&&rf+m.helper_audio_hz!=0&&std::isfinite(rf+m.data_audio_hz)&&rf+m.data_audio_hz!=0:(rf!=0||m.helper_audio_hz!=0)&&(rf!=0||m.data_audio_hz!=0);
        return nonzero&&!(config_.topology==LinkTopology::shared_radio&&m.actual_rf_shift_hz==0)&&m.reference_identity_known&&m.sample_rate&&m.settled_first_sample<m.settled_end_sample&&std::isfinite(m.actual_rf_shift_hz)&&m.actual_rf_shift_hz>=0&&std::isfinite(m.helper_audio_hz)&&std::isfinite(m.data_audio_hz)&&(m.sideband_sign==1||m.sideband_sign==-1)&&(m.mono_image_sign==1||m.mono_image_sign==-1)&&(m.provenance==LinkMappingProvenance::fixed_configuration||m.provenance==LinkMappingProvenance::modelled_physical_capture||m.provenance==LinkMappingProvenance::confirmed_adapter);
    }
    Vector row(const LinkMapping& m,double audio)const noexcept {const auto rf=m.sideband_sign*m.actual_rf_shift_hz;return config_.topology==LinkTopology::shared_radio?Vector{(rf+audio)*1e-6,0,0,0}:Vector{rf*1e-6,audio*1e-6,0,0};}
    static double dot(const Vector& a,const Vector& b)noexcept{double sum=0;for(std::size_t i=0;i<4;++i)sum=std::fma(a[i],b[i],sum);return sum;}
    static Vector multiply(const Matrix& a,const Vector& b)noexcept{Vector out{};for(std::size_t i=0;i<4;++i)out[i]=dot(a[i],b);return out;}
    static bool finite(const Vector& x,const Matrix& p)noexcept{for(std::size_t i=0;i<4;++i){if(!std::isfinite(x[i])||!std::isfinite(p[i][i])||p[i][i]<0)return false;for(double v:p[i])if(!std::isfinite(v))return false;}return true;}
    Matrix initial_covariance()const noexcept {Matrix out{};const Vector sigma{config_.rf_prior_sigma_ppm,config_.topology==LinkTopology::shared_radio?0:config_.audio_prior_sigma_ppm,config_.rf_drift_prior_sigma_ppm_per_second,config_.topology==LinkTopology::shared_radio?0:config_.audio_drift_prior_sigma_ppm_per_second};for(std::size_t i=0;i<4;++i)out[i][i]=sigma[i]*sigma[i];return out;}
    static bool update(Vector& state,Matrix& covariance,const Vector& h,double observed,double independent_variance,const ObservationNoise& noise)noexcept {
        const auto ph=multiply(covariance,h);
        const auto variance=dot(h,ph)+2*dot(h,noise.cross)+independent_variance+noise.process_variance;
        const auto innovation=observed-dot(h,state);
        if(!(variance>0)||!std::isfinite(variance))return false;
        Vector gain{};for(std::size_t i=0;i<4;++i){gain[i]=(ph[i]+noise.cross[i])/variance;state[i]=std::fma(gain[i],innovation,state[i]);}
        Matrix a{},next{};Vector ac{};
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){a[i][j]=(i==j?1.:0.)-gain[i]*h[j];ac[i]+=a[i][j]*noise.cross[j];}
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){for(std::size_t k=0;k<4;++k)for(std::size_t l=0;l<4;++l)next[i][j]+=a[i][k]*covariance[k][l]*a[j][l];next[i][j]+=gain[i]*(independent_variance+noise.process_variance)*gain[j]-ac[i]*gain[j]-gain[i]*ac[j];}
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)covariance[i][j]=.5*(next[i][j]+next[j][i]);return finite(state,covariance);
    }
    bool advance(Vector& x,Matrix& p,double dt)const noexcept {
        if(!nonnegative(dt))return false;
        Matrix f{};for(std::size_t i=0;i<4;++i)f[i][i]=1;f[0][2]=dt;f[1][3]=dt;
        Matrix next{};for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)for(std::size_t k=0;k<4;++k)for(std::size_t l=0;l<4;++l)next[i][j]+=f[i][k]*p[k][l]*f[j][l];
        for(std::size_t i=0;i<2;++i){const auto density=q(i);next[i][i]+=density*dt*dt*dt/3;next[i][i+2]+=density*dt*dt/2;next[i+2][i]+=density*dt*dt/2;next[i+2][i+2]+=density*dt;}
        x=multiply(f,x);p=next;return finite(x,p);
    }
    static std::size_t visit_slot(std::array<Visit,16>& visits,double rf,int sideband)noexcept {
        for(std::size_t i=0;i<visits.size();++i)if(visits[i].count&&visits[i].rf==rf&&visits[i].sideband==sideband)return i;
        for(std::size_t i=0;i<visits.size();++i)if(!visits[i].count){visits[i]={rf,sideband,0};return i;}
        auto at=std::min_element(visits.begin(),visits.end(),[](const auto& a,const auto& b){return a.count<b.count;});const auto i=static_cast<std::size_t>(at-visits.begin());visits[i]={rf,sideband,0};return i;
    }
    OscillatorFrequencyModelConfig config_{};Vector state_{};Matrix covariance_{};LinkMapping mapping_{};std::array<Visit,16> visits_{};
    std::uint64_t sample_=0,first_=0,last_end_=0,origin_sample_=0;double aperture_=0,aperture_cube_=0,excess_scatter_estimate_hz2_=0;
    std::array<NumericAperture,history_capacity> history_{};std::size_t history_head_=0,history_count_=0,measurements_=0,repeat_visits_=0;bool valid_=false,have_=false;
};
}
