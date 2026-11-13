#include "datapump/fast/session.hpp"
#include "cable_loopback.hpp"
#include "datapump/execution.hpp"
#include "receive_pcm_queue.hpp"
#include "session_state.hpp"
#include "telemetry.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/audio.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace datapump::fast {
namespace {
using Clock=std::chrono::steady_clock;
using SourceHash=std::array<std::uint8_t,32>;
class SourceDigest {
public:
    SourceDigest() {
        if(!context_ || EVP_DigestInit_ex(context_.get(),EVP_sha256(),nullptr)!=1)
            throw Error("Cannot initialize Fast cable test source verification");
    }
    void append(std::span<const std::uint8_t> bytes) {
        if(EVP_DigestUpdate(context_.get(),bytes.data(),bytes.size())!=1)
            throw Error("Cannot verify Fast cable test source");
    }
    SourceHash finish() {
        SourceHash hash{};
        unsigned count=0;
        if(EVP_DigestFinal_ex(context_.get(),hash.data(),&count)!=1 || count!=hash.size())
            throw Error("Cannot verify Fast cable test source");
        return hash;
    }
private:
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(),EVP_MD_CTX_free};
};
SourceReader checked_file_source(const std::filesystem::path& path,std::uint64_t quota) {
    std::error_code error;
    const auto size=std::filesystem::file_size(path,error);
    if(error||!std::filesystem::is_regular_file(path))throw Error("Fast source must be a readable regular file");
    if(size>quota)throw Error("Fast source exceeds local storage quota");
    return file_source(path);
}
bool received_text_matches(const ReceivedFile& file,std::string_view text) {
    // The native decoder interprets an attachment prefix in raw source bytes,
    // even when those bytes came from the Console. Compare that prefix too.
    if(file.is_attachment()) {
        const auto prefix=attachment::prefix(file.filename());
        if(!text.starts_with(prefix))return false;
        text.remove_prefix(prefix.size());
    }
    return std::ranges::equal(std::as_bytes(file.bytes()),std::as_bytes(std::span(text)));
}
void check_settings(const Settings& s) {
    if(s.receiver_assumptions)validate_receiver_assumptions(s.profile,*s.receiver_assumptions);
    if(s.simulation)validate(s.profile,*s.simulation);
    else {
        validate(s.profile);
        audio::validate_options({s.transmit_gain,s.exclusive});
        if(s.device.empty())throw Error("Select a fast audio device");
    }
    if(s.quota_bytes<65536||s.quota_bytes>256ULL*1024*1024)
        throw Error("Fast storage quota must be 64 KiB..256 MiB");
}
void check_format(const Settings& s,const audio::StreamFormat& f) {
    const auto high=occupied_upper_hz(s.profile);
    if(high>f.usable_passband_hz)throw Error("Fast waveform exceeds this audio device's usable passband");
}
Receiver make_receiver(const Settings& settings,StreamDecoder& decoder,Telemetry& telemetry,std::stop_token stop) {
    return Receiver(settings.profile,[&decoder](std::span<const float> interval,bool feedback) {
            return decoder.push_interval(interval,feedback);
        },[&telemetry](std::complex<float> symbol) noexcept {telemetry.record_symbol(symbol);},
        [&telemetry](std::complex<float> input) noexcept {telemetry.record_input(input);},stop,settings.receiver_assumptions);
}
std::string_view cable_failure(detail::CableEnd end) noexcept {
    switch(end) {
    case detail::CableEnd::capture_eof:return "capture ended before reception completed";
    case detail::CableEnd::capture_timeout:return "audio input supplied no usable capture before the startup limit";
    case detail::CableEnd::playback_eof:return "playback ended before supplying the selected source";
    case detail::CableEnd::no_completion:return "no complete message after playback and the receive tail";
    case detail::CableEnd::audio_limit:return "75-second audio limit reached";
    case detail::CableEnd::wall_limit:return "110-second time limit reached";
    case detail::CableEnd::cancelled:return "cancelled";
    default:return "the receiver did not establish completion";
    }
}
}
struct Session::Impl {
    mutable execution::Mutex mutex;
    Settings settings;
    Snapshot current;
    execution::Task worker;
    bool closing=false;
    Clock::time_point started{};

