#include "datapump/fast/cli.hpp"
#include "datapump/fast/file_transfer.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/runtime.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace datapump::fast {
namespace {
volatile std::sig_atomic_t interrupted=0;
void on_interrupt(int){interrupted=1;}
const char* help=R"(Fast APSK text and file transfer (separate from regular mode)
  pump fast-info [profile options]
  pump fast-tx (--text TEXT | --input FILE) (--output WAV | --device DEVICE)
  pump fast-rx --input WAV [--save FILE]
  pump fast-listen [--device DEVICE] [--seconds N] [--save FILE]

Matching local settings (no negotiation or received lengths):
  --profile wire|ssb|fm|acoustic      Default wire
  --apsk 4|16|64|256                 Profile default 16 (wire/SSB), 4 (FM/acoustic)
  --code-rate 1/2|3/4|7/8            Default 1/2
  --rs robust|high-rate              Two RS(128,112) or RS(128,120) words
  --interleave 1..64                 Default 16 outer groups
  --sample-rate 44100..192000        Default 48000 Hz
  --keyfile KEY                     Enable encryption using this keyfile
  --encrypt                        Require encryption and --keyfile
  --no-encryption                  Explicit plaintext; ignore keyfile options
  --key-name NAME [--pad PATH]       Existing named symmetric keyfile
  --quota-mb N                      Local storage quota, default 256 MiB
  --stereo                          Both audio output channels
  --json                            Machine-readable result

Every physical interval has 2048 inner-coded bits plus fixed sync/pilots.
Encryption is off without --keyfile. Plaintext checksums do not authenticate.
Text is at most 32768 UTF-8 bytes and uses the same wire format as file bytes.
WAV output includes 6.25 seconds of silence. EOF/cancellation is not physical end.
SNR simulation is available only in the fast_regression test executable.
)";
struct Args {
    std::map<std::string,std::string> values;
    Args(int argc,char** argv) {
        const std::set<std::string> flags{"help","json","stereo","encrypt","no-encryption"};
        const std::set<std::string> options{"input","text","output","save","keyfile","key-name","pad","profile","apsk","code-rate","rs","interleave","sample-rate","device","quota-mb","seconds"};
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
};
bool encryption_enabled(const Args& a) {
    if(a.has("encrypt")&&a.has("no-encryption"))throw Error("--encrypt and --no-encryption conflict");
    if(a.has("encrypt")&&!a.has("keyfile"))throw Error("--encrypt requires --keyfile");
    if(!a.has("keyfile")&&!a.has("no-encryption")&&(a.has("key-name")||a.has("pad")))
        throw Error("--key-name and --pad require --keyfile, or explicit --no-encryption");
    return !a.has("no-encryption") && a.has("keyfile");
}
Settings settings(const Args& a,bool load_key) {
    Settings s;s.profile=profile(parse_channel(a.get("profile","wire")));
    const auto order=a.integer("apsk",s.profile.constellation),depth=a.integer("interleave",s.profile.interleave_depth),rate=a.integer("sample-rate",48000);
    if(order>256||depth>64||rate>192000)throw Error("Fast profile value exceeds local bound");
    s.profile.constellation=static_cast<unsigned>(order);s.profile.interleave_depth=static_cast<unsigned>(depth);s.profile.sample_rate=static_cast<std::uint32_t>(rate);
    const auto code=a.get("code-rate","1/2");
    if(code=="1/2")s.profile.code_rate=CodeRate::half;else if(code=="3/4")s.profile.code_rate=CodeRate::three_quarters;else if(code=="7/8")s.profile.code_rate=CodeRate::seven_eighths;else throw Error("Fast code rate must be 1/2, 3/4 or 7/8");
    const auto rs=a.get("rs","robust");if(rs!="robust"&&rs!="high-rate")throw Error("Fast RS must be robust or high-rate");s.profile.robust=rs=="robust";
    s.device=a.get("device","default");s.mono=!a.has("stereo");
    auto quota=a.integer("quota-mb",256);if(!quota||quota>16384)throw Error("Fast quota must be 1..16384 MiB");s.quota_bytes=quota*1024*1024;
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
std::string text_preview(std::span<const std::uint8_t> bytes) {
    std::string out;out.reserve(bytes.size());
    constexpr char hex[]="0123456789abcdef";
    const auto escape_byte=[&](std::uint8_t value) {out+="\\x";out+=hex[value>>4];out+=hex[value&15];};
    for(std::size_t i=0;i<bytes.size();) {
        const auto first=bytes[i];
        if(first<0x80) {
            if(first>=0x20 || first=='\n' || first=='\r' || first=='\t') {
                if(first==0x7f)escape_byte(first);else out+=static_cast<char>(first);
            } else escape_byte(first);
            ++i;continue;
        }
        unsigned count=first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
        std::uint32_t code=first&((1u<<(7-count))-1);
        bool valid=count && i+count<=bytes.size();
        for(unsigned j=1;valid&&j<count;++j) {
            if((bytes[i+j]&0xc0)!=0x80)valid=false;
            else code=(code<<6)|(bytes[i+j]&0x3f);
        }
        valid=valid && code>=(count==2?0x80u:count==3?0x800u:0x10000u)
            && code<=0x10ffff && !(code>=0xd800&&code<=0xdfff);
        if(!valid) {escape_byte(first);++i;continue;}
        // Keep valid text legible while preventing terminal control and bidi
        // formatting characters from changing surrounding CLI presentation.
        if((code>=0x80&&code<=0x9f) || code==0x61c || code==0x200e || code==0x200f
                || (code>=0x2028&&code<=0x202e) || (code>=0x2066&&code<=0x2069)) {
            out+="\\u";
            for(int shift=12;shift>=0;shift-=4)out+=hex[(code>>shift)&15];
        } else for(unsigned j=0;j<count;++j)out+=static_cast<char>(bytes[i+j]);
        i+=count;
    }
    return out;
}
void report(const Snapshot& s,bool json) {
    const auto preview=s.complete&&s.file?s.file->preview():Bytes{};
    const auto text=text_preview(preview);
    const auto truncated=s.complete&&s.file&&s.file->size()>preview.size();
    if(json)std::cout<<"{\"complete\":"<<(s.complete?"true":"false")<<",\"physical_complete\":"<<(s.physical_complete?"true":"false")
        <<",\"cancelled\":"<<(s.cancelled?"true":"false")<<",\"source_bytes\":"<<s.source_bytes<<",\"intervals\":"<<s.intervals
        <<",\"encrypted\":"<<(s.encrypted?"true":"false")<<",\"authenticated\":"<<(s.authenticated?"true":"false")
        <<",\"authenticated_groups\":"<<s.authenticated_groups<<",\"checksum_groups\":"<<s.checksum_groups<<",\"corrected_bytes\":"<<s.corrected_bytes<<",\"erased_bytes\":"<<s.erased_bytes
        <<",\"evm\":"<<s.evm<<",\"goodput_bps\":"<<s.goodput_bps<<",\"text_preview\":\""<<json_escape(text)<<"\",\"preview_truncated\":"<<(truncated?"true":"false")
        <<",\"status\":\""<<json_escape(s.status)<<"\",\"error\":\""<<json_escape(s.error)<<"\"}\n";
    else {
        std::cout<<s.status<<"; "<<s.source_bytes<<" source bytes, "<<s.intervals<<" fixed intervals; encryption "<<(s.encrypted?"on":"off")
            <<(s.complete?(s.authenticated?"; authenticated":"; checksum checked"):"")<<(s.error.empty()?"":"; "+s.error)<<'\n';
        if(s.complete&&s.file)std::cout<<"Received text (escaped): \""<<json_escape(text)<<'"'<<(truncated?" [preview truncated]":"")<<'\n';
    }
}
}
int cli_main(int argc,char** argv) {
    Args a(argc,argv);const std::string command=argv[1];
    if(a.has("help")){std::cout<<help;return 0;}
    if(command!="fast-info"&&command!="fast-tx"&&command!="fast-rx"&&command!="fast-listen")throw Error("Unknown fast command");
    const auto encrypted=encryption_enabled(a);
    if(command=="fast-tx") {
        if(a.has("input")==a.has("text")||a.has("output")==a.has("device")||a.has("save"))
            throw Error("fast-tx requires exactly one of --text/--input and exactly one of --output/--device");
    } else if(a.has("text"))throw Error("--text is only available for fast-tx");
    auto s=settings(a,command!="fast-info");
    if(command=="fast-info") {
        const auto& p=s.profile;
        std::cout<<"{\"profile\":\""<<channel_name(p.channel)<<"\",\"sample_rate\":"<<p.sample_rate<<",\"symbol_rate\":"<<p.symbol_rate
            <<",\"carrier_hz\":"<<p.carrier_hz<<",\"occupied_bandwidth_hz\":"<<p.symbol_rate*(1+p.rolloff)<<",\"constellation\":"<<p.constellation
            <<",\"gross_bitrate\":"<<gross_bitrate(p)<<",\"physical_interval_bits\":2048,\"interval_symbols\":"<<interval_symbols(p)
            <<",\"cycle_intervals\":"<<cycle_intervals(p)<<",\"ciphertext_bytes\":"<<ciphertext_bytes(p)
            <<",\"encrypted\":"<<(encrypted?"true":"false")<<",\"source_bytes_per_group\":"<<source_bytes_per_group(p,encrypted)<<"}\n";return 0;
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
    while(session.active()) {
        if(interrupted||(seconds&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=static_cast<double>(seconds)))session.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    result=session.poll();if(result.complete&&a.has("save"))session.save(a.get("save"));report(result,a.has("json"));
    return result.error.empty()&&!result.cancelled&&(command=="fast-tx"||result.complete)?0:2;
}
}
