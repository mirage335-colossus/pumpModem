#include "local.hpp"
#include <chrono>

namespace datapump::lab::local_audio {
namespace {
class Native final : public Driver {
public:
    std::vector<aud::Device> devices() override {return aud::devices();}
    void capture(std::uint32_t rate,const std::string& device,const aud::CaptureCallback& callback,
                 std::stop_token stop,aud::StreamFormatCallback format,aud::Options options) override {
        aud::capture(rate,device,callback,stop,std::move(format),std::move(options));
    }
    void playback(std::uint32_t rate,const std::string& device,const aud::PlaybackCallback& callback,
                  std::stop_token stop,aud::StreamFormatCallback format,aud::ChannelMode channels,aud::Options options) override {
        aud::playback(rate,device,callback,stop,std::move(format),channels,std::move(options));
    }
    void schedule(double epoch,std::stop_token stop) override {aud::schedule_output(epoch,stop);}
    double now_utc() override {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    double minimum_lead() override {return aud::minimum_lead_seconds();}
};
}
std::unique_ptr<Driver> native_driver(){return std::make_unique<Native>();}
}
