#include "frequency_parse.hpp"
#include "datapump/audio.hpp"
#include "datapump/fast/cli.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/helper_filter.hpp"
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
#include "datapump/received_text.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "datapump/streaming_modem.hpp"
#include "transmit_timing.hpp"
#include "helper_power_reference.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
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
#include <numeric>
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
#define DATAPUMP_VERSION "001_00"
#endif
namespace {
const char* usage="Data Pump " DATAPUMP_VERSION R"HELP( — civilian audio text and file modem

Usage: pump COMMAND [OPTIONS]
  simulate     Free-running sampled channel and blind receiver acquisition
  listen       Continuous live receiver (or noise/loopback with --simulation)
  estimate     Exact airtime and relative LPI model (one-bit RX estimate)
  analyze-link Bounded statistical link analysis as JSON; no PCM or decoder
  tx           Encode text/file to WAV (--output) or live audio (--device)
  rx           Decode a WAV (--input) or record live audio (--device --seconds)
  keygen       Create an owner-only 128 MiB symmetric keyfile (--output)
  keys         List the named key sets in --keyfile
  devices      Enumerate local audio devices
  status-tx    Transmit exact few-bit callsign to WAV, without stream overhead
  status-rx    Discover raw pattern bits in WAV, then compare --bits; no MAC
  qr           Generate optical transfer QR Level L (--format svg|pbm)
  fast-info    Separate fast APSK profiles and fixed-interval geometry
  fast-tx      Stream encrypted files to WAV or audio (--help for fast options)
  fast-rx      Receive an encrypted fast WAV; physical absence required
  fast-listen  Receive an encrypted fast stream from live audio

Input/output:
  --text TEXT           Text to send (otherwise --input FILE or - for stdin)
  --input PATH          TX file or RX WAV (- means stdin)
  --kind text|file|screenshot  Default: text with --text/stdin, file with path
  --filename NAME       Display filename for file/screenshot (basename only)
  --output PATH         Explicit WAV/keyfile output; never overwrite
  --save PATH           Explicit save of received content or raw 0/1 text; never overwrite
  --json                Received content as JSON with restricted ASCII text and diagnostics
  --callsign TEXT --grid TEXT --repeatable

Received text and filenames use letters, digits, comma, period, @, space, -, _, /, =,
and newline (ASCII LF). JSON encodes newlines as \n.
Every other received byte is displayed as _. Use --save for original source bytes.

Modem:
  --bw HZ               Nominal bandwidth, 0.001..30000000 Hz; default1200
                        Decimal Hz and units such as 1.2kHz are accepted
  --sample-rate HZ      Internal real-stream clock, 64..120000000; covers bandwidth and Carrier minus Shift
  --carrier HZ          Absolute physical carrier; accepts Hz/kHz/MHz/GHz/THz; default max(1500,0.75*bw)
  --shift HZ            Oscillator translation; same units; default0; real USB stream = Carrier minus Shift
  --doppler VALUE       Static radial velocity in c/knots/mph/kph or frequency shift in %; default0.000000c
                        Positive velocity recedes; adjusts physical Carrier before unchanged Shift
  --spreading N         Manual 4-bit APSK chips/symbol, 1..16384 (disables auto)
  --target-snr DBHZ     Automatic target C/N0; default32, auto unless manual controls
  --receive-targets LIST Receive C/N0 search list, e.g. 40,6,-6; default32
                        Invalid lists reset to32; selected bandwidth/pattern stay fixed
  --pattern MODE        auto-keystream, auto-pattern, auto-tone, pattern-N, tone-N
  --scramble            Cryptographic pattern rotation (requires keyfile)
  --dsss                Legacy independent private mapper layer
  --dsss-factor N       Outer private spreading: 1 (off), 10, 100, 1000; requires keyfile
  --dsss-mode MODE      off or interleave; keeps a saved spreading factor while off
  --dsss-version NAME   interleave (default for outer DSSS) or legacy (diagnostic); peers must match
  --search-arithmetic MODE  Receiver policy: default, fp32-min, fp64-force (legacy IDs accepted)
                        Default selects demonstrated supported paths; FP64 fallbacks remain
                        Legacy integer IDs migrate to Default; no GPU receiver is implemented
  --receiver-strategy MODE  auto, existing, complex, amplitude (receiver only)
  --receiver-cpu-budget N  Estimated processing seconds per audio second (default 0.6)
  --sched-hop MODE     0, 3, 5 nominal total-time multiplier (requires keyfile)
  --sched-hop-dilution N  0 = one active hop, 3 = three consecutive active hops (developer control)
  --sched-frequency-search N  Frequency uncertainty multiplier 1, 2, 4, 8, 16, 32; active with Sched_Hop
  --symbol-burst MODE  off, 1, 2, 3, 5, 10 symbol waits; next starts on UTC 30s grid, at least 60s apart
  --symbol-burst-random N  0, 1, 2, 3 additional whole-symbol waits; effective zero without encryption
  --clock-sync SPEC     default or GPS_1ms-400ms_region-2564ms_offset
  --audio-error TIME    Per-station audio lag 0..+TIME; default or duration (70ms)
  --helper-power WATTS  Independent helper carrier power; default0 (disabled)
                        Numeric watts or W/kW/mW/uW, e.g. 1.5kW; tx/rx/status/listen/simulate
  --helper-data-power WATTS  Robust power for helper amplitude calibration; default4W
  --helper-ratio-limit DB  10, 20, 40, 60, 80, 90 (default); caps helper power
  --helper-frequency high  Helper auxiliary and pilot above Robust; only high supported
  --helper-callsign TEXT  Up to19 printable ASCII characters; defaults to --callsign
                        Ignored when encryption is selected; optional encrypted time instead
                        Empty: optional compact time; helper time is unauthenticated
  --helper-only         listen: transmit the helper without Robust data/noise
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
  --right-mono          TX on right channel; default left (mono devices use their sole channel)
  --no-mono             TX on both stereo channels
  --device-type audio   Analog audio input only; no network/raw serial input
  --seconds N           RX recording duration(default15); listen limit(default0)
  --tx-delay N          Delay after live TX completes; default6 seconds
  --snr DB              Simulator measured signal/noise power ratio; default20
  --simulation PRESET   e.g. "3dBm -120dB": TX power and channel attenuation
  --seed N --delay-samples N --frequency-offset HZ
  --oscillator MODEL    crystal (default), gpsdo-xo, gpsdo-tcxo, gpsdo-ocxo
                        LF/audio residual model; controls real RX search too
  --rf-oscillator MODEL Independent RF reference model; same choices, default crystal
  --rf-shift HZ         Alias for --shift
  --rf-carrier HZ       Alias for absolute --carrier; matching aliases may be combined
  --reference MODE      independent (default) or shared-radio (ADC/DAC and LO clock)
  --lf-reference 0      Shorthand for shared-radio; actual modem carrier stays nonzero
  --sideband upper      Optional compatibility setting; only real USB streams are supported
  --search-margin N     Static oscillator search bound multiplier, default 3; >=1
                        All oscillator presets are illustrative, with no hardware control
  --clock-error-ppm N   Override simulated signed clock error; search model stays fixed
  --phase-noise N       Override simulated diffusion, degrees/sqrt(second)
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
        const std::set<std::string> booleans={"json","repeatable","no-compression","no-mono","right-mono","scramble","dsss","progress","help","version","helper-only"};
        const std::set<std::string> valued={"text","input","output","save","kind","filename","callsign","grid",
            "bw","sample-rate","carrier","doppler","spreading","dsss-factor","dsss-version","dsss-mode","search-arithmetic","receiver-strategy","receiver-cpu-budget","sched-hop","sched-hop-dilution","sched-frequency-search","symbol-burst","symbol-burst-random","clock-sync","audio-error","fec","memory-mb","keyfile","pad","time","search-seconds",
            "device","device-type","seconds","tx-delay","snr","seed","delay-samples","frequency-offset","bits","format",
            "target-snr","receive-targets","pattern","simulation","oscillator","rf-oscillator","shift","rf-shift","rf-carrier","search-margin","reference","sideband","lf-reference","key-name","key-names","cache-mb","dsp-mb","clock-error-ppm","phase-noise","receiver-time",
            "recovery-seconds","recovery-threads","recovery-bits","recovery-errors",
            "helper-power","helper-data-power","helper-ratio-limit","helper-frequency","helper-callsign",
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
        if(has("no-mono")&&has("right-mono"))throw Error("--no-mono and --right-mono conflict");
    }
    bool has(const std::string& name)const{return values_.contains(name);}
    std::string get(const std::string& name,std::string fallback="")const {
        auto p=values_.find(name);return p==values_.end()?fallback:p->second;
    }
    double number(const std::string& name,double fallback) const {
        if(!has(name)) return fallback;
        const auto value=get(name);
        const bool frequency=name=="bw"||name=="carrier"||name=="shift"||name=="rf-shift"||name=="rf-carrier"||name=="lf-reference";
        if(frequency) {
            const auto parsed=frequency_input::parse(value);
            if(!parsed)throw Error("invalid finite frequency for --"+name+" (Hz, kHz, MHz, GHz or THz)");
            return *parsed;
        }
        std::size_t end=0;double result;
        try {result=std::stod(value,&end);}catch(...) {throw Error("invalid number for --"+name);}
        const auto suffix=value.substr(end);
        if(!suffix.empty()&&!(name=="search-margin"&&suffix=="x"))throw Error("invalid number for --"+name);
        if(!std::isfinite(result))throw Error("nonfinite number for --"+name);
        return result;
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
        if(command=="qr") reject({"keyfile","key-name","pad","scramble","dsss","dsss-factor","dsss-version","dsss-mode","repeatable","device"},"cannot be used with QR; QR contains plaintext input");
        if(command=="rx" || command=="status-rx") {
            reject({"output"},"is not a receive output; use --save PATH for decoded source bytes");
            reject({"text"},"is a transmit input; use --input for reception");
        }
        if(command!="rx") reject({"save"},"is only valid for rx");
        if(command!="tx" && command!="rx" && command!="status-tx" && command!="listen") reject({"device"},"is only valid for live audio commands");
        if(command!="keygen") reject({"key-names"},"is only valid for keygen");
        if(command!="simulate" && command!="listen" && command!="analyze-link") reject({"simulation"},"is only valid for simulate/listen/analyze-link");
        if(command!="simulate" && command!="listen" && command!="analyze-link") reject({"clock-error-ppm","phase-noise"},"is only valid for simulate/listen/analyze-link");
        if(command=="qr"||command=="keygen"||command=="devices")
            reject({"oscillator","rf-oscillator","shift","rf-shift","rf-carrier","doppler","search-margin","reference","sideband","lf-reference"},"requires a Robust modem command");
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
double helper_power(const Args& a,const std::string& name,double fallback) {
    if(!a.has(name))return fallback;
    auto value=a.get(name);
    std::erase_if(value,[](unsigned char ch){return std::isspace(ch)!=0;});
    double multiplier=1;
    if(value.ends_with("kW")){value.resize(value.size()-2);multiplier=1000;}
    else if(value.ends_with("mW")){value.resize(value.size()-2);multiplier=.001;}
    else if(value.ends_with("uW")){value.resize(value.size()-2);multiplier=.000001;}
    else if(value.ends_with("µW")||value.ends_with("μW")){value.resize(value.size()-3);multiplier=.000001;}
    else if(value.ends_with("W"))value.pop_back();
    std::size_t end=0;double watts=0;
    try {watts=std::stod(value,&end)*multiplier;}catch(...) {throw Error("invalid watts for --"+name);}
    if(end!=value.size()||!std::isfinite(watts)||watts<0)
        throw Error("--"+name+" requires finite nonnegative watts");
    return watts;
}
helper::Options helper_options(const Args& a,bool keyed) {
    helper::Options result;
    result.power_w=helper_power(a,"helper-power",0);
    result.data_power_w=helper_power(a,"helper-data-power",4);
    if(a.has("helper-ratio-limit")) {
        auto value=a.get("helper-ratio-limit");
        std::erase_if(value,[](unsigned char ch){return std::isspace(ch)!=0;});
        if(value.ends_with("dB"))value.resize(value.size()-2);
        std::size_t end=0;
        try {result.ratio_limit_db=std::stod(value,&end);}catch(...) {throw Error("invalid helper ratio limit");}
        if(end!=value.size())throw Error("invalid helper ratio limit");
    }
    if(result.ratio_limit_db!=10&&result.ratio_limit_db!=20&&result.ratio_limit_db!=40&&
       result.ratio_limit_db!=60&&result.ratio_limit_db!=80&&result.ratio_limit_db!=90)
        throw Error("--helper-ratio-limit must be 10, 20, 40, 60, 80, or 90 dB");
    const auto frequency=a.get("helper-frequency","high");
    if(frequency=="high")result.frequency=helper::Frequency::high;
    else throw Error("--helper-frequency supports only high; low and 0 Hz are no longer supported");
    result.callsign=keyed?"":a.get("helper-callsign",helper::effective_helper_power(result)>0?a.get("callsign"):"");
    if(result.callsign.size()>19||std::any_of(result.callsign.begin(),result.callsign.end(),
        [](unsigned char ch){return ch<32||ch>126;}))
        throw Error("helper carrier callsign must contain at most 19 printable ASCII characters");
    helper::validate(result);
    if(a.has("helper-only") && (a.command!="listen" || helper::effective_helper_power(result)<=0 || a.has("text") || a.has("input")))
        throw Error("--helper-only requires listen, Helper Carrier power above the Robust allocation, and no message input");
    if(helper::effective_helper_power(result)>0 && a.command!="tx" && a.command!="status-tx" && a.command!="listen" && a.command!="rx" && a.command!="status-rx" && a.command!="simulate")
        throw Error("enabled helper carrier is supported only by tx, status-tx, listen, rx, status-rx and simulate; use --helper-power 0 to disable it");
    return result;
}
tuning::OscillatorPreset oscillator_config(const Args& a) {
    auto result=tuning::parse_oscillator_preset(a.get("oscillator","crystal"));
    result.clock_error_ppm=a.number("clock-error-ppm",result.clock_error_ppm);
    result.phase_noise_degrees_per_sqrt_second=a.number("phase-noise",result.phase_noise_degrees_per_sqrt_second);
    return result;
}
modem::OscillatorSearchConfig oscillator_search_config(const Args& a) {
    if(tuning::parse_oscillator_preset(a.get("oscillator","crystal")).id=="ic-7100")throw Error("IC-7100 model is available for --rf-oscillator, not the unspecified audio clock");
    modem::OscillatorSearchConfig result;
    result.lf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.get("oscillator","crystal")));
    result.rf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.get("rf-oscillator","crystal")));
    result.margin=a.number("search-margin",3);
    const auto reference=a.get("reference","independent");
    if(reference!="independent"&&reference!="shared-radio")throw Error("reference must be independent or shared-radio");
    result.reference=reference=="shared-radio"?modem::OscillatorReference::shared_radio:modem::OscillatorReference::independent_audio;
    if(a.has("lf-reference")) {
        if(a.number("lf-reference",0)!=0)throw Error("lf-reference only accepts 0 Hz, indicating shared-radio clocks");
        if(a.has("reference")&&reference!="shared-radio")throw Error("lf-reference 0 conflicts with independent reference");
        result.reference=modem::OscillatorReference::shared_radio;
    }
    if(a.get("sideband","upper")!="upper")
        throw Error("only upper sideband is supported; Carrier minus Shift is the real stream frequency");
    result.sideband=modem::OscillatorSideband::upper;
    if(a.has("shift")&&a.has("rf-shift"))throw Error("choose shift or its rf-shift alias, not both");
    result.rf_shift_hz=a.number("shift",a.number("rf-shift",0));
    modem::validate_oscillator_search(result);
    return result;
}
void apply_oscillator_effects(const Args& args,const modem::Config& config,modem::ChannelConfig& channel) {
    const auto effects=modem::oscillator_effects(config);
    channel.clock_error_ppm=effects.clock_error_ppm;
    channel.frequency_offset_hz+=effects.frequency_offset_hz;
    channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    if(args.has("clock-error-ppm")) {
        const auto actual=args.number("clock-error-ppm",effects.clock_error_ppm);
        if(config.oscillator_search->reference==modem::OscillatorReference::shared_radio)
            channel.frequency_offset_hz+=(config.oscillator_search->sideband==modem::OscillatorSideband::upper?1.:-1.)*
                config.oscillator_search->rf_shift_hz*(actual-effects.clock_error_ppm)*1e-6;
        channel.clock_error_ppm=actual;
    }
    channel.phase_noise_degrees_per_sqrt_second=args.number("phase-noise",channel.phase_noise_degrees_per_sqrt_second);
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
                      a.has("sample-rate");
    return a.has("target-snr") || a.has("pattern") || !manual;
}
modem::Config config(const Args& a) {
    modem::Config c;
    c.bandwidth_hz=a.number("bw",1200);
    const auto dsss_mode=a.get("dsss-mode","");
    if(!dsss_mode.empty()&&dsss_mode!="off"&&dsss_mode!="interleave")throw Error("dsss-mode must be off or interleave");
    c.dsss_factor=static_cast<unsigned>(a.integer("dsss-factor",dsss_mode=="interleave"?10:1));
    if(c.dsss_factor!=1&&c.dsss_factor!=10&&c.dsss_factor!=100&&c.dsss_factor!=1000)throw Error("dsss-factor must be 1, 10, 100, or 1000");
    const auto dsss_version=tuning::parse_outer_dsss_version(a.get("dsss-version",c.dsss_factor>1?"interleave":"legacy"));
    if(dsss_mode=="interleave"&&(c.dsss_factor==1||dsss_version==modem::OuterDsssVersion::legacy_v1))
        throw Error("Interleave mode requires a spreading factor above one and the interleave construction");
    if(dsss_mode=="off")c.dsss_factor=1;
    c.search_arithmetic=tuning::parse_search_arithmetic(a.get("search-arithmetic","default"));
    c.outer_dsss_version=c.dsss_factor>1?dsss_version:modem::OuterDsssVersion::legacy_v1;
    const auto oscillator=oscillator_search_config(a);
    const auto carrier=a.number("carrier",a.number("rf-carrier",tuning::recommended_carrier_hz(modem::waveform_bandwidth_hz(c))));
    if(a.has("carrier")&&a.has("rf-carrier")&&carrier!=a.number("rf-carrier",0))
        throw Error("carrier and rf-carrier must specify the same absolute frequency");
    const auto adjusted_carrier=tuning::doppler_carrier_hz(carrier,tuning::parse_doppler(a.get("doppler","0.000000c")));
    const auto stream=static_cast<long double>(adjusted_carrier)-oscillator.rf_shift_hz;
    if(!std::isfinite(carrier)||carrier<=0||!std::isfinite(stream)||stream<=0||stream>30000000)
        throw Error("Doppler-adjusted Carrier minus Shift must be positive and at most 30 MHz for the real USB stream");
    c.carrier_hz=static_cast<double>(stream);
    const bool automatic=automatic_tuning(a);
    if(automatic) {
        if(a.has("spreading") || a.has("scramble") || a.has("dsss") || a.has("sample-rate"))
            throw Error("automatic tuning cannot be combined with manual spreading/scramble/dsss/sample-rate");
        const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",32),
            tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"),c.carrier_hz,c.dsss_factor,c.outer_dsss_version,c.search_arithmetic);
        c=plan.config;
        if(a.has("progress") || !plan.target_supported) std::cerr<<plan.explanation<<'\n';
    }
    c.receiver_strategy=tuning::parse_receiver_strategy(a.get("receiver-strategy","auto"));
    c.receiver_cpu_budget=a.number("receiver-cpu-budget",.6);
    tuning::validate_receiver_cpu_budget(c.receiver_cpu_budget);
    c.symbol_burst={modem::parse_symbol_burst_wait(a.get("symbol-burst","off")),
        modem::parse_symbol_burst_random(a.get("symbol-burst-random","0"))};
    const auto burst_multiplier=modem::sched_hop_multiplier(a.get("sched-hop","0"));
    const auto dilution=modem::sched_hop_active_hops(a.get("sched-hop-dilution","0"));
    if(burst_multiplier>1&&dilution>burst_multiplier)
        throw Error("sched-hop-dilution active count cannot exceed its multiplier");
    const auto frequency_multiplier=a.number("sched-frequency-search",1);
    if(frequency_multiplier!=1&&frequency_multiplier!=2&&frequency_multiplier!=4&&frequency_multiplier!=8&&
       frequency_multiplier!=16&&frequency_multiplier!=32)throw Error("sched-frequency-search must be 1, 2, 4, 8, 16, or 32");
    if(burst_multiplier>1) {
        if(c.symbol_burst.enabled())throw Error("Symbol_Burst and Sched_Hop cannot be enabled together");
        if(!a.has("keyfile")||c.spreading_mode==modem::SpreadingMode::tone)
            throw Error("scheduled hopping requires an encrypted pattern and --keyfile");
        modem::SchedHopPlan plan{burst_multiplier,dilution};
        const auto policy=clock_sync::parse(a.get("clock-sync","default"));
        plan.clock_error_seconds=plan.peer_clock_error_seconds=policy?policy->accuracy_seconds:.01;
        plan.region_seconds=policy?policy->region_seconds:0;
        plan.offset_seconds=policy?policy->offset_seconds:0;
        plan.audio_error_seconds=clock_sync::parse_audio_error(a.get("audio-error","default"));
        plan.validate();c.scheduled_hopping=std::make_shared<const modem::SchedHopPlan>(plan);
        c.frequency_search_multiplier=frequency_multiplier;
    }
    auto rate=a.integer("sample-rate",automatic?c.sample_rate:tuning::recommended_sample_rate(modem::waveform_bandwidth_hz(c),c.carrier_hz));
    if(rate>120000000) throw Error("internal sample rate exceeds120000000");
    c.sample_rate=static_cast<std::uint32_t>(rate);
    c.oscillator_search=oscillator;
    auto spreading=a.integer("spreading",c.spreading_factor);
    if(spreading==0 || spreading>16384) throw Error("spreading must be1..16384");
    c.spreading_factor=static_cast<unsigned>(spreading);
    c.memory_limit=budget(a);
    if(!automatic) {c.scramble=a.has("scramble")||c.dsss_factor>1;c.dsss=a.has("dsss");}
    if((a.has("scramble") || a.has("dsss") || c.dsss_factor>1) && !a.has("keyfile")) throw Error("encrypted spreading requires --keyfile");
    if(a.get("device-type","audio")!="audio") throw Error("this build supports analog audio only; SDR and IC-7100 frontends are not implemented");
    if(a.has("device") && modem::waveform_bandwidth_hz(c)>192000)
        throw Error("this bandwidth requires an SDR frontend; use simulation in this audio-only build");
    modem::validate(c);return c;
}
audio::StreamFormatCallback audio_passband_guard(const modem::Config& config) {
    const auto upper_edge=config.carrier_hz+modem::waveform_bandwidth_hz(config)/2;
    return [upper_edge](const audio::StreamFormat& format) {
        if(upper_edge>format.usable_passband_hz)
            throw Error("Selected upper band edge ("+std::to_string(upper_edge)+
                        " Hz) exceeds this audio path's usable passband ("+
                        std::to_string(format.usable_passband_hz)+
                        " Hz). Choose a narrower band, a wider audio device, or simulation.");
    };
}
long double helper_source_origin(const modem::Config& c,std::uint64_t timestamp,
                                const modem::StreamingTransmitter* source=nullptr) {
    if(c.symbol_burst.enabled() || (c.scheduled_hopping&&c.scheduled_hopping->enabled()&&!c.scheduled_hopping->continuous())) {
        if(!source)throw Error("helper carrier requires the prepared physical transmit origin");
        const auto physical=source->physical_output_origin();
        return (static_cast<long double>(physical/c.sample_rate)-timestamp)+
            static_cast<long double>(physical%c.sample_rate)/c.sample_rate;
    }
    return (static_cast<long double>(c.stream_phase_samples)-modem::training_sample_count(c)-
        modem::pattern_pulse_padding_samples(c))/c.sample_rate;
}
std::uint64_t helper_sample_count(long double seconds,std::uint32_t rate) {
    const auto count=std::ceil(seconds*rate);
    if(!std::isfinite(count)||count<0||count>=std::ldexp(1.L,64))
        throw Error("Helper Carrier envelope exceeds the sample range");
    return static_cast<std::uint64_t>(count);
}
std::uint64_t add_helper_samples(std::uint64_t first,std::uint64_t second) {
    if(first>UINT64_MAX-second)throw Error("Helper Carrier output sample count overflow");
    return first+second;
}
std::uint64_t helper_resampled_count(std::uint64_t samples,std::uint32_t source_rate,std::uint32_t output_rate) {
    const auto whole=samples/source_rate;
    if(whole>UINT64_MAX/output_rate)throw Error("Helper Carrier output sample count overflow");
    return add_helper_samples(whole*output_rate,
        ((samples%source_rate)*output_rate+source_rate-1)/source_rate);
}
long double helper_dwell_boundary(std::uint64_t epoch,long double seconds,bool upward) {
    auto dwells=(static_cast<long double>(epoch%2)+seconds)*2.5L;
    const auto nearest=std::round(dwells);
    if(std::abs(dwells-nearest)<=8*std::numeric_limits<long double>::epsilon()*std::max(1.L,std::abs(dwells)))
        dwells=nearest;
    return (upward?std::ceil(dwells):std::floor(dwells))*.4L-static_cast<long double>(epoch%2);
}
helper::EnvelopePlan helper_output_envelope(const modem::Config& config,const helper::Options& options,
        double helper_frequency,helper::EnvelopeRequirements requirements={}) {
    if(options.frequency!=helper::Frequency::cochannel && helper_frequency>225 &&
       helper_frequency+225<config.sample_rate/2.)try {
        const helper::PayloadFilter filter(config.sample_rate,helper_frequency);
        requirements.frontend_lookahead_seconds=std::max(requirements.frontend_lookahead_seconds,
            static_cast<double>(filter.delay())/config.sample_rate);
    }catch(const Error&) {
        // The optional CLI filter also yields to the full original receive
        // path when its geometry/workspace is unsupported.
    }
    return helper::plan_envelope(config,requirements);
}
template<class Factory>
void play_transmission(const Args& a, transfer::Options& options,const helper::Options& helper_settings, Factory make) {
    std::unique_ptr<modem::StreamingTransmitter> source;
    bool protected_stop=false;
    std::uint64_t provided_samples=0,leading_samples=0;
    helper::EnvelopePlan envelope;
    const auto helper_lead=[&] {return static_cast<long double>(leading_samples)/options.modem.sample_rate;};
    const auto prepare_envelope=[&] {
        if(helper::effective_helper_power(helper_settings)<=0)return;
        const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
        const auto start=helper_dwell_boundary(options.timestamp,origin-envelope.lead_seconds,false);
        leading_samples=helper_sample_count(origin-start,options.modem.sample_rate);
    };
    const auto helper_end=[&] {
        const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
        const auto end=helper_dwell_boundary(options.timestamp,
            origin+static_cast<long double>(source->total_samples())/options.modem.sample_rate+envelope.tail_seconds,true);
        return helper::protected_endpoint(options.timestamp,origin-helper_lead(),end);
    };
    const auto padded_source_end=[&] {
        if(!source)return std::uint64_t{0};
        if(helper::effective_helper_power(helper_settings)<=0)return source->total_samples();
        const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
        return std::max(add_helper_samples(leading_samples,source->total_samples()),
            helper_sample_count(helper_end()-origin+helper_lead(),options.modem.sample_rate));
    };
    audio::Options output;
    if(helper::effective_helper_power(helper_settings)>0)output.minimum_hardware_rate=helper::minimum_output_rate(
        helper_settings,helper::placement_band(options.modem),options.modem.sample_rate);
    std::optional<helper::Generator> helper_generator;
    if(helper::effective_helper_power(helper_settings)>0)output.playback_output_transform=[&](float payload,long double seconds,std::uint32_t rate) {
        if(!helper_generator||!source)throw Error("helper carrier output was not prepared");
        const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
        const auto start=origin-helper_lead();
        return helper_generator->mix(payload,start+seconds,rate,helper_end(),start);
    };
    const auto clock_policy=transfer::effective_clock_sync(options);
    output.maximum_utc_error_seconds=clock_sync::audio_lag_radius(options.audio_timing_error_seconds);
    output.presentation_lag_seconds=clock_sync::audio_lag_radius(options.audio_timing_error_seconds);
    auto timing_config=options.modem;
    if(options.key)timing_config.scramble=true;
    detail::configure_transmit_timing_protection(output,timing_config,[&]{return source.get();});
    if(helper::effective_helper_power(helper_settings)>0) {
        const auto placement=helper::plan(helper_settings,helper::placement_band(timing_config),120000000,0,
            modem::outer_dsss_transmit_gain(timing_config));
        helper::EnvelopeRequirements requirements;
        if(timing_config.oscillator_search) {
            const auto search=modem::oscillator_pattern_search(timing_config,
                clock_policy?modem::utc_transmit_rate_limit(timing_config):0);
            requirements.maximum_source_capture_ratio=1+search.requested_clock_half_width_ppm*1e-6;
        }
        envelope=helper_output_envelope(timing_config,helper_settings,placement.frequency_hz,requirements);
        output.timing_stop_duration_seconds+=static_cast<double>(envelope.maximum_stop_extension_seconds);
        output.timing_independent_cover_seconds+=static_cast<double>(envelope.tail_seconds);
        output.timing_protected_payload_end=[&](std::uint64_t frontier)->std::optional<std::uint64_t> {
            if(!source)return {};
            const auto physical=frontier>leading_samples?frontier-leading_samples:0;
            return add_helper_samples(leading_samples,source->predicted_physical_valid_payload_end(physical));
        };
    }
    output.timing_source_end=[&]() -> std::optional<std::uint64_t> {
        return source?std::optional<std::uint64_t>(padded_source_end()):std::nullopt;
    };
    if(timing_config.fake_hopping && timing_config.fake_hopping->smooth_ramps) {
        const modem::FhssPowerShape shape(*timing_config.fake_hopping,timing_config.sample_rate,
            modem::pattern_pulse_padding_samples(timing_config),modem::symbol_sample_count(timing_config));
        output.playback_output_gain=[&,shape](long double seconds) {
            seconds-=helper_lead();
            const auto& c=options.modem;
            const auto origin=helper::effective_helper_power(helper_settings)>0?
                helper_source_origin(c,options.timestamp,source.get())*c.sample_rate:
                (c.symbol_burst.enabled() && source?
                    static_cast<long double>(source->physical_output_origin())-static_cast<long double>(options.timestamp)*c.sample_rate:
                    static_cast<long double>(c.stream_phase_samples)-modem::training_sample_count(c)-modem::pattern_pulse_padding_samples(c));
            return shape.gain(options.timestamp,origin+seconds*c.sample_rate,c.sample_rate);
        };
    }
    output.follow_system_clock=clock_policy && audio::utc_follow_supported() &&
        (!options.modem.oscillator_search || options.modem.oscillator_search->reference==modem::OscillatorReference::independent_audio);
    if(output.follow_system_clock && options.modem.oscillator_search) {
        const auto& declared=*options.modem.oscillator_search;
        const auto& reference=declared.reference==modem::OscillatorReference::shared_radio && declared.rf_shift_hz>0?
            declared.rf:declared.lf;
        // Operational consistency declaration, not a per-device calibration.
        // Receiver link-domain/margin accounting remains unchanged.
        output.clock_timing=audio::ClockTimingPolicy{reference.accuracy_ppm*1e-6,.001,clock_policy->accuracy_seconds};
    }
    output.allow_timing_fallback=clock_policy.has_value();
    if(output.follow_system_clock) {
        const auto symbol_seconds=static_cast<double>(modem::symbol_sample_count(timing_config))/timing_config.sample_rate;
        output.reported_xrun_budget_seconds=std::min(.1,.01*symbol_seconds);
        output.reported_xrun_window_seconds=symbol_seconds;
    }
    output.timing_status=[](const audio::TimingStatus& status) {
        std::cerr<<(status.following_system_clock?"UTC timing: ":"UTC timing fallback: ")<<status.reason<<"\n";
    };
    if(clock_policy && a.has("time"))throw Error("UTC hardware transmission requires a freshly scheduled epoch");
    if(output.follow_system_clock && options.modem.oscillator_search) {
        const auto bank=modem::oscillator_pattern_search(options.modem,modem::utc_transmit_rate_limit(options.modem));
        output.maximum_timing_rate_correction=bank.transmit_rate_correction;
        if(output.maximum_timing_rate_correction>0) {
            options.modem.stream_phase_samples=0;
            output.estimated_timing=audio::EstimatedDeviceTiming{};
            output.estimated_timing->maximum_utc_error_seconds=clock_sync::audio_lag_radius(options.audio_timing_error_seconds);
            output.maximum_timing_slew_per_second=audio::conservative_timing_slew(options.modem.sample_rate,
                static_cast<double>(modem::symbol_sample_count(options.modem))/options.modem.sample_rate);
        }
    }
    output.timing_protected_stop=[&](const std::string& reason) {
        protected_stop=true;std::cerr<<reason<<"\n";if(source)source->request_finish();
    };
    const auto clock=[] {return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
    const auto schedule_helper_output=[&] {
        const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
        const auto playback=static_cast<long double>(options.timestamp)+origin-helper_lead();
        if(static_cast<long double>(clock())+audio::minimum_lead_seconds()>=playback)
            throw Error("Helper Carrier lead cannot precede the explicit Robust timestamp; disable the helper or choose a future --time");
        audio::schedule_output(static_cast<double>(playback));
    };
    if(a.has("time")) {
        source=make(options);prepare_envelope();
        if(helper::effective_helper_power(helper_settings)>0) {
            const auto origin=helper_source_origin(options.modem,options.timestamp,source.get());
            if(static_cast<long double>(clock())>=static_cast<long double>(options.timestamp)+origin-helper_lead())
                throw Error("Helper Carrier lead cannot precede the explicit Robust timestamp; disable the helper or choose a future --time");
        }
    }
    audio::playback(options.modem.sample_rate,a.get("device"),[&](std::span<float> chunk) {
        if(helper::effective_helper_power(helper_settings)<=0)return source->read(chunk);
        std::size_t count=static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(),
            leading_samples>provided_samples?leading_samples-provided_samples:0));
        std::fill_n(chunk.begin(),count,0.f);
        if(count<chunk.size())count+=source->read(chunk.subspan(count));
        provided_samples=add_helper_samples(provided_samples,count);
        if(helper::effective_helper_power(helper_settings)>0 && source->finished()) {
            const auto end=padded_source_end();
            const auto padding=static_cast<std::size_t>(std::min<std::uint64_t>(
                chunk.size()-count,end>provided_samples?end-provided_samples:0));
            std::fill_n(chunk.begin()+static_cast<std::ptrdiff_t>(count),padding,0.f);
            count+=padding;provided_samples=add_helper_samples(provided_samples,padding);
        }
        return count;
    },{},[&](const auto& format) {
        audio_passband_guard(options.modem)(format);
        std::optional<helper::Plan> helper_plan;
        if(helper::effective_helper_power(helper_settings)>0) {
            helper_plan=helper::plan(helper_settings,helper::placement_band(options.modem),
                format.hardware_rate,0,modem::outer_dsss_transmit_gain(options.modem));
        }
        if(!a.has("time")) {
            auto scheduled=detail::schedule_transfer_transmission(options,make,clock,{},0,
                audio::minimum_lead_seconds()+static_cast<double>(envelope.preparation_seconds));
            source=std::move(scheduled.transmitter);
            prepare_envelope();
            audio::schedule_output(scheduled.playback_epoch-static_cast<double>(helper_lead()));
        } else if(helper::effective_helper_power(helper_settings)>0)schedule_helper_output();
        if(helper_plan) {
            auto reference=helper::transmit_power_reference(options.modem,*source,
                options.timestamp,options.timestamp,options.key);
            reference.helper_start_seconds=helper_source_origin(options.modem,options.timestamp,source.get())-helper_lead();
            reference.helper_end_seconds=helper_end();
            helper_plan=helper::plan(helper_settings,helper::placement_band(options.modem),
                format.hardware_rate,0,modem::outer_dsss_transmit_gain(options.modem),&reference);
            helper_generator.emplace(helper_settings,*helper_plan,options.timestamp,true,options.key);
        }
    },a.has("no-mono")?audio::ChannelMode::stereo:a.has("right-mono")?audio::ChannelMode::right_mono:audio::ChannelMode::left_mono,output);
    if(protected_stop)throw Error("transmission truncated by UTC timing protection; cover and device drain completed");
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
    if(a.has("clock-sync"))options.clock_sync=clock_sync::parse(a.get("clock-sync"));
    if(a.has("audio-error"))options.audio_timing_error_seconds=clock_sync::parse_audio_error(a.get("audio-error"));
    clock_sync::validate_audio_error(options.audio_timing_error_seconds);
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
struct HelperCapture {
    bool present=false;
    std::optional<helper::Observation> observation;
    std::optional<helper::Reference> reference;
    std::uint64_t payload_origin_samples=0;
    bool payload_group_filtered=false;
};
// Optional diagnostic metadata never supplies pilot or payload authority.
class HelperMetadataReceiver {
    helper::Plan plan_;
    std::vector<Crypto> keys_;
    helper::FrequencyPrecisionOptions precision_;
    std::size_t grant_;
    std::optional<helper::AuxiliaryMetadataReceiver> receiver_;
    std::uint32_t rate_=0;
    std::uint64_t input_next_=0;
    bool unavailable_=false;
public:
    HelperMetadataReceiver(const helper::Plan& plan,std::vector<Crypto> keys,
                           helper::FrequencyPrecisionOptions precision,std::size_t grant=512*1024)
        :plan_(plan),keys_(std::move(keys)),precision_(precision),grant_(grant) {}
    void push(std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
        if(samples.size()>UINT64_MAX-first)throw Error("Helper metadata capture coordinate overflow");
        if(rate_!=rate||input_next_!=first) {receiver_.reset();unavailable_=false;rate_=rate;}
        if(!receiver_&&!unavailable_) {
            try {
                const auto required=helper::AuxiliaryMetadataReceiver::workspace_required(rate);
                if(required>grant_)unavailable_=true;
                else receiver_.emplace(rate,plan_.auxiliary_frequency_hz,plan_.frequency_hz,
                    keys_,precision_,required,first);
            }catch(const std::exception&) {unavailable_=true;}
        }
        if(receiver_)for(std::size_t offset=0;offset<samples.size();offset+=4096) {
            receiver_->push(samples.subspan(offset,std::min<std::size_t>(4096,samples.size()-offset)),first+offset);
            receiver_->advance();
        }
        input_next_=first+samples.size();
    }
    void limit_workspace(std::size_t available) {
        grant_=std::min(grant_,available);
        if(receiver_&&helper::AuxiliaryMetadataReceiver::workspace_required(rate_)>grant_) {
            receiver_.reset();unavailable_=true;
        }
    }
    std::optional<helper::Reference> take_update() {return receiver_?receiver_->take_update():std::nullopt;}
};
void observe_helper_capture(HelperCapture& capture,helper::Tracker& tracker,HelperMetadataReceiver& receiver,
                            std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
    tracker.push(samples,rate);
    if(auto update=tracker.take_update();update&&update->present) {
        capture.present=true;capture.observation=std::move(update);
    }
    receiver.push(samples,rate,first);
    if(auto update=receiver.take_update();update&&
        (update->metadata||!capture.reference||!capture.reference->metadata))capture.reference=std::move(update);
}
void report_helper_capture_json(const HelperCapture& helper_capture) {
    std::cout<<",\"helper_carrier\":{\"present\":"<<(helper_capture.present?"true":"false")
        <<",\"frequency_hz\":";
    if(helper_capture.observation)std::cout<<helper_capture.observation->frequency_hz;
    else std::cout<<"null";
    std::cout<<",\"metadata\":";
    if(helper_capture.reference&&helper_capture.reference->metadata) {
        const auto& metadata=*helper_capture.reference->metadata;
        std::cout<<"{\"kind\":\""<<(metadata.kind==helper::MetadataKind::callsign?"callsign":
            metadata.kind==helper::MetadataKind::clear_time?"clear_time":"encrypted_time")
            <<"\",\"authenticated\":false";
        if(metadata.kind==helper::MetadataKind::callsign)
            std::cout<<",\"callsign\":\""<<json_escape(received_text(metadata.callsign))<<'"';
        else std::cout<<",\"time_ticks\":"<<metadata.time_ticks;
        std::cout<<'}';
    } else std::cout<<"null";
    std::cout<<'}';
}
template<class Receive>
auto receive_with_helper_diagnostics(const Args& args,const std::optional<HelperCapture>& helper_capture,
                                     Receive&& receive) {
    try {return receive();}
    catch(const Error& error) {
        // A beacon remains observable when the separate payload has no usable
        // bits or its key is missing. Preserve the payload error/exit status.
        if(helper_capture && helper_capture->present) {
            if(args.command=="status-rx" || args.has("json")) {
                std::cout<<"{\"event\":\"helper_carrier\",\"content_validated\":false,\"stream_complete\":false,\"error\":\""
                         <<json_escape(received_text(error.what()))<<'"';
                report_helper_capture_json(*helper_capture);std::cout<<"}\n";
            } else {
                std::cerr<<"Helper carrier received; Robust payload unavailable\n";
            }
        }
        throw;
    }
}
transfer::Received receive_helper_payload(std::span<const float> samples,const transfer::Options& options,
        const helper::Options& helper_options,const std::optional<HelperCapture>& capture,
        const transfer::Progress& progress,std::size_t other_waveform_bytes=0) {
    auto receive_options=options;
    if(capture&&capture->payload_group_filtered) {
        if(capture->payload_origin_samples>UINT64_MAX-receive_options.input_sample_origin_samples)
            throw Error("Helper payload origin exceeds the receive sample counter");
        receive_options.input_sample_origin_samples+=capture->payload_origin_samples;
    }
    if(helper::effective_helper_power(helper_options)>0) {
        // A helper capture can contain a lead before the original keyed
        // source. Cover that arrival range without changing key epochs or
        // assuming the lead's exact start, a timecode, or a payload length.
        const auto envelope=helper::plan_envelope(options.modem);
        receive_options.capture_start_uncertainty_seconds=std::max(
            receive_options.capture_start_uncertainty_seconds,
            static_cast<double>(envelope.preparation_seconds));
    }
    if(helper::effective_helper_power(helper_options)>0 && helper_options.frequency!=helper::Frequency::cochannel &&
       capture && capture->present && !capture->payload_group_filtered)try {
        const auto frequency=capture->observation?capture->observation->frequency_hz:0.;
        const auto center=frequency-helper::auxiliary_pilot_spacing_hz/2;
        if(center>425 && center+425<options.modem.sample_rate/2.) {
            helper::PayloadFilter filter(options.modem.sample_rate,center,425);
            const auto margin=2*filter.delay();
            const auto budget=options.modem.memory_limit;
            const auto raw_bytes=samples.size_bytes();
            std::array<float,4096> block{};
            const auto auxiliary_bytes=filter.allocated_bytes()+sizeof(block);
            if(samples.size()>margin && raw_bytes<=budget && other_waveform_bytes<=budget-raw_bytes &&
               auxiliary_bytes<=budget-raw_bytes-other_waveform_bytes &&
               samples.size()-margin<=(budget-raw_bytes-other_waveform_bytes-auxiliary_bytes)/sizeof(float) &&
               filter.delay()<=UINT64_MAX-options.input_sample_origin_samples) {
                std::vector<float> selected;selected.reserve(samples.size()-margin);
                if(selected.capacity()<=(budget-raw_bytes-other_waveform_bytes-auxiliary_bytes)/sizeof(float)) {
                    for(std::size_t first=0;first<samples.size();first+=block.size()) {
                        const auto count=filter.push(samples.subspan(first,std::min(block.size(),samples.size()-first)),block);
                        selected.insert(selected.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
                    }
                    // Only completely observed FIR windows are returned. Their
                    // centers retain the original capture coordinate; EOF supplies
                    // no filter padding and no absent-symbol evidence.
                    auto filtered_options=receive_options;
                    filtered_options.input_sample_origin_samples+=filter.delay();
                    auto received=transfer::receive(selected,filtered_options,progress);
                    if(received.observed_bits)return received;
                }
            }
        }
    }catch(const std::exception&) {
        // Optional conditioning yields to the complete original receiver. Its
        // result/error remains authoritative when conditioning accepts no bits.
    }
    return transfer::receive(samples,receive_options,progress);
}
helper::FrequencyPrecisionOptions helper_precision_options(const modem::Config& config) {
    helper::FrequencyPrecisionOptions result;
    if(config.oscillator_search)result.phase_noise_radians_per_sqrt_second=
        modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second*std::numbers::pi/180;
    return result;
}
HelperCapture preselect_helper_wav(modem::Wav& wav,const modem::Config& config,const helper::Options& options,
                                   const std::optional<Crypto>& key={}) {
    const auto plan=helper::plan(options,helper::placement_band(config),
        wav.sample_rate,0,modem::outer_dsss_transmit_gain(config));
    HelperCapture helper_capture;
    helper::Tracker tracker(plan.frequency_hz);
    std::vector<Crypto> keys;if(key)keys.push_back(*key);
    const auto raw_bytes=wav.samples.capacity()*sizeof(float);
    const auto metadata_grant=raw_bytes<config.memory_limit?
        std::min<std::size_t>(512*1024,config.memory_limit-raw_bytes):0;
    HelperMetadataReceiver receiver(plan,std::move(keys),helper_precision_options(config),metadata_grant);
    for(std::size_t first=0;first<wav.samples.size();first+=4096)
        observe_helper_capture(helper_capture,tracker,receiver,
            std::span(wav.samples).subspan(first,std::min<std::size_t>(4096,wav.samples.size()-first)),
            wav.sample_rate,first);
    if(helper_capture.present&&helper_capture.observation) {
        // Reject the complete group in its physical PCM clock, before rate
        // conversion can alias or discard either tone into Robust's band.
        helper::PayloadFilter filter(wav.sample_rate,
            helper_capture.observation->frequency_hz-helper::auxiliary_pilot_spacing_hz/2,425);
        const auto quantum=wav.sample_rate/std::gcd(wav.sample_rate,config.sample_rate);
        const auto skip=(quantum-filter.delay()%quantum)%quantum;
        const auto physical_origin=skip+filter.delay();
        if(physical_origin%quantum)throw Error("Helper physical filter center is off the logical sample grid");
        helper_capture.payload_origin_samples=physical_origin/wav.sample_rate*config.sample_rate+
            (physical_origin%wav.sample_rate)*config.sample_rate/wav.sample_rate;
        std::array<float,4096> block{};
        std::size_t written=0;
        for(std::size_t first=skip;first<wav.samples.size();first+=block.size()) {
            const auto count=filter.push(std::span<const float>(wav.samples).subspan(first,
                std::min(block.size(),wav.samples.size()-first)),block);
            std::copy_n(block.begin(),count,wav.samples.begin()+static_cast<std::ptrdiff_t>(written));
            written+=count;
        }
        // No padding or filter flush: retain only fully observed FIR windows.
        wav.samples.resize(written);helper_capture.payload_group_filtered=true;
    }
    audio::Resampler converter(wav.sample_rate,config.sample_rate);
    std::vector<float> selected;
    const auto count=(static_cast<std::uint64_t>(wav.samples.size())*config.sample_rate+wav.sample_rate-1)/wav.sample_rate;
    if(count>config.memory_limit/sizeof(float))throw Error("helper receive conversion exceeds waveform memory limit");
    selected.reserve(static_cast<std::size_t>(count));
    std::array<float,4096> output{};
    std::size_t offset=0;
    while(!converter.finished()) {
        const auto available=std::min<std::size_t>(4096,wav.samples.size()-offset);
        const auto result=converter.process(std::span(wav.samples).subspan(offset,available),output,
            offset+available==wav.samples.size());
        if(!result.consumed&&!result.produced&&!converter.finished())throw Error("helper receive resampler stalled");
        offset+=result.consumed;
        selected.insert(selected.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(result.produced));
    }
    wav.samples=std::move(selected);wav.sample_rate=config.sample_rate;
    return helper_capture;
}
double hardware_delay(const Args& a,const modem::Config& c) {
    const auto requested=a.number("tx-delay",6);
    if(!std::isfinite(requested) || requested<6 || requested>3600)
        throw Error("tx-delay must be 6..3600 seconds");
    return std::max(requested,static_cast<double>(modem::pattern_absence_capture_samples(c))/c.sample_rate+1.);
}
void output_wave(const Args& a,std::vector<float> samples,const modem::Config& c,bool add_tail=true,
                 const helper::Options& helper_settings={},std::uint64_t helper_epoch=0,
                 long double helper_origin=0,const std::optional<Crypto>& helper_time_key={},
                 const helper::PowerReference* helper_reference=nullptr) {
    auto output_rate=c.sample_rate;
    if(helper::effective_helper_power(helper_settings)>0) {
        const auto physical_rate=std::max<std::uint64_t>(48000,4ULL*c.sample_rate);
        if(physical_rate>120000000)throw Error("helper WAV requires a separate supported physical sample rate");
        output_rate=static_cast<std::uint32_t>(physical_rate);
        const auto plan=helper::plan(helper_settings,helper::placement_band(c),output_rate,0,
            modem::outer_dsss_transmit_gain(c),helper_reference);
        const auto envelope=helper_output_envelope(c,helper_settings,plan.frequency_hz);
        const auto start_boundary=helper_dwell_boundary(helper_epoch,helper_origin-envelope.lead_seconds,false);
        const auto leading=helper_sample_count(helper_origin-start_boundary,output_rate);
        // Compose the public helper at the physical rate. Only the original
        // Robust samples enter the resampler, retaining their source origin.
        const auto helper_start=helper_origin-static_cast<long double>(leading)/output_rate;
        const auto helper_end=helper::protected_endpoint(helper_epoch,helper_start,
            helper_dwell_boundary(helper_epoch,helper_origin+
                static_cast<long double>(samples.size())/c.sample_rate+envelope.tail_seconds,true));
        const auto count=helper_sample_count(helper_end-helper_start,output_rate);
        const auto payload_count=helper_resampled_count(samples.size(),c.sample_rate,output_rate);
        if(count<add_helper_samples(leading,payload_count))throw Error("Helper Carrier envelope ends before the Robust waveform");
        if(count>c.memory_limit/sizeof(float))throw Error("helper WAV exceeds the configured waveform memory limit");
        const helper::Generator generator(helper_settings,plan,helper_epoch,true,helper_time_key);
        audio::Resampler converter(c.sample_rate,output_rate);
        converter.set_output_transform([&](float payload,long double seconds,std::uint32_t rate) {
            return generator.mix(payload,helper_origin+seconds,rate,helper_end,helper_start);
        });
        std::vector<float> mixed; mixed.reserve(static_cast<std::size_t>(count));
        for(std::uint64_t i=0;i<leading;++i)
            mixed.push_back(generator.mix(0,helper_start+static_cast<long double>(i)/output_rate,
                output_rate,helper_end,helper_start));
        std::array<float,4096> block{};
        std::size_t offset=0;
        while(!converter.finished()) {
            const auto available=std::min<std::size_t>(4096,samples.size()-offset);
            const auto progress=converter.process(std::span(samples).subspan(offset,available),block,
                offset+available==samples.size());
            if(!progress.consumed&&!progress.produced&&!converter.finished())throw Error("helper WAV resampler stalled");
            offset+=progress.consumed;
            mixed.insert(mixed.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(progress.produced));
        }
        if(mixed.size()!=add_helper_samples(leading,payload_count))throw Error("Helper Carrier Robust resampler duration changed");
        const auto post=count-mixed.size();
        while(mixed.size()<count)
            mixed.push_back(generator.mix(0,helper_start+static_cast<long double>(mixed.size())/output_rate,
                output_rate,helper_end,helper_start));
        // This diagnostic describes emitted PCM only. Reception never reads it
        // or uses transmitted lengths to select framing or declare completion.
        std::cerr<<"{\"event\":\"helper_envelope\",\"sample_rate\":"<<output_rate
            <<",\"helper_lead_frames\":"<<leading<<",\"robust_frames\":"<<payload_count
            <<",\"helper_post_frames\":"<<post<<"}\n";
        samples=std::move(mixed);
    }
    // A generated capture includes actual quiet samples for the receiver's
    // sole end rule. Reading a truncated external WAV never invents this tail.
    // The helper is mixed above, so this separation contains actual silence.
    const auto logical_tail=add_tail?modem::pattern_absence_capture_samples(c)+c.sample_rate:0;
    if(logical_tail>c.memory_limit/sizeof(float))throw Error("WAV including receive separation exceeds memory limit");
    const auto tail=output_rate==c.sample_rate?logical_tail:
        (logical_tail*output_rate+c.sample_rate-1)/c.sample_rate;
    if(tail>c.memory_limit/sizeof(float) || samples.size()>c.memory_limit/sizeof(float)-tail)
        throw Error("WAV including receive separation exceeds memory limit");
    samples.resize(samples.size()+tail,0);

    if(a.has("output")) {
        std::ostringstream wav(std::ios::out|std::ios::binary);
        modem::write_wav(wav,samples,output_rate);auto bytes=wav.str();
        write_new_file(a.get("output"),std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
    }
    if(a.has("device")) {
        const auto delay=hardware_delay(a,c);
        audio::play(samples,output_rate,a.get("device"),{},audio_passband_guard(c),a.has("no-mono")?audio::ChannelMode::stereo:a.has("right-mono")?audio::ChannelMode::right_mono:audio::ChannelMode::left_mono);
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
            bool content_validated=true,bool stream_complete=false,std::span<const std::uint8_t> raw_bits={},std::size_t missing_symbols=0,std::size_t observed_bits=0,std::string_view error={},bool short_text_decoded=false,const live::SignalUpdate* signal=nullptr,const transfer::RecoveryProgress* recovery=nullptr,const HelperCapture* helper_capture=nullptr) {
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
          <<"\",\"filename\":\""<<json_escape(received_text(m.filename))<<"\",\"callsign\":\""<<json_escape(received_text(m.callsign))
          <<"\",\"grid\":\""<<json_escape(received_text(m.grid))<<"\",\"repeatable\":"<<(m.repeatable?"true":"false")
          <<",\"data_text\":\""<<json_escape(received_text(m.data))<<"\",\"corrected_bytes\":"<<stream.corrected_bytes
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
        if(helper_capture)report_helper_capture_json(*helper_capture);
        std::cout<<"}\n";
    } else if(!a.has("save")) {
        if(!content_validated && m.data.empty() && !raw_bits.empty()) {
            for(auto bit:raw_bits)std::cout<<(bit?'1':'0');
            std::cout<<'\n';
        } else if(m.kind!=MessageKind::text) {
            std::cerr<<"Validated file: "<<received_text(m.filename)<<" ("<<m.data.size()<<" bytes). Use --save PATH to retrieve exact bytes.\n";
        } else std::cout<<received_text(m.data);
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
void report_received(const Args& a,const transfer::Received& received,const live::SignalUpdate* signal=nullptr,
                     const HelperCapture* helper_capture=nullptr) {
    report(a,received.content,received.diagnostics,received.timestamp,received.content_validated,received.stream_complete,received.raw_bits,received.missing_symbols,received.observed_bits,received.error,received.short_text_decoded,signal,&received.recovery_progress,helper_capture);
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
void report_lpi(const transfer::Estimate& transmission,const transfer::Options& options) {
    const auto model=lpi::estimate(transmission,options);
    const bool available=model.status==lpi::Status::available;
    const char* status=available?"available":
        model.status==lpi::Status::outside_weak_signal_model?"outside_weak_signal_model":"numeric_limit";
    std::cout<<"{\"model\":\"relative_weak_signal_radiometer\",\"status\":\""<<status
        <<"\",\"cn0_basis\":\"receiver_one_symbol_reference\""
        <<",\"hypothetical_encryption\":"<<(model.hypothetical_encryption?"true":"false")
        <<",\"warning\":";
    if(model.hypothetical_encryption)
        std::cout<<"\"Encryption is off: hypothetical private patterns at the current timing; actual public or tone transmission may be detected sooner.\"";
    else std::cout<<"null";
    std::cout<<",\"burst_exposure_basis\":\"current_draft_airtime_at_receiver_reference\""
        <<",\"equal_received_cn0\":true,\"known_band_window_and_noise\":true"
        <<",\"receiver_reference_symbol_snr_db\":"<<lpi::receiver_reference_symbol_snr_db
        <<",\"receiver_reference_is_calibrated\":false"
        <<",\"safe_traffic_limit\":false,\"detection_probability\":"<<lpi::detection_probability
        <<",\"false_alarm_probability_per_window\":"<<lpi::false_alarm_probability
        <<",\"reference_cn0_db_hz\":";json_number(model.reference_cn0_db_hz);
    std::cout<<",\"observation_bandwidth_hz\":";json_number(model.observation_bandwidth_hz);
    std::cout<<",\"in_band_snr_db\":";json_number(model.in_band_snr_db);
    std::cout<<",\"noise_rise_db\":";json_number(model.noise_rise_db);
    std::cout<<",\"symbol_seconds\":";json_number(model.symbol_seconds);
    std::cout<<",\"detection_seconds\":";
    if(available)json_number(model.detection_seconds);else std::cout<<"null";
    std::cout<<",\"equivalent_wire_symbols\":";
    if(available)json_number(model.equivalent_symbols);else std::cout<<"null";
    std::cout<<",\"observation_ratio\":";
    if(available)json_number(model.equivalent_symbols);else std::cout<<"null";
    std::cout<<",\"additional_wire_symbols\":";
    if(available)json_number(model.additional_symbols);else std::cout<<"null";
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
    apply_oscillator_effects(a,c,channel);
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
    const auto oscillator_search=modem::oscillator_pattern_search(c,transfer::effective_clock_sync(options)?modem::utc_transmit_rate_limit(c):0);
    const auto& geometry=oscillator_search.frequency;
    const auto phase=static_cast<long double>(channel.phase_noise_degrees_per_sqrt_second)*std::numbers::pi_v<long double>/180;
    const auto rho=std::pow(10.L,static_cast<long double>(link.snr_db_hz)/10);
    const auto full_half_width=static_cast<long double>(geometry.requested_half_width_hz);
    const auto full_frequency_count=1+2*std::ceil(full_half_width*experiment.symbol_seconds/.25L);
    std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"{\"analysis\":\"matched_correlation_reference\",\"production_decoder_run\":false,\"pcm_generated\":false"
        <<",\"conditional_on_matched_timing_and_clock\":true"
        <<",\"prescribed_template_correlation_not_measured\":true"
        <<",\"monte_carlo_intervals\":\"model_only_95_percent_Wilson\""
        <<",\"oscillator_model\":{\"preset\":\""<<oscillator.id<<"\",\"illustrative\":true,\"overridden\":"
        <<((a.has("clock-error-ppm")||a.has("phase-noise"))?"true":"false")<<'}'
        <<",\"oscillator_search\":{\"rf_preset\":\""<<tuning::parse_oscillator_preset(a.get("rf-oscillator","crystal")).id
        <<"\",\"reference\":\""<<(c.oscillator_search->reference==modem::OscillatorReference::shared_radio?"shared-radio":"independent")
        <<"\",\"sideband\":\""<<(c.oscillator_search->sideband==modem::OscillatorSideband::upper?"upper":"lower")
        <<"\",\"rf_shift_hz\":";json_number(c.oscillator_search->rf_shift_hz);
    std::cout<<",\"physical_rf_hz\":";json_number(modem::oscillator_effects(c).physical_rf_hz);
    std::cout<<",\"margin\":";json_number(c.oscillator_search->margin);
    std::cout<<",\"requested_frequency_half_width_hz\":";json_number(geometry.requested_half_width_hz);
    std::cout<<",\"covered_frequency_half_width_hz\":";json_number(geometry.half_width_hz);
    std::cout<<",\"requested_clock_half_width_ppm\":";json_number(oscillator_search.requested_clock_half_width_ppm);
    std::cout<<",\"covered_clock_half_width_ppm\":";json_number(oscillator_search.clock_half_width_ppm);
    std::cout<<",\"paired_hypotheses\":"<<oscillator_search.hypotheses.size()<<",\"limited\":"<<(oscillator_search.limited?"true":"false")<<'}'
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
    std::cout<<",\"carrier_hz\":";json_number(modem::oscillator_effects(c).physical_rf_hz);
    std::cout<<",\"stream_carrier_hz\":";json_number(c.carrier_hz);
    std::cout<<",\"shift_hz\":";json_number(c.oscillator_search->rf_shift_hz);
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
        <<",\"clock_in_search\":"<<(current.clock_in_search?"true":"false")
        <<",\"oscillator_search_limited\":"<<(current.oscillator_search_limited?"true":"false")
        <<",\"workspace_supported\":"<<(current.receiver_workspace_supported?"true":"false")
        <<",\"confidence_available\":"<<(current.confidence_available?"true":"false")
        <<",\"success_probability\":";
    if(current.confidence_available)json_number(current.success_probability);else std::cout<<"null";
    std::cout<<",\"clock_error_ppm\":";json_number(channel.clock_error_ppm);
    std::cout<<",\"frequency_offset_hz\":";json_number(channel.frequency_offset_hz);
    std::cout<<",\"actual_carrier_offset_hz\":";json_number(current.carrier_offset_hz);
    std::cout<<",\"requested_search_half_width_hz\":";json_number(current.requested_carrier_search_half_width_hz);
    std::cout<<",\"covered_search_half_width_hz\":";json_number(current.carrier_search_half_width_hz);
    std::cout<<",\"requested_clock_half_width_ppm\":";json_number(current.requested_clock_search_half_width_ppm);
    std::cout<<",\"covered_clock_half_width_ppm\":";json_number(current.clock_search_half_width_ppm);
    std::cout<<",\"frequency_rate_hypotheses\":"<<current.frequency_rate_hypotheses;
    std::cout<<",\"frequency_step_hz\":";json_number(geometry.step_hz);
    std::cout<<",\"clock_error_for_quarter_chip_ppm\":";
    json_number(1e6L*.25L*modem::pattern_chip_samples(c)/c.sample_rate/experiment.symbol_seconds);
    std::cout<<",\"frequency_hypotheses\":"<<geometry.count
        <<",\"frequency_hypotheses_cap\":"<<modem::maximum_pattern_frequency_hypotheses
        <<",\"full_oscillator_frequency_hypotheses\":";json_number(full_frequency_count);
    std::cout<<",\"modeled_symbol_snr_db\":";json_number(current.modeled_symbol_snr_db);
    std::cout<<",\"coherent_reference_only\":"<<(current.coherent_reference_only?"true":"false")
        <<",\"drift_model_available\":"<<(current.drift_model_available?"true":"false")
        <<",\"coherent_success_probability\":";
    if(current.confidence_available&&current.drift_model_available)json_number(current.coherent_success_probability);
    else std::cout<<"null";
    std::cout
        <<",\"drift_sections\":"<<current.drift_sections<<",\"drift_section_seconds\":";
    json_number(current.drift_section_seconds);
    std::cout<<",\"differential_windows\":"<<current.differential_windows<<",\"differential_window_seconds\":";
    json_number(current.differential_window_seconds);
    std::cout<<",\"differential_model_available\":"<<(current.differential_model_available?"true":"false")
        <<",\"differential_added_detection_probability\":";
    if(current.differential_model_available)json_number(current.differential_added_detection_probability);else std::cout<<"null";
    std::cout<<",\"probability_trials\":"<<current.probability_trials
        <<",\"probability_search_approximation\":"<<(current.probability_search_approximation?"true":"false")
        <<",\"probability_carrier_candidates\":"<<current.probability_carrier_candidates
        <<",\"probability_model_limit\":\""<<json_escape(current.probability_model_limit)<<"\""
        <<",\"one_bit_success_probability\":";
    if(current.one_bit_confidence_available)json_number(current.one_bit_success_probability);else std::cout<<"null";
    std::cout<<",\"one_bit_confidence_available\":"<<(current.one_bit_confidence_available?"true":"false");
    std::cout<<",\"one_bit_probability_sampling_low\":";
    if(current.probability_trials)json_number(current.one_bit_probability_low);else std::cout<<"null";
    std::cout<<",\"one_bit_probability_sampling_high\":";
    if(current.probability_trials)json_number(current.one_bit_probability_high);else std::cout<<"null";
    std::cout<<",\"success_probability_sampling_low\":";
    if(current.probability_interval_available)json_number(current.success_probability_low);else std::cout<<"null";
    std::cout<<",\"success_probability_sampling_high\":";
    if(current.probability_interval_available)json_number(current.success_probability_high);else std::cout<<"null";
    std::cout<<",\"phase_coherence_loss_db\":";json_number(current.phase_coherence_loss_db);
    std::cout<<",\"section_phase_coherence_loss_db\":";json_number(current.section_phase_coherence_loss_db);
    std::cout<<",\"cpu_reference\":\""<<simulation::reference_cpu<<"\",\"gpu_reference\":\""<<simulation::reference_gpu
        <<"\",\"gpu_hypothetical\":"<<(current.gpu_hypothetical?"true":"false")<<",\"cpu_seconds\":";
    json_number(current.cpu_seconds);std::cout<<",\"gpu_seconds\":";
    if(current.gpu_work_supported)json_number(current.gpu_seconds);else std::cout<<"null";
    std::cout<<",\"gpu_work_supported\":"<<(current.gpu_work_supported?"true":"false");
    std::cout<<",\"receiver_cpu_seconds\":";json_number(current.receiver_cpu_seconds);
    std::cout<<",\"receiver_frontend_seconds\":";json_number(current.receiver_frontend_seconds);
    std::cout<<",\"receiver_search_seconds\":";json_number(current.receiver_search_seconds);
    std::cout<<",\"receiver_kernel_rebuild_seconds\":";json_number(current.receiver_kernel_rebuild_seconds);
    std::cout<<",\"receiver_kernel_rebuild_upper_seconds\":";json_number(current.receiver_kernel_rebuild_upper_seconds);
    std::cout<<",\"pulse_projection_modeled\":"<<(current.pulse_projection_modeled?"true":"false")
        <<",\"pulse_segment_projection_modeled\":"<<(current.pulse_segment_projection_modeled?"true":"false")
        <<",\"kernel_rebuild_upper_bound\":"<<(current.kernel_rebuild_upper_bound?"true":"false");
    std::cout<<",\"payload_processing_seconds\":";json_number(current.payload_processing_seconds);
    std::cout<<",\"mitigation_seconds\":";json_number(current.mitigation_seconds);
    std::cout<<",\"tracking_seconds\":";json_number(current.tracking_seconds);
    std::cout<<",\"tracking_symbol_windows\":";json_number(current.tracking_symbol_windows);
    std::cout<<"},\"lpi\":";report_lpi(transmission,options);
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
void listen(const Args& a,const transfer::Options& options,const helper::Options& helper_settings) {
    live::Settings settings;
    settings.helper=helper_settings;
    settings.transfer=options;
    settings.content_limit=content_budget(a);
    settings.dsp_workspace_bytes=dsp_budget(a);
    if(!a.has("time")) settings.transfer.timestamp=0;
    settings.device=a.get("device","default");
    settings.mono=!a.has("no-mono");
    settings.channel_mode=a.has("right-mono")?audio::ChannelMode::right_mono:audio::ChannelMode::left_mono;
    if(a.has("simulation")) {
        const auto preset=tuning::parse_simulation_preset(a.get("simulation"));
        settings.simulation=preset.enabled;
        if(preset.enabled) settings.simulation_snr_db=tuning::link_budget(preset,
            options.modem.bandwidth_hz,options.modem.sample_rate).sample_snr_db;
    }
    settings.simulation_seed=a.integer("seed",1);
    modem::ChannelConfig oscillator;
    oscillator.frequency_offset_hz=a.number("frequency-offset",0);
    apply_oscillator_effects(a,options.modem,oscillator);
    settings.simulation_clock_error_ppm=oscillator.clock_error_ppm;
    settings.simulation_frequency_offset_hz=oscillator.frequency_offset_hz;
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
    if(a.has("helper-only"))session.transmit_helper();
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
            for(const auto& signal:snapshot.signals) if(!signal.helper_carrier && !signal.binary && !signal.validated && !signal.complete) {
                std::cout<<"{\"event\":\"preview\",\"validated\":false,\"frequency_hz\":"<<signal.frequency_hz
                    <<",\"data_text\":\""<<json_escape(received_text(signal.text))<<'"';
                report_signal_identity(signal);std::cout<<"}\n";
            }
        }
        if(a.has("json"))for(const auto& signal:snapshot.signals) {
            if(signal.helper_carrier) {
                std::cout<<"{\"event\":\"helper_carrier\",\"type\":\"helper\",\"content_validated\":false,\"authenticated\":false";
                report_signal_identity(signal);
                std::cout<<",\"complete\":"<<(signal.complete?"true":"false")
                    <<",\"frequency_hz\":"<<signal.frequency_hz<<",\"snr_db\":"<<signal.snr_db
                    <<",\"state\":\""<<(signal.complete?"received":"pending")
                    <<"\",\"description\":\""<<json_escape(received_text(signal.text))<<"\"}\n";
            } else if(signal.binary) {
                std::cout<<"{\"event\":\"raw_bits\",\"content_validated\":false,\"authenticated\":false";
                report_signal_identity(signal);
                std::cout<<",\"complete\":"<<(signal.complete?"true":"false")<<",\"raw_bits\":\"";
                for(const auto bit:signal.text)std::cout<<(bit=='1'?'1':'0');
                std::cout<<"\",\"raw_bit_count\":"<<signal.received_bits<<",\"pattern_score\":";
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
            if(signal.helper_carrier) {
                std::cout<<"Helper carrier "<<(signal.complete?"received":"pending")
                    <<" at "<<signal.frequency_hz<<" Hz ("<<signal.snr_db<<" dB) "<<received_text(signal.text)<<'\n';
                continue;
            }
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
        if(argc>1&&std::string_view(argv[1]).starts_with("fast-"))return fast::cli_main(argc,argv);
        Args a(argc,argv);
        if(a.has("version")) {std::cout<<"Data Pump "<<DATAPUMP_VERSION<<'\n';return 0;}
        if(a.has("help") || a.command.empty()) {std::cout<<usage;return 0;}
        const std::set<std::string> commands={"tx","rx","simulate","status-tx","status-rx","estimate","listen","analyze-link"};
        if(!commands.contains(a.command))static_cast<void>(helper_options(a,false));
        if((a.command=="estimate"||a.command=="analyze-link") && helper_power(a,"helper-power",0)>0)
            throw Error("enabled helper carrier is supported only by tx, status-tx, listen, rx, status-rx and simulate; use --helper-power 0 to disable it");
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
        if(!commands.contains(a.command)) throw Error("unknown command: "+a.command);
        auto c=config(a);auto timestamp=epoch(a);
        auto k=c.spreading_mode==modem::SpreadingMode::tone?std::optional<Crypto>{}:key(a);
        const auto helper_settings=helper_options(a,k.has_value());
        // Scheduled calendar geometry is keyed before transfer preparation,
        // including the bit-rate report for packed short symbols.
        if((c.scheduled_hopping&&c.scheduled_hopping->enabled())||c.symbol_burst.enabled())c.data_key=k;
        if(c.spreading_mode==modem::SpreadingMode::tone && a.has("keyfile"))
            std::cerr<<"Tone mode is unencrypted; key selection is disabled.\n";
        auto settings=transfer_options(a,c,k,timestamp);
        if(c.symbol_burst.enabled() && (a.command=="tx" || a.command=="status-tx" || a.command=="simulate")) {
            // File and simulated sources need the same eligible physical
            // calendar binding as hardware scheduling, including fractional P.
            const modem::SymbolBurstPlan plan(transfer::seeded_config(settings,timestamp));
            const auto slot=plan.first_slot({timestamp,0});
            timestamp=slot.payload_begin.epoch;
            c.stream_epoch=timestamp;c.stream_phase_samples=slot.payload_begin.phase;
            settings.timestamp=timestamp;settings.modem.stream_phase_samples=slot.payload_begin.phase;
        }
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
                <<",\"dsss_factor\":"<<c.dsss_factor<<",\"dsss_version\":\""<<tuning::outer_dsss_version_id(c.outer_dsss_version)<<'"'
                <<",\"search_arithmetic\":\""<<tuning::search_arithmetic_id(c.search_arithmetic)<<'"'
                <<",\"receiver_strategy\":\""<<tuning::receiver_strategy_id(c.receiver_strategy)<<'"'
                <<",\"receiver_cpu_budget\":"<<c.receiver_cpu_budget
                <<",\"symbol_burst\":"<<c.symbol_burst.wait_symbols
                <<",\"symbol_burst_random\":"<<c.symbol_burst.random_limit
                <<",\"symbol_burst_random_effective\":"<<(c.symbol_burst.enabled()&&k?c.symbol_burst.random_limit:0)
                <<",\"sched_hop\":\""<<modem::sched_hop_id(c.scheduled_hopping?c.scheduled_hopping->time_multiplier:1)<<'"'
                <<",\"sched_hop_dilution\":"<<modem::sched_hop_dilution_id(c.scheduled_hopping?c.scheduled_hopping->group_hops:1)
                <<",\"sched_frequency_search\":"<<c.frequency_search_multiplier
                <<",\"constellation_bits\":"<<c.constellation_bits
                <<",\"sample_rate\":"<<c.sample_rate<<",\"carrier_hz\":"<<modem::oscillator_effects(c).physical_rf_hz
                <<",\"stream_carrier_hz\":"<<c.carrier_hz<<",\"shift_hz\":"<<c.oscillator_search->rf_shift_hz
                <<",\"repeatable_allowed\":"<<(result.repeatable_allowed?"true":"false")
                <<",\"memory_supported\":"<<(result.memory_supported?"true":"false")
                <<",\"batch_memory_supported\":"<<(result.batch_memory_supported?"true":"false");
            if(automatic_tuning(a)) {
                const auto plan=tuning::resolve(c.bandwidth_hz,a.number("target-snr",32),
                    tuning::parse_pattern_mode(a.get("pattern",a.has("keyfile")?"auto-keystream":"auto-pattern")),a.has("keyfile"),c.carrier_hz,c.dsss_factor,c.outer_dsss_version,c.search_arithmetic);
                std::cout<<",\"estimated_symbol_snr_db\":"<<plan.estimated_symbol_snr_db
                    <<",\"symbol_seconds\":"<<modem::symbol_seconds(c)
                    <<",\"target_supported\":"<<(plan.target_supported?"true":"false");
            }
            std::cout<<",\"lpi\":";report_lpi(result,settings);
            std::cout<<"}\n";
            return 0;
        }
        if(a.command=="listen") {listen(a,settings,helper_settings);return 0;}
        if(a.command=="status-tx" || a.command=="status-rx") {
            const auto plain=status_bits(a,{},timestamp);
            if(a.command=="status-tx") {
                if(!a.has("output") && !a.has("device"))throw Error("status-tx requires --output or --device");
                if(a.has("output")) {
                    auto source=transfer::binary_transmitter(plain,settings);
                    const auto origin=helper::effective_helper_power(helper_settings)>0?helper_source_origin(c,settings.timestamp,source.get()):0;
                    const auto reference=helper::effective_helper_power(helper_settings)>0?std::optional<helper::PowerReference>(
                        helper::transmit_power_reference(c,*source,settings.timestamp,settings.timestamp,settings.key)):
                        std::nullopt;
                    if(source->total_samples()>c.memory_limit/sizeof(float))throw Error("status WAV exceeds the configured waveform memory limit");
                    std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
                    for(std::size_t offset=0;offset<samples.size();)offset+=source->read(std::span(samples).subspan(offset,std::min<std::size_t>(4096,samples.size()-offset)));
                    output_wave(a,std::move(samples),c,true,helper_settings,settings.timestamp,origin,settings.key,
                        reference?&*reference:nullptr);
                } else {
                    const auto delay=hardware_delay(a,c);
                    play_transmission(a,settings,helper_settings,[&](const auto& options){return transfer::binary_transmitter(plain,options);});
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
                }
            } else {
                auto wav=input_wav(a);
                std::optional<HelperCapture> helper_capture;
                if(helper::effective_helper_power(helper_settings)>0)helper_capture=preselect_helper_wav(wav,settings.modem,helper_settings,settings.key);
                settings.modem.sample_rate=wav.sample_rate;modem::validate(settings.modem);
                const auto result=receive_with_helper_diagnostics(a,helper_capture,[&]{return receive_helper_payload(wav.samples,settings,helper_settings,helper_capture,progress,
                    (wav.samples.capacity()-wav.samples.size())*sizeof(float));});
                std::cout<<"{\"authenticated\":false,\"content_validated\":false,\"known_bits\":\""<<a.get("bits")<<"\",\"raw_bits\":\"";
                for(auto bit:result.raw_bits)std::cout<<(bit?'1':'0');
                std::cout<<"\",\"raw_bit_count\":"<<result.raw_bits.size()<<",\"missing_symbols\":"<<result.missing_symbols
                    <<",\"stream_complete\":"<<(result.stream_complete?"true":"false")
                    <<",\"known_bits_match\":"<<(result.stream_complete && !result.missing_symbols && result.observed_bits==plain.size() && result.raw_bits==plain?"true":"false")<<",\"pattern_score\":";
                if(result.diagnostics.pattern_score && std::isfinite(*result.diagnostics.pattern_score))std::cout<<*result.diagnostics.pattern_score;else std::cout<<"null";
                std::cout<<",\"pattern_score_units\":\"model log evidence\"";
                if(helper_capture)report_helper_capture_json(*helper_capture);
                std::cout<<"}\n";
            }
            return 0;
        }
        if(a.command=="rx") {
            std::vector<float> samples;
            std::optional<HelperCapture> helper_capture;
            if(a.has("device")) {
                if(a.has("input"))throw Error("choose --input WAV or --device");
                audio::Options capture_options;
                auto capture_record_rate=c.sample_rate;
                bool helper_disabled=false,capture_format_accepted=false;
                std::optional<helper::Tracker> helper_tracker;
                std::optional<HelperMetadataReceiver> helper_receiver;
                std::uint64_t helper_raw_samples=0;
                std::uint32_t helper_raw_rate=0;
                const auto disable_helper=[&](const char* reason) {
                    helper_disabled=true;
                    helper_tracker.reset();helper_receiver.reset();helper_raw_rate=0;
                    helper_capture.emplace();
                    std::cerr<<"Helper carrier observation disabled: "<<reason<<'\n';
                };
                try {
                    if(helper::effective_helper_power(helper_settings)>0)capture_options.minimum_hardware_rate=helper::minimum_output_rate(
                        helper_settings,helper::placement_band(c),c.sample_rate);
                    capture_record_rate=std::max(c.sample_rate,capture_options.minimum_hardware_rate);
                }catch(const Error& error){disable_helper(error.what());}
                if(helper::effective_helper_power(helper_settings)>0) {
                    helper_capture.emplace();
                    capture_options.capture_monitor=[&](std::span<const float> raw,std::uint32_t rate) {
                        if(!helper_tracker||!helper_receiver)return;
                        // Keep the provider's original gap failure: a flat
                        // one-shot recording cannot represent discontinuities.
                        // Optional observation errors must not stop capture.
                        try {
                            if(rate!=helper_raw_rate||raw.size()>UINT64_MAX-helper_raw_samples) {
                                disable_helper("raw capture coordinates changed");return;
                            }
                            observe_helper_capture(*helper_capture,*helper_tracker,*helper_receiver,
                                raw,rate,helper_raw_samples);
                            helper_raw_samples+=raw.size();
                        } catch(const std::exception& error) {disable_helper(error.what());}
                    };
                }
                const auto on_format=[&](const auto& format) {
                    audio_passband_guard(c)(format);
                    capture_format_accepted=true;
                    if(!a.has("time")) {
                        settings.capture_epoch=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
                        settings.timestamp=static_cast<std::uint64_t>(*settings.capture_epoch);
                    }
                    if(helper::effective_helper_power(helper_settings)>0&&!helper_disabled)try {
                        const auto plan=helper::plan(helper_settings,helper::placement_band(c),
                            format.hardware_rate,0,modem::outer_dsss_transmit_gain(c));
                        std::vector<Crypto> keys;if(settings.key)keys.push_back(*settings.key);
                        helper_tracker.emplace(plan.frequency_hz);
                        // Decode optional metadata after recording, when the
                        // actual remaining waveform budget can fund its ring.
                        helper_receiver.emplace(plan,std::move(keys),helper_precision_options(c),0);
                        helper_capture.emplace();helper_raw_samples=0;helper_raw_rate=format.hardware_rate;
                    } catch(const std::exception& error) {disable_helper(error.what());}
                };
                if(helper::effective_helper_power(helper_settings)>0) {
                    try {
                        // Retain the complete group in a bounded physical-rate
                        // recording; preselect filters it before DATA conversion.
                        samples=audio::record(a.number("seconds",15),capture_record_rate,a.get("device"),budget(a),{},on_format,capture_options);
                    }catch(const Error& error) {
                        if(capture_format_accepted||!capture_options.minimum_hardware_rate)throw;
                        disable_helper(error.what());capture_options.minimum_hardware_rate=0;capture_record_rate=c.sample_rate;
                        samples=audio::record(a.number("seconds",15),c.sample_rate,a.get("device"),budget(a),{},on_format,capture_options);
                    }
                }
                else samples=audio::record(a.number("seconds",15),c.sample_rate,a.get("device"),budget(a),{},on_format);
                if(helper::effective_helper_power(helper_settings)>0&&(!helper_disabled||capture_record_rate!=c.sample_rate)) {
                    modem::Wav wav{capture_record_rate,std::move(samples)};
                    helper_capture=preselect_helper_wav(wav,c,helper_settings,settings.key);
                    samples=std::move(wav.samples);
                }
            }
            else {
                auto wav=input_wav(a);
                if(helper::effective_helper_power(helper_settings)>0)helper_capture=preselect_helper_wav(wav,c,helper_settings,settings.key);
                c.sample_rate=wav.sample_rate;modem::validate(c);samples=std::move(wav.samples);
            }
            settings.modem=c;
            auto result=receive_with_helper_diagnostics(a,helper_capture,[&]{return receive_helper_payload(samples,settings,helper_settings,helper_capture,progress,
                (samples.capacity()-samples.size())*sizeof(float));});
            report_received(a,result,nullptr,helper_capture?&*helper_capture:nullptr);return 0;
        }
        if(a.command=="tx" && !a.has("output") && !a.has("device")) throw Error("tx requires --output WAV or explicit --device");
        if(a.command=="simulate" && a.has("device")) throw Error("simulation uses in-memory loopback; omit --device");
        if(a.command=="tx") {
            const auto outgoing=message(a);
            const auto estimate=transfer::estimate(outgoing,settings);
            if(a.has("device")) {
                const auto delay=hardware_delay(a,c);
                play_transmission(a,settings,helper_settings,[&](const auto& options){return transfer::message_transmitter(outgoing,options);});
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(delay*1000)));
            } else if(helper::effective_helper_power(helper_settings)>0) {
                auto source=transfer::message_transmitter(outgoing,settings);
                const auto origin=helper_source_origin(c,settings.timestamp,source.get());
                const auto reference=helper::transmit_power_reference(c,*source,settings.timestamp,
                    settings.timestamp,settings.key);
                if(source->total_samples()>c.memory_limit/sizeof(float))throw Error("pattern waveform exceeds batch memory limit; use streaming output");
                std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
                std::size_t offset=0;
                while(offset<samples.size())offset+=source->read(std::span(samples).subspan(offset,
                    std::min<std::size_t>(4096,samples.size()-offset)));
                output_wave(a,std::move(samples),c,true,helper_settings,settings.timestamp,origin,settings.key,&reference);
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
        apply_oscillator_effects(a,c,channel);
        if(a.has("receiver-time")) channel.receiver_timestamp=a.integer("receiver-time",timestamp);
        const auto outgoing=message(a);
        transfer::Received result;
        std::optional<HelperCapture> helper_capture;
        if(helper::effective_helper_power(helper_settings)>0) {
            auto source=transfer::message_transmitter(outgoing,settings);
            modem::SampledSimulationChannel impairments(transfer::seeded_config(settings,timestamp),channel,
                helper_settings,helper::occupied_bandwidth(c));
            impairments.set_helper_time_key(settings.key);
            const auto physical_rate=impairments.physical_sample_rate();
            const auto helper_plan=helper::plan(helper_settings,helper::placement_band(c),
                physical_rate,0,modem::outer_dsss_transmit_gain(c));
            helper::Tracker tracker(helper_plan.frequency_hz);
            std::vector<Crypto> keys;if(settings.key)keys.push_back(*settings.key);
            HelperMetadataReceiver helper_receiver(helper_plan,std::move(keys),helper_precision_options(c),
                std::min<std::size_t>(512*1024,c.memory_limit));
            helper_capture.emplace();
            std::vector<float> physical,noisy;
            const auto sample_budget=c.memory_limit/sizeof(float);
            // A small initial reserve lets nearby metadata finish during the
            // Helper lead. Fund its workspace from actual retained capacities.
            // Before either vector grows later, relinquish optional decoding;
            // the existing waveform sample quotas remain unchanged.
            try {
                const auto metadata_bytes=helper::AuxiliaryMetadataReceiver::workspace_required(physical_rate);
                const auto physical_reserve=a.has("output")?2ULL*physical_rate:0;
                const auto logical_reserve=2ULL*c.sample_rate;
                if(metadata_bytes<=c.memory_limit&&
                   physical_reserve<=(c.memory_limit-metadata_bytes)/sizeof(float)&&
                   logical_reserve<=(c.memory_limit-metadata_bytes)/sizeof(float)-physical_reserve) {
                    physical.reserve(static_cast<std::size_t>(physical_reserve));
                    noisy.reserve(static_cast<std::size_t>(logical_reserve));
                }else helper_receiver.limit_workspace(0);
            }catch(const std::exception&) {
                std::vector<float>{}.swap(physical);std::vector<float>{}.swap(noisy);
                helper_receiver.limit_workspace(0);
            }
            const auto limit_metadata_capacity=[&](const std::vector<float>& destination,std::size_t incoming) {
                if(incoming>destination.capacity()-destination.size()) {
                    helper_receiver.limit_workspace(0);return;
                }
                auto remaining=c.memory_limit;
                if(physical.capacity()>remaining/sizeof(float))remaining=0;
                else remaining-=physical.capacity()*sizeof(float);
                if(noisy.capacity()>remaining/sizeof(float))remaining=0;
                else remaining-=noisy.capacity()*sizeof(float);
                helper_receiver.limit_workspace(remaining);
            };
            impairments.set_physical_observer([&](std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
                if(a.has("output")) {
                    if(physical.size()>sample_budget-noisy.size() || samples.size()>sample_budget-noisy.size()-physical.size())
                        throw Error("simulated helper WAV exceeds memory limit");
                }
                limit_metadata_capacity(physical,a.has("output")?samples.size():0);
                observe_helper_capture(*helper_capture,tracker,helper_receiver,samples,rate,first);
                if(a.has("output")) {
                    physical.insert(physical.end(),samples.begin(),samples.end());
                }
            });
            std::array<float,2048> block{};
            const auto append=[&](std::size_t count) {
                if(noisy.size()>sample_budget-physical.size() || count>sample_budget-physical.size()-noisy.size())
                    throw Error("simulated helper waveform exceeds memory limit");
                limit_metadata_capacity(noisy,count);
                noisy.insert(noisy.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
            };
            while(const auto count=impairments.read(*source,block))append(count);
            auto tail=modem::pattern_absence_capture_samples(c)+c.sample_rate;
            while(tail) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tail,block.size()));
                impairments.read_noise(std::span(block).first(count));append(count);tail-=count;
            }
            auto receiver_settings=settings;
            receiver_settings.timestamp=channel.receiver_timestamp.value_or(timestamp);
            helper_receiver.limit_workspace(0); // Optional metadata yields before ordinary payload work.
            result=receive_with_helper_diagnostics(a,helper_capture,[&]{return receive_helper_payload(noisy,receiver_settings,helper_settings,helper_capture,progress,
                (physical.capacity()+noisy.capacity()-noisy.size())*sizeof(float));});
            if(a.has("output")) {
                auto physical_config=c;physical_config.sample_rate=physical_rate;
                output_wave(a,std::move(physical),physical_config,false);
            }
        } else if(a.has("output")) {
            auto source=transfer::message_transmitter(outgoing,settings);
            modem::SampledSimulationChannel impairments(transfer::seeded_config(settings,timestamp),channel);
            std::array<float,2048> block{};std::vector<float> noisy;
            const auto append=[&](std::size_t count) {
                if(count>c.memory_limit/sizeof(float)-noisy.size())throw Error("simulated WAV exceeds memory limit");
                noisy.insert(noisy.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
            };
            while(const auto count=impairments.read(*source,block))append(count);
            auto tail=modem::pattern_absence_capture_samples(c)+c.sample_rate;
            while(tail) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tail,block.size()));
                impairments.read_noise(std::span(block).first(count));append(count);tail-=count;
            }
            auto receiver_settings=settings;
            receiver_settings.timestamp=channel.receiver_timestamp.value_or(timestamp);
            result=transfer::receive(noisy,receiver_settings,progress);
            output_wave(a,std::move(noisy),c,false);
        } else result=transfer::simulate(outgoing,settings,channel,progress);
        report_received(a,result,nullptr,helper_capture?&*helper_capture:nullptr);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"pump: "<<e.what()<<'\n';return 2;}
}
