#pragma once
#include "datapump/helper_receiver.hpp"
#include "helper_punctuation_numeric.hpp"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::helper {

struct PunctuationClockDomain {
    std::uint64_t capture_generation=0,rf_gauge=0,input_view=0,provider_identity=0;
    std::uint32_t sample_rate=0;
    bool operator==(const PunctuationClockDomain&)const=default;
};
struct PunctuationClockPrior {double lower_ppm=0,upper_ppm=0;};
// The constant model is an explicit conditional affine relation over the
// convex hull of the actual marker aperture AND requested scoring support.
// Consistent endpoint fits do not establish unknown intervening thermal drift.
enum class PunctuationClockModel {unknown,constant_rate_over_requested_support};
struct PunctuationClockRequest {
    // Scored support is inclusive [first_sample,last_sample]; captured end is
    // EXCLUSIVE, matching Receiver/Live. No sample at that end is observed.
    std::uint64_t first_sample=0,last_sample=0,captured_end_sample=0,maximum_marker_age_samples=0;
    PunctuationClockModel model=PunctuationClockModel::unknown;
    // An explicit conditional zero-slew assumption can cover already captured
    // samples after the last marker. It is NEVER filled from accuracy ppm.
    std::optional<double> declared_rate_slew_ppm_per_second;
};
enum class PunctuationClockReason {
    accepted,awaiting_observations,invalid_domain,domain_mismatch,unsupported_numeric_environment,invalid_marker,future_marker,duplicate_marker,
    changed_marker,nonincreasing_marker,invalid_prior,numerical_limit,alias_capacity,
    overlapping_markers,no_compatible_alias,ambiguous_alias,non_affine_aperture,
    unknown_cross_time_model,uncovered_scored_support,stale_marker,unsupported_future_model
};
inline const char* punctuation_clock_reason(PunctuationClockReason reason) noexcept {
    switch(reason) {
    case PunctuationClockReason::accepted:return "actual punctuation intervals support a conditional constant audio-clock rate";
    case PunctuationClockReason::awaiting_observations:return "audio-clock inference needs distinct actual punctuation observations";
    case PunctuationClockReason::invalid_domain:return "punctuation capture/provider/phase gauge or sample clock is unknown";
    case PunctuationClockReason::domain_mismatch:return "punctuation domain changed; owner must explicitly reset the clock history";
    case PunctuationClockReason::unsupported_numeric_environment:return "punctuation interval arithmetic environment is unsupported";
    case PunctuationClockReason::invalid_marker:return "actual punctuation provenance or interval is invalid";
    case PunctuationClockReason::future_marker:return "actual punctuation support is ahead of captured samples";
    case PunctuationClockReason::duplicate_marker:return "carried punctuation publication supplies no new observation";
    case PunctuationClockReason::changed_marker:return "an actual punctuation identity was rewritten";
    case PunctuationClockReason::nonincreasing_marker:return "actual punctuation dates are not increasing";
    case PunctuationClockReason::invalid_prior:return "audio-clock accuracy prior is invalid";
    case PunctuationClockReason::numerical_limit:return "punctuation clock interval arithmetic cannot certify endpoints";
    case PunctuationClockReason::alias_capacity:return "all compatible dwell aliases exceed optional bounded storage";
    case PunctuationClockReason::overlapping_markers:return "punctuation endpoint intervals overlap";
    case PunctuationClockReason::no_compatible_alias:return "no integer dwell spacing fits the audio-clock accuracy prior";
    case PunctuationClockReason::ambiguous_alias:return "multiple integer dwell spacings remain possible";
    case PunctuationClockReason::non_affine_aperture:return "actual marker intervals reject a common affine audio-clock rate";
    case PunctuationClockReason::unknown_cross_time_model:return "aperture mean audio clock does not certify unknown thermal evolution";
    case PunctuationClockReason::uncovered_scored_support:return "actual dated markers do not enclose the requested scored support";
    case PunctuationClockReason::stale_marker:return "actual punctuation evidence is outside the declared freshness window";
    case PunctuationClockReason::unsupported_future_model:return "post-marker clock use needs an explicit conditional zero-slew assumption";
    }
    return "punctuation clock is unqualified";
}
struct PunctuationClockBranch {
    double lower_ppm=0,upper_ppm=0;
    std::uint64_t dwells=0;
};
struct PunctuationClockResult {
    static constexpr std::size_t branch_capacity=64;
    PunctuationClockReason reason=PunctuationClockReason::awaiting_observations;
    PunctuationClockModel model=PunctuationClockModel::unknown;
    PunctuationClockDomain domain;
    std::array<PunctuationClockBranch,branch_capacity> mean_branches{};
    std::size_t branch_count=0,observations=0;
    bool aperture_mean_known=false,affine_aperture=false,qualified_for_acquisition=false;
    bool explicit_zero_slew=false;
    // Dwell-generator source-clock ratio, with legacy physical_audio_* names.
    // The default unsteered-beacon interpretation measures physical audio a
    // and must retain unresolved DATA UTC factors c. A separate explicit
    // common-corrected-source certificate may establish that these markers
    // instead measure DATA b=(1+a)*(1+c)-1. This fit itself grants no relation.
    double physical_audio_lower_ppm=0,physical_audio_upper_ppm=0;
    std::uint64_t first_observation_sample=0,last_observation_sample=0;
    std::uint64_t first_support_sample=0,last_support_sample=0,evidence_sample=0,publisher_generation=0;
    std::uint64_t requested_first_sample=0,requested_last_sample=0;
};

