#include "datapump/audio.hpp"
#include "../src/signal_view.hpp"
#include "../src/audio_trace.hpp"
#include "../src/scheduled_exact_source_samples.hpp"
#include "../src/scheduled_acquisition_certificate.hpp"
#include "../src/live_receiver_layout.hpp"
#include "datapump/sched_hop.hpp"
#include <fstream>
#include <cstdlib>
#include <numbers>
#include "cpu_work_budget.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace datapump;
using namespace std::chrono_literals;

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw Error(message);
}

#ifdef _WIN32
// The simulated capture yields for 1 ms after each unchanged PCM chunk.
// A coarse Windows timer can round that yield to 15.6 ms and make the fixture
// itself exceed its deadline before delivering all samples to an idle decoder.
// Scope the intended timer resolution to this test process, not the application.
struct FixtureTimerResolution {
    FixtureTimerResolution() {
        check(timeBeginPeriod(1) == TIMERR_NOERROR,
              "could not request the profile fixture's 1 ms timer resolution");
    }
    ~FixtureTimerResolution() { timeEndPeriod(1); }
    FixtureTimerResolution(const FixtureTimerResolution&) = delete;
    FixtureTimerResolution& operator=(const FixtureTimerResolution&) = delete;
};
#endif

// Supply the ordinary hardware capture/decoder queue with deterministic PCM.
// Keeping capture open at a checkpoint also proves that neither an audio EOF
// nor the end of a source token is used to manufacture physical completion.
struct CaptureScript {
    std::vector<float> samples;
    std::uint32_t rate = 0;
    std::atomic<std::size_t> released{0}, delivered{0};
    std::atomic<unsigned> capture_opened{0},capture_closed{0};
    std::size_t format_bytes=0;
    bool loopback=false;
    // Optional test-only virtual channel: hold playback until complete, then
    // apply the same selected simulation settings before releasing capture.
    std::optional<modem::ChannelConfig> loopback_channel;
    modem::Config loopback_modem;
    bool buffer_playback=false;
    std::size_t playback_chunk=0;
    std::vector<float> playback_input;
    std::atomic<bool> playback_started{false},playback_source_finished{false},finish_playback{false};
    std::atomic<std::size_t> played{0};
    std::atomic<unsigned> source_calls{0};
    std::atomic<bool> restart_capture{false};
    bool fail_playback_before_source=false,fail_playback_after_source=false;
    std::vector<float> monitor_samples;
    std::uint32_t monitor_rate=0;
    std::size_t monitor_format_bytes=0;
    std::atomic<std::size_t> monitor_released{0},monitor_delivered{0};
    std::atomic<unsigned> interruptions{0},interruptions_delivered{0};
    std::function<void()> after_monitor;
    std::function<void(std::size_t)> before_capture;
    std::function<audio::TimePrediction(std::size_t)> timing;
    std::optional<audio::TimingStatus> timing_status;
};
CaptureScript* capture_script = nullptr;
constexpr std::uint64_t epoch = 1800000000;

struct Waveform {
    std::vector<float> samples;
    std::string text, bits;
    std::size_t payload_end = 0, transmit_end = 0;
    std::uint32_t rate = 0;
    std::size_t symbol_samples = 0;
    bool interval = false, binary = false, ambiguous_tone = false;
};

struct Configuration {
    double bandwidth = 3600;
    std::optional<double> carrier;
    tuning::PatternMode mode = tuning::PatternMode::auto_pattern;
    float noise_sigma = 0;
    bool keyed = false;
};

modem::Config modem_config(double target, const Configuration& configuration) {
    return tuning::resolve(configuration.bandwidth, target, configuration.mode, configuration.keyed,
                           configuration.carrier).config;
}

Waveform waveform(double target, std::string text, bool binary = false,
                  const Configuration& configuration = {}) {
    transfer::Options options;
    options.modem = modem_config(target, configuration);
    options.timestamp = epoch;
    options.search_seconds = 0;
    if (configuration.keyed) options.key.emplace(Bytes(32, 0x67));
    Message message;
    message.data.assign(text.begin(), text.end());
    auto bits = transfer::message_wire_bits(message, options);
    if (configuration.keyed) transfer::xor_binary_bits(bits, options);
    if (binary) {
        bits.clear();
        for (char digit : text) {
            check(digit == '0' || digit == '1', "invalid explicit binary fixture");
            bits.push_back(static_cast<std::uint8_t>(digit - '0'));
        }
    }
    Waveform result;
    result.text = binary && text == "001" ? "e" : text;
    result.interval = !binary && !transfer::uses_raw_message(message);
    result.binary = binary && text != "001";
    result.ambiguous_tone = tuning::tone_mode(configuration.mode);
    for (auto bit : bits) result.bits.push_back(bit ? '1' : '0');
    if (!result.interval && !binary)
        check(result.bits == (text == "e" ? "001" : "110001001110100110100100"),
              "short dictionary fixture changed its independent wire vector");
    if (binary) {
        auto transmitter = transfer::binary_transmitter(bits, options);
        result.samples.resize(transmitter->total_samples());
        for (std::size_t offset = 0; offset < result.samples.size();)
            offset += transmitter->read(std::span(result.samples).subspan(offset));
    } else result.samples = transfer::transmit(message, options);
    result.transmit_end = result.samples.size();
    result.rate = options.modem.sample_rate;
    result.symbol_samples = modem::symbol_sample_count(options.modem);
    result.payload_end = modem::training_sample_count(options.modem) +
        modem::pattern_pulse_padding_samples(options.modem) + bits.size() * modem::symbol_sample_count(options.modem);
    // The modulator already emits its suppression tail. Include complete
    // absent symbols and the FFT search lookahead, including for long symbols.
    const auto absent_symbols = (6 * result.rate + result.symbol_samples - 1) / result.symbol_samples;
    result.samples.resize(result.payload_end + absent_symbols * result.symbol_samples +
                          std::max<std::size_t>(2 * result.rate, 2 * result.symbol_samples));
    if (configuration.noise_sigma) {
        std::mt19937 random(714);
        std::normal_distribution<float> noise(0, configuration.noise_sigma);
        for (auto& sample : result.samples) sample += noise(random);
    }
    return result;
}

live::Settings settings(std::vector<double> targets, const Configuration& configuration = {}) {
    live::Settings value;
    value.simulation = false;
    value.device = "controlled profile capture";
    value.transfer.modem = modem_config(55, configuration);
    value.transfer.timestamp = epoch;
    value.transfer.search_seconds = 0;
    value.transfer.automatic_receive_profiles = true;
    value.transfer.receive_targets_db_hz = std::move(targets);
    value.transfer.receive_pattern_mode = configuration.mode;
    if (configuration.keyed) {
        value.transfer.key.emplace(Bytes(32, 0x67));
        value.receive_keys.emplace_back(Bytes(32, 0x68));
    }
    value.content_limit = value.transfer.content_limit = 1024 * 1024;
    value.dsp_workspace_bytes = value.transfer.dsp_workspace_bytes = 32 * 1024 * 1024;
    return value;
}

struct Observations {
    std::set<std::uint64_t> ids;
    std::set<std::string> prefixes;
    std::map<std::uint64_t, live::SignalUpdate> rows;
    std::map<std::uint64_t, std::uint64_t> revisions;
    std::map<std::string, transfer::Received> content;
    std::size_t complete = 0, received = 0;
    bool allow_revisions = false;
    std::size_t workspace_limit = 32 * 1024 * 1024;

    void check_final(const Waveform& expected) const {
        check(rows.size() == 1, "one physical transmission retained multiple effective signal rows");
        const auto& signal = rows.begin()->second;
        check(!ids.empty() && signal.id == *ids.begin(),
              "stronger profile abandoned the original reception identity");
        if (expected.ambiguous_tone) {
            // Equal consecutive tones carry no observable symbol boundary.
            // Check coherent presentation, without inventing a profile flag
            // or an originating bit count absent from these waveforms.
            check(signal.complete && !signal.validated && signal.received_bits && !signal.missing_symbols,
                  "ambiguous tone reception did not retain one completed unvalidated interpretation");
            const auto& bits = signal.binary ? signal.text : signal.raw_bits;
            check(bits.size() == signal.received_bits && bits.find_first_not_of("01") == std::string::npos,
                  "tone interpretation lost its exact observed raw bits");
            check(content.size() == (signal.binary ? 0 : 1),
                  "ambiguous tone reception retained duplicate content");
            if (!content.empty()) {
                const auto& result = content.begin()->second;
                check(result.stream_complete && result.short_text_decoded && !result.content_validated &&
                      !result.content.authenticated && result.observed_bits == signal.received_bits,
                      "ambiguous tone result disagrees with its one completed row");
            }
            return;
        }
        check(signal.complete && signal.binary == expected.binary &&
              signal.validated == expected.interval && signal.text == expected.text &&
              signal.raw_bits == (expected.interval || expected.binary ? std::string{} : expected.bits) &&
              signal.received_bits == expected.bits.size() && signal.missing_symbols == 0,
              "winning profile completed with bogus text or incorrect transport metadata");
        check(content.size() == (expected.binary ? 0 : 1),
              "one transmission retained an incorrect number of effective received items");
        if (!content.empty()) check_received(content.begin()->second, expected);
    }

    static void check_received(const transfer::Received& result, const Waveform& expected) {
        check(result.stream_complete && result.short_text_decoded == !expected.interval &&
              result.content_validated == expected.interval &&
              !result.content.authenticated && result.observed_bits == expected.bits.size() && result.missing_symbols == 0,
              "completed profile result lost its source validation or public-stream semantics");
        check(std::string(result.content.message.data.begin(), result.content.message.data.end()) == expected.text,
              "a secondary profile published bogus received content");
        std::string raw;
        for (auto bit : result.raw_bits) raw.push_back(bit ? '1' : '0');
        check(raw == expected.bits.substr(0, 4096), "completed profile result changed exact transport bits");
    }

    void inspect(const live::Snapshot& snapshot, const Waveform& expected,
                 std::size_t sample_origin, bool before_absence) {
        check(snapshot.error.empty(), "live profile capture failed: " + snapshot.error);
        check(snapshot.status.find("overrun") == std::string::npos,
              "profile fixture overran the hardware decode queue");
        check(snapshot.dsp_buffered_bytes <= workspace_limit,
              "profile bank exceeded its DSP workspace");
        for (const auto& signal : snapshot.signals) {
            ids.insert(signal.id);
            if (!allow_revisions)
                check(ids.size() == 1, "one physical transmission created multiple live signal IDs");
            const auto prior = rows.find(signal.id);
            if (signal.revision < revisions[signal.id]) continue;
            revisions[signal.id] = signal.revision;
            for (const auto superseded : signal.superseded_ids) {
                revisions[superseded] = std::max(revisions[superseded], signal.revision);
                const auto obsolete = rows.find(superseded);
                if (obsolete != rows.end()) content.erase(obsolete->second.reception_id);
                rows.erase(superseded);
            }
            if (prior != rows.end() && signal.revision > prior->second.revision)
                content.erase(prior->second.reception_id);
            rows[signal.id] = signal;
            if (!signal.complete) {
                check(signal.binary && !signal.validated && signal.raw_bits.empty(),
                      "pending profile observation acquired completed text semantics");
                prefixes.insert(signal.text);
                continue;
            }
            if (!allow_revisions)
                check(!before_absence && snapshot.samples_received >= sample_origin + expected.payload_end + 6 * expected.rate,
                      "profile observation completed before six seconds of physical absence: input="+
                      std::to_string(snapshot.samples_received)+" payload_end="+std::to_string(expected.payload_end)+
                      " bits="+signal.raw_bits+" text="+signal.text+" count="+std::to_string(signal.received_bits)+" before_absence="+std::to_string(before_absence));
            ++complete;
            if (!allow_revisions && !expected.ambiguous_tone) check(signal.binary == expected.binary && signal.validated == expected.interval && signal.text == expected.text &&
                  signal.raw_bits == (expected.interval || expected.binary ? std::string{} : expected.bits) &&
                  signal.received_bits == expected.bits.size() && signal.missing_symbols == 0,
                  "winning profile completed with bogus text or incorrect transport metadata");
        }
        for (const auto& result : snapshot.received) {
            if (!allow_revisions) check(!before_absence, "source content was interpreted before physical absence");
            ++received;
            std::string id;
            constexpr char digits[] = "0123456789abcdef";
            for (auto byte : result.content.message.local_id) {
                id += digits[byte >> 4];
                id += digits[byte & 15];
            }
            content[id] = result;
            if (!allow_revisions && !expected.ambiguous_tone) check_received(result, expected);
        }
    }
};

