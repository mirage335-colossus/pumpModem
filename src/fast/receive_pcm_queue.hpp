#pragma once

#include "datapump/execution.hpp"
#include "datapump/fast/profile.hpp"
#include <chrono>
#include <cstddef>
#include <exception>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace datapump::fast::detail {

// The dense-pilot SSB profile has a larger bounded acquisition backlog.
// Device rate, waveform geometry and all caps are local settings.
std::size_t receive_pcm_capacity(const Profile&);
bool expected_audio_cancellation(const std::exception&,std::stop_token) noexcept;

class ReceivePcmQueue {
public:
    using Clock=std::chrono::steady_clock;

    struct Status {
        std::size_t queued_samples;
        std::size_t peak_samples;
        bool closed;
        bool overflow;
        std::string error;
    };

    explicit ReceivePcmQueue(std::size_t capacity);
    bool push(std::span<const float>);
    std::size_t pop(std::span<float>,std::stop_token stop={},Clock::time_point deadline=Clock::time_point::max());
    void close(std::string error={});
    Status status() const;
    std::size_t capacity() const noexcept { return samples_.size(); }
    std::size_t workspace_bytes() const noexcept { return sizeof(*this)+samples_.capacity()*sizeof(float); }

private:
    std::vector<float> samples_;
    mutable execution::Mutex mutex_;
    execution::StopCondition ready_;
    std::size_t head_=0, size_=0, peak_=0;
    bool closed_=false, overflow_=false;
    std::string error_;
};

} // namespace datapump::fast::detail