namespace punctuation_clock_detail {
inline bool environment() noexcept {return punctuation_numeric::environment();}
inline double lower(double value) noexcept {return std::nextafter(value,-std::numeric_limits<double>::infinity());}
inline double upper(double value) noexcept {return std::nextafter(value,std::numeric_limits<double>::infinity());}
inline long double pad(long double a,long double b=0) noexcept {
    return 128*std::numeric_limits<double>::epsilon()*std::max({1.L,std::abs(a),std::abs(b)});
}
struct RatioInterval {long double lower=0,upper=0;};
inline std::optional<RatioInterval> prior(PunctuationClockPrior p) noexcept {
    if(!environment() || !std::isfinite(p.lower_ppm) || !std::isfinite(p.upper_ppm) ||
       p.lower_ppm>p.upper_ppm || p.lower_ppm< -10000 || p.upper_ppm>10000)return {};
    const auto low=1+static_cast<long double>(p.lower_ppm)*1e-6L;
    const auto high=1+static_cast<long double>(p.upper_ppm)*1e-6L;
    return RatioInterval{low-pad(low),high+pad(high)};
}
inline long double distance(const PunctuationObservation& a,const PunctuationObservation& b) noexcept {
    // Subtract exact integer dates before widening; do not lose short marker
    // spacings near UINT64_MAX on double-sized long-double platforms.
    return static_cast<long double>(b.sample-a.sample)+b.fraction-a.fraction;
}
inline std::optional<RatioInterval> interval(const PunctuationObservation& a,const PunctuationObservation& b,
        std::uint64_t dwells,std::uint32_t rate,RatioInterval original) noexcept {
    if(b.sample<a.sample)return {};
    const auto delta=distance(a,b),uncertainty=static_cast<long double>(a.uncertainty_samples)+b.uncertainty_samples;
    const auto numeric=pad(delta,uncertainty);
    const auto low=delta-uncertainty-numeric,high=delta+uncertainty+numeric;
    if(!(low>0) || !std::isfinite(high) || high<low || !dwells || !rate)return {};
    const auto source=static_cast<long double>(dwells)*(.4L*rate);
    auto lower=source/high,upper=source/low;
    lower=std::max(original.lower,lower-pad(lower));upper=std::min(original.upper,upper+pad(upper));
    if(!std::isfinite(lower) || !std::isfinite(upper) || lower>upper)return {};
    return RatioInterval{lower,upper};
}
inline PunctuationClockBranch branch(RatioInterval value,std::uint64_t n) noexcept {
    const auto lo=(value.lower-1)*1e6L,hi=(value.upper-1)*1e6L;
    return {lower(static_cast<double>(lo-pad(lo))),upper(static_cast<double>(hi+pad(hi))),n};
}
struct Aliases {
    std::array<PunctuationClockBranch,PunctuationClockResult::branch_capacity> branches{};
    std::size_t count=0;
    PunctuationClockReason reason=PunctuationClockReason::accepted;
};
inline Aliases aliases(const PunctuationObservation& a,const PunctuationObservation& b,
        std::uint32_t rate,RatioInterval original) noexcept {
    Aliases result;
    if(b.sample<a.sample) {result.reason=PunctuationClockReason::nonincreasing_marker;return result;}
    const auto delta=distance(a,b),uncertainty=static_cast<long double>(a.uncertainty_samples)+b.uncertainty_samples;
    const auto numeric=pad(delta,uncertainty);
    const auto low=delta-uncertainty-numeric,high=delta+uncertainty+numeric;
    if(!(low>0) || !std::isfinite(high) || !rate) {result.reason=PunctuationClockReason::overlapping_markers;return result;}
    const auto period=.4L*rate;
    const auto first=std::max(1.L,std::ceil(std::nextafter((original.lower*low-pad(original.lower*low))/period,
        -std::numeric_limits<long double>::infinity())));
    const auto last=std::floor(std::nextafter((original.upper*high+pad(original.upper*high))/period,
        std::numeric_limits<long double>::infinity()));
    if(!std::isfinite(first) || !std::isfinite(last) || first>=1e15L || last>=1e15L) {
        result.reason=PunctuationClockReason::numerical_limit;return result;
    }
    if(last<first) {result.reason=PunctuationClockReason::no_compatible_alias;return result;}
    if(last-first>=result.branches.size()) {result.reason=PunctuationClockReason::alias_capacity;return result;}
    for(auto n=static_cast<std::uint64_t>(first),end=static_cast<std::uint64_t>(last);; ++n) {
        if(const auto value=interval(a,b,n,rate,original))result.branches[result.count++]=branch(*value,n);
        if(n==end)break;
    }
    if(!result.count)result.reason=PunctuationClockReason::no_compatible_alias;
    return result;
}
inline bool marker_valid(const PunctuationObservation& p) noexcept {
    if(!p.known || !p.generation || p.first_sample>p.sample || p.sample>p.last_sample ||
       !std::isfinite(p.fraction) || p.fraction<0 || p.fraction>=1 ||
       !std::isfinite(p.uncertainty_samples) || p.uncertainty_samples<0)return false;
    const auto left=static_cast<long double>(p.sample-p.first_sample)+p.fraction;
    const auto right=static_cast<long double>(p.last_sample-p.sample)+1-p.fraction;
    return p.uncertainty_samples<=left && p.uncertainty_samples<=right;
}
inline bool same(const PunctuationObservation& a,const PunctuationObservation& b) noexcept {
    return a.generation==b.generation && a.sample==b.sample && a.fraction==b.fraction &&
        a.uncertainty_samples==b.uncertainty_samples && a.first_sample==b.first_sample &&
        a.last_sample==b.last_sample && a.known==b.known;
}
} // namespace punctuation_clock_detail

