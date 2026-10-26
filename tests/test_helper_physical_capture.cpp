#include "datapump/channel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace datapump;
namespace {
using Channel=modem::SampledSimulationChannel;
void check(bool value,const char* reason) {if(!value)throw Error(reason);}
template<class F>void rejects(F action,const char* reason) {
    bool rejected=false;try {action();}catch(const Error&) {rejected=true;}
    check(rejected,reason);
}
helper::Options helper_options() {
    helper::Options h;h.power_w=15000;h.data_power_w=4;h.timecode=false;return h;
}
modem::ChannelConfig impairments() {
    modem::ChannelConfig c;c.snr_db=300;c.clock_error_ppm=137;
    c.phase_noise_degrees_per_sqrt_second=0;c.seed=581;return c;
}
modem::Config config() {
    modem::Config c;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=4;c.integration_seconds=.02;c.stream_epoch=1800000001;
    c.stream_phase_samples=137;return c;
}
struct Observer {
    std::uint64_t seen=0;
    std::uint32_t rate=0;
    double late_energy=0;
    std::uint64_t late_count=0;
    void operator()(std::span<const float> pcm,std::uint32_t hz,std::uint64_t first) {
        check(first==seen,"raw capture skipped or duplicated physical samples");
        check(!rate||rate==hz,"raw capture changed physical sample rate");rate=hz;
        for(std::size_t i=0;i<pcm.size();++i)if(first+i>=hz/2) {
            late_energy+=static_cast<double>(pcm[i])*pcm[i];++late_count;
        }
        seen+=pcm.size();
    }
};
std::vector<float> capture(Channel& channel,modem::StreamingTransmitter& source,std::size_t block) {
    std::vector<float> all;std::vector<float> pcm(block);
    while(const auto n=channel.read(source,pcm))all.insert(all.end(),pcm.begin(),pcm.begin()+static_cast<std::ptrdiff_t>(n));
    return all;
}
void explicit_raw_domain() {
    auto c=config();c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;
    auto model=impairments();model.frequency_offset_hz=3000;
    const auto h=helper_options();
    rejects([&] {Channel old(c,model,h);},"ordinary narrow route silently accepted a lost payload passband");
    const Channel::PreconversionCapture domain{-10000,13000,48000};
    Channel raw(c,model,h,0,{},domain);
    check(raw.physical_sample_rate()>=48000&&raw.physical_sample_rate()/2.>13010,
          "raw clock did not cover the declared signed capture domain");
    check(!raw.logical_payload_preserved(),"clipped logical baseline claimed to preserve wide payload");
    check(raw.working_bytes()<=Channel::workspace_bound,"raw capture escaped fixed channel workspace");
    modem::StreamingTransmitter source(modem::RawBits{{0,1,0}},c);
    std::array<float,31> small{};
    rejects([&] {raw.read(source,small);},"raw-domain bypass accepted no physical PCM consumer");
    Observer observer;raw.set_physical_observer(std::ref(observer));
    raw.prepare_helper_lead(source,2*c.sample_rate);
    std::vector<float> lead(c.sample_rate);
    check(raw.read(source,lead)==lead.size(),"prepared helper lead did not fill its dated idle interval");
    check(!source.samples_emitted()&&!raw.transmitted_samples(),"helper-only idle consumed Robust source PCM");
    check(observer.seen==raw.physical_received_samples()&&observer.late_count&&
          observer.late_energy/observer.late_count>.1,"physical raw lead omitted its real helper tone");
    // The legacy logical offset restriction is relaxed only inside this
    // explicit physical certificate, never by the ordinary channel API.
    model.frequency_offset_hz=6000;
    Channel wider(c,model,h,0,{},domain);
    rejects([&] {Channel old(c,model,h);},"ordinary offset validation was widened globally");
    rejects([&] {Channel bad(c,model,h,0,{},Channel::PreconversionCapture{-100,100,48000});},
            "actual helper/data signal escaped its declared raw domain");
    rejects([&] {Channel bad(c,model,{},0,{},domain);},"disabled helper bypassed ordinary capture validation");
    rejects([&] {Channel bad(c,model,h,0,{},Channel::PreconversionCapture{-1e9,1e9,0});},
            "unbounded physical capture domain was accepted");
}
void dated_lead_preserves_source() {
    const auto c=config();auto model=impairments();model.frequency_offset_hz=2.3;
    const auto h=helper_options();const std::uint64_t idle=3*c.sample_rate+137;
    Channel old(c,model,h),early(c,model,h);
    std::vector<float> old_idle(idle),early_idle(idle);old.read_noise(old_idle);
    old.begin_burst();early.begin_burst();
    modem::StreamingTransmitter a(modem::RawBits{{0,0,1}},c),b(modem::RawBits{{0,0,1}},c);
    const auto source_origin=b.physical_output_origin(),source_end=b.total_samples();
    Observer observer;early.set_physical_observer(std::ref(observer));
    early.prepare_helper_lead(b,idle);
    check(early.read(b,early_idle)==idle,"helper prelude did not fill exact receiver-clock idle");
    check(!b.samples_emitted()&&!early.transmitted_samples(),"dated prelude advanced keyed Robust source");
    check(observer.late_count&&observer.late_energy/observer.late_count>.1,
          "dated idle still contained noise without the helper");
    const auto x=capture(old,a,137),y=capture(early,b,31);
    check(x.size()==y.size()&&old.payload_begin_sample()==early.payload_begin_sample()&&
          old.payload_end_sample()==early.payload_end_sample(),"early helper moved the ordinary physical payload interval");
    check(b.physical_output_origin()==source_origin&&b.total_samples()==source_end&&b.finished()&&
          early.transmitted_samples()==source_end,"early helper changed or truncated the keyed source");
    check(a.emitted_payload_epoch()==b.emitted_payload_epoch()&&
          a.source_valid_payload_end()==b.source_valid_payload_end(),"helper prelude changed wire or payload coordinates");
    // Once the old route's short onset/filter transient has elapsed, both
    // routes have the same oscillator, source and immutable helper phase.
    double error=0;
    for(std::size_t i=c.sample_rate;i<x.size();++i)error=std::max(error,std::abs(static_cast<double>(x[i])-y[i]));
    check(error<1e-6,"early helper phase/source convention changed at its normal lead handoff");
    rejects([&] {early.prepare_helper_lead(b,1);},"active helper source accepted a second preparation");
    Channel disabled(c,model);
    modem::StreamingTransmitter other(modem::RawBits{{1}},c);
    rejects([&] {disabled.prepare_helper_lead(other,1);},"disabled helper accepted a prelude");
    Channel overflow(c,model,h);
    rejects([&] {overflow.prepare_helper_lead(other,std::numeric_limits<std::uint64_t>::max());},
            "overflowing helper prelude was accepted");
    check(!overflow.payload_begin_sample()&&!other.samples_emitted(),
          "failed helper preparation published a partial source binding");
    overflow.prepare_helper_lead(other,17);
}
}
int main() {
    try {explicit_raw_domain();dated_lead_preserves_source();std::cout<<"Helper physical capture checks passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
