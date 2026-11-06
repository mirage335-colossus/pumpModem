#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace datapump::triad::v3 {

// A bounded, relative source-time map for the isolated triad study. This does
// not read a system clock, change an epoch/key, estimate a remote clock, or
// qualify a receiver for the resulting waveform. Apply it to the narrow DATA
// source coordinate, before a separately clocked final audio-center NCO.
//
// Calls use nondecreasing relative physical times, never floating-point UTC.
// Between corrections the source coordinate is t + offset. A correction uses
// q(u)=10u^3-15u^4+6u^5, so position, rate and acceleration are continuous.
// An update during a transition replaces one queued target; that transition
// begins at rest at the existing endpoint. State and work remain bounded even
// if sample() advances over both transitions in one call.
class SourceClockFollower {
public:
    struct Limits {
        long double maximum_offset_seconds = 2;
        long double maximum_rate_correction = 300e-6L;
        long double maximum_acceleration = .2e-6L;
    };
    struct Sample {
        long double source_seconds;
        long double offset_seconds;
        long double rate_scale;
        long double acceleration;
    };

    SourceClockFollower() : SourceClockFollower(Limits{}) {}
    explicit SourceClockFollower(Limits limits) : limits_(limits) {
        if (!(std::isfinite(limits.maximum_offset_seconds) && limits.maximum_offset_seconds > 0 &&
              limits.maximum_offset_seconds <= 2 &&
              std::isfinite(limits.maximum_rate_correction) && limits.maximum_rate_correction > 0 &&
              limits.maximum_rate_correction <= 300e-6L &&
              std::isfinite(limits.maximum_acceleration) && limits.maximum_acceleration > 0 &&
              limits.maximum_acceleration <= .2e-6L))
            throw std::invalid_argument("invalid bounded triad clock-follow limits");
    }

    const Limits& limits() const noexcept { return limits_; }

    long double minimum_duration(long double displacement_seconds) const {
        if (!std::isfinite(displacement_seconds))
            throw std::invalid_argument("clock displacement must be finite");
        const auto distance = std::abs(displacement_seconds);
        // max q'=15/8; max |q''|=10/sqrt(3).
        return std::max(1.875L * distance / limits_.maximum_rate_correction,
            std::sqrt((10 / std::sqrt(3.L)) * distance / limits_.maximum_acceleration));
    }

    // target is absolute departure from the original affine source map, not
    // an incremental correction. Repeated updates never renew the ±2s budget.
    // An explicit duration is the transition's full duration, including when
    // queued. Rejected updates leave the accepted map unchanged.
    void update(long double time_seconds, long double target_offset_seconds,
                std::optional<long double> duration_seconds = std::nullopt) {
        check_time(time_seconds);
        if (!std::isfinite(target_offset_seconds) ||
            std::abs(target_offset_seconds) > limits_.maximum_offset_seconds)
            throw std::invalid_argument("clock target exceeds original offset envelope");
        if (duration_seconds && (!std::isfinite(*duration_seconds) || *duration_seconds <= 0))
            throw std::invalid_argument("clock transition duration must be finite and positive");
        auto next = *this;
        next.advance(time_seconds);
        const auto origin = next.active_ ? next.target_ : next.offset_;
        const auto duration = duration_seconds.value_or(next.minimum_duration(target_offset_seconds - origin));
        if (duration < next.minimum_duration(target_offset_seconds - origin))
            throw std::invalid_argument("clock transition exceeds rate or acceleration limit");
        if (next.active_) {
            next.pending_ = target_offset_seconds != next.target_;
            next.pending_target_ = target_offset_seconds;
            next.pending_duration_ = duration;
            // Check overflow now, not later in sample().
            if (next.pending_) checked_end(next.end_, duration);
        } else {
            next.start(time_seconds, target_offset_seconds, duration);
        }
        *this = next;
    }

    Sample sample(long double time_seconds) {
        check_time(time_seconds);
        advance(time_seconds);
        if (!active_) return {time_seconds + offset_, offset_, 1, 0};
        const auto u = (time_seconds - begin_) / duration_;
        const auto q = u*u*u * (10 + u*(-15 + 6*u));
        const auto derivative = 30*u*u*(1-u)*(1-u);
        const auto second = 60*u*(1-u)*(1-2*u);
        const auto delta = target_ - offset_;
        const auto offset = offset_ + delta*q;
        return {time_seconds + offset, offset, 1 + delta*derivative/duration_,
                delta*second/(duration_*duration_)};
    }

    bool transitioning() const noexcept { return active_; }
    bool target_queued() const noexcept { return pending_; }

private:
    static long double checked_end(long double begin, long double duration) {
        const auto end = begin + duration;
        if (!std::isfinite(end) || !(end > begin))
            throw std::invalid_argument("clock transition time is not representable");
        return end;
    }
    void check_time(long double time) const {
        if (!std::isfinite(time) || (seen_time_ && time < last_time_))
            throw std::invalid_argument("clock map requires finite nondecreasing relative time");
    }
    void start(long double begin, long double target, long double duration) {
        if (target == offset_) return;
        const auto end = checked_end(begin, duration);
        begin_ = begin;
        end_ = end;
        duration_ = duration;
        target_ = target;
        active_ = true;
    }
    void advance(long double time) {
        while (active_ && time >= end_) {
            offset_ = target_;
            active_ = false;
            if (pending_) {
                pending_ = false;
                start(end_, pending_target_, pending_duration_);
            }
        }
        seen_time_ = true;
        last_time_ = time;
    }

    Limits limits_;
    long double offset_ = 0, begin_ = 0, end_ = 0, duration_ = 0, target_ = 0;
    long double pending_target_ = 0, pending_duration_ = 0, last_time_ = 0;
    bool active_ = false, pending_ = false, seen_time_ = false;
};

} // namespace datapump::triad::v3
