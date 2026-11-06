#pragma once

#include "datapump/utc_timing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace datapump::audio {

// Explicit best-effort discipline, separate from the strict UTC/affine policy.
// Rates are fractions, not ppm. These limits govern software resampling only;
// selecting this policy does not certify a device, an RF oscillator or a path.
struct ContinuousClockOptions {
    double maximum_rate_correction = .001;
    // Correct measured ADC/DAC rate error through the full rate domain, but
    // catch up UTC phase gently: 2 seconds at 50ppm needs at least 11.1 hours.
    double maximum_phase_correction = 50e-6;
    double maximum_slew_per_second = 1e-5;
    double phase_time_constant_seconds = 1000;
    double rate_window_seconds = 60;
    double maximum_rate_window_seconds = 3600;
    double maximum_extrapolation_seconds = 2;
    double maximum_timestamp_uncertainty_seconds = 1;

    void validate() const {
        if (!std::isfinite(maximum_rate_correction) || maximum_rate_correction <= 0 || maximum_rate_correction > .001 ||
            !std::isfinite(maximum_phase_correction) || maximum_phase_correction <= 0 ||
            maximum_phase_correction > maximum_rate_correction ||
            !std::isfinite(maximum_slew_per_second) || maximum_slew_per_second <= 0 || maximum_slew_per_second > .001 ||
            !std::isfinite(phase_time_constant_seconds) || phase_time_constant_seconds < 1 ||
            !std::isfinite(rate_window_seconds) || rate_window_seconds < 1 ||
            !std::isfinite(maximum_rate_window_seconds) || maximum_rate_window_seconds < rate_window_seconds ||
            !std::isfinite(maximum_extrapolation_seconds) || maximum_extrapolation_seconds <= 0 ||
            !std::isfinite(maximum_timestamp_uncertainty_seconds) || maximum_timestamp_uncertainty_seconds <= 0)
            throw Error("invalid continuous audio-clock discipline options");
    }
};

struct ClockDisciplineReport {
    bool ready = false, holdover = true, saturated = false, capture = false;
    bool phase_correction_limited = false;
    double target_correction = 0, applied_correction = 0, q_estimate = 1;
    double phase_error_seconds = 0, uncertainty_seconds = 0, rate_uncertainty_fraction = .001;
    double last_clock_step_seconds = 0, cumulative_clock_step_seconds = 0;
    long double native_frame = 0, logical_seconds = 0;
    long double origin_utc_seconds = 0, estimated_utc_seconds = 0;
    std::uint64_t observations = 0, rejected = 0, rate_updates = 0, clock_steps = 0;
};

// Controls the existing continuous Resampler; it never changes its phase,
// integrates a second source clock, resets an epoch, or inserts/deletes PCM.
//
// q is steady/system seconds per nominal native frame-second. Device frames
// versus monotonic time capture slow OS rate adjustment as well as ADC/DAC
// rate error. UTC minus monotonic steps affect phase, never this rate fit.
//
// At update(), native_frame and logical_seconds MUST describe the same actual
// converter frontier. TX: accepted output frames and source_seconds(). RX:
// source_seconds()*native_rate and produced_output_frames/logical_rate.
// The caller applies target_correction with the declared slew limit. A target
// may change repeatedly; there is no FIFO or lifetime displacement allowance.
class ContinuousClock {
public:
    ContinuousClock(std::uint32_t native_rate, bool capture, ContinuousClockOptions options = {})
        : rate_(native_rate), capture_(capture), options_(options) {
        options_.validate();
        if (!rate_ || rate_ > 120000000) throw Error("invalid continuous audio native rate");
        rate_uncertainty_ = options_.maximum_rate_correction;
    }

    const ContinuousClockOptions& options() const noexcept { return options_; }

    // A query that cannot date its frame is not evidence of missing PCM. Hold
    // the applied rate until a usable observation returns; preserve all state.
    void unavailable() noexcept { available_ = false; }

