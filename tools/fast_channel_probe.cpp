// Bounded Fast test using the production transmitter, channel and decoder.
#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/fast/simulation.hpp"
#include "datapump/tuning.hpp"
#include "fast/cable_loopback.hpp"
#include "fast/ssb_receiver.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <openssl/evp.h>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace datapump;
using namespace datapump::fast;

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto source_quota = 1ULL << 20;
constexpr auto decoder_quota = 256ULL * 1024 * 1024;
constexpr auto large_estimate_bytes = 50'000'000ULL;
constexpr std::string_view filename = "fast-test.bin";
constexpr std::size_t block_samples = 4096;
constexpr std::size_t retained_capture_samples = 36*48000;

struct Options {
    Profile profile = capacity_profile(Channel::ssb);
    SimulationSettings channel;
    std::string base_oscillator = "crystal", shift_oscillator = "ic-7100", spread = "hf-worst";
    std::string capture_device, playback_device;
    std::filesystem::path retain_capture;
    std::uint64_t seed = 417;
    unsigned source_bytes = 384;
    double max_audio_seconds = 75, max_wall_seconds = 90, tx_gain = 1;
    bool encryption = true, live = false, profile_override = false;
    bool bootstrap_gate = false, bootstrap_only = false;
};

struct Metrics {
    std::uint64_t count = 0, clipped = 0, nonfinite = 0;
    double peak = 0, energy = 0;
    void add(std::span<const float> samples) {
        for (const double x : samples) {
            ++count;
            if(!std::isfinite(x)) {++nonfinite;continue;}
            peak = std::max(peak, std::abs(x));
            energy += x * x;
            clipped += std::abs(x) >= .999;
        }
    }
    double rms() const { return count>nonfinite ? std::sqrt(energy / (count-nonfinite)) : 0; }
};

std::uint64_t splitmix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Bytes payload(unsigned count, std::uint64_t seed) {
    Bytes bytes(count);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(splitmix(seed + i) >> 56);
    return bytes;
}

std::optional<Crypto> test_key(const Options& options) {
    if (!options.encryption) return std::nullopt;
    // Independent deterministic roots for this local test fixture only.
    std::array<std::uint8_t, 64> aes{}, serpent{};
    for (std::size_t i = 0; i < aes.size(); ++i) {
        aes[i] = static_cast<std::uint8_t>(splitmix((options.seed ^ 0x4c6ba4113b244b1aULL) + i) >> 56);
        serpent[i] = static_cast<std::uint8_t>(splitmix((options.seed ^ 0x69b5801e8aebdaecULL) + i) >> 56);
    }
    return Crypto::from_independent_roots(serpent, aes);
}

std::string json_string(std::string_view value) {
    std::string out = "\"";
    for (const auto c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) < 32) out += ' ';
        else out += c;
    }
    return out + '"';
}

template<class T>
void optional_json(const std::optional<T>& value) {
    if (value) std::cout << *value;
    else std::cout << "null";
}

struct ProcessMemory {
    std::uint64_t vm_size_kib,vm_peak_kib,vm_rss_kib,vm_hwm_kib;
};

std::optional<ProcessMemory> process_memory() noexcept {
#ifdef __linux__
    const auto file=::open("/proc/self/status",O_RDONLY|O_CLOEXEC);
    if(file<0)return {};
    std::array<char,4096> buffer{};
    const auto count=::read(file,buffer.data(),buffer.size());
    ::close(file);
    if(count<=0)return {};
    const std::string_view status(buffer.data(),static_cast<std::size_t>(count));
    const auto field=[&](std::string_view name)->std::optional<std::uint64_t> {
        const auto start=status.find(name);
        if(start==std::string_view::npos)return {};
        auto first=start+name.size();
        while(first<status.size() && (status[first]==' ' || status[first]=='\t'))++first;
        std::uint64_t value=0;
        const auto parsed=std::from_chars(status.data()+first,status.data()+status.size(),value);
        if(parsed.ec!=std::errc{})return {};
        return value;
    };
    const auto size=field("\nVmSize:"),peak=field("\nVmPeak:");
    const auto rss=field("\nVmRSS:"),high_water=field("\nVmHWM:");
    if(size && peak && rss && high_water)return ProcessMemory{*size,*peak,*rss,*high_water};
#endif
    return {};
}

void memory_json(const std::optional<ProcessMemory>& memory) {
    if(!memory) {std::cout << "null";return;}
    std::cout << "{\"vm_size_kib\":" << memory->vm_size_kib << ",\"vm_peak_kib\":" << memory->vm_peak_kib
              << ",\"vm_rss_kib\":" << memory->vm_rss_kib << ",\"vm_hwm_kib\":" << memory->vm_hwm_kib << '}';
}

std::string digest(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char,32> hash{};
    unsigned size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), hash.data(), &size, EVP_sha256(), nullptr) != 1 || size != hash.size())
        throw Error("cannot compute probe SHA-256 digest");
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto value : hash) out << std::setw(2) << static_cast<unsigned>(value);
    return out.str();
}

double elapsed(Clock::time_point from) {
    return std::chrono::duration<double>(Clock::now() - from).count();
}

void usage() {
    std::cout << "fast_channel_probe --channel ssb --encryption yes --transmit-watts W --path-loss-db dB\n"
        "  --base-oscillator ID|off --shift-oscillator ID|off|baseband-clock --spread ID\n"
        "  --selective-depth D --margin N --shift-hz Hz [--noise-dbm-hz dBm/Hz|off]\n"
        "  [--seed N] [--source-bytes N] [--max-audio-seconds S] [--max-wall-seconds S]\n"
        "  [--bootstrap-gate]                Continue the short SSB trial only after a verified bootstrap\n"
        "  [--bootstrap-only]                Stop an offline gated trial at the first coding cycle\n"
        "  [--profile-override [--expected-snr dB] [--constellation N] [--code-rate R] [--depth N]\n"
        "     [--symbol-rate baud] [--pilot-spacing N] [--marker-spacing N] [--rolloff R] [--ldpc-bits 16200|64800]]\n"
        "  [--live (--device NAME | --capture-device NAME --playback-device NAME) [--tx-gain G]\n"
        "     [--retain-capture NEW_F32LE_PATH]]\n";
}

double number(const std::string& value, const char* name) {
    std::size_t used = 0;
    const auto result = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(result)) throw Error(std::string("invalid ") + name);
    return result;
}

template<class T>
T integer(std::string_view value, const char* name) {
    T result{};
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) throw Error(std::string("invalid ") + name);
    return result;
}

