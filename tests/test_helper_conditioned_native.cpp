#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint64_t epoch=1800000000;
void check(bool value,const char* reason){if(!value)throw Error(reason);}
std::atomic<bool> admitted=false,committed=false,replayed=false;
bool select_trace(const char* name)noexcept {
    const std::string_view event{name};
    if(event=="receiver_helper_conditioning_admitted")admitted=true;
    if(event=="receiver_helper_conditioning_committed")committed=true;
    if(event=="receiver_helper_conditioning_replayed")replayed=true;
    return event.starts_with("receiver_helper_conditioning_") || event=="receiver_helper_replay_ordinary_reset" ||
        event=="receiver_signal_commit" || event=="trace_begin";
}
std::uint64_t number(const std::string& line,const char* key) {
    const auto at=line.find('"'+std::string(key)+"\":");
    if(at==std::string::npos)throw Error("native helper trace lacks coordinate "+std::string(key));
    return std::stoull(line.substr(at+std::string(key).size()+3));
}
live::Settings settings(bool beacon) {
    live::Settings value;value.device="conditioned native capture";value.full_duplex=true;
    value.transfer.timestamp=epoch;value.transfer.search_seconds=2;
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    std::array<std::uint8_t,32> key{};for(std::size_t i=0;i<key.size();++i)key[i]=i*11+7;
    value.transfer.key=Crypto(key);
    auto& c=value.transfer.modem;c.sample_rate=12000;c.carrier_hz=1000;c.bandwidth_hz=200;
    c.spreading_factor=64;c.pulse_shaping=false;c.scramble=true;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    c.oscillator_search=modem::OscillatorSearchConfig{
        tuning::oscillator_model(tuning::parse_oscillator_preset("crystal")),
        tuning::oscillator_model(tuning::parse_oscillator_preset("ic-7100")),beacon?3.5e7:6e7,3};
    value.helper.power_w=1500;value.helper.callsign.clear();
    value.content_limit=1024*1024;value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=256*1024*1024;
    check(!value.simulation,"native capture fixture enabled Simulation");
    check(modem::oscillator_pattern_search(c).hypotheses.size()>=512,"native fixture lacks expensive original acquisition");
    return value;
}
struct Provider {
    live::Settings value;
    bool beacon;
    std::atomic<double> now{static_cast<double>(epoch)};
    std::atomic<std::uint64_t> released=0,delivered=0;
    std::atomic<unsigned> captures=0;
    bool fault=false;
    std::atomic<bool> injected=false;
};
Provider* provider=nullptr;