void advance(live::Session& session, CaptureScript& capture, std::size_t target,
             Observations& observed, const Waveform& expected, std::size_t origin, bool before_absence) {
    testing::CpuWorkBudget budget(30s,"controlled profile PCM decoding");
    std::optional<std::chrono::steady_clock::time_point> drained_at;
    while (budget.pending()) {
        auto snapshot = session.snapshot();
        observed.inspect(snapshot, expected, origin, before_absence);
        // This functional arbitration fixture advances a controlled PCM clock;
        // it is not the separate fast_session real-time throughput check. The
        // old unconditional 20 ms / 1 ms producer could outrun instrumented
        // decoding and discard a valid waveform. Keep only a short burst ahead
        // and wait for the actual asynchronous queue to drain before granting
        // more input. The production queue, its overrun assertion and every
        // physical-symbol/end assertion stay unchanged.
        const auto delivered = capture.delivered.load();
        if (delivered < target && delivered >= capture.released.load() && snapshot.buffered_samples == 0) {
            const auto burst = std::max<std::size_t>(8, capture.rate / 10);
            capture.released = delivered + std::min(burst, target - delivered);
        }
        if (capture.delivered >= target && snapshot.buffered_samples == 0) {
            if (!drained_at) drained_at = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - *drained_at >= 250ms) return;
        } else drained_at.reset();
        std::this_thread::sleep_for(1ms);
    }
    throw Error("controlled profile capture did not drain its bounded decoder queue: delivered=" +
                std::to_string(capture.delivered.load()) + " target=" + std::to_string(target) +
                " buffered=" + std::to_string(session.snapshot().buffered_samples));
}

void receive_wave(live::Session& session, CaptureScript& capture, const Waveform& expected,
                  std::size_t origin, Observations& observed) {
    advance(session, capture, origin + expected.payload_end + 5 * expected.rate,
            observed, expected, origin, true);
    check(!observed.ids.empty() && (observed.allow_revisions || (observed.complete == 0 && observed.received == 0)),
          "profile bank hid pending data or completed without physical absence");
    if (expected.symbol_samples > 6 * expected.rate) {
        // More than six seconds of silence is insufficient while the absent
        // symbol itself still has not been observed through its endpoint. The
        // FFT's lookahead can consume most of that window before admitting the
        // last payload bit, so assert its prefix after that work is available.
        advance(session, capture, origin + expected.payload_end + expected.symbol_samples - 1,
                observed, expected, origin, true);
        check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete,
              "long profile completed before a full absent symbol was scored");
        std::string prefixes;for(const auto& prefix:observed.prefixes)prefixes+='['+prefix+"] ";
        for (std::size_t count = 1; count <= expected.bits.size(); ++count)
            check(observed.prefixes.contains(expected.bits.substr(0, count)),
                  "a long-profile accepted bit was hidden behind later message progress: expected="+
                  expected.bits.substr(0,count)+" prefixes="+prefixes);
    }
    if (!expected.ambiguous_tone)
        check(observed.prefixes.contains(expected.bits.substr(0, 4096)), "exact transport prefix was hidden until physical completion");
    check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete &&
          (expected.ambiguous_tone || observed.rows.begin()->second.text == expected.bits.substr(0, 4096)),
          "stronger admitted profile did not replace competing hypotheses in the pending row");
    advance(session, capture, origin + expected.samples.size(), observed, expected, origin, false);
    if (!observed.allow_revisions)
        check(observed.ids.size() == 1 && observed.complete == 1 &&
              observed.received == (observed.rows.begin()->second.binary ? 0 : 1),
              "one transmission must produce exactly one completed signal and one received item when decodable");
    observed.check_final(expected);
}

void run_case(const std::vector<double>& targets, double target, const std::string& text, bool binary,
              const Configuration& configuration = {}, bool allow_revisions = false) {
    const auto expected = waveform(target, text, binary, configuration);
    CaptureScript capture;
    capture.samples = expected.samples;
    capture.rate = expected.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings(targets, configuration));
    Observations observed;
    observed.allow_revisions = allow_revisions;
    receive_wave(session, capture, expected, 0, observed);
    session.stop();
}

void retired_public_streams_release_presentations(const std::vector<double>& targets) {
    constexpr unsigned repetitions=36;
    const auto expected=waveform(55,"the quick brown fox jumps over the lazy dog");
    auto c=modem_config(55,{});c.stream_epoch=epoch;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto stride=4*symbol+symbol/3;
    check(repetitions*stride<6*c.sample_rate,
          "retirement fixture permits physical absence between its starts");
    modem::PatternTransmitter transmitter(Bytes{1,0},c,epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(transmitter.total_samples()));
    transmitter.read_analytic(analytic);
    const auto padding=static_cast<std::size_t>(modem::pattern_pulse_padding_samples(c));
    check(analytic.size()==2*symbol+2*padding,"retirement fixture payload geometry changed");
    const auto last_end=(repetitions-1)*stride+2*symbol;
    const auto phrase_origin=last_end+8*c.sample_rate;
    CaptureScript capture;capture.rate=c.sample_rate;capture.samples.resize(phrase_origin);
    const auto rotation=std::polar(1.,.73);
    for(unsigned i=0;i<repetitions;++i)
        for(std::size_t sample=0;sample<2*symbol;++sample)
            capture.samples[i*stride+sample]=static_cast<float>((analytic[padding+sample]*rotation).real());
    capture.samples.insert(capture.samples.end(),expected.samples.begin(),expected.samples.end());
    capture_script=&capture;
    auto value=settings(targets);
    check(!value.transfer.key && value.receive_keys.empty(),"public lifetime fixture retained receive keys");
    live::Session session([]{return static_cast<double>(epoch);});session.start(value);
    std::set<std::uint64_t> pending;
    const auto advance_prefix=[&] {
        testing::CpuWorkBudget budget(30s,"retired public stream identities");
        std::optional<std::chrono::steady_clock::time_point> drained;
        while(budget.pending()) {
            const auto state=session.snapshot();
            check(state.error.empty(),"public retirement fixture failed: "+state.error);
            check(!state.receiver_health.failed(),"retired public streams exhausted receive coverage");
            check(state.dsp_buffered_bytes<=value.dsp_workspace_bytes,"retired public streams exceeded workspace");
            for(const auto& signal:state.signals) {
                if(!signal.complete) {
                    check(signal.binary && !signal.validated && signal.received_bits,
                          "retired stream acquired completed source semantics");
                    pending.insert(signal.id);
                } else check(state.samples_received>=last_end+6*c.sample_rate,
                             "stream retirement manufactured physical completion");
            }
            for(const auto& result:state.received)
                check(state.samples_received>=last_end+6*c.sample_rate && result.stream_complete,
                      "stream retirement interpreted source before physical absence");
            const auto delivered=capture.delivered.load();
            if(delivered<phrase_origin && delivered>=capture.released.load() &&
               !state.buffered_samples && !state.decoding_samples)
                capture.released=delivered+std::min<std::size_t>(capture.rate/50,phrase_origin-delivered);
            if(capture.delivered>=phrase_origin && !state.buffered_samples && !state.decoding_samples) {
                if(!drained)drained=std::chrono::steady_clock::now();
                if(std::chrono::steady_clock::now()-*drained>=250ms)return;
            } else drained.reset();
            std::this_thread::sleep_for(1ms);
        }
        throw Error("retired public stream fixture did not drain");
    };
    advance_prefix();
    check(pending.size()>=repetitions,"retirement fixture did not exceed the bounded candidate table");
    Observations observed;
    receive_wave(session,capture,expected,phrase_origin,observed);
    const auto final=session.snapshot();
    check(!final.receiver_health.failed() && final.error.empty(),"public interval after retirement lost receive coverage");
    session.stop();capture_script=nullptr;
    std::cout<<"retired public streams RX "<<targets[0]<<','<<targets[1]<<": "<<pending.size()
             <<" pending identities, exact interval and physical end passed\n";
}

void public_amplitude_rolling_arrivals() {
    struct Case {unsigned delay;bool timed=false,fallback=false;unsigned shift=0;bool clock_step=false;};
    for(const auto trial:{Case{12},Case{37},Case{29},Case{31},Case{95,false,false,1,true},
            Case{12,true,true},Case{12,true},Case{12,true,false,80}}) {
        const auto delay_seconds=trial.delay;
        auto value=settings({});auto& c=value.transfer.modem;
        c.sample_rate=512;c.bandwidth_hz=trial.timed?5:4;c.carrier_hz=128;c.spreading_factor=64;
        c.integration_seconds=0;c.pattern_symbols=true;c.constellation_bits=1;c.pulse_shaping=true;
        c.scramble=c.dsss=false;c.dsss_factor=1;c.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
        c.stream_epoch=epoch;value.transfer.timestamp=0;value.transfer.search_seconds=6;
        value.transfer.automatic_receive_profiles=false;value.transfer.clock_sync.reset();
        value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=(trial.timed&&!trial.fallback?256:128)*1024*1024;
        if(!trial.timed)check(modem::symbol_sample_count(c)==16384,"public amplitude arrival fixture changed32second symbols");
        if(trial.timed) {
            value.transfer.clock_sync=clock_sync::Policy{.001,.4,2.564};
            value.transfer.audio_timing_error_seconds=trial.fallback?.001:.2;
        }
        const auto delay=delay_seconds*c.sample_rate+37+trial.shift;
        auto tx_options=value.transfer;tx_options.timestamp=epoch;
        auto tx=transfer::binary_transmitter(Bytes{0,1},tx_options);
        Waveform expected;expected.text=expected.bits="01";expected.binary=true;expected.rate=c.sample_rate;
        expected.symbol_samples=modem::symbol_sample_count(c);
        expected.payload_end=delay+modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c)+2*expected.symbol_samples;
        expected.transmit_end=delay+tx->total_samples();
        expected.samples.resize(std::max(expected.transmit_end,expected.payload_end+2*expected.symbol_samples+2*c.sample_rate));
        for(std::size_t at=delay;at<expected.transmit_end;)
            at+=tx->read(std::span(expected.samples).subspan(at,expected.transmit_end-at));
        std::mt19937 random(7819+delay_seconds);std::normal_distribution<float> noise(0,.002F);
        for(auto& x:expected.samples)x+=noise(random);
        CaptureScript capture;capture.rate=c.sample_rate;capture.samples=expected.samples;
        capture.monitor_rate=c.sample_rate;capture.monitor_samples=expected.samples;capture_script=&capture;
        long double capture_epoch=epoch;
        if(trial.timed) {
            const auto prefix=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
            check(prefix%c.sample_rate!=0,"dated public amplitude fixture must cover fractional-second padding");
            capture_epoch=epoch+2.564L+value.transfer.audio_timing_error_seconds/2-
                static_cast<long double>(delay+prefix)/capture.rate;
            capture.timing=[&](std::size_t sample) {
                return audio::TimePrediction{capture_epoch+static_cast<long double>(sample)/capture.rate,
                    .02,1./capture.rate,audio::TimingQuality::estimated,0};
            };
            if(trial.fallback)capture.timing_status=audio::TimingStatus{false,.001,.02,
                "20 ms timestamp uncertainty exceeds Audio error 1 ms; ordinary capture/full search"};
        }
        std::atomic<double> now{static_cast<double>(capture_epoch)};
        capture.before_capture=[&](std::size_t end){now=static_cast<double>(capture_epoch)+double(end)/capture.rate+
            (trial.clock_step&&end>20*c.sample_rate&&end<70*c.sample_rate?500:0);};
        live::Session session([&]{return now.load();});session.start(value);Observations observed;
        observed.workspace_limit=value.dsp_workspace_bytes;
        const auto drain=[&](std::size_t target,bool before_absence) {
            testing::CpuWorkBudget budget(30s,"public amplitude rolling arrival");
            std::optional<std::chrono::steady_clock::time_point> drained;
            while(budget.pending()) {
                const auto state=session.snapshot();observed.inspect(state,expected,0,before_absence);
                check(!state.receiver_health.failed(),"rolling public amplitude lost receive coverage");
                const auto delivered=capture.delivered.load();
                if(delivered<target&&delivered==capture.released.load()&&!state.buffered_samples&&!state.decoding_samples) {
                    const auto end=delivered+std::min<std::size_t>(c.sample_rate/10,target-delivered);
                    capture.monitor_released=end;capture.released=end;
                }
                if(delivered>=target&&!state.buffered_samples&&!state.decoding_samples) {
                    if(!drained)drained=std::chrono::steady_clock::now();
                    if(std::chrono::steady_clock::now()-*drained>=250ms)return;
                } else drained.reset();
                std::this_thread::sleep_for(1ms);
            }
            throw Error("public amplitude arrival fixture didnotdrain");
        };
        drain(expected.payload_end-expected.symbol_samples+2*c.sample_rate,true);
        check(observed.rows.size()==1&&observed.prefixes.contains("0")&&!observed.rows.begin()->second.complete,
              "later public amplitude arrival hid its first pending bit");
        drain(expected.payload_end+6*c.sample_rate,true);
        check(observed.rows.size()==1&&observed.prefixes.contains("01")&&!observed.rows.begin()->second.complete,
              "rolling public amplitude lost exact pendingbits or used sixseconds as an early END");
        drain(expected.samples.size(),false);observed.check_final(expected);
        const auto state=session.snapshot();
        check(observed.ids.size()==1&&observed.complete==1&&state.receiver_admissions>2&&state.peak_receiver_instances>1,
              "public amplitude didnot retain one physical row across fresh arrival windows");
        check(state.clock_window_modeled==(trial.timed&&!trial.fallback),
              "public amplitude did not apply explicit timing or preserve Default/fallback");
        if(!trial.timed)check(state.receiver_admissions<12,
              "public amplitude still admitted a fresh overlapping bank every second");
        if(trial.fallback)check(state.clock_timing_fallback&&state.clock_timing_status.find("Audio error 1 ms")!=std::string::npos,
              "public amplitude timing fallback lost its visible provider reason");
        session.stop();capture_script=nullptr;
        std::cout<<"public amplitude delayed arrival "<<delay_seconds<<"s timed="<<trial.timed<<" fallback="<<trial.fallback<<" shift="<<trial.shift
                 <<": exact01, pendingidentity andphysicalabsence passed; admissions="<<state.receiver_admissions<<'\n';
    }
}

