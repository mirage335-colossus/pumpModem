// Test-only ideal physical loopback. This is not a device or GPU qualification.
// It links the real lab Companion and supplies deterministic, real-time dated
// samples; output is mapped to the exact requested scheduled epoch.
#include "local.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <charconv>
#include <cstdlib>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace datapump::lab::local_audio {
namespace {
constexpr std::uint64_t fixture_max_samples=10000000;
class Fixture final : public Driver {
    std::mutex mutex_;std::condition_variable_any changed_;
    std::optional<double> epoch_;std::optional<std::uint64_t> endpoint_;
    std::uint32_t output_rate_=0;std::vector<float> output_;bool aborted_=false;
    static double now() {return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
    static void wait_until(double epoch,std::stop_token stop) {
        while(!stop.stop_requested()) {
            const auto remain=epoch-now();if(remain<=0)return;
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(.01,remain)));
        }
        throw Error("fixture cancelled");
    }
    static aud::StreamFormat format(std::uint32_t rate) {
        aud::StreamFormat f;f.logical_rate=rate;f.hardware_rate=rate;f.usable_passband_hz=.49*rate;
        f.workspace_bytes=1024;f.timing_quality=aud::TimingQuality::estimated;
        f.timing_uncertainty_seconds=.001;return f;
    }
    static std::uint64_t early_eof() {
        const auto* raw=std::getenv("LAB_AUDIO_FIXTURE_EOF_FRAMES");
        if(!raw)return 0;
        const std::string value=raw;std::uint64_t result=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !result)
            throw Error("invalid fixture early EOF limit");
        return result;
    }
    static std::uint64_t prehistory() {
        const auto* raw=std::getenv("LAB_AUDIO_FIXTURE_PREHISTORY_SECONDS");
        if(!raw)return 0;
        const std::string value=raw;std::uint64_t result=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result>600)
            throw Error("invalid fixture synthetic prehistory duration");
        return result;
    }
    static void trace(const std::string& line) {
        const auto* path=std::getenv("LAB_AUDIO_FIXTURE_TRACE_PATH");
        if(!path)return;
        const int fd=::open(path,O_WRONLY|O_APPEND|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)throw Error("fixture trace file unavailable");
        struct stat info{};
        if(::fstat(fd,&info)<0 || !S_ISREG(info.st_mode) || info.st_uid!=::geteuid()) {
            ::close(fd);throw Error("unsafe fixture trace file");
        }
        std::string data=line+'\n';std::size_t at=0;
        while(at<data.size()) {
            const auto count=::write(fd,data.data()+at,data.size()-at);
            if(count<0 && errno==EINTR)continue;
            if(count<=0){::close(fd);throw Error("fixture trace write failed");}
            at+=static_cast<std::size_t>(count);
        }
        ::close(fd);
    }
