#pragma once
#include "live_helper_frontend.hpp"
#include "live_helper_score_frequency.hpp"

namespace datapump::live::detail {
// The real q=0 translator still emits at_delay(group_delay). The downstream
// FIR/resampler dependency dates are translator-output dates, so subtract this
// fixed delay from BOTH endpoints. There is no quadrature/Hilbert aperture to
// erode on this branch. The existing complete FIR/resampler padding remains.
inline std::optional<std::array<std::uint64_t,2>> helper_raw_frequency_support(
        const HelperViewBlock& block)noexcept {
    constexpr auto delay=helper::AnalyticFrequencyTranslator::group_delay;
    if(!block.view || !block.view->physical_rate || !block.view->logical_rate || !block.view->reference_passthrough || block.view->original_prior_retained ||
       block.view->conjugate || block.data_parameter_domain ||
       block.applied_correction_hz.lower_hz!=0 || block.applied_correction_hz.upper_hz!=0 ||
       block.corrected_dependency_first<delay ||
       block.corrected_dependency_first>=block.corrected_dependency_end ||
       block.corrected_dependency_end>block.raw_captured_end)return {};
    return std::array{block.corrected_dependency_first-delay,block.corrected_dependency_end-delay};
}
// This measurement is queried on the actual captured raw interval. It is not a
// latest-reference projection, phase/clock proof, or an actual-retune mapping.
inline std::optional<HelperFrequencyInterval> helper_raw_frequency_interval(
        const HelperViewBlock& block,const helper::ToneFrequencyInterval& measured)noexcept {
    const auto support=helper_raw_frequency_support(block);
    if(!support || !measured.valid || measured.first_sample!=(*support)[0] ||
       measured.end_sample!=(*support)[1] || measured.captured_end_sample!=block.raw_captured_end ||
       measured.evidence_first_sample>=measured.evidence_end_sample ||
       measured.evidence_end_sample>block.raw_captured_end ||
       !std::isfinite(measured.frequency_hz) || measured.frequency_hz<=0 ||
       !std::isfinite(measured.uncertainty_hz) || measured.uncertainty_hz<0)return {};
    return HelperFrequencyInterval{measured.frequency_hz,measured.uncertainty_hz,
        measured.first_sample,measured.end_sample-1,false,
        "dated fixed-RF raw pilot frequency; unchanged real PCM and q=0"};
}
// Raw real PCM has M=f_helper-H, rather than a fabricated zero-centered
// correction. The ordinary selector separately retains (H-D)*B and all LF/UTC.
inline std::optional<std::array<double,2>> helper_raw_frequency_m_bound(
        const HelperViewBlock& block,double expected,std::uint64_t rejection_first)noexcept {
    const auto support=helper_raw_frequency_support(block);
    const auto& measured=block.frequency_interval;
    if(!support || !measured || !std::isfinite(expected) || expected<=0 ||
       block.view->helper_hz!=expected || (rejection_first && (*support)[0]<rejection_first) ||
       measured->first_sample!=(*support)[0] || measured->last_sample!=(*support)[1]-1 ||
       measured->phase_mean || !std::isfinite(measured->frequency_hz) || measured->frequency_hz<=0 ||
       !std::isfinite(measured->uncertainty_hz) || measured->uncertainty_hz<0)return {};
    using helper_prior_detail::sum;
    const auto center=sum(measured->frequency_hz,-expected);
    const auto lower=sum(center.lower,-measured->uncertainty_hz).lower;
    const auto upper=sum(center.upper,measured->uncertainty_hz).upper;
    if(!std::isfinite(lower) || !std::isfinite(upper) || lower>upper)return {};
    return std::array{lower,upper};
}
}