// Queue processing time must not select the private epoch for already captured
// PCM. Delay the logical callback after the raw monitor callback, avoiding any
// dependency on CPU speed or scheduler sleeps in this timestamp regression.
void receiver_backlog_before_overflow(bool clear=false) {
    CaptureScript capture;capture.rate=modem_config(55,{}).sample_rate;
    capture.monitor_rate=capture.rate;capture.monitor_samples.resize(64);capture.monitor_released=64;
    capture.samples.resize(3*capture.rate);capture.released=64;capture_script=&capture;
    const auto main_thread=std::this_thread::get_id();std::thread::id capture_thread;
    std::atomic<bool> armed{false},entered{false},released{false};
    capture.after_monitor=[&]{capture_thread=std::this_thread::get_id();armed=true;};
    live::Session session([]{return double(epoch);},[&]{
        if(armed && std::this_thread::get_id()!=main_thread && std::this_thread::get_id()!=capture_thread && !entered.exchange(true))
            while(!released.load())std::this_thread::sleep_for(1ms);
        return std::chrono::steady_clock::now();
    });
    // Declared after Session so exceptions always release its decoder first.
    struct Release {std::atomic<bool>& flag;~Release(){flag=true;}} release{released};
    session.start(settings({55}));
    const auto await=[&](auto predicate) {
        const auto deadline=std::chrono::steady_clock::now()+10s;
        do {auto snapshot=session.snapshot();if(predicate(snapshot))return snapshot;std::this_thread::sleep_for(1ms);}
        while(std::chrono::steady_clock::now()<deadline);
        throw Error("receiver backlog fixture did not reach checkpoint");
    };
    const auto active=await([&](const auto& x){return entered && x.decoding_samples==64 && !x.buffered_samples;});
    check(!active.receiver_health.failed(),"stalled decoder falsely reported dropped samples");
    check(active.processed_physical_samples==0 && active.decoding_physical_samples==64 &&
          active.queued_physical_samples==0,
          "popped work advanced the physical decoder frontier before scoring");
    const auto aged=await([](const auto& x){return x.receiver_behind;});
    check(aged.buffered_samples==0 && aged.decoding_samples==64 && aged.receiver_oldest_input_seconds>=1. &&
          aged.receiver_backlog_seconds<1. && !aged.receiver_health.failed(),
          "popped in-flight work hid backlog until queue overflow");
    capture.released=capture.samples.size();
    const auto queued=await([&](const auto& x){return x.buffered_samples==capture.samples.size()-64;});
    check(queued.receiver_backlog_seconds==3. && queued.receiver_behind && !queued.receiver_health.failed(),
          "queued hardware backlog changed sample accounting or claimed input loss");
    check(queued.processed_physical_samples==0 && queued.decoding_physical_samples==64 &&
          queued.queued_physical_samples==capture.samples.size()-64,
          "queued capture and committed physical decoder coordinates were conflated");
    if(clear) {
        session.clear_received();
        const auto cleared=session.snapshot();
        check(cleared.receiver_input_revision>queued.receiver_input_revision &&
              cleared.processed_physical_samples==0 && cleared.queued_physical_samples==0 &&
              cleared.decoding_physical_samples==0,
              "Clear received retained the old physical processing frontier");
        check(!cleared.receiver_behind && cleared.receiver_backlog_seconds==0 &&
              cleared.receiver_oldest_input_seconds==0 && !cleared.buffered_samples && !cleared.decoding_samples &&
              !cleared.receiver_health.failed() && !cleared.receiver_timing && cleared.signals.empty() && cleared.received.empty(),
              "Clear received retained a logical queue/backlog or manufactured completion");
    }
    released=true;
    const auto drained=await([](const auto& x){return !x.buffered_samples && !x.decoding_samples;});
    check(drained.processed_physical_samples==(clear?0:capture.samples.size()) &&
          drained.queued_physical_samples==0 && drained.decoding_physical_samples==0,
          "drain published an ingress coordinate instead of completed receiver work");
    check(!drained.receiver_behind && drained.receiver_backlog_seconds==0 && drained.receiver_oldest_input_seconds==0 &&
          !drained.receiver_health.failed() && drained.signals.empty() && drained.received.empty(),
          "drained backlog latched a failure or manufactured completion");
    await([&](const auto&){return session.try_suspend_capture();});
    session.resume_capture();
    const auto resumed=session.snapshot();
    check(resumed.receiver_input_revision>drained.receiver_input_revision &&
          resumed.processed_physical_samples==0 && resumed.queued_physical_samples==0 &&
          resumed.decoding_physical_samples==0,
          "capture resume relabeled a stale physical decoder frontier");
    session.stop();
}

void captured_epoch_survives_delay() {
    Configuration config;config.keyed=true;config.mode=tuning::PatternMode::auto_keystream;
    const auto wave=waveform(55,"e",false,config);
    for(const double delay:{0.,30.})for(const bool timestamped:{false,true}) {
        CaptureScript capture;capture.samples=wave.samples;capture.rate=wave.rate;
        capture.monitor_rate=wave.rate;capture.monitor_samples.resize(64);
        capture.monitor_released=64;
        std::atomic<double> now{epoch+64./wave.rate};
        capture.after_monitor=[&]{now=epoch+delay;};
        if(timestamped)capture.timing=[&](std::size_t sample) {
            return audio::TimePrediction{epoch+static_cast<long double>(sample)/wave.rate,
                .00001,1./wave.rate,audio::TimingQuality::bounded,1e-9};
        };
        capture_script=&capture;
        auto value=settings({55},config);value.transfer.timestamp=0;value.receive_keys.clear();
        if(timestamped)value.transfer.clock_sync=clock_sync::Policy{.001,.4,0};
        value.transfer.search_seconds=static_cast<unsigned>(std::ceil((wave.payload_end-3*wave.symbol_samples)/double(wave.rate)))+1;
        live::Session session([&]{return now.load();});session.start(value);
        Observations observed;
        std::cout<<"Capture delay "<<delay<<" s, prefix "<<(wave.payload_end-3*wave.symbol_samples)/double(wave.rate)<<" s\n"<<std::flush;
        receive_wave(session,capture,wave,0,observed);
        check(!session.snapshot().receiver_health.failed(),"delayed captured PCM lost receive coverage");
        if(timestamped)check(session.snapshot().clock_timing_quality==audio::TimingQuality::bounded &&
            session.snapshot().clock_backend_uncertainty_seconds==.00001,
            "queued capture discarded timestamp quality or provider uncertainty");
        session.stop();
    }
}
void modeled_arrival_window(bool modeled=true,bool fallback=false) {
    auto value=settings({});value.transfer.automatic_receive_profiles=false;
    value.transfer.key.emplace(Bytes(32,0x67)); // Synthetic key required by the encrypted profile.
    auto& config=value.transfer.modem;
    config.sample_rate=64;config.bandwidth_hz=2;config.carrier_hz=16;
    config.spreading_factor=64;config.integration_seconds=64;config.scramble=true;
    modem::OscillatorSearchConfig oscillator;oscillator.lf={.0001,0};oscillator.rf={.0001,0};
    config.oscillator_search=oscillator;
    value.transfer.key.emplace(Bytes(32,0x67));value.receive_keys.clear();
    value.transfer.clock_sync=clock_sync::Policy{.001,.4,2.564};
    // This synthetic provider explicitly reports 100 ms uncertainty. Its
    // accepted engineering allowance must cover that value.
    value.transfer.audio_timing_error_seconds=fallback?.001:.2;
    auto transmitter=transfer::binary_transmitter(Bytes{0,0,1},value.transfer);
    Waveform wave;wave.bits="001";wave.text="e";wave.rate=config.sample_rate;
    wave.symbol_samples=modem::symbol_sample_count(config);
    const auto prefix=modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config);
    wave.payload_end=prefix+3*wave.symbol_samples;wave.transmit_end=transmitter->total_samples();
    wave.samples.resize(wave.payload_end+2*wave.symbol_samples);
    for(std::size_t offset=0;offset<wave.transmit_end;)
        offset+=transmitter->read(std::span(wave.samples).subspan(offset,wave.transmit_end-offset));
    std::mt19937 random(8071);std::normal_distribution<float> noise(0,.1f);
    for(auto& sample:wave.samples)sample+=noise(random);
    CaptureScript capture;capture.samples=wave.samples;capture.rate=wave.rate;
    capture.monitor_rate=wave.rate;capture.monitor_samples.resize(64);capture.monitor_released=64;
    const auto start=static_cast<long double>(epoch)+2.564L-static_cast<long double>(prefix)/wave.rate;
    if(modeled)capture.timing=[&](std::size_t sample) {
        return audio::TimePrediction{start+static_cast<long double>(sample)/wave.rate,
            .1,1./wave.rate,audio::TimingQuality::estimated,2e-6};
    };
    if(fallback)capture.timing_status=audio::TimingStatus{false,.001,.1,
        "100 ms captured timestamp uncertainty exceeds Audio error 1 ms; ordinary capture/full search"};
    capture_script=&capture;
    live::Session session([]{return static_cast<double>(epoch)+30;});session.start(value);
    Observations observed;
    try {receive_wave(session,capture,wave,0,observed);}
    catch(...) {
        std::cerr<<"UTC arrival diagnostic: modeled="<<session.snapshot().clock_window_modeled
            <<" audio_error_seconds="<<value.transfer.audio_timing_error_seconds<<'\n';
        throw;
    }
    check(session.snapshot().clock_window_modeled==(modeled && !fallback) && !session.snapshot().receiver_health.failed(),
        "estimated arrival model failed to reach compact Live acquisition or lost coverage");
    const auto timing=session.snapshot().receiver_timing;
    check(bool(timing)==(modeled && !fallback),"Live lost qualified bank timing metadata or retained fallback metadata");
    if(timing)check(timing->uncertainty_seconds==.1 && timing->rate_uncertainty_fraction==2e-6 &&
        timing->seconds_per_frame==1./wave.rate,"Live changed actual timing bounds before planner publication");
    if(fallback)check(session.snapshot().clock_timing_fallback &&
        session.snapshot().clock_timing_status.find("Audio error 1 ms")!=std::string::npos,
        "ordinary capture fallback lost its visible timing reason");
    session.stop();
}

