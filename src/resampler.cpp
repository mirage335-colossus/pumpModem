#include "datapump/resampler.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::audio {
Resampler::Resampler(std::uint32_t input_rate,std::uint32_t output_rate)
    :input_rate_(input_rate),output_rate_(output_rate) {
    if(input_rate<64 || input_rate>120000000 || output_rate<64 || output_rate>120000000)
        throw Error("resampler rate must be 64..120000000 Hz");
    if(input_rate==output_rate)return;
    if(static_cast<std::uint64_t>(input_rate)>static_cast<std::uint64_t>(output_rate)*4) {
        auto intermediate_rate=output_rate;
        while(static_cast<std::uint64_t>(intermediate_rate)*4<input_rate)intermediate_rate*=4;
        first_stage_=std::make_unique<Resampler>(input_rate,intermediate_rate);
        last_stage_=std::make_unique<Resampler>(intermediate_rate,output_rate);
        intermediate_.resize(256);
        return;
    }
    const double ratio=std::min(1.,static_cast<double>(output_rate)/input_rate);
    radius_=static_cast<std::size_t>(std::ceil(48/ratio));
    taps_=radius_*2+1;
    ring_.resize(taps_+2);
    coefficients_.resize((phases+1)*taps_);
    constexpr double pi=std::numbers::pi;
    const double cutoff=.46*ratio;
    for(unsigned phase=0;phase<=phases;++phase) {
        const double fraction=static_cast<double>(phase)/phases;
        double sum=0;
        for(std::size_t tap=0;tap<taps_;++tap) {
            const double distance=static_cast<double>(tap)-static_cast<double>(radius_)-fraction;
            const double window_position=distance/static_cast<double>(radius_);
            const double window=std::abs(window_position)>=1?0:.42+.5*std::cos(pi*window_position)+.08*std::cos(2*pi*window_position);
            const double value=std::abs(distance)<1e-12?2*cutoff:std::sin(2*pi*cutoff*distance)/(pi*distance);
            coefficients_[phase*taps_+tap]=static_cast<float>(value*window);
            sum+=value*window;
        }
        for(std::size_t tap=0;tap<taps_;++tap)
            coefficients_[phase*taps_+tap]=static_cast<float>(coefficients_[phase*taps_+tap]/sum);
    }
}
std::uint64_t Resampler::final_count() const {
    // Quotient/remainder avoids multiplying a long-running input counter.
    const auto whole=received_/input_rate_;
    if(whole>std::numeric_limits<std::uint64_t>::max()/output_rate_)
        throw Error("resampler duration exceeds counter range");
    const auto tail=((received_%input_rate_)*output_rate_+input_rate_-1)/input_rate_;
    if(whole*output_rate_>std::numeric_limits<std::uint64_t>::max()-tail)
        throw Error("resampler duration exceeds counter range");
    return whole*output_rate_+tail;
}
bool Resampler::finished() const noexcept {
    if(last_stage_)return last_stage_->finished();
    if(!ending_)return false;
    // source_ is the timestamp of the next output in input sample units.
    return source_>=received_;
}
std::size_t Resampler::workspace_bytes() const noexcept {
    return sizeof(*this)+(ring_.capacity()+coefficients_.capacity()+intermediate_.capacity())*sizeof(float)+
        (first_stage_?first_stage_->workspace_bytes()+last_stage_->workspace_bytes():0);
}
Resampler::Progress Resampler::process_stages(std::span<const float> input,std::span<float> output,bool end) {
    Progress progress;
    while(progress.produced<output.size() && !last_stage_->finished()) {
        const auto converted=last_stage_->process(
            std::span<const float>(intermediate_.data()+intermediate_position_,intermediate_count_-intermediate_position_),
            output.subspan(progress.produced),first_stage_->finished());
        intermediate_position_+=converted.consumed;
        progress.produced+=converted.produced;
        if(converted.consumed || converted.produced)continue;
        if(intermediate_position_!=intermediate_count_)
            throw Error("resampler stage buffer invariant failed");
        const auto filled=first_stage_->process(input.subspan(progress.consumed),intermediate_,end);
        progress.consumed+=filled.consumed;
        intermediate_position_=0;intermediate_count_=filled.produced;
        if(!filled.consumed && !filled.produced && !first_stage_->finished())break;
    }
    if(end && progress.consumed==input.size())ending_=true;
    return progress;
}
double Resampler::passband_hz() const noexcept {
    return (input_rate_==output_rate_?.5:.42)*std::min(input_rate_,output_rate_);
}
float Resampler::interpolate() const {
    const double phase_position=static_cast<double>(remainder_)*phases/output_rate_;
    const auto phase=static_cast<unsigned>(phase_position);
    const double blend=phase_position-phase;
    double value=0;
    for(std::size_t tap=0;tap<taps_;++tap) {
        // Negative input times and the final FIR tail are zero extended.
        if(tap<radius_ && source_<radius_-tap)continue;
        if(tap>=radius_ && source_>std::numeric_limits<std::uint64_t>::max()-(tap-radius_))continue;
        const auto index=tap>=radius_?source_+(tap-radius_):source_-(radius_-tap);
        if(index<first_ || index>=received_)continue;
        const auto offset=static_cast<std::size_t>(index-first_);
        if(offset>=size_)throw Error("resampler history invariant failed");
        const auto a=coefficients_[phase*taps_+tap];
        const auto b=coefficients_[(phase+1)*taps_+tap];
        value+=ring_[(head_+offset)%ring_.size()]*(a+(b-a)*blend);
    }
    return static_cast<float>(value);
}
void Resampler::discard_history() {
    const auto keep=source_>radius_?source_-radius_:0;
    const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(size_,keep>first_?keep-first_:0));
    head_=(head_+count)%ring_.size();size_-=count;first_+=count;
}
Resampler::Progress Resampler::process(std::span<const float> input,std::span<float> output,bool end) {
    if(ending_ && !input.empty())throw Error("cannot append to a finished resampler");
    if(first_stage_)return process_stages(input,output,end);
    Progress progress;
    if(input_rate_==output_rate_) {
        const auto count=std::min(input.size(),output.size());
        for(std::size_t i=0;i<count;++i) {
            if(!std::isfinite(input[i]))throw Error("nonfinite resampler input");
            output[i]=input[i];
        }
        if(received_>std::numeric_limits<std::uint64_t>::max()-count)throw Error("resampler duration exceeds counter range");
        received_+=count;source_=received_;produced_=received_;
        ending_=ending_ || (end && count==input.size());
        return {count,count};
    }
    while(progress.produced<output.size()) {
        if(end && progress.consumed==input.size())ending_=true;
        if(ending_ && produced_>=final_count())break;
        if((source_<=std::numeric_limits<std::uint64_t>::max()-radius_ && received_>source_+radius_) || ending_) {
            const auto sample=interpolate();
            if(!std::isfinite(sample))throw Error("resampler output overflow");
            output[progress.produced++]=sample;
            ++produced_;
            const auto advance=static_cast<std::uint64_t>(remainder_)+input_rate_;
            if(source_>std::numeric_limits<std::uint64_t>::max()-advance/output_rate_)
                throw Error("resampler duration exceeds counter range");
            source_+=advance/output_rate_;remainder_=static_cast<std::uint32_t>(advance%output_rate_);
            discard_history();
        } else if(progress.consumed<input.size()) {
            if(size_==ring_.size())throw Error("resampler buffer invariant failed");
            const auto sample=input[progress.consumed++];
            if(!std::isfinite(sample))throw Error("nonfinite resampler input");
            ring_[(head_+size_)%ring_.size()]=sample;++size_;
            if(received_==std::numeric_limits<std::uint64_t>::max())throw Error("resampler duration exceeds counter range");
            ++received_;
        } else break;
    }
    if(end && progress.consumed==input.size())ending_=true;
    return progress;
}
}