    bool observe(const FrameTimestamp& value) {
        if (!valid(value)) return reject();
        if (latest_) {
            if (value.continuity != latest_->continuity)
                throw Error("continuous audio device counter generation changed");
            if (value.frame < latest_->frame)
                throw Error("continuous audio device frame counter moved backward");
            if (value.steady_seconds < latest_->steady_seconds)
                throw Error("continuous audio monotonic clock moved backward");
            // Repeated unchanged counter observations are useful phase dates
            // but cannot establish a new frame-rate slope.
            if (value.frame > latest_->frame && value.steady_seconds == latest_->steady_seconds)
                return reject();
        }

        if (!latest_) {
            origin_utc_ = value.utc_seconds - value.frame / rate_;
            rate_anchor_ = value;
        } else {
            const auto step = (value.utc_seconds-latest_->utc_seconds) -
                              (value.steady_seconds-latest_->steady_seconds);
            const auto read_radius = value.clock_read_uncertainty_seconds + latest_->clock_read_uncertainty_seconds;
            if (std::abs(step) > std::max(1e-4L, static_cast<long double>(read_radius))) {
                increment(clock_steps_);
                last_step_ = static_cast<double>(step);
                cumulative_steps_ += step;
            }
            const auto native_elapsed = (value.frame-rate_anchor_->frame) / rate_;
            if (native_elapsed >= options_.rate_window_seconds) {
                const auto elapsed = value.steady_seconds-rate_anchor_->steady_seconds;
                const auto measured_q = elapsed/native_elapsed;
                const auto radius = (value.uncertainty_seconds + rate_anchor_->uncertainty_seconds +
                    value.clock_read_uncertainty_seconds + rate_anchor_->clock_read_uncertainty_seconds) / native_elapsed;
                const bool plausible = std::isfinite(measured_q) && measured_q >= .99L && measured_q <= 1.01L &&
                                       std::isfinite(radius);
                if (plausible && radius <= options_.maximum_rate_correction) {
                    // Bounded EWMA, not an accumulating regression or a claim
                    // that timing errors from different windows are independent.
                    q_ = .5L*q_ + .5L*measured_q;
                    rate_uncertainty_ = .5*rate_uncertainty_ + .5*static_cast<double>(radius);
                    increment(rate_updates_);
                    rate_anchor_ = value;
                } else if (!plausible || native_elapsed >= options_.maximum_rate_window_seconds) {
                    // Implausible slopes are never clipped into valid ones.
                    // Also bound the age of an uninformative anchor; report
                    // the failed fit and begin a new finite window.
                    increment(rejected_);
                    rate_anchor_ = value;
                }
                // A plausible but uncertain fit needs a longer baseline, not
                // a fresh anchor every minimum window. For example, 100ms
                // endpoint uncertainty needs over 200s at a 1000ppm limit.
                // Retaining one anchor preserves bounded memory and lets the
                // radius decrease without assuming independent query errors.
            }
        }
        latest_ = value;
        available_ = true;
        increment(observations_);
        return true;
    }

