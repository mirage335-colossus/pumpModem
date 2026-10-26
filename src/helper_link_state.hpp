#pragma once

#include "datapump/helper_receiver.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
#include <xmmintrin.h>
#endif

namespace datapump::helper {

// PRIVATE PROPOSAL. No receiver, PCM/NCO, payload, phase curve, metadata, UTC or
// completion state is owned here. The caller retains ordinary broad fallback.
// All fractions and slew rates are effective relative link quantities, not
// per-end oscillator specifications. Accuracy is never divided by time.
enum class LinkTopology { independent_audio, shared_radio };
enum class LinkMappingProvenance {
    unknown, fixed_configuration, modelled_physical_capture, confirmed_adapter,
    fake_schedule_preview
};
struct LinkModel {
    LinkTopology topology=LinkTopology::independent_audio;
    // Caller supplies the complete configured static envelope, including its
    // search margin. No confidence or margin multiplier is invented here.
    double rf_accuracy_fraction=0,audio_accuracy_fraction=0;
    // Empty means thermal/mean-frequency evolution is not modelled. Existing
    // presets must remain empty; an explicit zero is a constant-mean assumption.
    std::optional<double> rf_maximum_fractional_slew_per_second;
    std::optional<double> audio_maximum_fractional_slew_per_second;
    // A freshness policy, not a future thermal-drift certificate.
    double maximum_current_measurement_age_seconds=0;
};
struct LinkMapping {
    // Zero is a valid ID. Unknown reference identity has its own flag.
    std::uint64_t capture_generation=0,timebase_identity=0,reference_identity=0;
    bool reference_identity_known=false;
    std::uint32_t sample_rate=0;
    // Settled actual mapping interval [first,end), in this capture timebase.
    std::uint64_t settled_first_sample=0,settled_end_sample=0;
    double actual_rf_shift_hz=0,helper_audio_hz=0,data_audio_hz=0;
    int sideband_sign=1;
    // The caller retains both signed branches when mono image is unresolved.
    // Zero never selects the strongest image or silently merges two states.
    int mono_image_sign=1;
    LinkMappingProvenance provenance=LinkMappingProvenance::unknown;
    std::uint64_t phase_segment=0;
    bool phase_continuity_known=false;
};
struct LinkObservation {
    const Reference& reference;
    const LinkMapping& mapping;
    // Certificate from the actual raw fit: its published frequency interval
    // already encloses at least this deterministic delivered-Hz/s slew, in
    // addition to its configured Wiener process enclosure. This component
    // never adds that within-fit allowance to the interval again.
    std::optional<double> fit_slew_enclosed_hz_per_second;
};
struct LinkInterval {
    double lower=-std::numeric_limits<double>::infinity();
    double upper=std::numeric_limits<double>::infinity();
};
enum class LinkAuthority { declined, current_measurement, predictive_state };
enum class LinkReason {
    accepted_prediction, current_thermal_evolution_unmodelled,
    current_reference_identity_unknown, invalid_model, invalid_mapping,
    fake_mapping, mono_image_unresolved, invalid_measurement, stale_measurement,
    future_measurement, straddled_mapping, timebase_changed, reference_changed,
    topology_changed, nonincreasing_measurement, within_fit_slew_not_enclosed,
    oscillator_innovation_exceeded, numeric_limit, no_state,
    unknown_evolution_prediction_declined, prediction_horizon_exceeded,
    unsupported_numeric_environment, cached_record_requires_validation
};
inline const char* link_reason(LinkReason reason) noexcept {
    switch(reason) {
    case LinkReason::accepted_prediction:return "dated fractional state fits the declared oscillator evolution";
    case LinkReason::current_thermal_evolution_unmodelled:return "current measured reference; thermal frequency evolution is unmodelled";
    case LinkReason::current_reference_identity_unknown:return "current measured reference; common oscillator identity is unknown";
    case LinkReason::invalid_model:return "invalid fractional oscillator model";
    case LinkReason::invalid_mapping:return "actual settled capture mapping is unavailable";
    case LinkReason::fake_mapping:return "FHSS presentation is not an actual RF mapping";
    case LinkReason::mono_image_unresolved:return "mono image remains ambiguous; retain both ordinary signed branches";
    case LinkReason::invalid_measurement:return "invalid dated helper frequency observation";
    case LinkReason::stale_measurement:return "helper frequency support is older than the current-reference horizon";
    case LinkReason::future_measurement:return "helper frequency support is ahead of the captured frontier";
    case LinkReason::straddled_mapping:return "helper frequency fit straddles an unsettled RF mapping";
    case LinkReason::timebase_changed:return "capture generation or timebase changed; explicit reset required";
    case LinkReason::reference_changed:return "oscillator reference identity changed; explicit reset required";
    case LinkReason::topology_changed:return "active RF/audio topology changed; explicit reset required";
    case LinkReason::nonincreasing_measurement:return "helper frequency endpoint is not independently newer";
    case LinkReason::within_fit_slew_not_enclosed:return "raw fit has no enclosure for the declared within-fit slew";
    case LinkReason::oscillator_innovation_exceeded:return "helper frequency innovation exceeds the declared oscillator evolution";
    case LinkReason::numeric_limit:return "fractional-state arithmetic cannot certify a bounded domain";
    case LinkReason::no_state:return "no dated predictive link state";
    case LinkReason::unknown_evolution_prediction_declined:return "thermal drift is unmodelled; current measurement is not a future prediction";
    case LinkReason::prediction_horizon_exceeded:return "requested prediction exceeds its explicit finite horizon";
    case LinkReason::unsupported_numeric_environment:return "fractional-state arithmetic environment is unsupported";
    case LinkReason::cached_record_requires_validation:return "cached measurement record; no current acquisition authority";
    }
    return "helper link assistance declined";
}
struct LinkResult {
    LinkAuthority authority=LinkAuthority::declined;
    LinkReason reason=LinkReason::no_state;
    // Declined results expose the ordinary static domain when it is valid.
    bool ordinary_domain_valid=false;
    // Mapping continuity only. This NEVER qualifies measured phase: the caller
    // still needs independent fresh phase/punctuation and forecast checks.
    bool phase_bridge_allowed=false;
    // Physical coordinates, even though the shared implementation uses one
    // internal coordinate. Active does not mean individually resolved.
    bool rf_coordinate_active=false,audio_coordinate_active=false;
    bool fractional_coordinates_equal=false;
    std::uint64_t state_sample=0,evidence_first_sample=0,evidence_last_sample=0;
    LinkInterval rf_fraction,audio_fraction,helper_residual_hz,data_residual_hz;
    double required_fit_slew_hz_per_second=0;
    double required_fit_drift_phase_radius_radians=0;
    std::size_t retained_halfspaces=0;
};

namespace link_state_detail {
using Real=long double;
constexpr auto infinity=std::numeric_limits<Real>::infinity();
// Conservative absolute budget for underflowed intermediate operations. It is
// intentionally a normal value, so correctness does not require subnormals.
constexpr auto underflow_budget=128*std::numeric_limits<Real>::min();
inline bool arithmetic_environment() noexcept {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || (defined(_M_FP_FAST) && _M_FP_FAST) || FLT_EVAL_METHOD!=0
    return false;
#else
    // The arithmetic budget is qualified only for binary64, x87 extended or
    // IEEE binary128 Real, without excess evaluation of float/double operands.
    constexpr bool real_format=std::numeric_limits<Real>::is_iec559&&std::numeric_limits<Real>::radix==2&&
        ((std::numeric_limits<Real>::digits==53&&std::numeric_limits<Real>::max_exponent==1024&&std::numeric_limits<Real>::min_exponent==-1021)||
         (std::numeric_limits<Real>::digits==64&&std::numeric_limits<Real>::max_exponent==16384&&std::numeric_limits<Real>::min_exponent==-16381)||
         (std::numeric_limits<Real>::digits==113&&std::numeric_limits<Real>::max_exponent==16384&&std::numeric_limits<Real>::min_exponent==-16381));
    if(!real_format||!std::numeric_limits<double>::is_iec559||std::numeric_limits<double>::radix!=2||
       std::numeric_limits<double>::digits!=53||std::numeric_limits<double>::max_exponent!=1024||
       sizeof(double)!=sizeof(std::uint64_t)||std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    if(_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))return false;
#endif
    volatile double tiny=std::numeric_limits<double>::denorm_min(),one=1;
    volatile double normal=std::numeric_limits<double>::min(),half=.5;
    const double input=tiny*one,output=normal*half;
    if(std::bit_cast<std::uint64_t>(input)!=1||std::bit_cast<std::uint64_t>(output)!=0x0008000000000000ULL)return false;
    volatile Real real_tiny=std::numeric_limits<Real>::denorm_min(),real_one=1;
    volatile Real real_normal=std::numeric_limits<Real>::min(),real_half=.5;
    volatile Real epsilon=std::numeric_limits<Real>::epsilon();
    const Real real_input=real_tiny*real_one,real_output=real_normal*real_half;
    const Real next=real_one+epsilon;
    // The final probe also detects a reduced x87 precision-control word.
    return real_input>0&&real_output>0&&next-real_one==epsilon;
#endif
}
inline bool nonnegative(double x) noexcept {return std::isfinite(x)&&x>=0;}
inline Real error(Real scale) noexcept {
    if(scale==0)return 0;
    return std::nextafter(128*std::numeric_limits<Real>::epsilon()*std::abs(scale),infinity);
}
inline Real outward(Real x) noexcept {return x==0?0:std::nextafter(x,infinity);}
inline Real positive_product_upper(Real a,Real b) noexcept {
    if(a==0||b==0)return 0;
    const auto product=a*b;
    if(!std::isfinite(product))return infinity;
    return outward(product+std::max(error(product),underflow_budget));
}
inline Real positive_sum_upper(Real a,Real b) noexcept {
    if(a==0&&b==0)return 0;
    const auto sum=a+b;
    if(!std::isfinite(sum))return infinity;
    return outward(sum+std::max(error(sum),underflow_budget));
}
inline double up(Real x) noexcept {
    if(x==0)return 0;
    if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<double>::max())return std::numeric_limits<double>::infinity();
    return std::nextafter(static_cast<double>(x),std::numeric_limits<double>::infinity());
}
inline double down(Real x) noexcept {
    if(x==0)return 0;
    if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<double>::max())return -std::numeric_limits<double>::infinity();
    return std::nextafter(static_cast<double>(x),-std::numeric_limits<double>::infinity());
}
inline Real seconds(std::uint64_t delta,std::uint32_t rate) noexcept {
    // Exact unsigned subtraction happens at the caller, before floating point
    // conversion. Widen conversion/division even with a 53-bit long double.
    const auto t=static_cast<Real>(delta)/rate;
    return outward(t+error(t));
}
struct Vector {Real rf=0,audio=0;};
struct Halfspace {Vector a;Real b=0;};
enum class ActiveTopology { independent,shared, audio_only };
struct Geometry {
    Vector helper,data;
    Real rf_box=0,audio_box=0,rf_slew=0,audio_slew=0;
    bool evolution_known=false;
    ActiveTopology topology=ActiveTopology::independent;
};
inline Real box_support(Vector c,Real rf,Real audio) noexcept {
    if((c.rf==0||rf==0)&&(c.audio==0||audio==0))return 0;
    const auto value=std::abs(c.rf)*rf+std::abs(c.audio)*audio;
    if(value==0)return underflow_budget;
    return outward(value+error(value));
}

