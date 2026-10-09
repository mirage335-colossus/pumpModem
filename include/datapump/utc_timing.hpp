#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace datapump::audio {
// A timestamp describes a device frame, not the time a callback happened to
// run. An estimated driver timeline is useful for steering, but is NOT a
// calibrated bound on a codec, USB radio, RF path, or arbitrary audio plugin.
enum class TimingQuality { unavailable, estimated, bounded };
struct FrameTimestamp {
    long double frame=0, utc_seconds=0, steady_seconds=0;
    double uncertainty_seconds=0, clock_read_uncertainty_seconds=0;
    TimingQuality quality=TimingQuality::unavailable;
    std::uint64_t continuity=0;
};
struct TimePrediction {
    long double utc_seconds=0;
    double uncertainty_seconds=0, seconds_per_frame=0;
    TimingQuality quality=TimingQuality::unavailable;
    double rate_uncertainty_fraction=1;
};

// A paired hardware link counter and UTC reading measures RATE independently
// of the unknown codec/radio presentation offset. These bounds cover the mean
// rate over the reported interval; they are not a guarantee about future clock
// wander. Only a provider with a separate wander bound may authorize steering.
struct LinkTimestamp {
    long double device_seconds=0,utc_seconds=0,steady_seconds=0;
    double device_error_seconds=0,pair_error_seconds=0;
    std::uint64_t continuity=0;
};
struct LinkRateInterval {
    double lower=0,upper=0;
    long double duration_seconds=0;
    double midpoint() const {return lower+(upper-lower)/2;}
    double uncertainty() const {return (upper-lower)/2;}
};
class LinkRateTimeline {
public:
    void observe(const LinkTimestamp& value) {
        if(!std::isfinite(value.device_seconds) || value.device_seconds<0 ||
           !std::isfinite(value.utc_seconds) || value.utc_seconds<0 ||
           !std::isfinite(value.steady_seconds) ||
           !std::isfinite(value.device_error_seconds) || value.device_error_seconds<0 ||
           !std::isfinite(value.pair_error_seconds) || value.pair_error_seconds<0)
            throw Error("invalid paired audio link timestamp");
        if(latest_) {
            const auto du=value.utc_seconds-latest_->utc_seconds;
            const auto ds=value.steady_seconds-latest_->steady_seconds;
            if(value.continuity!=latest_->continuity || value.device_seconds<=latest_->device_seconds ||
               du<=0 || ds<=0 || std::abs(du-ds)>.001L*ds+
                    value.pair_error_seconds+latest_->pair_error_seconds)
                throw Error("paired audio link timestamp continuity lost");
        } else anchor_=value;
        latest_=value;
    }
    std::optional<LinkRateInterval> interval() const {
        if(!anchor_ || !latest_)return std::nullopt;
        const auto da=latest_->device_seconds-anchor_->device_seconds;
        const auto du=latest_->utc_seconds-anchor_->utc_seconds;
        const auto ea=latest_->device_error_seconds+anchor_->device_error_seconds;
        const auto eu=latest_->pair_error_seconds+anchor_->pair_error_seconds;
        if(da<=ea || du<=eu)return std::nullopt;
        // Outward rounding preserves endpoint inclusion after conversion to
        // double. Query jitter and counter resolution are not averaged away.
        return LinkRateInterval{
            std::nextafter(static_cast<double>((du-eu)/(da+ea)-1),-std::numeric_limits<double>::infinity()),
            std::nextafter(static_cast<double>((du+eu)/(da-ea)-1),std::numeric_limits<double>::infinity()),da};
    }
private:
    std::optional<LinkTimestamp> anchor_,latest_;
};