void default_hop_calendar(unsigned multiplier) {
    auto value=settings({});value.transfer.automatic_receive_profiles=false;
    value.transfer.key.emplace(Bytes(32,0x67));value.receive_keys.clear();
    value.transfer.clock_sync.reset();value.transfer.audio_timing_error_seconds=0;
    auto& c=value.transfer.modem;c.sample_rate=128;c.bandwidth_hz=16;c.carrier_hz=32;
    c.integration_seconds=2;c.spreading_factor=64;c.scramble=true;
    c.receiver_strategy=modem::ReceiverStrategy::existing_only;
    c.oscillator_search.emplace();c.oscillator_search->lf={0,0};c.oscillator_search->rf={0,0};
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=multiplier;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;
    plan->audio_error_seconds=plan->region_seconds=0;plan->ramp_seconds=.01;
    c.scheduled_hopping=plan;c.data_key=value.transfer.key;
    const modem::SchedHopSourceMap calendar(*plan,*value.transfer.key,c.sample_rate);
    auto selected=epoch;
    for(;selected<epoch+64;++selected)if(calendar.locate({selected*c.sample_rate,0}).active)break;
    check(selected<epoch+64,"default hop fixture has no active epoch");
    value.transfer.timestamp=selected;
    auto tx=transfer::binary_transmitter(Bytes{0,0,1},value.transfer);
    const auto output=tx->physical_output_origin();
    const auto anchor=calendar.locate({selected*c.sample_rate,0}).source;
    const auto symbol=modem::symbol_sample_count(c),failed=(6*c.sample_rate+symbol-1)/symbol;
    const auto end=calendar.source_to_physical(anchor.add_samples((3+failed)*symbol),true);
    const auto end_samples=static_cast<std::uint64_t>(std::ceil(end.distance({output,0})));
    CaptureScript capture;capture.rate=c.sample_rate;capture.monitor_rate=c.sample_rate;
    capture.samples.resize(std::max<std::uint64_t>(tx->total_samples(),end_samples)+12*c.sample_rate);
    for(std::size_t at=0;at<tx->total_samples();) {
        const auto n=tx->read(std::span(capture.samples).subspan(at,tx->total_samples()-at));
        check(n>0,"scheduled test transmitter stalled");at+=n;
    }
    std::mt19937 random(8071);std::normal_distribution<float> noise(0,.01f);
    for(auto& x:capture.samples)x+=noise(random);
    // Dates are physical events, independent of the delayed processing clock.
    // Positive provider uncertainty remains present even with Audio error0.
    capture.timing=[&](std::size_t sample) {
        return audio::TimePrediction{(static_cast<long double>(output)+sample)/c.sample_rate,
            .002,1./c.sample_rate,audio::TimingQuality::estimated,0};
    };
    capture_script=&capture;
    live::Session session([=]{return double(selected)+20;});session.start(value);
    testing::CpuWorkBudget budget(20s,"Default shared hop calendar");
    std::set<std::size_t> prefixes;bool complete=false,modeled=false,dated=false;
    while(budget.pending()) {
        const auto state=session.snapshot();
        check(state.error.empty()&&!state.receiver_health.failed(),"Default calendar Live failure: "+state.error);
        modeled|=state.clock_window_modeled;dated|=state.receiver_timing.has_value();
        for(const auto& signal:state.signals) {
            const auto bits=signal.raw_bits.empty()?signal.text:signal.raw_bits;
            check(signal.received_bits<=3 && bits==std::string("001").substr(0,signal.received_bits),
                "Default calendar admitted extra or wrong bits");
            if(signal.received_bits)prefixes.insert(signal.received_bits);
            if(signal.complete) {
                check(capture.delivered>=end_samples,"scheduled null/EOF manufactured physical completion");
                check(signal.received_bits==3,"Default calendar completed a truncated message");complete=true;
            }
        }
        const auto delivered=capture.delivered.load();
        if(delivered<capture.samples.size() && delivered>=capture.released && !state.buffered_samples)
            capture.released=delivered+std::min<std::size_t>(16,capture.samples.size()-delivered);
        if(complete && delivered==capture.samples.size() && !state.buffered_samples)break;
        std::this_thread::sleep_for(1ms);
    }
    if(!(complete&&modeled&&dated)) {
        const auto state=session.snapshot();
        std::cerr<<"calendar diagnostic multiplier="<<multiplier<<" delivered="<<capture.delivered
            <<" total="<<capture.samples.size()<<" sampled="<<state.samples_received
            <<" buffered="<<state.buffered_samples<<" modeled="<<modeled<<" dated="<<dated
            <<" prefixes="<<prefixes.size()<<" status="<<state.status<<'\n';
    }
    check(complete&&modeled&&dated,"Default calendar did not decode and physically complete with retained provider dates");
    check(prefixes==std::set<std::size_t>{1,2,3},"Default calendar delayed per-bit publication");
    session.stop();capture_script=nullptr;
}

void sequential_profiles(const std::vector<double>& targets, double first_target, double second_target) {
    const auto first = waveform(first_target, "e"), second = waveform(second_target, "hello");
    CaptureScript capture;
    capture.samples = first.samples;
    capture.samples.insert(capture.samples.end(), second.samples.begin(), second.samples.end());
    capture.rate = first.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings(targets));
    Observations first_observed, second_observed;
    receive_wave(session, capture, first, 0, first_observed);
    receive_wave(session, capture, second, first.samples.size(), second_observed);
    check(first_observed.ids != second_observed.ids, "profile arbitration merged separate physical transmissions");
    session.stop();
}

void separated_long_and_short() {
    Configuration configuration;
    configuration.bandwidth = 16;
    configuration.carrier = 16;
    const auto first = waveform(5, "0", true, configuration);
    const auto second = waveform(55, "001", true, configuration);
    // The first receiver must finish an entire 32-second absent symbol. A
    // different sender may already start after seven seconds of actual silence;
    // its independent clock must not be absorbed by that pending absence wait.
    const auto second_origin = first.transmit_end + 7 * first.rate;
    CaptureScript capture;
    capture.samples = first.samples;
    capture.samples.resize(std::max(capture.samples.size(), second_origin + second.samples.size()));
    std::copy(second.samples.begin(), second.samples.end(), capture.samples.begin() + second_origin);
    capture.rate = first.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(settings({55, 5}, configuration));
    Observations observed;
    observed.allow_revisions = true;
    advance(session, capture, second_origin + second.payload_end + 5 * second.rate,
            observed, first, 0, false);
    check(observed.content.empty() &&
          std::any_of(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
              return !row.second.complete && row.second.text == "001";
          }), "independent short transmission completed before physical absence or lost its exact pending prefix");
    advance(session, capture, first.payload_end + first.symbol_samples - 1,
            observed, first, 0, false);
    const auto long_row = std::find_if(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
        return !row.second.complete && row.second.binary && row.second.text == "0" && row.second.received_bits == 1;
    });
    const auto short_row = std::find_if(observed.rows.begin(), observed.rows.end(), [](const auto& row) {
        return row.second.complete && !row.second.binary && row.second.text == "e" && row.second.raw_bits == "001";
    });
    check(observed.rows.size() == 2 && long_row != observed.rows.end() && short_row != observed.rows.end(),
          "long pending absence merged an independent short transmission or completed before a full absent symbol");
    const auto long_id = long_row->first, short_id = short_row->first;
    advance(session, capture, capture.samples.size(), observed, first, 0, false);
    check(observed.rows.size() == 2 && observed.rows.contains(long_id) && observed.rows.contains(short_id) &&
          observed.rows.at(long_id).complete && observed.rows.at(long_id).binary &&
          observed.rows.at(long_id).text == "0" && observed.rows.at(long_id).received_bits == 1 &&
          observed.rows.at(short_id).complete && observed.rows.at(short_id).raw_bits == "001" &&
          observed.content.size() == 1,
          "separate long and short physical transmissions lost their distinct completed identities");
    Observations::check_received(observed.content.begin()->second, second);
    session.stop();
}

void continuous_long_fft_single_bit() {
    auto value = settings({});
    auto& config = value.transfer.modem;
    config.sample_rate = 64;
    config.bandwidth_hz = 8;
    config.carrier_hz = 16;
    config.spreading_factor = 64;
    config.integration_seconds = 40;
    value.transfer.automatic_receive_profiles = false;
    // A single public profile with ample workspace selects the full FFT
    // receiver. The multi-profile fixtures above prefer streamed templates
    // and therefore cannot catch delays caused by FFT input batching.
    modem::PatternSearch search;
    search.expand_clock_search = true;
    search.allow_local_clock_fallback = true;
    search.start_offset_seconds = static_cast<double>(modem::pattern_pulse_padding_samples(config)) /
                                  config.sample_rate;
    search.start_uncertainty_seconds = 1;
    modem::PatternReceiver path(config, value.dsp_workspace_bytes / 2, search);
    check(!path.clock_windowed(), "long single-bit fixture did not select the full FFT receiver");

    Waveform expected;
    expected.text = expected.bits = "0";
    expected.binary = true;
    expected.rate = config.sample_rate;
    expected.symbol_samples = modem::symbol_sample_count(config);
    expected.payload_end = modem::training_sample_count(config) +
        modem::pattern_pulse_padding_samples(config) + expected.symbol_samples;
    auto transmitter = transfer::binary_transmitter(Bytes{0}, value.transfer);
    expected.transmit_end = transmitter->total_samples();
    expected.samples.resize(expected.payload_end + 3 * expected.symbol_samples);
    for (std::size_t offset = 0; offset < expected.transmit_end;)
        offset += transmitter->read(std::span(expected.samples).subspan(offset, expected.transmit_end - offset));

    CaptureScript capture;
    capture.samples = expected.samples;
    capture.rate = expected.rate;
    capture_script = &capture;
    live::Session session([] { return static_cast<double>(epoch); });
    session.start(value);
    Observations observed;
    // Keep the microphone stream open. A complete, high-SNR payload symbol
    // must become pending without waiting for the rest of an arbitrary FFT
    // block, for a full absent symbol, or for an offline finish() flush.
    advance(session, capture, expected.payload_end + expected.symbol_samples / 2,
            observed, expected, 0, true);
    check(observed.rows.size() == 1 && observed.prefixes.contains("0") &&
          !observed.rows.begin()->second.complete && observed.complete == 0 && observed.received == 0,
          "continuous full FFT capture hid the one-bit pending prefix until a later input block");
    advance(session, capture, expected.payload_end + expected.symbol_samples - 1,
            observed, expected, 0, true);
    check(observed.rows.size() == 1 && !observed.rows.begin()->second.complete,
          "continuous full FFT capture completed before observing the whole absent symbol");
    advance(session, capture, expected.samples.size(), observed, expected, 0, false);
    check(observed.ids.size() == 1 && observed.complete == 1 && observed.received == 0,
          "continuous full FFT capture changed identity or duplicated one-bit completion");
    observed.check_final(expected);
    session.stop();
}

