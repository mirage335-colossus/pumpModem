#pragma once
#include "datapump/pattern_receiver.hpp"
#include "helper_punctuation_numeric.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace datapump::modem::detail {
// Synchronous immutable selector; never retained by Impl/pending/track/worker.
// Original private geometry, trials and warm/END scoring remain authoritative.
struct PatternFftClockPrior {
    std::span<const std::uint8_t> mask;
    bool keep(std::size_t pair)const noexcept{return pair<mask.size() && mask[pair]!=0;}
};
namespace clock_prior_detail {
struct Interval {double lower=0,upper=0;};
inline Interval sum(double a,double b) noexcept {
    const auto value=a+b,bv=value-a,error=(a-(value-bv))+(b-bv);
    const auto inf=std::numeric_limits<double>::infinity();
    return {error<0?std::nextafter(value,-inf):value,error>0?std::nextafter(value,inf):value};
}
inline Interval cell(double center,double radius,double domain) noexcept {
    const auto lower=sum(center,-radius),upper=sum(center,radius);
    return {std::max(-domain,lower.upper),std::min(domain,upper.lower)};
}
inline std::uint64_t mask_id(std::span<const std::uint8_t> mask)noexcept {
    std::uint64_t id=1469598103934665603ULL;
    for(const auto bit:mask){id^=bit;id*=1099511628211ULL;}
    id^=mask.size();id*=1099511628211ULL;return id;
}
inline bool same_domain(const PatternAcquisitionClockDomain& a,const PatternAcquisitionClockDomain& b)noexcept {
    return a.original_pairs.data()==b.original_pairs.data() && a.original_pairs.size()==b.original_pairs.size() &&
        a.legacy_frequency_mask.data()==b.legacy_frequency_mask.data() &&
        a.legacy_frequency_mask.size()==b.legacy_frequency_mask.size() &&
        a.legacy_mask_id==b.legacy_mask_id && a.input_sample_origin==b.input_sample_origin &&
        a.input_phase_view_id==b.input_phase_view_id && a.symbol_samples==b.symbol_samples &&
        a.sample_rate==b.sample_rate && a.frequency_origin_hz==b.frequency_origin_hz &&
        a.frequency_cell_radius_hz==b.frequency_cell_radius_hz &&
        a.data_clock_half_width_ppm==b.data_clock_half_width_ppm &&
        a.clock_cell_half_width_ppm==b.clock_cell_half_width_ppm;
}
} // namespace clock_prior_detail

inline std::optional<PatternFftClockPrior> pattern_fft_clock_prior(
        const PatternAcquisitionClockPrior& prior,PatternAcquisitionScoredSupport support,
        PatternAcquisitionClockResult& result) noexcept {
    using namespace clock_prior_detail;
    result={};result.status=PatternAcquisitionClockStatus::invalid_reference;
    const auto intervals=prior.intervals();const auto mask=prior.pair_mask();
    if(!helper::punctuation_numeric::environment() || intervals.empty() || intervals.size()>64 ||
       prior.reference_evidence_sample()>=prior.reference_captured_end_sample())return {};
    result.status=PatternAcquisitionClockStatus::domain_changed;
    const auto& domain=support.domain;
    const auto& issued=prior.scored_support();
    if(!same_domain(issued.domain,domain) || domain.legacy_mask_id!=mask_id(domain.legacy_frequency_mask))return {};
    result.status=PatternAcquisitionClockStatus::uncovered_scored_support;
    if(issued.first_sample!=support.first_sample || issued.last_sample!=support.last_sample ||
       issued.consumed_end_sample!=support.consumed_end_sample || support.first_sample>support.last_sample ||
       support.last_sample>UINT64_MAX-domain.input_sample_origin ||
       support.last_sample>=prior.captured_input_end_sample() ||
       support.consumed_end_sample>prior.captured_input_end_sample())return {};
    result.status=PatternAcquisitionClockStatus::unsupported_clock_domain;
    const auto pairs=domain.original_pairs;const auto legacy=domain.legacy_frequency_mask;
    const auto physical=domain.data_clock_half_width_ppm,radius=domain.clock_cell_half_width_ppm;
    if(!domain.sample_rate || !domain.symbol_samples || domain.input_phase_view_id || domain.frequency_origin_hz!=0 ||
       !std::isfinite(physical) || physical<0 || physical>10000 || !std::isfinite(radius) || radius<0 || radius>10000 ||
       !std::isfinite(domain.frequency_cell_radius_hz) || domain.frequency_cell_radius_hz<0 || pairs.empty() ||
       pairs.size()!=mask.size() || pairs.size()!=legacy.size() || pairs.size()>maximum_pattern_frequency_rate_hypotheses)return {};
    for(const auto& branch:intervals)
        if(!std::isfinite(branch.lower_ppm) || !std::isfinite(branch.upper_ppm) || branch.lower_ppm>branch.upper_ppm ||
           branch.lower_ppm< -physical || branch.upper_ppm>physical)return {};
    std::array<double,maximum_pattern_rate_hypotheses> original{},selected{};std::size_t original_count=0,selected_count=0;
    for(std::size_t f=0;f<pairs.size();++f) {
        result.status=PatternAcquisitionClockStatus::invalid_pair_subset;
        if(mask[f]>1 || legacy[f]>1 || (mask[f] && !legacy[f]) ||
           !std::isfinite(pairs[f].frequency_offset_hz) || !std::isfinite(pairs[f].clock_error_ppm) ||
           std::abs(pairs[f].clock_error_ppm)>10000)return {};
        const auto add=[&](auto& rates,std::size_t& count)->bool {
            const auto end=rates.begin()+static_cast<std::ptrdiff_t>(count);
            if(std::find(rates.begin(),end,pairs[f].clock_error_ppm)!=end)return true;
            if(count==rates.size())return false;
            rates[count++]=pairs[f].clock_error_ppm;return true;
        };
        if(legacy[f]){++result.original_pairs;if(!add(original,original_count))return {};}
        if(mask[f]){++result.selected_pairs;if(!add(selected,selected_count))return {};}
    }
    result.original_clock_rates=original_count;result.selected_clock_rates=selected_count;
    std::sort(selected.begin(),selected.begin()+static_cast<std::ptrdiff_t>(selected_count));
    result.status=PatternAcquisitionClockStatus::uncovered_clock_interval;
    // Independently verify the issued DATA-LF union is covered by selected
    // ORIGINAL cells. No tolerance bridges a gap; exact touching stays exact.
    // The trusted builder separately proves joint RF physics/coverage.
    for(const auto& branch:intervals) {
        auto covered=branch.lower_ppm;bool reached=false;
        for(std::size_t i=0;i<selected_count;++i) {
            const auto span=cell(selected[i],radius,physical);
            if(span.upper<span.lower || span.upper<covered)continue;
            if(span.lower>covered)break;
            covered=std::max(covered,span.upper);reached=true;
            if(covered>=branch.upper_ppm)break;
        }
        if(!reached || covered<branch.upper_ppm)return {};
    }
    if(!result.selected_pairs || result.selected_pairs>=result.original_pairs) {
        result.status=PatternAcquisitionClockStatus::no_clock_reduction;return {};
    }
    result.status=PatternAcquisitionClockStatus::active;return PatternFftClockPrior{mask};
}
} // namespace datapump::modem::detail
