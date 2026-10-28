#pragma once
#include "helper_oscillator_model.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::live::detail {
// Actual retunes only. The audio coordinate is the EFFECTIVE common Helper /
// DATA source B=(1+a)(1+c)-1; it is not a measurement of physical a or UTC c.
// RF is capture-effective (may include RF*ADC products), not physical RF ppm.
// Ordinary masks retain their original B/UTC union and H-D coupling.
struct HelperOscillatorConfig {
    helper::OscillatorFrequencyModelConfig model;
    double maximum_actual_rf_hz=0,innovation_sigma_limit=6,local_slew_bound_hz_per_second=.5;
    bool common_effective_source=false;
};
inline HelperOscillatorConfig declared_hf_oscillator_tracking(double capture_rf_sigma_ppm,
        double effective_source_sigma_ppm,double retune_variance_hz2)noexcept {
    HelperOscillatorConfig c;c.common_effective_source=true;c.maximum_actual_rf_hz=54e6;
    c.model.topology=helper::LinkTopology::independent_audio;
    // Explicit operational Gaussian assumptions, NOT static accuracy / 3 or
    // an inference from Wiener phase noise. Hardware calibration is unclaimed.
    c.model.rf_prior_sigma_ppm=capture_rf_sigma_ppm;c.model.audio_prior_sigma_ppm=effective_source_sigma_ppm;
    // An explicitly EXACT original coordinate has no hidden drift domain.
    // The caller supplies expanded B, so LF=0 with nonzero UTC still uses Q_B.
    c.model.rf_drift_prior_sigma_ppm_per_second=capture_rf_sigma_ppm==0?0:.01;
    c.model.audio_drift_prior_sigma_ppm_per_second=effective_source_sigma_ppm==0?0:.1;
    c.model.rf_acceleration_psd_ppm2_per_second3=capture_rf_sigma_ppm==0?0:helper::operational_hf_rf_acceleration_psd_ppm2_per_second3;
    c.model.audio_acceleration_psd_ppm2_per_second3=effective_source_sigma_ppm==0?0:helper::operational_hf_audio_acceleration_psd_ppm2_per_second3;
    c.model.maximum_prediction_age_seconds=3;c.model.retune_variance_floor_hz2=retune_variance_hz2;
    c.model.learn_repeat_visit_scatter=true;return c;
}
struct HelperOscillatorProvider {std::uint64_t identity=0;double phase_noise_radians_per_sqrt_second=0;};
struct HelperOscillatorSeed {
    helper::LinkMapping mapping;
    helper::OscillatorFrequencyPrediction state;
    std::uint64_t epoch=0,raw_origin_generation=0,actual_first_sample=0,pilot_first_sample=0,pilot_end_sample=0;
};
struct HelperOscillatorQuery {
    // valid is conditional frequency mathematics; available also needs captured
    // input and a recent independent FREQUENCY witness. A confirmed retune does
    // not erase that witness or renew its date. Neither grants phase/time.
    bool valid=false,available=false,direct_aperture=false;
    // Direct Helper apertures carry no joint DATA projection. Consumers also
    // require available before using this independently explicit certificate.
    bool data_frequency_valid=false;
    double helper_residual_hz=0,helper_uncertainty_hz=0;
    double data_residual_hz=0,data_uncertainty_hz=0;
    helper::OscillatorFrequencyPrediction state;
};
struct HelperOscillatorUpdate {
    bool accepted=false,duplicate=false,innovation_veto=false;
    helper::OscillatorModelReason reason=helper::OscillatorModelReason::no_observation;
    double innovation_hz=0,innovation_variance_hz2=0,independent_variance_hz2=0,residual_variance_hz2=0;
    HelperOscillatorSeed seed;
};
class LiveHelperOscillator {
public:
    static constexpr std::size_t scratch_allowance=65536;
    // Same short coast as the pilot's existing presence age, dated from a
    // frequency aperture rather than a phase update or retune notification.
    // The longer model history/maximum age remains available numerically.
    static constexpr double frequency_witness_coast_seconds=.5;
    static constexpr std::size_t storage_required()noexcept{return sizeof(LiveHelperOscillator);}
    bool configure(const HelperOscillatorConfig& c,HelperOscillatorProvider p,std::size_t grant)noexcept {
        if(configured_||grant<sizeof(*this)||!provider_valid(p)||!c.common_effective_source||
           !std::isfinite(c.maximum_actual_rf_hz)||c.maximum_actual_rf_hz<=0||
           !std::isfinite(c.innovation_sigma_limit)||c.innovation_sigma_limit<=0||
           !std::isfinite(c.local_slew_bound_hz_per_second)||c.local_slew_bound_hz_per_second<0||
           c.model.topology!=helper::LinkTopology::independent_audio||!model_.reset(c.model))return false;
        config_=c;provider_=p;configured_=true;epoch_=raw_origin_generation_=1;return true;
    }
    // Explicit raw-provider discontinuity: no payload-start/reset entry exists.
    bool reset_provider(HelperOscillatorProvider p)noexcept {
        if(!configured_||!provider_valid(p)||!model_.reset(config_.model))return false;
        provider_=p;have_segment_=domain_blocked_=false;closed_=true;seed_={};last_state_={};last_observation_={};++epoch_;++raw_origin_generation_;return true;
    }
    void close_reference()noexcept{closed_=true;}
    const HelperOscillatorConfig& assumptions()const noexcept{return config_;}
    const HelperOscillatorSeed& seed()const noexcept{return seed_;}
    bool begin_segment(std::uint64_t actual_first,const helper::LinkMapping& m,HelperOscillatorProvider p,
            std::uint64_t captured_end)noexcept {
        if(!configured_||domain_blocked_||!same_provider(p)||!mapping_valid(m)||actual_first!=captured_end||actual_first>m.settled_first_sample)return block();
        if(have_segment_&&(m.capture_generation!=seed_.mapping.capture_generation||m.timebase_identity!=seed_.mapping.timebase_identity||
            m.reference_identity!=seed_.mapping.reference_identity||m.sample_rate!=seed_.mapping.sample_rate))return block();
        if(have_segment_&&(m.phase_segment<=seed_.mapping.phase_segment||actual_first!=seed_.mapping.settled_end_sample))return block();
        HelperOscillatorSeed next;next.mapping=m;next.epoch=epoch_;next.raw_origin_generation=raw_origin_generation_;next.actual_first_sample=actual_first;
        next.state=model_.predict(m,m.settled_first_sample);
        if(next.state.valid&&next.state.evidence_end_sample>actual_first)return block();
        seed_=next;have_segment_=true;last_observation_={};return true;
    }
    HelperOscillatorUpdate observe(const helper::ToneFrequencyObservation& o,HelperOscillatorProvider p,
            std::uint64_t captured_end)noexcept {
        HelperOscillatorUpdate out;out.seed=seed_;
        if(configured_&&!same_provider(p)){block();return out;}
        if(!configured_||domain_blocked_||!have_segment_||!raw_valid(o,seed_.mapping,captured_end))return out;
        out.independent_variance_hz2=o.independent_variance_hz2;out.residual_variance_hz2=o.residual_variance_hz2;
        if(last_observation_.valid){out.duplicate=o.end_sample==last_observation_.end_sample;out.reason=helper::OscillatorModelReason::reused_phase_segment;return out;}
        helper::IndependentFrequencyAperture a{seed_.mapping,o.first_sample,o.end_sample,o.mean_sample,o.mean_sample_fraction,o.frequency_hz,
            o.independent_variance_hz2+o.residual_variance_hz2,static_cast<double>(o.end_sample-o.first_sample)/seed_.mapping.sample_rate,
            helper::FrequencyApertureProcessMoments{o.valid,o.first_sample,o.end_sample,o.state_endpoint_sample,o.mass,o.mu_seconds,
                o.integral_k_seconds2,o.integral_b_minus_s_k_seconds3,o.integral_k_squared_seconds3,o.k_at_end_seconds,o.k_prime_at_end}};
        // One conventional innovation check. The same declared floor remains
        // after a reset; IID PLL frequency errors are never fitted as drift.
        if(last_state_.valid&&o.state_endpoint_sample>=last_state_.sample){
            const auto prior=advance(last_state_,o.state_endpoint_sample,seed_.mapping.sample_rate);
            const auto rf=seed_.mapping.sideband_sign*seed_.mapping.actual_rf_shift_hz,h=seed_.mapping.helper_audio_hz,mu=o.mu_seconds;
            const std::array<double,4> row{rf,h,-rf*mu,-h*mu};
            const std::array<double,4> x{prior.rf_fraction,prior.audio_fraction,prior.rf_drift_fraction_per_second,prior.audio_drift_fraction_per_second};
            std::array<double,4> cross{};const auto qr=*config_.model.rf_acceleration_psd_ppm2_per_second3*1e-12,qa=*config_.model.audio_acceleration_psd_ppm2_per_second3*1e-12;
            cross={rf*qr*o.integral_b_minus_s_k_seconds3,h*qa*o.integral_b_minus_s_k_seconds3,rf*qr*o.integral_k_seconds2,h*qa*o.integral_k_seconds2};
            double mean=0,variance=a.variance_hz2+prior.retune_variance_hz2+(rf*rf*qr+h*h*qa)*o.integral_k_squared_seconds3;
            for(std::size_t i=0;i<4;++i){mean+=row[i]*x[i];variance+=2*row[i]*cross[i];for(std::size_t j=0;j<4;++j)variance+=row[i]*prior.fractional_covariance[i][j]*row[j];}
            out.innovation_hz=seed_.mapping.mono_image_sign*o.frequency_hz-h-mean;out.innovation_variance_hz2=variance;
            if(!(variance>0)||!std::isfinite(variance))return out;
            if(std::abs(out.innovation_hz)>config_.innovation_sigma_limit*std::sqrt(variance)){
                out.innovation_veto=true;++epoch_;seed_.epoch=epoch_;seed_.state={};last_state_={};closed_=true;
                // Adaptive innovation scatter must retain its measured noise
                // history. A fixed-noise laboratory configuration may restart;
                // both paths invalidate the contradicted prior seed.
                if(!config_.model.learn_repeat_visit_scatter && !model_.reset(config_.model))return out;
            }
        }
        out.reason=model_.observe(a,captured_end);out.accepted=out.reason==helper::OscillatorModelReason::accepted;
        if(out.accepted){last_state_=model_.predict(seed_.mapping,o.state_endpoint_sample);last_observation_=o;
            // Reweighting history changes the old prior covariance too. Do not
            // reuse its smaller R on staged input; direct observed support stays.
            if(seed_.state.valid && last_state_.retune_variance_hz2>seed_.state.retune_variance_hz2){
                ++epoch_;seed_.epoch=epoch_;seed_.state={};
            }
            seed_.pilot_first_sample=o.first_sample;seed_.pilot_end_sample=o.end_sample;closed_=false;}
        out.seed=seed_;return out;
    }
    HelperOscillatorQuery query(const HelperOscillatorSeed& saved,std::uint64_t at,std::uint64_t captured_end,
            double data_audio_hz)const noexcept {
        HelperOscillatorQuery out;
        if(!configured_||domain_blocked_||saved.epoch!=epoch_||!saved.state.valid||!same_domain(saved.mapping)||
           at<saved.state.sample||at<saved.mapping.settled_first_sample||at>=saved.mapping.settled_end_sample)return out;
        auto state=advance(saved.state,at,saved.mapping.sample_rate);
        if(state.observation_age_seconds>config_.model.maximum_prediction_age_seconds)return out;
        out=project(state,saved.mapping,data_audio_hz);
        // Later loss or the next retune's bootstrap cannot revoke the old
        // hop's already observed frequency support while bounded PCM drains.
        // A closed current reference grants no extrapolation past that dated
        // pilot. This transports frequency only; phase is reacquired per hop.
        out.available=out.valid&&frequency_witness(state,captured_end,saved.mapping.sample_rate)&&at<captured_end&&
            (!closed_||(saved.pilot_end_sample&&at<saved.pilot_end_sample));return out;
    }
    HelperOscillatorQuery current(std::uint64_t at,std::uint64_t captured_end,double data_audio_hz)const noexcept {
        HelperOscillatorQuery out;if(!configured_||domain_blocked_||!have_segment_)return out;
        const auto state=model_.predict(seed_.mapping,at);if(!state.valid)return out;
        out=project(state,seed_.mapping,data_audio_hz);
        out.available=out.valid&&!closed_&&frequency_witness(state,captured_end,seed_.mapping.sample_rate)&&at<captured_end;return out;
    }
    // A current full fit can support its OBSERVED aperture without backward
    // prediction. Keep deterministic slew as a separate additive interval.
    HelperOscillatorQuery direct(const helper::ToneFrequencyObservation& o,const HelperOscillatorSeed& saved,
            std::uint64_t at,std::uint64_t captured_end)const noexcept {
        const auto& m=saved.mapping;
        // A measured aperture is immutable dated evidence. Current presence
        // gates current()/future prediction, not correction of those captured
        // samples. Model reweighting/veto invalidates prior predictions, not a
        // measured aperture; provider reset still revokes its origin even if
        // mapping identifiers are reused. Every query stays inside support.
        HelperOscillatorQuery out;if(!configured_||domain_blocked_||saved.raw_origin_generation!=raw_origin_generation_||saved.pilot_end_sample!=o.end_sample||!same_domain(m)||!raw_valid(o,m,captured_end)||at<o.first_sample||at>=o.end_sample)return out;
        const auto distance=(at>=o.mean_sample?static_cast<double>(at-o.mean_sample):-static_cast<double>(o.mean_sample-at))-o.mean_sample_fraction;
        out.helper_residual_hz=m.mono_image_sign*o.frequency_hz-m.helper_audio_hz;
        out.helper_uncertainty_hz=3*std::sqrt(o.independent_variance_hz2+o.residual_variance_hz2)+config_.local_slew_bound_hz_per_second*std::abs(distance)/m.sample_rate;
        out.valid=out.available=std::isfinite(out.helper_uncertainty_hz);out.direct_aperture=true;return out;
    }
private:
    static bool frequency_witness(const helper::OscillatorFrequencyPrediction& state,
            std::uint64_t captured_end,std::uint32_t rate)noexcept {
        return state.evidence_end_sample && state.evidence_end_sample<=captured_end&&
            std::isfinite(state.observation_age_seconds)&&state.observation_age_seconds>=0&&
            state.sample>=state.evidence_end_sample-1&&
            state.sample-(state.evidence_end_sample-1)<=
                static_cast<std::uint64_t>(rate*frequency_witness_coast_seconds);
    }
    static bool provider_valid(HelperOscillatorProvider p)noexcept{return p.identity&&std::isfinite(p.phase_noise_radians_per_sqrt_second)&&p.phase_noise_radians_per_sqrt_second>=0;}
    bool same_provider(HelperOscillatorProvider p)const noexcept{return p.identity==provider_.identity&&p.phase_noise_radians_per_sqrt_second==provider_.phase_noise_radians_per_sqrt_second;}
    bool block()noexcept{domain_blocked_=closed_=true;return false;}
    bool mapping_valid(const helper::LinkMapping& m)const noexcept{return m.reference_identity_known&&m.sample_rate&&m.actual_rf_shift_hz>0&&m.actual_rf_shift_hz<=config_.maximum_actual_rf_hz&&m.settled_first_sample<m.settled_end_sample&&m.mono_image_sign==1&&(m.sideband_sign==1||m.sideband_sign==-1)&&std::isfinite(m.helper_audio_hz)&&std::isfinite(m.data_audio_hz)&&(m.provenance==helper::LinkMappingProvenance::confirmed_adapter||m.provenance==helper::LinkMappingProvenance::modelled_physical_capture);}
    bool same_domain(const helper::LinkMapping& m)const noexcept{return have_segment_&&m.capture_generation==seed_.mapping.capture_generation&&m.timebase_identity==seed_.mapping.timebase_identity&&m.reference_identity_known&&m.reference_identity==seed_.mapping.reference_identity&&m.sample_rate==seed_.mapping.sample_rate&&mapping_valid(m);}
    static bool raw_valid(const helper::ToneFrequencyObservation& o,const helper::LinkMapping& m,std::uint64_t end)noexcept{return o.valid&&o.first_sample>=m.settled_first_sample&&o.first_sample<o.end_sample&&o.end_sample<=m.settled_end_sample&&o.end_sample<=end&&o.state_endpoint_sample==o.end_sample-1&&o.mean_sample>=o.first_sample&&o.mean_sample<o.end_sample&&std::isfinite(o.mean_sample_fraction)&&o.mean_sample_fraction>=0&&o.mean_sample_fraction<1&&std::isfinite(o.frequency_hz)&&std::isfinite(o.independent_variance_hz2)&&o.independent_variance_hz2>0&&std::isfinite(o.residual_variance_hz2)&&o.residual_variance_hz2>=0;}
    helper::OscillatorFrequencyPrediction advance(helper::OscillatorFrequencyPrediction p,std::uint64_t at,std::uint32_t rate)const noexcept {
        if(at<p.sample){p.valid=false;return p;}const auto dt=static_cast<double>(at-p.sample)/rate;
        p.rf_fraction+=dt*p.rf_drift_fraction_per_second;p.audio_fraction+=dt*p.audio_drift_fraction_per_second;
        auto old=p.fractional_covariance;
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){p.fractional_covariance[i][j]=old[i][j];if(i<2)p.fractional_covariance[i][j]+=dt*old[i+2][j];if(j<2)p.fractional_covariance[i][j]+=dt*old[i][j+2];if(i<2&&j<2)p.fractional_covariance[i][j]+=dt*dt*old[i+2][j+2];}
        for(std::size_t i=0;i<2;++i){const auto q=(i?*config_.model.audio_acceleration_psd_ppm2_per_second3:*config_.model.rf_acceleration_psd_ppm2_per_second3)*1e-12;p.fractional_covariance[i][i]+=q*dt*dt*dt/3;p.fractional_covariance[i][i+2]+=q*dt*dt/2;p.fractional_covariance[i+2][i]+=q*dt*dt/2;p.fractional_covariance[i+2][i+2]+=q*dt;}
        p.sample=at;p.observation_age_seconds+=dt;return p;
    }
    static HelperOscillatorQuery project(const helper::OscillatorFrequencyPrediction& p,const helper::LinkMapping& m,double data)noexcept {
        HelperOscillatorQuery out;out.state=p;const auto rf=m.sideband_sign*m.actual_rf_shift_hz;
        const auto variance=[&](double h){return rf*rf*p.fractional_covariance[0][0]+2*rf*h*p.fractional_covariance[0][1]+h*h*p.fractional_covariance[1][1]+p.retune_variance_hz2;};
        out.helper_residual_hz=rf*p.rf_fraction+m.helper_audio_hz*p.audio_fraction;out.data_residual_hz=rf*p.rf_fraction+data*p.audio_fraction;
        const auto vh=variance(m.helper_audio_hz),vd=variance(data);if(!std::isfinite(vh)||!std::isfinite(vd)||vh<0||vd<0)return out;
        out.helper_uncertainty_hz=3*std::sqrt(vh);out.data_uncertainty_hz=3*std::sqrt(vd);
        out.valid=p.valid&&std::isfinite(data)&&std::isfinite(out.helper_residual_hz)&&std::isfinite(out.data_residual_hz)&&std::isfinite(out.helper_uncertainty_hz)&&std::isfinite(out.data_uncertainty_hz);
        out.data_frequency_valid=out.valid;return out;
    }
    HelperOscillatorConfig config_;HelperOscillatorProvider provider_;helper::OscillatorFrequencyModel model_;
    HelperOscillatorSeed seed_;helper::OscillatorFrequencyPrediction last_state_;helper::ToneFrequencyObservation last_observation_;
    std::uint64_t epoch_=0,raw_origin_generation_=0;bool configured_=false,have_segment_=false,closed_=true,domain_blocked_=false;
};
}
