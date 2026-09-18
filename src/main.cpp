#include "datapump/audio.hpp"
#include "datapump/channel.hpp"
#include "datapump/correlation_experiment.hpp"
#include <array>
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/lpi_estimate.hpp"
#include "datapump/stream_codec.hpp"
#include "datapump/qr.hpp"
#include "datapump/runtime.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/live.hpp"
#include "datapump/streaming_modem.hpp"
#include "transmit_timing.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace datapump;
#ifndef DATAPUMP_VERSION
#define DATAPUMP_VERSION "0.7.2"
#endif
namespace {
const char* usage="Data Pump " DATAPUMP_VERSION R"HELP( — civilian audio text and file modem

Usage: pump COMMAND [OPTIONS]
  simulate     Free-running sampled channel and blind receiver acquisition
  listen       Continuous live receiver (or noise/loopback with --simulation)
  estimate     Exact airtime and advisory LPI model (assumes TX target C/N0)
  analyze-link Bounded statistical link analysis as JSON; no PCM or decoder
  tx           Encode text/file to WAV (--output) or live audio (--device)
  rx           Decode a WAV (--input) or record live audio (--device --seconds)
  keygen       Create an owner-only 128 MiB symmetric keyfile (--output)
  keys         List the named key sets in --keyfile
  devices      Enumerate local audio devices
  status-tx    Transmit exact few-bit callsign to WAV, without stream overhead
  status-rx    Discover raw pattern bits in WAV, then compare --bits; no MAC
  qr           Generate optical transfer QR Level L (--format svg|pbm)

Input/output:
  --text TEXT           Text to send (otherwise --input FILE or - for stdin)
  --input PATH          TX file or RX WAV (- means stdin)
  --kind text|file|screenshot  Default: text with --text/stdin, file with path
  --filename NAME       Display filename for file/screenshot (basename only)
  --output PATH         Explicit WAV/keyfile output; never overwrite
  --save PATH           Explicit save of received content or raw 0/1 text; never overwrite
  --json                Received content as JSON with base64 payload and diagnostics
  --callsign TEXT --grid TEXT --repeatable

Modem:
  --bw HZ               Nominal bandwidth, 0.01..30000000 Hz; default1200
                        Decimal Hz and units such as 1.2kHz are accepted
  --sample-rate HZ      Internal DSP clock, 64..120000000; default max(6000,4*bw)
  --carrier HZ          Default max(1500,0.75*bw); explicit overrides stay available
  --spreading N         Manual 4-bit APSK chips/symbol, 1..16384 (disables auto)
  --target-snr DBHZ     Automatic target C/N0; default32, auto unless manual controls
  --receive-targets LIST Receive C/N0 search list, e.g. 40,6,-6; default32
                        Invalid lists reset to32; selected bandwidth/pattern stay fixed
  --pattern MODE        auto-keystream, auto-pattern, auto-tone, pattern-N, tone-N
  --scramble            Cryptographic pattern rotation (requires keyfile)
  --dsss                Independent encrypted direct-sequence spreading
  --fec 20|60|off        Reed-Solomon parity overhead, default60
  --no-compression      Diagnostic override; both peers must select the same source codec
  --memory-mb N         Legacy batch PCM workspace budget, default256 MiB
  --cache-mb N          Received content/input limit, default256 MiB
  --dsp-mb N            Independent streaming DSP workspace, default64 MiB
  --keyfile PATH        Symmetric keyfile; independent data and pattern keystreams
  --key-name NAME       Select a named key set (default: first)
  --key-names A,B,C     Names to create with keygen (default: Default)
  --pad PATH            Required external >1GiB pad when bound to keyfile
  --time SECONDS        Local epoch; default current UNIX second
  --search-seconds N    RX epoch trials ±N seconds, nearest first; default6
  --recovery-seconds N  Post-end hard-bit recovery budget, default300; 0 disables
  --recovery-threads N  Recovery workers, default0 uses available CPU cores
  --recovery-bits N     Separate retained hard-bit slots, default65536
  --recovery-errors N   Reserve RS capacity for additional byte errors, default2
  --progress            Emit timing-search progress to stderr

Audio/simulation:
  --device ID           OS audio endpoint; listen automatically uses default
  --no-mono             TX on both stereo channels; default right only (mono devices use their sole channel)
  --device-type audio   Analog audio input only; no network/raw serial input
  --seconds N           RX recording duration(default15); listen limit(default0)
  --tx-delay N          Delay after live TX completes; default6 seconds
  --snr DB              Simulator measured signal/noise power ratio; default20
  --simulation PRESET   e.g. "3dBm -120dB": TX power and channel attenuation
  --seed N --delay-samples N --frequency-offset HZ
  --oscillator MODEL    crystal (default), gpsdo-xo, gpsdo-tcxo, gpsdo-ocxo
                        Illustrative residual clock/phase models; no hardware control
  --clock-error-ppm N   Override model's relative clock error; crystal default100
  --phase-noise N       Override diffusion, degrees/sqrt(second); crystal default0.5
  --receiver-time N     Independent receive epoch for simulate (default --time)

Statistical link analysis (analyze-link only):
  --tx-dbm N --attenuation-db N  Custom link; attenuation is nonpositive
                        Both are required unless --simulation selects a preset
  --noise-figure-db N   Receiver noise figure; default10 dB
  --symbol-seconds N    Override analyzed symbol duration; otherwise use TX plan
  --coherent-seconds N  Maximum coherent segment; default3600 seconds
  --trials N           Monte Carlo trials per reference model; 1..1000000, default10000
  --hypotheses N        Total searched alternatives, including both bits; default1000000
  --false-alarm P       Global reference false-alarm budget; default0.000001
  --residual-frequency-hz N  Error after hypothetical acquisition; default0
  --template-correlation N   Assumed normalized template correlation; default0
                        Uses --text, --input or exact --bits; never decodes them
                        Without --receive-targets, current RX is assumed to match TX

Very slow status:
  --bits 010            Exact known callsign bits (1..4096), no MAC or FEC
  --format svg|pbm      QR output format; default svg; up to500 characters
  --help --version

Examples:
  pump simulate --text "CQ hello" --snr 12 --json
  pump tx --input screenshot.png --kind screenshot --output transfer.wav
  pump rx --input transfer.wav --save received.png
  pump tx --text "hello" --device default
  pump rx --device default --seconds 30 --json
  pump analyze-link --text a --bw 100 --target-snr -36 --tx-dbm 3 --attenuation-db -200
GUI: datapump-gui
)HELP";

