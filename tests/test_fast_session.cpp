#include "datapump/fast/session.hpp"
#include "../src/fast/session_state.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/modem.hpp"
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
std::atomic<audio::ChannelMode> last_channels=audio::ChannelMode::stereo;
std::mutex mutex;
std::vector<float> input;
std::vector<float> output;
std::size_t offset=0;
bool hold=false,record=false;
void reset(std::vector<float> samples={},bool pause=false,bool record_output=false) {
    std::lock_guard lock(mutex);input=std::move(samples);offset=0;hold=pause;record=record_output;output.clear();
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
        // Capacity decoding is intentionally exercised against the production
        // one-second FIFO. Feed Fast's 50 ms blocks at audio pace; the old 3 ms
        // fixture clock imposed an unrelated 16.7x real-time requirement.
        std::this_thread::sleep_for(rate>=44100?50ms:3ms);
    }
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode channels) {
    fixture::last_channels=channels;
    ++fixture::active;struct Done {~Done(){--fixture::active;}} done;
    if(format)format({rate,rate,rate*.49,4096});
    std::vector<float> block(rate/20);
    while(!stop.stop_requested()) {
        auto n=source(block);if(!n)break;++fixture::played;
        {std::lock_guard lock(fixture::mutex);if(fixture::record)fixture::output.insert(fixture::output.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));}
        std::this_thread::sleep_for(3ms);
    }
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
    check(s.mono&&s.channel_mode==audio::ChannelMode::left_mono,"Default cable API settings must drive the left output channel");
    const auto before=fixture::played.load();session.transmit(path);
    await([&]{return fixture::played>before;},"fast playback did not start");
    check(fixture::last_channels.load()==audio::ChannelMode::left_mono,"Session lost default cable left output routing");
    bool blocked=false;try{session.configure(s);}catch(const Error&){blocked=true;}
    check(blocked,"active fast settings changed midstream");session.cancel();
    await([&]{return !session.active();},"fast cancellation did not release audio");
    auto result=session.poll();check(result.cancelled&&!result.complete&&!result.physical_complete&&!result.file,"cancelled fast TX claimed reception");
    fixture::reset({},true);session.listen();await([]{return fixture::active>0;},"fast capture did not start");session.close();
    await([&]{return session.ready_to_close();},"fast shutdown blocked");
    result=session.poll();check(!result.complete&&!result.physical_complete&&!result.file,"capture cancellation manufactured completion");
}
void text_audio_roundtrip() {
    const auto text=std::string("Fast text: café\nline two")+std::string(1,'\0')+" tail";
    for(bool encrypted:{false,true}) {
        fast::Settings s;s.device="fixture";s.profile.interleave_depth=1;
        s.channel_mode=encrypted?audio::ChannelMode::right_mono:audio::ChannelMode::stereo;
        s.mono=encrypted; // Explicit local routing overrides stay usable.
        if(encrypted)s.key=Crypto(Bytes(32,37));
        fixture::reset({},true,true);
        fast::Session tx;tx.configure(s);tx.transmit_text(text);
        await([&]{return !tx.active();},"text audio TX did not finish");
        const auto sent=tx.poll();
        check(fixture::last_channels.load()==audio::output_channels(s.mono,s.channel_mode),"Session ignored explicit local output routing");
        check(sent.error.empty()&&sent.source_bytes==text.size()&&sent.encrypted==encrypted,
              "text audio source or encryption selection changed");
        check(sent.estimated_seconds>6.25&&sent.transmit_fraction==1,"TX estimate and terminal progress missing");
        check(!sent.authenticated&&!sent.complete,"text TX claimed remote authentication or completion");
        std::vector<float> recorded;
        {std::lock_guard lock(fixture::mutex);recorded=std::move(fixture::output);}
        check(!recorded.empty(),"text transmission did not produce audio");
        fixture::reset(std::move(recorded),true);
        fast::Session rx;rx.configure(s);rx.listen();
        await([&]{return !rx.active();},"text audio RX did not finish");
        const auto received=rx.poll();
        if(!received.complete || !received.physical_complete || !received.file || !received.error.empty())
            std::cerr<<"text RX: "<<received.status<<"; error="<<received.error
                <<"; physical_end="<<received.physical_complete<<"; intervals="<<received.intervals<<'\n';
        check(received.complete&&received.physical_complete&&received.file&&received.error.empty(),
              "text audio reception did not observe a valid physical end");
        check(Bytes(received.file->bytes().begin(),received.file->bytes().end())==Bytes(text.begin(),text.end()),"text audio changed UTF-8, newline or zero bytes");
        check(received.encrypted==encrypted&&received.authenticated==encrypted,
              "public text was labelled authenticated or encryption state lost");
        check(encrypted?(received.authenticated_groups>0&&received.checksum_groups==0):
                        (received.authenticated_groups==0&&received.checksum_groups>0),
              "text authentication and public checksum counters were confused");
        // Deterministically exercise a stop between publication of this real
        // physical-end result and the worker's separate teardown update.
        auto stopping_after_end=received;stopping_after_end.active=stopping_after_end.listening=true;
        fast::detail::finish_worker(stopping_after_end,true);
        check(!stopping_after_end.active&&!stopping_after_end.cancelled&&stopping_after_end.complete&&
              stopping_after_end.physical_complete&&stopping_after_end.file==received.file&&
              stopping_after_end.authenticated==received.authenticated&&stopping_after_end.status==received.status,
              "A stop during teardown revoked an already published physical-end result");
        if(encrypted)s.key.reset();else s.key=Crypto(Bytes(32,38));
        rx.configure(s);
        check(rx.poll().encrypted==encrypted&&rx.poll().authenticated==encrypted,
              "changing the next transfer's mode relabelled completed content");
        bool rejected=false;
        try{rx.transmit_text(std::string(fast::text_byte_limit+1,'x'));}catch(const Error&){rejected=true;}
        check(rejected&&!rx.active()&&rx.poll().file==received.file,"oversized text disturbed a completed reception");
    }
}
void damaged_cycle_continues() {
    fast::Settings s;s.device="fixture";s.profile=fast::classic_profile(fast::Channel::wire);
    s.profile.constellation=4;s.profile.code_rate=fast::CodeRate::three_quarters;
    s.profile.interleave_depth=1;s.profile.symbol_rate=12000;s.profile.carrier_hz=10000;
    const auto cycle=fast::cycle_intervals(s.profile);
    Bytes source(440);std::uint32_t random=417;
    for(auto& byte:source){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<std::uint8_t>(random);}
    fast::StreamEncoder encoder(s.profile,std::nullopt,fast::xz_source(fast::byte_source(std::move(source))),fast::SourceEncoding::xz);
    std::size_t intervals=0;
    fast::Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
        if(!encoder.next_interval(bits))return false;
        // Damage the second source cycle, retaining all sampled modem markers
        // and pilots. The final source cycle must still reach its checksum.
        if(intervals/cycle==2)std::fill(bits.begin(),bits.end(),0);
        ++intervals;return true;
    });
    std::vector<float> wave;std::array<float,4096> block{};
    while(auto n=transmitter.read(block))wave.insert(wave.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
    check(intervals==4*cycle,"Damaged session fixture must contain bootstrap and three source cycles");
    // Supply enough real tail samples to finish scoring the last interval,
    // but nowhere near the six seconds required for physical completion.
    wave.resize(wave.size()+s.profile.sample_rate/10,0);
    fixture::reset(std::move(wave),true);
    fast::Session rx;rx.configure(s);rx.listen();
    await([&]{const auto r=rx.poll();return r.coding_cycles==4||!r.active;},"Later sampled cycle was not decoded after damage");
    const auto pending=rx.poll();
    check(pending.active&&!pending.decoding_stopped&&pending.failed_cycles==1&&pending.checksum_groups==2,
        "Session stopped processing valid cycles after a damaged cycle");
    check(!pending.physical_complete&&!pending.complete&&!pending.file&&pending.source_bytes==0&&pending.error.empty(),
        "Damaged pending session exposed source or manufactured physical completion");
    check(pending.status.find("continuing decoding")!=std::string::npos&&pending.verified_bytes>0,
        "Session did not report continued decoding with missing data");
    auto stopping_before_end=pending;
    fast::detail::finish_worker(stopping_before_end,true);
    check(stopping_before_end.cancelled&&!stopping_before_end.active&&!stopping_before_end.complete&&
        !stopping_before_end.physical_complete&&!stopping_before_end.file,
        "Stopping before physical end manufactured a completed result");
    {std::lock_guard lock(fixture::mutex);fixture::input.resize(fixture::input.size()+fast::end_silence_samples(s.profile),0);}
    await([&]{return !rx.active();},"Damaged reception did not end after real physical absence");
    const auto result=rx.poll();
    check(result.physical_complete&&!result.complete&&!result.file&&!result.error.empty()&&
        result.failed_cycles==1&&result.checksum_groups==2&&!result.decoding_stopped,
        "Damaged reception lost later verified cycles or became a complete file");
    bool refused=false;try{rx.save("/tmp/datapump-must-not-save-damaged-session.bin");}catch(const Error&){refused=true;}
    check(refused,"Session allowed saving an incomplete file");
}
}
int main(){try{idle_release();pending_preserved();fast_cancel();text_audio_roundtrip();damaged_cycle_continues();std::cout<<"fast session ownership, text transfer and damaged-cycle continuation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
