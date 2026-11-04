#pragma once

#include "helper_oscillator_model.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::live::detail {

// Conditional numeric model for *completed* high/low pilot apertures. This
// owns no PCM, phase, timing or receiver admission authority. In particular,
// it must not be combined as an independent estimate with a high-only model
// that consumed the same high samples. The caller must establish the marginal
// variance, temporal independence and selected-tone assumptions before using
// any of its conditional predictions for receiver decisions. Digital geometry
// checks here do not certify the actual radio passband or analog filtering.
class HelperPairModel {
public:
    struct ClockDomain {
        // Effective relative source-clock fraction, including all LF/UTC
        // alternatives and the supported evolution during this aperture.
        double lower_fraction=0,upper_fraction=0;
    };
    struct Config {
        helper::OscillatorFrequencyModelConfig model;
        double maximum_actual_rf_hz=0;
        double maximum_witness_age_seconds=0;
    };
    struct Observation {
        helper::LinkMapping mapping;
        helper::ToneFrequencyObservation high,low;
        double low_audio_hz=550;
        ClockDomain clock_domain;
        // A clear or source replacement revokes queued work even when a
        // capture's externally supplied identifiers are reused.
        std::uint64_t clear_generation=0;
    };
    enum class Reason {
        accepted,not_configured,invalid_mapping,invalid_measurement,
        unsupported_segment,clock_domain_inconsistent,stale_generation,
        nonmonotone_activation,model_declined
    };
    struct Update {
        Reason reason=Reason::not_configured;
        helper::OscillatorModelReason model_reason=helper::OscillatorModelReason::no_observation;
        // A pair update is always conditional; this is not an acquisition or
        // phase certificate. On failure the caller retains its high-only path.
        bool pair_used=false;
        double measured_audio_fraction=0,three_sigma_audio_fraction=0;
        std::uint64_t evidence_first_sample=0,evidence_end_sample=0,activation_frontier=0;
    };
    struct Prediction {
        bool conditional_valid=false;
        helper::OscillatorFrequencyPrediction state;
        std::uint64_t activation_frontier=0;
    };

    // The separate stack allowance must be funded as well as the retained
    // numeric object. Its 64 KiB value covers the measured GCC reset frame on
    // the development host; other compilers/platforms still require checking.
    static constexpr std::size_t worker_stack_allowance=65536;
    static constexpr std::size_t storage_required() noexcept { return sizeof(HelperPairModel); }
    static constexpr std::size_t reservation_bytes() noexcept { return storage_required()+worker_stack_allowance; }

    bool configure(const Config& config,std::uint64_t clear_generation,std::size_t grant) noexcept {
        if(configured_||grant<reservation_bytes()||
           config.model.topology!=helper::LinkTopology::independent_audio||
           !std::isfinite(config.maximum_actual_rf_hz)||config.maximum_actual_rf_hz<=0||config.maximum_actual_rf_hz>54e6||
           !std::isfinite(config.maximum_witness_age_seconds)||config.maximum_witness_age_seconds<0||
           !model_.reset(config.model))return false;
        config_=config;clear_generation_=clear_generation;configured_=true;closed_=true;
        return true;
    }

    // Owner serializes reset/observe/publish and has already reserved the
    // model plus stack allowance. reset() on the underlying numeric model
    // uses a large temporary; it must not run in an unbudgeted capture path.
    bool reset_generation(std::uint64_t clear_generation,std::size_t grant) noexcept {
        if(!configured_||clear_generation<=clear_generation_||grant<reservation_bytes())return false;
        if(!model_.reset(config_.model)){configured_=false;closed_=true;return false;}
        clear_generation_=clear_generation;last_segment_=last_end_=activation_frontier_=0;
        have_=false;closed_=true;return true;
    }