void simulated_long_fft_single_bit(double duration = 40, double snr = 30, std::uint64_t seed = 1) {
    auto value = settings({});
    auto& config = value.transfer.modem;
    config.sample_rate = 64;
    config.bandwidth_hz = 8;
    config.carrier_hz = 16;
    config.spreading_factor = 64;
    config.integration_seconds = duration;
    value.transfer.automatic_receive_profiles = false;
    value.simulation = true;
    value.simulation_snr_db = snr;
    value.simulation_seed = seed;
    const bool require_reception = snr == 30;
    if (duration >= 400)
        check(modem::default_pattern_frequency_offsets(config).size() > 5,
              "seed fixture lost its expanded carrier and coupled-clock bank");
    // Retain the free-running sound-card model: 100 ppm clock mismatch and
    // 0.5 degrees per square-root second of independent phase diffusion.
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([] { return static_cast<double>(epoch); }, [&] {
        return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(replay_milliseconds.load());
    });
    session.start(value);
    check(session.snapshot().simulation_compute_seconds == 0,
          "idle simulation retained a previous computation's elapsed time");
    session.transmit_bits(Bytes{0});
    testing::CpuWorkBudget budget(30s,"long sampled FFT simulation");
    bool computed = false;
    double elapsed = 0, fraction = 0;
    std::uint64_t samples = 0;
    while (budget.pending()) {
        const auto snapshot = session.snapshot();
        check(snapshot.error.empty(), "long sampled simulation failed: " + snapshot.error);
        check(snapshot.received.empty(), "single raw zero unexpectedly became source text");
        check(snapshot.simulation_compute_seconds >= elapsed,
              "simulation elapsed time moved backward while its presentation clock was frozen");
        if (snapshot.transmitting && elapsed > 0 && snapshot.samples_received == samples &&
            snapshot.transmission_fraction == fraction)
            check(snapshot.simulation_compute_seconds > elapsed,
                  "simulation elapsed time stopped advancing while audio/DSP progress was unchanged");
        elapsed = snapshot.simulation_compute_seconds;
        fraction = snapshot.transmission_fraction;
        samples = snapshot.samples_received;
        if (snapshot.simulation_receiving_tail)
            check(snapshot.transmitting && !snapshot.transmission_finished && fraction == 1,
                  "receiver-tail stage claimed unfinished TX audio or completed simulation work");
        if (snapshot.transmission_finished && snapshot.simulation_replay) {
            check(!snapshot.simulation_receiving_tail && elapsed > 0,
                  "completed computation kept the tail stage or lost its wall-clock elapsed time");
            computed = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    check(computed, "long sampled simulation did not finish: audio fraction " + std::to_string(fraction) +
          ", samples " + std::to_string(samples) + ", elapsed " + std::to_string(elapsed));
    std::optional<std::uint64_t> pending_id;
    std::size_t complete = 0;
    for (std::int64_t milliseconds = 0; milliseconds <= 3000; milliseconds += 10) {
        replay_milliseconds = milliseconds;
        const auto snapshot = session.snapshot();
        check(snapshot.error.empty(), "long sampled replay failed: " + snapshot.error);
        check(snapshot.simulation_compute_seconds == elapsed && !snapshot.simulation_receiving_tail,
              "presentation replay changed the completed computation's elapsed time or stage");
        check(snapshot.received.empty(), "single raw zero unexpectedly became source text during replay");
        check(snapshot.dsp_buffered_bytes <= value.dsp_workspace_bytes,
              "long sampled simulation exceeded the configured DSP workspace");
        for (const auto& signal : snapshot.signals) {
            if (!require_reception) continue;
            check(signal.binary && !signal.validated && signal.text == "0" &&
                  signal.received_bits == 1 && signal.missing_symbols == 0,
                  "long sampled simulation changed its exact one-bit reception");
            if (!signal.complete) {
                if (!pending_id) pending_id = signal.id;
                check(signal.id == *pending_id, "long sampled simulation changed its pending reception identity");
            } else {
                check(pending_id && signal.id == *pending_id,
                      "long sampled simulation withheld its pending bit or changed identity at completion");
                ++complete;
            }
        }
    }
    check(!require_reception || (pending_id && complete == 1),
          "long sampled simulation did not replay a pending bit and exactly one completed reception");
    const auto finished = session.snapshot();
    check(!finished.transmitting && finished.transmission_finished && !finished.simulation_replay,
          "sampled simulation did not leave replay after its presentation deadline");
    session.configure(value);
    check(session.snapshot().simulation_compute_seconds == 0 && !session.snapshot().simulation_receiving_tail,
          "reconfiguration retained the completed simulation's elapsed time or stage");
    session.stop();
}

// A failed, popped block must invalidate its physical input revision. It
// cannot remain phantom queued work or be advertised as successfully scored.
void receiver_exception_frontier() {
    for(const bool queue_more:{false,true}) {
        CaptureScript capture;capture.rate=modem_config(55,{}).sample_rate;
        capture.monitor_rate=capture.rate;capture.monitor_samples.resize(64);capture.monitor_released=64;
        capture.samples.resize(5*capture.rate);capture.released=0;capture_script=&capture;
        const auto main_thread=std::this_thread::get_id();std::thread::id capture_thread;
        std::atomic<bool> armed{false},entered{false},released{false};
        capture.after_monitor=[&]{capture_thread=std::this_thread::get_id();armed=true;};
        live::Session session([]{return double(epoch);},[&]{
            if(armed && std::this_thread::get_id()!=main_thread && std::this_thread::get_id()!=capture_thread && !entered.exchange(true)) {
                while(!released.load())std::this_thread::sleep_for(1ms);
                // Bypass feed_bank's ordinary per-receiver Error handling to
                // exercise the outer decoder exception boundary specifically.
                throw std::runtime_error("injected receiver frontier failure");
            }
            return std::chrono::steady_clock::now();
        });
        struct Release {std::atomic<bool>& flag;~Release(){flag=true;}} release{released};
        const auto await=[&](auto condition) {
            const auto deadline=std::chrono::steady_clock::now()+10s;
            do {auto snapshot=session.snapshot();if(condition(snapshot))return snapshot;std::this_thread::sleep_for(1ms);}
            while(std::chrono::steady_clock::now()<deadline);
            throw Error("receiver exception frontier fixture did not reach checkpoint");
        };
        session.start(settings({55}));const auto initial_revision=session.snapshot().receiver_input_revision;
        capture.released=64;
        await([&](const auto& x){return entered && x.decoding_samples==64 && !x.buffered_samples;});
        const auto discarded=queue_more?3*capture.rate:64U;
        capture.released=discarded;
        await([&](const auto& x){return capture.delivered==discarded && x.buffered_samples==discarded-64;});
        released=true;
        const auto failed=await([&](const auto& x){return x.receiver_health.receiver_reset && !x.decoding_samples;});
        check(failed.receiver_input_revision>initial_revision && failed.processed_physical_samples==0 &&
              failed.queued_physical_samples==0 && failed.decoding_physical_samples==0 && !failed.buffered_samples,
              "failed removed PCM remained phantom queued work or advanced the scored frontier");
        check(failed.error=="injected receiver frontier failure" && failed.receiver_health.dropped_samples==discarded,
              "receiver exception lost its error or abandoned-PCM accounting");
        check(failed.signals.empty() && failed.received.empty(),"receiver failure manufactured completion");
        capture.released=capture.samples.size();
        const auto drained=await([&](const auto& x){return capture.delivered==capture.samples.size() &&
            !x.buffered_samples && !x.decoding_samples;});
        check(drained.processed_physical_samples==capture.samples.size()-discarded &&
              drained.queued_physical_samples==0 && drained.decoding_physical_samples==0 &&
              drained.receiver_health.receiver_reset && drained.receiver_health.dropped_samples==discarded,
              "fresh receiver revision conflated lost PCM with newly completed work");
        check(drained.signals.empty() && drained.received.empty(),"post-failure quiet input manufactured completion");
        session.stop();capture_script=nullptr;
    }
}

void receiver_loss_health() {
    const Configuration geometry{16.,8.};
    auto value=settings({55},geometry);value.transfer.automatic_receive_profiles=false;
    const auto wave=waveform(55,"001",true,geometry);
    const auto prefix=wave.payload_end+value.transfer.modem.sample_rate;
    const auto oversized=value.dsp_workspace_bytes/8/sizeof(float)+1;
    for(const bool device_gap:{false,true}) {
        CaptureScript script;script.rate=wave.rate;script.monitor_rate=48000;
        script.samples.assign(wave.samples.begin(),wave.samples.begin()+prefix);
        script.samples.resize(prefix+(device_gap?0:oversized)+10*wave.rate);
        capture_script=&script;live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        std::uint64_t pending=0;bool complete=false;
        const auto await=[&](auto condition) {
            const auto deadline=std::chrono::steady_clock::now()+5s;
            while(std::chrono::steady_clock::now()<deadline) {
                const auto snapshot=session.snapshot();
                for(const auto& signal:snapshot.signals) {
                    complete|=signal.complete;
                    if(!signal.complete && signal.received_bits) {
                        check(signal.binary && signal.text==wave.bits.substr(0,signal.received_bits),
                              "health fixture lost exact pending prefix");
                        if(pending)check(pending==signal.id,"health fixture changed pending identity");
                        pending=signal.id;
                    }
                }
                check(snapshot.received.empty() && !complete,"lost input manufactured physical completion");
                check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"loss reporting exceeded workspace");
                if(condition(snapshot))return snapshot;
                std::this_thread::sleep_for(2ms);
            }
            throw Error("receiver health fixture did not reach checkpoint");
        };
        script.released=prefix;
        const auto before=await([&](const auto& x){return pending && x.buffered_samples==0 && x.decoding_samples==0;});
        check(!before.receiver_health.failed(),"healthy pending receiver reported loss");
        if(device_gap)script.interruptions=1;else script.released=prefix+oversized;
        const auto lost=await([&](const auto& x){return device_gap?x.receiver_health.input_interrupted:x.receiver_health.input_overruns!=0;});
        check(lost.receiver_health.input_overruns==(device_gap?0:1) &&
              lost.receiver_health.dropped_samples==(device_gap?0:oversized),"receiver loss counters misreported discarded PCM");
        // Give the fresh acquisition complete observed absence. It must not
        // complete the prefix retained before the missing segment.
        script.released=script.samples.size();
        const auto after=await([&](const auto& x){return script.delivered==script.samples.size() && x.buffered_samples==0 && x.decoding_samples==0;});
        check(after.receiver_health.failed(),"healthy PCM cleared latched loss");
        session.clear_received();
        const auto cleared=session.snapshot();
        check(!cleared.receiver_health.failed() && cleared.signals.empty() && cleared.received.empty() &&
              !cleared.receiver_behind,"Clear received retained latched loss or manufactured completion");
        session.configure(value);
        check(!session.snapshot().receiver_health.failed(),"reconfiguration retained obsolete receiver failure");
        session.stop();capture_script=nullptr;
    }
}
void independent_environment_monitor() {
    auto value=settings({55},Configuration{16.,8.});
    value.transfer.automatic_receive_profiles=false;
    const auto config=value.transfer.modem;
    check(config.sample_rate==64,"monitor fixture did not select narrowband input clock");
    for(const unsigned budget_case:{0U,1U,2U}) {
        const bool fallback=budget_case==2;
        CaptureScript script;script.rate=config.sample_rate;script.monitor_rate=48000;
        script.monitor_format_bytes=budget_case?value.dsp_workspace_bytes/8-96*1024+(fallback?1:0):0;
        script.monitor_samples.resize(4096);script.samples.resize(128);
        for(std::size_t i=0;i<script.monitor_samples.size();++i)
            script.monitor_samples[i]=static_cast<float>((i<2048?.5:.25)*std::cos(2*std::numbers::pi*3000*i/48000));
        for(std::size_t i=0;i<script.samples.size();++i)
            script.samples[i]=static_cast<float>(.2*std::cos(2*std::numbers::pi*config.carrier_hz*i/config.sample_rate));
        capture_script=&script;live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        const auto await=[&](auto predicate) {
            const auto deadline=std::chrono::steady_clock::now()+3s;
            while(std::chrono::steady_clock::now()<deadline) {
                const auto snapshot=session.snapshot();check(snapshot.error.empty(),"monitor fixture: "+snapshot.error);
                if(predicate(snapshot))return snapshot;std::this_thread::sleep_for(2ms);
            }
            throw Error("monitor fixture deadline");
        };
        script.monitor_released=2048;
        await([&](const auto&){return script.monitor_delivered==2048;});
        if(!fallback) {
            const auto before=await([](const auto& x){return x.environment_monitor;});
            check(before.waveform_sample_rate==48000 && before.spectrum_bin_hz==48000./2048 &&
                  before.samples_received==0 && before.virtual_seconds==0 && before.signals.empty() && before.received.empty() &&
                  !before.simulation_spectrum_gain_db && before.waveform==std::vector<float>(script.monitor_samples.begin(),script.monitor_samples.begin()+2048),
                  "hardware-only monitoring changed receiver time/progress or lost raw preview geometry");
            check(before.dsp_buffered_bytes<=value.dsp_workspace_bytes,"monitor exceeded configured workspace");
            std::this_thread::sleep_for(60ms);script.monitor_released=4096;
            const auto next=await([&](const auto& x){return x.spectrum_revision>before.spectrum_revision;});
            check(next.samples_received==0 && next.signals.empty(),"another environment frame manufactured reception progress");
            script.released=script.samples.size();
            const auto logical=await([&](const auto& x){return x.samples_received==script.samples.size() && !x.constellation.empty();});
            const auto expected=live::detail::signal_plots(script.samples,config);
            check(logical.constellation==expected.constellation && logical.environment_monitor &&
                  logical.waveform_sample_rate==48000 && logical.spectrum_revision==next.spectrum_revision &&
                  logical.waveform==next.waveform && logical.spectrum_db==next.spectrum_db,
                  "logical IQ overwrote environment samples or duplicated a waterfall observation");
        } else {
            script.released=script.samples.size();
            const auto logical=await([&](const auto& x){return x.samples_received==script.samples.size() && !x.waveform.empty();});
            check(!logical.environment_monitor && logical.waveform_sample_rate==64 && logical.spectrum_bin_hz==64./2048,
                  "insufficient monitor headroom lost logical-rate fallback");
        }
        session.stop();capture_script=nullptr;
    }
}
void sampled_long_fft_seeds() {
    for (const auto seed : {1ULL, 7ULL, 19ULL, 73ULL}) for (const auto snr : {30., -25.}) {
        try { simulated_long_fft_single_bit(400, snr, seed); }
        catch (const Error& error) {
            throw Error("seed " + std::to_string(seed) + " / SNR " + std::to_string(snr) + ": " + error.what());
        }
    }
}
void duplex_loopback(bool keyed=false) {
    Configuration geometry{16.,8.};
    geometry.keyed=keyed;
    auto value=settings({55},geometry);value.transfer.automatic_receive_profiles=false;value.full_duplex=true;
    if(keyed)value.receive_keys.clear(); // selected TX key must also receive its loopback
    const auto expected=waveform(55,"001",true,geometry);
    CaptureScript capture;capture.rate=expected.rate;capture.samples.resize(expected.samples.size());
    capture.loopback=true;capture.format_bytes=16384;capture_script=&capture;
    live::Session session([]{return static_cast<double>(epoch);});session.start(value);
    const auto await=[&](auto predicate) {
        testing::CpuWorkBudget budget(30s,"single-session hardware duplex loopback");
        while(budget.pending()) {auto snapshot=session.snapshot();
            check(snapshot.error.empty(),"duplex loopback failed: "+snapshot.error);
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"duplex audio exceeded workspace");
            if(predicate(snapshot))return snapshot;std::this_thread::sleep_for(1ms);
        }
        throw Error("duplex loopback did not reach checkpoint");
    };
    await([&](const auto&){return capture.capture_opened.load()==1;});
    session.transmit_bits(Bytes{0,0,1});
    Observations observed;
    const auto running=await([&](const auto& snapshot) {
        observed.inspect(snapshot,expected,0,true);return capture.playback_source_finished.load();
    });
    check(running.transmitting && capture.capture_opened==1 && !capture.capture_closed &&
          capture.played==expected.transmit_end && running.samples_received==capture.delivered,
          "duplex paused/reopened input or double-counted local TX samples");
    check(std::equal(capture.samples.begin(),capture.samples.begin()+expected.transmit_end,expected.samples.begin()),
          "duplex changed the transmitted PCM");
    // Playback has reached EOF but remains open; only actually captured silence
    // can close reception. Publish each observed prefix while TX is active.
    advance(session,capture,expected.payload_end+5*expected.rate,observed,expected,0,true);
    check(observed.complete==0 && observed.received==0 && observed.prefixes.contains(expected.bits),
          "duplex EOF completed a reception or suppressed pending progress");
    advance(session,capture,capture.samples.size(),observed,expected,0,false);
    observed.check_final(expected);
    check(session.snapshot().transmitting && capture.capture_closed==0,
          "duplex receiver did not complete independently while playback stayed open");
    // A clear during the still-open transmission preserves it and removes RX.
    const auto transmission=session.snapshot().transmission_id;
    const auto key_lock=session.snapshot().transmit_key_lock_seconds;
    session.clear_received();
    const auto cleared=session.snapshot();
    check(cleared.transmitting && cleared.transmission_id==transmission && !cleared.transmission_cancelled &&
          cleared.signals.empty() && cleared.received.empty() && !cleared.receiver_health.failed() &&
          !cleared.receiver_behind && cleared.transmit_key_lock_seconds==key_lock,
          "Clear received cancelled TX, changed key-use lock or retained receive state");
    session.cancel_transmit();
    await([&](const auto& snapshot){return !snapshot.transmitting && snapshot.transmission_cancelled;});
    check(!capture.capture_closed && capture.capture_opened==1,"cancelling duplex TX closed input");
    session.stop();
}

