#include "../src/live_receptions.hpp"
#include "datapump/types.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/resampler.hpp"
#include "datapump/live.hpp"
#include "datapump/pattern_search.hpp"
#include "../src/audio_trace.hpp"
#include "../src/live_simulation_clock.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstdlib>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Bytes payload_bits() {
    constexpr std::uint64_t word=0xD6E4B39A1275CC07ULL;
    Bytes result;for(unsigned i=64;i;--i)result.push_back(static_cast<std::uint8_t>((word>>(i-1))&1));
    return result;
}
std::string binary(const Bytes& bits) {std::string result;for(const auto bit:bits)result+=bit?'1':'0';return result;}
void history_preserves_complete_tail() {
    using History=live::detail::ReceptionHistory;
    const auto expected=payload_bits();Bytes visible;
    History history;std::uint64_t ids=0;
    auto helper=History::Candidate{};helper.id=1;helper.family=9;helper.first=1000;
    helper.symbol_samples=256;helper.end=helper.first+expected.size()*helper.symbol_samples;
    helper.frequency=500;helper.frequency_tolerance=1;helper.absence_samples=6000;
    helper.score=.8*static_cast<double>(expected.size()*helper.symbol_samples);helper.timing_tolerance=5;
    helper.helper_assisted=true;
    const auto first=history.observe(helper,[&]{return ++ids;});if(first.selected)visible=expected;
    std::cout<<"stage=helper_pending selected="<<first.selected<<" signal_id="<<first.signal_id
             <<" revision="<<first.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    auto ordinary=helper;ordinary.id=2;ordinary.end-=2*ordinary.symbol_samples;
    ordinary.score=.7*static_cast<double>((expected.size()-2)*ordinary.symbol_samples);ordinary.helper_assisted=false;ordinary.complete=true;
    const auto truncated=history.observe(ordinary,[&]{return ++ids;});
    if(truncated.selected)visible.assign(expected.begin(),expected.end()-2);
    std::cout<<"stage=ordinary_short_end selected="<<truncated.selected<<" signal_id="<<truncated.signal_id
             <<" revision="<<truncated.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    helper.complete=true;
    const auto completed=history.observe(helper,[&]{return ++ids;});if(completed.selected)visible=expected;
    std::cout<<"stage=helper_full_end selected="<<completed.selected<<" signal_id="<<completed.signal_id
             <<" revision="<<completed.revision<<" visible_count="<<visible.size()<<" visible="<<binary(visible)<<'\n';
    check(first.selected&&first.signal_id==truncated.signal_id&&first.signal_id==completed.signal_id&&ids==1,
          "compatible helper/ordinary updates changed the pending identity");
    check(visible==expected,"ordinary/helper presentation arbitration permanently discarded an accepted payload tail");
    auto stronger=helper;stronger.id=3;stronger.helper_assisted=false;stronger.score=.9*static_cast<double>(expected.size()*helper.symbol_samples);
    const auto revised=history.observe(stronger,[&]{return ++ids;});
    check(revised.selected&&revised.signal_id==first.signal_id&&revised.revision>completed.revision&&ids==1,
          "legitimately stronger ordinary reception could not revise the same completed row");
    std::cout<<"stage=ordinary_full_stronger selected="<<revised.selected<<" signal_id="<<revised.signal_id
             <<" revision="<<revised.revision<<" visible_count="<<visible.size()<<'\n';
}
struct PhysicalResult {Bytes bits;std::vector<std::size_t> progress;std::uint64_t first=0,physical_end=0,completed_at=0;std::size_t completed=0;};
PhysicalResult physical(bool helper_on) {
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=1000;
    c.spreading_factor=64;c.stream_epoch=1800000000;
    helper::Options helper;helper.power_w=helper_on?1500:0;helper.callsign="N0CALL";
    modem::ChannelConfig model;model.snr_db=30;model.clock_error_ppm=0;model.phase_noise_degrees_per_sqrt_second=0;model.seed=5181;
    modem::SampledSimulationChannel channel(c,model,helper);
    modem::StreamingTransmitter tx(modem::RawBits{payload_bits()},c);
    modem::PatternSearch search;search.worker_threads=1;search.chunk_bits=1;
    search.start_offset_seconds=0;search.start_uncertainty_seconds=6;
    modem::StreamingReceiver receiver(c,32*1024*1024,search);
    helper::PayloadFilter filter;
    if(helper_on) {
        const auto placement=helper::plan(helper,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,
            modem::outer_dsss_transmit_gain(c));
        check(placement.frequency_hz+225<c.sample_rate/2.,"physical payload-tail fixture did not exercise adjacent-helper FIR");
        filter=helper::PayloadFilter(c.sample_rate,placement.frequency_hz);
    }
    audio::Resampler playback(c.sample_rate,48000),capture(48000,c.sample_rate);
    std::map<std::pair<std::uint64_t,std::uint64_t>,PhysicalResult> streams;
    std::uint64_t supplied=0;
    const auto score=[&](std::span<const float> block) {
        std::vector<float> filtered(block.size());const auto count=filter.push(block,filtered);
        receiver.push(std::span(filtered).first(count));receiver.process_pending();supplied+=count;
        for(const auto& burst:receiver.take_pattern_bursts()) {
            const auto identity=std::pair{burst.stream_first_sample,burst.stream_first_symbol};auto& result=streams[identity];
            result.first=identity.first+filter.delay();
            check(!burst.missing_slots,"physical payload-tail fixture lost timed slots");
            check(burst.first_stream_symbol==result.bits.size(),"physical payload chunks skipped or repeated accepted bit coordinates");
            check(burst.bits.size()<=1,"physical payload-tail fixture delayed per-bit progress");
            for(const auto bit:burst.bits) {result.bits.push_back(bit);result.progress.push_back(result.bits.size());}
            result.physical_end=burst.end_sample+filter.delay();
            if(burst.complete){++result.completed;result.completed_at=supplied+filter.delay();}
        }
    };
    const auto captured=[&](std::span<const float> hardware,bool end=false) {
        std::array<float,1024> logical{};std::size_t consumed=0;
        do {const auto p=capture.process(hardware.subspan(consumed),logical,end);consumed+=p.consumed;
            if(p.produced)score(std::span(logical).first(p.produced));
            if(!p.consumed&&!p.produced)break;
        }while(consumed<hardware.size()||(end&&!capture.finished()));
        check(consumed==hardware.size(),"capture resampler failed to consume finite payload-tail input");
    };
    const auto route=[&](std::span<const float> logical,bool end=false) {
        std::array<float,4096> hardware{};std::size_t consumed=0;
        do {const auto p=playback.process(logical.subspan(consumed),hardware,end);consumed+=p.consumed;
            if(p.produced)captured(std::span(hardware).first(p.produced));
            if(!p.consumed&&!p.produced)break;
        }while(consumed<logical.size()||(end&&!playback.finished()));
        check(consumed==logical.size(),"playback resampler failed to consume finite payload-tail input");
    };
    std::array<float,173> logical{};
    while(const auto count=channel.read(tx,logical))route(std::span(logical).first(count));
    std::uint64_t trailing=10ULL*c.sample_rate;
    while(trailing){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,logical.size()));channel.read_noise(std::span(logical).first(count));route(std::span(logical).first(count));trailing-=count;}
    route({},true);captured({},true);
    check(streams.size()==1,"physical helper payload-tail fixture found multiple independent payload streams");
    const auto result=streams.begin()->second;
    std::cout<<"physical helper="<<helper_on<<" bits="<<result.bits.size()<<" complete="<<result.completed
             <<" first="<<result.first<<" physical_end="<<result.physical_end<<" completed_at="<<result.completed_at
             <<" prefix="<<binary(result.bits)<<'\n';
    check(result.bits==payload_bits(),"sampled helper/resampler/filter route truncated or changed the exact 64-bit payload");
    check(result.completed==1,"sampled helper/resampler/filter route omitted or duplicated physical END");
    check(result.progress.size()==payload_bits().size(),"sampled route omitted accepted bit progress");
    for(std::size_t i=0;i<result.progress.size();++i)check(result.progress[i]==i+1,"sampled route batched/reordered retained bit coordinates");
    check(result.completed_at>=result.physical_end+6ULL*c.sample_rate,
          "sampled helper route completed without six seconds of actual absent-symbol scoring");
    return result;
}
void physical_matched() {
    const auto off=physical(false),on=physical(true);
    check(off.bits==on.bits&&off.progress==on.progress,"helper changed complete physical payload bits or progress");
}