// A bounded intersection of halfspaces and a fixed static box. We never need
// fragile polygon clipping: each query uses a nonnegative dual combination
// plus the support of its EXPLICIT residual vector over the static box. Any
// multiplier (including a rounded or poorly conditioned one) remains a valid
// upper bound after the arithmetic allowance. Capacity eviction only widens.
struct Domain {
    static constexpr std::size_t capacity=12;
    std::array<Halfspace,capacity> planes{};
    std::size_t count=0;
    Real rf_box=0,audio_box=0;
    Real dual(Vector c,const Halfspace* a,Real x,const Halfspace* b=nullptr,Real y=0) const noexcept {
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0)return infinity;
        const auto ax=a?x*a->a.rf:0,ay=a?x*a->a.audio:0;
        const auto bx=b?y*b->a.rf:0,by=b?y*b->a.audio:0;
        const auto left=a?x*a->b:0,right=b?y*b->b:0;
        const Vector residual{c.rf-ax-bx,c.audio-ay-by};
        const auto remainder=box_support(residual,rf_box,audio_box);
        const auto scale=std::abs(left)+std::abs(right)+
            (std::abs(c.rf)+std::abs(ax)+std::abs(bx))*rf_box+
            (std::abs(c.audio)+std::abs(ay)+std::abs(by))*audio_box+remainder;
        const auto result=left+right+remainder+std::max(error(scale),underflow_budget);
        return std::isfinite(result)?outward(result):infinity;
    }
    Real upper(Vector c) const noexcept {
        auto bound=box_support(c,rf_box,audio_box);
        for(std::size_t i=0;i<count;++i) {
            const auto& a=planes[i];
            if(a.a.rf!=0)bound=std::min(bound,dual(c,&a,c.rf/a.a.rf));
            if(a.a.audio!=0)bound=std::min(bound,dual(c,&a,c.audio/a.a.audio));
            for(std::size_t j=0;j<i;++j) {
                const auto& b=planes[j];
                const auto determinant=a.a.rf*b.a.audio-a.a.audio*b.a.rf;
                if(!std::isfinite(determinant)||determinant==0)continue;
                const auto x=(c.rf*b.a.audio-c.audio*b.a.rf)/determinant;
                const auto y=(a.a.rf*c.audio-a.a.audio*c.rf)/determinant;
                // No assertion that the rounded multipliers solve the dual:
                // dual() always carries their actual residual to the box.
                bound=std::min(bound,dual(c,&a,x,&b,y));
            }
        }
        return bound;
    }
    LinkInterval range(Vector c) const noexcept {
        const auto high=upper(c),low=-upper({-c.rf,-c.audio});
        return {down(low),up(high)};
    }
    bool add(Vector a,Real b) noexcept {
        const auto scale=std::max(std::abs(a.rf),std::abs(a.audio));
        if(!std::isfinite(scale)||!std::isfinite(b)||scale==0)return false;
        a={a.rf/scale,a.audio/scale};b/=scale;
        const auto budget=std::abs(b)+std::abs(a.rf)*rf_box+std::abs(a.audio)*audio_box;
        b=outward(b+std::max(error(budget),underflow_budget));
        if(!std::isfinite(b)||count==capacity)return false;
        planes[count++]={a,b};return true;
    }
    void make_room_for_strip() noexcept {
        if(count+2<=capacity)return;
        for(std::size_t i=2;i<count;++i)planes[i-2]=planes[i];
        count-=2;
    }
    bool advance(Real dt,Vector slew) noexcept {
        for(std::size_t i=0;i<count;++i) {
            const auto rate=positive_sum_upper(positive_product_upper(std::abs(planes[i].a.rf),slew.rf),
                positive_product_upper(std::abs(planes[i].a.audio),slew.audio));
            const auto growth=positive_product_upper(rate,dt);
            const auto scale=std::abs(planes[i].b)+growth;
            planes[i].b=outward(planes[i].b+growth+std::max(error(scale),underflow_budget));
            if(!std::isfinite(planes[i].b))return false;
        }
        return true;
    }
    bool feasible() const noexcept {
        // A constructive witness is needed before publishing a refined state.
        // Failure to find one declines; it never proves an empty domain from
        // rounded intersections. Candidates are checked with an upper error
        // bound. This is bounded O(capacity^3), with no heap or LP library.
        const auto within=[&](Vector p) {
            if(!std::isfinite(p.rf)||!std::isfinite(p.audio)||std::abs(p.rf)>rf_box||std::abs(p.audio)>audio_box)return false;
            for(std::size_t i=0;i<count;++i) {
                const auto x=planes[i].a.rf*p.rf,y=planes[i].a.audio*p.audio;
                if(x+y+error(std::abs(x)+std::abs(y))>planes[i].b)return false;
            }
            return true;
        };
        if(within({0,0}))return true;
        for(const auto x:{-rf_box,rf_box})for(const auto y:{-audio_box,audio_box})if(within({x,y}))return true;
        std::array<Halfspace,capacity+4> all{};
        for(std::size_t i=0;i<count;++i)all[i]=planes[i];
        all[count]={{1,0},rf_box};all[count+1]={{-1,0},rf_box};
        all[count+2]={{0,1},audio_box};all[count+3]={{0,-1},audio_box};
        for(std::size_t i=0;i<count+4;++i)for(std::size_t j=0;j<i;++j) {
            const auto& a=all[i];const auto& b=all[j];
            const auto determinant=a.a.rf*b.a.audio-a.a.audio*b.a.rf;
            if(!std::isfinite(determinant)||determinant==0)continue;
            // Move supporting lines slightly INWARD for witness construction;
            // this never changes the stored outer domain or query certificate.
            const auto ab=a.b-error(std::abs(a.b)+box_support(a.a,rf_box,audio_box));
            const auto bb=b.b-error(std::abs(b.b)+box_support(b.a,rf_box,audio_box));
            const Vector p{(ab*b.a.audio-a.a.audio*bb)/determinant,
                (a.a.rf*bb-ab*b.a.rf)/determinant};
            if(within(p))return true;
        }
        return false;
    }
};
} // namespace link_state_detail

