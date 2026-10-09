#pragma once
#include "datapump/audio.hpp"
#include "datapump/simulation_estimate.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <limits>

namespace datapump::gui {
// Cache only engineering advice. The receiver still consumes every original
// timestamp/uncertainty and makes its own admission and search decisions.
class ReceiverTimingAdvice {
public:
    using Clock=std::chrono::steady_clock;
    const simulation::ReceiverTimingModel& model() const {return model_;}
    bool qualified() const {return model_.capture_seconds_per_frame.has_value();}
    void reset() {model_={};tightening_.reset();}
    bool observe(const std::optional<audio::TimePrediction>& observation,
                 double sample_rate,double audio_error,Clock::time_point now) {
        const auto previous=model_;
        if(!observation||!std::isfinite(sample_rate)||sample_rate<=0||
           !std::isfinite(audio_error)||audio_error<0||
           !std::isfinite(observation->uncertainty_seconds)||observation->uncertainty_seconds<0||
           observation->uncertainty_seconds>audio_error||
           !std::isfinite(observation->seconds_per_frame)||observation->seconds_per_frame<=0||
           !std::isfinite(observation->rate_uncertainty_fraction)||observation->rate_uncertainty_fraction<0) {
            reset();return model_!=previous;
        }
        const double period=1/sample_rate;
        // Nominal-centred interval contains both endpoints of the observed
        // slope interval. No fixed ppm floor, and no per-block UTC anchor.
        const long double required=std::abs(static_cast<long double>(observation->seconds_per_frame)-period)*sample_rate+
            observation->rate_uncertainty_fraction;
        if(!std::isfinite(required)){reset();return model_!=previous;}
        double bound=0;
        if(required>0) {
            int exponent=0;
            // Outward round before choosing a bucket, including exact boundary
            // cases. The arrival-map radius is fraction/Fs, not period*fraction.
            std::frexp(std::nextafter(required,std::numeric_limits<long double>::infinity()),&exponent);
            bound=std::ldexp(1.,exponent);
        }
        if(!std::isfinite(bound)){reset();return model_!=previous;}
        const auto retained=model_.capture_rate_uncertainty_fraction.value_or(-1);
        if(!qualified()||model_.capture_seconds_per_frame!=period||
           model_.capture_error_seconds!=audio_error||bound>retained) {
            model_={audio_error,bound,period};tightening_.reset();
        } else if(bound<=retained/4&&bound<retained) {
            // Permit a better settled fit, without rebuilding the entire advice
            // bank for each small fluctuation of a continuously fitted clock.
            if(!tightening_)tightening_=Tightening{now,bound};
            tightening_->bound=std::max(tightening_->bound,bound);
            if(now-tightening_->since>=std::chrono::seconds(2)) {
                model_.capture_rate_uncertainty_fraction=tightening_->bound;tightening_.reset();
            }
        } else tightening_.reset();
        return model_!=previous;
    }
private:
    struct Tightening {Clock::time_point since;double bound;};
    simulation::ReceiverTimingModel model_;
    std::optional<Tightening> tightening_;
};
}