class Args {
    std::map<std::string,std::string> values_;
public:
    std::string command;
    Args(int argc,char** argv) {
        const std::set<std::string> booleans={"json","repeatable","no-compression","no-mono","scramble","dsss","progress","help","version"};
        const std::set<std::string> valued={"text","input","output","save","kind","filename","callsign","grid",
            "bw","sample-rate","carrier","spreading","fec","memory-mb","keyfile","pad","time","search-seconds",
            "device","device-type","seconds","tx-delay","snr","seed","delay-samples","frequency-offset","bits","format",
            "target-snr","receive-targets","pattern","simulation","oscillator","key-name","key-names","cache-mb","dsp-mb","clock-error-ppm","phase-noise","receiver-time",
            "recovery-seconds","recovery-threads","recovery-bits","recovery-errors",
            "tx-dbm","attenuation-db","noise-figure-db","symbol-seconds","coherent-seconds",
            "trials","hypotheses","false-alarm","residual-frequency-hz","template-correlation"};
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--tx" || arg=="--rx") {if(!command.empty()) throw Error("choose one command");command=arg.substr(2);continue;}
            if(!arg.starts_with("--")) {if(!command.empty())throw Error("unexpected positional argument: "+arg);command=arg;continue;}
            arg=arg.substr(2);auto equal=arg.find('=');
            auto name=arg.substr(0,equal);
            if(name=="keyfile_symmetric") name="keyfile";
            if(values_.contains(name)) throw Error("duplicate option: --"+name);
            if(booleans.contains(name)) {
                if(equal!=std::string::npos) throw Error("flag does not take a value: --"+name);
                values_[name]="true";
            } else if(valued.contains(name)) {
                if(equal!=std::string::npos) values_[name]=arg.substr(equal+1);
                else {if(++i>=argc || std::string_view(argv[i]).starts_with("--")) throw Error("missing value for --"+name);values_[name]=argv[i];}
            } else throw Error("unknown option: --"+name);
        }
    }
    bool has(const std::string& name)const{return values_.contains(name);}
    std::string get(const std::string& name,std::string fallback="")const {
        auto p=values_.find(name);return p==values_.end()?fallback:p->second;
    }
    double number(const std::string& name,double fallback) const {
        if(!has(name)) return fallback;
        const auto value=get(name);std::size_t end=0;double result;
        try {result=std::stod(value,&end);} catch(...) {throw Error("invalid number for --"+name);}
        double scale=1;
        auto suffix=value.substr(end);
        if(name=="bw" && (suffix=="kHz" || suffix=="k")) scale=1000;
        else if(name=="bw" && suffix=="MHz") scale=1000000;
        else if(!suffix.empty() && !(name=="bw" && suffix=="Hz")) throw Error("invalid number for --"+name);
        if(!std::isfinite(result*scale)) throw Error("nonfinite number for --"+name);
        return result*scale;
    }
    std::uint64_t integer(const std::string& name,std::uint64_t fallback)const {
        if(!has(name)) return fallback;
        const auto value=get(name);std::uint64_t result=0;
        auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);
        if(error!=std::errc{} || end!=value.data()+value.size()) throw Error("invalid nonnegative integer for --"+name);
        return result;
    }
    void validate_options() const {
        const auto reject=[this](std::initializer_list<const char*> names,const std::string& reason) {
            for(const auto name:names) if(has(name)) throw Error("--"+std::string(name)+" "+reason);
        };
        if(command=="qr") reject({"keyfile","key-name","pad","scramble","dsss","repeatable","device"},"cannot be used with QR; QR contains plaintext input");
        if(command=="rx" || command=="status-rx") {
            reject({"output"},"is not a receive output; use --save PATH for decoded source bytes");
            reject({"text"},"is a transmit input; use --input for reception");
        }
        if(command!="rx") reject({"save"},"is only valid for rx");
        if(command!="tx" && command!="rx" && command!="status-tx" && command!="listen") reject({"device"},"is only valid for live audio commands");
        if(command!="keygen") reject({"key-names"},"is only valid for keygen");
        if(command!="simulate" && command!="listen" && command!="analyze-link") reject({"simulation"},"is only valid for simulate/listen/analyze-link");
        if(command!="simulate" && command!="listen" && command!="analyze-link") reject({"oscillator","clock-error-ppm","phase-noise"},"is only valid for simulate/listen/analyze-link");
        if(command!="analyze-link")
            reject({"tx-dbm","attenuation-db","noise-figure-db","symbol-seconds","coherent-seconds","trials",
                "hypotheses","false-alarm","residual-frequency-hz","template-correlation"},"is only valid for analyze-link");
        if(command=="analyze-link") {
            reject({"snr","output","tx-delay","seconds","format"},"is not an analyze-link option");
            if(!has("bits") && !has("text") && !has("input"))
                throw Error("analyze-link requires --text, --input or --bits");
            if(has("bits"))reject({"text","input","kind","filename","callsign","grid","repeatable"},"cannot accompany exact --bits");
            if(has("simulation"))reject({"tx-dbm","attenuation-db"},"cannot accompany a simulation preset");
            else if(!has("tx-dbm") || !has("attenuation-db"))
                throw Error("analyze-link requires both --tx-dbm and --attenuation-db, or --simulation");
        }
        if(command!="simulate") reject({"receiver-time"},"is only valid for simulate");
        if(command!="rx" && command!="simulate" && command!="listen")
            reject({"recovery-seconds","recovery-threads","recovery-bits","recovery-errors"},"is only valid for rx/simulate/listen");
        if(command=="listen") reject({"snr","frequency-offset","delay-samples","output","tx-delay"},"is not a listen option; choose a simulation preset for its continuous channel");
        if(command=="tx" && has("output") && has("device")) throw Error("choose one TX destination: --output or --device");
    }
};
tuning::OscillatorPreset oscillator_config(const Args& a) {
    auto result=tuning::parse_oscillator_preset(a.get("oscillator","crystal"));
    result.clock_error_ppm=a.number("clock-error-ppm",result.clock_error_ppm);
    result.phase_noise_degrees_per_sqrt_second=a.number("phase-noise",result.phase_noise_degrees_per_sqrt_second);
    return result;
}
bool stdout_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout))!=0;
#else
    return isatty(fileno(stdout))!=0;
