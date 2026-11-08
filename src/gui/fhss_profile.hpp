#pragma once
#include "datapump/modem.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include "../frequency_parse.hpp"
#include <vector>
#include <span>
#include <charconv>
#include <limits>

namespace datapump::gui::fhss {
inline constexpr std::size_t list_limit=200,list_byte_limit=8192;
// Pure argument/editor parsing. File access is confined to the controller.
inline std::vector<double> parse_list(std::string_view text) {
    if(text.size()>list_byte_limit)throw std::invalid_argument("FHSS list exceeds 8192 bytes");
    std::string uncommented(text);
    for(std::size_t at=0;(at=uncommented.find('#',at))!=std::string::npos;) {
        const auto end=uncommented.find('\n',at);
        uncommented.erase(at,end==std::string::npos?end:end-at);
    }
    text=uncommented;
    std::vector<double> result;
    while(!text.empty()) {
        const auto end=text.find_first_of(",;\n");
        auto item=text.substr(0,end);
        if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
        if(const auto comment=item.find('#');comment!=std::string_view::npos)item=item.substr(0,comment);
        while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.front())))item.remove_prefix(1);
        while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.back())))item.remove_suffix(1);
        if(item.empty())continue;
        const auto hz=frequency_input::parse(item);
        if(!hz||*hz<0)throw std::invalid_argument("FHSS list requires nonnegative finite Shift frequencies in Hz, kHz, MHz, GHz or THz");
        if(std::find(result.begin(),result.end(),*hz)!=result.end())throw std::invalid_argument("FHSS list contains a duplicate frequency");
        if(result.size()==list_limit)throw std::invalid_argument("FHSS list permits at most 200 frequencies");
        result.push_back(*hz);
    }
    if(result.size()<2)throw std::invalid_argument("FHSS list requires 2–200 distinct frequencies");
    return result;
}
inline std::string list_text(std::span<const double> list,char separator=',') {
    std::string result;
    for(const auto hz:list) {
        char buffer[32];const auto number=std::to_chars(buffer,buffer+sizeof(buffer),hz,
            std::chars_format::general,std::numeric_limits<double>::max_digits10);
        if(number.ec!=std::errc{})throw std::invalid_argument("FHSS frequency formatting failed");
        if(!result.empty())result+=separator;
        result.append(buffer,number.ptr);
    }
    return result;
}
enum class Preference { off, fcc, eu, list };
inline Preference parse(std::string_view id) {
    if(id=="off")return Preference::off;
    // The original illustration had no jurisdiction. Migrate to FCC once;
    // the preference is subsequently independent of the resolved RF band.
    if(id=="fake-fcc"||id=="fake-0.4s-200")return Preference::fcc;
    if(id=="fake-eu")return Preference::eu;
    if(id=="fake-list")return Preference::list;
    throw std::invalid_argument("FHSS must be off, fake-fcc, fake-eu or fake-list.");
}
inline std::string_view id(Preference p) {
    return p==Preference::fcc?"fake-fcc":p==Preference::eu?"fake-eu":p==Preference::list?"fake-list":"off";
}
struct Band {double lower_hz=0,upper_hz=0;};
// The saved preference/list is retained for the canonical triad backend.
}
