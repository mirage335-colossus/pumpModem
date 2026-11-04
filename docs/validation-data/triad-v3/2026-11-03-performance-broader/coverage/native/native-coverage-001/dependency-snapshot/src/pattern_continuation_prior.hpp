#pragma once
#include "datapump/pattern_receiver.hpp"
#include "pattern_fft_clock_prior.hpp"

namespace datapump::modem::detail {
struct PatternContinuationFrequency {
    std::span<const std::uint8_t> mask;
    PatternContinuationFrequencyResult* work=nullptr;
    bool keep(std::size_t f)const noexcept{return f<mask.size() && mask[f]!=0;}
};
inline std::optional<PatternContinuationFrequency> pattern_continuation_frequency_prior(
        const PatternContinuationFrequencyPrior& prior,const PatternContinuationScoredSupport& current,
        PatternContinuationFrequencyResult& result) noexcept {
    result={};result.status=PatternContinuationFrequencyStatus::invalid_reference;
    const auto& issued=prior.support();const auto& s=current.input;const auto& domain=s.domain;
    const auto mask=prior.pair_mask();
    if(!helper::punctuation_numeric::environment() || !prior.reference_observed_end_sample() ||
       prior.raw_last_sample()>=prior.reference_observed_end_sample())return {};
    result.status=PatternContinuationFrequencyStatus::domain_changed;
    if(!clock_prior_detail::same_domain(issued.input.domain,domain) || issued.ring_bins!=current.ring_bins ||
       issued.input.first_sample!=s.first_sample || issued.input.last_sample!=s.last_sample ||
       issued.input.consumed_end_sample!=s.consumed_end_sample || s.first_sample>s.last_sample ||
       s.last_sample>UINT64_MAX-domain.input_sample_origin || s.last_sample>=prior.input_captured_end_sample() ||
       s.consumed_end_sample>prior.input_captured_end_sample())return {};
    result.status=PatternContinuationFrequencyStatus::invalid_pair_subset;
    const auto pairs=domain.original_pairs;
    if(pairs.empty() || pairs.size()!=mask.size() || pairs.size()>maximum_pattern_frequency_rate_hypotheses ||
       !domain.legacy_frequency_mask.empty() || domain.legacy_mask_id || domain.frequency_origin_hz!=0 ||
       std::signbit(domain.frequency_origin_hz) || domain.input_phase_view_id || !domain.sample_rate ||
       !std::isfinite(domain.data_clock_half_width_ppm) || domain.data_clock_half_width_ppm<0 || domain.data_clock_half_width_ppm>10000 ||
       !std::isfinite(domain.clock_cell_half_width_ppm) || domain.clock_cell_half_width_ppm<0 || domain.clock_cell_half_width_ppm>10000 ||
       !std::isfinite(domain.frequency_cell_radius_hz) || domain.frequency_cell_radius_hz<0)return {};
    std::array<double,maximum_pattern_rate_hypotheses> rates{};
    std::array<bool,maximum_pattern_rate_hypotheses> retained{};
    std::size_t count=0,selected=0;
    for(std::size_t f=0;f<pairs.size();++f) {
        if(mask[f]>1 || !std::isfinite(pairs[f].frequency_offset_hz) || !std::isfinite(pairs[f].clock_error_ppm) || std::abs(pairs[f].clock_error_ppm)>10000)return {};
        std::size_t at=0;while(at<count && rates[at]!=pairs[f].clock_error_ppm)++at;
        if(at==count){if(count==rates.size())return {};rates[count++]=pairs[f].clock_error_ppm;}
        if(mask[f]){retained[at]=true;++selected;}
    }
    if(!selected || selected==pairs.size() || std::any_of(retained.begin(),retained.begin()+count,[](bool b){return !b;}))return {};
    result.status=PatternContinuationFrequencyStatus::active;
    return PatternContinuationFrequency{mask,&result};
}
}
