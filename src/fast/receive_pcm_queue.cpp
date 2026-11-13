#include "receive_pcm_queue.hpp"
#include "datapump/fast/ldpc.hpp"
#include "ssb_receiver.hpp"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace datapump::fast::detail {

bool expected_audio_cancellation(const std::exception& error,std::stop_token stop) noexcept {
    // The audio API reports cooperative stop with this Error. A concurrent
    // stop does not turn a previously thrown device fault into cancellation.
    return stop.stop_requested() && std::string_view(error.what())=="audio operation cancelled";
}

std::size_t receive_pcm_capacity(const Profile& profile) {
    const auto rate=static_cast<std::size_t>(profile.sample_rate);
    if(supports_delay_doppler_receiver(profile))return 8*rate;
    if(profile.acoustic_ofdm)return 4*rate;
    if(profile.channel==Channel::wire && profile.capacity_mode &&
       !profile.compact_convolutional && profile.ldpc_frame_bits==ldpc::coded_bits) {
        const auto frames=profile.interleave_depth;
        if(!frames || frames>16)
            throw std::invalid_argument("Fast wire capture requires 1..16 local LDPC frames");
        // A failed four-frame cycle blocked the capture consumer for nearly
        // two seconds in a physical cable test. Allow half a second per
        // full-size frame on top of the existing one-second ingress reserve.
        // This absorbs a bounded burst, without assuming sustained capacity.
        return rate+(rate*frames+1)/2;
    }
    return rate;
}

ReceivePcmQueue::ReceivePcmQueue(std::size_t capacity):samples_(capacity) {
    if(samples_.empty())throw std::invalid_argument("Fast capture queue requires a positive capacity");
}

bool ReceivePcmQueue::push(std::span<const float> input) {
    const std::lock_guard lock(mutex_);
    if(closed_)return false;
    if(input.size()>samples_.size()-size_) {
        overflow_=true;
        closed_=true;
        ready_.notify_all();
        return false;
    }
    const auto write=(head_+size_)%samples_.size();
    const auto first=std::min(input.size(),samples_.size()-write);
    std::ranges::copy(input.first(first),samples_.begin()+static_cast<std::ptrdiff_t>(write));
    std::ranges::copy(input.subspan(first),samples_.begin());
    size_+=input.size();
    peak_=std::max(peak_,size_);
    ready_.notify_one();
    return true;
}

std::size_t ReceivePcmQueue::pop(std::span<float> output,std::stop_token stop,Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    const auto available=[&]{return size_ || closed_;};
    if(deadline==Clock::time_point::max())ready_.wait(lock,stop,available);
    else ready_.wait_until(lock,stop,deadline,available);
    if(stop.stop_requested() || overflow_ || !error_.empty())return 0;
    const auto count=std::min(size_,output.size());
    const auto first=std::min(count,samples_.size()-head_);
    const auto readable=std::span<const float>(samples_);
    std::ranges::copy(readable.subspan(head_,first),output.begin());
    std::ranges::copy(readable.first(count-first),output.begin()+static_cast<std::ptrdiff_t>(first));
    head_=(head_+count)%samples_.size();
    size_-=count;
    return count;
}

void ReceivePcmQueue::close(std::string error) {
    const std::lock_guard lock(mutex_);
    if(error_.empty())error_=std::move(error);
    closed_=true;
    ready_.notify_all();
}

ReceivePcmQueue::Status ReceivePcmQueue::status() const {
    const std::lock_guard lock(mutex_);
    return {size_,peak_,closed_,overflow_,error_};
}

} // namespace datapump::fast::detail
