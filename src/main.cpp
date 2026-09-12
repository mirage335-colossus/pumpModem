#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/packet.hpp"
#include "datapump/qr.hpp"
#include "datapump/runtime.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/live.hpp"
#include "datapump/streaming_modem.hpp"
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
#define DATAPUMP_VERSION "0.5.4"
#endif
namespace {
const char* usage="Data Pump " DATAPUMP_VERSION R"HELP( — civilian audio text and file modem

Usage: pump COMMAND [OPTIONS]
  simulate     Accelerated AWGN loopback; --output WAV uses raw sampled audio
  listen       Continuous live receiver (or noise/loopback with --simulation)
  estimate     Calculate exact message airtime without creating a waveform
  tx           Encode text/file to WAV (--output) or live audio (--device)
  rx           Decode a WAV (--input) or record live audio (--device --seconds)
  pack/unpack  Framed packet byte streams, for external tools; no device I/O
  keygen       Create an owner-only 128 MiB symmetric keyfile (--output)
  keys         List the named key sets in --keyfile
  devices      Enumerate local audio devices
  status-tx    Transmit exact few-bit callsign to WAV, without packet overhead
  status-rx    Correlate a known few-bit callsign in WAV; not authenticated
  qr           Generate optical transfer QR Level L (--format svg|pbm)

Input/output:
  --text TEXT           Text to send (otherwise --input FILE or - for stdin)
  --input PATH          TX file, RX WAV, or packet input (- means stdin)
  --kind text|file|screenshot  Default: text with --text/stdin, file with path
  --filename NAME       Display filename for file/screenshot (basename only)
  --output PATH         Explicit WAV/keyfile/packet output; never overwrite
  --save PATH           Explicit save of a validated received file; never overwrite
  --json                Received content as JSON with base64 payload and diagnostics
  --callsign TEXT --grid TEXT --repeatable

Modem:
  --bw HZ               Nominal bandwidth, default1200 (also 1.2kHz etc.)
  --sample-rate HZ      Internal DSP clock, 64..120000000; default max(6000,4*bw)
  --carrier HZ          Default max(1500,0.75*bw); explicit overrides stay available
  --spreading N         Manual 4-bit APSK chips/symbol, 1..16384 (disables auto)
  --target-snr DBHZ     Automatic target C/N0; default40, auto unless manual controls
  --pattern MODE        auto-keystream, auto-pattern, auto-tone, pattern-N, tone-N
  --scramble            Cryptographic pattern rotation (requires keyfile)
  --dsss                Independent encrypted direct-sequence spreading
  --fec 20|60|off        Reed-Solomon parity overhead, default20
  --no-compression      Diagnostic override; normal compression is automatic
  --memory-mb N         Legacy batch PCM workspace budget, default256 MiB
  --cache-mb N          Received content/input limit, default256 MiB
  --dsp-mb N            Independent streaming DSP workspace, default64 MiB
  --keyfile PATH        Symmetric keyfile; encrypted preamble, frame, FEC
  --key-name NAME       Select a named key set (default: first)
  --key-names A,B,C     Names to create with keygen (default: Default)
  --pad PATH            Required external >1GiB pad when bound to keyfile
  --time SECONDS        Shared start epoch; default current UNIX second
  --search-seconds N    RX epoch trials ±N seconds, nearest first; default6
  --progress            Emit timing-search progress to stderr

Audio/simulation:
  --device ID           OS audio endpoint; listen automatically uses default
  --device-type audio   Analog audio input only; no network/raw serial input
  --seconds N           RX recording duration(default15); listen limit(default0)
  --tx-delay N          Delay after live TX completes; default6 seconds
  --snr DB              Simulator measured signal/noise power ratio; default20
  --simulation PRESET   e.g. "3dBm -120dB": TX power and channel attenuation
  --seed N --delay-samples N --frequency-offset HZ
  --clock-error-ppm N   Relative crystal error; default100 (0 for ideal clock)
  --phase-noise N       Phase diffusion, degrees/sqrt(second); default0.5

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
GUI: datapump-gui
)HELP";

