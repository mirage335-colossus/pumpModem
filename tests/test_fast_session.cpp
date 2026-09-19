#include "datapump/fast/session.hpp"
#include "datapump/live.hpp"
#include "datapump/audio.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace fixture {
std::atomic<unsigned> opened=0,active=0,played=0;
std::mutex mutex;
std::vector<float> input;
std::size_t offset=0;
bool hold=false;
void reset(std::vector<float> samples={},bool pause=false) {
    std::lock_guard lock(mutex);input=std::move(samples);offset=0;hold=pause;
}
}
// Device fixture replaces hardware symbols only in this executable. It never
// feeds transmitter decisions into either sampled receiver.
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::opened;++fixture::active;
    struct Done {~Done(){--fixture::active;}} done;
    if(format)format({rate,rate,rate*.49,4096});
    while(!stop.stop_requested()) {
        std::vector<float> block;
        {
            std::lock_guard lock(fixture::mutex);
            const auto n=std::min<std::size_t>(rate/20,fixture::input.size()-fixture::offset);
            if(n) {block.assign(fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::offset),fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::offset+n));fixture::offset+=n;}
            else if(!fixture::hold)block.assign(rate/20,0);
        }
        if(!block.empty()&&!consume(block))break;
        std::this_thread::sleep_for(3ms);
    }
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,bool) {
    ++fixture::active;struct Done {~Done(){--fixture::active;}} done;
    if(format)format({rate,rate,rate*.49,4096});
    std::vector<float> block(rate/20);
    while(!stop.stop_requested()) {auto n=source(block);if(!n)break;++fixture::played;std::this_thread::sleep_for(3ms);}
}
}
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
template<class P> void await(P predicate,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(!predicate()){if(std::chrono::steady_clock::now()>deadline)throw Error(message);std::this_thread::sleep_for(5ms);}
}
live::Settings regular() {
    live::Settings s;s.transfer.timestamp=1800000000;s.transfer.search_seconds=0;
    s.transfer.modem.sample_rate=8000;s.transfer.modem.bandwidth_hz=1000;s.transfer.modem.carrier_hz=1500;
    s.transfer.modem.spreading_factor=16;s.dsp_workspace_bytes=32*1024*1024;s.device="fixture";
    return s;
}
void idle_release() {
    fixture::reset();live::Session session;auto s=regular();session.start(s);
    await([]{return fixture::active.load()>0;},"regular capture did not start");
    await([&]{return session.try_suspend_capture();},"idle capture did not close");
    check(fixture::active==0,"suspension acknowledged before device closure");
    const auto opens=fixture::opened.load();
    session.configure(s);std::this_thread::sleep_for(30ms);
    check(fixture::opened==opens,"background regular configure stole fast audio reservation");
    bool rejected=false;try{session.transmit_bits(Bytes{0});}catch(const Error&){rejected=true;}
    check(rejected,"regular TX bypassed exclusive audio ownership");
    session.resume_capture();await([&]{return fixture::opened>opens;},"regular capture did not resume");
    check(session.snapshot().received.empty(),"idle suspension manufactured received content");session.stop();
}
void pending_preserved() {
    auto s=regular();auto tx=transfer::binary_transmitter(Bytes{0,0,1},s.transfer);
    std::vector<float> wave;
    std::array<float,1024> block{};
    while(auto n=tx->read(block))wave.insert(wave.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
    fixture::reset(std::move(wave),true);live::Session session;session.start(s);
    bool pending=false;
    await([&]{auto snapshot=session.snapshot();for(const auto& event:snapshot.signals)if(event.received_bits&&!event.complete)pending=true;return pending;},"regular sampled pending prefix was not admitted");
    const auto opens=fixture::opened.load();
    check(!session.try_suspend_capture(),"mode handoff interrupted an admitted regular reception");
    std::this_thread::sleep_for(20ms);
    check(fixture::opened==opens&&fixture::active>0,"regular pending receiver lost audio");
    check(session.snapshot().received.empty(),"suspension attempt fabricated physical completion");session.stop();
}
void fast_cancel() {
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-session-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    {std::ofstream file(path,std::ios::binary);std::string bytes(65536,'x');file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
    struct Remove {std::filesystem::path path;~Remove(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
    fast::Session session;fast::Settings s;s.device="fixture";s.key=Crypto::random();session.configure(s);
    const auto before=fixture::played.load();session.transmit(path);
    await([&]{return fixture::played>before;},"fast playback did not start");
    bool blocked=false;try{session.configure(s);}catch(const Error&){blocked=true;}
    check(blocked,"active fast settings changed midstream");session.cancel();
    await([&]{return !session.active();},"fast cancellation did not release audio");
    auto result=session.poll();check(result.cancelled&&!result.complete&&!result.physical_complete&&!result.file,"cancelled fast TX claimed reception");
    fixture::reset({},true);session.listen();await([]{return fixture::active>0;},"fast capture did not start");session.close();
    await([&]{return session.ready_to_close();},"fast shutdown blocked");
    result=session.poll();check(!result.complete&&!result.physical_complete&&!result.file,"capture cancellation manufactured completion");
}
}
int main(){try{idle_release();pending_preserved();fast_cancel();std::cout<<"fast session ownership passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
