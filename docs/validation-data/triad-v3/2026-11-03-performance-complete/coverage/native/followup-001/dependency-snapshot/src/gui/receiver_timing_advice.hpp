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
    bool dated() const {return model_.capture_seconds_per_frame.has_value();}
    // Strict Audio-budget admission remains distinct from dated calendar advice.
    bool qualified() const {return dated()&&audio_qualified_;}
    void reset() {model_={};tightening_.reset();audio_qualified_=false;}
    bool observe(const std::optional<audio::TimePrediction>& observation,
                 double sample_rate,double audio_error,Clock::time_point now,bool calendar=false) {
        const auto previous=model_;const auto previous_qualified=audio_qualified_;
        if(!observation||!std::isfinite(sample_rate)||sample_rate<=0||
           !std::isfinite(audio_error)||audio_error<0||
           !std::isfinite(observation->uncertainty_seconds)||observation->uncertainty_seconds<0||
           (!calendar&&observation->uncertainty_seconds>audio_error)||
           (calendar&&(observation->quality==audio::TimingQuality::unavailable||
               !std::isfinite(observation->utc_seconds)))||
           !std::isfinite(observation->seconds_per_frame)||observation->seconds_per_frame<=0||
           !std::isfinite(observation->rate_uncertainty_fraction)||observation->rate_uncertainty_fraction<0) {
            reset();return model_!=previous||audio_qualified_!=previous_qualified;
        }
        audio_qualified_=observation->uncertainty_seconds<=audio_error;
        const double period=1/sample_rate;
        // Nominal-centred interval contains both endpoints of the observed
        // slope interval. No fixed ppm floor, and no per-block UTC anchor.
        const long double required=std::abs(static_cast<long double>(observation->seconds_per_frame)-period)*sample_rate+
            observation->rate_uncertainty_fraction;
        if(!std::isfinite(required)){reset();return model_!=previous||audio_qualified_!=previous_qualified;}
        const auto outward_bucket=[](long double value) {
            if(value<=0)return 0.;
            int exponent=0;
            std::frexp(std::nextafter(value,std::numeric_limits<long double>::infinity()),&exponent);
            return std::ldexp(1.,exponent);
        };
        const double bound=outward_bucket(required);
        // The selected causal lag and a provider's timestamp precision are
        // separate. Above Audio's radius, retain a conservative stable bucket
        // of the FULL provider radius; never replace it with the selection.
        const double error=calendar&&!audio_qualified_?
            std::max(audio_error,outward_bucket(observation->uncertainty_seconds)):audio_error;
        if(!std::isfinite(bound)||!std::isfinite(error)) {
            reset();return model_!=previous||audio_qualified_!=previous_qualified;
        }
        const auto retained=model_.capture_rate_uncertainty_fraction.value_or(-1);
        const auto retained_error=model_.capture_error_seconds.value_or(-1);
        if(!dated()||model_.capture_seconds_per_frame!=period||
           (!calendar&&retained_error!=error)||error>retained_error||bound>retained) {
            model_={error,bound,period};tightening_.reset();
        } else if((bound<=retained/4&&bound<retained)||error<retained_error) {
            // Permit a better settled fit, without rebuilding the entire advice
            // bank for each small fluctuation of a continuously fitted clock.
            if(!tightening_)tightening_=Tightening{now,bound,error};
            tightening_->bound=std::max(tightening_->bound,bound);
            tightening_->error=std::max(tightening_->error,error);
            if(now-tightening_->since>=std::chrono::seconds(2)) {
                model_.capture_rate_uncertainty_fraction=tightening_->bound;
                model_.capture_error_seconds=tightening_->error;tightening_.reset();
            }
        } else tightening_.reset();
        return model_!=previous||audio_qualified_!=previous_qualified;
    }
private:
    struct Tightening {Clock::time_point since;double bound,error;};
    simulation::ReceiverTimingModel model_;
    std::optional<Tightening> tightening_;
    bool audio_qualified_=false;
};
}