void run(bool beacon,bool fault=false) {
    auto value=settings(beacon);const auto rate=value.transfer.modem.sample_rate;
    const auto directory=std::getenv("DATAPUMP_HELPER_NATIVE_TRACE_DIR")?
        std::filesystem::path(std::getenv("DATAPUMP_HELPER_NATIVE_TRACE_DIR")):std::filesystem::temp_directory_path();
    const auto path=directory/("helper-conditioned-native-"+std::to_string(beacon)+"-"+std::to_string(fault)+"-"+
        std::to_string(audio::trace::identity())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    Provider script{value,beacon};script.fault=fault;provider=&script;
    struct Release {~Release(){provider=nullptr;}}release;
    admitted=false;committed=false;replayed=false;
    bool finished=false,exact=false;std::uint64_t samples=0,frontier=0,revision=0,replayed_at=0;
    try {
        audio::trace::Scope trace(path,false,&select_trace);
        live::Session session([&]{return script.now.load();});session.start(value);
        const auto deadline=std::chrono::steady_clock::now()+90s;
        while(std::chrono::steady_clock::now()<deadline) {
            const auto s=session.snapshot();samples=s.samples_received;
            if(!s.error.empty() && !fault)std::cerr<<"native helper error: "<<s.error<<'\n';
            if(fault)check((s.error.empty() || s.error=="pattern input contains a nonfinite sample") &&
                !s.receiver_health.input_overruns && !s.receiver_health.input_interrupted &&
                !s.receiver_health.search_limited && !s.receiver_health.dropped_samples &&
                s.status.find("FAIL: transmit")==std::string::npos,
                "ordinary replay error changed its row-only reset semantics");
            else check(s.error.empty() && !s.receiver_health.failed() && !s.receiver_health.dropped_samples,
                       "native conditioning changed receiver health");
            check(s.dsp_buffered_bytes<=value.dsp_workspace_bytes,"native conditioning exceeded its DSP workspace");
            if(samples) {
                if(!revision)revision=s.receiver_input_revision;
                check(revision==s.receiver_input_revision,"native conditioning reset its capture revision");
                check(s.processed_physical_samples>=frontier && s.processed_physical_samples<=samples,
                      "native conditioning regressed or overran its committed physical frontier");
                frontier=s.processed_physical_samples;
            }
            for(const auto& signal:s.signals)if(!signal.helper_carrier && (signal.received_bits||signal.complete)) {
                check(!beacon || fault,"helper-only capture manufactured Robust bits or END");
                const auto raw=signal.raw_bits.empty()?signal.text:signal.raw_bits;
                check(raw=="0" || raw=="00" || raw=="001","native conditioned reception changed an accepted prefix");
                exact=exact||(signal.complete && raw=="001" && signal.received_bits==3 && !signal.missing_symbols);
            }
            if(beacon && replayed && !replayed_at)replayed_at=samples;
            if(beacon&&!fault?replayed_at&&samples>=replayed_at+2ULL*rate:exact) {finished=true;break;}
            // Pace actual captured blocks against the live ingress queue, not
            // its committed frontier: the dormant original correctly holds
            // that frontier while the conditional transaction is reversible.
            if(script.delivered==script.released && s.buffered_samples<=rate/10 && samples<60ULL*rate)
                script.released+=rate/25;
            std::this_thread::sleep_for(1ms);
        }
        if(!finished && fault) {
            const auto s=session.snapshot();
            std::cerr<<"forced replay diagnostic samples="<<samples<<" delivered="<<script.delivered
                     <<" frontier="<<s.processed_physical_samples<<" buffered="<<s.buffered_samples
                     <<" error="<<s.error<<'\n';
        }
        check(finished,"bounded non-Simulation conditioned capture did not finish");
        const auto settle=std::chrono::steady_clock::now()+5s;
        while(script.delivered!=script.released && std::chrono::steady_clock::now()<settle)
            std::this_thread::sleep_for(1ms);
        check(script.delivered==script.released,"last released native input block did not arrive");
        // Capture remains open. Neither channel/source EOF nor fixture teardown
        // may flush an FIR tail or supply absence that was never captured.
        const auto before=script.delivered.load();std::this_thread::sleep_for(40ms);
        const auto final=session.snapshot();
        samples=final.samples_received;
        check(script.delivered==before && samples==before,"held native capture generated extra input");
        if(fault)check(script.injected && final.error=="pattern input contains a nonfinite sample" &&
            final.receiver_health.receiver_reset && !final.receiver_health.input_interrupted &&
            !final.receiver_health.input_overruns && !final.receiver_health.dropped_samples,
            "forced replay did not retain the ordinary visible row-reset error");
        else check(final.error.empty() && !final.receiver_health.failed(),"native final pending work reset reception");
        check(final.processed_physical_samples>=frontier,"held native capture regressed its physical frontier");
        frontier=final.processed_physical_samples;
        if(!beacon) {
            const auto plan=helper::plan(value.helper,value.transfer.modem.carrier_hz,200,48000);
            const helper::PayloadFilter filter(rate,plan.frequency_hz);
            check(final.processed_physical_samples<=samples-filter.delay(),"native receiver committed uncaptured FIR centers");
        }
        session.stop();
    }catch(...) {std::cerr<<"native helper trace retained at "<<path<<'\n';throw;}
    std::ifstream input(path);std::string line;unsigned admissions=0,resets=0;bool restored=false,observed_end=false,pending=false;
    std::uint64_t replay_end=0,id=0,last=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos) {
            ++admissions;check(number(line,"resident")==5,"native conditioning dropped an original epoch");
            check(number(line,"conditioned_lanes")*2<number(line,"original_lanes"),"native conditioning did not narrow actual search");
            check(number(line,"workspace")<=number(line,"capacity"),"native frozen original bank was not charged");
        }
        if(line.find("\"event\":\"receiver_helper_conditioning_fallback\"")!=std::string::npos) {
            replay_end=number(line,"replay_end");
            check(number(line,"replay_samples")>0 && replay_end-number(line,"sample")==number(line,"replay_samples"),
                  "native fallback omitted or duplicated a captured input interval");
        }
        if(line.find("\"event\":\"receiver_helper_conditioning_replayed\"")!=std::string::npos) {
            restored=true;check(number(line,"sample")==replay_end && number(line,"resident")==5,
                                "native fallback failed to replay all original epochs to the physical frontier");
        }
        if(line.find("\"event\":\"receiver_helper_replay_ordinary_reset\"")!=std::string::npos) {
            ++resets;
            check(number(line,"feed_begin_sample")<number(line,"replay_end") &&
                number(line,"sample")<=number(line,"replay_end"),"forced ordinary reset had no uncommitted raw replay");
            check(number(line,"remaining_bound")<=number(line,"numeric_ceiling"),"ordinary replay reset lost its finite promise");
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos &&
           (number(line,"received_bits") || line.find("\"complete\":1")!=std::string::npos)) {
            check(!beacon || fault,"native helper-only trace manufactured bits");
            if(number(line,"epoch")!=epoch || number(line,"first_stream_symbol")!=0)continue;
            if(!id)id=number(line,"signal_id");
            check(number(line,"signal_id")==id && number(line,"received_bits")>=last,"native pending reception identity or bit progress changed");
            last=number(line,"received_bits");
            check(last>=1 && last<=3,"native primary reception accepted an extra payload bit");
            const auto prefix=std::string("\"raw_bits_prefix\":\"")+std::string("001").substr(0,last)+'"';
            check(line.find(prefix)!=std::string::npos,"native primary trace changed an exact accepted bit prefix");
            if(line.find("\"complete\":1")!=std::string::npos)
                observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+6ULL*rate;
            else pending=true;
        }
    }
    check(admissions==1,"native capture did not exercise one primary conditioning transaction");
    check(fault?restored&&!committed&&resets==5&&exact&&pending&&observed_end:
        beacon?restored&&!committed:exact&&committed&&pending&&observed_end,
          "native conditioned capture lacked exact completion or loss-reference fallback evidence");
    check(script.captures==1,"native conditioning reopened the capture provider");
    std::cout<<"native helper beacon="<<beacon<<" fault="<<fault<<" samples="<<samples<<" committed_frontier="<<frontier<<'\n';
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
}
}

