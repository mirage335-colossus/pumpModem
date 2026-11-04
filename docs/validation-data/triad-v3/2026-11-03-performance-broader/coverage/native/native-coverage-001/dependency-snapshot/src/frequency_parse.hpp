#pragma once
#include <charconv>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace datapump::frequency_input {
// Apply decimal units before the one floating-point conversion. Multiplying an
// already-rounded MHz/GHz/THz value can change a small Carrier - Shift result;
// long double does not provide extra precision on every supported platform.
inline std::optional<double> parse(std::string_view text) {
    const auto space=[](char c){return std::isspace(static_cast<unsigned char>(c))!=0;};
    const auto digit=[](char c){return c>='0'&&c<='9';};
    while(!text.empty()&&space(text.front()))text.remove_prefix(1);
    while(!text.empty()&&space(text.back()))text.remove_suffix(1);
    if(text.empty())return {};
    if(text.front()=='+') {
        text.remove_prefix(1);
        if(text.empty()||text.front()=='-'||text.front()=='+')return {};
    }
    std::size_t pos=0,digits=0;
    if(text[pos]=='-')++pos;
    while(pos<text.size()&&digit(text[pos])){++pos;++digits;}
    if(pos<text.size()&&text[pos]=='.') {
        ++pos;
        while(pos<text.size()&&digit(text[pos])){++pos;++digits;}
    }
    if(!digits)return {};
    const auto mantissa=text.substr(0,pos);
    int exponent=0;
    if(pos<text.size()&&(text[pos]=='e'||text[pos]=='E')) {
        ++pos;bool negative=false;
        if(pos<text.size()&&(text[pos]=='+'||text[pos]=='-'))negative=text[pos++]=='-';
        const auto first=pos;
        while(pos<text.size()&&digit(text[pos])) {
            if(exponent>1000000)return {};
            exponent=exponent*10+(text[pos++]-'0');
        }
        if(pos==first)return {};
        if(negative)exponent=-exponent;
    }
    while(pos<text.size()&&space(text[pos]))++pos;
    std::string unit(text.substr(pos));
    for(auto& c:unit)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(unit=="khz"||unit=="k")exponent+=3;
    else if(unit=="mhz"||unit=="m")exponent+=6;
    else if(unit=="ghz"||unit=="g")exponent+=9;
    else if(unit=="thz"||unit=="t")exponent+=12;
    else if(!unit.empty()&&unit!="hz")return {};
    const auto normalized=std::string(mantissa)+"e"+std::to_string(exponent);
    double value=0;
    const auto result=std::from_chars(normalized.data(),normalized.data()+normalized.size(),value,std::chars_format::general);
    if(result.ec!=std::errc{}||result.ptr!=normalized.data()+normalized.size()||!std::isfinite(value))return {};
    return value==0?0:value;
}
}
