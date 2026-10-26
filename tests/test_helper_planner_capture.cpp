#include "helper_forecast_capture.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace datapump;
namespace {void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}}
int main(){try {
    live::Settings settings;settings.transfer.modem.sample_rate=6000;
    gui::planner::Inputs inputs;inputs.options=settings.transfer;
    live::Snapshot snapshot;snapshot.running=true;snapshot.capture_raw_sample_rate=48000;
    snapshot.capture_raw_nyquist_hz=24000;snapshot.capture_converter_passband_hz=2520;
    snapshot.capture_transport={audio::CaptureTransportKind::fixed_nominal_audio,48000,6000,0,0};
    gui::planner_capture::apply(inputs,settings,snapshot);
    check(inputs.helper_forecast_capture_sample_rate==48000&&inputs.helper_forecast_capture_logical_sample_rate==6000&&
        inputs.helper_forecast_capture_passband_hz==std::nextafter(24000.,0.)&&inputs.helper_forecast_fixed_nominal_rate_conversion,
        "actual capture witness was not bound to the planner");
    check(inputs.helper_forecast_capture_description.find("DIGITAL")!=std::string::npos&&
        inputs.helper_forecast_capture_description.find("unverified")!=std::string::npos,
        "digital Nyquist was presented as analog passband evidence");
    const auto captured=gui::planner_capture::key(snapshot);
    snapshot.hardware_sample_rate=96000;snapshot.audio_passband_hz=45000;
    check(gui::planner_capture::key(snapshot)==captured,"playback format invalidated or replaced capture geometry");
    snapshot.capture_transport.kind=audio::CaptureTransportKind::unknown;
    check(gui::planner_capture::key(snapshot)!=captured,"lost transport witness did not invalidate conditional planning");
    gui::planner_capture::apply(inputs,settings,snapshot);
    check(!inputs.helper_forecast_capture_sample_rate&&!inputs.helper_forecast_fixed_nominal_rate_conversion,
        "unknown/steered conversion was inferred from a raw rate");
    snapshot.capture_raw_sample_rate=6000;snapshot.capture_raw_nyquist_hz=3000;
    gui::planner_capture::apply(inputs,settings,snapshot);
    check(!inputs.helper_forecast_capture_sample_rate,"equal raw/logical rates manufactured a transport witness");
    snapshot.capture_transport={audio::CaptureTransportKind::fixed_nominal_audio,6000,12000,0,0};
    gui::planner_capture::apply(inputs,settings,snapshot);
    check(!inputs.helper_forecast_capture_sample_rate,"another logical rate's map was reused");
    snapshot.running=false;gui::planner_capture::apply(inputs,settings,snapshot);
    check(!inputs.helper_forecast_capture_sample_rate,"stopped capture retained stale format authority");
    snapshot.running=true;snapshot.capture_raw_nyquist_hz=std::numeric_limits<double>::quiet_NaN();
    check(gui::planner_capture::key(snapshot)==gui::planner_capture::key(snapshot),"invalid format causes perpetual NaN invalidation");
    settings.simulation=true;gui::planner_capture::apply(inputs,settings,snapshot);
    check(inputs.helper_forecast_plan_sampled_capture&&!inputs.helper_forecast_capture_sample_rate,
        "simulation borrowed a stale hardware format instead of the per-target pure channel query");
    std::cout<<"helper planner capture PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