// Optional separately funded history/scratch; this class owns no Robust
// receiver, payload, phase curve, epoch or resampler. Endpoint intervals retain
// their provider's statistical/model limitations. No new confidence percentage
// or independent-noise assumption is manufactured by combining them.
class PunctuationClock {
public:
    // The caller must fund this optional persistent history AND call scratch;
    // workspace_bound is a proposed aggregate cap, subject to focused compiler
    // stack validation before this component can be used by Live.
    static constexpr std::size_t marker_capacity=16,workspace_bound=8*1024;
    // A provider identity is caller-owned and must change on Receiver
    // reconstruction. Provider-local generations alone are not global IDs.
    // Domain changes NEVER reset implicitly: delayed old publications cannot
    // roll an owner back to an older capture/view/gauge/provider.
    void reset(PunctuationClockDomain domain) noexcept {domain_=domain;generation_=0;count_=0;markers_={};}
    PunctuationClockReason observe(const Reference& reference,PunctuationClockDomain domain,
            std::uint64_t captured_end_sample) noexcept {
        using namespace punctuation_clock_detail;
        if(!environment())return PunctuationClockReason::unsupported_numeric_environment;
        if(!domain.capture_generation || !domain.rf_gauge || !domain.provider_identity || !domain.sample_rate ||
           reference.sample_rate!=domain.sample_rate)return PunctuationClockReason::invalid_domain;
        const auto& p=reference.punctuation;
        if(!marker_valid(p))return PunctuationClockReason::invalid_marker;
        if(reference.sample>=captured_end_sample || p.last_sample>reference.sample)return PunctuationClockReason::future_marker;
        if(!domain_.capture_generation)reset(domain); // first fully validated observation binds once
        if(!(domain==domain_))return PunctuationClockReason::domain_mismatch;
        if(generation_ && p.generation<generation_)return PunctuationClockReason::nonincreasing_marker;
        if(generation_ && p.generation>generation_) {count_=0;markers_={};}
        generation_=p.generation;
        if(count_) {
            const auto& last=markers_[count_-1];
            if(p.sample==last.sample)return same(p,last)?PunctuationClockReason::duplicate_marker:PunctuationClockReason::changed_marker;
            if(p.sample<last.sample || p.last_sample<=last.last_sample)return PunctuationClockReason::nonincreasing_marker;
        }
        if(count_==markers_.size()) {std::move(markers_.begin()+1,markers_.end(),markers_.begin());--count_;}
        markers_[count_++]=p;return PunctuationClockReason::accepted;
    }
    PunctuationClockResult qualify(PunctuationClockPrior accuracy,const PunctuationClockRequest& requested)const noexcept {
        using namespace punctuation_clock_detail;
        PunctuationClockResult result;result.domain=domain_;result.model=requested.model;
        result.requested_first_sample=requested.first_sample;result.requested_last_sample=requested.last_sample;
        if(!environment()) {result.reason=PunctuationClockReason::unsupported_numeric_environment;return result;}
        const auto original=prior(accuracy);
        if(!original) {result.reason=PunctuationClockReason::invalid_prior;return result;}
        if(!domain_.capture_generation || !domain_.rf_gauge || !domain_.provider_identity || !domain_.sample_rate) {
            result.reason=PunctuationClockReason::invalid_domain;return result;
        }
        std::size_t usable=0;
        while(usable<count_ && markers_[usable].last_sample<requested.captured_end_sample)++usable;
        result.observations=usable;
        if(usable<2)return result;
        const auto& first=markers_[0];const auto& last=markers_[usable-1];
        result.first_observation_sample=first.sample;result.last_observation_sample=last.sample;
        result.first_support_sample=first.first_sample;result.last_support_sample=last.last_sample;
        result.evidence_sample=last.last_sample;result.publisher_generation=last.generation;
        const auto ends=aliases(first,last,domain_.sample_rate,*original);
        result.mean_branches=ends.branches;result.branch_count=ends.count;
        if(!ends.count) {result.reason=ends.reason;return result;}
        result.aperture_mean_known=true;
        if(ends.count!=1) {result.reason=PunctuationClockReason::ambiguous_alias;return result;}
        std::array<std::uint64_t,marker_capacity> ticks{};
        for(std::size_t i=1;i<usable;++i) {
            const auto found=aliases(first,markers_[i],domain_.sample_rate,*original);
            if(found.count!=1) {
                result.reason=found.count?PunctuationClockReason::ambiguous_alias:found.reason;return result;
            }
            ticks[i]=found.branches[0].dwells;
            if(ticks[i]<=ticks[i-1]) {result.reason=PunctuationClockReason::non_affine_aperture;return result;}
        }
        auto fitted=*original;
        for(std::size_t i=0;i<usable;++i)for(std::size_t j=i+1;j<usable;++j) {
            const auto compatible=interval(markers_[i],markers_[j],ticks[j]-ticks[i],domain_.sample_rate,*original);
            if(!compatible) {result.reason=PunctuationClockReason::non_affine_aperture;return result;}
            fitted.lower=std::max(fitted.lower,compatible->lower);fitted.upper=std::min(fitted.upper,compatible->upper);
            if(fitted.lower>fitted.upper) {result.reason=PunctuationClockReason::non_affine_aperture;return result;}
        }
        result.affine_aperture=true;
        const auto affine=branch(fitted,ticks[usable-1]);
        result.physical_audio_lower_ppm=affine.lower_ppm;result.physical_audio_upper_ppm=affine.upper_ppm;
        if(requested.model!=PunctuationClockModel::constant_rate_over_requested_support) {
            result.reason=PunctuationClockReason::unknown_cross_time_model;return result;
        }
        if(requested.first_sample>requested.last_sample || requested.last_sample>=requested.captured_end_sample) {
            result.reason=PunctuationClockReason::uncovered_scored_support;return result;
        }
        if(!requested.maximum_marker_age_samples || requested.captured_end_sample-1-last.last_sample>requested.maximum_marker_age_samples) {
            result.reason=PunctuationClockReason::stale_marker;return result;
        }
        // Enclose requested integer support inside the actual marker aperture,
        // accounting for endpoint intervals, without converting huge dates to
        // floating point. A publication's nominal dwell date is never used.
        if(requested.first_sample<first.sample ||
           static_cast<long double>(requested.first_sample-first.sample)<first.fraction+first.uncertainty_samples) {
            result.reason=PunctuationClockReason::uncovered_scored_support;return result;
        }
        const auto after_last=requested.last_sample>=last.sample?
            static_cast<long double>(requested.last_sample-last.sample)-last.fraction:
            -static_cast<long double>(last.sample-requested.last_sample)-last.fraction;
        if(after_last> -static_cast<long double>(last.uncertainty_samples)) {
            if(!requested.declared_rate_slew_ppm_per_second ||
               !std::isfinite(*requested.declared_rate_slew_ppm_per_second) ||
               *requested.declared_rate_slew_ppm_per_second!=0) {
                result.reason=PunctuationClockReason::unsupported_future_model;return result;
            }
            result.explicit_zero_slew=true;
        }
        result.qualified_for_acquisition=true;result.reason=PunctuationClockReason::accepted;return result;
    }
    std::size_t observation_count()const noexcept {return count_;}
    std::size_t working_bytes()const noexcept {return sizeof(*this);}
private:
    std::array<PunctuationObservation,marker_capacity> markers_{};
    PunctuationClockDomain domain_;
    std::uint64_t generation_=0;
    std::size_t count_=0;
};
static_assert(sizeof(PunctuationClock)<=1024,"punctuation clock history needs a separate bounded optional grant");

} // namespace datapump::helper
