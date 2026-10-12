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
namespace {
struct HardwareParams {
    int access=0,format=0;unsigned channels=0,rate=0,resample=1;
    unsigned long buffer=0,period=0;
};
struct SoftwareParams {unsigned long avail_min=0,start_threshold=0;};
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
int snd_pcm_get_params(void*,unsigned long* buffer,unsigned long* period) {
    *buffer=alsa_test::state.buffer_frames;*period=alsa_test::state.period_frames;return 0;
}
int snd_pcm_hw_params_malloc(HardwareParams** params) {
    auto& s=alsa_test::state;if(!s.period_refinement)return -38;
    *params=new HardwareParams;++s.parameter_objects;return 0;
}
void snd_pcm_hw_params_free(HardwareParams* p){delete p;--alsa_test::state.parameter_objects;}
int snd_pcm_sw_params_malloc(SoftwareParams** params) {
    *params=new SoftwareParams;++alsa_test::state.parameter_objects;return 0;
}
void snd_pcm_sw_params_free(SoftwareParams* p){delete p;--alsa_test::state.parameter_objects;}
int snd_pcm_hw_params_current(void*,HardwareParams* p) {
    const auto& s=alsa_test::state;*p={3,2,s.channels,s.rate,0,s.buffer_frames,s.period_frames};return 0;
}
int snd_pcm_hw_free(void*) {
    auto& s=alsa_test::state;++s.hardware_releases;s.hardware_released=true;return s.hardware_release_result;
}
int snd_pcm_hw_params_any(void*,HardwareParams* p){
    if(!alsa_test::state.hardware_released)throw std::runtime_error("hardware still constrains period negotiation");
    *p={};return 0;
}
int snd_pcm_sw_params_current(void*,SoftwareParams* p) {
    const auto& s=alsa_test::state;*p={s.avail_min,s.start_threshold};return 0;
}
int snd_pcm_hw_params_set_access(void*,HardwareParams* p,int access){p->access=access;return 0;}
int snd_pcm_hw_params_set_format(void*,HardwareParams* p,int format){p->format=format;return 0;}
int snd_pcm_hw_params_set_channels(void*,HardwareParams* p,unsigned channels){p->channels=channels;return 0;}
int snd_pcm_hw_params_set_rate(void*,HardwareParams* p,unsigned rate,int direction){
    if(direction)throw std::runtime_error("expected exact rate constraint");p->rate=rate;return 0;
}
int snd_pcm_hw_params_set_rate_resample(void*,HardwareParams* p,unsigned resample){p->resample=resample;return 0;}
int snd_pcm_hw_params_set_buffer_size(void*,HardwareParams* p,unsigned long buffer){p->buffer=buffer;return 0;}
int snd_pcm_hw_params_set_period_size_near(void*,HardwareParams* p,unsigned long* period,int*) {
    auto& s=alsa_test::state;++s.period_requests;s.requested_period=*period;
    if(s.after_period_request)s.after_period_request();
    // current() fixes all intervals and therefore cannot be modified this way.
    if(s.period_candidate_rejected || p->period)return -22;
    p->period=s.negotiated_period?s.negotiated_period:*period;*period=p->period;return 0;
}
int snd_pcm_hw_params(void*,HardwareParams* p) {
    auto& s=alsa_test::state;
    if(p->access!=3 || p->format!=2 || p->channels!=s.channels || p->rate!=s.rate ||
       p->resample!=0 || p->buffer!=s.buffer_frames || !p->period)
        throw std::runtime_error("period negotiation changed fixed hardware configuration");
    // Model a partial apply even when the driver reports failure.
    s.hardware_released=false;s.period_frames=p->period;s.avail_min=p->period;s.start_threshold=0;
    const auto n=s.hw_applies++;
    return n<s.hw_apply_results.size()?s.hw_apply_results[n]:0;
}
int snd_pcm_sw_params(void*,SoftwareParams* p) {
    auto& s=alsa_test::state;s.avail_min=p->avail_min;s.start_threshold=p->start_threshold;
    const auto n=s.sw_applies++;
    return n<s.sw_apply_results.size()?s.sw_apply_results[n]:0;
}
int snd_pcm_delay(void*,long* delay) {
    auto& s=alsa_test::state;++s.delay_calls;if(s.before_delay)s.before_delay();
    const auto result=s.delay_result<s.delay_results.size()?s.delay_results[s.delay_result++]:0;
    if(result<0)return result;
    *delay=s.delay_observation?s.delay_observation():s.delay_frames;return result;
}
int snd_pcm_state(void*) {
    auto& s=alsa_test::state;return s.observe_state?s.observe_state():s.pcm_state;
}
long snd_pcm_readi(void*,void* buffer,unsigned long count) {
    auto* pcm=static_cast<std::int16_t*>(buffer);
    auto& s=alsa_test::state;
    if(s.read_result<s.read_results.size()) {
        const auto result=s.read_results[s.read_result++];
        if(result<=0)return result;
        count=std::min(count,static_cast<unsigned long>(result));
    }
    count=std::min(count,static_cast<unsigned long>(s.read_limit));
    for(std::size_t i=0;i<count;++i) {
        pcm[i]=s.sample?s.sample(s.captured,s.rate):static_cast<std::int16_t>(s.captured%32768);
        ++s.captured;
    }
    return static_cast<long>(count);
}
long snd_pcm_writei(void*,const void* buffer,unsigned long count) {
    auto& s=alsa_test::state;s.write_frames.push_back(count);if(s.before_write)s.before_write();
    if(s.write_result<s.write_results.size()) {
        const auto result=s.write_results[s.write_result++];
        if(result<=0)return result;
        count=std::min(count,static_cast<unsigned long>(result));
    }
    count=std::min(count,static_cast<unsigned long>(s.write_limit));
    const auto* pcm=static_cast<const std::int16_t*>(buffer);s.played.insert(s.played.end(),pcm,pcm+count*s.channels);if(s.after_write)s.after_write(count);
    return static_cast<long>(count);
}
int snd_pcm_recover(void*,int error,int){
    auto& s=alsa_test::state;s.recovered_errors.push_back(error);
    if(s.recover_result>=0 && s.after_recover)s.after_recover();
    return s.recover_result;
}
int snd_pcm_drain(void*){
    auto& s=alsa_test::state;++s.drain_calls;
    const auto result=s.drain_result<s.drain_results.size()?s.drain_results[s.drain_result++]:0;
    if(s.after_drain)s.after_drain();
    return result;
}
int snd_pcm_drop(void*){
    auto& s=alsa_test::state;++s.drops;s.stopped=true;
    if(s.after_drop)s.after_drop();return 0;
}
int snd_pcm_prepare(void*){
    auto& s=alsa_test::state;++s.prepares;s.stopped=false;
    if(s.after_prepare)s.after_prepare();return 0;
}
int snd_pcm_close(void*){++alsa_test::state.closes;--alsa_test::state.live;return 0;}
int snd_pcm_wait(void*,int timeout){
    auto& s=alsa_test::state;if(s.before_wait)s.before_wait(timeout);
    return s.wait_result<s.wait_results.size()?s.wait_results[s.wait_result++]:1;
}
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
