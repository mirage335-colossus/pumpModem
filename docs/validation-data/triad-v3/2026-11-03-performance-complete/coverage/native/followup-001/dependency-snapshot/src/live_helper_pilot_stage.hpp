#pragma once

#include "datapump/helper_frontend.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace datapump::live::detail {

// Actual raw lookahead, separately prepaid from the frontend's logical output
// queue. This is one bounded FIFO, not a searchable PCM history. Once a view
// owns input, every sample leaves through drain(), including reference loss.
class HelperPilotPcmStage {
    std::unique_ptr<float[]> samples_;
    std::size_t capacity_=0,head_=0,size_=0;
    std::uint64_t next_=0;
public:
    static constexpr double maximum_seconds=.4;
    static std::size_t sample_capacity(std::uint32_t rate) {
        if(!rate)throw Error("pilot stage has no physical sample clock");
        return (static_cast<std::uint64_t>(rate)*2+4)/5;
    }
    static std::size_t reservation_bytes(std::uint32_t rate) {
        const auto count=sample_capacity(rate);
        if(count>SIZE_MAX/sizeof(float))throw Error("pilot stage exceeds its finite workspace");
        return count*sizeof(float);
    }
    HelperPilotPcmStage(std::uint32_t rate,std::uint64_t first):
        samples_(std::make_unique<float[]>(sample_capacity(rate))),capacity_(sample_capacity(rate)),next_(first) {}
    std::size_t workspace_bytes()const noexcept{return capacity_*sizeof(float);}
    std::size_t size()const noexcept{return size_;}
    std::uint64_t first_sample()const noexcept{return next_-size_;}
    std::uint64_t captured_end_sample()const noexcept{return next_;}
    void reset(std::uint64_t first)noexcept{head_=size_=0;next_=first;}
    template<class Consume> void drain(std::uint64_t end,Consume&& consume) {
        end=std::min(end,next_);
        while(size_ && first_sample()<end) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(
                std::min(size_,capacity_-head_),end-first_sample()));
            consume(std::span<const float>(samples_.get()+head_,count),first_sample());
            head_=(head_+count)%capacity_;size_-=count;
        }
    }
    // Caller has already observed the incoming chunk. A committed view drains
    // old input at the .4 s deadline; only pre-admission waiting may forget it.
    template<class Consume> void make_room(std::size_t incoming,bool committed,Consume&& consume) {
        if(incoming>capacity_)throw Error("pilot observation chunk exceeds the bounded raw stage");
        if(incoming<=capacity_-size_)return;
        const auto remove=incoming-(capacity_-size_);
        if(committed)drain(first_sample()+remove,consume);
        else {head_=(head_+remove)%capacity_;size_-=remove;}
    }
    void append(std::span<const float> input,std::uint64_t first) {
        if(first!=next_ || input.size()>capacity_-size_ || input.size()>UINT64_MAX-next_)
            throw Error("pilot stage input has a gap or exceeds its prepaid bound");
        for(const auto sample:input) {samples_[(head_+size_)%capacity_]=sample;++size_;}
        next_+=input.size();
    }
};

// Projection phase is diagnostic only. A local independent context supplies
// both phase and slope, with a conservative endpoint phase-error gate. Pooled
// frequency records, metadata, and elapsed lead grant no phase authority.
inline std::optional<helper::AnalyticFrequencyTranslator::PilotObservation> helper_pilot_phase_observation(
        const helper::ToneTrackerSnapshot& tracked,double expected,std::uint32_t rate)noexcept {
    if(!rate || tracked.sample_rate!=rate || !tracked.phase_fit_qualified ||
       !std::isfinite(expected) || !std::isfinite(tracked.phase_fit_cycles) ||
       !std::isfinite(tracked.phase_fit_uncertainty_cycles) || tracked.phase_fit_uncertainty_cycles<0 ||
       !std::isfinite(tracked.phase_fit_frequency_hz) ||
       !std::isfinite(tracked.phase_fit_frequency_uncertainty_hz) || tracked.phase_fit_frequency_uncertainty_hz<0 ||
       !std::isfinite(tracked.phase_fit_sample_fraction) || tracked.phase_fit_sample_fraction<0 ||
       tracked.phase_fit_sample_fraction>=1 || tracked.phase_fit_first_sample>=tracked.phase_fit_end_sample ||
       tracked.phase_fit_first_sample>UINT64_MAX-2*helper::AnalyticFrequencyTranslator::group_delay ||
       tracked.phase_fit_end_sample-tracked.phase_fit_first_sample<helper::AnalyticFrequencyTranslator::tap_count ||
       tracked.phase_fit_end_sample>tracked.captured_end_sample ||
       tracked.phase_fit_sample<tracked.phase_fit_first_sample || tracked.phase_fit_sample>=tracked.phase_fit_end_sample)
        return {};
    const auto left=static_cast<double>(tracked.phase_fit_sample-tracked.phase_fit_first_sample)+tracked.phase_fit_sample_fraction;
    const auto right=static_cast<double>(tracked.phase_fit_end_sample-tracked.phase_fit_sample)-tracked.phase_fit_sample_fraction;
    const auto uncertainty=tracked.phase_fit_support_uncertainty_cycles.value_or(
        tracked.phase_fit_uncertainty_cycles+tracked.phase_fit_frequency_uncertainty_hz*std::max(left,right)/rate);
    if(!std::isfinite(uncertainty) || uncertainty<0 || uncertainty>=.125)
        return {};
    return helper::AnalyticFrequencyTranslator::PilotObservation{expected,tracked.phase_fit_frequency_hz,
        tracked.phase_fit_cycles,tracked.phase_fit_sample,tracked.phase_fit_sample_fraction,
        tracked.phase_fit_first_sample,tracked.phase_fit_end_sample,tracked.captured_end_sample};
}

struct HelperPilotFrequencyPlan {
    bool valid=false;
    double tracking_frequency_hz=0,residual_uncertainty_hz=0;
};
// A constant NCO slope over one short produced raw interval. Endpoint
// covariance bounds and deterministic slope variation enclose every delayed
// input center; no current estimate is silently redated onto buffered PCM.
inline HelperPilotFrequencyPlan helper_pilot_frequency_plan(const helper::ToneTrackerSnapshot& model,
        std::uint64_t first_output,std::uint64_t end_output)noexcept {
    HelperPilotFrequencyPlan result;
    constexpr auto delay=helper::AnalyticFrequencyTranslator::group_delay;
    if(first_output<delay || first_output>=end_output)return result;
    const auto first=first_output-delay,last=end_output-1-delay;
    const auto middle=first+(last-first)/2;
    const auto fraction=.5*((last-first)%2);
    const auto a=model.frequency_at(first),b=model.frequency_at(last),center=model.frequency_at(middle,fraction);
    if(!a.valid || !b.valid || !center.valid)return result;
    result.tracking_frequency_hz=center.frequency_hz;
    result.residual_uncertainty_hz=std::nextafter(std::max(
        a.uncertainty_hz+std::abs(a.frequency_hz-center.frequency_hz),
        b.uncertainty_hz+std::abs(b.frequency_hz-center.frequency_hz)),std::numeric_limits<double>::infinity());
    result.valid=std::isfinite(result.residual_uncertainty_hz);return result;
}
}
