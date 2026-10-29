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
    std::unique_ptr<float[]> pcm_;
    std::unique_ptr<helper::ToneTracker> low_;
    std::size_t capacity_=0,size_=0,head_=0;
    std::uint64_t next_=0,last_attempt_end_=0;
    std::uint32_t rate_=0;
    double high_=0,audio_fraction_bound_=0;
public:
    static std::size_t reservation_bytes(std::uint32_t rate,double phase_sigma) {
        helper::ToneTrackerConfig c{rate,550,1,phase_sigma,.5,true};
        const auto samples=(static_cast<std::uint64_t>(rate)+1)/2;
        if(samples>SIZE_MAX/sizeof(float))throw Error("paired pilot capture is too large");
        const auto tracker=helper::ToneTracker::workspace_required(c);
        if(tracker>SIZE_MAX-samples*sizeof(float)-sizeof(HelperPairedDwell))throw Error("paired pilot workspace overflow");
        return sizeof(HelperPairedDwell)+tracker+samples*sizeof(float);
    }
    HelperPairedDwell(std::uint32_t rate,std::uint64_t first,double high,double audio_fraction_bound,double phase_sigma):
        capacity_((static_cast<std::uint64_t>(rate)+1)/2),next_(first),rate_(rate),high_(high),audio_fraction_bound_(audio_fraction_bound) {
        if(!rate_||!std::isfinite(high_)||high_<=550||!std::isfinite(audio_fraction_bound_)||audio_fraction_bound_<0)throw Error("invalid paired pilot geometry");
        helper::ToneTrackerConfig c{rate,550,1,phase_sigma,.5,true};
        pcm_=std::make_unique<float[]>(capacity_);
        low_=std::make_unique<helper::ToneTracker>(c,helper::ToneTracker::workspace_required(c),first);
    }
    void reset(std::uint64_t first)noexcept {size_=head_=0;next_=first;last_attempt_end_=0;low_->reset(first);}
    void push(std::span<const float> samples,std::uint64_t first) {
        if(first!=next_||samples.size()>UINT64_MAX-next_)throw Error("paired pilot raw frontier mismatch");
        for(float sample:samples){if(size_==capacity_){head_=(head_+1)%capacity_;--size_;}pcm_[(head_+size_)%capacity_]=sample;++size_;++next_;}
    }
    std::optional<helper::ToneFrequencyObservation> measure(const helper::ToneFrequencyObservation& high) {
        if(!high.valid || !rate_ || high.first_sample>=high.end_sample || high.end_sample>next_ || high.first_sample<next_-size_ || high.end_sample==last_attempt_end_ ||
           !std::isfinite(high.frequency_hz)||!std::isfinite(high.independent_variance_hz2)||high.independent_variance_hz2<=0||
           !std::isfinite(high.residual_variance_hz2)||high.residual_variance_hz2<0)return {};
        last_attempt_end_=high.end_sample;
        // Same common displacement; retain the complete declared effective
        // audio-scale box and uncertainty of the actual high measurement.
        const auto center=550+(high.frequency_hz-high_);
        const auto half=std::nextafter(std::max(15.,3*std::sqrt(high.independent_variance_hz2+high.residual_variance_hz2)+
            std::abs(high_-550)*audio_fraction_bound_+1.),INFINITY);
        if(!std::isfinite(center)||!std::isfinite(half)||center-half<20||center+half>rate_/2.-20||half>=190)return {};
        low_->reset(high.first_sample);
        low_->begin_pilot_segment(high.first_sample,center,half,false,1,high.first_sample,true,high.end_sample);
        auto offset=static_cast<std::size_t>(high.first_sample-(next_-size_));
        auto index=(head_+offset)%capacity_;auto remaining=static_cast<std::size_t>(high.end_sample-high.first_sample);auto sample=high.first_sample;
        while(remaining){const auto count=std::min(remaining,capacity_-index);low_->push(std::span<const float>(pcm_.get()+index,count),sample);sample+=count;remaining-=count;index=(index+count)%capacity_;}
        auto out=low_->snapshot().frequency_observation;
        return out.valid?std::optional<helper::ToneFrequencyObservation>{out}:std::nullopt;
    }
};
}