Options parse(int argc, char** argv) {
    Options o;
    Channel channel = Channel::ssb;
    std::optional<double> expected_snr, symbol_rate, rolloff;
    std::optional<unsigned> constellation, depth, pilot_spacing, marker_spacing, ldpc_bits;
    std::optional<std::string> code_rate;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") { usage(); std::exit(0); }
        auto value = [&]() -> std::string {
            if (++i == argc) throw Error("missing value for " + option);
            return argv[i];
        };
        if (option == "--channel") channel = parse_channel(value());
        else if (option == "--encryption") {
            const auto mode = value();
            if (mode != "yes" && mode != "no") throw Error("encryption must be yes or no");
            o.encryption = mode == "yes";
        } else if (option == "--transmit-watts") o.channel.transmit_watts = number(value(), "transmit watts");
        else if (option == "--path-loss-db") o.channel.path_loss_db = number(value(), "path loss");
        else if (option == "--base-oscillator") o.base_oscillator = value();
        else if (option == "--shift-oscillator") o.shift_oscillator = value();
        else if (option == "--spread") o.spread = value();
        else if (option == "--selective-depth") {
            o.channel.selective_slow_depth = number(value(), "selective depth");
            o.channel.selective_fast_depth = o.channel.selective_slow_depth;
        } else if (option == "--selective-slow-depth") o.channel.selective_slow_depth = number(value(), "slow selective depth");
        else if (option == "--selective-fast-depth") o.channel.selective_fast_depth = number(value(), "fast selective depth");
        else if (option == "--margin") o.channel.margin = number(value(), "margin");
        else if (option == "--shift-hz") o.channel.shift_hz = number(value(), "shift frequency");
        else if (option == "--noise-dbm-hz") {
            const auto noise = value();
            if (noise == "off") o.channel.noise_dbm_hz.reset();
            else o.channel.noise_dbm_hz = number(noise, "noise density");
        } else if (option == "--seed") o.seed = integer<std::uint64_t>(value(), "seed");
        else if (option == "--source-bytes") o.source_bytes = integer<unsigned>(value(), "source byte count");
        else if (option == "--max-audio-seconds") o.max_audio_seconds = number(value(), "audio limit");
        else if (option == "--max-wall-seconds") o.max_wall_seconds = number(value(), "wall limit");
        else if (option == "--bootstrap-gate") o.bootstrap_gate = true;
        else if (option == "--bootstrap-only") o.bootstrap_only = true;
        else if (option == "--profile-override") o.profile_override = true;
        else if (option == "--expected-snr") expected_snr = number(value(), "expected SNR");
        else if (option == "--constellation") constellation = integer<unsigned>(value(), "constellation");
        else if (option == "--code-rate") code_rate = value();
        else if (option == "--depth") depth = integer<unsigned>(value(), "interleave depth");
        else if (option == "--symbol-rate") symbol_rate = number(value(), "symbol rate");
        else if (option == "--pilot-spacing") pilot_spacing = integer<unsigned>(value(), "pilot spacing");
        else if (option == "--marker-spacing") marker_spacing = integer<unsigned>(value(), "marker spacing");
        else if (option == "--rolloff") rolloff = number(value(), "rolloff");
        else if (option == "--ldpc-bits") ldpc_bits = integer<unsigned>(value(), "LDPC frame bits");
        else if (option == "--live") o.live = true;
        else if (option == "--device") o.capture_device = o.playback_device = value();
        else if (option == "--capture-device") o.capture_device = value();
        else if (option == "--playback-device") o.playback_device = value();
        else if (option == "--retain-capture") {
            o.retain_capture = value();
            if(o.retain_capture.empty())throw Error("capture retention requires a new output path");
        }
        else if (option == "--tx-gain") o.tx_gain = number(value(), "transmit gain");
        else throw Error("unknown option: " + option);
    }
    const bool has_override = expected_snr || constellation || code_rate || depth ||
        symbol_rate || pilot_spacing || marker_spacing || rolloff || ldpc_bits;
    if (has_override != o.profile_override) throw Error("use --profile-override together with an explicit profile control");
    if (ldpc_bits && (channel != Channel::ssb || (*ldpc_bits != 16200 && *ldpc_bits != 64800)))
        throw Error("Fast SSB LDPC frame override must be 16200 or 64800 bits");
    if (o.bootstrap_only && (!o.bootstrap_gate || o.live))
        throw Error("bootstrap-only requires an offline bootstrap-gated trial");
    const auto snr = expected_snr.value_or(default_expected_snr(channel));
    o.profile = resolve_snr_preset(channel, snr).profile;
    if (constellation) o.profile.constellation = *constellation;
    if (code_rate) o.profile.code_rate = parse_code_rate(*code_rate);
    if (depth) o.profile.interleave_depth = *depth;
    if (symbol_rate) o.profile.symbol_rate = *symbol_rate;
    if (pilot_spacing) o.profile.pilot_spacing_symbols = *pilot_spacing;
    if (marker_spacing) o.profile.marker_spacing_intervals = *marker_spacing;
    if (rolloff) o.profile.rolloff = *rolloff;
    if (ldpc_bits) o.profile.ldpc_frame_bits = *ldpc_bits;
    else o.profile=normalize_ssb_frame_size(o.profile);
    if (o.max_audio_seconds < .05 || o.max_audio_seconds > 600 ||
        o.max_wall_seconds < .05 || o.max_wall_seconds > 900 ||
        !o.source_bytes || o.source_bytes > 512 * 1024)
        throw Error("audio and wall bounds must be 0.05..600 and 0.05..900 seconds; source must be 1..524288 bytes");
    if (o.live) {
        if (o.capture_device.empty() || o.playback_device.empty())
            throw Error("live requires explicit capture and playback devices");
        audio::Options audio_options;
        audio_options.transmit_gain = o.tx_gain;
        audio::validate_options(audio_options);
    } else if (!o.capture_device.empty() || !o.playback_device.empty() || o.tx_gain != 1 || !o.retain_capture.empty()) {
        throw Error("audio devices, transmit gain and capture retention apply only to a live cable test");
    }
    constexpr double radians = std::numbers::pi / 180;
    if (o.base_oscillator != "off") {
        const auto base = tuning::parse_oscillator_preset(o.base_oscillator);
        if (!base.baseband_supported) throw Error("the selected oscillator only describes an RF reference");
        o.channel.baseband_ppm = base.clock_error_ppm;
        o.channel.baseband_phase_rad_sqrt_s = base.phase_noise_degrees_per_sqrt_second * radians;
        o.base_oscillator = base.id;
    }
    o.channel.shared_reference = o.shift_oscillator == "baseband-clock";
    if (!o.channel.shared_reference && o.shift_oscillator != "off") {
        const auto shift = tuning::parse_oscillator_preset(o.shift_oscillator);
        o.channel.shift_ppm = shift.clock_error_ppm;
        o.channel.shift_phase_rad_sqrt_s = shift.phase_noise_degrees_per_sqrt_second * radians;
        o.shift_oscillator = shift.id;
    }
    const auto spread = tuning::parse_spread_preset(o.spread);
    o.spread = spread.id;
    o.channel.doppler_hz = spread.doppler_hz;
    o.channel.delay_ms = spread.delay_ms;
    o.channel.seed = o.seed;
    validate(o.profile, o.channel);
    if(o.bootstrap_gate && (o.profile.ldpc_frame_bits!=16200 ||
       !datapump::fast::detail::supports_modeled_ssb_receiver(o.profile)))
        throw Error("the bootstrap gate requires the standard short Fast SSB profile");
    return o;
}

