#include "alsa_stub.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace alsa_test {
// CLI subprocesses get one deterministic 48 kHz endpoint. Unit fixtures reset
// this state and configure their own devices before each contract scenario.
State state=[] {State value;value.available={"default"};value.supported_rates={48000};return value;}();
void reset(){state=State{};}
}
extern "C" {
int snd_pcm_open(void** pcm,const char* name,int direction,int) {
    auto& s=alsa_test::state;s.attempts.emplace_back(name);
    for(const auto& [device,error]:s.open_errors)if(device==name){if(s.after_open)s.after_open();return error;}
    if(std::find(s.available.begin(),s.available.end(),name)==s.available.end()){if(s.after_open)s.after_open();return -2;}
    *pcm=reinterpret_cast<void*>(1);s.selected=name;s.direction=direction;++s.opens;++s.live;
    if(s.after_open)s.after_open();return 0;
}
int snd_pcm_set_params(void*,int format,int access,unsigned channels,unsigned rate,int,unsigned) {
    auto& s=alsa_test::state;s.rate=rate;s.configured.emplace_back(s.selected,rate);
    s.configured_channels.push_back(channels);
    if(s.after_configure)s.after_configure();
    if(format!=2 || access!=3 || (channels!=1 && channels!=2))throw std::runtime_error("expected interleaved S16 ALSA PCM");
    if(s.direction && channels!=1)throw std::runtime_error("capture channel contract changed");
    if(std::find(s.supported_channels.begin(),s.supported_channels.end(),channels)==s.supported_channels.end())return -22;
    if(!s.supported_rates.empty() && std::find(s.supported_rates.begin(),s.supported_rates.end(),rate)==s.supported_rates.end())return -22;
    if(std::find(s.wrong_format.begin(),s.wrong_format.end(),s.selected)!=s.wrong_format.end())return -22;
    s.channels=channels;++s.configured_streams;return 0;
}
long snd_pcm_readi(void*,void* buffer,unsigned long count) {
    auto* pcm=static_cast<std::int16_t*>(buffer);
    auto& s=alsa_test::state;
    count=std::min(count,static_cast<unsigned long>(s.read_limit));
    for(std::size_t i=0;i<count;++i) {
        pcm[i]=s.sample?s.sample(s.captured,s.rate):static_cast<std::int16_t>(s.captured%32768);
        ++s.captured;
    }
    return static_cast<long>(count);
}
long snd_pcm_writei(void*,const void* buffer,unsigned long count) {
    auto& s=alsa_test::state;s.write_frames.push_back(count);count=std::min(count,static_cast<unsigned long>(s.write_limit));
    const auto* pcm=static_cast<const std::int16_t*>(buffer);s.played.insert(s.played.end(),pcm,pcm+count*s.channels);
    return static_cast<long>(count);
}
int snd_pcm_recover(void*,int,int){return -1;}
int snd_pcm_drain(void*){return 0;}
int snd_pcm_close(void*){++alsa_test::state.closes;--alsa_test::state.live;return 0;}
int snd_pcm_wait(void*,int){return 1;}
int snd_device_name_hint(int,const char*,void*** hints) {
    auto& s=alsa_test::state;auto& list=s.hints;++s.hint_calls;
    *hints=static_cast<void**>(std::calloc(list.size()+1,sizeof(void*)));
    for(std::size_t i=0;i<list.size();++i)(*hints)[i]=&list[i];
    if(s.after_hint)s.after_hint();return s.hint_error;
}
char* snd_device_name_get_hint(const void* hint,const char* id) {
    const auto& item=*static_cast<const alsa_test::Hint*>(hint);
    const auto& value=std::strcmp(id,"IOID")==0?item.io:item.name;
    if(value.empty())return nullptr;
    auto* result=static_cast<char*>(std::malloc(value.size()+1));std::memcpy(result,value.c_str(),value.size()+1);return result;
}
int snd_device_name_free_hint(void** hints){++alsa_test::state.hints_freed;std::free(hints);return 0;}
}