void duplex_spread_loopback() {
    struct Result {std::vector<float> sent,received;bool decoded=false;};
    const auto run_case=[](double delay,double doppler,double depth) {
        Configuration geometry{1200.,1500.};
        auto value=settings({55},geometry);value.transfer.automatic_receive_profiles=false;value.full_duplex=true;
        value.transfer.search_seconds=1;
        value.simulation_snr_db=90;value.simulation_clock_error_ppm=0;
        value.simulation_phase_noise_degrees_per_sqrt_second=0;value.simulation_seed=731;
        value.simulation_spread_delay_ms=delay;value.simulation_spread_doppler_hz=doppler;
        value.simulation_perturbation_slow_depth=depth;value.simulation_perturbation_fast_depth=depth;
        CaptureScript capture;capture.rate=value.transfer.modem.sample_rate;capture.loopback=true;
        capture.loopback_channel=live::detail::channel_config(value);capture.loopback_modem=value.transfer.modem;
        capture.format_bytes=16384;capture_script=&capture;
        live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        const auto await=[&](auto predicate,const char* stage) {
            testing::CpuWorkBudget budget(40s,stage);
            while(budget.pending()) {const auto snapshot=session.snapshot();
                check(snapshot.error.empty(),std::string(stage)+": "+snapshot.error);
                if(predicate(snapshot))return snapshot;std::this_thread::sleep_for(1ms);
            }
            throw Error(std::string(stage)+" timed out");
        };
        await([&](const auto&){return capture.capture_opened.load()==1;},"virtual duplex capture");
        session.transmit_bits(Bytes{0,0,1});
        await([&](const auto&){return capture.playback_source_finished.load();},"virtual duplex playback");
        const auto transmitted=capture.playback_input;
        const auto impaired=capture.samples;
        // Feed the modeled output and fully observed silence through the
        // ordinary hardware capture/receiver path while playback stays open.
        capture.samples.resize(capture.samples.size()+12*capture.rate);
        const auto target=capture.samples.size();
        bool decoded=false;testing::CpuWorkBudget budget(40s,"virtual duplex modeled reception");
        while(budget.pending()) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty(),"virtual duplex reception failed: "+snapshot.error);
            for(const auto& signal:snapshot.signals)
                if(signal.complete && (signal.binary?signal.text:signal.raw_bits)=="001")decoded=true;
            const auto delivered=capture.delivered.load();
            if(delivered<target && delivered>=capture.released.load() && snapshot.buffered_samples==0)
                capture.released=delivered+std::min<std::size_t>(std::max<std::size_t>(8,capture.rate/10),target-delivered);
            if(decoded && delivered>=target && snapshot.buffered_samples==0)break;
            std::this_thread::sleep_for(1ms);
        }
        session.cancel_transmit();session.stop();capture_script=nullptr;
        return Result{transmitted,impaired,decoded};
    };
    const auto off=run_case(0,0,0),spread=run_case(.05,.01,0),each=run_case(.05,.01,.01);
    check(off.sent==spread.sent && off.sent==each.sent,"duplex test sources changed between channel cases");
    const auto differs=[](const auto& a,const auto& b) {
        const auto n=std::min(a.size(),b.size());double difference=0,energy=0;
        for(std::size_t i=0;i<n;++i){const auto d=double(a[i])-b[i];difference+=d*d;energy+=double(a[i])*a[i];}
        return energy>1e-5 && difference>energy*1e-7;
    };
    check(differs(off.received,spread.received),"virtual duplex spread did not change captured PCM");
    check(differs(spread.received,each.received),"virtual duplex modulation did not change captured PCM");
    check(off.decoded && spread.decoded && each.decoded,"virtual duplex did not receive all mild channel cases");
}

void hardware_channel_loopback() {
    struct Result {std::vector<float> pcm;bool decoded=false;std::uint32_t rate=0;};
    const auto run_case=[](bool enabled,double delay,double doppler,double depth,std::size_t chunk) {
        Configuration geometry{1200.,1500.};
        auto value=settings({55},geometry);value.transfer.automatic_receive_profiles=false;value.full_duplex=true;
        value.transfer.search_seconds=1;value.hardware_channel_test=enabled;
        value.simulation_seed=731;
        value.simulation_spread_delay_ms=delay;value.simulation_spread_doppler_hz=doppler;
        value.simulation_perturbation_slow_depth=depth;value.simulation_perturbation_fast_depth=depth;
        // Simulation-only noise and oscillator settings must not affect
        // physical output, including its explicit channel test.
        value.simulation_snr_db=-80;value.simulation_clock_error_ppm=100;
        value.simulation_frequency_offset_hz=300;value.simulation_phase_noise_degrees_per_sqrt_second=10;
        CaptureScript capture;capture.rate=value.transfer.modem.sample_rate;capture.loopback=true;
        capture.buffer_playback=true;capture.playback_chunk=chunk;capture.format_bytes=16384;capture_script=&capture;
        live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        const auto await=[&](auto predicate,const char* stage) {
            testing::CpuWorkBudget budget(40s,stage);
            while(budget.pending()) {const auto snapshot=session.snapshot();
                check(snapshot.error.empty(),std::string(stage)+": "+snapshot.error);
                check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"hardware channel exceeded workspace");
                if(predicate(snapshot))return snapshot;std::this_thread::sleep_for(1ms);
            }
            throw Error(std::string(stage)+" timed out");
        };
        await([&](const auto&){return capture.capture_opened.load()==1;},"hardware channel capture");
        session.transmit_bits(Bytes{0,0,1});
        await([&](const auto&){return capture.playback_source_finished.load();},"hardware channel playback");
        const auto emitted=capture.playback_input;
        check(capture.samples==emitted,"hardware loopback test modified playback PCM before capture");
        capture.samples.resize(capture.samples.size()+12*capture.rate);
        const auto target=capture.samples.size();bool decoded=false;
        testing::CpuWorkBudget budget(40s,"hardware channel reception");
        while(budget.pending()) {
            const auto snapshot=session.snapshot();check(snapshot.error.empty(),"hardware channel reception: "+snapshot.error);
            for(const auto& signal:snapshot.signals)
                if(signal.complete && (signal.binary?signal.text:signal.raw_bits)=="001")decoded=true;
            const auto delivered=capture.delivered.load();
            if(delivered<target && delivered>=capture.released.load() && snapshot.buffered_samples==0)
                capture.released=delivered+std::min<std::size_t>(std::max<std::size_t>(8,capture.rate/10),target-delivered);
            if(decoded && delivered>=target && snapshot.buffered_samples==0)break;
            std::this_thread::sleep_for(1ms);
        }
        session.cancel_transmit();session.stop();capture_script=nullptr;
        return Result{emitted,decoded,capture.rate};
    };
    const auto plain=run_case(false,0,0,0,113);
    const auto disabled=run_case(false,6,10,.65,113);
    const auto zero=run_case(true,0,0,.65,17);
    check(plain.pcm==disabled.pcm,"disabled hardware test applied selected channel effects");
    check(plain.pcm==zero.pcm,"zero-reference hardware test changed original playback PCM");
    const auto spread=run_case(true,.05,.01,0,113);
    const auto each=run_case(true,.05,.01,.01,113);
    const auto partitioned=run_case(true,.05,.01,.01,17);
    const auto differs=[](const auto& a,const auto& b) {
        const auto n=std::min(a.size(),b.size());double difference=0,energy=0;
        for(std::size_t i=0;i<n;++i){const auto d=double(a[i])-b[i];difference+=d*d;energy+=double(a[i])*a[i];}
        return energy>1e-5 && difference>energy*1e-7;
    };
    check(differs(plain.pcm,spread.pcm),"production hardware spread did not change emitted PCM");
    check(differs(spread.pcm,each.pcm),"production hardware modulation did not change emitted PCM");
    // A depth-dependent fixed headroom gain alone is not evidence that
    // frequency-selective modulation reached the physical playback stream.
    double reference_energy=0,cross=0;
    const auto common=std::min(spread.pcm.size(),each.pcm.size());
    for(std::size_t i=0;i<common;++i) {
        reference_energy+=double(spread.pcm[i])*spread.pcm[i];
        cross+=double(spread.pcm[i])*each.pcm[i];
    }
    check(reference_energy>0,"hardware channel reference has no signal energy");
    const auto fitted_gain=cross/reference_energy;double residual=0;
    for(std::size_t i=0;i<common;++i) {
        const auto difference=double(each.pcm[i])-fitted_gain*spread.pcm[i];residual+=difference*difference;
    }
    check(residual>reference_energy*1e-8,"hardware modulation only changed a constant output gain");
    check(each.pcm==partitioned.pcm,"hardware channel changed with playback block partition");
    check(spread.pcm.size()>=plain.pcm.size()+static_cast<std::size_t>(std::ceil(.00005*plain.rate)),
          "production playback omitted the delayed channel endpoint");
    check(plain.decoded && disabled.decoded && zero.decoded && spread.decoded && each.decoded && partitioned.decoded,
          "production hardware loopback did not decode every mild channel case");
}

void duplex_transmit_failure(bool after_generation) {
    Configuration geometry{16.,8.};geometry.keyed=true;
    auto value=settings({55},geometry);value.transfer.automatic_receive_profiles=false;
    value.full_duplex=true;value.receive_keys.clear();
    CaptureScript capture;capture.rate=value.transfer.modem.sample_rate;capture.loopback=true;
    capture.fail_playback_before_source=!after_generation;
    capture.fail_playback_after_source=after_generation;capture_script=&capture;
    live::Session session([]{return static_cast<double>(epoch);});session.start(value);
    const auto await=[&](auto predicate) {
        testing::CpuWorkBudget budget(10s,"duplex failed transmission visibility",testing::instrumented_test,
            testing::CpuWorkBudget::Clock::now(),std::cerr,false);
        while(budget.pending()) {auto state=session.snapshot();
            if(predicate(state))return state;std::this_thread::sleep_for(1ms);
        }
        throw Error("duplex failure fixture did not reach checkpoint");
    };
    await([&](const auto&){return capture.capture_opened==1;});
    session.transmit_bits(Bytes{0,0,1});
    auto failed=await([](const auto& state){return state.transmission_failed;});
    check(!failed.transmitting && failed.transmission_finished && failed.transmission_fraction<1 &&
          failed.error=="injected audio output failure" && failed.status=="Transmission failed",
          "failed output was presented as successful transmission");
    check(capture.played==0 && capture.source_calls==(after_generation?1U:0U),
          "failed playback retried or submitted generated PCM");
    check((failed.transmit_key_lock_seconds>0)==after_generation,
          "TX failure either consumed an unused key epoch or released generated private patterns");
    capture.restart_capture=true;
    failed=await([&](const auto&){return capture.capture_opened>=2;});
    check(failed.transmission_failed && failed.error=="injected audio output failure",
          "duplex capture restart erased the TX failure");
    session.clear_received();failed=session.snapshot();
    check(failed.transmission_failed && failed.error=="injected audio output failure" &&
          (failed.transmit_key_lock_seconds>0)==after_generation,
          "Clear received erased TX failure or changed private epoch protection");
    session.stop();
}

}

