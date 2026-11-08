#include "datapump/audio.hpp"
#include "datapump/audio_entropy.hpp"
#include "datapump/crypto.hpp"
#include "datapump/fast/cli.hpp"
#include "datapump/qr.hpp"
#include "datapump/runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#ifndef DATAPUMP_VERSION
#define DATAPUMP_VERSION "001_00"
#endif
using namespace datapump;
namespace {
constexpr const char* unavailable="Robust audio commands are unavailable until the canonical triad backend is connected. The retired single-carrier modem has been removed.";
constexpr const char* usage=R"HELP(Data Pump — audio text and file modem
Usage: pump COMMAND [OPTIONS]
  keygen       Create an owner-only symmetric keyfile (--output PATH)
  keys         List named key entries (--keyfile PATH)
  devices      Enumerate local audio devices
  qr           Encode plaintext as QR (--text TEXT or --input PATH)
  fast-info    Show Fast modem profiles
  fast-tx      Transmit using Fast modem (--help for options)
  fast-rx      Receive a Fast WAV
  fast-listen  Receive a Fast live stream

Key options: --key-names A,B --device ID --pad PATH
QR options: --format svg|pbm --output PATH (otherwise stdout)
General: --help --version
Robust audio commands await connection to the canonical triad backend.
)HELP";
class Args {
    std::map<std::string,std::string> values_;
public:
    std::string command;
    Args(int argc,char** argv) {
        const std::set<std::string> valued={"output","key-names","device","pad","keyfile","text","input","format"};
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(!arg.starts_with("--")) {if(!command.empty())throw Error("unexpected argument: "+arg);command=std::move(arg);continue;}
            arg.erase(0,2);const auto equals=arg.find('=');auto name=arg.substr(0,equals);
            if(values_.contains(name))throw Error("duplicate option: --"+name);
            if(name=="help"||name=="version") {
                if(equals!=std::string::npos)throw Error("flag does not take a value: --"+name);
                values_[name]="true";
            } else if(valued.contains(name)) {
                if(equals!=std::string::npos)values_[name]=arg.substr(equals+1);
                else {if(++i>=argc||std::string_view(argv[i]).starts_with("--"))throw Error("missing value for --"+name);values_[name]=argv[i];}
            } else throw Error("unknown option: --"+name);
        }
    }
    bool has(const std::string& name) const {return values_.contains(name);}
    std::string get(const std::string& name,std::string fallback={}) const {auto it=values_.find(name);return it==values_.end()?fallback:it->second;}
    void only(std::initializer_list<const char*> allowed) const {
        for(const auto& [name,value]:values_) {
            (void)value;bool ok=name=="help"||name=="version";
            for(const auto* option:allowed)ok=ok||name==option;
            if(!ok)throw Error("--"+name+" is not valid for "+command);
        }
    }
};
}
int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
        if(argc>1&&std::string_view(argv[1]).starts_with("fast-"))return fast::cli_main(argc,argv);
        // Reject the removed backend before parsing options or opening files/devices.
        if(argc>1) {
            const std::set<std::string_view> retired={"tx","rx","listen","simulate","estimate","analyze-link","status-tx","status-rx","--tx","--rx"};
            if(retired.contains(argv[1]))throw Error(unavailable);
        }
        Args args(argc,argv);
        if(args.has("version")){std::cout<<"Data Pump "<<DATAPUMP_VERSION<<'\n';return 0;}
        if(args.has("help")||args.command.empty()){std::cout<<usage;return 0;}
        if(args.command=="devices") {
            args.only({});for(const auto& device:audio::devices())std::cout<<device.id<<'\t'<<device.description<<'\n';return 0;
        }
        if(args.command=="keys") {
            args.only({"keyfile","pad"});if(!args.has("keyfile"))throw Error("keys requires --keyfile PATH");
            for(const auto& entry:load_keyring(args.get("keyfile"),args.has("pad")?std::optional<std::filesystem::path>(args.get("pad")):std::nullopt))std::cout<<entry.name<<'\n';
            return 0;
        }
        if(args.command=="keygen") {
            args.only({"output","key-names","device","pad"});if(!args.has("output"))throw Error("keygen requires --output PATH");
            std::vector<std::string> names;std::istringstream input(args.get("key-names","Default"));std::string name;
            while(std::getline(input,name,','))names.push_back(name);
            if(args.get("key-names").ends_with(','))throw Error("key names must not be empty");
            KeyfileGenerationOptions generation;generation.audio=audio::keyfile_audio_source(args.get("device","default"));
            generation.allow_audio_fallback=true;generation.progress=[](std::string_view stage){std::cerr<<stage<<"...\n";};
            const auto result=create_keyring(args.get("output"),names,args.has("pad")?std::optional<std::filesystem::path>(args.get("pad")):std::nullopt,generation);
            std::cerr<<"Created symmetric keyfile: "<<args.get("output")<<'\n';
            if(!result.warning.empty())std::cerr<<"WARNING: "<<result.warning<<'\n';
            return 0;
        }
        if(args.command=="qr") {
            args.only({"text","input","format","output"});
            if(args.has("text")&&args.has("input"))throw Error("choose --text or --input");
            Bytes bytes;
            if(args.has("text")){const auto text=args.get("text");bytes.assign(text.begin(),text.end());}
            else if(args.has("input")&&args.get("input")!="-") {std::ifstream in(args.get("input"),std::ios::binary);if(!in)throw Error("cannot open input");bytes=read_bounded(in,default_memory_limit);}
            else bytes=read_bounded(std::cin,default_memory_limit);
            const std::string text(bytes.begin(),bytes.end());const auto format=args.get("format","svg");
            if(format!="svg"&&format!="pbm")throw Error("QR format must be svg or pbm");
            const auto image=format=="svg"?qr_svg(text):qr_pbm(text,1);
            if(args.has("output"))write_new_file(args.get("output"),Bytes(image.begin(),image.end()));else std::cout<<image;
            return 0;
        }
        throw Error("unknown command: "+args.command);
    } catch(const std::exception& error){std::cerr<<"pump: "<<error.what()<<'\n';return 2;}
}
