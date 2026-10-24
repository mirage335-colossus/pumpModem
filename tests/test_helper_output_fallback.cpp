#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint64_t epoch=1800000000;
void check(bool value,const char* message) {if(!value)throw Error(message);}
struct Provider {
    std::atomic<unsigned> opened=0,closed=0,formats=0,source_calls=0;
    std::atomic<bool> capture_started=false;
    bool reject_helper_rate=false,fail_after_pcm=false,accept_buffered_utc=false,timing_reported=false;
    std::optional<long double> accepted_utc;
    std::uint32_t output_rate=0;
    std::vector<std::uint32_t> requested_rates;
    std::vector<float> pcm;
};
Provider* provider=nullptr;
live::Settings settings() {
    live::Settings value;value.device="helper output fixture";value.full_duplex=true;
    value.transfer.timestamp=epoch;value.transfer.search_seconds=0;
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    value.transfer.key.emplace(Bytes(32,0x63));
    auto& c=value.transfer.modem;
    c.sample_rate=8000;c.carrier_hz=1000;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.scramble=true;
    value.content_limit=1024*1024;value.dsp_workspace_bytes=32*1024*1024;
    value.helper.power_w=100;return value;
}
struct Result {
    live::Snapshot snapshot;
    unsigned opened=0,closed=0,formats=0,source_calls=0;
    bool timing_reported=false;
    std::optional<long double> accepted_utc;
    std::uint32_t output_rate=0;
    std::vector<std::uint32_t> requested_rates;
    std::vector<float> pcm;
};
Result run(live::Settings value,bool reject_rate=false,bool gap=false,bool helper_only=false,
           std::shared_ptr<const modem::FhssPlan> hopping={}) {
    Provider script;script.reject_helper_rate=reject_rate;script.fail_after_pcm=gap;
    script.accept_buffered_utc=bool(hopping);
    provider=&script;
    struct Release {~Release(){provider=nullptr;}}release;
    Result result;
    {
        live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!script.capture_started && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(1ms);
        check(script.capture_started,"fallback fixture capture did not start");
        if(helper_only)session.transmit_helper(std::move(hopping));
        else session.transmit_bits(Bytes{0,0,1},false,std::move(hopping));
        bool finished=false;
        while(std::chrono::steady_clock::now()<deadline) {
            auto state=session.snapshot();
            if(state.transmission_id && !state.transmitting && state.transmission_finished) {
                result.snapshot=std::move(state);finished=true;break;
            }
            std::this_thread::sleep_for(1ms);
        }
        session.stop();
        check(finished,"fallback fixture did not finish the transmit request");
    } // All source/capture callbacks join before provider state is inspected.
    result.opened=script.opened;result.closed=script.closed;
    result.formats=script.formats;result.source_calls=script.source_calls;
    result.output_rate=script.output_rate;
    result.timing_reported=script.timing_reported;result.accepted_utc=script.accepted_utc;
    result.requested_rates=std::move(script.requested_rates);result.pcm=std::move(script.pcm);
    return result;
}
void rate_failure_retries_ordinary() {
    auto value=settings();auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline),fallback=run(value,true);
    check(!ordinary.snapshot.transmission_failed&&!ordinary.pcm.empty(),"ordinary fallback control failed");
    check(!fallback.snapshot.transmission_failed && fallback.snapshot.error.empty() &&
          fallback.snapshot.transmission_fraction==1,"helper provider-rate failure aborted Robust TX");
    check(fallback.opened==2 && fallback.closed==2 && fallback.formats==1 &&
          fallback.requested_rates.size()==2 && fallback.requested_rates[0]>=48000 &&
          fallback.requested_rates[1]==0,"helper rate failure did not make exactly one pre-PCM ordinary retry");
    check(fallback.pcm==ordinary.pcm,"disabled helper retained audio scaling or changed the ordinary payload");
    check(fallback.snapshot.helper_status.find("transmitting ordinary Robust signal")!=std::string::npos,
          "ordinary output fallback omitted its helper-unavailable notice");
}
void low_geometry_yields_to_payload() {
    auto value=settings();value.transfer.modem.carrier_hz=200;value.helper.frequency=helper::Frequency::low;
    auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline),fallback=run(value);
    check(!fallback.snapshot.transmission_failed && fallback.snapshot.error.empty() &&
          fallback.snapshot.transmission_fraction==1,"invalid Low helper geometry aborted Robust TX");
    check(fallback.opened==1 && fallback.formats==1 && fallback.requested_rates==std::vector<std::uint32_t>{0},
          "invalid Low geometry opened helper output or restarted ordinary output");
    check(!ordinary.pcm.empty() && fallback.pcm==ordinary.pcm,
          "invalid Low helper geometry changed ordinary transmit PCM");
}
void emitted_gap_is_never_retried() {
    const auto result=run(settings(),false,true);
    check(result.snapshot.transmission_failed && result.snapshot.error=="fixture PCM discontinuity" &&
          result.snapshot.transmission_fraction<1,"actual output gap lost its latched TX failure");
    check(result.opened==1 && result.closed==1 && result.source_calls>0 && !result.pcm.empty(),
          "actual output gap restarted private PCM or occurred before any generated output");
    check(result.snapshot.transmit_key_lock_seconds>0,"actual output gap released the exposed private key epoch");
}
void helper_only_cannot_succeed_without_helper() {
    auto value=settings();value.transfer.modem.carrier_hz=200;value.helper.frequency=helper::Frequency::low;
    const auto result=run(value,false,false,true);
    check(result.snapshot.transmission_failed && !result.snapshot.error.empty() &&
          result.snapshot.transmission_fraction==0,"invalid helper-only request pretended to send a beacon");
    check(result.opened==0 && result.pcm.empty(),"invalid helper-only request emitted ordinary payload or silence");
}
void fhss_output_uses_one_gate_allowance() {
    auto value=settings();value.transfer.timestamp=0;value.transfer.audio_timing_error_seconds=0;
    value.transfer.modem.pulse_shaping=true;value.transfer.modem.spreading_factor=64;
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=200;hops->dwell_seconds=.4;
    for(unsigned i=0;i<hops->channels;++i)hops->shifts[i]=30000000.+2000*i;
    auto expected_config=value.transfer.modem;
    expected_config.fake_hopping=transfer::transmit_hopping(*hops,value.transfer);
    // Production FHSS needs finite pulse support. At 8 kHz and a 200 Hz
    // nominal rate the 80-sample chip has 1.25*8000/80 = 125 Hz RRC support;
    // its single 10 ms gate contributes another 200 Hz, for 325 Hz total.
    check(modem::waveform_gate_bandwidth_allowance_hz(expected_config)==200 &&
          helper::occupied_bandwidth(expected_config)==325,
          "FHSS helper fixture does not contain exactly one 200 Hz gate allowance");
    const auto common=helper::plan(value.helper,expected_config.carrier_hz,
        helper::occupied_bandwidth(expected_config),48000,0,
        modem::outer_dsss_transmit_gain(expected_config));
    const auto result=run(value,false,false,false,hops);
    check(!result.snapshot.transmission_failed && result.snapshot.error.empty() &&
          result.opened==1 && result.output_rate==48000 && result.pcm.size()>=48000,
          "FHSS helper fixture did not render one second of actual hardware-rate PCM");
    check(result.timing_reported && result.accepted_utc && *result.accepted_utc>epoch,
          "FHSS mock did not accept its fresh buffered UTC target");
    // Estimate the emitted carrier from real output samples. Independent 40 ms
    // coherent fits tolerate the helper's phase punctuation and metadata; this
    // is not a second call through the output-transform/configuration logic.
    const auto window=result.output_rate/25;
    const auto count=std::min<std::size_t>(result.pcm.size(),result.output_rate);
    double best_frequency=0,best_power=-1;
    for(int offset=-150;offset<=150;++offset) {
        const auto frequency=common.frequency_hz+offset;
        const auto step=std::polar(1.,-2*std::numbers::pi*frequency/result.output_rate);
        std::complex<double> oscillator{1,0},sum{};double power=0;
        for(std::size_t i=0;i<count;++i) {
            sum+=static_cast<double>(result.pcm[i])*oscillator;oscillator*=step;
            if((i+1)%window==0) {power+=std::norm(sum);sum={};}
        }
        if(power>best_power) {best_power=power;best_frequency=frequency;}
    }
    check(std::abs(best_frequency-common.frequency_hz)<=1,
          "FHSS emitted helper frequency disagrees with the common single-gate plan");
}
}

