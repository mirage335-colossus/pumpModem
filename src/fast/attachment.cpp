#include "datapump/fast/attachment.hpp"
#include <algorithm>
#include <limits>

namespace datapump::fast::attachment {
namespace {
bool valid_name(std::string_view name) {
    if(name.empty()||name.size()>filename_limit||name=="."||name==".."||
       name.back()=='.'||name.back()==' ')return false;
    // The appended separator must not complete an earlier closing marker.
    if((std::string(name)+std::string(closing)).find(closing)!=name.size())return false;
    for(std::size_t i=0;i<name.size();) {
        const auto first=static_cast<unsigned char>(name[i++]);
        if(first<0x20||first==0x7f||std::string_view("/\\:<>\"|?*").find(first)!=std::string_view::npos)return false;
        if(first<0x80)continue;
        unsigned continuation=0;std::uint32_t codepoint=0,minimum=0;
        if(first>=0xc2&&first<=0xdf){continuation=1;codepoint=first&31;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){continuation=2;codepoint=first&15;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){continuation=3;codepoint=first&7;minimum=0x10000;}
        else return false;
        if(continuation>name.size()-i)return false;
        while(continuation--) {
            const auto next=static_cast<unsigned char>(name[i++]);
            if((next&0xc0)!=0x80)return false;
            codepoint=(codepoint<<6)|(next&63);
        }
        if(codepoint<minimum||codepoint>0x10ffff||(codepoint>=0xd800&&codepoint<=0xdfff)||
           (codepoint>=0x80&&codepoint<=0x9f))return false;
    }
    return true;
}
}
std::string filename_from_path(const std::filesystem::path& path) {
    const auto bytes=path.filename().u8string();
    const std::string name(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(!valid_name(name))throw Error("Fast attachment filename must be a UTF-8 basename of 1..255 bytes without path or control characters");
    return name;
}
std::string prefix(std::string_view name) {
    if(!valid_name(name))throw Error("Fast attachment filename must be a UTF-8 basename of 1..255 bytes without path or control characters");
    return std::string(opening)+std::string(name)+std::string(closing);
}
std::uint64_t source_limit(std::uint64_t limit) {
    if(limit>std::numeric_limits<std::uint64_t>::max()-prefix_limit)
        throw Error("Fast attachment source quota exceeds address space");
    return limit+prefix_limit;
}
SourceReader source(SourceReader reader,std::string_view filename,std::uint64_t limit) {
    if(!reader)throw Error("Missing Fast attachment source reader");
    return [reader=std::move(reader),header=prefix(filename),position=std::size_t{0},used=std::uint64_t{0},limit]
           (std::span<std::uint8_t> out) mutable {
        if(position<header.size()) {
            const auto n=std::min(out.size(),header.size()-position);
            std::copy_n(header.begin()+static_cast<std::ptrdiff_t>(position),n,out.begin());position+=n;return n;
        }
        const auto n=reader(out);
        if(n>out.size())throw Error("Fast attachment reader exceeded its buffer");
        if(n>limit-used)throw Error("Fast attachment content exceeds local storage quota");
        used+=n;return n;
    };
}
Description inspect(std::span<const std::uint8_t> bytes) {
    if(bytes.size()<opening.size()+closing.size()||!std::equal(opening.begin(),opening.end(),bytes.begin()))return {};
    const auto width=std::min(bytes.size()-opening.size(),filename_limit+closing.size());
    const std::string_view window(reinterpret_cast<const char*>(bytes.data()+opening.size()),width);
    const auto end=window.find(closing);
    if(end==window.npos||!valid_name(window.substr(0,end)))return {};
    return {std::string(window.substr(0,end)),opening.size()+end+closing.size()};
}
}