// These definitions deliberately replace audio.cpp in this statically linked
// test executable. The live Session still uses its real asynchronous hardware
// capture queue, receiver bank, progress presentation and content publication.
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,
             std::stop_token stop,StreamFormatCallback format,Options options) {
    validate_options(options);
    if(capture_script && capture_script->monitor_rate) {
        auto& script=*capture_script;
        if(script.timing_status) {
            check(bool(options.timing_status),"timing fallback fixture lost status observer");
            options.timing_status(*script.timing_status);
        }
        check(rate==script.rate && device=="controlled profile capture","monitor fixture capture identity changed");
        if(format)format({rate,script.monitor_rate,rate/2.,script.monitor_format_bytes});
        while(!stop.stop_requested()) {
            const auto interrupts=script.interruptions.load();
            if(interrupts>script.interruptions_delivered.load()) {
                if(options.capture_discontinuity)options.capture_discontinuity();
                script.interruptions_delivered=interrupts;
            }
            auto begin=script.monitor_delivered.load(),end=script.monitor_released.load();
            if(end>begin) {
                if(script.before_capture)script.before_capture(end);
                if(options.capture_monitor)options.capture_monitor(std::span(script.monitor_samples).subspan(begin,end-begin),script.monitor_rate);
                script.monitor_delivered=end;
                if(script.after_monitor)script.after_monitor();
            }
            begin=script.delivered.load();end=script.released.load();
            if(end>begin) {
                if(script.timing && (!script.timing_status || script.timing_status->following_system_clock || options.request_capture_timing)) {
                    check((options.follow_system_clock || options.request_capture_timing) && bool(options.capture_timing),"timed fixture lost its capture callback");
                    options.capture_timing(script.timing(begin));
                }
                if(!callback(std::span(script.samples).subspan(begin,end-begin)))return;script.delivered=end;
            }
            std::this_thread::sleep_for(1ms);
        }
        return;
    }
    capture(rate,device,callback,stop,std::move(format));
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& callback,
              std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options) {
    validate_options(options);
    playback(rate,device,callback,stop,std::move(format),channels);
}
std::vector<Device> devices() { return {{"controlled profile capture", "deterministic test input"}}; }
void capture(std::uint32_t rate, const std::string& device, const CaptureCallback& on_chunk,
             std::stop_token stop, StreamFormatCallback on_format) {
    check(capture_script && device == "controlled profile capture" && rate == capture_script->rate,
          "unexpected hardware capture request in profile fixture");
    auto& script = *capture_script;
    ++script.capture_opened;
    struct Closed {CaptureScript& script;~Closed(){++script.capture_closed;}}closed{script};
    if (on_format) on_format({rate, rate, static_cast<double>(rate) / 2, script.format_bytes});
    while (!stop.stop_requested()) {
        if(script.restart_capture.exchange(false))return;
        const auto begin = script.delivered.load();
        const auto end = std::min(script.released.load(), begin + std::max<std::uint32_t>(8, rate / 50));
        if (end > begin) {
            if (!on_chunk(std::span(script.samples).subspan(begin, end - begin))) return;
            script.delivered = end;
        }
        std::this_thread::sleep_for(1ms);
    }
}
void play(std::span<const float>, std::uint32_t, const std::string&, std::stop_token,
          StreamFormatCallback, ChannelMode) { throw Error("profile fixture unexpectedly played audio"); }
std::vector<float> record(double, std::uint32_t, const std::string&, std::size_t,
                          std::stop_token, StreamFormatCallback) { throw Error("profile fixture unexpectedly recorded audio"); }
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode) {
    check(capture_script && capture_script->loopback && device=="controlled profile capture" && rate==capture_script->rate,
          "profile fixture unexpectedly played audio");
    auto& script=*capture_script;
    if(script.fail_playback_before_source)throw Error("injected audio output failure");
    if(format)format({rate,rate,rate/2.,32768});
    script.playback_started=true;
    std::vector<float> output(script.playback_chunk?script.playback_chunk:std::max<std::size_t>(8,rate/50));
    while(!stop.stop_requested()) {
        const auto count=source(output);
        ++script.source_calls;
        if(script.fail_playback_after_source)throw Error("injected audio output failure");
        if(!count)break;
        const auto offset=script.played.load();
        if(script.loopback_channel || script.buffer_playback) {
            script.playback_input.insert(script.playback_input.end(),output.begin(),output.begin()+count);
            script.played=offset+count;
            continue;
        }
        check(offset+count<=script.samples.size(),"duplex loopback output exceeded bounded fixture");
        std::copy_n(output.begin(),count,script.samples.begin()+offset);
        script.played=offset+count;script.released=offset+count;
        while(script.delivered<offset+count && !stop.stop_requested())std::this_thread::sleep_for(1ms);
    }
    if(script.loopback_channel && !stop.stop_requested())
        script.samples=modem::simulate(script.playback_input,script.loopback_modem,*script.loopback_channel);
    else if(script.buffer_playback && !stop.stop_requested())script.samples=script.playback_input;
    script.playback_source_finished=true;
    while(!stop.stop_requested() && !script.finish_playback)std::this_thread::sleep_for(1ms);
}
}

void outer_version_profile_identity() {
    auto value=settings({});value.transfer.automatic_receive_profiles=false;
    value.transfer.key.emplace(Bytes(32,0x67)); // Synthetic key required by the encrypted profile.
    auto& config=value.transfer.modem;
    config.bandwidth_hz=120;config.carrier_hz=1500;config.sample_rate=6000;
    config.scramble=config.dsss=true;config.dsss_factor=10;
    config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    value.long_message_modem=config;
    live::validate_settings(value);
    value.long_message_modem->outer_dsss_version=modem::OuterDsssVersion::legacy_v1;
    bool rejected=false;
    try {live::validate_settings(value);}catch(const Error&){rejected=true;}
    check(rejected,"short and long hardware profiles must not silently mix outer wire versions");
    config.dsss_factor=1;value.long_message_modem->dsss_factor=1;
    live::validate_settings(value); // Version has no effect when outer DSSS is Off.
}

// Functional instrument, not a sustained/headroom or sensitivity benchmark.
// It drives the real Live capture queue with bounded original scheduled PCM,
// proves the native Pool is actually reached and checks its joined teardown.
bool live_exact_source_pool(const std::filesystem::path& requested_trace={}) {
    namespace xs=modem::detail::scheduled_source_samples;
#if defined(__GLIBC__) && defined(__GNUC__) && __GNUC__>=14 && defined(__x86_64__) && !defined(__FAST_MATH__)
    const auto native_qualified=xs::environment() && modem::detail::scheduled_acquisition_certificate::coordinate_environment();
#else
    const auto native_qualified=false;
#endif
    if(!native_qualified){std::cout<<"OMITTED: native exact-source positive-hit coverage; unqualified native-screen environment\n";return false;} // Numeric qualification has a separate fallback unit.
    struct FixtureEnvironment {
        const char* name;std::optional<std::string> previous;
        FixtureEnvironment(const char* key,const char* value):name(key) {
            if(const auto* old=std::getenv(name))previous=old;
#if defined(_WIN32) && !defined(__linux__)
            check(_putenv_s(name,value)==0,"could not set exact source fixture environment");
#else
            check(setenv(name,value,1)==0,"could not set exact source fixture environment");
#endif
        }
        ~FixtureEnvironment() {
#if defined(_WIN32) && !defined(__linux__)
            const auto result=_putenv_s(name,previous?previous->c_str():"");
#else
            const auto result=previous?setenv(name,previous->c_str(),1):unsetenv(name);
#endif
            // Restoration failure must not silently contaminate subsequent cases.
            if(result!=0)std::terminate();
        }
        FixtureEnvironment(const FixtureEnvironment&)=delete;
        FixtureEnvironment& operator=(const FixtureEnvironment&)=delete;
    };
    const FixtureEnvironment lazy("DATAPUMP_SCHED_LAZY_ACQUISITION","1"),
        source("DATAPUMP_SCHED_EXACT_SOURCE_TILES","1"),batch("DATAPUMP_SCHED_LAZY_BATCH","auto"),
        origin_classes("DATAPUMP_SCHED_ORIGIN_CLASSES","1");
    auto value=settings({});value.transfer.automatic_receive_profiles=false;
    // This isolated instrument has an explicit budget; ordinary cases retain 32 MiB.
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=128U*1024U*1024U;
    value.transfer.key.emplace(Bytes(32,0x73));value.receive_keys.clear();
    // The synthetic generator and capture share an exact clock and no device
    // lag. Supply that evidence when cropping away the physical TX prefix.
    value.transfer.clock_sync=clock_sync::Policy{0,0,0};
    value.transfer.audio_timing_error_seconds=0;
    auto& c=value.transfer.modem;c.sample_rate=512;c.bandwidth_hz=16;c.carrier_hz=128;
    c.integration_seconds=8;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    // This positive integration instrument exercises the native preparation
    // which consumes exact source rows. FP64-force deliberately bypasses that
    // approximation; its original-template behavior has separate regressions.
    c.receiver_strategy=modem::ReceiverStrategy::existing_only;c.search_arithmetic=modem::SearchArithmetic::default_mode;
    c.oscillator_search.emplace();c.oscillator_search->lf={0,0};c.oscillator_search->rf={0,0};
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=10;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
    plan->ramp_seconds=.01;c.scheduled_hopping=plan;c.data_key=value.transfer.key;
    const modem::SchedHopSourceMap calendar(*plan,*value.transfer.key,c.sample_rate);
    auto selected=epoch;for(;selected<epoch+128;++selected)
        if(calendar.locate({selected*c.sample_rate,0}).active)break;
    check(selected<epoch+128,"bounded keyed calendar had no eligible integer epoch");
    value.transfer.timestamp=selected;c.stream_epoch=selected;c.stream_phase_samples=0;
    check(!modem::packed_scheduled_symbols(c)&&modem::symbol_sample_count(c)==8ULL*c.sample_rate,
        "positive Live source fixture lost its split-symbol geometry");
    auto transmitter=transfer::binary_transmitter(Bytes{0},value.transfer);
    CaptureScript capture;capture.rate=c.sample_rate;
    // Endpoint is elapsed PCM; output origin is an absolute UTC sample address.
    const auto valid_end=transmitter->physical_valid_payload_end();
    const auto output_origin=transmitter->physical_output_origin();
    check(valid_end>0 && valid_end<=transmitter->total_samples(),"original scheduled payload endpoint escaped output");
    check(valid_end<=std::numeric_limits<std::uint64_t>::max()-output_origin,
        "original scheduled payload endpoint overflowed UTC coordinate");
    const auto absolute_valid_end=output_origin+valid_end;
    check(absolute_valid_end>=output_origin,"original scheduled payload endpoint precedes output");
    // Numeric screening is due only after a complete source symbol. Capture
    // that bit plus two physical frame lookahead, while retaining an incomplete
    // reception: no complete absent source symbol is supplied.
    const auto observed=std::min<std::size_t>(transmitter->total_samples(),valid_end+8ULL*c.sample_rate);
    check(observed<=std::numeric_limits<std::uint64_t>::max()-output_origin,
        "partial capture end overflowed UTC coordinate");
    check(observed<=(2U*1024U*1024U)/sizeof(float) && observed>valid_end &&
        calendar.locate({output_origin+observed,0}).source.distance(calendar.locate({absolute_valid_end,0}).source)<
            modem::symbol_sample_count(c),"positive Live instrument supplied a whole absent source symbol or excessive PCM");
    const auto payload_anchor=selected*static_cast<std::uint64_t>(c.sample_rate);
    check(payload_anchor>=output_origin && payload_anchor-output_origin<valid_end,
        "scheduled payload anchor escaped the original capture");
    const auto skipped=static_cast<std::size_t>(payload_anchor-output_origin);
    // Explicit-timestamp mock capture begins at the payload UTC anchor. The
    // scheduled prefix has a physical duration different from its source duration.
    // Read original PCM without changing transmitter keys, geometry or samples.
    std::vector<float> original_capture(observed);
    for(std::size_t at=0;at<observed;){const auto n=transmitter->read(std::span(original_capture).subspan(at));
        check(n>0,"bounded original transmitter ended before partial capture");at+=n;}
    capture.samples.assign(original_capture.begin()+skipped,original_capture.end());
    capture.monitor_rate=c.sample_rate;
    std::atomic<std::size_t> timing_observations{0};
    capture.timing=[&,selected,rate=c.sample_rate](std::size_t sample) {
        ++timing_observations;
        return audio::TimePrediction{static_cast<long double>(selected)+static_cast<long double>(sample)/rate,
            0,1./rate,audio::TimingQuality::bounded,0};
    };
    const auto provided=capture.samples.size();bool accepted_zero=false;
    capture_script=&capture;
    const auto path=requested_trace.empty()?std::filesystem::current_path()/
        ("live-exact-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl"):requested_trace;
    std::shared_ptr<audio::trace::State> trace_state;
    {
        audio::trace::Scope trace(path);trace_state=audio::trace::active.load();
        live::Session session([=]{return static_cast<double>(selected);});session.start(value);
        testing::CpuWorkBudget budget(30s,"positive Live exact source binding");
        std::optional<std::chrono::steady_clock::time_point> drained_at;
        bool drained=false;
        while(budget.pending()) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty(),"positive Live source binding failed: "+snapshot.error);
            check(!snapshot.receiver_health.failed() && snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,
                "optional exact source Pool caused a receiver reset or workspace overflow");
            for(const auto& signal:snapshot.signals) {
                check(!signal.complete && signal.received_bits<=1 &&
                    (!signal.received_bits || signal.text=="0" || signal.raw_bits=="0"),
                    "bounded first-bit capture manufactured extra/wrong bits or physical absence");
                accepted_zero|=signal.received_bits==1 && !signal.complete &&
                    (signal.text=="0" || signal.raw_bits=="0");
            }
            const auto delivered=capture.delivered.load();
            if(delivered<provided && delivered>=capture.released.load() && !snapshot.buffered_samples)
                capture.released=delivered+std::min<std::size_t>(std::max<std::size_t>(8,capture.rate/10),provided-delivered);
            if(delivered>=provided && !snapshot.buffered_samples &&
               snapshot.processed_physical_samples>=provided && snapshot.receiver_backlog_seconds==0) {
                if(!drained_at)drained_at=std::chrono::steady_clock::now();
                if(std::chrono::steady_clock::now()-*drained_at>=250ms){drained=true;break;}
            } else drained_at.reset();
            std::this_thread::sleep_for(1ms);
        }
        check(drained,"partial Live exact-source fixture did not drain its bounded capture queue");
        check(timing_observations>0,"cropped exact-source fixture lost its physical sample timestamps");
        check(accepted_zero,"complete intended source bit never published pending zero in positive Live fixture");
        session.clear_received();session.stop();
    }
    check(bool(trace_state),"positive Live source trace State is missing");
    {std::lock_guard lock(trace_state->mutex);
        check(trace_state->done && !trace_state->dropped && !trace_state->io_failed && !trace_state->size_limited,
            "positive Live source trace is incomplete");}
    std::ifstream trace(path);check(bool(trace),"positive Live source trace is missing");
    std::string line;bool allocated=false,queried=false,final=false,footer=false;
    const auto field=[](const std::string& text,const char* name) {
        const auto label=std::string("\"")+name+"\":";const auto at=text.find(label);
        return at==std::string::npos?0ULL:std::stoull(text.substr(at+label.size()));};
    while(std::getline(trace,line)) {
        if(line.find("\"event\":\"receiver_exact_source_samples\"")!=std::string::npos) {
            allocated|=field(line,"rows")>0&&field(line,"bytes")>0;
            queried|=field(line,"queries")>0&&field(line,"cached_points")>0;
            final|=line.find("\"final_snapshot\":1")!=std::string::npos;
        }
        footer|=line.find("\"event\":\"trace_end\"")!=std::string::npos;
    }
    check(allocated&&queried&&final&&footer,"Live optional source Pool was allocated without productive joined native binding or final accounting");
    check(xs::binding.pool==nullptr && !xs::binding.reclaim,"Live instrument leaked a source binding into caller TLS");
    capture_script=nullptr;
    std::cout<<"Live exact source Pool positive partial functional trace: "<<path<<"\n";
    return true;
}