bool tail_trace_filter(const char* name) noexcept {
    const std::string_view event{name};return event=="trace_begin"||event=="receiver_signal_commit"||
        event=="receiver_selection"||event=="receiver_bank_admitted"||event.starts_with("receiver_helper_");
}
std::uint64_t trace_number(const std::string& line,const std::string& key) {
    const auto needle='"'+key+"\":";const auto at=line.find(needle);
    check(at!=std::string::npos,"payload-tail trace omitted required physical coordinates");
    return std::stoull(line.substr(at+needle.size()));
}
struct TailDates {
    std::atomic<std::uint64_t> transmit{0};
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& observation) {
        if(observation.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<TailDates*>(opaque)->transmit=static_cast<std::uint64_t>(observation.epoch);
    },1800000000.125};
    TailDates() {live::detail::SimulationHook* empty=nullptr;check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"payload-tail simulation hook already installed");}
    ~TailDates(){live::detail::simulation_hook.store(nullptr);}
};
struct LiveTailResult {Bytes wire;std::uint64_t transmit_epoch=0;};
LiveTailResult live_tail(bool helper_on,int kind=0) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    auto& c=value.transfer.modem;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    std::array<std::uint8_t,32> secret{};for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(i*11+7);
    value.transfer.key=Crypto(secret);value.transfer.timestamp=0;value.transfer.search_seconds=0;
    value.transfer.recovery_options.enabled=false;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_frequency_offset_hz=0;
    value.simulation_phase_noise_degrees_per_sqrt_second=0;value.simulation_seed=5181;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=64*1024*1024;value.content_limit=1024*1024;
    value.helper.power_w=helper_on?1500:0;value.helper.callsign.clear();
    Message message;
    if(kind==1)message.data=Bytes{'t','a','i','l',' ','c','o','m','p','l','e','t','e',' ','t','e','s','t'};
    if(kind==2) {message.kind=MessageKind::file;message.filename="tail.bin";for(unsigned i=0;i<17;++i)message.data.push_back(static_cast<std::uint8_t>(i*37+11));}
    if(kind) {value.transfer.fec=FecMode::rs60;value.transfer.compression=true;value.transfer.recovery_options.enabled=true;
        value.transfer.recovery_options.workers=1;value.transfer.recovery_options.budget=std::chrono::seconds(5);}
    auto expected=kind?transfer::message_wire_bits(message,value.transfer):payload_bits();
    if(kind)transfer::xor_binary_bits(expected,value.transfer);
    check(expected.size()<=4096,"tail fixture must keep its entire expected wire in the ordinary diagnostic prefix");
    auto expected_text=binary(expected);Bytes dated_wire;
    const auto base=std::getenv("DATAPUMP_HELPER_TAIL_EVIDENCE");
    const auto path=(base?std::filesystem::path(base):std::filesystem::temp_directory_path())/
        (std::string("helper-tail-rolling-")+std::to_string(kind)+"-"+(helper_on?"on":"off")+"-"+std::to_string(audio::trace::identity())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false;std::uint64_t admitted=0,samples=0,transmit_epoch=0;
    {
        audio::trace::Scope trace(path,false,&tail_trace_filter);TailDates dates;
        // Control only the initial station clock. The receiver still has no
        // fixed message timestamp and must admit later virtual-media epochs.
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);if(kind)session.transmit(message);else session.transmit_bits(expected);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(kind==2?90:60);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();samples=snapshot.samples_received;admitted=snapshot.receiver_admissions;
            if(!snapshot.error.empty())std::cerr<<"live helper="<<helper_on<<" error="<<snapshot.error<<'\n';
            check(snapshot.error.empty()&&!snapshot.receiver_health.failed()&&!snapshot.receiver_health.dropped_samples,
                  "rolling Auto helper payload-tail fixture caused an ordinary receiver error");
            if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                transmit_epoch=dates.transmit.load();check(transmit_epoch>0,"rolling payload-tail fixture missed actual transmit date");
                auto dated=value.transfer;dated.timestamp=transmit_epoch;
                if(kind) {dated_wire=transfer::message_wire_bits(message,dated);expected=dated_wire;
                    transfer::xor_binary_bits(expected,dated);expected_text=binary(expected);}
                else {dated_wire=expected;transfer::xor_binary_bits(dated_wire,dated);}
                const auto visible=session.snapshot();
                for(const auto& signal:visible.signals) {
                    if(signal.helper_carrier||!signal.received_bits)continue;
                    const auto raw=signal.binary?signal.text:signal.raw_bits;
                    std::cout<<"live_visible helper="<<helper_on<<" id="<<signal.id<<" revision="<<signal.revision
                             <<" count="<<signal.received_bits<<" complete="<<signal.complete<<" raw="<<raw<<'\n';
                    if(signal.complete&&!kind)check(signal.received_bits==expected.size()&&raw==expected_text,
                        "rolling Auto helper completed with missing or changed final payload bits");
                    if(!kind)exact=exact||(signal.complete&&signal.received_bits==expected.size()&&raw==expected_text);
                }
                if(kind)for(const auto& received:visible.received) {
                    std::cout<<"live_source kind="<<kind<<" helper="<<helper_on<<" observed="<<received.observed_bits
                             <<" diagnostic_bits="<<received.raw_bits.size()<<" source_bytes="<<received.content.message.data.size()
                             <<" complete="<<received.stream_complete<<" validated="<<received.content_validated
                             <<" authenticated="<<received.content.authenticated
                             <<" wire_equal="<<(received.raw_bits==expected)<<" data_equal="<<(received.content.message.data==message.data)
                             <<" kind_equal="<<(received.content.message.kind==message.kind)<<" filename_equal="<<(received.content.message.filename==message.filename)<<'\n';
                    exact=exact||(received.stream_complete&&received.content_validated&&received.content.authenticated&&
                        received.observed_bits==expected.size()&&received.raw_bits==expected&&
                        received.content.message.data==message.data&&received.content.message.kind==message.kind&&
                        received.content.message.filename==message.filename);
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
    }
    std::ifstream input(path);std::string line;std::uint64_t id=0,revision=0,last=0;bool complete=false,pending=false,observed_end=false,assisted_committed=false,phase_applied=false;
    while(std::getline(input,line)) {
        phase_applied=phase_applied||line.find("\"event\":\"receiver_helper_phase_applied\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto count=trace_number(line,"received_bits");if(!count)continue;
        assisted_committed=assisted_committed||line.find("\"helper_assisted\":1")!=std::string::npos;
        const auto current_id=trace_number(line,"signal_id");const auto current_revision=trace_number(line,"revision");
        if(!id){id=current_id;revision=current_revision;}
        check(current_id==id,"rolling helper payload-tail fixture replaced the pending row identity");
        check(count>=last,"rolling helper payload-tail fixture withdrew an accepted tail");
        last=count;revision=current_revision;
        const std::string marker="\"raw_bits_prefix\":\"";const auto at=line.find(marker);
        check(at!=std::string::npos,"rolling helper commit omitted available raw-bit prefix");
        const auto begin=at+marker.size(),end=line.find('"',begin);
        if(line.find("\"complete\":1")==std::string::npos)
            check(line.substr(begin,end-begin)==expected_text.substr(0,std::min<std::uint64_t>(count,128)),
                  "rolling helper commit changed the exact accepted payload prefix");
        if(line.find("\"complete\":1")!=std::string::npos) {
            complete=count==expected.size();observed_end=trace_number(line,"feed_end_sample")>=
                trace_number(line,"physical_end")+6ULL*c.sample_rate;
        } else pending=true;
    }
    std::cout<<"rolling kind="<<kind<<" helper="<<helper_on<<" finished="<<finished<<" exact="<<exact<<" trace_bits="<<last
             <<" transmit_epoch="<<transmit_epoch<<" id="<<id<<" revision="<<revision<<" pending="<<pending<<" complete="<<complete
             <<" observed_end="<<observed_end<<" helper_assisted_committed="<<assisted_committed<<" phase_applied="<<phase_applied<<" admissions="<<admitted<<" samples="<<samples<<" trace="<<path<<'\n';
    check(admitted>8,"controlled-clock tail fixture did not retain actual rolling receiver admissions");
    check(finished&&exact&&pending&&complete&&observed_end,
          "rolling Auto helper payload-tail fixture lost full payload, progress or physical completion");
    if(!base){std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");}
    return {dated_wire,transmit_epoch};
}
void live_matched(int kind=0) {
    const auto ordinary=live_tail(false,kind),helper=live_tail(true,kind);
    // Each run above compares its entire dated wire and recovered source. A
    // helper lead reserves a future private epoch rather than delaying a source
    // already keyed for the current epoch; its masked wire can therefore differ.
    check(ordinary.transmit_epoch && helper.transmit_epoch,"matched helper fixture omitted its actual transmit date");
    if(ordinary.transmit_epoch==helper.transmit_epoch)
        check(ordinary.wire==helper.wire,"matched same-epoch helper fixture changed the complete dated wire");
    std::cout<<"matched_source kind="<<kind<<" off_epoch="<<ordinary.transmit_epoch
             <<" on_epoch="<<helper.transmit_epoch<<" dated_wire_equal="<<(ordinary.wire==helper.wire)<<'\n';
}

}
int main(int argc,char** argv) {try {
    if(argc==2&&std::string(argv[1])=="--history-only"){history_preserves_complete_tail();return 0;}
    if(argc==2&&std::string(argv[1])=="--physical-only"){physical_matched();return 0;}
    if(argc==2&&std::string(argv[1])=="--live-only"){live_matched();return 0;}
    if(argc==2&&std::string(argv[1])=="--framed-text-only"){live_matched(1);return 0;}
    if(argc==2&&std::string(argv[1])=="--attachment-only"){live_matched(2);return 0;}
    if(argc==1){history_preserves_complete_tail();physical_matched();live_matched();live_matched(1);live_matched(2);return 0;}
    throw std::runtime_error("unknown helper payload-tail test mode");
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
