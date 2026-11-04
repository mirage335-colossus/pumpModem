#pragma once
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
// A single recent aperture is retained, not a searchable history.  The low
// tone is searched once after a causal high-pilot full-aperture fit supplies
// its common displacement.  Neither measurement is a phase certificate here.
class HelperPairedDwell {
    std::unique_ptr<float[]> pcm_,work_;
    std::unique_ptr<helper::ToneTracker> low_;
    std::size_t capacity_=0,size_=0,head_=0;
    std::uint64_t next_=0,generation_=1,work_generation_=0;
    std::uint32_t rate_=0;
    double high_=0,audio_fraction_bound_=0;
    helper::ToneFrequencyObservation high_observation_;
    std::uint64_t phase_segment_=0;
    double actual_rf_hz_=0;
    std::size_t work_size_=0,work_offset_=0;
    bool pending_=false,started_=false,complete_=false;
public:
    static constexpr std::size_t worker_stack_allowance=65536;
    struct Result {
        helper::ToneFrequencyObservation high,low;
        std::uint64_t phase_segment=0;
        double actual_rf_hz=0;
        bool usable=false,kernel_match=false,stale=false;
    };
    static std::size_t reservation_bytes(std::uint32_t rate,double phase_sigma) {
        helper::ToneTrackerConfig c{rate,550,1,phase_sigma,.5,true};
        const auto samples=(static_cast<std::uint64_t>(rate)+1)/2;
        if(samples>SIZE_MAX/(2*sizeof(float)))throw Error("paired pilot capture is too large");
        const auto tracker=helper::ToneTracker::workspace_required(c);
        if(tracker>SIZE_MAX-2*samples*sizeof(float)-sizeof(HelperPairedDwell)-worker_stack_allowance)throw Error("paired pilot workspace overflow");
        return sizeof(HelperPairedDwell)+tracker+2*samples*sizeof(float)+worker_stack_allowance;
    }
    HelperPairedDwell(std::uint32_t rate,std::uint64_t first,double high,double audio_fraction_bound,double phase_sigma):
        capacity_((static_cast<std::uint64_t>(rate)+1)/2),next_(first),rate_(rate),high_(high),audio_fraction_bound_(audio_fraction_bound) {
        if(!rate_||!std::isfinite(high_)||high_<=550||!std::isfinite(audio_fraction_bound_)||audio_fraction_bound_<0)throw Error("invalid paired pilot geometry");
        helper::ToneTrackerConfig c{rate,550,1,phase_sigma,.5,true};
        pcm_=std::make_unique<float[]>(capacity_);
        work_=std::make_unique<float[]>(capacity_);
        low_=std::make_unique<helper::ToneTracker>(c,helper::ToneTracker::workspace_required(c),first);
    }
    std::size_t workspace_bytes()const noexcept {
        return sizeof(*this)+2*capacity_*sizeof(float)+(low_?low_->workspace_bytes():0);
    }
    // Capture may reset while a worker owns its immutable snapshot. Never
    // mutate the worker's tracker here; the generation invalidates its result.
    void reset(std::uint64_t first)noexcept {size_=head_=0;next_=first;if(generation_!=UINT64_MAX)++generation_;}
    void push(std::span<const float> samples,std::uint64_t first) {
        if(first!=next_||samples.size()>UINT64_MAX-next_)throw Error("paired pilot raw frontier mismatch");
        for(float sample:samples){if(size_==capacity_){head_=(head_+1)%capacity_;--size_;}pcm_[(head_+size_)%capacity_]=sample;++size_;++next_;}
    }
    bool submit(const helper::ToneFrequencyObservation& high,std::uint64_t phase_segment,double actual_rf_hz) {
        if(pending_ || generation_==UINT64_MAX || !phase_segment || !std::isfinite(actual_rf_hz) || actual_rf_hz<=0 ||
           !high.valid || !rate_ || high.first_sample>=high.end_sample || high.end_sample>next_ || high.first_sample<next_-size_ ||
           !std::isfinite(high.frequency_hz)||!std::isfinite(high.independent_variance_hz2)||high.independent_variance_hz2<=0||
           !std::isfinite(high.residual_variance_hz2)||high.residual_variance_hz2<0)return false;
        // Same common displacement; retain the complete declared effective
        // audio-scale box and uncertainty of the actual high measurement.
        const auto center=550+(high.frequency_hz-high_);
        const auto half=std::nextafter(std::max(15.,3*std::sqrt(high.independent_variance_hz2+high.residual_variance_hz2)+
            std::abs(high_-550)*audio_fraction_bound_+1.),INFINITY);
        // The tracker samples noise at ±200 Hz as well as the searched pilot.
        // This proves digital capture clearance only; radio passband remains
        // an independent physical limitation.
        if(!std::isfinite(center)||!std::isfinite(half)||center-half-200<20||center+half+200>rate_/2.-20||half>=190)return false;
        const auto offset=static_cast<std::size_t>(high.first_sample-(next_-size_));
        const auto index=(head_+offset)%capacity_;work_size_=static_cast<std::size_t>(high.end_sample-high.first_sample);
        const auto first=std::min(work_size_,capacity_-index);
        std::copy_n(pcm_.get()+index,first,work_.get());
        std::copy_n(pcm_.get(),work_size_-first,work_.get()+first);
        high_observation_=high;phase_segment_=phase_segment;actual_rf_hz_=actual_rf_hz;
        work_generation_=generation_;work_offset_=0;pending_=true;started_=complete_=false;
        return true;
    }
    bool pending()const noexcept{return pending_;}
    bool stale()const noexcept{return pending_ && work_generation_!=generation_;}
    std::uint64_t worker_first_sample()const noexcept{return high_observation_.first_sample;}
    std::uint64_t worker_end_sample()const noexcept{return high_observation_.end_sample;}
    std::uint64_t worker_phase_segment()const noexcept{return phase_segment_;}
    // Only the optional worker calls advance, outside the capture lock. Each
    // call is bounded by a raw-sample quantum; the final tracker fit remains a
    // single measured quantum and is charged as such by the caller.
    bool advance(std::size_t samples) {
        if(!pending_ || complete_ || !samples)return false;
        if(!started_) {
            const auto& high=high_observation_;
            const auto center=550+(high.frequency_hz-high_);
            const auto half=std::nextafter(std::max(15.,3*std::sqrt(high.independent_variance_hz2+high.residual_variance_hz2)+
                std::abs(high_-550)*audio_fraction_bound_+1.),INFINITY);
            low_->reset(high.first_sample);
            low_->begin_pilot_segment(high.first_sample,center,half,false,1,high.first_sample,true,high.end_sample);
            started_=true;
        }
        const auto count=std::min(samples,work_size_-work_offset_);
        low_->push(std::span<const float>(work_.get()+work_offset_,count),high_observation_.first_sample+work_offset_);
        work_offset_+=count;complete_=work_offset_==work_size_;
        return count!=0;
    }
    bool complete()const noexcept{return complete_;}
    // Called only with the capture lock held after joining any worker quantum.
    void discard()noexcept {pending_=false;started_=complete_=false;}
    Result take()noexcept {
        Result out;if(!pending_ || !complete_)return out;
        out.high=high_observation_;out.phase_segment=phase_segment_;out.actual_rf_hz=actual_rf_hz_;
        out.stale=work_generation_!=generation_||out.high.end_sample>next_;
        if(!out.stale) {
            out.low=low_->snapshot().frequency_observation;
            const auto& a=out.high;const auto& b=out.low;
            out.kernel_match=b.valid&&a.first_sample==b.first_sample&&a.end_sample==b.end_sample&&
                a.state_endpoint_sample==b.state_endpoint_sample&&a.mean_sample==b.mean_sample&&
                a.mean_sample_fraction==b.mean_sample_fraction&&a.mass==b.mass&&a.mu_seconds==b.mu_seconds&&
                a.integral_k_seconds2==b.integral_k_seconds2&&a.integral_b_minus_s_k_seconds3==b.integral_b_minus_s_k_seconds3&&
                a.integral_k_squared_seconds3==b.integral_k_squared_seconds3&&a.k_at_end_seconds==b.k_at_end_seconds&&
                a.k_prime_at_end==b.k_prime_at_end;
            out.usable=out.kernel_match;
        }
        pending_=false;started_=complete_=false;return out;
    }
};
}