// Only the device is synthetic. These actual sampled waveform blocks pass
// through the production non-Simulation capture/observer/queue/decoder path;
// no Reference, receiver truth, source boundary or completion is injected.
namespace datapump::audio {
std::vector<Device> devices(){return {{"conditioned native capture","dated sampled PCM fixture"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options options) {
    auto& p=*provider;++p.captures;
    modem::ChannelConfig impairments;impairments.snr_db=40;impairments.frequency_offset_hz=2.17;
    impairments.clock_error_ppm=73;impairments.phase_noise_degrees_per_sqrt_second=0;impairments.seed=5181;
    modem::SampledSimulationChannel channel(transfer::seeded_config(p.value.transfer,epoch),impairments,p.value.helper);
    channel.set_helper_time_key(p.value.transfer.key);channel.begin_burst(p.beacon);
    auto source=transfer::binary_transmitter(Bytes{0,0,1},p.value.transfer);
    const auto hardware=channel.physical_sample_rate();
    check(rate==p.value.transfer.modem.sample_rate && hardware>=options.minimum_hardware_rate,
          "native fixture did not honor the requested physical audio rate");
    if(format)format({rate,hardware,rate*.42,modem::SampledSimulationChannel::workspace_bound});
    std::uint64_t physical_offset=0;
    const auto observe=[&](auto raw,std::uint32_t physical,std::uint64_t first) {
        p.now=static_cast<double>(epoch)+static_cast<double>(physical_offset+first+raw.size())/physical;
        if(options.capture_monitor)options.capture_monitor(raw,physical);
    };
    channel.set_physical_observer(observe);
    std::array<float,480> output{};bool ended=false,later_payload=false;
    while(!stop.stop_requested()) {
        if(p.delivered==p.released) {std::this_thread::sleep_for(1ms);continue;}
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),p.released-p.delivered));
        if(p.fault && !later_payload && p.delivered>=6ULL*rate) {
            // A later actual RF burst tests ordinary acquisition after reset.
            // Keep the same physical ADC sample lattice with a negligible
            // auxiliary carrier; bright-adjacent rejection is a separate case.
            physical_offset+=channel.physical_received_samples();
            auto auxiliary=p.value.helper;auxiliary.power_w=1e-12;
            channel=modem::SampledSimulationChannel(transfer::seeded_config(p.value.transfer,epoch),impairments,auxiliary);
            check(channel.physical_sample_rate()==hardware,"later RF fixture changed the ADC sample lattice");
            channel.set_physical_observer(observe);channel.begin_burst(false);
            source=transfer::binary_transmitter(Bytes{0,0,1},p.value.transfer);
            later_payload=true;ended=false;
        }
        std::size_t read=0;
        if(!ended && (!p.beacon || later_payload || p.delivered<3ULL*rate))read=channel.read(*source,std::span(output).first(count),stop);
        if(read<count) {ended=true;channel.read_noise(std::span(output).subspan(read,count-read),stop);}
        if(p.fault && admitted && !p.injected.exchange(true))
            output[count-1]=std::numeric_limits<float>::quiet_NaN();
        if(!callback(std::span<const float>(output).first(count)))return;
        p.delivered+=count;
    }
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format){capture(rate,device,callback,stop,std::move(format),{});}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode,Options) {
    throw Error("native receive-only fixture unexpectedly transmitted");
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& callback,std::stop_token stop,
              StreamFormatCallback format,ChannelMode channels){playback(rate,device,callback,stop,std::move(format),channels,{});}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("unexpected direct playback in native receive fixture");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected direct record in native receive fixture");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--success")run(false);
        else if(argc==2 && std::string_view(argv[1])=="--fallback")run(true);
        else if(argc==2 && std::string_view(argv[1])=="--replay-error")run(true,true);
        else {run(false);run(true);run(true,true);}
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