    ~Impl() {worker.request_stop();if(worker.joinable())worker.join();}
    template<class F> void update(F action) {
        std::lock_guard lock(mutex);action(current);
        current.elapsed_seconds=std::chrono::duration<double>(Clock::now()-started).count();
        current.goodput_bps=current.elapsed_seconds>0?8.0*static_cast<double>(current.source_bytes)/current.elapsed_seconds:0;
        ++current.revision;
    }
    void publish_capture(detail::CaptureTelemetry& capture_display) {
        if(auto diagnostics=capture_display.publish())
            update([&](auto& out){out.diagnostics=std::move(diagnostics);});
    }
    void observe_receive(std::span<const float> samples, const Receiver& receiver,
                         const StreamDecoder& decoder, Telemetry& telemetry,
                         const SimulationReport* simulation = nullptr, std::uint64_t estimated_samples = 0,
                         detail::CaptureTelemetry* capture_display = nullptr) {
        const auto dsp=receiver.progress();const auto coding=decoder.snapshot();
        std::shared_ptr<const Diagnostics> diagnostics;
        if(capture_display)capture_display->record_receiver(telemetry,samples.size(),dsp.acquired,dsp.last_present_signal);
        else {
            telemetry.record_samples(samples);
            diagnostics=telemetry.publish(dsp.acquired);
        }
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
            const auto status=coding.decoding_stopped?"File decoding stopped: "+coding.status:
                coding.failed_cycles?"Receiving fast stream with missing data; continuing decoding":
                dsp.acquired?"Receiving fast stream; content pending":"Listening for Fast modem training";
            out.status=out.loopback_test?"Cable test · "+status:simulation?"Simulation · "+status:status;
            out.receiver_backend=receiver.backend_name();
            if(simulation) {
                out.simulation=*simulation;
                if(estimated_samples)
                    out.transmit_fraction=std::min(.999,static_cast<double>(simulation->output_samples)/estimated_samples);
            }
        });
    }
    void finish_receive(std::stop_token stop, Receiver& receiver, StreamDecoder& decoder,
                        bool early_simulation_completion = false) {
        receiver.finish();
        const bool end=!stop.stop_requested()&&receiver.progress().physical_complete&&!early_simulation_completion;
        decoder.finish(end);
        const auto result=decoder.snapshot();
        update([&](auto& out) {
            out.physical_complete=end;out.complete=result.complete;out.source_bytes=result.source_bytes;
            out.receiver_backend=receiver.backend_name();
            out.physical_completion_input_tick=end?receiver.physical_completion_input_tick():std::nullopt;
            out.encrypted=result.encrypted;out.authenticated=result.authenticated;
            out.authenticated_groups=result.authenticated_groups;out.checksum_groups=result.checksum_groups;
            out.decoding_stopped=result.decoding_stopped;out.coding_cycles=result.coding_cycles;
            out.failed_cycles=result.failed_cycles;out.verified_bytes=result.verified_bytes;
            out.file=decoder.result();out.status=result.status;
            if(out.simulated) {
                if(early_simulation_completion)out.status="Receiver ended before the simulated quiet window";
                out.status="Simulation · "+out.status;
                if(!stop.stop_requested())out.transmit_fraction=1;
            }
            if((end||out.simulated)&&!result.complete&&!stop.stop_requested())out.error=out.status;
        });
    }
    void receive(std::stop_token stop,const Settings& s,std::uint64_t stream_id) {
        Telemetry telemetry(s.profile,false,stream_id);
        detail::CaptureTelemetry capture_display(s.profile,stream_id);
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        auto receiver=make_receiver(s,decoder,telemetry,stop);
        detail::ReceivePcmQueue queue(detail::receive_pcm_capacity(s.profile));
        update([&](auto& out) {
            out.receiver_backend=receiver.backend_name();
            out.capture_queue_capacity_samples=queue.capacity();
            out.capture_queue_workspace_bytes=queue.workspace_bytes();
        });
        execution::Task display([&](std::stop_token display_stop) {
            execution::Mutex wait_mutex;
            execution::StopCondition condition;
            std::unique_lock lock(wait_mutex);
            while(!display_stop.stop_requested()) {
                publish_capture(capture_display);
                condition.wait_for(lock,display_stop,std::chrono::milliseconds(100),[]{return false;});
            }
        });
        execution::Task capture([&](std::stop_token capture_stop) {
            try {
                audio::Options options;
                options.transmit_gain=s.transmit_gain;
                options.exclusive=s.exclusive;
                options.capture_discontinuity=[&queue] {
                    constexpr auto reason="Fast capture discontinuity: missing sample time; transfer incomplete";
                    queue.close(reason);
                    throw Error(reason);
                };
                audio::capture(s.profile.sample_rate,s.device,[&](std::span<const float> samples) {
                    if(capture_stop.stop_requested() || stop.stop_requested())return false;
                    capture_display.record_pcm(samples);
                    return queue.push(samples);
                },capture_stop,[&](const auto& format) {
                    try {check_format(s,format);}
                    catch(const std::exception& error) {queue.close(error.what());throw;}
                },std::move(options));
            } catch(const std::exception& e) {
                if(!detail::expected_audio_cancellation(e,capture_stop))queue.close(e.what());
            }
            queue.close();
        });
        std::stop_callback cancel_capture(stop,[&]{capture.request_stop();});
        std::array<float,4096> block{};
        while(!stop.stop_requested()) {
            execution::checkpoint();
            const auto count=queue.pop(block,stop);
            if(!count)break;
            const auto samples=std::span<const float>(block).first(count);
            const auto consumed=receiver.consume(samples);
            observe_receive(samples.first(consumed),receiver,decoder,telemetry,nullptr,0,&capture_display);
            if(receiver.progress().physical_complete)break;
        }
        capture.request_stop();capture.join();
        display.request_stop();display.join();
        publish_capture(capture_display);
        const auto captured=queue.status();
        update([&](auto& out){out.peak_capture_backlog_samples=captured.peak_samples;});
        if(!stop.stop_requested()) {
            if(captured.overflow)throw Error("Fast capture overrun: missing sample time; transfer incomplete");
            if(!captured.error.empty())throw Error(captured.error);
        }
        finish_receive(stop,receiver,decoder);
    }
    void simulate(std::stop_token stop, const Settings& s, Transmitter& transmitter,
                  Telemetry& telemetry, std::uint64_t estimated_samples) {
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        auto receiver=make_receiver(s,decoder,telemetry,stop);
        SimulationChannel channel(s.profile,*s.simulation,[&](std::span<std::complex<float>> output) {
            return stop.stop_requested()?std::size_t{0}:transmitter.read_analytic(output);
        });
        std::array<float,1024> block{};
        bool early_completion=false;
        while(!stop.stop_requested()) {
            execution::checkpoint();
            const bool quiet=channel.report().quiet_start_sample.has_value();
            const auto count=channel.read(block);
            if(!count||stop.stop_requested())break;
            const auto samples=std::span<const float>(block).first(count);
            const auto consumed=receiver.consume(samples);
            observe_receive(samples.first(consumed),receiver,decoder,telemetry,&channel.report(),estimated_samples);
            if(receiver.progress().physical_complete) {
                // Source/channel state can invalidate a simulation result, but
                // only the receiver's scored absence can complete it.
                if(receiver.uses_delay_doppler()) {
                    const auto end=receiver.physical_completion_input_tick();
                    const auto start=channel.report().quiet_start_sample;
                    early_completion=!end || !start || *end<*start || *end-*start<6ULL*s.profile.sample_rate;
                } else early_completion=!quiet;
                break;
            }
        }
        finish_receive(stop,receiver,decoder,early_completion);
    }
    void loopback(std::stop_token stop,const Settings& s,const SimulationSettings& effects,std::uint64_t stream_id,
                  const std::filesystem::path& path,const std::optional<std::string>& text) {
        const auto deadline=Clock::now()+std::chrono::seconds(110);
        const auto audio_limit=75ULL*s.profile.sample_rate;
        std::stop_source receiver_stop;
        std::stop_callback cancel_receiver(stop,[&]{receiver_stop.request_stop();});
        const auto token=receiver_stop.get_token();
        // Preparation and receiver construction precede the audio watchdog.
        // Cover them with the same transaction deadline and cancellation token.
        execution::Task preparation_guard([&](std::stop_token guard_stop) {
            execution::Mutex wait_mutex;execution::StopCondition condition;
            std::unique_lock lock(wait_mutex);
            while(!guard_stop.stop_requested()&&Clock::now()<deadline) {
                condition.wait_until(lock,guard_stop,std::min(deadline,Clock::now()+std::chrono::milliseconds(100)),[]{return false;});
                if(!guard_stop.stop_requested())update([](auto&){});
            }
            if(!guard_stop.stop_requested())receiver_stop.request_stop();
        });
        std::string filename;
        SourceHash expected_hash{};
        PreparedXzSource input;
        if(text) input=prepare_xz_source(byte_source(Bytes(text->begin(),text->end())),s.quota_bytes,token);
        else {
            filename=attachment::filename_from_path(path);
            auto source=checked_file_source(path,s.quota_bytes);
            SourceDigest digest;
            input=prepare_xz_attachment([&](std::span<std::uint8_t> output) {
                if(Clock::now()>=deadline)throw Error("110-second cable test time limit reached during source preparation");
                const auto count=source(output);
                digest.append(output.first(count));
                return count;
            },filename,s.quota_bytes,token);
            expected_hash=digest.finish();
        }
        if(!input.source_bytes)throw Error("Choose nonempty Fast cable test text or file content");
        const auto source_bytes=input.source_bytes;
        const auto estimate=estimate_transmission(s.profile,s.key.has_value(),input.encoded.size());
        if(estimate.samples>audio_limit-detail::cable_capture_preroll(s.profile.sample_rate))
            throw Error("The selected Fast source and profile exceed the 75-second cable test audio limit");
        StreamEncoder encoder(s.profile,s.key,byte_source(std::move(input.encoded)),SourceEncoding::xz);
        Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
            return !token.stop_requested() && encoder.next_interval(bits);
        });
        SimulationChannel channel(s.profile,effects,[&](std::span<std::complex<float>> output) {
            return token.stop_requested()?std::size_t{0}:transmitter.read_analytic(output);
        },ChannelPurpose::live_playback);
        Telemetry telemetry(s.profile,false,stream_id);
        detail::CaptureTelemetry capture_display(s.profile,stream_id,detail::cable_capture_preroll(s.profile.sample_rate));
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        auto receiver=make_receiver(s,decoder,telemetry,token);
        execution::Mutex report_mutex;
        auto published=channel.report();
        update([&](auto& out) {
            out.estimated_seconds=estimate.seconds;
            out.loopback_stage="Opening audio input";
            out.receiver_backend=receiver.backend_name();
            out.capture_queue_capacity_samples=detail::receive_pcm_capacity(s.profile);
        });
        const detail::CableOptions options{s.profile,s.device,s.device,audio::output_channels(s.mono,s.channel_mode),
            s.transmit_gain,detail::cable_upper_hz(s.profile,published),s.exclusive,audio_limit,deadline};
        detail::CableCallbacks callbacks;
        bool early_completion=false;
        bool acquired=false;
        callbacks.capture_observer=[&](std::span<const float> pcm) {
            capture_display.record_pcm(pcm);
            return true;
        };
        callbacks.publish_display=[&]{publish_capture(capture_display);};
        callbacks.publish_progress=[&](const detail::CableProgress& progress) {
            update([&](auto& out) {
                switch(progress.phase) {
                case detail::CablePhase::opening_capture:out.loopback_stage="Opening audio input";break;
                case detail::CablePhase::opening_playback:out.loopback_stage="Opening audio output";break;
                case detail::CablePhase::sending:out.loopback_stage="Sending";break;
                case detail::CablePhase::waiting_completion:out.loopback_stage="Waiting for physical completion";break;
                case detail::CablePhase::closing:out.loopback_stage="Closing audio";break;
                }
                out.transmitted_samples=progress.transmitted_samples;
                out.captured_samples=progress.captured_samples;
            });
        };
        callbacks.produce=[&](std::span<float> pcm) {
            const auto count=channel.read(pcm);
            const auto report=channel.report();
            {
                const std::lock_guard lock(report_mutex);
                published=report;
            }
            return detail::CableProduced{count,report.quiet_start_sample.has_value()};
        };
        callbacks.consume=[&](std::span<const float> pcm,bool quiet_before) {
            const auto consumed=receiver.consume(pcm);
            SimulationReport report;
            {
                const std::lock_guard lock(report_mutex);
                report=published;
            }
            observe_receive(pcm.first(consumed),receiver,decoder,telemetry,&report,estimate.samples,&capture_display);
            const auto ended=receiver.progress().physical_complete;
            acquired=acquired||receiver.progress().acquired;
            early_completion=ended&&!quiet_before;
            return detail::CableConsumed{consumed,ended};
        };
        preparation_guard.request_stop();preparation_guard.join();
        if(Clock::now()>=deadline)throw Error("110-second cable test time limit reached during preparation");
        const auto transport=detail::run_cable_loopback(options,callbacks,receiver_stop);
        callbacks.publish_display();
        update([&](auto& out){out.loopback_stage="Verifying received source";});
        receiver.finish();
        const bool end=!stop.stop_requested() && transport.end==detail::CableEnd::receiver &&
            transport.error.empty() && receiver.progress().physical_complete && !early_completion;
        decoder.finish(end);
        const auto decoded=decoder.snapshot();
        const auto file=decoder.result();
        bool passed=false;
        if(end && decoded.complete && (!s.key || decoded.authenticated) && file) {
            if(text)passed=received_text_matches(*file,*text);
            else if(file->is_attachment() && file->filename()==filename && file->size()==source_bytes) {
                SourceDigest digest;
                digest.append(file->bytes());
                passed=digest.finish()==expected_hash;
            }
        }
        update([&](auto& out) {
            const bool publish=!stop.stop_requested();
            const bool timed_out=Clock::now()>=deadline;
            const bool physical=end&&publish&&!timed_out;
            const bool complete=passed&&physical;
            out.simulation=channel.report();
            out.capture_queue_capacity_samples=transport.capacity_samples;
            out.capture_queue_workspace_bytes=transport.workspace_bytes;
            out.peak_capture_backlog_samples=transport.peak_samples;
            out.transmitted_samples=transport.transmitted_samples;
            out.captured_samples=transport.captured_samples;
            out.physical_complete=physical;
            out.physical_completion_input_tick=physical?receiver.physical_completion_input_tick():std::nullopt;
            out.complete=complete;
            out.authenticated=complete&&decoded.authenticated;
            out.source_bytes=complete?source_bytes:0;
            // Publish the actual decoded receive only after the bounded cable
            // test has proved physical completion, integrity and exact source
            // equality. Failed, cancelled and mismatched tests expose no file.
            out.file=complete?file:nullptr;
            out.loopback_stage=complete?"Complete":!publish?"Cancelled":"Incomplete";
            if(!publish)out.status="Cable test cancelled; transfer incomplete";
            else if(complete) {
                out.transmit_fraction=1.;
                out.status=std::string("Cable test passed · exact selected ")+(text?"text":"file")+
                    " received ("+std::to_string(source_bytes)+" bytes)";
            } else {
                const auto reason=!transport.error.empty()?transport.error:
                    timed_out?std::string(cable_failure(detail::CableEnd::wall_limit)):
                    early_completion?std::string("receiver ended before playback reached quiet"):
                    end?(decoded.complete?std::string("received source differed from the selected input"):decoded.status):
                        transport.end==detail::CableEnd::no_completion&&!acquired?
                            std::string("no Fast training acquired before playback and the receive tail ended"):
                            std::string(cable_failure(transport.end));
                out.status="Cable test incomplete · "+reason;
            }
            if(!complete&&publish)out.error=out.status;
        });
    }
    void send(std::stop_token stop,const Settings& s,PreparedXzSource input,bool text,std::uint64_t stream_id) {
        const auto estimate=estimate_transmission(s.profile,s.key.has_value(),input.encoded.size());
        const auto source_bytes=input.source_bytes;
        std::uint64_t generated=0;
        update([&](auto& out){out.estimated_seconds=estimate.seconds;});
        Telemetry telemetry(s.profile,!s.simulation,stream_id);
        StreamEncoder encoder(s.profile,s.key,byte_source(std::move(input.encoded)),SourceEncoding::xz);
        Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
            if(stop.stop_requested())return false;
            return encoder.next_interval(bits);
        },[&](std::complex<float> symbol) noexcept {if(!s.simulation)telemetry.record_symbol(symbol);});
        if(s.simulation) {
            simulate(stop,s,transmitter,telemetry,estimate.samples);
            return;
        }
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
    void launch(bool tx,const std::filesystem::path& path={},std::optional<std::string> text={},
                std::optional<SimulationSettings> cable={}) {
        Settings s;
        std::uint64_t stream_id=0;
        {
            std::lock_guard lock(mutex);
            if(closing)throw Error("Fast session is closing");
            if(current.active)throw Error("Fast audio is already active");
            check_settings(settings);
            if(cable) {
                if(settings.simulation)throw Error("Choose live audio before starting a Fast cable test");
                validate(settings.profile,*cable,ChannelPurpose::live_playback);
                if(text?text->empty():path.empty())throw Error("Choose nonempty Fast cable test text or a source file");
            }
            if(!tx&&settings.simulation)throw Error("Fast simulation starts from a local transmission");
            if(text&&(text->size()>text_byte_limit||text->size()>settings.quota_bytes))
                throw Error("Fast text exceeds the local 32768-byte limit");
            s=settings;
            const auto revision=current.revision+1;current={};current.revision=revision;
            current.active=true;current.transmitting=tx;current.listening=!tx||cable.has_value();
            current.encrypted=s.key.has_value();current.simulated=s.simulation.has_value();
            current.loopback_test=cable.has_value();
            if(cable)current.loopback_stage="Preparing source";
            stream_id=next_diagnostics_stream_id();
            current.diagnostics=initial_diagnostics(s.profile,tx&&!s.simulation&&!cable,stream_id);
            current.status=cable?"Preparing Fast cable test":s.simulation?"Preparing Fast simulation":
                tx?"Preparing fast transmission":"Opening fast audio input";
            started=Clock::now();
        }
        if(worker.joinable())worker.join();
        worker=execution::Task([this,s=std::move(s),tx,path,text=std::move(text),cable=std::move(cable),stream_id](std::stop_token stop) {
            try {
                if(cable)loopback(stop,s,*cable,stream_id,path,text);
                else if(!tx)receive(stop,s,stream_id);
                else if(text)send(stop,s,prepare_xz_source(byte_source(Bytes(text->begin(),text->end())),s.quota_bytes,stop),true,stream_id);
                else {
                    auto source=checked_file_source(path,s.quota_bytes);
                    send(stop,s,prepare_xz_attachment(std::move(source),attachment::filename_from_path(path),s.quota_bytes,stop),false,stream_id);
                }
            }
            catch(const std::exception& e) {update([&](auto& out) {
                if(!stop.stop_requested())out.error=out.loopback_test&&Clock::now()-started>=std::chrono::seconds(110)?
                    "110-second cable test time limit reached":e.what();
                out.status=out.loopback_test?"Cable test incomplete":"Fast transfer incomplete";
                if(out.loopback_test)out.loopback_stage=stop.stop_requested()?"Cancelled":"Incomplete";
            });}
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
void Session::test_loopback_text(const SimulationSettings& channel,const std::string& text){impl_->launch(true,{},text,channel);}
void Session::test_loopback_file(const SimulationSettings& channel,const std::filesystem::path& source){impl_->launch(true,source,{},channel);}
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