public:
    std::vector<aud::Device> devices() override {return {{"fixture","Synthetic test-only loopback"}};}
    void capture(std::uint32_t rate,const std::string&,const aud::CaptureCallback& callback,
                 std::stop_token stop,aud::StreamFormatCallback on_format,aud::Options options) override {
        if(!options.request_capture_timing || !options.capture_timing)throw Error("fixture needs dated capture");
        // Optional synthetic prehistory is replayed quickly, then the sample
        // stream follows real time. It tests receiver state, not physical I/O.
        on_format(format(rate));const double origin=now()-static_cast<double>(prehistory());std::uint64_t position=0;
        {
            std::ostringstream text;text<<std::setprecision(17)<<"{\"event\":\"origin\",\"utc\":"<<origin
                <<",\"rate\":"<<rate<<",\"uncertainty\":0.001,\"quality\":1}";
            trace(text.str());
        }
        const auto block=std::max<std::uint32_t>(1,std::min<std::uint32_t>(4096,rate/20));
        std::vector<float> pcm(block);std::uint64_t noise=0x123456789abcdefULL;
        const auto eof=early_eof();
        while(!stop.stop_requested()) {
            const auto count=eof?static_cast<std::size_t>(std::min<std::uint64_t>(block,eof-position)):pcm.size();
            if(!count){trace("{\"event\":\"stopped\",\"frames\":"+std::to_string(position)+",\"reason\":\"early_eof\"}");return;}
            wait_until(origin+static_cast<double>(position+count)/rate,stop);
            {
                std::unique_lock lock(mutex_);
                for(std::size_t i=0;i<count;++i) {
                    noise^=noise<<13;noise^=noise>>7;noise^=noise<<17;
                    float sample=static_cast<float>((static_cast<double>(noise&0xffff)/32767.5-1.)*1e-4);
                    const double t=origin+static_cast<double>(position+i)/rate;
                    if(epoch_ && t>=*epoch_) {
                        if(output_rate_!=rate)throw Error("fixture capture/playback rates differ");
                        const auto index=static_cast<std::uint64_t>(std::floor((t-*epoch_)*rate));
                        if(index<fixture_max_samples) {
                            const bool available=changed_.wait_for(lock,stop,std::chrono::milliseconds(500),[&]{
                                return aborted_ || index<output_.size() || (endpoint_ && index>=*endpoint_);
                            });
                            if(!available || aborted_)throw Error("fixture playback underrun or interruption");
                            if(index<output_.size())sample+=output_[static_cast<std::size_t>(index)];
                        } else throw Error("fixture output exceeded bounded sample limit");
                    }
                    pcm[i]=sample;
                }
            }
            aud::TimePrediction stamp;stamp.utc_seconds=static_cast<long double>(origin)+static_cast<long double>(position)/rate;
            stamp.uncertainty_seconds=.001;stamp.seconds_per_frame=1./rate;stamp.quality=aud::TimingQuality::estimated;
            stamp.rate_uncertainty_fraction=0;options.capture_timing(stamp);
            if(!callback(std::span<const float>(pcm).first(count))) {
                trace("{\"event\":\"stopped\",\"frames\":"+std::to_string(position+count)+",\"reason\":\"callback_stop\"}");return;
            }
            position+=count;
        }
        throw Error("fixture capture cancelled");
    }
    void playback(std::uint32_t rate,const std::string&,const aud::PlaybackCallback& callback,
                  std::stop_token stop,aud::StreamFormatCallback on_format,aud::ChannelMode,aud::Options options) override {
        {std::lock_guard lock(mutex_);output_.clear();output_rate_=rate;epoch_.reset();endpoint_.reset();aborted_=false;}
        try {
            on_format(format(rate));
            std::optional<double> epoch;{std::lock_guard lock(mutex_);epoch=epoch_;}
            if(!epoch)throw Error("fixture playback was not scheduled");
            std::vector<float> pcm(std::max<std::uint32_t>(1,std::min<std::uint32_t>(4096,rate/20)));
            std::uint64_t produced=0;
            for(;;) {
                if(stop.stop_requested())throw Error("fixture playback cancelled");
                const auto count=callback(pcm);if(count>pcm.size())throw Error("invalid fixture callback count");
                if(!count)break;
                if(count>fixture_max_samples-produced)throw Error("fixture playback exceeds test sample bound");
                {
                    std::lock_guard lock(mutex_);
                    for(std::size_t i=0;i<count;++i) {
                        const auto sample=pcm[i]*static_cast<float>(options.transmit_gain);
                        if(!std::isfinite(sample) || std::abs(sample)>1)throw Error("fixture playback clipping");
                        output_.push_back(sample);
                    }
                    produced+=count;changed_.notify_all();
                }
                // Simulate a one-second hardware delivery queue rather than
                // acknowledging all source samples before physical time.
                wait_until(*epoch+static_cast<double>(produced)/rate-1.,stop);
            }
            {std::lock_guard lock(mutex_);endpoint_=produced;changed_.notify_all();}
            wait_until(*epoch+static_cast<double>(produced)/rate,stop);
        } catch(...) {{std::lock_guard lock(mutex_);aborted_=true;changed_.notify_all();}throw;}
    }
    void schedule(double epoch,std::stop_token stop) override {
        if(stop.stop_requested() || !std::isfinite(epoch) || epoch-now()<2)throw Error("fixture scheduled start has insufficient lead");
        std::lock_guard lock(mutex_);epoch_=epoch;changed_.notify_all();
    }
    double now_utc() override {return now();}
    double minimum_lead() override {return 2;}
};
}
std::unique_ptr<Driver> native_driver(){return std::make_unique<Fixture>();}
}
