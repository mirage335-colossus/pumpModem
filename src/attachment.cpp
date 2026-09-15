#include "datapump/attachment.hpp"
#include <algorithm>

namespace datapump::attachment {
namespace {
bool valid_name(std::string_view name) {
    if(name.empty() || name.size()>filename_limit || name=="." || name==".." ||
       name.back()=='.' || name.back()==' ' || name.find(suffix)!=name.npos)return false;
    for(std::size_t i=0;i<name.size();) {
        const auto first=static_cast<unsigned char>(name[i++]);
        if(first<0x20 || first==0x7f || std::string_view("/\\:<>\"|?*").find(first)!=std::string_view::npos)return false;
        if(first<0x80)continue;
        unsigned count;std::uint32_t value,minimum;
        if(first>=0xc2 && first<=0xdf){count=1;value=first&31;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){count=2;value=first&15;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){count=3;value=first&7;minimum=0x10000;}
        else return false;
        if(count>name.size()-i)return false;
        while(count--) {
            const auto next=static_cast<unsigned char>(name[i++]);
            if((next&0xc0)!=0x80)return false;
            value=(value<<6)|(next&63);
        }
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff) ||
           (value>=0x80 && value<=0x9f))return false;
    }
    return true;
}
}
std::string marker(std::string_view filename) {
    if(!valid_name(filename))throw Error("Attachment filename must be a UTF-8 basename of 1..255 bytes without path or control characters");
    return std::string(prefix)+std::string(filename)+std::string(suffix);
}
std::size_t source_limit(std::size_t content_limit) {
    if(content_limit>Bytes{}.max_size()-marker_limit)throw Error("Attachment source quota exceeds address space");
    return content_limit+marker_limit;
}
Bytes encode(const Message& message,std::size_t content_limit) {
    if(message.data.size()>content_limit)throw Error("Message exceeds content limit");
    if(message.kind==MessageKind::text)return message.data;
    if(message.kind!=MessageKind::file && message.kind!=MessageKind::screenshot)throw Error("Invalid local source kind");
    const auto label=marker(message.filename);
    (void)source_limit(content_limit);
    Bytes source;source.reserve(message.data.size()+label.size());
    source.insert(source.end(),label.begin(),label.end());
    source.insert(source.end(),message.data.begin(),message.data.end());
    return source;
}
void interpret(Message& message,std::size_t content_limit) {
    message.kind=MessageKind::text;message.filename.clear();message.repeatable=false;
    const auto& data=message.data;
    if(data.size()>=prefix.size()+suffix.size() && std::equal(prefix.begin(),prefix.end(),data.begin())) {
        const auto bounded=std::min(data.size()-prefix.size(),filename_limit+suffix.size());
        const std::string_view window(reinterpret_cast<const char*>(data.data()+prefix.size()),bounded);
        const auto end=window.find(suffix);
        if(end!=window.npos && valid_name(window.substr(0,end))) {
            message.filename=std::string(window.substr(0,end));message.kind=MessageKind::file;
            message.data.erase(message.data.begin(),message.data.begin()+static_cast<std::ptrdiff_t>(prefix.size()+end+suffix.size()));
        }
    }
    if(message.data.size()>content_limit)throw Error("Decoded source exceeds content limit");
}
}