int main(int argc, char** argv) {
    std::string context;
    try {
#ifdef _WIN32
        const FixtureTimerResolution timer_resolution;
#endif
        const std::string suite = argc > 1 ? argv[1] : "all";
        std::optional<audio::trace::Scope> calendar_trace;
        if((suite=="hop_calendar" || suite=="public_amplitude") && argc>2)calendar_trace.emplace(argv[2]);
        check(suite == "public_amplitude" || suite == "retired" || suite == "all" || suite == "hop_calendar" || suite == "exact_source" || suite == "original" || suite == "matrix" || suite == "long" || suite == "long_fft" || suite == "long_seeds" || suite == "interval_queue" || suite == "monitor" || suite == "health" || suite == "frontier_failure" || suite == "capture_time" || suite == "arrival_time" || suite == "arrival_time_baseline" || suite == "backlog" || suite == "clear" || suite == "duplex" || suite == "duplex_spread" || suite == "hardware_channel" || suite == "arrival_fallback" || suite == "tx_failure",
              "unknown profile test suite");
        if(suite=="hardware_channel") {context="production hardware channel";hardware_channel_loopback();return 0;}
        if(suite=="duplex_spread") {context="modeled virtual duplex";duplex_spread_loopback();return 0;}
        if(suite=="all" || suite=="public_amplitude") {context="rolling public amplitude arrivals";public_amplitude_rolling_arrivals();if(suite=="public_amplitude")return 0;}
        if(suite=="all" || suite=="retired") {
            context="retired public reception identities";
            retired_public_streams_release_presentations({32,55});
            retired_public_streams_release_presentations({55,32});
            if(suite=="retired")return 0;
        }
        if(suite=="exact_source") {context="positive partial Live exact source binding";if(!live_exact_source_pool(argc>2?std::filesystem::path(argv[2]):std::filesystem::path{}))return 77;}
        if(suite=="all" || suite=="duplex") {
            context="outer DSSS profile identity";outer_version_profile_identity();
            context="single-session hardware duplex loopback";duplex_loopback();
            context="single-session keyed hardware duplex loopback";duplex_loopback(true);
        }
        if(suite=="all" || suite=="tx_failure") {
            context="duplex failed output before generation";duplex_transmit_failure(false);
            context="duplex failed output after generation";duplex_transmit_failure(true);
        }
        if(suite=="all" || suite=="clear") {context="Clear received backlog cancellation";receiver_backlog_before_overflow(true);}
        if(suite=="all" || suite=="backlog") {context="receiver backlog before overflow";receiver_backlog_before_overflow();}
        if(suite=="all" || suite=="capture_time") {context="captured private epoch survives decode delay";captured_epoch_survives_delay();}
        if(suite=="all" || suite=="arrival_fallback") {context="unqualified UTC capture falls back visibly";modeled_arrival_window(true,true);}
        if(suite=="all" || suite=="arrival_time") {context="modeled compact UTC arrival";modeled_arrival_window();}
        if(suite=="all" || suite=="hop_calendar") {context="Default shared hop calendar3/5 Audio0";default_hop_calendar(3);default_hop_calendar(5);}
        if(suite=="arrival_time_baseline") {context="full-window UTC arrival control";modeled_arrival_window(false);}
        if(suite=="all" || suite=="frontier_failure") {context="receiver exception physical frontier";receiver_exception_frontier();}
        if(suite=="all" || suite=="health") {context="receiver loss health";receiver_loss_health();}
        if(suite=="all" || suite=="monitor") {context="independent environment monitor";independent_environment_monitor();}
        if (suite == "interval_queue") {
            context = "RX 55,32 TX 32 fixed interval queue pacing";
            run_case({55, 32}, 32, "hello fixed intervals", false);
        }
        if (suite == "all" || suite == "long_seeds") {
            context = "sampled expanded FFT simulation across strong and weak noise seeds";
            sampled_long_fft_seeds();
        }
        if (suite == "all" || suite == "long" || suite == "long_fft") {
            context = "continuous public full FFT capture of one 40-second bit";
            continuous_long_fft_single_bit();
            context = "sampled public full FFT simulation of one 40-second bit";
            simulated_long_fft_single_bit();
        }
        if (suite == "all" || suite == "original") for (const auto& targets : {std::vector<double>{55, 32}, std::vector<double>{32, 55}}) {
            for (const double target : {55., 32.}) {
                const auto profiles = "RX " + std::to_string(targets[0]) + "," + std::to_string(targets[1]) +
                    " TX " + std::to_string(target);
                context = profiles + " text e";
                run_case(targets, target, "e", false);
                context = profiles + " binary 001";
                run_case(targets, target, "001", true);
                context = profiles + " text hello";
                run_case(targets, target, "hello", false);
                context = profiles + " fixed interval text";
                run_case(targets, target, "hello fixed intervals", false);
            }
            context = "sequential TX 55 then 32, first RX " + std::to_string(targets[0]);
            sequential_profiles(targets, 55, 32);
            context = "sequential TX 32 then 55, first RX " + std::to_string(targets[0]);
            sequential_profiles(targets, 32, 55);
        }
        if (suite == "all" || suite == "matrix") {
            for (auto targets : {std::vector<double>{55, 62, 72}, std::vector<double>{20, 26, 32}}) {
                std::set<std::uint64_t> durations;
                for (auto target : targets)
                    durations.insert(modem::symbol_sample_count(modem_config(target, {})));
                check(durations.size() == targets.size(), "profile matrix lost its distinct symbol geometries");
                // Different automatic chip floors and integration durations;
                // every permutation must choose evidence, never bank order.
                do {
                    for (double target : targets) {
                        context = "permuted RX " + std::to_string(targets[0]) + "," +
                            std::to_string(targets[1]) + "," + std::to_string(targets[2]) +
                            " TX " + std::to_string(target) + " binary 001";
                        run_case(targets, target, "001", true);
                    }
                } while (std::next_permutation(targets.begin(), targets.end()));
                for (double target : targets) {
                    context = "three-profile TX " + std::to_string(target) + " binary 0";
                    run_case(targets, target, "0", true);
                }
            }
            Configuration noisy;
            noisy.noise_sigma = .005f;
            for (double target : {55., 26., 20.}) {
                context = "mixed noisy RX 55,26,20 TX " + std::to_string(target);
                run_case({55, 26, 20}, target, "001", true, noisy);
            }
            Configuration tone;
            tone.mode = tuning::PatternMode::auto_tone;
            const auto double_tone = waveform(55, "00", true, tone);
            const auto single_tone = waveform(32, "0", true, tone);
            const auto physical_end = double_tone.payload_end + 3 * double_tone.rate;
            check(double_tone.bits == "00" && single_tone.bits == "0" &&
                  double_tone.payload_end == single_tone.payload_end &&
                  std::equal(double_tone.samples.begin(), double_tone.samples.begin() + physical_end,
                             single_tone.samples.begin()),
                  "unframed repeated-tone fixture no longer demonstrates identical PCM across symbol durations");
            for (double target : {55., 26., 20.}) {
                context = "tone RX 20,55,26 TX " + std::to_string(target) + " binary 0";
                run_case({20, 55, 26}, target, "0", true, tone);
            }
            Configuration keyed;
            keyed.keyed = true;
            for (double target : {55., 32., 26.}) {
                context = "keyed RX 32,26,55 with an unrelated receive key, TX " + std::to_string(target);
                run_case({32, 26, 55}, target, "001", true, keyed);
            }
        }
        if (suite == "all" || suite == "long") {
            Configuration long_symbols;
            long_symbols.bandwidth = 16;
            long_symbols.carrier = 16;
            check(modem_config(5, long_symbols).sample_rate == 64 &&
                  modem::symbol_seconds(modem_config(5, long_symbols)) > 6,
                  "long fixture no longer exercises a full absent symbol beyond six seconds");
            for (const auto& targets : {std::vector<double>{55, 5}, std::vector<double>{5, 55},
                                       std::vector<double>{55, 20, 5}, std::vector<double>{5, 20, 55}}) {
                context = "long RX first " + std::to_string(targets.front()) +
                    " count " + std::to_string(targets.size()) + " TX 5 binary 001";
                run_case(targets, 5, "001", true, long_symbols, true);
            }
            context = "off-clock short transmission during long-profile absence wait";
            separated_long_and_short();
        }
        std::cout << "live receive profile arbitration passed\n";
    } catch (const std::exception& error) {
        std::cerr << context << ": " << error.what() << '\n';
        return 1;
    }
}
