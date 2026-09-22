#include "datapump/fast/cli.hpp"
#include "datapump/fast/file_transfer.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <thread>

namespace datapump::fast {
namespace {
volatile std::sig_atomic_t interrupted=0;
void on_interrupt(int){interrupted=1;}
const char* help=R"(Fast QAM/LDPC and APSK text and file transfer (separate from regular mode)
  pump fast-info [profile options]
  pump fast-tx (--text TEXT | --input FILE) (--output WAV | --device DEVICE)
  pump fast-rx --input WAV [--save FILE]
  pump fast-listen [--device DEVICE] [--seconds N] [--save FILE]

Matching local settings (no negotiation or received lengths):
  --profile wire|ssb|fm|acoustic|acoustic-short   Default wire
  --format classic|capacity          Default capacity for every channel
                                    Acoustic: OFDM, 16-QAM, LDPC 3/4, depth 8
                                    Acoustic-short: separate shorter-transfer acoustic profile
  --expected-snr DB                  Select modeled defaults over the ORIGINAL channel bandwidth
                                    Cable 65..25 dB / 18 kHz; acoustic 13..-27 dB / 17.5 kHz
                                    SSB/FM 20..-20 dB / 2.4 kHz; weak presets narrow bandwidth
                                    Assumed, not measured or negotiated. Explicit options override.
  --qam 4|16|64|256|1024|4096|16384|65536|262144|1048576|4194304
                                    Cable default 4194304-QAM, LDPC 8/9
  --apsk 4|16|64|256                 Select classic format; default 256 (classic wire)
  --code-rate 1/2|2/3|3/4|7/8|7/9|8/9|9/10   Capacity supports 1/2, 2/3, 3/4, 7/9, 8/9, 9/10 LDPC
  --rs robust|high-rate|0.3%          Capacity uses approximately 0.3% parity/data
  --interleave 1..16 (capacity), 1..64 (classic); defaults: cable/radio 4, acoustic 8, acoustic-short 1
  --sample-rate 44100..192000        Default 48000 Hz
  --symbol-rate HZ --carrier HZ --rolloff N --amplitude N
  --marker-spacing 1..16            Single-carrier capacity intervals per full marker (16)
  --pilot-spacing 16..1024           Single-carrier capacity data symbols per pilot group (256)
  --ofdm-fft 2048..32768 --ofdm-prefix SAMPLES --ofdm-pilots 2..32
  --ofdm-low HZ --ofdm-high HZ      Acoustic OFDM defaults: FFT 32768, prefix 4096, pilots 16
                                    Band 500..18000 Hz; 48000 Hz audio required
  --estimate-bytes N                fast-info: estimate airtime for a local source size
  --keyfile KEY                     Enable encryption using this keyfile
  --encrypt                        Require encryption and --keyfile
  --no-encryption                  Explicit plaintext; ignore keyfile options
  --key-name NAME [--pad PATH]       Existing named symmetric keyfile
  --quota-mb N                      Local storage quota, default 256 MiB
  --stereo                          Same waveform on both outputs
  --mono                            Left output only (default for every profile)
  --right-mono                      Right output only; left silent
  --json                            Machine-readable result

Every physical interval has 2048 inner-coded bits plus fixed sync/pilots.
Encryption is off without --keyfile. Plaintext checksums do not authenticate.
Text is at most 32768 UTF-8 bytes and uses the same wire format as file bytes.
WAV output includes at least 6.25 seconds of silence, extended for complete OFDM blocks.
EOF/cancellation is not physical end.
SNR simulation is available only in the fast_regression test executable.
Output routing is local; peers need not use the same --mono/--right-mono/--stereo setting.
)";
struct Args {
    std::map<std::string,std::string> values;
    Args(int argc,char** argv) {
        const std::set<std::string> flags{"help","json","stereo","mono","right-mono","encrypt","no-encryption"};
        const std::set<std::string> options{"input","text","output","save","keyfile","key-name","pad","profile","format","qam","apsk","code-rate","rs","interleave","sample-rate","device","quota-mb","seconds","symbol-rate","carrier","rolloff","amplitude","marker-spacing","pilot-spacing","estimate-bytes","ofdm-fft","ofdm-prefix","ofdm-low","ofdm-high","ofdm-pilots","expected-snr"};
        for(int i=2;i<argc;++i) {
            std::string name=argv[i];if(!name.starts_with("--"))throw Error("Expected a fast --option");name.erase(0,2);
            if(values.contains(name))throw Error("Duplicate fast option: "+name);
            if(flags.contains(name))values.emplace(name,"1");
            else {
                if(!options.contains(name))throw Error("Unknown fast option: "+name);
                if(++i>=argc)throw Error("Missing fast option value: "+name);
                if(name=="text" && std::string_view(argv[i]).size()>text_byte_limit)
                    throw Error("Fast text exceeds the 32768-byte local limit");
                values.emplace(name,argv[i]);
            }
        }
    }
    bool has(const std::string& k)const{return values.contains(k);}
    std::string get(const std::string& k,std::string fallback={})const {auto it=values.find(k);return it==values.end()?fallback:it->second;}
    std::uint64_t integer(const std::string& k,std::uint64_t fallback)const {
        if(!has(k))return fallback;
        const auto text=get(k);std::uint64_t n=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw Error("Invalid fast integer: "+k);
        return n;
    }
    double real(const std::string& k,double fallback)const {
        if(!has(k))return fallback;
        const auto text=get(k);double n=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(n))throw Error("Invalid fast number: "+k);
        return n;
    }
};
bool encryption_enabled(const Args& a) {
    if(a.has("encrypt")&&a.has("no-encryption"))throw Error("--encrypt and --no-encryption conflict");
    if(a.has("encrypt")&&!a.has("keyfile"))throw Error("--encrypt requires --keyfile");
    if(!a.has("keyfile")&&!a.has("no-encryption")&&(a.has("key-name")||a.has("pad")))
        throw Error("--key-name and --pad require --keyfile, or explicit --no-encryption");
    return !a.has("no-encryption") && a.has("keyfile");
}
Settings settings(const Args& a,bool load_key) {
    Settings s;const auto channel=parse_channel(a.get("profile","wire"));
    if(a.has("qam")&&a.has("apsk"))throw Error("--qam and --apsk conflict");
    const auto format=a.get("format",a.has("apsk")?"classic":"capacity");
    if(format!="classic"&&format!="capacity")throw Error("Fast format must be classic or capacity");
    if((format=="classic"&&a.has("qam"))||(format=="capacity"&&a.has("apsk")))throw Error("Constellation option conflicts with selected format");
    s.profile=format=="capacity"?capacity_profile(channel):classic_profile(channel);
    if(a.has("expected-snr")) {
        if(format!="capacity")throw Error("Expected-SNR presets require capacity format");
        s.profile=resolve_snr_preset(channel,a.real("expected-snr",default_expected_snr(channel))).profile;
    }
    const auto order=a.integer(a.has("qam")?"qam":"apsk",s.profile.constellation),depth=a.integer("interleave",s.profile.interleave_depth),rate=a.integer("sample-rate",48000);
    if(order>4194304||depth>64||rate>192000)throw Error("Fast profile value exceeds local bound");
    s.profile.constellation=static_cast<unsigned>(order);s.profile.interleave_depth=static_cast<unsigned>(depth);s.profile.sample_rate=static_cast<std::uint32_t>(rate);
    s.profile.code_rate=parse_code_rate(a.get("code-rate",std::string(code_rate_name(s.profile.code_rate))));
    const auto rs=a.get("rs",s.profile.capacity_mode?"0.3%":s.profile.robust?"robust":"high-rate");
    if(s.profile.capacity_mode?rs!="0.3%":(rs!="robust"&&rs!="high-rate"))throw Error("RS selection does not match Fast format");
    s.profile.robust=rs=="robust";
    s.profile.symbol_rate=a.real("symbol-rate",s.profile.symbol_rate);s.profile.carrier_hz=a.real("carrier",s.profile.carrier_hz);
    s.profile.rolloff=a.real("rolloff",s.profile.rolloff);s.profile.amplitude=a.real("amplitude",s.profile.amplitude);
    const auto markers=a.integer("marker-spacing",s.profile.marker_spacing_intervals),pilots=a.integer("pilot-spacing",s.profile.pilot_spacing_symbols);
    if(markers>16||pilots>1024)throw Error("Fast marker/pilot spacing exceeds local bound");
    if(!s.profile.capacity_mode&&(a.has("marker-spacing")||a.has("pilot-spacing")))throw Error("Marker/pilot spacing options require capacity format");
    s.profile.marker_spacing_intervals=static_cast<unsigned>(markers);s.profile.pilot_spacing_symbols=static_cast<unsigned>(pilots);
    if(s.profile.acoustic_ofdm) {
        for(const auto* option:{"symbol-rate","carrier","rolloff","marker-spacing","pilot-spacing"})
            if(a.has(option))throw Error(std::string("--")+option+" is only applicable to single-carrier waveforms");
        const auto fft=a.integer("ofdm-fft",s.profile.ofdm_fft_size),prefix=a.integer("ofdm-prefix",s.profile.ofdm_prefix_samples);
        if(fft>32768||prefix>32768)throw Error("OFDM geometry exceeds its local bound");
        s.profile.ofdm_fft_size=static_cast<unsigned>(fft);s.profile.ofdm_prefix_samples=static_cast<unsigned>(prefix);
        const auto pilot_stride=a.integer("ofdm-pilots",s.profile.ofdm_pilot_stride);
        if(pilot_stride>32)throw Error("OFDM pilot stride exceeds its local bound");
        s.profile.ofdm_pilot_stride=static_cast<unsigned>(pilot_stride);
        s.profile.ofdm_low_hz=a.real("ofdm-low",s.profile.ofdm_low_hz);s.profile.ofdm_high_hz=a.real("ofdm-high",s.profile.ofdm_high_hz);
    } else for(const auto* option:{"ofdm-fft","ofdm-prefix","ofdm-low","ofdm-high","ofdm-pilots"})
        if(a.has(option))throw Error("OFDM options require an acoustic capacity profile");
    if(unsigned(a.has("mono"))+unsigned(a.has("right-mono"))+unsigned(a.has("stereo"))>1)
        throw Error("--mono, --right-mono and --stereo conflict");
    s.device=a.get("device","default");
    s.mono=!a.has("stereo");
    s.channel_mode=a.has("right-mono")?audio::ChannelMode::right_mono:
        s.mono?audio::ChannelMode::left_mono:audio::ChannelMode::stereo;
    auto quota=a.integer("quota-mb",256);if(!quota||quota>256)throw Error("Fast quota must be 1..256 MiB");s.quota_bytes=quota*1024*1024;
    validate(s.profile);
    if(encryption_enabled(a)&&load_key) {
        auto ring=load_keyring(a.get("keyfile"),a.has("pad")?std::optional<std::filesystem::path>(a.get("pad")):std::nullopt);
        if(ring.empty())throw Error("Fast keyfile has no keys");
        if(a.has("key-name")) {
            auto it=std::find_if(ring.begin(),ring.end(),[&](const auto& entry){return entry.name==a.get("key-name");});
            if(it==ring.end())throw Error("Requested fast key name not found");
            s.key=it->key;
        } else s.key=ring.front().key;
    }
    return s;
}
void report(const Snapshot& s,bool json) {
    if(json)std::cout<<"{\"complete\":"<<(s.complete?"true":"false")<<",\"physical_complete\":"<<(s.physical_complete?"true":"false")
        <<",\"cancelled\":"<<(s.cancelled?"true":"false")<<",\"source_bytes\":"<<s.source_bytes<<",\"intervals\":"<<s.intervals
        <<",\"encrypted\":"<<(s.encrypted?"true":"false")<<",\"authenticated\":"<<(s.authenticated?"true":"false")
        <<",\"authenticated_groups\":"<<s.authenticated_groups<<",\"checksum_groups\":"<<s.checksum_groups<<",\"corrected_bytes\":"<<s.corrected_bytes<<",\"erased_bytes\":"<<s.erased_bytes
        <<",\"ldpc_frames\":"<<s.ldpc_frames<<",\"ldpc_failed_frames\":"<<s.ldpc_failed_frames
        <<",\"ldpc_iterations\":"<<s.ldpc_iterations<<",\"ldpc_changed_bits\":"<<s.ldpc_changed_bits
        <<",\"decoding_stopped\":"<<(s.decoding_stopped?"true":"false")<<",\"coding_cycles\":"<<s.coding_cycles
        <<",\"failed_cycles\":"<<s.failed_cycles<<",\"verified_bytes\":"<<s.verified_bytes
        <<",\"estimated_seconds\":"<<s.estimated_seconds<<",\"transmit_fraction\":"<<s.transmit_fraction
        <<",\"evm\":"<<s.evm<<",\"goodput_bps\":"<<s.goodput_bps
        <<",\"is_attachment\":"<<(s.complete&&s.file&&s.file->is_attachment()?"true":"false")
        <<",\"filename\":\""<<received_text(s.complete&&s.file?s.file->filename():std::string{})<<"\""
        <<",\"status\":\""<<json_escape(s.status)<<"\",\"error\":\""<<json_escape(s.error)<<"\"}\n";
    else {
        std::cout<<s.status<<"; "<<s.source_bytes<<" source bytes, "<<s.intervals<<" fixed intervals; encryption "<<(s.encrypted?"on":"off")
            <<(s.complete?(s.authenticated?"; authenticated":"; checksum checked"):"")
            <<(s.failed_cycles?"; "+std::to_string(s.failed_cycles)+" damaged coding cycles":"")
            <<(s.error.empty()?"":"; "+s.error)<<'\n';
    }
}
}
int cli_main(int argc,char** argv) {
    Args a(argc,argv);const std::string command=argv[1];
    if(a.has("help")){std::cout<<help;return 0;}
    if(command!="fast-info"&&command!="fast-tx"&&command!="fast-rx"&&command!="fast-listen")throw Error("Unknown fast command");
    const auto encrypted=encryption_enabled(a);
    if(command!="fast-info"&&a.has("estimate-bytes"))throw Error("--estimate-bytes is only available for fast-info");
    if(command=="fast-tx") {
        if(a.has("input")==a.has("text")||a.has("output")==a.has("device")||a.has("save"))
            throw Error("fast-tx requires exactly one of --text/--input and exactly one of --output/--device");
    } else if(a.has("text"))throw Error("--text is only available for fast-tx");
    auto s=settings(a,command!="fast-info");
    if(command=="fast-info") {
        const auto& p=s.profile;
        // Fractional baud is part of the authenticated local profile. Preserve
        // enough decimal digits for a JSON reader to reconstruct that value.
        std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10);
        std::cout<<"{\"profile\":\""<<channel_name(p.channel)<<"\",\"sample_rate\":"<<p.sample_rate<<",\"symbol_rate\":"<<(p.acoustic_ofdm?double(p.sample_rate)/(p.ofdm_fft_size+p.ofdm_prefix_samples):p.symbol_rate)
            <<",\"format\":\""<<(p.capacity_mode?"capacity":"classic")<<"\",\"code_rate\":\""<<code_rate_name(p.code_rate)<<"\""
            <<",\"carrier_hz\":"<<(p.acoustic_ofdm?(occupied_lower_hz(p)+occupied_upper_hz(p))*.5:p.carrier_hz)<<",\"occupied_bandwidth_hz\":"<<occupied_bandwidth_hz(p)<<",\"constellation\":"<<p.constellation
            <<",\"amplitude\":"<<p.amplitude
            <<",\"mono\":"<<(s.mono?"true":"false")
            <<",\"audio_channels\":\""<<(s.channel_mode==audio::ChannelMode::stereo?"stereo":s.channel_mode==audio::ChannelMode::right_mono?"right":"left")<<"\""
            <<",\"shannon_snr_db_assumed\":30,\"shannon_capacity_bps\":"<<occupied_bandwidth_hz(p)*std::log2(1001.)
            <<",\"shannon_capacity_at_40db_bps\":"<<occupied_bandwidth_hz(p)*std::log2(10001.)
            <<",\"shannon_capacity_at_60db_bps\":"<<occupied_bandwidth_hz(p)*std::log2(1000001.)
            <<",\"gross_bitrate\":"<<gross_bitrate(p)<<",\"physical_interval_bits\":2048,\"interval_symbols\":"<<interval_symbols(p)
            <<",\"cycle_intervals\":"<<cycle_intervals(p)<<",\"ciphertext_bytes\":"<<ciphertext_bytes(p)
            <<",\"encrypted\":"<<(encrypted?"true":"false")<<",\"source_bytes_per_group\":"<<source_bytes_per_group(p,encrypted);
        if(p.capacity_mode) {
            const auto parity=capacity_parity_symbols(p)*2;
            const auto info=capacity_information_bytes(p);
            std::cout<<",\"source_bytes_per_cycle\":"<<capacity_source_bytes_per_cycle(p,encrypted);
            if(p.compact_convolutional)
                std::cout<<",\"inner_code\":\"convolutional\",\"inner_information_bytes\":"<<info
                    <<",\"inner_coded_bits\":"<<capacity_coded_bits(p);
            else std::cout<<",\"ldpc_blocks_per_cycle\":"<<p.interleave_depth;
            std::cout<<",\"rs_parity_bytes\":"<<parity<<",\"rs_parity_data_ratio\":"<<static_cast<double>(parity)/((info&~std::size_t{1})-parity);
            if(!p.acoustic_ofdm)std::cout<<",\"marker_spacing_intervals\":"<<p.marker_spacing_intervals<<",\"pilot_spacing_symbols\":"<<p.pilot_spacing_symbols;
        }
        std::cout<<",\"waveform\":\""<<(p.acoustic_ofdm?"ofdm":"single-carrier")<<"\"";
        if(p.capacity_mode) {
            const auto preset=resolve_snr_preset(p.channel,a.real("expected-snr",default_expected_snr(p.channel)));
            const auto reference=preset.reference_bandwidth_hz;
            const auto selected=preset.expected_snr_db+10*std::log10(reference/occupied_bandwidth_hz(p));
            std::cout<<",\"expected_snr_db\":"<<preset.expected_snr_db<<",\"snr_reference_bandwidth_hz\":"<<reference
                <<",\"selected_band_snr_db_assumed\":"<<selected
                <<",\"snr_preset_unmodified\":"<<(profile_id(p)==profile_id(preset.profile)?"true":"false")
                <<",\"reference_band_shannon_capacity_bps\":"<<reference*std::log2(1+std::pow(10.,preset.expected_snr_db/10.));
        }
        if(p.acoustic_ofdm)std::cout<<",\"ofdm_fft_size\":"<<p.ofdm_fft_size<<",\"ofdm_prefix_samples\":"<<p.ofdm_prefix_samples<<",\"ofdm_pilot_stride\":"<<p.ofdm_pilot_stride
            <<",\"ofdm_low_hz\":"<<p.ofdm_low_hz<<",\"ofdm_high_hz\":"<<p.ofdm_high_hz
            <<",\"occupied_lower_hz\":"<<occupied_lower_hz(p)<<",\"occupied_upper_hz\":"<<occupied_upper_hz(p);
        if(p.channel==Channel::acoustic_short&&p.capacity_mode&&!p.compact_convolutional)
            std::cout<<",\"ldpc_frame_bits\":"<<p.ldpc_frame_bits<<",\"ofdm_training_blocks\":"<<p.ofdm_training_blocks;
        if(a.has("estimate-bytes")) {
            const auto estimate=estimate_transmission(p,encrypted,a.integer("estimate-bytes",0));
            std::cout<<",\"estimated_seconds\":"<<estimate.seconds<<",\"estimated_source_bps\":"<<estimate.source_bps
                <<",\"estimated_intervals\":"<<estimate.intervals<<",\"estimated_samples\":"<<estimate.samples;
        }
        std::cout<<"}\n";return 0;
    }
    Snapshot result;
    if(command=="fast-tx") {
        if(a.has("output")) {
            result=a.has("text")?transmit_text_wave(s,a.get("text"),a.get("output")):transmit_wave(s,a.get("input"),a.get("output"));
            report(result,a.has("json"));return 0;
        }
    } else if(command=="fast-rx") {
        if(!a.has("input")||a.has("device")||a.has("output"))throw Error("fast-rx requires --input WAV; use fast-listen for audio input");
        result=receive_wave(s,a.get("input"));
        if(result.complete&&a.has("save"))result.file->save(a.get("save"));
        report(result,a.has("json"));return result.complete?0:2;
    } else if(a.has("input")||a.has("output"))throw Error("fast-listen uses audio hardware");
    interrupted=0;auto previous=std::signal(SIGINT,on_interrupt);
    struct Restore {decltype(previous) value;~Restore(){std::signal(SIGINT,value);}} restore{previous};
    Session session;session.configure(s);
    if(command=="fast-tx") {
        if(a.has("text"))session.transmit_text(a.get("text"));else session.transmit(a.get("input"));
    } else session.listen();
    const auto start=std::chrono::steady_clock::now();const auto seconds=a.integer("seconds",0);
    auto last_progress=start-std::chrono::seconds(1);
    while(session.active()) {
        const auto now=std::chrono::steady_clock::now();
        if(command=="fast-tx"&&!a.has("json")&&now-last_progress>=std::chrono::seconds(1)) {
            const auto current=session.poll();
            std::cerr<<"TX "<<static_cast<int>(100*current.transmit_fraction)<<"% · estimated "<<current.estimated_seconds<<" s total\n";
            last_progress=now;
        }
        if(interrupted||(seconds&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=static_cast<double>(seconds)))session.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    result=session.poll();if(result.complete&&a.has("save"))session.save(a.get("save"));report(result,a.has("json"));
    return result.error.empty()&&!result.cancelled&&(command=="fast-tx"||result.complete)?0:2;
}
}
