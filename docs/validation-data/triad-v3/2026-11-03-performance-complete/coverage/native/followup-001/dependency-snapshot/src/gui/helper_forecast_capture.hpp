#pragma once
#include "link_planner_model.hpp"
#include <cmath>
#include <optional>
#include <string>
namespace datapump::gui::planner_capture {
// Only active CAPTURE fields enter this key. Playback negotiation must not
// replace a raw reference's clock/map or repeatedly invalidate the planner.
struct Key {
    bool running=false,simulation=false;
    std::uint32_t raw_rate=0;
    double raw_nyquist_hz=0,converter_passband_hz=0;
    audio::CaptureTransportRecipe transport={};
    bool operator==(const Key&)const=default;
};
inline Key key(const live::Snapshot& s) {
    if(!s.running)return {};
    return {true,s.simulation,s.capture_raw_sample_rate,
        std::isfinite(s.capture_raw_nyquist_hz)?s.capture_raw_nyquist_hz:0.,
        std::isfinite(s.capture_converter_passband_hz)?s.capture_converter_passband_hz:0.,s.capture_transport};
}
inline void apply(planner::Inputs& input,const live::Settings& settings,const live::Snapshot& snapshot) {
    input.helper_forecast_capture_sample_rate.reset();input.helper_forecast_capture_passband_hz.reset();
    input.helper_forecast_capture_logical_sample_rate.reset();
    input.helper_forecast_fixed_nominal_rate_conversion=false;
    input.helper_forecast_plan_sampled_capture=settings.simulation;
    input.helper_forecast_capture_description.clear();
    if(settings.simulation) {
        input.helper_forecast_capture_description="Sampled physical capture is planned per target by the actual channel clock selector; it does not certify a radio/ADC passband.";
        return;
    }
    const auto format=key(snapshot);
    if(!format.running||format.simulation||!format.raw_rate||
       format.raw_nyquist_hz!=format.raw_rate/2.) {
        input.helper_forecast_capture_description="Current raw hardware capture format is unavailable; playback or logical modem rate supplies no substitute.";
        return;
    }
    const auto& map=format.transport;
    if(map.kind!=audio::CaptureTransportKind::fixed_nominal_audio||map.raw_rate!=format.raw_rate||
       map.logical_rate!=input.options.modem.sample_rate) {
        input.helper_forecast_capture_description="Raw capture rate is known, but its fixed nominal raw-to-logical construction witness is unavailable. Equal rates or clock status do not prove that map.";
        return;
    }
    input.helper_forecast_capture_sample_rate=format.raw_rate;
    input.helper_forecast_capture_logical_sample_rate=map.logical_rate;
    input.helper_forecast_capture_passband_hz=std::nextafter(format.raw_nyquist_hz,0.);
    input.helper_forecast_fixed_nominal_rate_conversion=true;
    input.helper_forecast_capture_description="Actual raw capture "+std::to_string(format.raw_rate)+
        " Hz with a fixed nominal conversion witness. The capture limit used here is DIGITAL Nyquist only; ADC/radio amplitude and analog passband remain unverified conditions. Converter flat passband describes logical output and is not raw analog evidence.";
}
}