struct Probe {
    struct PrefixComparison {
        std::size_t hard_sign_errors = 0, neutral = 0, nonfinite = 0;
        double evm = 0.;
    };
    struct MemoryObservation {
        std::string_view stage;
        std::optional<std::uint64_t> wire_cycle;
        std::optional<std::uint64_t> receiver_consumed_input_samples;
        double receiver_wall_seconds = 0;
        std::optional<ProcessMemory> memory;
    };
    const std::optional<ProcessMemory> baseline_memory=process_memory();
    const Options& options;
    const Bytes& source;
    const std::size_t encoded_bytes;
    const std::string encoded_hash;
    const TransmitEstimate estimate;
    StreamEncoder encoder;
    Transmitter tx;
    SimulationChannel channel;
    StreamDecoder decoder;
    Receiver receiver;
    Metrics digital, captured;
    std::uint64_t samples = 0, peak_spool_bytes = 0;
    std::optional<std::uint64_t> acquired_samples;
    std::array<std::array<std::uint8_t,physical_interval_bits>,2> transmitted_prefix{};
    std::array<PrefixComparison,2> compared_prefix{};
    std::size_t prefix_sent = 0, prefix_compared = 0;
    const std::size_t intervals_per_cycle;
    const std::uint64_t bootstrap_limit_samples;
    std::stop_source& receiver_stop;
    std::uint64_t submitted_intervals=0;
    bool bootstrap_checked=false, bootstrap_validated=false;
    std::size_t bootstrap_nonfinite_llrs=0;
    std::optional<std::uint64_t> last_requested_feedback,last_validated_feedback;
    std::string_view receiver_stage="receiver_dsp";
    std::array<MemoryObservation,32> memory_trace{};
    std::size_t memory_observations=0;
    double receiver_wall = 0, max_push_wall = 0;
    std::optional<Clock::time_point> receiver_work_started;
    double ideal_finish_seconds = 0, modeled_backlog_seconds = 0;

    Probe(const Options& o, const Bytes& input, const std::optional<Crypto>& key, PreparedXzSource xz,
          std::stop_source& stop)
        : options(o), source(input), encoded_bytes(xz.encoded.size()), encoded_hash(digest(xz.encoded)),
          estimate(estimate_transmission(o.profile, o.encryption, encoded_bytes)),
          encoder(datapump::fast::testing::deterministic_encoder(o.profile, key, byte_source(std::move(xz.encoded)),
              o.seed ^ 0x4267e23dba723f99ULL, SourceEncoding::xz)),
          tx(o.profile, [&](std::span<std::uint8_t> bits) {
              if (!encoder.next_interval(bits)) return false;
              if (!options.live && prefix_sent<transmitted_prefix.size())
                  std::ranges::copy(bits,transmitted_prefix[prefix_sent++].begin());
              return true;
          }),
          channel(o.profile, o.channel, [&](std::span<std::complex<float>> out) { return tx.read_analytic(out); }),
          decoder(o.profile, key, decoder_quota, SourceEncoding::xz),
          receiver(make_receiver(o,stop.get_token())),intervals_per_cycle(o.profile.capacity_mode?cycle_intervals(o.profile):0),
          // Allow public pilot lookahead and, for cable, the actual capture
          // preroll. This is a probe budget only, outside the receiver.
          bootstrap_limit_samples(o.bootstrap_gate?transmission_samples(o.profile,intervals_per_cycle)+
              o.profile.sample_rate/(o.live?2:4):0),
          receiver_stop(stop) {
        record_memory("receiver_ready");
        namespace native=datapump::fast::detail;
        if(auto* modem=native::ReceiverDiagnostics::ssb(receiver))modem->set_cycle_observer(
            [this](std::uint64_t cycle,native::SsbCycleObservation observation) {
                using Stage=native::SsbCycleObservation;
                switch(observation) {
                    case Stage::before_refinement:
                        receiver_stage="cycle_refinement";
                        record_memory("before_refinement",cycle);
                        break;
                    case Stage::after_refinement:
                        record_memory("after_refinement",cycle);
                        receiver_stage="receiver_dsp";
                        break;
                    case Stage::refinement_failed:
                        record_memory("refinement_failed",cycle);
                        break;
                }
            });
    }

    void record_memory(std::string_view stage,std::optional<std::uint64_t> cycle={}) noexcept {
        std::optional<std::uint64_t> consumed;
        if(const auto* modem=resilient())consumed=modem->consumed_input_samples();
        else if(!receiver_work_started)consumed=samples;
        const auto service=receiver_wall+(receiver_work_started?elapsed(*receiver_work_started):0.);
        memory_trace[memory_observations++%memory_trace.size()]={stage,cycle,consumed,service,process_memory()};
    }

    Receiver make_receiver(const Options& o,std::stop_token stop) {
        const auto submit = [this](std::span<const float> soft,bool feedback) {
            receiver_stage="codec";
            // Offline receipt only. The receiver and decoder never consume
            // the transmitter's protected bits or the comparison result.
            if (!options.live && prefix_compared<prefix_sent && soft.size()==physical_interval_bits) {
                const auto& expected=transmitted_prefix[prefix_compared];
                auto& receipt=compared_prefix[prefix_compared++];
                receipt.evm=progress().evm;
                for(std::size_t i=0;i<soft.size();++i) {
                    if(!std::isfinite(soft[i]))++receipt.nonfinite;
                    else if(soft[i]==0.F)++receipt.neutral;
                    else receipt.hard_sign_errors+=(soft[i]>0.F)!=bool(expected[i]);
                }
            }
            const bool closing=intervals_per_cycle && submitted_intervals%intervals_per_cycle==intervals_per_cycle-1;
            const bool check_bootstrap=options.bootstrap_gate && submitted_intervals==intervals_per_cycle-1;
            if(options.bootstrap_gate && submitted_intervals<intervals_per_cycle)
                bootstrap_nonfinite_llrs+=static_cast<std::size_t>(std::ranges::count_if(soft,[](float llr) {
                    return !std::isfinite(llr);
                }));
            if(closing) {
                const auto cycle=submitted_intervals/intervals_per_cycle;
                if(feedback)last_requested_feedback=cycle;
                record_memory("before_codec_cycle",cycle);
            }
            auto event=decoder.push_interval(soft,feedback||check_bootstrap);
            if(feedback && event)last_validated_feedback=event->wire_ordinal;
            if(check_bootstrap) {
                const auto validation=options.encryption?CapacityCycleValidation::authenticated:CapacityCycleValidation::checksummed;
                bootstrap_checked=true;
                bootstrap_validated=event && event->wire_ordinal==0 && event->validation==validation;
                if(!bootstrap_passed() || options.bootstrap_only)receiver_stop.request_stop();
            }
            if(closing)record_memory("after_codec_cycle",submitted_intervals/intervals_per_cycle);
            ++submitted_intervals;
            peak_spool_bytes=std::max(peak_spool_bytes,decoder.snapshot().spool_bytes);
            receiver_stage="receiver_dsp";
            return feedback?std::move(event):std::nullopt;
        };
        return Receiver(o.profile,submit,{},{},stop);
    }

