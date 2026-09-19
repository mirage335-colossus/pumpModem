#include "datapump/legacy/session.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace datapump::legacy {
namespace {
void check(const Settings& s) {validate(s.config);if(s.device.empty())throw std::invalid_argument("Select a Legacy audio device");}
void check_format(const Settings& s,const audio::StreamFormat& f) {
    const double half=s.config.mode==Mode::olivia4_2000?1000:(s.config.mode==Mode::bpsk125?250:62.5);
    if(s.config.carrier_hz+half>f.usable_passband_hz)throw std::runtime_error("Legacy signal exceeds the audio device passband");
}
}
struct Session::Impl {
    mutable std::mutex mutex;
    Settings settings;
    Snapshot current;
    std::jthread worker;
    bool closing=false;
    std::uint64_t event_serial=0;
    std::size_t retained_text=0;
    ~Impl(){worker.request_stop();if(worker.joinable())worker.join();}
    template<class Action>void update(Action action) {std::lock_guard lock(mutex);action(current);++current.revision;}
    void append(std::string_view text,bool tx) {
        if(text.empty())return;
        update([&](auto& out) {
            out.events.push_back({++event_serial,tx,std::string(text)});retained_text+=text.size();
            while(retained_text>65536&&!out.events.empty()) {
                retained_text-=out.events.front().text.size();out.events.erase(out.events.begin());
            }
        });
    }
    void samples(std::span<const float> input) {
        update([&](auto& out) {
            ++out.audio_revision;
            constexpr std::size_t limit=4096;
            if(input.size()>=limit)out.recent_samples.assign(input.end()-limit,input.end());
            else {
                const auto remove=out.recent_samples.size()+input.size()>limit?out.recent_samples.size()+input.size()-limit:0;
                out.recent_samples.erase(out.recent_samples.begin(),out.recent_samples.begin()+static_cast<std::ptrdiff_t>(remove));
                out.recent_samples.insert(out.recent_samples.end(),input.begin(),input.end());
            }
        });
    }
    void receive(std::stop_token stop,const Settings& s) {
        Receiver receiver(s.config,[&](std::string_view text){append(text,false);});
        std::mutex queue_mutex;std::condition_variable_any changed;
        std::deque<std::vector<float>> queue;std::size_t queued_samples=0;
        bool done=false,overrun=false;std::string capture_error;
        std::jthread capture([&](std::stop_token capture_stop) {
            try {
                audio::capture(sample_rate,s.device,[&](std::span<const float> block) {
                    std::lock_guard lock(queue_mutex);
                    if(block.size()>sample_rate-queued_samples) {overrun=true;changed.notify_all();return false;}
                    queue.emplace_back(block.begin(),block.end());queued_samples+=block.size();changed.notify_all();
                    return !stop.stop_requested();
                },capture_stop,[&](const auto& format){check_format(s,format);});
            }catch(const std::exception& e) {
                std::lock_guard lock(queue_mutex);if(!capture_stop.stop_requested()&&!stop.stop_requested())capture_error=e.what();
            }
            {std::lock_guard lock(queue_mutex);done=true;}changed.notify_all();
        });
        std::stop_callback stop_capture(stop,[&]{capture.request_stop();changed.notify_all();});
        while(!stop.stop_requested()) {
            std::vector<float> block;
            {
                std::unique_lock lock(queue_mutex);
                changed.wait(lock,stop,[&]{return done||overrun||!queue.empty();});
                if(stop.stop_requested())break;
                if(overrun)throw std::runtime_error("Legacy input overrun; receive stream interrupted");
                if(queue.empty()) {
                    if(!capture_error.empty())throw std::runtime_error(capture_error);
                    if(done)break;
                    continue;
                }
                block=std::move(queue.front());queue.pop_front();queued_samples-=block.size();
            }
            receiver.push(block);samples(block);
        }
        capture.request_stop();capture.join();
    }
    void send(std::stop_token stop,const Settings& s,const std::string& text) {
        Transmitter transmitter(s.config,text,[&](std::string_view sent){append(sent,true);});
        audio::playback(sample_rate,s.device,[&](std::span<float> out) {
            if(stop.stop_requested())return std::size_t{};
            const auto count=transmitter.read(out);if(count)samples(out.first(count));return count;
        },stop,[&](const auto& format){check_format(s,format);},s.mono);
        if(!stop.stop_requested())update([&](auto& out){out.sent_bytes=text.size();});
    }
    void launch(bool tx,std::string text={}) {
        Settings s;
        {
            std::lock_guard lock(mutex);
            if(closing)throw std::runtime_error("Legacy session is closing");
            if(current.active)throw std::runtime_error("Legacy audio is already active");
            check(settings);
            if(tx&&(text.empty()||text.size()>text_byte_limit||text.find('\0')!=std::string::npos))
                throw std::invalid_argument("Legacy text must contain 1 to 32768 non-NUL bytes");
            s=settings;current.active=true;current.listening=!tx;current.transmitting=tx;current.error.clear();
            current.recent_samples.clear();
            if(tx){++current.transmission;current.sent_bytes=0;}
            current.status=tx?"Transmitting · simplex":"Listening · simplex";++current.revision;
        }
        if(worker.joinable())worker.join();
        worker=std::jthread([this,s,tx,text=std::move(text)](std::stop_token stop) {
            try {if(tx)send(stop,s,text);else receive(stop,s);}
            catch(const std::exception& e) {if(!stop.stop_requested())update([&](auto& out){out.error=e.what();});}
            update([&](auto& out) {
                out.active=out.listening=out.transmitting=false;
                out.status=!out.error.empty()?"Legacy audio error":stop.stop_requested()?"Legacy audio stopped":tx?"Text sent":"Legacy input closed";
            });
        });
    }
};
Session::Session():impl_(std::make_unique<Impl>()){}
Session::~Session()=default;
void Session::configure(const Settings& settings) {
    check(settings);std::lock_guard lock(impl_->mutex);
    if(impl_->current.active)throw std::runtime_error("Stop Legacy audio before changing settings");
    impl_->settings=settings;
}
void Session::listen(){impl_->launch(false);}
void Session::transmit_text(const std::string& text){impl_->launch(true,text);}
void Session::cancel(){impl_->worker.request_stop();}
Snapshot Session::poll()const{std::lock_guard lock(impl_->mutex);return impl_->current;}
bool Session::active()const{std::lock_guard lock(impl_->mutex);return impl_->current.active;}
void Session::close(){std::lock_guard lock(impl_->mutex);impl_->closing=true;impl_->worker.request_stop();}
bool Session::ready_to_close()const{return !active();}
}