    // captured_end is the actual frontier at which this asynchronous result
    // is committed, not its old aperture endpoint. It is also the earliest
    // point at which a later sample could use the result.
    Update observe_completed(const Observation& in,std::uint64_t captured_end) noexcept {
        Update out;
        if(!configured_)return out;
        if(in.clear_generation!=clear_generation_){out.reason=Reason::stale_generation;return out;}
        const auto& m=in.mapping;const auto& h=in.high;const auto& l=in.low;
        if(!mapping_valid(m)){out.reason=Reason::invalid_mapping;return out;}
        // One physical retune contributes at most one pair. Treating multiple
        // windows as independent would average the same retune nuisance down.
        if(have_ && m.phase_segment<=last_segment_){out.reason=Reason::unsupported_segment;return out;}
        if(captured_end<h.end_sample || (have_ && captured_end<activation_frontier_)) {
            out.reason=Reason::nonmonotone_activation;return out;
        }
        if(!raw_valid(h,m,captured_end)||!raw_valid(l,m,captured_end)||!same_kernel(h,l)||
           in.low_audio_hz!=550||in.low_audio_hz>=m.helper_audio_hz||
           !std::isfinite(in.clock_domain.lower_fraction)||!std::isfinite(in.clock_domain.upper_fraction)||
           in.clock_domain.lower_fraction>in.clock_domain.upper_fraction||
           in.clock_domain.lower_fraction<=-1||in.clock_domain.upper_fraction>=1) {
            out.reason=Reason::invalid_measurement;return out;
        }
        const double vh=h.independent_variance_hz2+h.residual_variance_hz2;
        const double vl=l.independent_variance_hz2+l.residual_variance_hz2;
        const double span=m.helper_audio_hz-in.low_audio_hz;
        const double difference=m.mono_image_sign*(h.frequency_hz-l.frequency_hz)-span;
        const double scale=difference/span;
        // For any same-window covariance whose marginal variances are bounded
        // by vh,vl, 2*diag(vh,vl) dominates the full 2x2 covariance. This does
        // not bound temporal correlation, selection bias, or invalid marginals.
        const double uncertainty=3*std::sqrt(2*(vh+vl))/span;
        if(!std::isfinite(vh)||!std::isfinite(vl)||!(vh>0)||!(vl>0)||
           !std::isfinite(scale)||!std::isfinite(uncertainty)||!std::isfinite(2*vh)||!std::isfinite(2*vl)) {
            out.reason=Reason::invalid_measurement;return out;
        }
        // Both local fit and its ±200 Hz noise probes must remain inside the
        // digitally observed positive-frequency band, with 20 Hz margin.
        // This is necessary capture geometry, not an SSB passband certificate.
        const auto captured=[&](const helper::ToneFrequencyObservation& o,double variance) {
            const auto margin=200.+3*std::sqrt(2*variance);
            return std::isfinite(margin)&&o.frequency_hz-margin>=20 &&
                o.frequency_hz+margin<=m.sample_rate/2.-20;
        };
        if(!captured(h,vh)||!captured(l,vl)){out.reason=Reason::invalid_measurement;return out;}
        out.measured_audio_fraction=scale;out.three_sigma_audio_fraction=uncertainty;
        if(scale+uncertainty<in.clock_domain.lower_fraction || scale-uncertainty>in.clock_domain.upper_fraction) {
            out.reason=Reason::clock_domain_inconsistent;return out;
        }
        auto high=aperture(m,h,2*vh);
        auto low_mapping=m;low_mapping.helper_audio_hz=in.low_audio_hz;
        auto low=aperture(low_mapping,l,2*vl);
        out.model_reason=model_.observe_pair(high,low,captured_end);
        if(out.model_reason!=helper::OscillatorModelReason::accepted){out.reason=Reason::model_declined;return out;}
        have_=true;closed_=false;last_segment_=m.phase_segment;last_end_=h.end_sample;
        activation_frontier_=captured_end;
        out.reason=Reason::accepted;out.pair_used=true;out.evidence_first_sample=h.first_sample;
        out.evidence_end_sample=h.end_sample;out.activation_frontier=captured_end;
        return out;
    }

    // During asynchronous model computation capture can advance beyond the
    // frontier sampled before the work started. Before publishing a result,
    // the owner must serialize with capture and advance this to the current
    // actual captured frontier. It may never be moved backward. No caller may
    // expose a prediction between computation and that final publication.
    bool advance_activation_frontier(std::uint64_t captured_end) noexcept {
        if(!configured_||!have_||closed_||captured_end<activation_frontier_)return false;
        activation_frontier_=captured_end;return true;
    }

