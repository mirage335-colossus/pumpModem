#pragma once
#include "datapump/pattern_receiver.hpp"
#include "punctuation_clock.hpp"
#include "helper_link_state.hpp"
#include "pattern_fft_clock_prior.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace datapump::helper {
enum class JointClockInputBasis {unknown,unshifted_fixed_filter_and_nominal_resampler};
// Conditional link relation, independent of marker/frequency qualification.
// Data Pump's normal output composes helper tone/markers at the SAME corrected
// Robust source coordinate as payload/FHSS. A separate generic beacon cannot
// assert this relation. Unknown keeps the original independent-a/UTC union.
enum class JointClockSourceRelation {physical_audio_unsteered,common_corrected_robust_source};
// Caller expands the exact native support through its actual fixed FIR/
// resampler mapping. These are RAW capture coordinates, distinct from the
// receiver-local coordinates in scored. No clipping to available PCM is legal.
struct JointClockCellRequest {
    modem::PatternAcquisitionScoredSupport scored;
    PunctuationClockDomain raw_domain;
    std::uint64_t raw_first_sample=0,raw_last_sample=0,raw_captured_end_sample=0;
    std::uint64_t input_captured_end_sample=0;
    // Must be the ordinary profile's effective steering policy. Disabled means
    // ordinary DATA has no UTC rate steering, not that c was measured as zero.
    bool ordinary_utc_steering_enabled=true;
    // Caller certifies the immutable row's ACTUAL input view. Core cannot infer
    // an external NCO/phase/time warp from its own optional phase-curve pointer.
    // Unknown/transformed views decline. A nominal sample-rate conversion does
    // preserve physical fractional clock/Hz; its entire real FIR/resampler
    // support must still be expanded above in the separate RAW sample clock.
    JointClockInputBasis input_basis=JointClockInputBasis::unknown;
    JointClockSourceRelation source_relation=JointClockSourceRelation::physical_audio_unsteered;
    // Optional immutable publication date for an owned RAW frequency record.
    // This is an EXCLUSIVE original capture end, not clock/phase authority.
    // Empty preserves validation at the actual current raw captured frontier.
    std::optional<std::uint64_t> frequency_observation_frontier;
};
enum class JointClockCellReason {
    accepted,unsupported_numeric_environment,invalid_domain,unsupported_input_basis,
    unqualified_audio_clock,unqualified_frequency,uncovered_scored_support,
    unsupported_catalogue,uncovered_data_clock,legacy_frequency_mask_too_narrow,
    uncovered_joint_cells,no_pair_reduction
};
struct JointClockCellResult {
    JointClockCellReason reason=JointClockCellReason::invalid_domain;
    std::size_t original_pairs=0,selected_pairs=0,original_clock_rates=0,selected_clock_rates=0;
    double utc_correction_fraction=0;
    // Existing ordinary current-reference frequency authority is retained.
    // This component adds no thermal/cross-time frequency guarantee.
    LinkAuthority frequency_authority=LinkAuthority::declined;
    JointClockSourceRelation source_relation=JointClockSourceRelation::physical_audio_unsteered;
};
namespace joint_clock_detail {
using Interval=modem::detail::clock_prior_detail::Interval;
inline bool finite(Interval x)noexcept{return std::isfinite(x.lower)&&std::isfinite(x.upper)&&x.lower<=x.upper;}
inline double down(double x)noexcept{return std::nextafter(x,-std::numeric_limits<double>::infinity());}
inline double up(double x)noexcept{return std::nextafter(x,std::numeric_limits<double>::infinity());}
inline Interval add(Interval a,Interval b)noexcept {
    return {modem::detail::clock_prior_detail::sum(a.lower,b.lower).lower,
            modem::detail::clock_prior_detail::sum(a.upper,b.upper).upper};
}
inline Interval negate(Interval x)noexcept{return {-x.upper,-x.lower};}
inline Interval subtract(Interval a,Interval b)noexcept{return add(a,negate(b));}
inline Interval multiply(Interval a,Interval b)noexcept {
    if((a.lower==0&&a.upper==0)||(b.lower==0&&b.upper==0))return {};
    const std::array<double,4> p{a.lower*b.lower,a.lower*b.upper,a.upper*b.lower,a.upper*b.upper};
    return {down(*std::min_element(p.begin(),p.end())),up(*std::max_element(p.begin(),p.end()))};
}
inline Interval divide_positive(Interval a,Interval b)noexcept {
    if(!(a.lower>0)||!(b.lower>0))return {1,0};
    return {down(a.lower/b.upper),up(a.upper/b.lower)};
}
inline Interval fraction(Interval ppm)noexcept {
    return {ppm.lower==0?0:down(ppm.lower/1e6),ppm.upper==0?0:up(ppm.upper/1e6)};
}
inline Interval ratio(Interval ppm)noexcept{return add({1,1},fraction(ppm));}
inline Interval ppm(Interval ratio)noexcept{return multiply(subtract(ratio,{1,1}),{1e6,1e6});}
inline Interval intersection(Interval a,Interval b)noexcept{return {std::max(a.lower,b.lower),std::min(a.upper,b.upper)};}
inline Interval clock_cell(double center,double radius,double domain,bool inward)noexcept {
    const auto low=modem::detail::clock_prior_detail::sum(center,-radius);
    const auto high=modem::detail::clock_prior_detail::sum(center,radius);
    return {std::max(-domain,inward?low.upper:low.lower),std::min(domain,inward?high.lower:high.upper)};
}
inline Interval rf_cell(double center,double radius,bool inward)noexcept {
    const auto low=modem::detail::clock_prior_detail::sum(center,-radius);
    const auto high=modem::detail::clock_prior_detail::sum(center,radius);
    return {inward?low.upper:low.lower,inward?high.lower:high.upper};
}
// For d in this slab and c in [cL,cU], the feasible physical-a projection is
// A intersect [(1+dL)/(1+cU)-1,(1+dU)/(1+cL)-1]. Its rectangular hull with d
// can retain infeasible combinations, but cannot lose any feasible (a,d,c).
inline Interval feasible_audio(Interval audio,Interval data,Interval steering)noexcept {
    return intersection(audio,ppm(divide_positive(ratio(data),steering)));
}
inline Interval data_clock(Interval audio,Interval steering)noexcept{return ppm(multiply(ratio(audio),steering));}
inline Interval rf_hull(Interval helper,double data_hz,double helper_hz,Interval audio,Interval data,Interval steering)noexcept {
    const auto feasible=feasible_audio(audio,data,steering);
    if(!finite(feasible))return {1,0};
    return add(helper,subtract(multiply({data_hz,data_hz},fraction(data)),
                               multiply({helper_hz,helper_hz},fraction(feasible))));
}
inline Interval common_source_rf_hull(Interval helper,double data_hz,double helper_hz,Interval data)noexcept {
    // Subtraction itself is enclosed: arbitrary representable D/H need not
    // have an exactly representable difference. d is the SAME coordinate in
    // both terms; multiplying separate independent copies would over-widen.
    return add(helper,multiply(subtract({data_hz,data_hz},{helper_hz,helper_hz}),fraction(data)));
}
} // namespace joint_clock_detail