    const ModemProgress& progress() const {
        return receiver.progress();
    }
    const datapump::fast::detail::SsbReceiver* resilient() const {
        return datapump::fast::detail::ReceiverDiagnostics::ssb(receiver);
    }
    std::optional<std::uint64_t> completion_tick() const {
        return receiver.physical_completion_input_tick();
    }

    bool bootstrap_passed() const { return bootstrap_validated && !bootstrap_nonfinite_llrs; }
    bool bootstrap_pending() const { return bootstrap_limit_samples && !bootstrap_passed(); }
    std::uint64_t input_limit(std::uint64_t full_limit) const {
        return bootstrap_pending()?std::min(bootstrap_limit_samples,full_limit):full_limit;
    }

    const char* bootstrap_end_reason() const {
        if(!bootstrap_checked)return nullptr;
        if(!bootstrap_passed())return "bootstrap_rejected";
        return options.bootstrap_only?"bootstrap_verified":nullptr;
    }

    const char* input_limit_reason(std::uint64_t full_limit) const {
        if(samples<input_limit(full_limit))return "wall_limit";
        if(bootstrap_pending() && bootstrap_limit_samples<=full_limit)
            return "bootstrap_audio_limit";
        return "audio_limit";
    }

    std::size_t produce(std::span<float> out) {
        const auto n = channel.read(out);
        digital.add(out.first(n));
        return n;
    }
    void consume(std::span<const float> pcm) {
        captured.add(pcm);
        const auto started = Clock::now();
        receiver_work_started=started;
        const auto record_time=[&] {
            const auto duration=elapsed(started);
            receiver_wall+=duration;
            receiver_work_started.reset();
            max_push_wall=std::max(max_push_wall,duration);
            if(!options.live) {
                // Replay measured block service against ideal arrivals. This
                // estimate is separate from the physical hardware FIFO.
                const auto arrival=static_cast<double>(samples+pcm.size())/options.profile.sample_rate;
                ideal_finish_seconds=std::max(ideal_finish_seconds,arrival)+duration;
                modeled_backlog_seconds=std::max(modeled_backlog_seconds,ideal_finish_seconds-arrival);
            }
        };
        std::size_t consumed=0;
        try {
            consumed=receiver.consume(pcm);
        } catch(...) {
            record_time();
            throw;
        }
        record_time();
        samples += consumed;
        if (!acquired_samples && progress().acquired) acquired_samples = samples;
    }
    bool exact() const {
        const auto result = decoder.result();
        return result && result->is_attachment() && result->filename() == filename &&
            std::ranges::equal(result->bytes(), source);
    }
};

struct RetainedCapture {
    std::uint64_t observed_samples = 0;
    std::size_t recorded_samples = 0, workspace_bytes = 0;
    bool discontinuity_reported = false;
    Metrics levels;
    std::string sha256, error;
};

std::string save_capture(const std::filesystem::path& path,std::span<float> samples) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    if constexpr(std::endian::native!=std::endian::little) {
        for(auto& sample:samples) {
            const auto word=std::bit_cast<std::uint32_t>(sample);
            sample=std::bit_cast<float>((word<<24)|((word&0xff00)<<8)|((word>>8)&0xff00)|(word>>24));
        }
    }
    const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(samples.data()),samples.size_bytes());
    auto hash=digest(bytes);
#ifdef _WIN32
    auto* file=_wfopen(path.c_str(),L"wbx");
#else
    auto* file=std::fopen(path.c_str(),"wbx");
#endif
    if(!file)throw Error("cannot exclusively create retained Fast capture");
    const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    const bool closed=std::fclose(file)==0;
    if(!written || !closed) {
        std::error_code ignored;
        std::filesystem::remove(path,ignored);
        throw Error("cannot write retained Fast capture");
    }
    return hash;
}

struct Run {
    std::string end_reason, error, audio_error;
    std::optional<audio::StreamFormat> capture_format, playback_format;
    std::optional<ProcessMemory> audio_ready_memory;
    std::size_t fifo_bytes = 0, fifo_capacity_samples = 0, maximum_backlog_samples = 0;
    std::size_t capture_pending_at_stop = 0;
    std::optional<std::uint64_t> failed_input_first_sample;
    std::size_t failed_input_samples = 0;
    std::optional<RetainedCapture> retained_capture;
    std::string_view failed_receiver_stage;
};

bool receive_block(Probe& probe,std::span<const float> pcm,Run& run) {
    try {probe.consume(pcm);return true;}
    catch(const std::exception& error) {
        probe.record_memory("receiver_failure");
        run.failed_input_first_sample=probe.samples;
        run.failed_input_samples=pcm.size();
        run.failed_receiver_stage=probe.receiver_stage;
        run.end_reason="receiver_error";
        run.error=error.what();
        return false;
    }
}

Run offline(Probe& probe, Clock::time_point deadline, std::uint64_t limit) {
    Run run;
    std::array<float, block_samples> pcm{};
    while (probe.samples < probe.input_limit(limit) && Clock::now() < deadline) {
        const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(), probe.input_limit(limit) - probe.samples));
        const auto before_quiet = probe.channel.report().quiet_start_sample.has_value();
        const auto n = probe.produce(std::span(pcm).first(request));
        if (!n) { run.end_reason = "simulated_channel_eof"; return run; }
        if(!receive_block(probe,std::span(pcm).first(n),run))return run;
        if(const auto reason=probe.bootstrap_end_reason()) { run.end_reason=reason; return run; }
        if (probe.progress().physical_complete) {
            if(probe.bootstrap_pending()) { run.end_reason="bootstrap_missing"; return run; }
            bool after_tail=before_quiet;
            if (probe.resilient()) {
                const auto complete=probe.completion_tick();
                const auto quiet=probe.channel.report().quiet_start_sample;
                after_tail=complete && quiet && *complete>=*quiet &&
                    *complete-*quiet>=std::uint64_t{6}*probe.options.profile.sample_rate;
            }
            run.end_reason = after_tail ? "physical_completion" : "early_physical_completion";
            return run;
        }
    }
    run.end_reason = probe.input_limit_reason(limit);
    return run;
}

