#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace datapump::gui::smoke_detail {
// Only the replacement/cancellation fixture controls presentation time. Normal
// smoke replays retain their real-time cadence checks, and DSP keeps its own
// clock. A slow native paint cannot finish a fixture before its next UI action.
class InterruptionClock {
public:
    using Clock=std::chrono::steady_clock;
    using Source=std::function<Clock::time_point()>;
    explicit InterruptionClock(Source source=Clock::now):source_(std::move(source)) {}
    Clock::time_point now() const {
        const std::lock_guard lock(mutex_);
        return held_.value_or(source_()+offset_);
    }
    void pause() {
        const std::lock_guard lock(mutex_);
        if(!held_)held_=source_()+offset_;
    }
    void advance_frame() {
        const std::lock_guard lock(mutex_);
        if(held_)*held_+=std::chrono::milliseconds(50);
    }
    void resume() {
        const std::lock_guard lock(mutex_);
        if(held_) {offset_=*held_-source_();held_.reset();}
    }
private:
    Source source_;
    mutable std::mutex mutex_;
    Clock::duration offset_{};
    std::optional<Clock::time_point> held_;
};
}