// Same existing link seam as live_transmit_lock/live_profiles: this fixture
// replaces device I/O, while Session, source generation, output-transform
// composition, resampling, completion and key exposure remain production code.
namespace datapump::audio {
std::vector<Device> devices(){return {{"helper output fixture","bounded synthetic output"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback&,std::stop_token stop,
             StreamFormatCallback format) {
    if(format)format({rate,48000,rate*.5,0});
    provider->capture_started=true;
    while(!stop.stop_requested())std::this_thread::sleep_for(1ms);
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options) {
    capture(rate,device,callback,stop,std::move(format));
}
void playback(std::uint32_t logical,const std::string&,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode,Options options) {
    validate_options(options);auto& script=*provider;
    ++script.opened;script.requested_rates.push_back(options.minimum_hardware_rate);
    struct Close {Provider& script;~Close(){++script.closed;}}close{script};
    if(script.reject_helper_rate&&options.minimum_hardware_rate)
        throw Error("fixture helper hardware rate unavailable");
    const auto hardware=options.minimum_hardware_rate?48000u:logical;
    script.output_rate=hardware;
    Resampler converter(logical,hardware);
    converter.set_output_gain(options.playback_output_gain);
    converter.set_output_transform(options.playback_output_transform);
    // Like the existing timed-provider fixture, accept the synthetic UTC
    // presentation target without waiting against the host's unrelated clock.
    // Only the new FHSS request uses this buffered-provider behavior.
    detail::UtcPlaybackScope schedule(script.accept_buffered_utc);
    if(script.accept_buffered_utc) {
        check(bool(options.timing_status),"FHSS mock omitted its timing status callback");
        options.timing_status({options.follow_system_clock,.05,.03,"fixture buffered UTC target accepted"});
        script.timing_reported=true;
    }
    if(format) {++script.formats;format({logical,hardware,logical*.5,converter.workspace_bytes()});}
    if(script.accept_buffered_utc)script.accepted_utc=detail::utc_playback_epoch;
    std::array<float,512> input{},output{};std::size_t offset=0,count=0;bool ending=false;
    while(!stop.stop_requested()&&!converter.finished()) {
        if(offset==count&&!ending) {
            count=source(input);++script.source_calls;offset=0;ending=count==0;
            check(count<=input.size(),"fixture source exceeded its supplied output extent");
        }
        const auto progress=converter.process(std::span<const float>(input).subspan(offset,count-offset),output,ending);
        offset+=progress.consumed;
        script.pcm.insert(script.pcm.end(),output.begin(),output.begin()+progress.produced);
        if(progress.produced&&script.fail_after_pcm) {
            check(bool(options.playback_discontinuity),"private source omitted its output discontinuity hook");
            options.playback_discontinuity("fixture PCM discontinuity");
            throw Error("private output discontinuity hook failed to stop playback");
        }
    }
    if(stop.stop_requested())throw Error("audio operation cancelled");
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode channels) {
    playback(rate,device,source,stop,std::move(format),channels,{});
}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("unexpected direct playback in helper fallback fixture");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected record in helper fallback fixture");
}
}
int main() {
    try {
        rate_failure_retries_ordinary();low_geometry_yields_to_payload();emitted_gap_is_never_retried();
        helper_only_cannot_succeed_without_helper();
        fhss_output_uses_one_gate_allowance();
        std::cout<<"Helper output fallback, exact ordinary PCM, discontinuity, helper-only failure and FHSS placement passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