Run live(Probe& probe, Clock::time_point deadline, std::uint64_t limit) {
    const auto& o=probe.options;
    Run run;
    if(!o.retain_capture.empty() && std::filesystem::exists(o.retain_capture))
        throw Error("retained Fast capture output already exists");
    std::vector<float> retained_pcm(o.retain_capture.empty()?0:retained_capture_samples);
    if(!retained_pcm.empty()) {
        run.retained_capture.emplace();
        run.retained_capture->workspace_bytes=retained_pcm.capacity()*sizeof(float);
    }
    namespace native=datapump::fast::detail;
    const native::CableOptions options{o.profile,o.capture_device,o.playback_device,audio::ChannelMode::left_mono,
        o.tx_gain,native::cable_upper_hz(o.profile,probe.channel.report()),false,limit,deadline};
    native::CableCallbacks callbacks;
    callbacks.produce=[&](std::span<float> pcm) {
        const auto count=probe.produce(pcm);
        return native::CableProduced{count,probe.channel.report().quiet_start_sample.has_value()};
    };
    callbacks.consume=[&](std::span<const float> pcm,bool quiet_before) {
        const auto previous=probe.samples;
        if(receive_block(probe,pcm,run)) {
            if(const auto reason=probe.bootstrap_end_reason())run.end_reason=reason;
            else if(probe.progress().physical_complete)
                run.end_reason=probe.bootstrap_pending()?"bootstrap_missing":
                    quiet_before?"physical_completion":"early_physical_completion";
        }
        return native::CableConsumed{static_cast<std::size_t>(probe.samples-previous),!run.end_reason.empty()};
    };
    callbacks.input_limit=[&]{return probe.input_limit(limit);};
    if(run.retained_capture)callbacks.capture_observer=[&](std::span<const float> pcm) {
        auto& receipt=*run.retained_capture;
        const auto output=std::span(retained_pcm).subspan(receipt.recorded_samples);
        const auto count=std::min(output.size(),pcm.size());
        std::ranges::copy(pcm.first(count),output.begin());
        receipt.recorded_samples+=count;
        receipt.observed_samples+=pcm.size();
        return true;
    };
    callbacks.playback_ready=[&]{run.audio_ready_memory=process_memory();};
    const auto transport=native::run_cable_loopback(options,callbacks,probe.receiver_stop);
    run.maximum_backlog_samples=transport.peak_samples;
    run.capture_pending_at_stop=transport.pending_samples;
    run.fifo_capacity_samples=transport.capacity_samples;
    run.fifo_bytes=transport.workspace_bytes;
    run.capture_format=transport.capture_format;
    run.playback_format=transport.playback_format;
    run.audio_error=transport.error;
    if(run.error.empty()) {
        if(!run.audio_error.empty()) {
            run.error=run.audio_error;
            run.end_reason="audio_error";
        } else if(transport.end!=native::CableEnd::receiver) {
            run.end_reason=transport.end==native::CableEnd::audio_limit?probe.input_limit_reason(limit):
                std::string(native::cable_end_name(transport.end));
        }
    }
    if(run.retained_capture) {
        auto& receipt=*run.retained_capture;
        receipt.discontinuity_reported=transport.capture_discontinuity;
        auto samples=std::span(retained_pcm).first(receipt.recorded_samples);
        receipt.levels.add(samples);
        try {
            if(samples.empty())throw Error("no audio samples were available to retain");
            receipt.sha256=save_capture(o.retain_capture,samples);
        } catch(const std::exception& error) {
            receipt.error=error.what();
            if(run.error.empty()) {
                run.error=receipt.error;
                run.end_reason="capture_retention_error";
            }
        }
    }
    return run;
}

void write_audio_format(const std::optional<audio::StreamFormat>& format) {
    if (!format) { std::cout << "null"; return; }
    std::cout << "{\"logical_rate\":" << format->logical_rate << ",\"hardware_rate\":" << format->hardware_rate
              << ",\"usable_passband_hz\":" << format->usable_passband_hz << ",\"workspace_bytes\":" << format->workspace_bytes
              << ",\"hardware_channels\":" << format->hardware_channels << ",\"selected_capture_channel\":";
    optional_json(format->selected_capture_channel);
    std::cout << '}';
}