class LinkState {
    using Real=link_state_detail::Real;
    using Vector=link_state_detail::Vector;
    using Geometry=link_state_detail::Geometry;
    using Domain=link_state_detail::Domain;
public:
    static constexpr std::size_t halfspace_capacity=Domain::capacity;
    bool reset(const LinkModel& model) noexcept {
        *this=LinkState{};model_=model;
        valid_=link_state_detail::arithmetic_environment()&&valid_model(model);return valid_;
    }
    // captured_end_sample is the EXCLUSIVE observed-input end used by Live and
    // Receiver. An observation at that index is future input, not captured PCM.
    LinkResult observe(const LinkObservation& observation,std::uint64_t captured_end_sample) noexcept {
        if(!link_state_detail::arithmetic_environment())return reject_environment();
        const auto& r=observation.reference;const auto& m=observation.mapping;
        Geometry g;auto reason=geometry(m,g);
        if(reason!=LinkReason::accepted_prediction)return reject(m,reason);
        if(!r.frequency_valid||r.sample_rate!=m.sample_rate||!std::isfinite(r.frequency_hz)||
           !link_state_detail::nonnegative(r.frequency_uncertainty_hz)||
           !link_state_detail::nonnegative(r.frequency_process_uncertainty_hz)||
           r.frequency_process_uncertainty_hz>r.frequency_uncertainty_hz||
           (r.frequency_fit!=FrequencyFit::local_polynomial&&r.frequency_fit!=FrequencyFit::conditioned_polynomial&&
            r.frequency_fit!=FrequencyFit::masked_tone_profile))return reject(m,LinkReason::invalid_measurement);
        const auto last=r.frequency_sample?r.frequency_sample:r.sample;
        if(last>r.sample||r.sample>=captured_end_sample)return reject(m,LinkReason::future_measurement);
        if(last==std::numeric_limits<std::uint64_t>::max()||r.frequency_first_sample>last)
            return reject(m,LinkReason::invalid_measurement);
        if(r.frequency_first_sample<m.settled_first_sample||last>=m.settled_end_sample)
            return reject(m,LinkReason::straddled_mapping);
        const auto captured_last_sample=captured_end_sample-1;
        const auto age=link_state_detail::seconds(captured_last_sample-last,m.sample_rate);
        if(age>model_.maximum_current_measurement_age_seconds && captured_last_sample!=last)
            return reject(m,LinkReason::stale_measurement);
        if(bound_) {
            if(m.capture_generation!=mapping_.capture_generation||m.timebase_identity!=mapping_.timebase_identity||
               m.sample_rate!=mapping_.sample_rate)return reject(m,LinkReason::timebase_changed);
            if(!m.reference_identity_known||m.reference_identity!=mapping_.reference_identity)
                return reject(m,LinkReason::reference_changed);
            if(g.topology!=topology_)return reject(m,LinkReason::topology_changed);
            if(last<=sample_)return reject(m,LinkReason::nonincreasing_measurement);
        }
        const auto hertz=static_cast<Real>(m.mono_image_sign)*r.frequency_hz-m.helper_audio_hz;
        const auto uncertainty=static_cast<Real>(r.frequency_uncertainty_hz);
        const auto input_error=link_state_detail::error(std::abs(hertz)+std::abs(static_cast<Real>(r.frequency_hz))+
            std::abs(static_cast<Real>(m.helper_audio_hz)));
        const auto lower=hertz-uncertainty-input_error,upper=hertz+uncertainty+input_error;
        const auto required_slew=link_state_detail::positive_sum_upper(
            link_state_detail::positive_product_upper(std::abs(g.helper.rf),g.rf_slew),
            link_state_detail::positive_product_upper(std::abs(g.helper.audio),g.audio_slew));
        const auto duration=link_state_detail::seconds(last-r.frequency_first_sample+1,m.sample_rate);
        const auto phase_radius=link_state_detail::positive_product_upper(
            link_state_detail::positive_product_upper(std::numbers::pi_v<Real>,required_slew),
            link_state_detail::positive_product_upper(duration,duration));
        if(!std::isfinite(lower)||!std::isfinite(upper)||!std::isfinite(required_slew)||!std::isfinite(phase_radius))
            return reject(m,LinkReason::numeric_limit);
        const bool predictive=g.evolution_known&&m.reference_identity_known;
        if(predictive&&(!observation.fit_slew_enclosed_hz_per_second||
           !link_state_detail::nonnegative(*observation.fit_slew_enclosed_hz_per_second)||
           static_cast<Real>(*observation.fit_slew_enclosed_hz_per_second)<required_slew)) {
            auto declined=reject(m,LinkReason::within_fit_slew_not_enclosed);
            declined.required_fit_slew_hz_per_second=link_state_detail::up(required_slew);
            declined.required_fit_drift_phase_radius_radians=link_state_detail::up(phase_radius);
            return declined;
        }
        Domain next;next.rf_box=g.rf_box;next.audio_box=g.audio_box;
        if(predictive&&bound_) {
            next=domain_;
            if(!next.advance(link_state_detail::seconds(last-sample_,m.sample_rate),{g.rf_slew,g.audio_slew}))
                return reject(m,LinkReason::numeric_limit);
        }
        const auto predicted=next.range(g.helper);
        if(upper<static_cast<Real>(predicted.lower)||lower>static_cast<Real>(predicted.upper))
            return reject(m,LinkReason::oscillator_innovation_exceeded);
        next.make_room_for_strip();
        if(!next.add(g.helper,upper)||!next.add({-g.helper.rf,-g.helper.audio},-lower)||!next.feasible())
            return reject(m,LinkReason::numeric_limit);
        auto result=describe(next,m,g,last,r.frequency_first_sample,last);
        result.authority=predictive?LinkAuthority::predictive_state:LinkAuthority::current_measurement;
        result.reason=predictive?LinkReason::accepted_prediction:
            m.reference_identity_known?LinkReason::current_thermal_evolution_unmodelled:LinkReason::current_reference_identity_unknown;
        result.required_fit_slew_hz_per_second=link_state_detail::up(required_slew);
        result.required_fit_drift_phase_radius_radians=link_state_detail::up(phase_radius);
        result.phase_bridge_allowed=bound_&&!suspended_&&same_phase_mapping(mapping_,m);
        if(predictive) {
            domain_=next;mapping_=m;sample_=last;first_=r.frequency_first_sample;
            topology_=g.topology;bound_=true;suspended_=false;
        }
        // Current-only state is informational, never fed back as a latent mean.
        current_=result;have_current_=true;
        return result;
    }
    LinkResult predict(const LinkMapping& mapping,std::uint64_t at_sample,double maximum_horizon_seconds) const noexcept {
        if(!link_state_detail::arithmetic_environment())return environment_decline();
        Geometry g;const auto reason=geometry(mapping,g);
        if(reason!=LinkReason::accepted_prediction)return ordinary(mapping,reason);
        if(!g.evolution_known)return ordinary(mapping,LinkReason::unknown_evolution_prediction_declined);
        if(!bound_)return ordinary(mapping,LinkReason::no_state);
        if(suspended_)return ordinary(mapping,suspension_reason_);
        if(mapping.capture_generation!=mapping_.capture_generation||mapping.timebase_identity!=mapping_.timebase_identity||
           mapping.sample_rate!=mapping_.sample_rate)return ordinary(mapping,LinkReason::timebase_changed);
        if(!mapping.reference_identity_known||mapping.reference_identity!=mapping_.reference_identity)
            return ordinary(mapping,LinkReason::reference_changed);
        if(g.topology!=topology_)return ordinary(mapping,LinkReason::topology_changed);
        if(at_sample<sample_)return ordinary(mapping,LinkReason::nonincreasing_measurement);
        if(!link_state_detail::nonnegative(maximum_horizon_seconds))return ordinary(mapping,LinkReason::prediction_horizon_exceeded);
        const auto dt=link_state_detail::seconds(at_sample-sample_,mapping.sample_rate);
        if(at_sample!=sample_&&dt>maximum_horizon_seconds)return ordinary(mapping,LinkReason::prediction_horizon_exceeded);
        if(at_sample<mapping.settled_first_sample||at_sample>=mapping.settled_end_sample)
            return ordinary(mapping,LinkReason::straddled_mapping);
        auto next=domain_;
        if(!next.advance(dt,{g.rf_slew,g.audio_slew}))return ordinary(mapping,LinkReason::numeric_limit);
        auto result=describe(next,mapping,g,at_sample,first_,sample_);
        result.authority=LinkAuthority::predictive_state;result.reason=LinkReason::accepted_prediction;
        result.phase_bridge_allowed=same_phase_mapping(mapping_,mapping);
        return result;
    }
    // Historical dated record only. This accessor has no captured frontier and
    // therefore deliberately returns NO acquisition authority. Use observe()
    // with a new exclusive frontier, or predict() with declared evolution.
    std::optional<LinkResult> cached_measurement_record() const noexcept {
        if(!link_state_detail::arithmetic_environment()||!have_current_)return {};
        auto record=current_;record.authority=LinkAuthority::declined;
        record.reason=LinkReason::cached_record_requires_validation;record.phase_bridge_allowed=false;
        return record;
    }
    bool has_predictive_state() const noexcept {return link_state_detail::arithmetic_environment()&&bound_&&!suspended_;}
    bool prediction_suspended() const noexcept {return suspended_;}

private:
    static bool valid_model(const LinkModel& m) noexcept {
        const auto valid_optional=[](const std::optional<double>& x) {return !x||link_state_detail::nonnegative(*x);};
        return (m.topology==LinkTopology::independent_audio||m.topology==LinkTopology::shared_radio)&&
            link_state_detail::nonnegative(m.rf_accuracy_fraction)&&m.rf_accuracy_fraction<1&&
            link_state_detail::nonnegative(m.audio_accuracy_fraction)&&m.audio_accuracy_fraction<1&&
            valid_optional(m.rf_maximum_fractional_slew_per_second)&&valid_optional(m.audio_maximum_fractional_slew_per_second)&&
            link_state_detail::nonnegative(m.maximum_current_measurement_age_seconds);
    }
    LinkReason geometry(const LinkMapping& m,Geometry& g) const noexcept {
        if(!valid_)return LinkReason::invalid_model;
        if(m.provenance==LinkMappingProvenance::fake_schedule_preview)return LinkReason::fake_mapping;
        if((m.provenance!=LinkMappingProvenance::fixed_configuration&&m.provenance!=LinkMappingProvenance::modelled_physical_capture&&
            m.provenance!=LinkMappingProvenance::confirmed_adapter)||!m.sample_rate||m.settled_first_sample>=m.settled_end_sample||
           !link_state_detail::nonnegative(m.actual_rf_shift_hz)||!std::isfinite(m.helper_audio_hz)||
           !std::isfinite(m.data_audio_hz)||(m.sideband_sign!=1&&m.sideband_sign!=-1))return LinkReason::invalid_mapping;
        if(m.mono_image_sign!=1&&m.mono_image_sign!=-1)return LinkReason::mono_image_unresolved;
        const auto rf=static_cast<Real>(m.sideband_sign)*m.actual_rf_shift_hz;
        if(m.actual_rf_shift_hz==0) {
            g.topology=link_state_detail::ActiveTopology::audio_only;
            g.helper={0,m.helper_audio_hz};g.data={0,m.data_audio_hz};g.audio_box=model_.audio_accuracy_fraction;
            g.evolution_known=model_.audio_maximum_fractional_slew_per_second.has_value();
            g.audio_slew=model_.audio_maximum_fractional_slew_per_second.value_or(0);
        } else if(model_.topology==LinkTopology::shared_radio) {
            g.topology=link_state_detail::ActiveTopology::shared;
            g.helper={rf+m.helper_audio_hz,0};g.data={rf+m.data_audio_hz,0};g.rf_box=model_.rf_accuracy_fraction;
            g.evolution_known=model_.rf_maximum_fractional_slew_per_second.has_value();
            g.rf_slew=model_.rf_maximum_fractional_slew_per_second.value_or(0);
        } else {
            g.helper={rf,m.helper_audio_hz};g.data={rf,m.data_audio_hz};
            g.rf_box=model_.rf_accuracy_fraction;g.audio_box=model_.audio_accuracy_fraction;
            g.evolution_known=model_.rf_maximum_fractional_slew_per_second&&model_.audio_maximum_fractional_slew_per_second;
            g.rf_slew=model_.rf_maximum_fractional_slew_per_second.value_or(0);
            g.audio_slew=model_.audio_maximum_fractional_slew_per_second.value_or(0);
        }
        if(!std::isfinite(g.helper.rf)||!std::isfinite(g.helper.audio)||
           (g.helper.rf==0&&g.helper.audio==0)||!std::isfinite(g.data.rf)||!std::isfinite(g.data.audio))
            return LinkReason::invalid_mapping;
        return LinkReason::accepted_prediction;
    }
    static bool same_phase_mapping(const LinkMapping& a,const LinkMapping& b) noexcept {
        return a.reference_identity_known&&b.reference_identity_known&&a.reference_identity==b.reference_identity&&
            a.phase_continuity_known&&b.phase_continuity_known&&a.phase_segment==b.phase_segment&&
            a.actual_rf_shift_hz==b.actual_rf_shift_hz&&a.helper_audio_hz==b.helper_audio_hz&&a.data_audio_hz==b.data_audio_hz&&
            a.sideband_sign==b.sideband_sign&&a.mono_image_sign==b.mono_image_sign;
    }
    LinkResult describe(const Domain& d,const LinkMapping&,const Geometry& g,std::uint64_t at,
            std::uint64_t first,std::uint64_t last) const noexcept {
        LinkResult r;r.ordinary_domain_valid=true;r.state_sample=at;r.evidence_first_sample=first;r.evidence_last_sample=last;
        r.rf_fraction=d.range({1,0});r.audio_fraction=d.range({0,1});
        r.rf_coordinate_active=g.topology!=link_state_detail::ActiveTopology::audio_only;
        r.audio_coordinate_active=true;
        if(g.topology==link_state_detail::ActiveTopology::shared) {
            r.audio_fraction=r.rf_fraction;r.fractional_coordinates_equal=true;
        }
        if(g.topology==link_state_detail::ActiveTopology::audio_only)
            r.rf_fraction={link_state_detail::down(-static_cast<Real>(model_.rf_accuracy_fraction)),
                link_state_detail::up(model_.rf_accuracy_fraction)};
        r.helper_residual_hz=d.range(g.helper);r.data_residual_hz=d.range(g.data);r.retained_halfspaces=d.count;
        return r;
    }
    LinkResult ordinary(const LinkMapping& m,LinkReason reason) const noexcept {
        LinkResult r;r.reason=reason;Geometry g;
        if(geometry(m,g)!=LinkReason::accepted_prediction)return r;
        Domain d;d.rf_box=g.rf_box;d.audio_box=g.audio_box;r=describe(d,m,g,0,0,0);r.reason=reason;return r;
    }
    LinkResult reject(const LinkMapping& m,LinkReason reason) noexcept {
        // Keep the last accepted domain/date. A rejected chirp cannot reset or
        // reseed it, and a caller cannot recover optional steering by querying
        // the stale cache. A later valid independent measurement may recover.
        have_current_=false;
        if(bound_) {suspended_=true;suspension_reason_=reason;}
        return ordinary(m,reason);
    }
    static LinkResult environment_decline() noexcept {
        LinkResult r;r.reason=LinkReason::unsupported_numeric_environment;return r;
    }
    LinkResult reject_environment() noexcept {
        have_current_=false;
        if(bound_) {suspended_=true;suspension_reason_=LinkReason::unsupported_numeric_environment;}
        return environment_decline();
    }
    LinkModel model_{};
    Domain domain_{};
    LinkMapping mapping_{};
    LinkResult current_{};
    std::uint64_t sample_=0,first_=0;
    link_state_detail::ActiveTopology topology_=link_state_detail::ActiveTopology::independent;
    LinkReason suspension_reason_=LinkReason::no_state;
    bool valid_=false,bound_=false,have_current_=false,suspended_=false;
};
static_assert(sizeof(LinkState)<=2048,"optional link-state storage must be explicitly charged");
static_assert(sizeof(link_state_detail::Domain)<=768,"bounded helper domain cannot grow with observations");

} // namespace datapump::helper