    // Conditional frequency prediction only. Later information cannot grant
    // earlier phase or backdate the processed frontier. close() suppresses
    // new predictions on Helper loss; the ordinary receiver owns draining.
    Prediction predict(const helper::LinkMapping& current,std::uint64_t at,
            std::uint64_t captured_end,std::uint64_t clear_generation) const noexcept {
        Prediction out;
        if(!configured_||!have_||closed_||clear_generation!=clear_generation_||
           !mapping_valid(current)||at<activation_frontier_||at>=captured_end||at<last_end_-1)return out;
        const auto age=static_cast<double>(at-(last_end_-1))/current.sample_rate;
        if(!std::isfinite(age)||age>config_.maximum_witness_age_seconds)return out;
        out.state=model_.predict(current,at);
        out.conditional_valid=out.state.valid;
        out.activation_frontier=activation_frontier_;
        return out;
    }
    // Numeric model noise parameter only, not evidence or an authority flag.
    // Its changes can invalidate previously retained policy certificates.
    double retune_variance_hz2() const noexcept { return model_.current_retune_variance_hz2(); }
    void close() noexcept { closed_=true; }

private:
    bool mapping_valid(const helper::LinkMapping& m) const noexcept {
        return m.reference_identity_known && m.sample_rate && m.phase_segment &&
            m.settled_first_sample<m.settled_end_sample &&
            std::isfinite(m.actual_rf_shift_hz) && m.actual_rf_shift_hz>0 &&
            m.actual_rf_shift_hz<=config_.maximum_actual_rf_hz &&
            std::isfinite(m.helper_audio_hz) && std::isfinite(m.data_audio_hz) &&
            m.helper_audio_hz>=2400 && (m.sideband_sign==1||m.sideband_sign==-1) &&
            m.mono_image_sign==1 &&
            (m.provenance==helper::LinkMappingProvenance::modelled_physical_capture||
             m.provenance==helper::LinkMappingProvenance::confirmed_adapter);
    }
    static bool raw_valid(const helper::ToneFrequencyObservation& o,const helper::LinkMapping& m,
            std::uint64_t captured_end) noexcept {
        return o.valid && o.first_sample>=m.settled_first_sample && o.first_sample<o.end_sample &&
            o.end_sample<=m.settled_end_sample && o.end_sample<=captured_end &&
            o.state_endpoint_sample==o.end_sample-1 && o.mean_sample>=o.first_sample &&
            o.mean_sample<o.end_sample && std::isfinite(o.mean_sample_fraction) &&
            o.mean_sample_fraction>=0 && o.mean_sample_fraction<1 && std::isfinite(o.frequency_hz) &&
            std::isfinite(o.independent_variance_hz2) && o.independent_variance_hz2>0 &&
            std::isfinite(o.residual_variance_hz2) && o.residual_variance_hz2>=0;
    }
    static bool same_kernel(const helper::ToneFrequencyObservation& a,
            const helper::ToneFrequencyObservation& b) noexcept {
        return a.first_sample==b.first_sample && a.end_sample==b.end_sample &&
            a.state_endpoint_sample==b.state_endpoint_sample && a.mean_sample==b.mean_sample &&
            a.mean_sample_fraction==b.mean_sample_fraction && a.mass==b.mass && a.mu_seconds==b.mu_seconds &&
            a.integral_k_seconds2==b.integral_k_seconds2 &&
            a.integral_b_minus_s_k_seconds3==b.integral_b_minus_s_k_seconds3 &&
            a.integral_k_squared_seconds3==b.integral_k_squared_seconds3 &&
            a.k_at_end_seconds==b.k_at_end_seconds && a.k_prime_at_end==b.k_prime_at_end;
    }
    static helper::IndependentFrequencyAperture aperture(const helper::LinkMapping& m,
            const helper::ToneFrequencyObservation& o,double variance) noexcept {
        helper::FrequencyApertureProcessMoments moments{o.valid,o.first_sample,o.end_sample,
            o.state_endpoint_sample,o.mass,o.mu_seconds,o.integral_k_seconds2,
            o.integral_b_minus_s_k_seconds3,o.integral_k_squared_seconds3,
            o.k_at_end_seconds,o.k_prime_at_end};
        return {m,o.first_sample,o.end_sample,o.mean_sample,o.mean_sample_fraction,o.frequency_hz,
            variance,static_cast<double>(o.end_sample-o.first_sample)/m.sample_rate,moments};
    }
    Config config_;
    helper::OscillatorFrequencyModel model_;
    std::uint64_t clear_generation_=0,last_segment_=0,last_end_=0,activation_frontier_=0;
    bool configured_=false,have_=false,closed_=true;
};

} // namespace datapump::live::detail
