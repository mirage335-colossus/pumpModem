#include "launch_command.hpp"
#include "datapump/tuning.hpp"
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace datapump::gui::launch_command {
namespace {
constexpr std::size_t maximum_command_size=8192,maximum_arguments=128;
bool space(char value) {return std::isspace(static_cast<unsigned char>(value))!=0;}
[[noreturn]] void invalid(std::string message) {throw std::invalid_argument(std::move(message));}
std::vector<std::string> tokenize(std::string_view command) {
    if(command.size()>maximum_command_size)invalid("Launch command is too long (maximum 8192 bytes).");
    std::vector<std::string> arguments;
    std::string token;char quote=0;bool started=false;
    for(std::size_t i=0;i<command.size();++i) {
        const auto ch=command[i];
        if(ch=='\0')invalid("Launch command contains a null character.");
        if(ch=='\\'&&i+1<command.size()&&quote!='\'') {
            const auto next=command[i+1];
            // Preserve Windows path separators. Escapes only quote delimiters
            // or whitespace; a pasted line continuation performs no expansion.
            if(next=='"'||(!quote&&(next=='\''||space(next)))) {
                ++i;
                if(next=='\r'&&i+1<command.size()&&command[i+1]=='\n'){++i;continue;}
                if(next=='\n')continue;
                token+=next;started=true;continue;
            }
        }
        if(quote) {
            if(ch==quote)quote=0;else token+=ch;
            started=true;
        } else if(ch=='\''||ch=='"') {quote=ch;started=true;}
        else if(space(ch)) {
            if(started){arguments.push_back(std::move(token));token.clear();started=false;}
        } else {token+=ch;started=true;}
        if(arguments.size()>maximum_arguments)invalid("Launch command has too many arguments.");
    }
    if(quote)invalid("Launch command has an unclosed quote.");
    if(started)arguments.push_back(std::move(token));
    return arguments;
}
double number(std::string_view text,std::string_view flag,double low,double high,bool positive=false) {
    const auto original=text;
    if(!text.empty()&&text.front()=='+') {
        text.remove_prefix(1);
        if(!text.empty()&&(text.front()=='+'||text.front()=='-'))
            invalid(std::string(flag)+" requires a finite number; got '"+std::string(original)+"'.");
    }
    double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(value))
        invalid(std::string(flag)+" requires a finite number; got '"+std::string(original)+"'.");
    if(value<low||value>high||(positive&&value<=0))
        invalid(std::string(flag)+" is outside its supported range.");
    return value==0?0:value;
}
unsigned workspace(std::string_view text) {
    if(!text.empty()&&text.back()=='%')text.remove_suffix(1);
    if(text=="25")return 25;
    if(text=="50")return 50;
    if(text=="75")return 75;
    invalid("--dsp-workspace must be 25%, 50%, or 75%.");
}
double frequency(std::string_view text,std::string_view flag,double low) {
    while(!text.empty()&&space(text.front()))text.remove_prefix(1);
    while(!text.empty()&&space(text.back()))text.remove_suffix(1);
    const auto original=text;
    if(!text.empty()&&text.front()=='+')text.remove_prefix(1);
    double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(text.empty()||text.front()=='+'||(!original.empty()&&original.front()=='+'&&text.front()=='-')||
       parsed.ec!=std::errc{}||!std::isfinite(value))invalid(std::string(flag)+" requires a finite frequency in Hz, kHz, or MHz.");
    std::string suffix(parsed.ptr,text.data()+text.size());
    while(!suffix.empty()&&space(suffix.front()))suffix.erase(suffix.begin());
    for(auto& ch:suffix)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if(suffix=="khz"||suffix=="k")value*=1000;
    else if(suffix=="mhz"||suffix=="m")value*=1000000;
    else if(!suffix.empty()&&suffix!="hz")invalid(std::string(flag)+" requires Hz, kHz, or MHz units.");
    if(!std::isfinite(value)||value<low||value<=0||value>30000000)
        invalid(std::string(flag)+" is outside its supported frequency range.");
    return value;
}
std::string numeric(double value) {
    if(!std::isfinite(value))invalid("Cannot format a non-finite launch setting.");
    if(value==0)value=0;
    std::array<char,64> buffer{};
    const auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value,std::chars_format::general);
    if(result.ec!=std::errc{})invalid("Cannot format a launch setting.");
    return {buffer.data(),result.ptr};
}
}