class Args {
    std::map<std::string,std::string> values_;
public:
    std::string command;
    Args(int argc,char** argv) {
        const std::set<std::string> booleans={"json","repeatable","no-compression","scramble","dsss","progress","help","version"};
        const std::set<std::string> valued={"text","input","output","save","kind","filename","callsign","grid",
            "bw","sample-rate","carrier","spreading","fec","memory-mb","keyfile","pad","time","search-seconds",
            "device","device-type","seconds","tx-delay","snr","seed","delay-samples","frequency-offset","bits","format",
            "target-snr","pattern","simulation","key-name","key-names","cache-mb","dsp-mb","clock-error-ppm","phase-noise"};
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
        if(command=="rx" || command=="unpack" || command=="status-rx") {
            reject({"output"},"is not a receive output; use --save PATH for verified text/files");
            reject({"text"},"is a transmit input; use --input for reception");
        }
        if(command!="rx" && command!="unpack") reject({"save"},"is only valid for rx/unpack");
        if(command!="tx" && command!="rx" && command!="status-tx" && command!="listen") reject({"device"},"is only valid for live audio commands");
        if(command!="keygen") reject({"key-names"},"is only valid for keygen");
        if(command!="simulate" && command!="listen") reject({"simulation"},"is only valid for simulate/listen");
        if(command!="simulate" && command!="listen") reject({"clock-error-ppm","phase-noise"},"is only valid for simulate/listen");
        if(command=="listen") reject({"snr","frequency-offset","delay-samples","output","tx-delay"},"is not a listen option; choose a simulation preset for its continuous channel");
        if(command=="tx" && has("output") && has("device")) throw Error("choose one TX destination: --output or --device");
    }
};
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
        const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",40),
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
    auto fec=a.get("fec","20");
    if(fec=="20") options.fec=FecMode::rs20;
    else if(fec=="60") options.fec=FecMode::rs60;
    else if(fec=="off") options.fec=FecMode::off;
    else throw Error("fec must be20,60,or off");
    options.compression=!a.has("no-compression");
    return options;
}
Bytes input_bytes(const Args& a) {
    auto path=a.get("input","-");
    const auto limit=a.command=="unpack"?transfer::packet_workspace_limit(content_budget(a)):content_budget(a);
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
    m.callsign=a.get("callsign");m.grid=a.get("grid");m.repeatable=a.has("repeatable");
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
        if(a.command=="pack" && stdout_terminal()) throw Error("binary packets require stdout redirection or --output PATH");
        std::cout.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!std::cout)throw Error("stdout write failed");
    }
}
void output_wave(const Args& a,const std::vector<float>& samples,const modem::Config& c) {
    if(a.has("output")) {
        std::ostringstream wav(std::ios::out|std::ios::binary);
        modem::write_wav(wav,samples,c.sample_rate);auto bytes=wav.str();
        write_new_file(a.get("output"),std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
    }
    if(a.has("device")) {
        auto delay=a.number("tx-delay",6);
        if(delay<0 || delay>3600) throw Error("tx-delay must be0..3600 seconds");
        audio::play(samples,c.sample_rate,a.get("device"),{},audio_passband_guard(c));
        if(a.has("keyfile"))std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
    }
}
std::string id_string(const Message& m) {
    std::ostringstream id;for(auto b:m.id) id<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b);return id.str();
}
void report(const Args& a,const DecodedPacket& packet,const modem::Diagnostics& d={},std::uint64_t timestamp=0) {
    const auto& m=packet.message;
    if(a.has("save")) write_new_file(a.get("save"),m.data);
    if(a.has("json")) {
        std::cout<<"{\"validated\":true,\"authenticated\":"<<(packet.authenticated?"true":"false")
          <<",\"id\":\""<<id_string(m)<<"\",\"kind\":\""<<(m.kind==MessageKind::text?"text":m.kind==MessageKind::file?"file":"screenshot")
          <<"\",\"filename\":\""<<json_escape(m.filename)<<"\",\"callsign\":\""<<json_escape(m.callsign)
          <<"\",\"grid\":\""<<json_escape(m.grid)<<"\",\"repeatable\":"<<(m.repeatable?"true":"false")
          <<",\"data_base64\":\""<<base64_encode(m.data)<<"\",\"corrected_bytes\":"<<packet.corrected_bytes
          <<",\"timestamp\":"<<timestamp<<",\"diagnostics\":{\"sample_offset\":"<<d.sample_offset
          <<",\"correlation\":"<<d.preamble_correlation<<",\"snr_db\":"<<(std::isfinite(d.snr_db)?d.snr_db:0)
          <<",\"bit_rate\":"<<d.bit_rate<<",\"waveform\":[";
        for(std::size_t i=0;i<d.waveform.size();++i) std::cout<<(i?",":"")<<d.waveform[i];
        std::cout<<"],\"constellation\":[";
        for(std::size_t i=0;i<d.constellation.size();++i) std::cout<<(i?",":"")<<'['<<d.constellation[i].real()<<','<<d.constellation[i].imag()<<']';
        std::cout<<"]}}\n";
    } else if(!a.has("save")) {
        if(m.kind!=MessageKind::text) {
            std::cerr<<"Validated file: "<<m.filename<<" ("<<m.data.size()<<" bytes). Use --save PATH or --json to retrieve.\n";
        } else if(stdout_terminal()) std::cout<<terminal_text(m.data);
        else std::cout.write(reinterpret_cast<const char*>(m.data.data()),static_cast<std::streamsize>(m.data.size()));
    }
}
Bytes status_bits(const Args& a,const std::optional<Crypto>& k,std::uint64_t time) {
    auto input=a.get("bits");if(input.empty() || input.size()>4096) throw Error("status requires1..4096 known binary --bits");
    Bytes bits;
    for(char c:input) {if(c!='0' && c!='1')throw Error("status bits must contain only0 and1");bits.push_back(static_cast<std::uint8_t>(c-'0'));}
    if(k) {auto stream=k->stream(StreamPurpose::Data,time,0,(bits.size()+7)/8);for(std::size_t i=0;i<bits.size();++i)bits[i]^=static_cast<std::uint8_t>((stream[i/8]>>(7-i%8))&1);}
    return bits;
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
    if(a.has("simulation")) {
        const auto preset=tuning::parse_simulation_preset(a.get("simulation"));
        settings.simulation=preset.enabled;
        if(preset.enabled) settings.simulation_snr_db=tuning::link_budget(preset,
            options.modem.bandwidth_hz,options.modem.sample_rate).sample_snr_db;
    }
    settings.simulation_seed=a.integer("seed",1);
    settings.simulation_clock_error_ppm=a.number("clock-error-ppm",100);
    settings.simulation_phase_noise_degrees_per_sqrt_second=a.number("phase-noise",.5);
    if(a.has("keyfile") && !a.has("key-name")) {
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
            for(const auto& signal:snapshot.signals) if(!signal.validated) {
                const Bytes text(signal.text.begin(),signal.text.end());
                std::cout<<"{\"event\":\"preview\",\"validated\":false,\"frequency_hz\":"<<signal.frequency_hz
                    <<",\"data_base64\":\""<<base64_encode(text)<<"\"}\n";
            }
        }
        for(const auto& received:snapshot.received) report(a,received.packet,received.diagnostics,received.timestamp);
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
        const std::set<std::string> commands={"pack","unpack","tx","rx","simulate","status-tx","status-rx","estimate","listen"};
        if(!commands.contains(a.command)) throw Error("unknown command: "+a.command);
        auto c=config(a);auto timestamp=epoch(a);auto k=key(a);
        auto settings=transfer_options(a,c,k,timestamp);
        transfer::Progress progress;
        if(a.has("progress")) progress=[](std::uint64_t candidate) {std::cerr<<"Searching epoch "<<candidate<<'\n';};
        if(a.command=="estimate") {
            const auto result=transfer::estimate(message(a),settings);
            std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)
                <<"{\"packet_bytes\":"<<result.packet_bytes<<",\"content_bytes\":"<<result.content_bytes
                <<",\"packet_seconds\":"<<result.packet_seconds<<",\"content_seconds\":"<<result.content_seconds
                <<",\"total_seconds\":"<<result.total_seconds<<",\"bit_rate\":"<<modem::bit_rate(c)
                <<",\"spreading\":"<<c.spreading_factor
                <<",\"constellation_bits\":"<<c.constellation_bits
                <<",\"sample_rate\":"<<c.sample_rate<<",\"carrier_hz\":"<<c.carrier_hz
                <<",\"repeatable_allowed\":"<<(result.repeatable_allowed?"true":"false")
                <<",\"memory_supported\":"<<(result.memory_supported?"true":"false")
                <<",\"batch_memory_supported\":"<<(result.batch_memory_supported?"true":"false");
            if(automatic_tuning(a)) {
                const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",40),
                    tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"));
                std::cout<<",\"estimated_symbol_snr_db\":"<<plan.estimated_symbol_snr_db
                    <<",\"symbol_seconds\":"<<modem::symbol_seconds(c)
                    <<",\"target_supported\":"<<(plan.target_supported?"true":"false");
            }
            std::cout<<"}\n";
            return 0;
        }
        if(a.command=="listen") {listen(a,settings);return 0;}
        if(a.command=="pack") {
            output_bytes(a,transfer::pack(message(a),settings));return 0;
        }
        if(a.command=="unpack") {
            report(a,transfer::unpack(input_bytes(a),settings),{},timestamp);return 0;
        }
        if(a.command=="status-tx" || a.command=="status-rx") {
            c=transfer::seeded_config(settings,timestamp);auto bits=status_bits(a,k,timestamp);
            if(a.command=="status-tx") {
                if(!a.has("output") && !a.has("device")) throw Error("status-tx requires --output or --device");
                output_wave(a,modem::modulate_status(bits,c),c);
            } else {
                auto wav=input_wav(a);c.sample_rate=wav.sample_rate;modem::validate(c);
                auto correlation=modem::detect_status(wav.samples,bits,c);
                std::cout<<"{\"authenticated\":false,\"known_bits\":\""<<a.get("bits")<<"\",\"correlation\":"<<correlation<<"}\n";
            }
            return 0;
        }
        if(a.command=="rx") {
            std::vector<float> samples;
            if(a.has("device")) {if(a.has("input"))throw Error("choose --input WAV or --device");samples=audio::record(a.number("seconds",15),c.sample_rate,a.get("device"),budget(a),{},audio_passband_guard(c));}
            else {auto wav=input_wav(a);c.sample_rate=wav.sample_rate;modem::validate(c);samples=std::move(wav.samples);}
            settings.modem=c;
            auto result=transfer::receive(samples,settings,progress);report(a,result.packet,result.diagnostics,result.timestamp);return 0;
        }
        if(a.command=="tx" && !a.has("output") && !a.has("device")) throw Error("tx requires --output WAV or explicit --device");
        if(a.command=="simulate" && a.has("device")) throw Error("simulation uses in-memory loopback; omit --device");
        if(a.command=="tx") {
            const auto outgoing=message(a);
            const auto estimate=transfer::estimate(outgoing,settings);
            if(a.has("device")) {
                const auto delay=a.number("tx-delay",6);
                if(delay<0 || delay>3600)throw Error("tx-delay must be 0..3600 seconds");
                modem::StreamingTransmitter source(transfer::transmission_wire(outgoing,settings),
                    transfer::seeded_config(settings,timestamp),settings.dsp_workspace_bytes);
                audio::playback(c.sample_rate,a.get("device"),[&](std::span<float> chunk){return source.read(chunk);},{},audio_passband_guard(c));
                if(settings.key)std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
            } else output_wave(a,transfer::transmit(outgoing,settings),c);
            std::cerr<<"Transmitted "<<estimate.packet_bytes<<" frame bytes, "<<estimate.total_seconds
                     <<" seconds; start epoch "<<timestamp<<'\n';return 0;
        }
        modem::ChannelConfig channel;
        channel.snr_db=a.number("snr",20);channel.seed=a.integer("seed",1);
        if(a.has("simulation")) {
            if(a.has("snr")) throw Error("choose --simulation preset or --snr");
            channel.snr_db=tuning::link_budget(tuning::parse_simulation_preset(a.get("simulation")),c.bandwidth_hz,c.sample_rate).sample_snr_db;
        }
        channel.delay_samples=a.integer("delay-samples",137);
        channel.frequency_offset_hz=a.number("frequency-offset",0);
        channel.clock_error_ppm=a.number("clock-error-ppm",100);
        channel.phase_noise_degrees_per_sqrt_second=a.number("phase-noise",.5);
        const auto outgoing=message(a);
        transfer::Received result;
        if(a.has("output")) {
            const auto samples=transfer::transmit(outgoing,settings);
            const auto noisy=modem::simulate(samples,transfer::seeded_config(settings,timestamp),channel);
            output_wave(a,noisy,c);
            result=transfer::receive(noisy,settings,progress);
        } else result=transfer::simulate(outgoing,settings,channel,progress);
        report(a,result.packet,result.diagnostics,result.timestamp);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"pump: "<<e.what()<<'\n';return 2;}
}