int finish(Probe& probe, const Run& run, Clock::time_point start, std::clock_t cpu_start) {
    const auto& o = probe.options;
    const auto& p = o.profile;
    const auto& c = o.channel;
    const auto& cr = probe.channel.report();
    probe.record_memory("transport_stopped");
    const bool physical = run.end_reason == "physical_completion" && !probe.bootstrap_pending();
    const auto finish_started = Clock::now();
    probe.receiver_work_started=finish_started;
    probe.receiver.finish();
    probe.decoder.finish(physical);
    probe.record_memory("after_decoder_finish");
    probe.receiver_wall += elapsed(finish_started);
    probe.receiver_work_started.reset();
    const auto progress = probe.progress();
    const auto d = probe.decoder.snapshot();
    const auto result = probe.decoder.result();
    const auto exact = probe.exact();
    const bool success = physical && d.complete && exact && (!o.encryption || d.authenticated);
    const bool bootstrap_success=o.bootstrap_only && run.end_reason=="bootstrap_verified" &&
        probe.bootstrap_passed() && !probe.digital.nonfinite && !probe.captured.nonfinite;
    std::string status = run.end_reason;
    if (success) status = "complete";
    else if(run.end_reason=="bootstrap_verified" && !bootstrap_success)status="bootstrap_invalid_samples";
    else if (physical) status = d.complete && !exact ? "payload_mismatch" : "decode_failed";
    else if (run.end_reason == "simulated_channel_eof")
        status = progress.acquired ? "physical_end_missing" : "no_acquisition";
    const double wall = elapsed(start);
    const double cpu = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    const double sampled = static_cast<double>(probe.samples) / p.sample_rate;
    const auto bulk = estimate_transmission(p, o.encryption, large_estimate_bytes);
    std::cout << std::setprecision(16) << std::boolalpha
        << "{\"schema\":\"fast_channel_probe.v1\",\"status\":" << json_string(status)
        << ",\"end_reason\":" << json_string(run.end_reason) << ",\"live\":" << o.live
        << ",\"receiver_backend\":" << json_string(probe.receiver.backend_name())
        << ",\"encrypted\":" << o.encryption << ",\"authenticated\":" << d.authenticated
        << ",\"complete\":" << d.complete << ",\"physical_complete\":" << progress.physical_complete
        << ",\"bootstrap_gate\":" << o.bootstrap_gate << ",\"bootstrap_only\":" << o.bootstrap_only
        << ",\"bootstrap_checked\":" << probe.bootstrap_checked
        << ",\"bootstrap_validated\":" << probe.bootstrap_validated
        << ",\"bootstrap_nonfinite_llrs\":" << probe.bootstrap_nonfinite_llrs
        << ",\"bootstrap_input_limit_samples\":" << probe.bootstrap_limit_samples
        << ",\"exact_bytes\":" << exact << ",\"source_bytes\":" << probe.source.size()
        << ",\"received_bytes\":" << (result ? result->size() : 0)
        << ",\"source_hash\":" << json_string(digest(probe.source))
        << ",\"received_hash\":" << json_string(result ? digest(result->bytes()) : "")
        << ",\"encoded_source_bytes\":" << probe.encoded_bytes << ",\"encoded_source_hash\":" << json_string(probe.encoded_hash)
        << ",\"source_encoding\":\"xz-attachment\",\"seed\":" << cr.seed
        << ",\"source_samples\":" << cr.source_samples << ",\"output_samples\":" << cr.output_samples
        << ",\"quiet_start_sample\":";
    optional_json(cr.quiet_start_sample);
    std::cout << ",\"physical_completion_input_tick\":";
    optional_json(probe.completion_tick());
    std::cout << ",\"observed_quiet_after_tail_seconds\":";
    std::optional<double> quiet_seconds;
    if (!o.live && probe.completion_tick() && cr.quiet_start_sample &&
        *probe.completion_tick()>=*cr.quiet_start_sample)
        quiet_seconds=static_cast<double>(*probe.completion_tick()-*cr.quiet_start_sample)/p.sample_rate;
    optional_json(quiet_seconds);
    std::cout << ",\"offline_prefix_comparison\":[";
    for(std::size_t i=0;i<probe.prefix_compared;++i) {
        if(i)std::cout << ',';
        const auto& interval=probe.compared_prefix[i];
        std::cout << "{\"ordinal\":" << i << ",\"hard_sign_errors\":" << interval.hard_sign_errors
                  << ",\"neutral\":" << interval.neutral << ",\"nonfinite\":" << interval.nonfinite
                  << ",\"evm\":";
        if(std::isfinite(interval.evm))std::cout << interval.evm;
        else std::cout << "null";
        std::cout << '}';
    }
    std::cout << ']';
    if (const auto* modem=probe.resilient()) {
        const auto& acquisition=modem->acquisition_progress();
        const auto& fit=modem->last_acquisition_diagnostics();
        using FitStatus=datapump::fast::detail::SsbAcquisitionDiagnostics::Status;
        const auto fit_status=[](FitStatus status) -> std::string_view {
            switch(status) {
                case FitStatus::insufficient_support:return "insufficient_support";
                case FitStatus::no_periodic_model:return "no_periodic_model";
                case FitStatus::no_verified_marker:return "no_verified_marker";
                case FitStatus::cancelled:return "cancelled";
                case FitStatus::locked:return "locked";
            }
            return "unknown";
        };
        std::cout << ",\"acquisition_projected_windows\":" << acquisition.projected_windows
                  << ",\"acquisition_training_proposals\":" << acquisition.training_proposals
                  << ",\"acquisition_fit_requests\":" << acquisition.fit_requests
                  << ",\"acquisition_last_fit_status\":";
        if(acquisition.fit_requests)std::cout << json_string(fit_status(fit.status));
        else std::cout << "null";
        std::cout << ",\"acquisition_last_marker_decoy_evaluated\":" << fit.marker_decoy_evaluated;
        std::cout << ",\"acquisition_first_fit_attempts\":[";
        bool first_attempt=true;
        for(const auto& attempt:modem->first_acquisition_attempts()) {
            if(!first_attempt)std::cout << ',';
            first_attempt=false;
            std::cout << "{\"training_first_center_tick\":" << attempt.training_first_center_tick
                      << ",\"observed_past_last_center_tick\":" << attempt.observed_past_last_center_tick
                      << ",\"earliest_epoch_from_training_ticks\":" << attempt.earliest_epoch_from_training_ticks
                      << ",\"latest_epoch_from_training_ticks\":" << attempt.latest_epoch_from_training_ticks
                      << ",\"wall_seconds\":" << attempt.wall_seconds
                      << ",\"training_nmse\":" << attempt.training_nmse
                      << ",\"status\":" << json_string(fit_status(attempt.status))
                      << ",\"marker_evaluations\":" << attempt.marker_evaluations
                      << ",\"marker_primary_rejections\":" << attempt.marker_primary_rejections
                      << ",\"marker_decoy_banks\":" << attempt.marker_decoy_banks
                      << ",\"marker_primary_seconds\":" << attempt.marker_primary_seconds
                      << ",\"marker_decoy_seconds\":" << attempt.marker_decoy_seconds
                      << ",\"marker_row_seconds\":" << attempt.marker_row_seconds
                      << ",\"marker_gram_seconds\":" << attempt.marker_gram_seconds
                      << ",\"marker_factor_seconds\":" << attempt.marker_factor_seconds << '}';
        }
        std::cout << ']';
        const auto& selection=modem->model_progress();
        std::cout << ",\"resilient_model\":";
        if(selection.selected)std::cout << json_string(
            *selection.selected==datapump::fast::detail::SsbReceiverModel::local_fir ?
                "local-fir" : "sparse-delay-doppler");
        else std::cout << "null";
        std::cout << ",\"local_fir_eligible\":" << selection.local_eligible
                  << ",\"local_fir_tested\":" << selection.local_tested
                  << ",\"local_fir_admission_present\":" << selection.local_admission_present
                  << ",\"local_fir_prospective_present\":" << selection.local_prospective_present
                  << ",\"local_fir_admission_nmse\":";
        optional_json(selection.local_admission_nmse);
        std::cout << ",\"local_fir_prospective_nmse\":";
        optional_json(selection.local_prospective_nmse);
        std::cout << ",\"local_fir_pooled_nmse\":";
        optional_json(selection.local_pooled_nmse);
        const auto& refinement=modem->refinement_progress();
        std::cout << ",\"cycle_refinement_attempted\":" << refinement.attempted
                  << ",\"cycle_refinement_accepted\":" << refinement.accepted
                  << ",\"cycle_refinement_rate_updates\":" << refinement.rate_updates
                  << ",\"cycle_refinement_phase_updates\":" << refinement.phase_updates
                  << ",\"first_accepted_refinement_wire_ordinal\":";
        optional_json(refinement.first_accepted_wire_ordinal);
        std::cout << ",\"first_phase_update_wire_ordinal\":";
        optional_json(refinement.first_phase_update_wire_ordinal);
        const auto& quiet=modem->quiet_progress();
        const auto write_range=[](const auto& range) {
            if(range)std::cout << '[' << range->first << ',' << range->past_last << ']';
            else std::cout << "null";
        };
        const auto write_scores=[&](std::string_view name,const auto& score) {
            std::cout << ',' << json_string(name) << ":{\"units\":" << score.units << ",\"last_input_ticks\":";
            write_range(score.last);
            std::cout << ",\"last_groups\":" << score.last_groups
                      << ",\"last_observable_groups\":" << score.last_observable_groups
                      << ",\"last_squared_coherence\":" << score.last_squared_coherence
                      << ",\"last_projected_to_raw_energy\":" << score.last_projected_to_raw_energy << '}';
        };
        std::cout << ",\"quiet_progress\":{\"longest_ticks\":" << quiet.longest_quiet_ticks
                  << ",\"no_detection_unobservable_units\":" << quiet.no_detection_unobservable_units
                  << ",\"unscored_payload_intervals\":" << quiet.unscored_payload_intervals
                  << ",\"low_power_calibrated_units\":" << quiet.low_power_calibrated_units
                  << ",\"low_power_coherent_veto_units\":" << quiet.low_power_coherent_veto_units
                  << ",\"low_power_ceiling\":";
        optional_json(quiet.low_power_ceiling);
        std::cout
                  << ",\"current_input_ticks\":";
        write_range(quiet.current_quiet_run);
        std::cout << ",\"pending_input_ticks\":";
        write_range(quiet.pending_observation);
        std::cout << ",\"last_predictive_payload_input_ticks\":";
        write_range(quiet.last_predictive_payload);
        std::cout << ",\"eligible_after_input_tick\":";
        optional_json(quiet.quiet_eligible_after_tick);
        write_scores("absent",quiet.absent);
        write_scores("present",quiet.present);
        write_scores("no_detection",quiet.no_detection);
        write_scores("unscored",quiet.unscored);
        std::cout << '}';
    }
    std::cout << ",\"sampled_seconds\":" << sampled << ",\"wall_seconds\":" << wall
        << ",\"process_cpu_seconds\":" << cpu << ",\"receiver_wall_seconds\":" << probe.receiver_wall
        << ",\"maximum_receiver_push_wall_seconds\":" << probe.max_push_wall
        << ",\"modeled_realtime_backlog_seconds\":" << probe.modeled_backlog_seconds
        << ",\"receiver_realtime_ratio\":" << (sampled ? probe.receiver_wall / sampled : 0)
        << ",\"source_goodput_bps\":" << (success && sampled ? 8 * probe.source.size() / sampled : 0)
        << ",\"fixture_estimated_airtime_seconds\":" << probe.estimate.seconds
        << ",\"bulk_estimated_encoded_bytes\":" << large_estimate_bytes
        << ",\"bulk_estimated_encoded_bps\":" << bulk.source_bps
        << ",\"acquired\":" << progress.acquired << ",\"acquired_by_sample\":";
    optional_json(probe.acquired_samples);
    std::cout << ",\"receiver_acquired_clock_error_ppm\":";
    optional_json(progress.acquired?std::optional{progress.clock_error_ppm}:std::nullopt);
    std::cout << ",\"receiver_acquired_carrier_error_hz\":";
    optional_json(progress.acquired?std::optional{progress.carrier_error_hz}:std::nullopt);
    std::cout << ",\"intervals\":" << d.intervals << ",\"verified_bytes\":" << d.verified_bytes
        << ",\"failed_cycles\":" << d.failed_cycles << ",\"ldpc_frames\":" << d.ldpc_frames
        << ",\"ldpc_failed_frames\":" << d.ldpc_failed_frames << ",\"ldpc_iterations\":" << d.ldpc_iterations
        << ",\"dsp_workspace_bytes\":" << (probe.channel.workspace_bytes() + probe.tx.workspace_bytes() +
            probe.receiver.workspace_bytes())
        << ",\"probe_pcm_workspace_bytes\":" << (run.fifo_bytes + block_samples*sizeof(float) +
            (run.retained_capture?run.retained_capture->workspace_bytes:0))
        << ",\"decoder_memory_quota_bytes\":" << decoder_quota << ",\"peak_decoder_spool_bytes\":" << probe.peak_spool_bytes
        << ",\"maximum_capture_backlog_samples\":" << run.maximum_backlog_samples
        << ",\"capture_fifo_capacity_samples\":" << run.fifo_capacity_samples
        << ",\"capture_pending_samples_at_stop\":" << run.capture_pending_at_stop
        << ",\"digital_peak\":" << probe.digital.peak << ",\"digital_rms\":" << probe.digital.rms()
        << ",\"digital_clipped_samples\":" << probe.digital.clipped
        << ",\"digital_nonfinite_samples\":" << probe.digital.nonfinite
        << ",\"capture_peak\":" << probe.captured.peak << ",\"capture_rms\":" << probe.captured.rms()
        << ",\"capture_clipped_samples\":" << probe.captured.clipped
        << ",\"capture_nonfinite_samples\":" << probe.captured.nonfinite
        << ",\"audio_error\":" << json_string(run.audio_error)
        << ",\"retained_capture\":";
    if(const auto& capture=run.retained_capture) {
        std::cout << "{\"path\":" << json_string(o.retain_capture.string()) << ",\"format\":\"mono-f32le\""
            << ",\"sample_rate\":" << p.sample_rate << ",\"first_input_sample\":0"
            << ",\"observed_samples\":" << capture->observed_samples
            << ",\"recorded_samples\":" << capture->recorded_samples
            << ",\"saved_bytes\":" << (capture->sha256.empty()?0:capture->recorded_samples*sizeof(float))
            << ",\"workspace_bytes\":" << capture->workspace_bytes
            << ",\"discontinuity_reported\":" << capture->discontinuity_reported
            << ",\"cap_truncated\":" << (capture->recorded_samples<capture->observed_samples)
            << ",\"peak\":" << capture->levels.peak << ",\"nonfinite_samples\":" << capture->levels.nonfinite
            << ",\"clipped_samples\":" << capture->levels.clipped
            << ",\"sha256\":" << json_string(capture->sha256)
            << ",\"error\":" << json_string(capture->error) << '}';
    } else std::cout << "null";
    std::cout << ",\"receiver_failure_first_sample\":";
    optional_json(run.failed_input_first_sample);
    std::cout << ",\"receiver_failure_input_samples\":" << run.failed_input_samples
              << ",\"receiver_failure_stage\":";
    if(run.failed_receiver_stage.empty())std::cout << "null";
    else std::cout << json_string(run.failed_receiver_stage);
    std::cout << ",\"last_requested_feedback_wire_ordinal\":";
    optional_json(probe.last_requested_feedback);
    std::cout << ",\"last_validated_feedback_wire_ordinal\":";
    optional_json(probe.last_validated_feedback);
    std::cout << ",\"process_memory\":{\"baseline\":";
    memory_json(probe.baseline_memory);
    std::cout << ",\"audio_ready\":";
    memory_json(run.audio_ready_memory);
    std::cout << ",\"recent_observations\":[";
    const auto retained=std::min(probe.memory_observations,probe.memory_trace.size());
    const auto first=probe.memory_observations-retained;
    for(std::size_t i=0;i<retained;++i) {
        if(i)std::cout << ',';
        const auto& reading=probe.memory_trace[(first+i)%probe.memory_trace.size()];
        std::cout << "{\"stage\":" << json_string(reading.stage) << ",\"wire_cycle\":";
        optional_json(reading.wire_cycle);
        std::cout << ",\"receiver_consumed_input_samples\":";
        optional_json(reading.receiver_consumed_input_samples);
        std::cout << ",\"receiver_wall_seconds\":" << reading.receiver_wall_seconds << ",\"memory\":";
        memory_json(reading.memory);
        std::cout << '}';
    }
    std::cout << "]}"
        << ",\"profile\":{\"channel\":" << json_string(channel_name(p.channel))
        << ",\"identity_sha256\":" << json_string(digest(profile_id(p)))
        << ",\"overridden\":" << o.profile_override << ",\"capacity_mode\":" << p.capacity_mode
        << ",\"ofdm\":" << p.acoustic_ofdm << ",\"ofdm_fft_size\":" << p.ofdm_fft_size
        << ",\"ofdm_prefix_samples\":" << p.ofdm_prefix_samples << ",\"ofdm_pilot_stride\":" << p.ofdm_pilot_stride
        << ",\"ofdm_training_blocks\":" << p.ofdm_training_blocks << ",\"ldpc_frame_bits\":" << p.ldpc_frame_bits
        << ",\"compact_convolutional\":" << p.compact_convolutional << ",\"amplitude\":" << p.amplitude
        << ",\"sample_rate\":" << p.sample_rate << ",\"symbol_rate\":" << p.symbol_rate
        << ",\"carrier_hz\":" << p.carrier_hz << ",\"occupied_lower_hz\":" << occupied_lower_hz(p)
        << ",\"occupied_upper_hz\":" << occupied_upper_hz(p) << ",\"rolloff\":" << p.rolloff
        << ",\"gross_bitrate\":" << gross_bitrate(p) << ",\"constellation\":" << p.constellation
        << ",\"code_rate\":" << json_string(code_rate_name(p.code_rate)) << ",\"interleave_depth\":" << p.interleave_depth
        << ",\"marker_spacing_intervals\":" << p.marker_spacing_intervals << ",\"pilot_spacing_symbols\":" << p.pilot_spacing_symbols << '}'
        << ",\"channel\":{\"transmit_watts\":" << c.transmit_watts << ",\"path_loss_db\":" << c.path_loss_db
        << ",\"noise_dbm_hz\":";
    optional_json(c.noise_dbm_hz);
    std::cout << ",\"base_oscillator\":" << json_string(o.base_oscillator) << ",\"shift_oscillator\":" << json_string(o.shift_oscillator)
        << ",\"baseband_ppm\":" << c.baseband_ppm << ",\"shift_ppm\":" << c.shift_ppm
        << ",\"baseband_phase_rad_sqrt_s\":" << c.baseband_phase_rad_sqrt_s
        << ",\"shift_phase_rad_sqrt_s\":" << c.shift_phase_rad_sqrt_s
        << ",\"shared_reference\":" << c.shared_reference << ",\"shift_hz\":" << c.shift_hz
        << ",\"spread\":" << json_string(o.spread) << ",\"delay_ms\":" << c.delay_ms << ",\"doppler_hz\":" << c.doppler_hz
        << ",\"margin\":" << c.margin << ",\"selective_slow_depth\":" << c.selective_slow_depth
        << ",\"selective_fast_depth\":" << c.selective_fast_depth << ",\"received_power_dbm\":" << cr.received_power_dbm
        << ",\"cn0_db_hz\":";
    optional_json(cr.cn0_db_hz);
    std::cout << ",\"spread_delay_ms\":" << cr.spread_delay_ms << ",\"spread_doppler_hz\":" << cr.spread_doppler_hz
        << ",\"selective_spacing_hz\":" << cr.selective_spacing_hz << ",\"noise_variance\":" << cr.noise_variance
        << ",\"nominal_signal_power\":" << cr.nominal_signal_power << ",\"sample_clock_error_ppm\":" << cr.sample_clock_error_ppm
        << ",\"shift_error_hz\":" << cr.shift_error_hz << ",\"clock_bound_ppm\":" << cr.clock_bound_ppm
        << ",\"frequency_bound_hz\":" << cr.frequency_bound_hz << ",\"propagation_tail_samples\":" << cr.propagation_tail_samples << '}'
        << ",\"max_audio_seconds\":" << o.max_audio_seconds << ",\"max_wall_seconds\":" << o.max_wall_seconds
        << ",\"capture_device\":" << json_string(o.capture_device) << ",\"playback_device\":" << json_string(o.playback_device)
        << ",\"tx_gain\":" << o.tx_gain << ",\"capture_format\":";
    write_audio_format(run.capture_format);
    std::cout << ",\"playback_format\":";
    write_audio_format(run.playback_format);
    std::cout << ",\"status_text\":" << json_string(d.status) << ",\"error\":" << json_string(run.error) << "}\n";
    return success || bootstrap_success ? 0 : 2;
}
} // namespace