Patch parse_arguments(std::span<const std::string> arguments) {
    if(arguments.empty())invalid("Enter a launch command or settings flags.");
    if(arguments.size()>maximum_arguments)invalid("Launch command has too many arguments.");
    std::size_t total=0;
    for(const auto& argument:arguments) {
        if(argument.size()>maximum_command_size-total)invalid("Launch command is too long (maximum 8192 bytes).");
        total+=argument.size();
        if(argument.find('\0')!=std::string::npos)invalid("Launch command contains a null character.");
    }
    Patch result;
    for(std::size_t i=0;i<arguments.size();++i) {
        const std::string_view argument=arguments[i];
        const auto equal=argument.find('=');
        const auto flag=argument.substr(0,equal);
        if(flag=="--auto-pattern") {
            if(equal!=std::string_view::npos)invalid("--auto-pattern does not take a value.");
            result.pattern="auto-pattern";continue;
        }
        const bool known=flag=="--tx-dbm"||flag=="--path-loss-db"||flag=="--noise-dbm-hz"||
            flag=="--oscillator"||flag=="--target-snr"||flag=="--short-target-snr"||flag=="--long-target-snr"||
            flag=="--rate"||flag=="--bw"||flag=="--carrier"||flag=="--dsp-workspace"||flag=="--pattern";
        if(!known)invalid("Unknown launch option: "+std::string(flag));
        std::string_view value;
        if(equal!=std::string_view::npos)value=argument.substr(equal+1);
        else {
            if(i+1==arguments.size()||std::string_view(arguments[i+1]).starts_with("--"))
                invalid("Missing value for "+std::string(flag)+".");
            value=arguments[++i];
        }
        if(value.empty())invalid("Missing value for "+std::string(flag)+".");
        if(flag=="--tx-dbm")result.tx_dbm=number(value,flag,-200,100);
        else if(flag=="--path-loss-db")result.path_loss_db=number(value,flag,0,500);
        else if(flag=="--noise-dbm-hz")result.noise_dbm_hz=number(value,flag,-250,0);
        else if(flag=="--target-snr")result.target_db_hz=number(value,flag,-200,200);
        else if(flag=="--short-target-snr")result.short_target_db_hz=number(value,flag,-200,200);
        else if(flag=="--long-target-snr")result.long_target_db_hz=number(value,flag,-200,200);
        else if(flag=="--rate"||flag=="--bw")result.rate_hz=frequency(value,flag,.01);
        else if(flag=="--carrier")result.carrier_hz=frequency(value,flag,0);
        else if(flag=="--dsp-workspace")result.workspace_percent=workspace(value);
        else if(flag=="--oscillator")result.oscillator=std::string(tuning::parse_oscillator_preset(value).id);
        else if(flag=="--pattern")result.pattern=std::string(tuning::pattern_mode_name(tuning::parse_pattern_mode(value)));
    }
    return result;
}
Patch parse(std::string_view command) {
    auto arguments=tokenize(command);
    // An executable token identifies a launch command; it is never executed
    // or inspected. Both slash styles and quoted paths remain ordinary text.
    if(!arguments.empty()&&arguments.front().empty())invalid("Launch command has an empty executable name.");
    if(!arguments.empty()&&!std::string_view(arguments.front()).starts_with("--"))arguments.erase(arguments.begin());
    return parse_arguments(arguments);
}
std::string format(const Patch& settings) {
    std::vector<std::string> arguments;
    const auto add=[&](std::string flag,std::string value) {
        arguments.push_back(std::move(flag));arguments.push_back(std::move(value));
    };
    if(settings.pattern) {
        if(*settings.pattern=="auto-pattern")arguments.emplace_back("--auto-pattern");
        else add("--pattern",*settings.pattern);
    }
    if(settings.tx_dbm)add("--tx-dbm",numeric(*settings.tx_dbm));
    if(settings.path_loss_db)add("--path-loss-db",numeric(*settings.path_loss_db));
    if(settings.noise_dbm_hz)add("--noise-dbm-hz",numeric(*settings.noise_dbm_hz));
    if(settings.oscillator)add("--oscillator",std::string(tuning::parse_oscillator_preset(*settings.oscillator).id));
    if(settings.target_db_hz)add("--target-snr",numeric(*settings.target_db_hz));
    if(settings.short_target_db_hz)add("--short-target-snr",numeric(*settings.short_target_db_hz));
    if(settings.long_target_db_hz)add("--long-target-snr",numeric(*settings.long_target_db_hz));
    if(settings.rate_hz)add("--rate",numeric(*settings.rate_hz));
    if(settings.carrier_hz)add("--carrier",numeric(*settings.carrier_hz));
    if(settings.workspace_percent)add("--dsp-workspace",std::to_string(*settings.workspace_percent)+"%");
    // Validate programmatic exports as strictly as pasted text before returning
    // any command. Canonical enum values contain no shell metacharacters.
    (void)parse_arguments(arguments);
#ifdef _WIN32
    std::string result=".\\datapump-gui.exe";
#else
    std::string result="./datapump-gui";
#endif
    for(const auto& argument:arguments){result+=' ';result+=argument;}
    return result;
}
}
