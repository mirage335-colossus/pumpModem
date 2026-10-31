#include "datapump/helper_inversion_timing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::helper {
namespace {
HelperInversionTiming::Config checked(HelperInversionTiming::Config c){
    if(c.sample_rate<8000||c.sample_rate>96000||!std::isfinite(c.auxiliary_hz)||
       !std::isfinite(c.high_hz)||!std::isfinite(c.low_hz)||c.low_hz<=0||
       c.auxiliary_hz<=c.low_hz||c.high_hz<=c.auxiliary_hz||c.high_hz>=c.sample_rate/2.)
        throw std::invalid_argument("invalid Helper inversion geometry");
    return c;
}
}
HelperInversionTiming::HelperInversionTiming(Config c):config_(checked(c)),
    receiver_(config_.sample_rate,config_.auxiliary_hz,config_.high_hz,{}, {},
        AuxiliaryMetadataReceiver::workspace_required(config_.sample_rate),0,{},config_.low_hz){}
void HelperInversionTiming::observe(std::span<const std::complex<float>> samples,std::uint64_t first){
    if(samples.empty())return;
    if(samples.size()>static_cast<std::size_t>(config_.sample_rate)*2/5||
       first>std::numeric_limits<std::uint64_t>::max()-samples.size()||
       (started_&&first<next_))throw std::invalid_argument("invalid Helper inversion capture extent");
    for(const auto z:samples)if(!std::isfinite(z.real())||!std::isfinite(z.imag()))
        throw std::invalid_argument("nonfinite Helper inversion input");
    // advance() publishes at most one update per bounded processing chunk.
    if(started_&&first==next_&&pending_count_+(samples.size()+real_.size()-1)/real_.size()>pending_.size())
        throw std::length_error("drain Helper inversion observations before supplying more input");
    if(!started_||first!=next_)missing(first);
    for(std::size_t at=0;at<samples.size();){
        const auto n=std::min(real_.size(),samples.size()-at);
        for(std::size_t k=0;k<n;++k)real_[k]=samples[at+k].real();
        receiver_.push(std::span<const float>(real_).first(n),first+at);
        receiver_.advance();
        // Metadata success is neither queried nor required for the marker.
        const auto marker=receiver_.take_punctuation_update();
        if(marker.known){pending_[(pending_first_+pending_count_)%pending_.size()]=marker;++pending_count_;++distinct_markers_;}
        at+=n;
    }
    next_=first+samples.size();
}
void HelperInversionTiming::missing(std::uint64_t next){
    if(started_&&next<next_)throw std::invalid_argument("backward Helper inversion gap");
    receiver_.reset(next);pending_first_=pending_count_=0;next_=next;started_=true;
}
PunctuationObservation HelperInversionTiming::take_update() noexcept{
    if(!pending_count_)return {};
    const auto out=pending_[pending_first_];pending_first_=(pending_first_+1)%pending_.size();--pending_count_;return out;
}
std::size_t HelperInversionTiming::workspace_bytes() const noexcept{
    // Includes the prepaid detector storage and this adapter; external runtime
    // allocator/library overhead is not a claimed part of that fixed grant.
    return sizeof(*this)+AuxiliaryMetadataReceiver::workspace_required(config_.sample_rate);
}
}