int main(int argc, char** argv) try {
    const auto options = parse(argc, argv);
    const auto start = Clock::now();
    const auto cpu_start = std::clock();
    const auto source = payload(options.source_bytes, options.seed);
    const auto key = test_key(options);
    const auto deadline = start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(options.max_wall_seconds));
    std::stop_source receiver_stop;
    Probe probe(options, source, key, prepare_xz_attachment(byte_source(source), filename, source_quota),
                receiver_stop);
    std::jthread watchdog;
    if (!options.live && probe.resilient()) watchdog=std::jthread([&](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::unique_lock lock(mutex);
        condition.wait_until(lock,stop,deadline,[] { return false; });
        if (!stop.stop_requested()) receiver_stop.request_stop();
    });
    const auto limit = static_cast<std::uint64_t>(options.max_audio_seconds * options.profile.sample_rate);
    const auto run = options.live ? live(probe, deadline, limit) : offline(probe, deadline, limit);
    return finish(probe, run, start, cpu_start);
} catch (const std::exception& error) {
    std::cout << "{\"schema\":\"fast_channel_probe.v1\",\"status\":\"error\",\"reason\":" << json_string(error.what())
              << ",\"process_memory_after_error\":";
    memory_json(process_memory());
    std::cout << "}\n";
    return 2;
}