    ClockDisciplineReport update(long double native_frame, long double logical_seconds,
                                 double current_correction) {
        if (!std::isfinite(native_frame) || native_frame < 0 ||
            !std::isfinite(logical_seconds) || logical_seconds < 0 ||
            !std::isfinite(current_correction) || std::abs(current_correction) > .001 + 8*std::numeric_limits<double>::epsilon())
            throw Error("invalid continuous audio converter frontier");
        if (last_frontier_ && (native_frame < last_frontier_->first || logical_seconds < last_frontier_->second))
            throw Error("continuous audio converter frontier moved backward");
        last_frontier_ = std::pair{native_frame, logical_seconds};
        ClockDisciplineReport report;
        report.ready = latest_.has_value();
        report.capture = capture_;
        report.native_frame = native_frame;
        report.logical_seconds = logical_seconds;
        report.origin_utc_seconds = origin_utc_;
        report.target_correction = report.applied_correction = current_correction;
        report.q_estimate = static_cast<double>(q_);
        report.rate_uncertainty_fraction = rate_uncertainty_;
        report.observations = observations_;
        report.rejected = rejected_;
        report.rate_updates = rate_updates_;
        report.clock_steps = clock_steps_;
        report.last_clock_step_seconds = last_step_;
        report.cumulative_clock_step_seconds = static_cast<double>(cumulative_steps_);
        if (!latest_) return report;

        const auto ahead = (native_frame-latest_->frame)/rate_;
        report.estimated_utc_seconds = latest_->utc_seconds + ahead*q_;
        // Subtract the common large UTC origin before adding relative media
        // coordinates. No control state grows with the number of corrections.
        const auto error = (latest_->utc_seconds-origin_utc_) + ahead*q_ - logical_seconds;
        const auto radius = latest_->uncertainty_seconds + latest_->clock_read_uncertainty_seconds +
                            std::abs(ahead)*rate_uncertainty_;
        report.phase_error_seconds = static_cast<double>(error);
        report.uncertainty_seconds = static_cast<double>(radius);
        report.holdover = !available_ || std::abs(ahead) > options_.maximum_extrapolation_seconds;
        if (report.holdover) return report;

        const auto effective_error = std::copysign(std::max(0.L, std::abs(error)-radius), error);
        const auto phase_rate = effective_error/options_.phase_time_constant_seconds;
        report.phase_correction_limited = std::abs(phase_rate) > options_.maximum_phase_correction;
        // Phase catch-up limits waveform retiming without withholding the
        // measured device-rate correction. Do not apply this cap to q itself.
        const auto requested_rate = q_ + std::clamp(phase_rate,
            -static_cast<long double>(options_.maximum_phase_correction),
             static_cast<long double>(options_.maximum_phase_correction));
        // Common domain whose direct AND reciprocal converter corrections fit
        // ±maximum_rate_correction. Avoid asymmetric TX/RX boundary behavior.
        const auto lower = 1.L/(1.L+options_.maximum_rate_correction);
        const auto upper = 1.L+options_.maximum_rate_correction;
        const auto chosen_rate = std::clamp(requested_rate, lower, upper);
        report.saturated = requested_rate < lower || requested_rate > upper;
        const auto target = capture_ ? 1.L/chosen_rate-1.L : chosen_rate-1.L;
        report.target_correction = std::clamp(static_cast<double>(target),
            -options_.maximum_rate_correction, options_.maximum_rate_correction);
        return report;
    }

private:
    static void increment(std::uint64_t& value) noexcept {
        if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
    }
    bool reject() noexcept { increment(rejected_); available_ = false; return false; }
    bool valid(const FrameTimestamp& value) const noexcept {
        return std::isfinite(value.frame) && value.frame >= 0 &&
            std::isfinite(value.utc_seconds) && value.utc_seconds >= 0 &&
            std::isfinite(value.steady_seconds) && value.steady_seconds >= 0 &&
            std::isfinite(value.uncertainty_seconds) && value.uncertainty_seconds >= 0 &&
            value.uncertainty_seconds <= options_.maximum_timestamp_uncertainty_seconds &&
            std::isfinite(value.clock_read_uncertainty_seconds) && value.clock_read_uncertainty_seconds >= 0 &&
            value.clock_read_uncertainty_seconds <= options_.maximum_timestamp_uncertainty_seconds &&
            (value.quality == TimingQuality::estimated || value.quality == TimingQuality::bounded);
    }

    std::uint32_t rate_;
    bool capture_, available_ = false;
    ContinuousClockOptions options_;
    std::optional<FrameTimestamp> latest_, rate_anchor_;
    std::optional<std::pair<long double,long double>> last_frontier_;
    long double origin_utc_ = 0, q_ = 1, cumulative_steps_ = 0;
    double rate_uncertainty_ = .001, last_step_ = 0;
    std::uint64_t observations_ = 0, rejected_ = 0, rate_updates_ = 0, clock_steps_ = 0;
};

} // namespace datapump::audio
