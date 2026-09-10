#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/packet.hpp"
#include "datapump/qr.hpp"
#include "datapump/runtime.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/live.hpp"
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
namespace {
const char* usage=R"HELP(Data Pump 0.1 — civilian audio text and file modem

Usage: pump COMMAND [OPTIONS]
  simulate     Encode, modulate, add seeded AWGN, acquire, correct and verify
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
  --sample-rate HZ      Manual sample rate, 8000..384000; default48000
  --carrier HZ          Default1500 (bandwidth/2+1000 for wide bandwidths)
  --spreading N         Chips per dibit, 1..16384; default1
  --target-snr DBHZ     Automatic integration target C/N0; default40
  --pattern MODE        auto-keystream, auto-pattern, auto-tone, pattern-N, tone-N
  --scramble            Cryptographic pattern rotation (requires keyfile)
  --dsss                Independent encrypted direct-sequence spreading
  --fec 20|60|off        Reed-Solomon parity overhead, default20
  --no-compression      Diagnostic override; normal compression is automatic
  --memory-mb N         Buffer budget, default256 MiB
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
            "target-snr","pattern","simulation","key-name","key-names"};
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
modem::Config config(const Args& a) {
    modem::Config c;
    c.bandwidth_hz=a.number("bw",1200);
    const bool automatic=a.has("target-snr") || a.has("pattern");
    if(automatic) {
        if(a.has("spreading") || a.has("scramble") || a.has("dsss") || a.has("sample-rate") || a.has("carrier"))
            throw Error("automatic tuning cannot be combined with manual spreading/scramble/dsss/sample-rate/carrier");
        const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",40),
            tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"));
        c=plan.config;
        if(a.has("progress") || !plan.target_supported) std::cerr<<plan.explanation<<'\n';
    }
    auto rate=a.integer("sample-rate",automatic?c.sample_rate:c.bandwidth_hz>22050?96000:48000);
    if(rate>384000) throw Error("sample rate exceeds384000");
    c.sample_rate=static_cast<std::uint32_t>(rate);
    c.carrier_hz=a.number("carrier",automatic?c.carrier_hz:c.bandwidth_hz>2400?c.bandwidth_hz/2+1000:1500);
    auto spreading=a.integer("spreading",c.spreading_factor);
    if(spreading==0 || spreading>16384) throw Error("spreading must be1..16384");
    c.spreading_factor=static_cast<unsigned>(spreading);
    c.memory_limit=budget(a);
    if(!automatic) {c.scramble=a.has("scramble");c.dsss=a.has("dsss");}
    if((a.has("scramble") || a.has("dsss")) && !a.has("keyfile")) throw Error("encrypted spreading requires --keyfile");
    if(a.get("device-type","audio")!="audio") throw Error("this build supports analog audio only; SDR and IC-7100 frontends are not implemented");
    modem::validate(c);return c;
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
    if(path=="-") return read_bounded(std::cin,budget(a));
    std::ifstream input(path,std::ios::binary);
    if(!input) throw Error("cannot open input: "+path);
    return read_bounded(input,budget(a));
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
    if(m.data.size()>budget(a)) throw Error("message exceeds memory budget");
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
        audio::play(samples,c.sample_rate,a.get("device"));
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
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
    if(!a.has("time")) settings.transfer.timestamp=0;
    settings.device=a.get("device","default");
    if(a.has("simulation")) {
        const auto preset=tuning::parse_simulation_preset(a.get("simulation"));
        settings.simulation=preset.enabled;
        if(preset.enabled) settings.simulation_snr_db=tuning::link_budget(preset,
            options.modem.bandwidth_hz,options.modem.sample_rate).sample_snr_db;
    }
    settings.simulation_seed=a.integer("seed",1);
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
        if(a.has("version")) {std::cout<<"Data Pump 0.1.0\n";return 0;}
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
            std::cout<<"{\"packet_bytes\":"<<result.packet_bytes<<",\"content_bytes\":"<<result.content_bytes
                <<",\"packet_seconds\":"<<result.packet_seconds<<",\"content_seconds\":"<<result.content_seconds
                <<",\"total_seconds\":"<<result.total_seconds<<",\"bit_rate\":"<<modem::bit_rate(c)
                <<",\"spreading\":"<<c.spreading_factor
                <<",\"repeatable_allowed\":"<<(result.repeatable_allowed?"true":"false")
                <<",\"memory_supported\":"<<(result.memory_supported?"true":"false");
            if(a.has("target-snr") || a.has("pattern")) {
                const auto snr=a.number("target-snr",40)+10*std::log10(2/modem::bit_rate(c));
                std::cout<<",\"estimated_symbol_snr_db\":"<<snr<<",\"target_supported\":"<<(snr+1e-10>=10?"true":"false");
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
            if(a.has("device")) {if(a.has("input"))throw Error("choose --input WAV or --device");samples=audio::record(a.number("seconds",15),c.sample_rate,a.get("device"),budget(a));}
            else {auto wav=input_wav(a);c.sample_rate=wav.sample_rate;modem::validate(c);samples=std::move(wav.samples);}
            settings.modem=c;
            auto result=transfer::receive(samples,settings,progress);report(a,result.packet,result.diagnostics,result.timestamp);return 0;
        }
        if(a.command=="tx" && !a.has("output") && !a.has("device")) throw Error("tx requires --output WAV or explicit --device");
        if(a.command=="simulate" && a.has("device")) throw Error("simulation uses in-memory loopback; omit --device");
        if(a.command=="tx") {
            const auto samples=transfer::transmit(message(a),settings);
            output_wave(a,samples,c);
            const auto total_bytes=static_cast<std::size_t>(std::llround(static_cast<double>(samples.size())*modem::bit_rate(c)/(8.0*c.sample_rate)));
            const auto frame_bytes=total_bytes-modem::preamble(c).size();
            std::cerr<<"Transmitted "<<frame_bytes<<" frame bytes, "<<static_cast<double>(samples.size())/c.sample_rate
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
