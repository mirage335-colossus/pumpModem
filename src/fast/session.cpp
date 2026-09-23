#include "datapump/fast/session.hpp"
#include "session_state.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace datapump::fast {
namespace {
using Clock=std::chrono::steady_clock;
void check_settings(const Settings& s) {
    validate(s.profile);
    audio::validate_options({s.transmit_gain,s.exclusive});
    if(s.device.empty())throw Error("Select a fast audio device");
    if(s.quota_bytes<65536||s.quota_bytes>256ULL*1024*1024)
        throw Error("Fast storage quota must be 64 KiB..256 MiB");
}
void check_format(const Settings& s,const audio::StreamFormat& f) {
    const auto high=occupied_upper_hz(s.profile);
    if(high>f.usable_passband_hz)throw Error("Fast waveform exceeds this audio device's usable passband");
}
}
struct Session::Impl {
    mutable std::mutex mutex;
    Settings settings;
    Snapshot current;
    std::jthread worker;
    bool closing=false;
    Clock::time_point started{};

    ~Impl() {worker.request_stop();if(worker.joinable())worker.join();}
    template<class F> void update(F action) {
        std::lock_guard lock(mutex);action(current);
        current.elapsed_seconds=std::chrono::duration<double>(Clock::now()-started).count();
        current.goodput_bps=current.elapsed_seconds>0?8.0*static_cast<double>(current.source_bytes)/current.elapsed_seconds:0;
        ++current.revision;
    }
    void receive(std::stop_token stop,const Settings& s,std::uint64_t stream_id) {
        Telemetry telemetry(s.profile,false,stream_id);
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        Receiver receiver(s.profile,[&](std::span<const float> interval) {decoder.push_interval(interval);},
            [&](std::complex<float> symbol) noexcept {telemetry.record_symbol(symbol);},
            [&](std::complex<float> input) noexcept {telemetry.record_input(input);});
        std::mutex queue_mutex;std::condition_variable_any changed;
        std::deque<std::vector<float>> queue;std::size_t queued_samples=0;
        bool done=false,overrun=false;std::string capture_error;
        const auto queue_limit=static_cast<std::size_t>(s.profile.sample_rate)*(s.profile.acoustic_ofdm?4:1);
        std::jthread capture([&](std::stop_token capture_stop) {
            try {
                audio::capture(s.profile.sample_rate,s.device,[&](std::span<const float> samples) {
                    std::lock_guard lock(queue_mutex);
                    // OFDM uses four seconds for bounded parallel LDPC cycles;
                    // single-carrier keeps its one-second bound. Capture never
                    // waits for DSP/FEC and never silently concatenates a gap.
                    if(samples.size()>queue_limit-queued_samples) {
                        overrun=true;changed.notify_all();return false;
                    }
                    queue.emplace_back(samples.begin(),samples.end());queued_samples+=samples.size();
                    changed.notify_all();return !stop.stop_requested();
                },capture_stop,[&](const auto& format){check_format(s,format);},{s.transmit_gain,s.exclusive});
            } catch(const std::exception& e) {
                std::lock_guard lock(queue_mutex);if(!capture_stop.stop_requested()&&!stop.stop_requested())capture_error=e.what();
            }
            {std::lock_guard lock(queue_mutex);done=true;}changed.notify_all();
        });
        std::stop_callback cancel_capture(stop,[&]{capture.request_stop();changed.notify_all();});
        while(!stop.stop_requested()) {
            std::vector<float> samples;
            {
                std::unique_lock lock(queue_mutex);
                changed.wait(lock,stop,[&]{return done||overrun||!queue.empty();});
                if(stop.stop_requested())break;
                if(overrun)throw Error("Fast capture overrun: missing sample time; transfer incomplete");
                if(queue.empty()) {
                    if(!capture_error.empty())throw Error(capture_error);
                    if(done)break;
                    continue;
                }
                samples=std::move(queue.front());queue.pop_front();queued_samples-=samples.size();
            }
            receiver.push(samples);
            const auto dsp=receiver.progress();const auto coding=decoder.snapshot();
            telemetry.record_samples(samples);
            const auto diagnostics=telemetry.publish(dsp.acquired);
            update([&](auto& out) {
                if(diagnostics)out.diagnostics=diagnostics;
                out.intervals=coding.intervals;out.authenticated_groups=coding.authenticated_groups;
                out.checksum_groups=coding.checksum_groups;
                out.corrected_bytes=coding.corrected_bytes;out.erased_bytes=coding.erased_bytes;
                out.ldpc_frames=coding.ldpc_frames;out.ldpc_failed_frames=coding.ldpc_failed_frames;
                out.ldpc_iterations=coding.ldpc_iterations;out.ldpc_changed_bits=coding.ldpc_changed_bits;
                out.decoding_stopped=coding.decoding_stopped;out.coding_cycles=coding.coding_cycles;
                out.failed_cycles=coding.failed_cycles;out.verified_bytes=coding.verified_bytes;
                out.evm=dsp.evm;out.carrier_error_hz=dsp.carrier_error_hz;out.clock_error_ppm=dsp.clock_error_ppm;
                out.status=coding.decoding_stopped?"File decoding stopped: "+coding.status:
                    coding.failed_cycles?"Receiving fast stream with missing data; continuing decoding":
                    dsp.acquired?"Receiving fast stream; content pending":"Listening for Fast modem training";
            });
            if(dsp.physical_complete)break;
        }
        capture.request_stop();capture.join();
        receiver.finish();
        const bool end=!stop.stop_requested()&&receiver.progress().physical_complete;
        decoder.finish(end);
        const auto result=decoder.snapshot();
        update([&](auto& out) {
            out.physical_complete=end;out.complete=result.complete;out.source_bytes=result.source_bytes;
            out.encrypted=result.encrypted;out.authenticated=result.authenticated;
            out.authenticated_groups=result.authenticated_groups;out.checksum_groups=result.checksum_groups;
            out.decoding_stopped=result.decoding_stopped;out.coding_cycles=result.coding_cycles;
            out.failed_cycles=result.failed_cycles;out.verified_bytes=result.verified_bytes;
            out.file=decoder.result();out.status=result.status;
            if(end&&!result.complete)out.error=result.status;
        });
    }
    void send(std::stop_token stop,const Settings& s,PreparedXzSource input,bool text,std::uint64_t stream_id) {
        const auto estimate=estimate_transmission(s.profile,s.key.has_value(),input.encoded.size());
        const auto source_bytes=input.source_bytes;
        std::uint64_t generated=0;
        update([&](auto& out){out.estimated_seconds=estimate.seconds;});
        Telemetry telemetry(s.profile,true,stream_id);
        StreamEncoder encoder(s.profile,s.key,byte_source(std::move(input.encoded)),SourceEncoding::xz);
        Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
            if(stop.stop_requested())return false;
            return encoder.next_interval(bits);
        },[&](std::complex<float> symbol) noexcept {telemetry.record_symbol(symbol);});
        // The playback producer is bounded; unlike capture it can wait for the
        // fixed coding work. XZ bytes are prepared in bounded RAM before audio opens;
        // no full waveform or bit-vector is retained.
        auto silence=end_silence_samples(s.profile);
        audio::playback(s.profile.sample_rate,s.device,[&](std::span<float> output) {
            if(stop.stop_requested())return std::size_t{0};
            auto count=transmitter.read(output);
            if(!count&&silence) {
                count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),silence));
                std::fill_n(output.begin(),count,0);silence-=count;
            }
            generated+=count;
            telemetry.record_samples(output.first(count));
            const auto diagnostics=telemetry.publish(false);
            update([&](auto& out) {
                if(diagnostics)out.diagnostics=diagnostics;
                out.transmit_fraction=std::min(.999,static_cast<double>(generated)/static_cast<double>(estimate.samples));
                out.source_bytes=source_bytes;out.intervals=encoder.intervals_emitted();
                out.status=std::string("Transmitting fast ")+(s.key?"encrypted ":"unencrypted ")+(text?"text":"file");
            });
            return count;
        },stop,[&](const auto& format){check_format(s,format);},audio::output_channels(s.mono,s.channel_mode),{s.transmit_gain,s.exclusive});
        update([&](auto& out) {
            out.status=stop.stop_requested()?"Transmission cancelled":"Transmission sent; receiver observes physical absence";
            if(!stop.stop_requested())out.transmit_fraction=1;
            // A sender cannot claim remote reception or authentication.
            out.complete=false;
        });
    }
    void launch(bool tx,const std::filesystem::path& path={},std::optional<std::string> text={}) {
        Settings s;
        std::uint64_t stream_id=0;
        {
            std::lock_guard lock(mutex);
            if(closing)throw Error("Fast session is closing");
            if(current.active)throw Error("Fast audio is already active");
            check_settings(settings);
            if(text&&(text->size()>text_byte_limit||text->size()>settings.quota_bytes))
                throw Error("Fast text exceeds the local 32768-byte limit");
            s=settings;
            const auto revision=current.revision+1;current={};current.revision=revision;
            current.active=true;current.transmitting=tx;current.listening=!tx;
            current.encrypted=s.key.has_value();
            stream_id=next_diagnostics_stream_id();
            current.diagnostics=initial_diagnostics(s.profile,tx,stream_id);
            current.status=tx?"Preparing fast transmission":"Opening fast audio input";started=Clock::now();
        }
        if(worker.joinable())worker.join();
        worker=std::jthread([this,s=std::move(s),tx,path,text=std::move(text),stream_id](std::stop_token stop) {
            try {
                if(!tx)receive(stop,s,stream_id);
                else if(text)send(stop,s,prepare_xz_source(byte_source(Bytes(text->begin(),text->end())),s.quota_bytes,stop),true,stream_id);
                else {
                    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
                    if(ec||!std::filesystem::is_regular_file(path))throw Error("Fast source must be a readable regular file");
                    if(size>s.quota_bytes)throw Error("Fast source exceeds local storage quota");
                    send(stop,s,prepare_xz_attachment(file_source(path),attachment::filename_from_path(path),s.quota_bytes,stop),false,stream_id);
                }
            }
            catch(const std::exception& e) {update([&](auto& out){if(!stop.stop_requested())out.error=e.what();out.status="Fast transfer incomplete";});}
            update([&](auto& out) {
                detail::finish_worker(out,stop.stop_requested());
            });
        });
    }
};
Session::Session():impl_(std::make_unique<Impl>()){}
Session::~Session()=default;
void Session::configure(const Settings& s) {
    check_settings(s);std::lock_guard lock(impl_->mutex);
    if(impl_->current.active)throw Error("Stop fast audio before changing its settings");
    impl_->settings=s;
}
void Session::transmit(const std::filesystem::path& source){impl_->launch(true,source);}
void Session::transmit_text(const std::string& text) {
    if(text.size()>text_byte_limit)throw Error("Fast text exceeds the local 32768-byte limit");
    impl_->launch(true,{},text);
}
void Session::listen(){impl_->launch(false);}
void Session::cancel(){impl_->worker.request_stop();}
Snapshot Session::poll() const {std::lock_guard lock(impl_->mutex);return impl_->current;}
void Session::save(const std::filesystem::path& destination) const {
    auto result=poll();if(!result.complete||!result.file)throw Error("No complete fast file is available");
    result.file->save(destination);
}
void Session::clear_received(){std::lock_guard lock(impl_->mutex);impl_->current.file.reset();}
bool Session::active() const {std::lock_guard lock(impl_->mutex);return impl_->current.active;}
void Session::close(){std::lock_guard lock(impl_->mutex);impl_->closing=true;impl_->worker.request_stop();}
bool Session::ready_to_close() const{return !active();}
}