// Explicit engineering assumptions for a generic driver, separate from GPS
// accuracy and RF oscillator specifications. Constant unmeasured codec latency
// is not timestamp jitter and must never be divided by a calibration duration.
// A stationary clock over the finite message is an assumption, not a hardware
// guarantee. Runtime continuity/error guards still stop an invalid trajectory.
struct EstimatedDeviceTiming {
    double variable_timestamp_error_seconds=.001;
    double maximum_utc_error_seconds=.03;
    double maximum_prepare_seconds=120;
    void validate() const {
        if(!std::isfinite(variable_timestamp_error_seconds) || variable_timestamp_error_seconds<=0 ||
           !std::isfinite(maximum_utc_error_seconds) || maximum_utc_error_seconds<=0 ||
           !std::isfinite(maximum_prepare_seconds) || maximum_prepare_seconds<=0)
            throw Error("invalid estimated audio timing model");
    }
    std::optional<double> initial_correction(const std::optional<LinkRateInterval>& measured,
            double source_seconds,double fixed_latency_allowance,double maximum_rate) const {
        validate();
        if(!std::isfinite(source_seconds) || source_seconds<=0 ||
           !std::isfinite(fixed_latency_allowance) || fixed_latency_allowance<0 ||
           !std::isfinite(maximum_rate) || maximum_rate<=0 || maximum_rate>.001)
            throw Error("invalid estimated audio timing preparation");
        const auto available=maximum_utc_error_seconds-fixed_latency_allowance-
            variable_timestamp_error_seconds;
        if(available<=0)throw Error("audio latency exceeds the estimated UTC allowance");
        const auto horizon=source_seconds/(1-maximum_rate);
        // A sufficiently short stream needs no rate estimate or added wait.
        if(maximum_rate*horizon<=available)return 0.;
        if(!measured)return std::nullopt;
        if(!std::isfinite(measured->lower) || !std::isfinite(measured->upper) ||
           measured->lower>measured->upper)throw Error("invalid estimated audio rate interval");
        if(measured->lower>maximum_rate || measured->upper< -maximum_rate)
            throw Error("audio clock is outside the selected UTC correction model");
        if(measured->lower< -maximum_rate || measured->upper>maximum_rate ||
           measured->uncertainty()*horizon>available) {
            if(measured->duration_seconds>=maximum_prepare_seconds)
                throw Error("UTC rate preparation is inconclusive; retain the full timing search");
            return std::nullopt;
        }
        if(measured->lower<=0 && measured->upper>=0 &&
           std::max(-measured->lower,measured->upper)*horizon<=available)return 0.;
        return measured->midpoint();
    }
};

// Fixed storage and relative-coordinate arithmetic, including when UTC is
// years from the epoch. Frame/UTC observations are never used as crypto indices.
class UtcTimeline {
public:
    explicit UtcTimeline(std::uint32_t rate,double rate_limit=.001)
        :rate_(rate),rate_limit_(rate_limit) {
        if(!rate || !std::isfinite(rate_limit) || rate_limit<=0 || rate_limit>.001)
            throw Error("invalid audio UTC clock model");
    }
    void observe(FrameTimestamp value) {
        if(!std::isfinite(value.frame) || !std::isfinite(value.utc_seconds) ||
           !std::isfinite(value.steady_seconds) || value.frame<0 || value.utc_seconds<0 ||
           !std::isfinite(value.uncertainty_seconds) || value.uncertainty_seconds<0 ||
           !std::isfinite(value.clock_read_uncertainty_seconds) || value.clock_read_uncertainty_seconds<0 ||
           value.quality==TimingQuality::unavailable)
            throw Error("audio UTC timestamp unavailable");
        if(count_) {
            const auto& previous=points_[(next_+points_.size()-1)%points_.size()];
            const auto elapsed=value.steady_seconds-previous.steady_seconds;
            const auto wall_elapsed=value.utc_seconds-previous.utc_seconds;
            if(value.continuity!=previous.continuity || value.frame<previous.frame || elapsed<=0 || wall_elapsed<=0)
                throw Error("audio UTC clock continuity lost");
            // Permit continuous system-clock discipline, but never conceal a
            // wall-clock step by relabelling samples or rewinding their origin.
            const auto clock_error=value.clock_read_uncertainty_seconds+previous.clock_read_uncertainty_seconds;
            if(std::abs(wall_elapsed-elapsed)>rate_limit_*elapsed+clock_error+1e-9L)
                throw Error("system clock stepped during UTC audio");
            if(value.frame==previous.frame)return; // no new device observation
            const auto duration=(value.frame-previous.frame)/rate_;
            if(std::abs(duration-wall_elapsed)>2*rate_limit_*wall_elapsed+
               value.uncertainty_seconds+previous.uncertainty_seconds)
                throw Error("audio device clock exceeds UTC timing model");
        }
        points_[next_]=value;next_=(next_+1)%points_.size();count_=std::min(count_+1,points_.size());
        fit();
    }
    bool ready() const noexcept {return count_!=0;}
    TimePrediction predict(long double frame) const {
        if(!count_ || !std::isfinite(frame) || frame<0)throw Error("audio UTC timeline not ready");
        const auto& latest=points_[(next_+points_.size()-1)%points_.size()];
        const auto horizon=std::abs(frame-latest.frame)/rate_;
        // Rate fitting cannot improve the provider's stated accuracy. Retain
        // the full residual, and widen extrapolation by the entire rate bound.
        const auto span=latest.frame-points_[count_<points_.size()?0:next_].frame;
        const auto rate_uncertainty=span>0?std::min(2*rate_limit_,2*uncertainty_*rate_/static_cast<double>(span)):2*rate_limit_;
        return {origin_+slope_*(frame-center_),
            uncertainty_+static_cast<double>(2*rate_limit_*horizon),static_cast<double>(slope_),quality_,rate_uncertainty};
    }
private:
    std::uint32_t rate_;
    double rate_limit_;
    std::array<FrameTimestamp,64> points_{};
    std::size_t count_=0,next_=0;
    long double origin_=0,center_=0,slope_=0;
    double uncertainty_=0;
    TimingQuality quality_=TimingQuality::unavailable;
    void fit() {
        const auto& latest=points_[(next_+points_.size()-1)%points_.size()];
        long double x=0,y=0,xx=0,xy=0;
        // Subtraction precedes accumulation so epoch magnitude cannot destroy
        // fractional-frame detail. The 64-point fit costs O(1) per audio block.
        for(std::size_t i=0;i<count_;++i) {x+=points_[i].frame-latest.frame;y+=points_[i].utc_seconds-latest.utc_seconds;}
        x/=count_;y/=count_;center_=latest.frame+x;origin_=latest.utc_seconds+y;
        for(std::size_t i=0;i<count_;++i) {
            const auto dx=points_[i].frame-center_;
            xx+=dx*dx;xy+=dx*(points_[i].utc_seconds-origin_);
        }
        slope_=1.L/rate_;
        if(xx>0) slope_=std::clamp(xy/xx,(1.L-rate_limit_)/rate_,(1.L+rate_limit_)/rate_);
        uncertainty_=0;quality_=TimingQuality::bounded;
        for(std::size_t i=0;i<count_;++i) {
            uncertainty_=std::max(uncertainty_,points_[i].uncertainty_seconds+
                static_cast<double>(std::abs(points_[i].utc_seconds-origin_-slope_*(points_[i].frame-center_))));
            if(points_[i].quality!=TimingQuality::bounded)quality_=TimingQuality::estimated;
        }
    }
};