#endif
}
std::size_t budget(const Args& a) {
    auto mb=a.integer("memory-mb",256);
    if(mb==0 || mb>4096) throw Error("memory-mb must be 1..4096");
    return static_cast<std::size_t>(mb*1024*1024);
}
std::size_t content_budget(const Args& a) {
    const auto mb=a.integer("cache-mb",256);
    if(!mb || mb>4096)throw Error("cache-mb must be 1..4096");
    return static_cast<std::size_t>(mb*1024*1024);
}
std::size_t dsp_budget(const Args& a) {
    const auto mb=a.integer("dsp-mb",64);
    if(!mb || mb>1024)throw Error("dsp-mb must be 1..1024");
    return static_cast<std::size_t>(mb*1024*1024);
}
bool automatic_tuning(const Args& a) {
    const bool manual=a.has("spreading") || a.has("scramble") || a.has("dsss") ||
                      a.has("sample-rate") || a.has("carrier");
    return a.has("target-snr") || a.has("pattern") || !manual;
}
modem::Config config(const Args& a) {
    modem::Config c;
    c.bandwidth_hz=a.number("bw",1200);
    const bool automatic=automatic_tuning(a);
    if(automatic) {
        if(a.has("spreading") || a.has("scramble") || a.has("dsss") || a.has("sample-rate") || a.has("carrier"))
            throw Error("automatic tuning cannot be combined with manual spreading/scramble/dsss/sample-rate/carrier");
        const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",32),
            tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"));
        c=plan.config;
        if(a.has("progress") || !plan.target_supported) std::cerr<<plan.explanation<<'\n';
    }
    auto rate=a.integer("sample-rate",automatic?c.sample_rate:tuning::recommended_sample_rate(c.bandwidth_hz));
    if(rate>120000000) throw Error("internal sample rate exceeds120000000");
    c.sample_rate=static_cast<std::uint32_t>(rate);
    c.carrier_hz=a.number("carrier",automatic?c.carrier_hz:tuning::recommended_carrier_hz(c.bandwidth_hz));
    auto spreading=a.integer("spreading",c.spreading_factor);
    if(spreading==0 || spreading>16384) throw Error("spreading must be1..16384");
    c.spreading_factor=static_cast<unsigned>(spreading);
    c.memory_limit=budget(a);
    if(!automatic) {c.scramble=a.has("scramble");c.dsss=a.has("dsss");}
    if((a.has("scramble") || a.has("dsss")) && !a.has("keyfile")) throw Error("encrypted spreading requires --keyfile");
    if(a.get("device-type","audio")!="audio") throw Error("this build supports analog audio only; SDR and IC-7100 frontends are not implemented");
    if(a.has("device") && c.bandwidth_hz>192000)
        throw Error("this bandwidth requires an SDR frontend; use simulation in this audio-only build");
    modem::validate(c);return c;
}
audio::StreamFormatCallback audio_passband_guard(const modem::Config& config) {
    const auto upper_edge=config.carrier_hz+config.bandwidth_hz/2;
    return [upper_edge](const audio::StreamFormat& format) {
        if(upper_edge>format.usable_passband_hz)
            throw Error("Selected upper band edge ("+std::to_string(upper_edge)+
                        " Hz) exceeds this audio path's usable passband ("+
                        std::to_string(format.usable_passband_hz)+
                        " Hz). Choose a narrower band, a wider audio device, or simulation.");
    };
}
template<class Factory>
void play_transmission(const Args& a, transfer::Options& options, Factory make) {
    std::unique_ptr<modem::StreamingTransmitter> source;
    if(a.has("time"))source=make(options);
    audio::playback(options.modem.sample_rate,a.get("device"),[&](std::span<float> chunk) {
        return source->read(chunk);
    },{},[&](const auto& format) {
        audio_passband_guard(options.modem)(format);
        if(a.has("time"))return;
        const auto clock=[] {return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
        options.modem.stream_phase_samples=0;
        auto scheduled=detail::schedule_transmission(options.modem,[&](std::uint64_t epoch) {
            options.timestamp=epoch;return make(options);
        },clock);
        source=std::move(scheduled.transmitter);
        detail::wait_for_playback(scheduled.playback_epoch,clock);
    },!a.has("no-mono"));
}
std::uint64_t epoch(const Args& a) {
    return a.integer("time",static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
}
std::optional<Crypto> key(const Args& a) {
    if(!a.has("keyfile")) {if(a.has("pad") || a.has("key-name")) throw Error("--pad/--key-name requires --keyfile");return std::nullopt;}
    auto entries=load_keyring(a.get("keyfile"),a.has("pad")?std::optional<std::filesystem::path>(a.get("pad")):std::nullopt);
    if(!a.has("key-name")) return entries.front().key;
    for(const auto& entry:entries) if(entry.name==a.get("key-name")) return entry.key;
    throw Error("key set not found: "+a.get("key-name"));
}
transfer::Options transfer_options(const Args& a,const modem::Config& c,const std::optional<Crypto>& k,std::uint64_t timestamp) {
    transfer::Options options;
    options.modem=c;
    options.content_limit=content_budget(a);
    options.dsp_workspace_bytes=dsp_budget(a);
    options.key=k;
    options.timestamp=timestamp;
    const auto window=a.integer("search-seconds",6);
    if(window>32768) throw Error("clock drift search exceeds32768 seconds");
    options.search_seconds=static_cast<unsigned>(window);
    const auto recovery_seconds=a.integer("recovery-seconds",300);
    const auto recovery_threads=a.integer("recovery-threads",0);
    const auto recovery_bits=a.integer("recovery-bits",65536);
    const auto recovery_errors=a.integer("recovery-errors",2);
    if(recovery_seconds>86400)throw Error("recovery-seconds must be 0..86400");
    if(recovery_threads>1024)throw Error("recovery-threads must be 0..1024");
    if(recovery_bits<1024 || recovery_bits>1048576)throw Error("recovery-bits must be 1024..1048576");
    if(recovery_errors>24)throw Error("recovery-errors must be 0..24");
    options.recovery_options.enabled=recovery_seconds!=0;
    options.recovery_options.budget=std::chrono::milliseconds(recovery_seconds*1000);
    options.recovery_options.workers=static_cast<unsigned>(recovery_threads);
    options.recovery_options.retained_bits=static_cast<std::size_t>(recovery_bits);
    options.recovery_options.extra_errors=static_cast<unsigned>(recovery_errors);
    if(a.has("receive-targets") && !automatic_tuning(a))throw Error("receive-targets requires automatic tuning");
    options.automatic_receive_profiles=automatic_tuning(a);
    options.receive_pattern_mode=tuning::parse_pattern_mode(a.get("pattern",k?"auto-keystream":"auto-pattern"));
    const auto targets=tuning::parse_receive_targets(a.get("receive-targets","32"));
    options.receive_targets_db_hz=targets.values;
    if(targets.reset)std::cerr<<"Invalid receive target list; reset to 32 dB-Hz.\n";
    auto fec=a.get("fec","60");
    if(fec=="20") options.fec=FecMode::rs20;
    else if(fec=="60") options.fec=FecMode::rs60;
    else if(fec=="off") options.fec=FecMode::off;
    else throw Error("fec must be20,60,or off");
    options.compression=!a.has("no-compression");
    return options;
}
Bytes input_bytes(const Args& a) {
    auto path=a.get("input","-");
    const auto limit=content_budget(a);
    if(path=="-") return read_bounded(std::cin,limit);
    std::ifstream input(path,std::ios::binary);
    if(!input) throw Error("cannot open input: "+path);
    return read_bounded(input,limit);
}
Message message(const Args& a) {
    if(a.has("text") && a.has("input")) throw Error("choose --text or --input");
    Message m;
    if(a.has("text")) {auto text=a.get("text");m.data=Bytes(text.begin(),text.end());}
    else m.data=input_bytes(a);
    auto kind=a.get("kind",a.has("input") && a.get("input")!="-"?"file":"text");
    if(kind=="text") m.kind=MessageKind::text;
    else if(kind=="file") m.kind=MessageKind::file;
    else if(kind=="screenshot") m.kind=MessageKind::screenshot;
    else throw Error("unknown message kind");
    if(m.kind!=MessageKind::text) m.filename=a.get("filename",std::filesystem::path(a.get("input")).filename().string());
    m.callsign=a.get("callsign");m.grid=a.get("grid");m.repeatable=m.kind==MessageKind::text && a.has("repeatable");
    if(m.data.size()>content_budget(a)) throw Error("message exceeds content limit");
    return m;
}
modem::Wav input_wav(const Args& a) {
    if(a.get("input")=="-") return modem::read_wav(std::cin,budget(a));
    if(!a.has("input")) throw Error("RX requires --input WAV or --device");
    std::ifstream in(a.get("input"),std::ios::binary);
    if(!in) throw Error("cannot open input WAV");
    return modem::read_wav(in,budget(a));
}
void output_bytes(const Args& a,const Bytes& data) {
    if(a.has("output")) write_new_file(a.get("output"),data);
    else {
        std::cout.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!std::cout)throw Error("stdout write failed");
    }
}
double hardware_delay(const Args& a,const modem::Config& c) {
    const auto requested=a.number("tx-delay",6);
    if(!std::isfinite(requested) || requested<6 || requested>3600)
        throw Error("tx-delay must be 6..3600 seconds");
    return std::max(requested,static_cast<double>(modem::pattern_absence_samples(c))/c.sample_rate+1.);
}
void output_wave(const Args& a,std::vector<float> samples,const modem::Config& c,bool add_tail=true) {
    // A generated capture includes actual quiet samples for the receiver's
    // sole end rule. Reading a truncated external WAV never invents this tail.
    const auto tail=add_tail?modem::pattern_absence_samples(c)+c.sample_rate:0;
    if(tail>c.memory_limit/sizeof(float) || samples.size()>c.memory_limit/sizeof(float)-tail)
        throw Error("WAV including receive separation exceeds memory limit");
    samples.resize(samples.size()+tail,0);

    if(a.has("output")) {
        std::ostringstream wav(std::ios::out|std::ios::binary);
        modem::write_wav(wav,samples,c.sample_rate);auto bytes=wav.str();
        write_new_file(a.get("output"),std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
    }
    if(a.has("device")) {
        const auto delay=hardware_delay(a,c);
        audio::play(samples,c.sample_rate,a.get("device"),{},audio_passband_guard(c),!a.has("no-mono"));
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
    }
}
std::string id_string(const Message& m) {
    std::ostringstream id;for(auto b:m.local_id) id<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b);return id.str();
}
void report_fec_region(const FecRegionStats& stats) {
    std::cout<<"{\"received_bits\":"<<stats.received_bits<<",\"corrected_bits\":"<<stats.corrected_bits
        <<",\"missing_bits\":"<<stats.missing_bits<<",\"corrected_bytes\":"<<stats.corrected_bytes
        <<",\"erased_bytes\":"<<stats.erased_bytes<<",\"repaired_bytes\":"<<stats.repaired_bytes<<'}';
}
void report_signal_identity(const live::SignalUpdate& signal) {
    std::cout<<",\"signal_id\":"<<signal.id<<",\"revision\":"<<signal.revision<<",\"superseded_ids\":[";
    for(std::size_t i=0;i<signal.superseded_ids.size();++i)
        std::cout<<(i?",":"")<<signal.superseded_ids[i];
    std::cout<<']';
}
std::string_view recovery_state_name(transfer::RecoveryState state) {
    switch(state) {
    case transfer::RecoveryState::none:return "none";
    case transfer::RecoveryState::ready:return "ready";
    case transfer::RecoveryState::running:return "running";
    case transfer::RecoveryState::incomplete:return "incomplete";
    case transfer::RecoveryState::recovered:return "recovered";
    case transfer::RecoveryState::exhausted:return "exhausted";
    case transfer::RecoveryState::ambiguous:return "ambiguous";
    case transfer::RecoveryState::cancelled:return "cancelled";
    case transfer::RecoveryState::unavailable:return "unavailable";
    }
    return "unavailable";
}
void report_recovery_json(const transfer::RecoveryProgress& recovery) {
    std::cout<<",\"recovery\":{\"state\":\""<<recovery_state_name(recovery.state)
        <<"\",\"attempts\":"<<recovery.attempts<<",\"total\":"<<recovery.total
        <<",\"elapsed_ms\":"<<recovery.elapsed.count()<<'}';
}
void report_recovery_status(bool complete,const transfer::RecoveryProgress& recovery) {
    std::cerr<<"Reception "<<(complete?"complete":"incomplete")<<"; recovery "
        <<recovery_state_name(recovery.state)<<" ("<<recovery.attempts<<'/'<<recovery.total
        <<" attempts, "<<recovery.elapsed.count()<<" ms).\n";
    if(recovery.state==transfer::RecoveryState::incomplete || recovery.state==transfer::RecoveryState::cancelled)
        std::cerr<<"Recovery search is unfinished; no unique recovered source has been established.\n";
}
void report(const Args& a,const StreamContent& stream,const modem::Diagnostics& d={},std::uint64_t timestamp=0,
            bool content_validated=true,bool stream_complete=false,std::span<const std::uint8_t> raw_bits={},std::size_t missing_symbols=0,std::size_t observed_bits=0,std::string_view error={},bool short_text_decoded=false,const live::SignalUpdate* signal=nullptr,const transfer::RecoveryProgress* recovery=nullptr) {
    const auto& m=stream.message;
    if(a.has("save")) {
        if(!stream_complete || !(content_validated || short_text_decoded))
            throw Error("No complete decoded source to save; use --json for reception diagnostics");
        write_new_file(a.get("save"),m.data);
    }
    if(a.has("json")) {
        std::cout<<"{\"validated\":"<<(content_validated?"true":"false")<<",\"content_validated\":"<<(content_validated?"true":"false")
          <<",\"stream_complete\":"<<(stream_complete?"true":"false")
          <<",\"short_text_decoded\":"<<(short_text_decoded?"true":"false")
          <<",\"authenticated\":"<<(content_validated&&stream.authenticated?"true":"false")
          <<",\"id\":\""<<id_string(m)<<"\",\"kind\":\""<<(m.kind==MessageKind::text?"text":m.kind==MessageKind::file?"file":"screenshot")
          <<"\",\"filename\":\""<<json_escape(m.filename)<<"\",\"callsign\":\""<<json_escape(m.callsign)
          <<"\",\"grid\":\""<<json_escape(m.grid)<<"\",\"repeatable\":"<<(m.repeatable?"true":"false")
          <<",\"data_base64\":\""<<base64_encode(m.data)<<"\",\"corrected_bytes\":"<<stream.corrected_bytes
          <<",\"fec_repairs\":{\"data\":";
        report_fec_region(stream.fec_stats.data);std::cout<<",\"integrity\":";report_fec_region(stream.fec_stats.integrity);
        std::cout<<",\"parity\":";report_fec_region(stream.fec_stats.parity);std::cout<<"},\"pre_fec_accuracy\":";
        if(stream.pre_fec_accuracy)std::cout<<"{\"received_data_bits\":"<<stream.pre_fec_accuracy->received_data_bits
            <<",\"corrected_data_bits\":"<<stream.pre_fec_accuracy->corrected_data_bits
            <<",\"missing_data_bits\":"<<stream.pre_fec_accuracy->missing_data_bits<<'}';else std::cout<<"null";
        std::cout<<",\"timestamp\":"<<timestamp<<",\"raw_bits\":\"";
        for(auto bit:raw_bits)std::cout<<(bit?'1':'0');
        std::cout<<"\",\"raw_bit_count\":"<<raw_bits.size()<<",\"missing_symbols\":"<<missing_symbols
          <<",\"observed_bit_count\":"<<observed_bits<<",\"error\":\""<<json_escape(std::string(error))<<"\""
          <<",\"diagnostics\":{\"sample_offset\":"<<d.sample_offset
          <<",\"correlation\":"<<d.preamble_correlation<<",\"snr_db\":";
        if(d.pattern_score || !std::isfinite(d.snr_db))std::cout<<"null";else std::cout<<d.snr_db;
        std::cout<<",\"pattern_score\":";
        if(d.pattern_score && std::isfinite(*d.pattern_score))std::cout<<*d.pattern_score;else std::cout<<"null";
        std::cout<<",\"pattern_score_units\":\"model log evidence\",\"bit_rate\":"<<d.bit_rate<<",\"waveform\":[";
        for(std::size_t i=0;i<d.waveform.size();++i) std::cout<<(i?",":"")<<d.waveform[i];
        std::cout<<"],\"constellation\":[";
        for(std::size_t i=0;i<d.constellation.size();++i) std::cout<<(i?",":"")<<'['<<d.constellation[i].real()<<','<<d.constellation[i].imag()<<']';
        std::cout<<"]}";
        if(recovery)report_recovery_json(*recovery);
        if(signal)report_signal_identity(*signal);
        std::cout<<"}\n";
    } else if(!a.has("save")) {
        if(!content_validated && m.data.empty() && !raw_bits.empty()) {
            for(auto bit:raw_bits)std::cout<<(bit?'1':'0');
            std::cout<<'\n';
        } else if(m.kind!=MessageKind::text) {
            std::cerr<<"Validated file: "<<m.filename<<" ("<<m.data.size()<<" bytes). Use --save PATH or --json to retrieve.\n";
        } else if(stdout_terminal()) std::cout<<terminal_text(m.data);
        else std::cout.write(reinterpret_cast<const char*>(m.data.data()),static_cast<std::streamsize>(m.data.size()));
    }
    if(!a.has("json") && !content_validated) {
        if(!stream_complete)std::cerr<<"Incomplete capture: physical symbol absence has not completed the stream.\n";
        if(!error.empty())std::cerr<<error<<'\n';
        if(observed_bits>raw_bits.size())std::cerr<<"Showing only the first "<<raw_bits.size()<<" of "<<observed_bits<<" observed symbol slots.\n";
    }
    if(missing_symbols && !a.has("json"))
        std::cerr<<"Raw bits include "<<missing_symbols<<(missing_symbols==1?" zero placeholder for a missing symbol.\n":" zero placeholders for missing symbols.\n");
    if(recovery && recovery->state!=transfer::RecoveryState::none && !a.has("json"))
        report_recovery_status(stream_complete,*recovery);
}
void report_received(const Args& a,const transfer::Received& received,const live::SignalUpdate* signal=nullptr) {
    report(a,received.content,received.diagnostics,received.timestamp,received.content_validated,received.stream_complete,received.raw_bits,received.missing_symbols,received.observed_bits,received.error,received.short_text_decoded,signal,&received.recovery_progress);
}
Bytes status_bits(const Args& a,const std::optional<Crypto>& k,std::uint64_t time) {
    auto input=a.get("bits");if(input.empty() || input.size()>4096) throw Error("status requires1..4096 known binary --bits");
    Bytes bits;
    for(char c:input) {if(c!='0' && c!='1')throw Error("status bits must contain only0 and1");bits.push_back(static_cast<std::uint8_t>(c-'0'));}
    if(k) {auto stream=k->stream(StreamPurpose::Data,time,0,(bits.size()+7)/8);for(std::size_t i=0;i<bits.size();++i)bits[i]^=static_cast<std::uint8_t>((stream[i/8]>>(7-i%8))&1);}
    return bits;
}
void json_number(long double value) {
    if(std::isfinite(value))std::cout<<value;else std::cout<<"null";
}
void report_lpi(const transfer::Estimate& transmission,const transfer::Options& options,
                double cn0_db_hz,bool simulated) {
    const auto model=lpi::estimate(transmission,options,cn0_db_hz);
    const bool available=model.status==lpi::Status::available;
    const char* status=available?"available":model.status==lpi::Status::public_waveform?"public_waveform":
        model.status==lpi::Status::outside_weak_signal_model?"outside_weak_signal_model":"numeric_limit";
    std::cout<<"{\"model\":\"ideal_weak_signal_radiometer\",\"status\":\""<<status
        <<"\",\"cn0_basis\":\""<<(simulated?"simulated_link":"assumed_tx_target")
        <<"\",\"equal_received_cn0\":true,\"known_band_window_and_noise\":true"
        <<",\"safe_traffic_limit\":false,\"detection_probability\":"<<lpi::detection_probability
        <<",\"false_alarm_probability_per_window\":"<<lpi::false_alarm_probability
        <<",\"cn0_db_hz\":";json_number(model.cn0_db_hz);
    std::cout<<",\"observation_bandwidth_hz\":";json_number(model.observation_bandwidth_hz);
    std::cout<<",\"in_band_snr_db\":";json_number(model.in_band_snr_db);
    std::cout<<",\"noise_rise_db\":";json_number(model.noise_rise_db);
    std::cout<<",\"symbol_seconds\":";json_number(model.symbol_seconds);
    std::cout<<",\"detection_seconds\":";
    if(available)json_number(model.detection_seconds);else std::cout<<"null";
    std::cout<<",\"equivalent_wire_symbols\":";
    if(available)json_number(model.equivalent_symbols);else std::cout<<"null";
    std::cout<<",\"burst_exposure_ratio\":";
    if(available)json_number(model.burst_exposure_ratio);else std::cout<<"null";
    std::cout<<'}';
}
void report_correlation_experiment(const simulation::CorrelationExperimentResult& result,double chip_seconds) {
    std::cout<<"{\"segments\":"<<result.segments<<",\"segment_seconds\":";json_number(result.segment_seconds);
    std::cout<<",\"expected_coherence\":";json_number(result.expected_coherence);
    std::cout<<",\"expected_signal_energy\":";json_number(result.expected_signal_energy);
    std::cout<<",\"noncentrality\":";json_number(2.L*result.expected_signal_energy);
    std::cout<<",\"threshold\":";json_number(result.threshold);
    std::cout<<",\"template_correlation\":";json_number(result.template_correlation);
    std::cout<<",\"chips_per_segment\":";json_number(result.segment_seconds/chip_seconds);
    std::cout<<",\"trials\":"<<result.trials<<",\"detected_correct\":"<<result.detected_correct
        <<",\"noise_pair_above\":"<<result.noise_pair_above<<",\"correct_detection_probability\":";
    json_number(result.correct_probability);
    std::cout<<",\"correct_detection_probability_low\":";json_number(result.correct_probability_low);
    std::cout<<",\"correct_detection_probability_high\":";json_number(result.correct_probability_high);
    std::cout<<",\"signal_statistic_mean\":";json_number(result.signal_statistic_mean);
    std::cout<<",\"signal_statistic_variance\":";json_number(result.signal_statistic_variance);
    std::cout<<",\"phase_mean_energy_approximation\":"<<(result.phase_mean_energy_approximation?"true":"false")<<'}';
}
void analyze_link(const Args& a,transfer::Options options) {
    if(a.has("symbol-seconds")) {
        options.modem.integration_seconds=a.number("symbol-seconds",0);
        if(options.modem.integration_seconds<=0)throw Error("symbol-seconds must be positive");
        modem::validate(options.modem);
    }
    const bool matching_profile=!a.has("receive-targets");
    if(matching_profile)options.automatic_receive_profiles=false;
    const auto& c=options.modem;
    auto preset=a.has("simulation")?tuning::parse_simulation_preset(a.get("simulation")):
        tuning::SimulationPreset{"custom",true,a.number("tx-dbm",0),a.number("attenuation-db",0)};
    const auto noise_figure=a.number("noise-figure-db",10);
    const auto link=tuning::link_budget(preset,c.bandwidth_hz,c.sample_rate,noise_figure);
    modem::ChannelConfig channel;
    channel.snr_db=link.sample_snr_db;channel.seed=a.integer("seed",1);
    channel.delay_samples=a.integer("delay-samples",137);
    channel.frequency_offset_hz=a.number("frequency-offset",0);
    const auto oscillator=oscillator_config(a);
    channel.clock_error_ppm=oscillator.clock_error_ppm;
    channel.phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
    modem::validate_channel(c,channel);
    const auto trials=a.integer("trials",10000);
    if(!trials || trials>1000000)throw Error("trials must be 1..1000000");
    const auto segment_seconds=a.number("coherent-seconds",3600);
    if(segment_seconds<=0 || segment_seconds>1e18)throw Error("coherent-seconds must be positive and at most 1e18");
    simulation::CorrelationExperimentParameters experiment;
    experiment.cn0_db_hz=link.snr_db_hz;
    experiment.symbol_seconds=static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate;
    experiment.segment_seconds=experiment.symbol_seconds;
    experiment.trials=static_cast<std::size_t>(trials);experiment.seed=channel.seed;
    experiment.search_hypotheses=a.number("hypotheses",1e6);
    experiment.false_alarm_probability=a.number("false-alarm",1e-6);
    experiment.template_correlation=a.number("template-correlation",0);
    experiment.phase_noise_degrees_per_sqrt_second=0;
    const auto ideal=simulation::correlation_experiment(experiment);
    experiment.phase_noise_degrees_per_sqrt_second=channel.phase_noise_degrees_per_sqrt_second;
    experiment.residual_frequency_hz=a.number("residual-frequency-hz",0);
    const auto coherent=simulation::correlation_experiment(experiment);
    experiment.segment_seconds=std::min(experiment.symbol_seconds,segment_seconds);
    const auto segmented=simulation::correlation_experiment(experiment);
    transfer::Estimate transmission;
    bool raw=true;
    if(a.has("bits"))transmission=transfer::estimate_binary(status_bits(a,{},options.timestamp),options);
    else {
        const auto outgoing=message(a);raw=transfer::uses_raw_message(outgoing);
        transmission=transfer::estimate(outgoing,options);
    }
    const auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(c,
        options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{c};
    const auto current=simulation::estimate(transmission,options,raw,channel,profiles);
    const auto geometry=modem::default_pattern_frequency_search(c);
    const auto phase=static_cast<long double>(channel.phase_noise_degrees_per_sqrt_second)*std::numbers::pi_v<long double>/180;
    const auto rho=std::pow(10.L,static_cast<long double>(link.snr_db_hz)/10);
    const auto full_half_width=static_cast<long double>(c.carrier_hz)*modem::default_clock_uncertainty_ppm/1e6L;
    const auto full_frequency_count=1+2*std::ceil(full_half_width*experiment.symbol_seconds/.25L);
    std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"{\"analysis\":\"matched_correlation_reference\",\"production_decoder_run\":false,\"pcm_generated\":false"
        <<",\"conditional_on_matched_timing_and_clock\":true"
        <<",\"prescribed_template_correlation_not_measured\":true"
        <<",\"monte_carlo_intervals\":\"model_only_95_percent_Wilson\""
        <<",\"oscillator_model\":{\"preset\":\""<<oscillator.id<<"\",\"illustrative\":true,\"overridden\":"
        <<((a.has("clock-error-ppm")||a.has("phase-noise"))?"true":"false")<<'}'
        <<",\"receive_profile_assumption\":\""<<(matching_profile?"matching_transmit_profile":"explicit_receive_targets")<<'"'
        <<",\"link\":{\"transmit_dbm\":";json_number(preset.transmit_dbm);
    std::cout<<",\"attenuation_db\":";json_number(preset.attenuation_db);
    std::cout<<",\"noise_figure_db\":";json_number(noise_figure);
    std::cout<<",\"noise_density_dbm_hz\":";json_number(-174.L+noise_figure);
    std::cout<<",\"received_power_dbm\":";json_number(link.received_power_dbm);
    std::cout<<",\"cn0_db_hz\":";json_number(link.snr_db_hz);
    std::cout<<",\"snr_100hz_db\":";json_number(link.snr_db_hz-20.L);
    std::cout<<",\"selected_band_snr_db\":";json_number(link.snr_db);
    std::cout<<",\"ideal_18db_symbol_seconds\":";json_number(std::pow(10.L,(18.L-link.snr_db_hz)/10));
    // Linear Es/N0 for zero residual carrier error; zero diffusion has no
    // finite asymptote, represented by JSON null rather than infinity.
    std::cout<<",\"zero_residual_coherent_energy_asymptote_linear\":";
    if(phase>0)json_number(4*rho/(phase*phase));else std::cout<<"null";
    std::cout<<"},\"transmission\":{\"wire_bits\":"<<transmission.wire_bits
        <<",\"coded_bytes\":"<<transmission.coded_bytes<<",\"content_bytes\":"<<transmission.content_bytes
        <<",\"raw_wire_path\":"<<(raw?"true":"false")<<",\"waveform_samples\":"<<transmission.waveform_samples
        <<",\"sample_rate\":"<<c.sample_rate<<",\"bandwidth_hz\":";json_number(c.bandwidth_hz);
    std::cout<<",\"carrier_hz\":";json_number(c.carrier_hz);
    std::cout<<",\"tx_target_cn0_db_hz\":";json_number(a.number("target-snr",32));
    std::cout<<",\"symbol_duration_source\":\""<<(a.has("symbol-seconds")?"explicit_override":"configured_tx_plan")<<'"';
    std::cout<<",\"symbol_seconds\":";json_number(experiment.symbol_seconds);
    std::cout<<",\"coded_seconds\":";json_number(transmission.coded_seconds);
    std::cout<<",\"content_seconds\":";json_number(transmission.content_seconds);
    std::cout<<",\"waveform_seconds\":";json_number(transmission.total_seconds);
    std::cout<<",\"simulated_seconds\":";json_number(current.simulated_seconds);
    std::cout<<"},\"current_receiver\":{\"profile_matches\":"<<(current.profile_matches?"true":"false")
        <<",\"receiver_profiles\":"<<current.receiver_profiles
        <<",\"carrier_in_search\":"<<(current.carrier_in_search?"true":"false")
        <<",\"workspace_supported\":"<<(current.receiver_workspace_supported?"true":"false")
        <<",\"confidence_available\":"<<(current.confidence_available?"true":"false")
        <<",\"success_probability\":";
    if(current.confidence_available)json_number(current.success_probability);else std::cout<<"null";
    std::cout<<",\"clock_error_ppm\":";json_number(channel.clock_error_ppm);
    std::cout<<",\"frequency_offset_hz\":";json_number(channel.frequency_offset_hz);
    std::cout<<",\"actual_carrier_offset_hz\":";json_number(current.carrier_offset_hz);
    std::cout<<",\"requested_search_half_width_hz\":";json_number(current.carrier_search_half_width_hz);
    std::cout<<",\"frequency_step_hz\":";json_number(geometry.step_hz);
    std::cout<<",\"clock_error_for_quarter_chip_ppm\":";
    json_number(1e6L*.25L*modem::pattern_chip_samples(c)/c.sample_rate/experiment.symbol_seconds);
    std::cout<<",\"frequency_hypotheses\":"<<geometry.count
        <<",\"frequency_hypotheses_cap\":"<<modem::maximum_pattern_frequency_hypotheses
        <<",\"full_200ppm_frequency_hypotheses\":";json_number(full_frequency_count);
    std::cout<<",\"modeled_symbol_snr_db\":";json_number(current.modeled_symbol_snr_db);
    std::cout<<",\"cpu_reference\":\""<<simulation::reference_cpu<<"\",\"gpu_reference\":\""<<simulation::reference_gpu
        <<"\",\"gpu_hypothetical\":"<<(current.gpu_hypothetical?"true":"false")<<",\"cpu_seconds\":";
    json_number(current.cpu_seconds);std::cout<<",\"gpu_seconds\":";json_number(current.gpu_seconds);
    std::cout<<",\"tracking_seconds\":";json_number(current.tracking_seconds);
    std::cout<<",\"tracking_symbol_windows\":";json_number(current.tracking_symbol_windows);
    std::cout<<"},\"lpi\":";report_lpi(transmission,options,link.snr_db_hz,true);
    std::cout<<",\"reference_assumptions\":{\"search_hypotheses\":";json_number(experiment.search_hypotheses);
    std::cout<<",\"false_alarm_probability\":";json_number(experiment.false_alarm_probability);
    std::cout<<",\"noise_pair_union_bound\":";
    json_number(2.L*experiment.false_alarm_probability/experiment.search_hypotheses);
    std::cout<<",\"residual_frequency_hz\":";json_number(experiment.residual_frequency_hz);
    std::cout<<",\"phase_noise_degrees_per_sqrt_second\":";json_number(experiment.phase_noise_degrees_per_sqrt_second);
    std::cout<<",\"requested_coherent_seconds\":";json_number(segment_seconds);
    std::cout<<",\"template_correlation\":";json_number(experiment.template_correlation);
    std::cout<<",\"seed\":"<<experiment.seed<<"},\"experiments\":{\"ideal_coherent\":";
    const auto chip_seconds=static_cast<double>(modem::pattern_chip_samples(c))/c.sample_rate;
    report_correlation_experiment(ideal,chip_seconds);std::cout<<",\"coherent_phase_model\":";
    report_correlation_experiment(coherent,chip_seconds);std::cout<<",\"segmented_phase_model\":";
    report_correlation_experiment(segmented,chip_seconds);std::cout<<"}}\n";
}
volatile std::sig_atomic_t interrupted=0;
void interrupt_handler(int) {interrupted=1;}
void listen(const Args& a,const transfer::Options& options) {
    live::Settings settings;
    settings.transfer=options;
    settings.content_limit=content_budget(a);
    settings.dsp_workspace_bytes=dsp_budget(a);
    if(!a.has("time")) settings.transfer.timestamp=0;
    settings.device=a.get("device","default");
    settings.mono=!a.has("no-mono");
    if(a.has("simulation")) {
        const auto preset=tuning::parse_simulation_preset(a.get("simulation"));
        settings.simulation=preset.enabled;
        if(preset.enabled) settings.simulation_snr_db=tuning::link_budget(preset,
            options.modem.bandwidth_hz,options.modem.sample_rate).sample_snr_db;
    }
    settings.simulation_seed=a.integer("seed",1);
    const auto oscillator=oscillator_config(a);
    settings.simulation_clock_error_ppm=oscillator.clock_error_ppm;
    settings.simulation_phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
    if(options.modem.spreading_mode!=modem::SpreadingMode::tone && a.has("keyfile") && !a.has("key-name")) {
        for(const auto& entry:load_keyring(a.get("keyfile"),a.has("pad")?
            std::optional<std::filesystem::path>(a.get("pad")):std::nullopt)) settings.receive_keys.push_back(entry.key);
    }
    const auto seconds=a.number("seconds",0);
    if(seconds<0 || seconds>86400) throw Error("listen seconds must be 0..86400 (0 means continuous)");
    std::optional<Message> outgoing;
    if(a.has("text") || a.has("input")) outgoing=message(a);
    live::Session session;
    session.start(settings);
    if(outgoing) session.transmit(*outgoing);
    interrupted=0;
    const auto previous=std::signal(SIGINT,interrupt_handler);
    struct RestoreSignal {decltype(previous) handler;~RestoreSignal(){std::signal(SIGINT,handler);}} restore{previous};
    const auto started=std::chrono::steady_clock::now();
    std::string last_error;
    while(!interrupted) {
        const auto snapshot=session.snapshot();
        if(snapshot.error!=last_error) {
            last_error=snapshot.error;
            if(!last_error.empty()) std::cerr<<"pump: "<<last_error<<'\n';
        }
        if(a.has("json") && a.has("progress")) {
            std::cout<<"{\"event\":\"signal\",\"sequence\":"<<snapshot.sequence
                <<",\"samples_received\":"<<snapshot.samples_received
                <<",\"virtual_seconds\":"<<snapshot.virtual_seconds
                <<",\"transmission_seconds\":"<<snapshot.transmission_seconds
                <<",\"transmission_fraction\":"<<snapshot.transmission_fraction
                <<",\"dsp_buffered_bytes\":"<<snapshot.dsp_buffered_bytes
                <<",\"simulation\":"<<(snapshot.simulation?"true":"false")
                <<",\"transmitting\":"<<(snapshot.transmitting?"true":"false")<<"}\n";
            for(const auto& signal:snapshot.signals) if(!signal.binary && !signal.validated && !signal.complete) {
                const Bytes text(signal.text.begin(),signal.text.end());
                std::cout<<"{\"event\":\"preview\",\"validated\":false,\"frequency_hz\":"<<signal.frequency_hz
                    <<",\"data_base64\":\""<<base64_encode(text)<<'"';
                report_signal_identity(signal);std::cout<<"}\n";
            }
        }
        if(a.has("json"))for(const auto& signal:snapshot.signals) {
            if(signal.binary) {
                std::cout<<"{\"event\":\"raw_bits\",\"content_validated\":false,\"authenticated\":false";
                report_signal_identity(signal);
                std::cout<<",\"complete\":"<<(signal.complete?"true":"false")<<",\"raw_bits\":\""
                    <<json_escape(signal.text)<<"\",\"raw_bit_count\":"<<signal.received_bits<<",\"pattern_score\":";
                if(signal.pattern_score && std::isfinite(*signal.pattern_score))std::cout<<*signal.pattern_score;else std::cout<<"null";
                std::cout<<",\"pattern_score_units\":\"model log evidence\"";
                report_recovery_json(signal.recovery_progress);std::cout<<"}\n";
            } else {
                // Every profile revision is observable without --progress,
                // including retractions of previously completed interpretations.
                std::cout<<"{\"event\":\"reception_update\"";report_signal_identity(signal);
                std::cout<<",\"complete\":"<<(signal.complete?"true":"false")
                    <<",\"content_validated\":"<<(signal.validated?"true":"false")
                    <<",\"reception_id\":\""<<json_escape(signal.reception_id)<<'"';
                report_recovery_json(signal.recovery_progress);std::cout<<"}\n";
            }
        }
        else for(const auto& signal:snapshot.signals) {
            const auto state=signal.recovery_progress.state;
            if(state!=transfer::RecoveryState::none && state!=transfer::RecoveryState::running &&
               !(state==transfer::RecoveryState::recovered && signal.validated)) {
                std::cerr<<"Signal "<<signal.id<<": ";
                report_recovery_status(signal.complete,signal.recovery_progress);
            }
        }
        for(const auto& received:snapshot.received) {
            const auto reception_id=id_string(received.content.message);
            const auto signal=std::find_if(snapshot.signals.rbegin(),snapshot.signals.rend(),[&](const auto& event) {
                return event.complete&&event.reception_id==reception_id;
            });
            report_received(a,received,signal==snapshot.signals.rend()?nullptr:&*signal);
        }
        std::cout.flush();
        if(!snapshot.running && !snapshot.error.empty()) throw Error(snapshot.error);
        if(seconds>0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>=seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    session.stop();
}
}

int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
        Args a(argc,argv);
        if(a.has("version")) {std::cout<<"Data Pump "<<DATAPUMP_VERSION<<'\n';return 0;}
        if(a.has("help") || a.command.empty()) {std::cout<<usage;return 0;}
        a.validate_options();
        if(a.command=="qr") {
            auto content=message(a).data;
            auto text=std::string(content.begin(),content.end());
            auto format=a.get("format","svg");
            if(format!="svg" && format!="pbm") throw Error("QR format must be svg or pbm");
            auto image=format=="svg"?qr_svg(text):qr_pbm(text,1);
            output_bytes(a,Bytes(image.begin(),image.end()));return 0;
        }
        if(a.command=="keygen") {
            if(!a.has("output")) throw Error("keygen requires --output PATH");
            std::vector<std::string> names;
            std::istringstream input(a.get("key-names","Default"));
            std::string name;
            while(std::getline(input,name,',')) names.push_back(name);
            if(a.get("key-names").ends_with(',')) throw Error("key names must not be empty");
            create_keyring(a.get("output"),names,a.has("pad")?std::optional<std::filesystem::path>(a.get("pad")):std::nullopt);
            std::cerr<<"Created symmetric keyfile: "<<a.get("output")<<'\n';return 0;
        }
        if(a.command=="keys") {
            if(!a.has("keyfile")) throw Error("keys requires --keyfile PATH");
            for(const auto& entry:load_keyring(a.get("keyfile"),a.has("pad")?
                std::optional<std::filesystem::path>(a.get("pad")):std::nullopt))
                std::cout<<entry.name<<'\n';
            return 0;
        }
        if(a.command=="devices") {
            for(const auto& d:audio::devices()) std::cout<<d.id<<'\t'<<d.description<<'\n';
            return 0;
        }
        const std::set<std::string> commands={"tx","rx","simulate","status-tx","status-rx","estimate","listen","analyze-link"};
        if(!commands.contains(a.command)) throw Error("unknown command: "+a.command);
        auto c=config(a);auto timestamp=epoch(a);
        auto k=c.spreading_mode==modem::SpreadingMode::tone?std::optional<Crypto>{}:key(a);
        if(c.spreading_mode==modem::SpreadingMode::tone && a.has("keyfile"))
            std::cerr<<"Tone mode is unencrypted; key selection is disabled.\n";
        auto settings=transfer_options(a,c,k,timestamp);
        if(a.command=="analyze-link") {analyze_link(a,settings);return 0;}
        transfer::Progress progress;
        if(a.has("progress")) progress=[](std::uint64_t candidate) {std::cerr<<"Searching epoch "<<candidate<<'\n';};
        if(a.command=="estimate") {
            const auto result=transfer::estimate(message(a),settings);
            const auto capacity=tuning::shannon_capacity_bps(c.bandwidth_hz,a.number("target-snr",32));
            std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)
                <<"{\"coded_bytes\":"<<result.coded_bytes<<",\"wire_bits\":"<<result.wire_bits<<",\"content_bytes\":"<<result.content_bytes
                <<",\"coded_seconds\":"<<result.coded_seconds<<",\"content_seconds\":"<<result.content_seconds
                <<",\"total_seconds\":"<<result.total_seconds<<",\"bit_rate\":"<<modem::bit_rate(c)
                <<",\"shannon_capacity_bps\":";
            if(std::isfinite(capacity))std::cout<<capacity;else std::cout<<"null";
            std::cout<<",\"spreading\":"<<c.spreading_factor
                <<",\"constellation_bits\":"<<c.constellation_bits
                <<",\"sample_rate\":"<<c.sample_rate<<",\"carrier_hz\":"<<c.carrier_hz
                <<",\"repeatable_allowed\":"<<(result.repeatable_allowed?"true":"false")
                <<",\"memory_supported\":"<<(result.memory_supported?"true":"false")
                <<",\"batch_memory_supported\":"<<(result.batch_memory_supported?"true":"false");
            if(automatic_tuning(a)) {
                const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",32),
                    tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"));
                std::cout<<",\"estimated_symbol_snr_db\":"<<plan.estimated_symbol_snr_db
                    <<",\"symbol_seconds\":"<<modem::symbol_seconds(c)
                    <<",\"target_supported\":"<<(plan.target_supported?"true":"false");
            }
            std::cout<<",\"lpi\":";report_lpi(result,settings,a.number("target-snr",32),false);
            std::cout<<"}\n";
            return 0;
        }
        if(a.command=="listen") {listen(a,settings);return 0;}
        if(a.command=="status-tx" || a.command=="status-rx") {
            const auto plain=status_bits(a,{},timestamp);
            if(a.command=="status-tx") {
                if(!a.has("output") && !a.has("device"))throw Error("status-tx requires --output or --device");
                if(a.has("output")) {
                    auto source=transfer::binary_transmitter(plain,settings);
                    if(source->total_samples()>c.memory_limit/sizeof(float))throw Error("status WAV exceeds the configured waveform memory limit");
                    std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
                    for(std::size_t offset=0;offset<samples.size();)offset+=source->read(std::span(samples).subspan(offset,std::min<std::size_t>(4096,samples.size()-offset)));
                    output_wave(a,samples,c);
                } else {
                    const auto delay=hardware_delay(a,c);
                    play_transmission(a,settings,[&](const auto& options){return transfer::binary_transmitter(plain,options);});
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
                }
            } else {
                auto wav=input_wav(a);settings.modem.sample_rate=wav.sample_rate;modem::validate(settings.modem);
                const auto result=transfer::receive(wav.samples,settings,progress);
                std::cout<<"{\"authenticated\":false,\"content_validated\":false,\"known_bits\":\""<<a.get("bits")<<"\",\"raw_bits\":\"";
                for(auto bit:result.raw_bits)std::cout<<(bit?'1':'0');
                std::cout<<"\",\"raw_bit_count\":"<<result.raw_bits.size()<<",\"missing_symbols\":"<<result.missing_symbols
                    <<",\"stream_complete\":"<<(result.stream_complete?"true":"false")
                    <<",\"known_bits_match\":"<<(result.stream_complete && !result.missing_symbols && result.observed_bits==plain.size() && result.raw_bits==plain?"true":"false")<<",\"pattern_score\":";
                if(result.diagnostics.pattern_score && std::isfinite(*result.diagnostics.pattern_score))std::cout<<*result.diagnostics.pattern_score;else std::cout<<"null";
                std::cout<<",\"pattern_score_units\":\"model log evidence\"}\n";
            }
            return 0;
        }
        if(a.command=="rx") {
            std::vector<float> samples;
            if(a.has("device")) {
                if(a.has("input"))throw Error("choose --input WAV or --device");
                samples=audio::record(a.number("seconds",15),c.sample_rate,a.get("device"),budget(a),{},[&](const auto& format) {
                    audio_passband_guard(c)(format);
                    if(!a.has("time")) {
                        settings.capture_epoch=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
                        settings.timestamp=static_cast<std::uint64_t>(*settings.capture_epoch);
                    }
                });
            }
            else {auto wav=input_wav(a);c.sample_rate=wav.sample_rate;modem::validate(c);samples=std::move(wav.samples);}
            settings.modem=c;
            auto result=transfer::receive(samples,settings,progress);report_received(a,result);return 0;
        }
        if(a.command=="tx" && !a.has("output") && !a.has("device")) throw Error("tx requires --output WAV or explicit --device");
        if(a.command=="simulate" && a.has("device")) throw Error("simulation uses in-memory loopback; omit --device");
        if(a.command=="tx") {
            const auto outgoing=message(a);
            const auto estimate=transfer::estimate(outgoing,settings);
            if(a.has("device")) {
                const auto delay=hardware_delay(a,c);
                play_transmission(a,settings,[&](const auto& options){return transfer::message_transmitter(outgoing,options);});
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
            } else output_wave(a,transfer::transmit(outgoing,settings),c);
            std::cerr<<"Transmitted ";
            std::cerr<<estimate.wire_bits<<" pattern bits, ";
            std::cerr<<estimate.total_seconds
                     <<" seconds; start epoch "<<settings.timestamp<<'\n';return 0;
        }
        modem::ChannelConfig channel;
        channel.snr_db=a.number("snr",20);channel.seed=a.integer("seed",1);
        if(a.has("simulation")) {
            if(a.has("snr")) throw Error("choose --simulation preset or --snr");
            channel.snr_db=tuning::link_budget(tuning::parse_simulation_preset(a.get("simulation")),c.bandwidth_hz,c.sample_rate).sample_snr_db;
        }
        channel.delay_samples=a.integer("delay-samples",137);
        channel.frequency_offset_hz=a.number("frequency-offset",0);
        const auto oscillator=oscillator_config(a);
        channel.clock_error_ppm=oscillator.clock_error_ppm;
        channel.phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
        if(a.has("receiver-time")) channel.receiver_timestamp=a.integer("receiver-time",timestamp);
        const auto outgoing=message(a);
        transfer::Received result;
        if(a.has("output")) {
            auto source=transfer::message_transmitter(outgoing,settings);
            modem::SampledSimulationChannel impairments(transfer::seeded_config(settings,timestamp),channel);
            std::array<float,2048> block{};std::vector<float> noisy;
            const auto append=[&](std::size_t count) {
                if(count>c.memory_limit/sizeof(float)-noisy.size())throw Error("simulated WAV exceeds memory limit");
                noisy.insert(noisy.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
            };
            while(const auto count=impairments.read(*source,block))append(count);
            auto tail=modem::pattern_absence_samples(c)+c.sample_rate;
            while(tail) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tail,block.size()));
                impairments.read_noise(std::span(block).first(count));append(count);tail-=count;
            }
            auto receiver_settings=settings;
            receiver_settings.timestamp=channel.receiver_timestamp.value_or(timestamp);
            result=transfer::receive(noisy,receiver_settings,progress);
            output_wave(a,std::move(noisy),c,false);
        } else result=transfer::simulate(outgoing,settings,channel,progress);
        report_received(a,result);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"pump: "<<e.what()<<'\n';return 2;}
}
