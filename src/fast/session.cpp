#include "datapump/fast/session.hpp"
#include "cable_loopback.hpp"
#include "datapump/execution.hpp"
#include "receive_pcm_queue.hpp"
#include "session_state.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/audio.hpp"
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
constexpr std::string_view cable_message="DataPump Fast audio cable test.";
void check_settings(const Settings& s) {
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
Receiver make_receiver(const Profile& profile,StreamDecoder& decoder,Telemetry& telemetry,std::stop_token stop) {
    return Receiver(profile,[&decoder](std::span<const float> interval,bool feedback) {
            return decoder.push_interval(interval,feedback);
        },[&telemetry](std::complex<float> symbol) noexcept {telemetry.record_symbol(symbol);},
        [&telemetry](std::complex<float> input) noexcept {telemetry.record_input(input);},stop);
}
std::string_view cable_failure(detail::CableEnd end) noexcept {
    switch(end) {
    case detail::CableEnd::capture_eof:return "capture ended before reception completed";
    case detail::CableEnd::playback_eof:return "playback ended before supplying the diagnostic";
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
    void observe_receive(std::span<const float> samples, const Receiver& receiver,
                         const StreamDecoder& decoder, Telemetry& telemetry,
                         const SimulationReport* simulation = nullptr, std::uint64_t estimated_samples = 0) {
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
            const auto status=coding.decoding_stopped?"File decoding stopped: "+coding.status:
                coding.failed_cycles?"Receiving fast stream with missing data; continuing decoding":
                dsp.acquired?"Receiving fast stream; content pending":"Listening for Fast modem training";
            out.status=out.loopback_test?"Cable test · "+status:simulation?"Simulation · "+status:status;
            out.receiver_backend=receiver.backend_name();
            if(out.receiver_backend=="standard-ssb")out.status+=" · Standard SSB receiver for this profile";
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
            if(out.receiver_backend=="standard-ssb")out.status+=" · Standard SSB receiver for this profile";
            if((end||out.simulated)&&!result.complete&&!stop.stop_requested())out.error=out.status;
        });
    }
    void receive(std::stop_token stop,const Settings& s,std::uint64_t stream_id) {
        Telemetry telemetry(s.profile,false,stream_id);
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        auto receiver=make_receiver(s.profile,decoder,telemetry,stop);
        detail::ReceivePcmQueue queue(detail::receive_pcm_capacity(s.profile));
        update([&](auto& out) {
            out.receiver_backend=receiver.backend_name();
            out.capture_queue_capacity_samples=queue.capacity();
            out.capture_queue_workspace_bytes=queue.workspace_bytes();
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
                    return !capture_stop.stop_requested() && !stop.stop_requested() && queue.push(samples);
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
            observe_receive(samples.first(consumed),receiver,decoder,telemetry);
            if(receiver.progress().physical_complete)break;
        }
        capture.request_stop();capture.join();
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
        auto receiver=make_receiver(s.profile,decoder,telemetry,stop);
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
                if(receiver.uses_resilient_ssb()) {
                    const auto end=receiver.physical_completion_input_tick();
                    const auto start=channel.report().quiet_start_sample;
                    early_completion=!end || !start || *end<*start || *end-*start<6ULL*s.profile.sample_rate;
                } else early_completion=!quiet;
                break;
            }
        }
        finish_receive(stop,receiver,decoder,early_completion);
    }
    void loopback(std::stop_token stop,const Settings& s,const SimulationSettings& effects,std::uint64_t stream_id) {
        const auto deadline=Clock::now()+std::chrono::seconds(110);
        const auto audio_limit=75ULL*s.profile.sample_rate;
        auto input=prepare_xz_source(byte_source(Bytes(cable_message.begin(),cable_message.end())),s.quota_bytes,stop);
        const auto estimate=estimate_transmission(s.profile,s.key.has_value(),input.encoded.size());
        if(estimate.samples>audio_limit-s.profile.sample_rate/4)
            throw Error("The selected Fast profile exceeds the 75-second cable test audio limit");
        std::stop_source receiver_stop;
        std::stop_callback cancel_receiver(stop,[&]{receiver_stop.request_stop();});
        const auto token=receiver_stop.get_token();
        StreamEncoder encoder(s.profile,s.key,byte_source(std::move(input.encoded)),SourceEncoding::xz);
        Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
            return !token.stop_requested() && encoder.next_interval(bits);
        });
        SimulationChannel channel(s.profile,effects,[&](std::span<std::complex<float>> output) {
            return token.stop_requested()?std::size_t{0}:transmitter.read_analytic(output);
        });
        Telemetry telemetry(s.profile,false,stream_id);
        StreamDecoder decoder(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);
        auto receiver=make_receiver(s.profile,decoder,telemetry,token);
        execution::Mutex report_mutex;
        auto published=channel.report();
        update([&](auto& out) {
            out.estimated_seconds=estimate.seconds;
            out.receiver_backend=receiver.backend_name();
            out.capture_queue_capacity_samples=detail::receive_pcm_capacity(s.profile);
        });
        const detail::CableOptions options{s.profile,s.device,s.device,audio::output_channels(s.mono,s.channel_mode),
            .1,detail::cable_upper_hz(s.profile,published),s.exclusive,audio_limit,deadline};
        detail::CableCallbacks callbacks;
        bool early_completion=false;
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
            observe_receive(pcm.first(consumed),receiver,decoder,telemetry,&report,estimate.samples);
            const auto ended=receiver.progress().physical_complete;
            early_completion=ended&&!quiet_before;
            return detail::CableConsumed{consumed,ended};
        };
        const auto transport=detail::run_cable_loopback(options,callbacks,receiver_stop);
        receiver.finish();
        const bool end=!stop.stop_requested() && transport.end==detail::CableEnd::receiver &&
            transport.error.empty() && receiver.progress().physical_complete && !early_completion;
        decoder.finish(end);
        const auto decoded=decoder.snapshot();
        const auto file=decoder.result();
        const bool passed=end && decoded.complete && (!s.key || decoded.authenticated) && file &&
            !file->is_attachment() && std::ranges::equal(file->bytes(),cable_message);
        update([&](auto& out) {
            const bool publish=!stop.stop_requested();
            const bool physical=end&&publish;
            const bool complete=passed&&publish;
            out.simulation=channel.report();
            out.capture_queue_capacity_samples=transport.capacity_samples;
            out.capture_queue_workspace_bytes=transport.workspace_bytes;
            out.peak_capture_backlog_samples=transport.peak_samples;
            out.physical_complete=physical;
            out.physical_completion_input_tick=physical?receiver.physical_completion_input_tick():std::nullopt;
            out.complete=complete;
            out.authenticated=complete&&decoded.authenticated;
            out.source_bytes=complete?decoded.source_bytes:0;
            out.file.reset();
            if(!publish)out.status="Cable test cancelled; transfer incomplete";
            else if(complete) {
                out.transmit_fraction=1.;
                out.status="Cable test passed · exact diagnostic message received";
            } else {
                const auto reason=!transport.error.empty()?transport.error:
                    early_completion?std::string("receiver ended before playback reached quiet"):
                    end?(decoded.complete?std::string("received diagnostic message differed"):decoded.status):
                        std::string(cable_failure(transport.end));
                out.status="Cable test incomplete · "+reason;
            }
            if(out.receiver_backend=="standard-ssb")out.status+=" · Standard SSB receiver for this profile";
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
                validate(settings.profile,*cable);
            }
            if(!tx&&settings.simulation)throw Error("Fast simulation starts from a local transmission");
            if(text&&(text->size()>text_byte_limit||text->size()>settings.quota_bytes))
                throw Error("Fast text exceeds the local 32768-byte limit");
            s=settings;
            const auto revision=current.revision+1;current={};current.revision=revision;
            current.active=true;current.transmitting=tx;current.listening=!tx||cable.has_value();
            current.encrypted=s.key.has_value();current.simulated=s.simulation.has_value();
            current.loopback_test=cable.has_value();
            stream_id=next_diagnostics_stream_id();
            current.diagnostics=initial_diagnostics(s.profile,tx&&!s.simulation&&!cable,stream_id);
            current.status=cable?"Preparing Fast cable test":s.simulation?"Preparing Fast simulation":
                tx?"Preparing fast transmission":"Opening fast audio input";
            started=Clock::now();
        }
        if(worker.joinable())worker.join();
        worker=execution::Task([this,s=std::move(s),tx,path,text=std::move(text),cable=std::move(cable),stream_id](std::stop_token stop) {
            try {
                if(cable)loopback(stop,s,*cable,stream_id);
                else if(!tx)receive(stop,s,stream_id);
                else if(text)send(stop,s,prepare_xz_source(byte_source(Bytes(text->begin(),text->end())),s.quota_bytes,stop),true,stream_id);
                else {
                    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
                    if(ec||!std::filesystem::is_regular_file(path))throw Error("Fast source must be a readable regular file");
                    if(size>s.quota_bytes)throw Error("Fast source exceeds local storage quota");
                    send(stop,s,prepare_xz_attachment(file_source(path),attachment::filename_from_path(path),s.quota_bytes,stop),false,stream_id);
                }
            }
            catch(const std::exception& e) {update([&](auto& out) {
                if(!stop.stop_requested())out.error=e.what();
                out.status=out.loopback_test?"Cable test incomplete":"Fast transfer incomplete";
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
void Session::test_loopback(const SimulationSettings& channel){impl_->launch(true,{},{},channel);}
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
