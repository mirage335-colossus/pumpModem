#include "datapump/live.hpp"
#include <cmath>
namespace datapump::live {
namespace {
constexpr const char* unavailable="Robust is unavailable until the canonical triad backend is connected.";
[[noreturn]] void require_backend(){throw Error(unavailable);}
}
struct Session::Impl {Settings settings;};
Session::Session():impl_(std::make_unique<Impl>()){}
Session::~Session()=default;
void validate_settings(const Settings& settings){modem::validate(settings.transfer.modem);}
void Session::start(const Settings& value){configure(value);}
void Session::configure(const Settings& value){validate_settings(value);impl_->settings=value;}
void Session::set_mono(bool mono){impl_->settings.mono=mono;impl_->settings.channel_mode=audio::output_channels(mono);}
void Session::set_channel_mode(audio::ChannelMode channels){impl_->settings.channel_mode=channels;}
void Session::set_transmit_gain(double gain){if(!std::isfinite(gain)||gain<0)throw Error("invalid output gain");impl_->settings.transmit_gain=gain;}
void Session::set_helper_callsign(std::string callsign){impl_->settings.helper.callsign=std::move(callsign);}
bool Session::try_suspend_capture(){return true;}
void Session::resume_capture(){}
void Session::clear_received(){}
void Session::transmit(const Message&){require_backend();}
void Session::transmit_bits(std::span<const std::uint8_t>){require_backend();}
void Session::transmit_noise(){require_backend();}
void Session::transmit_helper(){require_backend();}
void Session::cancel_transmit(){}
bool Session::resume_recovery(std::uint64_t){return false;}
void Session::cancel_recovery(std::uint64_t){}
Snapshot Session::snapshot(){Snapshot result;result.status=unavailable;result.receiver_error=unavailable;result.simulation=impl_->settings.simulation;return result;}
void Session::stop(){}
}