struct UtcCorrection {
    double rate_fraction=0, error_seconds=0, uncertainty_seconds=0;
    TimingQuality quality=TimingQuality::unavailable;
};

// Keep a continuously adjusted finite transmission inside one affine source
// map. Existing receivers keep a fixed rate hypothesis across successive bits;
// a per-symbol slew limit alone does not preserve their later boundaries.
// Coordinates are logical source seconds and nominal hardware elapsed seconds.
// This guard bounds time warp, not interpolation or detector sensitivity.
class AffineTimingGuard {
public:
    AffineTimingGuard(long double source_origin,double initial_rate,
            double hardware_horizon_seconds,double maximum_warp_seconds,double roundoff_seconds=0)
        :origin_(source_origin),rate_(initial_rate),horizon_(hardware_horizon_seconds),limit_(maximum_warp_seconds),
         roundoff_(roundoff_seconds) {
        if(!std::isfinite(origin_) || origin_<0 || !std::isfinite(rate_) || std::abs(rate_)>.001 ||
           !std::isfinite(horizon_) || horizon_<=0 || !std::isfinite(limit_) || limit_<=0 ||
           !std::isfinite(roundoff_) || roundoff_<0 || roundoff_>=limit_)
            throw Error("invalid finite UTC timing guard");
    }
    double constrain(double proposed,long double source_seconds,long double elapsed_seconds,
                     double current_rate) const {
        if(!std::isfinite(proposed) || std::abs(proposed)>.001 ||
           !std::isfinite(source_seconds) || !std::isfinite(elapsed_seconds) || elapsed_seconds<0 ||
           !std::isfinite(current_rate) || std::abs(current_rate)>.001)
            throw Error("invalid finite UTC timing coordinate");
        const auto error=std::abs(source_seconds-origin_-(1.L+rate_)*elapsed_seconds);
        if(error>limit_ || elapsed_seconds>horizon_)
            throw Error("UTC correction exceeded the receiver's finite affine timing allowance");
        // The resampler slews monotonically between current and target. Its
        // integral deviation is at most this maximum rate distance times the
        // complete horizon, even across arbitrary output blocks. Keep the cap
        // fixed: recomputing it from rounded intermediate source coordinates
        // can make a previously safe current derivative appear infeasible.
        // The reserved arithmetic allowance is spent once over the message.
        const auto radius=(static_cast<long double>(limit_)-roundoff_)/horizon_;
        if(std::abs(static_cast<long double>(current_rate)-rate_)>radius+
                16*std::numeric_limits<double>::epsilon())
            throw Error("UTC correction has no safe continuation within the affine timing allowance");
        return static_cast<double>(std::clamp(static_cast<long double>(proposed),
            static_cast<long double>(rate_)-radius,static_cast<long double>(rate_)+radius));
    }
    // Check generated output before submitting any of that block to a device.
    // A caller supplying the wrong duration must stop, never emit past the
    // finite horizon used to justify the rate constraint.
    void verify(long double source_seconds,long double elapsed_seconds) const {
        if(!std::isfinite(source_seconds) || !std::isfinite(elapsed_seconds) || elapsed_seconds<0 ||
           elapsed_seconds>horizon_ ||
           std::abs(source_seconds-origin_-(1.L+rate_)*elapsed_seconds)>limit_)
            throw Error("generated UTC output exceeds its finite affine timing allowance");
    }
private:
    long double origin_;
    double rate_,horizon_,limit_,roundoff_;
};
inline UtcCorrection presentation_correction(const TimePrediction& predicted,long double desired_utc,
        std::uint32_t hardware_rate,double maximum_error_seconds=.03,double response_seconds=8,
        double maximum_rate_fraction=.001,double current_rate_fraction=0) {
    if(!std::isfinite(desired_utc) || !hardware_rate || !std::isfinite(maximum_error_seconds) ||
       maximum_error_seconds<=0 || !std::isfinite(response_seconds) || response_seconds<=0 ||
       predicted.quality==TimingQuality::unavailable || !std::isfinite(predicted.utc_seconds) ||
       !std::isfinite(predicted.uncertainty_seconds) || predicted.uncertainty_seconds<0 ||
       !std::isfinite(predicted.seconds_per_frame) || predicted.seconds_per_frame<=0 ||
       !std::isfinite(maximum_rate_fraction) || maximum_rate_fraction<0 || maximum_rate_fraction>.001 ||
       !std::isfinite(current_rate_fraction) || std::abs(current_rate_fraction)>.001 ||
       !std::isfinite(predicted.rate_uncertainty_fraction) || predicted.rate_uncertainty_fraction<0)
        throw Error("invalid UTC presentation correction");
    const auto error=static_cast<double>(predicted.utc_seconds-desired_utc);
    if(std::abs(error)+predicted.uncertainty_seconds>maximum_error_seconds)
        throw Error("UTC audio timing error exceeds allowance; transmission stopped");
    auto rate=predicted.seconds_per_frame*hardware_rate-1;
    // Preserve a rate already consistent with the observation interval. Do
    // not turn timestamp rounding/buffering uncertainty into waveform jitter.
    if(std::abs(rate-current_rate_fraction)<=predicted.rate_uncertainty_fraction)rate=current_rate_fraction;
    const auto phase_error=std::copysign(std::max(0.,std::abs(error)-predicted.uncertainty_seconds),error);
    return {std::clamp(rate+phase_error/response_seconds,-maximum_rate_fraction,maximum_rate_fraction),
        error,predicted.uncertainty_seconds,predicted.quality};
}

// Additional time-map curvature budget, using the whole coherent symbol and
// the PCM Nyquist edge. This carrier-only bound does not qualify interpolation,
// finite pulse tails, nonlinear limiting, or an uncalibrated hardware path.
inline double conservative_timing_slew(std::uint32_t rate,double symbol_seconds) {
    if(!rate || !std::isfinite(symbol_seconds) || symbol_seconds<=0)
        throw Error("invalid UTC waveform duration");
    return std::min(1e-5,.01/(static_cast<double>(rate)*symbol_seconds*symbol_seconds));
}

namespace detail {
// A native playback scope intercepts schedule_output before source generation.
// The scheduled epoch is a presentation target, never a request to reseed.
inline thread_local bool utc_playback_active=false;
inline thread_local std::optional<long double> utc_playback_epoch;
struct UtcPlaybackScope {
    bool previous_active=utc_playback_active;
    std::optional<long double> previous_epoch=utc_playback_epoch;
    explicit UtcPlaybackScope(bool active) {utc_playback_active=active;utc_playback_epoch.reset();}
    ~UtcPlaybackScope() {utc_playback_active=previous_active;utc_playback_epoch=previous_epoch;}
};
}
}