// Retained optional setup storage; caller must explicitly fund this object AND
// bounded build scratch separately from the clock history/observer grant.
// No heap, payload, PCM, decoder cursor or mutable receiver state is owned.
// Not copyable/movable: borrowed prior views remain tied to this exact owner.
class JointClockCellCertificate {
public:
    JointClockCellCertificate()=default;
    JointClockCellCertificate(const JointClockCellCertificate&)=delete;
    JointClockCellCertificate& operator=(const JointClockCellCertificate&)=delete;
    JointClockCellCertificate(JointClockCellCertificate&&)=delete;
    JointClockCellCertificate& operator=(JointClockCellCertificate&&)=delete;
    const JointClockCellResult& result()const noexcept{return result_;}
    std::optional<modem::PatternAcquisitionClockPrior> prior()const noexcept {
        if(!valid_)return {};
        return modem::PatternAcquisitionClockPrior{support_,{intervals_.data(),interval_count_},
            {mask_.data(),support_.domain.original_pairs.size()},input_end_,evidence_,raw_end_};
    }
    bool build(const modem::Config& config,const JointClockCellRequest& request,
            const PunctuationClockResult& clock,const LinkObservation& observation,
            const LinkModel& frequency_policy,const PunctuationClockDomain& frequency_domain)noexcept {
        using namespace joint_clock_detail;
        valid_=false;result_={};interval_count_=0;mask_.fill(0);
        result_.reason=JointClockCellReason::unsupported_numeric_environment;
        if(!punctuation_numeric::environment())return false;
        const auto& domain=request.scored.domain;const auto pairs=domain.original_pairs;
        const auto& mapping=observation.mapping;
        const auto legacy=domain.legacy_frequency_mask;
        result_.reason=JointClockCellReason::invalid_domain;
        if(!request.raw_domain.capture_generation || !request.raw_domain.rf_gauge || !request.raw_domain.provider_identity ||
           !request.raw_domain.sample_rate || request.raw_domain!=clock.domain || request.raw_domain!=frequency_domain ||
           mapping.capture_generation!=request.raw_domain.capture_generation || mapping.sample_rate!=request.raw_domain.sample_rate ||
           mapping.timebase_identity!=request.raw_domain.input_view || mapping.phase_segment!=request.raw_domain.rf_gauge ||
           config.sample_rate!=domain.sample_rate || config.carrier_hz!=mapping.data_audio_hz)return false;
        result_.reason=JointClockCellReason::unsupported_input_basis;
        if(domain.input_phase_view_id || domain.frequency_origin_hz!=0 ||
           request.input_basis!=JointClockInputBasis::unshifted_fixed_filter_and_nominal_resampler)return false;
        const bool common_source=request.source_relation==JointClockSourceRelation::common_corrected_robust_source;
        if(request.source_relation!=JointClockSourceRelation::physical_audio_unsteered && !common_source)return false;
        // An independent RF oscillator remains physical. Only the audio/source
        // coordinate is b=(1+a)(1+c)-1; individual a and c are NOT set to zero.
        // Shared-radio coupling needs its own proof and retains the old path.
        if(common_source && (frequency_policy.topology!=LinkTopology::independent_audio ||
           !std::isfinite(domain.data_clock_half_width_ppm) || domain.data_clock_half_width_ppm<0 ||
           static_cast<long double>(frequency_policy.audio_accuracy_fraction)*1e6L<domain.data_clock_half_width_ppm))return false;
        result_.source_relation=request.source_relation;
        result_.reason=JointClockCellReason::unqualified_audio_clock;
        if(!clock.qualified_for_acquisition || !clock.affine_aperture ||
           clock.model!=PunctuationClockModel::constant_rate_over_requested_support || !clock.branch_count ||
           clock.branch_count>intervals_.size() || clock.evidence_sample>=request.raw_captured_end_sample)return false;
        result_.reason=JointClockCellReason::unqualified_frequency;
        // Validate the ACTUAL raw observation at its own exclusive observed
        // frontier. A separately owned dated record may retain that original
        // frontier; it never moves the actual clock/marker/support capture end.
        // A copied LinkResult alone cannot supply this publication provenance.
        // No thermal/cross-time model or phase-mean tightening is invented.
        const auto frequency_frontier=request.frequency_observation_frontier.value_or(request.raw_captured_end_sample);
        if(!frequency_frontier || frequency_frontier>request.raw_captured_end_sample)return false;
        // Retrospective authority covers only the fully observed scored
        // aperture at that original date, never later input without an
        // independently justified evolution model. Equality is one future
        // sample because the publication frontier is exclusive.
        if(request.frequency_observation_frontier && request.raw_last_sample>=frequency_frontier) {
            result_.reason=JointClockCellReason::uncovered_scored_support;return false;
        }
        LinkState current_frequency;
        if(!current_frequency.reset(frequency_policy))return false;
        const auto frequency=current_frequency.observe(observation,frequency_frontier);
        if(frequency.authority==LinkAuthority::declined || !frequency.ordinary_domain_valid ||
           !finite({frequency.helper_residual_hz.lower,frequency.helper_residual_hz.upper}) ||
           frequency.evidence_first_sample>frequency.evidence_last_sample ||
           frequency.evidence_last_sample>=frequency_frontier || frequency.state_sample>=frequency_frontier ||
           frequency.evidence_last_sample>=request.raw_captured_end_sample ||
           frequency.state_sample>=request.raw_captured_end_sample ||
           !std::isfinite(mapping.helper_audio_hz) || !std::isfinite(mapping.data_audio_hz) ||
           (mapping.provenance!=LinkMappingProvenance::fixed_configuration &&
            mapping.provenance!=LinkMappingProvenance::modelled_physical_capture &&
            mapping.provenance!=LinkMappingProvenance::confirmed_adapter))return false;
        result_.reason=JointClockCellReason::uncovered_scored_support;
        if(request.scored.first_sample>request.scored.last_sample ||
           request.scored.last_sample>UINT64_MAX-domain.input_sample_origin || request.raw_first_sample>request.raw_last_sample ||
           request.scored.last_sample>=request.input_captured_end_sample ||
           request.scored.consumed_end_sample>request.input_captured_end_sample ||
           request.raw_last_sample>=request.raw_captured_end_sample ||
           clock.requested_first_sample>request.raw_first_sample || clock.requested_last_sample<request.raw_last_sample ||
           mapping.settled_first_sample>request.raw_first_sample || mapping.settled_end_sample<=request.raw_last_sample)return false;
        result_.reason=JointClockCellReason::unsupported_catalogue;
        if(pairs.empty() || pairs.size()>mask_.size() || pairs.size()!=legacy.size() || !domain.symbol_samples ||
           domain.legacy_mask_id!=modem::detail::clock_prior_detail::mask_id(legacy) ||
           !std::isfinite(domain.data_clock_half_width_ppm) || domain.data_clock_half_width_ppm<0 || domain.data_clock_half_width_ppm>10000 ||
           !std::isfinite(domain.clock_cell_half_width_ppm) || domain.clock_cell_half_width_ppm<0 || domain.clock_cell_half_width_ppm>10000 ||
           !std::isfinite(domain.frequency_cell_radius_hz) || domain.frequency_cell_radius_hz<0)return false;
        double correction=0;
        try {
            if(modem::symbol_sample_count(config)!=domain.symbol_samples)return false;
            correction=request.ordinary_utc_steering_enabled?modem::utc_transmit_rate_limit(config):0;
        }catch(...){return false;}
        if(!std::isfinite(correction) || correction<0 || correction>.001)return false;
        result_.utc_correction_fraction=correction;result_.frequency_authority=frequency.authority;
        const Interval steering=add({1,1},{-correction,correction});
        const Interval helper{frequency.helper_residual_hz.lower,frequency.helper_residual_hz.upper};
        std::array<double,modem::maximum_pattern_rate_hypotheses> rates{};
        std::array<Interval,modem::maximum_pattern_rate_hypotheses> inner{};
        std::size_t rate_count=0;
        for(std::size_t f=0;f<pairs.size();++f) {
            if(legacy[f]>1 || !std::isfinite(pairs[f].frequency_offset_hz) ||
               !std::isfinite(pairs[f].clock_error_ppm) || std::abs(pairs[f].clock_error_ppm)>10000)return false;
            result_.original_pairs+=legacy[f]!=0;
            const auto end=rates.begin()+static_cast<std::ptrdiff_t>(rate_count);
            if(std::find(rates.begin(),end,pairs[f].clock_error_ppm)==end) {
                if(rate_count==rates.size())return false;
                rates[rate_count]=pairs[f].clock_error_ppm;
                inner[rate_count++]=clock_cell(pairs[f].clock_error_ppm,domain.clock_cell_half_width_ppm,
                    domain.data_clock_half_width_ppm,true);
            }
        }
        result_.original_clock_rates=rate_count;
        // Retain ALL qualified alias branches. In the default model markers
        // measure physical a and DATA still includes every UTC c. With the
        // explicit common-source relation they measure DATA b itself: adding
        // c AGAIN would double-count the same actual resampler correction.
        for(std::size_t b=0;b<clock.branch_count;++b) {
            const Interval audio{clock.mean_branches[b].lower_ppm,clock.mean_branches[b].upper_ppm};
            if(!finite(audio) || audio.lower< -10000 || audio.upper>10000)return false;
            const auto data=common_source?audio:data_clock(audio,steering);
            result_.reason=JointClockCellReason::uncovered_data_clock;
            if(!finite(data) || data.lower< -domain.data_clock_half_width_ppm || data.upper>domain.data_clock_half_width_ppm)return false;
            intervals_[interval_count_++]={data.lower,data.upper};
            for(std::size_t f=0;f<pairs.size();++f) {
                const auto d=intersection(clock_cell(pairs[f].clock_error_ppm,domain.clock_cell_half_width_ppm,
                    domain.data_clock_half_width_ppm,false),data);
                if(!finite(d))continue;
                const auto required=common_source?
                    common_source_rf_hull(helper,mapping.data_audio_hz,mapping.helper_audio_hz,d):
                    rf_hull(helper,mapping.data_audio_hz,mapping.helper_audio_hz,audio,d,steering);
                const auto rf=rf_cell(pairs[f].frequency_offset_hz,domain.frequency_cell_radius_hz,false);
                if(required.lower>required.upper)continue;
                if(!finite(required) || !finite(rf))return false;
                if(rf.lower<=required.upper && rf.upper>=required.lower)mask_[f]=1;
            }
        }
        result_.reason=JointClockCellReason::legacy_frequency_mask_too_narrow;
        for(std::size_t f=0;f<pairs.size();++f)if(mask_[f] && !legacy[f])return false;
        std::array<std::uint16_t,modem::maximum_pattern_frequency_rate_hypotheses> order{};
        std::size_t selected=0;
        for(std::size_t f=0;f<pairs.size();++f)if(mask_[f])order[selected++]=static_cast<std::uint16_t>(f);
        std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(selected),[&](auto a,auto b){
            return pairs[a].frequency_offset_hz<pairs[b].frequency_offset_hz;});
        result_.selected_pairs=selected;
        for(std::size_t r=0;r<rate_count;++r)for(std::size_t f=0;f<pairs.size();++f)
            if(mask_[f]&&pairs[f].clock_error_ppm==rates[r]){++result_.selected_clock_rates;break;}
        result_.reason=JointClockCellReason::uncovered_joint_cells;
        for(std::size_t b=0;b<interval_count_;++b) {
            const Interval audio{clock.mean_branches[b].lower_ppm,clock.mean_branches[b].upper_ppm};
            const Interval data{intervals_[b].lower_ppm,intervals_[b].upper_ppm};
            std::array<double,2*modem::maximum_pattern_rate_hypotheses+2> endpoints{};std::size_t n=0;
            endpoints[n++]=data.lower;endpoints[n++]=data.upper;
            for(std::size_t r=0;r<rate_count;++r)if(finite(inner[r])) {
                if(inner[r].lower>data.lower && inner[r].lower<data.upper)endpoints[n++]=inner[r].lower;
                if(inner[r].upper>data.lower && inner[r].upper<data.upper)endpoints[n++]=inner[r].upper;
            }
            std::sort(endpoints.begin(),endpoints.begin()+static_cast<std::ptrdiff_t>(n));
            n=static_cast<std::size_t>(std::unique(endpoints.begin(),endpoints.begin()+static_cast<std::ptrdiff_t>(n))-endpoints.begin());
            const auto covered=[&](double low,double high)noexcept {
                const auto required=common_source?
                    common_source_rf_hull(helper,mapping.data_audio_hz,mapping.helper_audio_hz,{low,high}):
                    rf_hull(helper,mapping.data_audio_hz,mapping.helper_audio_hz,audio,{low,high},steering);
                if(required.lower>required.upper)return true; // outward feasibility proves this slab empty
                if(!finite(required))return false;
                auto reached=required.lower;bool any=false;
                for(std::size_t i=0;i<selected;++i) {
                    const auto& pair=pairs[order[i]];
                    const auto cell=clock_cell(pair.clock_error_ppm,domain.clock_cell_half_width_ppm,
                        domain.data_clock_half_width_ppm,true);
                    if(!finite(cell) || cell.lower>low || cell.upper<high)continue;
                    const auto rf=rf_cell(pair.frequency_offset_hz,domain.frequency_cell_radius_hz,true);
                    if(!finite(rf))return false;
                    if(rf.upper<required.lower)continue;
                    if(rf.lower>reached)return false;
                    reached=std::max(reached,rf.upper);any=true;
                    if(reached>=required.upper)return true;
                }
                return any && reached>=required.upper;
            };
            if(n==1){if(!covered(endpoints[0],endpoints[0]))return false;}
            else for(std::size_t i=1;i<n;++i)if(!covered(endpoints[i-1],endpoints[i]))return false;
        }
        result_.reason=JointClockCellReason::no_pair_reduction;
        if(!selected || selected>=result_.original_pairs)return false;
        support_=request.scored;input_end_=request.input_captured_end_sample;
        evidence_=std::max(clock.evidence_sample,frequency.evidence_last_sample);raw_end_=request.raw_captured_end_sample;
        result_.reason=JointClockCellReason::accepted;valid_=true;return true;
    }
private:
    std::array<std::uint8_t,modem::maximum_pattern_frequency_rate_hypotheses> mask_{};
    std::array<modem::PatternAcquisitionClockInterval,PunctuationClockResult::branch_capacity> intervals_{};
    modem::PatternAcquisitionScoredSupport support_;
    JointClockCellResult result_;
    std::size_t interval_count_=0;
    std::uint64_t input_end_=0,evidence_=0,raw_end_=0;
    bool valid_=false;
};
} // namespace datapump::helper
