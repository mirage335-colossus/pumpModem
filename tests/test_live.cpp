#include "datapump/live.hpp"
#include <chrono>
#include <atomic>
#include <iostream>
#include <thread>
using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
live::Settings settings() {
    live::Settings value;value.simulation=true;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_phase_noise_degrees_per_sqrt_second=0;
    value.device="simulation must not open hardware";
    value.transfer.timestamp=1800000000;value.transfer.search_seconds=0;
    value.transfer.modem.sample_rate=8000;value.transfer.modem.bandwidth_hz=1000;
    value.transfer.modem.carrier_hz=1500;value.transfer.modem.spreading_factor=16;
    value.transfer.fec=FecMode::rs20;value.transfer.compression=true;
    value.content_limit=1024*1024;value.transfer.content_limit=value.content_limit;
    value.dsp_workspace_bytes=32*1024*1024;
    return value;
}
void run() {
    auto value=settings();live::Session session;
    session.start(value);
    Message sent;const std::string source="hello fixed intervals";
    sent.data.assign(source.begin(),source.end());sent.data.insert(sent.data.end(),2,0);
    session.transmit(sent);
    const auto deadline=std::chrono::steady_clock::now()+40s;
    bool received=false;
    while(std::chrono::steady_clock::now()<deadline) {
        auto snapshot=session.snapshot();
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"live aggregate workspace bounded");
        check(snapshot.signals.size()<=64,"signal queue bounded");
        for(const auto& item:snapshot.received) {
            check(item.stream_complete && item.content_validated,"live content requires physical completion");
            check(item.content.message.data==sent.data,"live compressed bytes and trailing zeros preserved");
            check(!item.content.authenticated,"public stream has no authentication claim");received=true;
        }
        if(received)break;
        if(!snapshot.error.empty())throw Error(snapshot.error);
        std::this_thread::sleep_for(5ms);
    }
    check(received,"fixed interval simulation completed");
    session.transmit_bits(Bytes{0,0,1});
    bool raw=false;
    const auto raw_deadline=std::chrono::steady_clock::now()+20s;
    while(std::chrono::steady_clock::now()<raw_deadline) {
        auto snapshot=session.snapshot();
        for(const auto& signal:snapshot.signals)if(signal.binary && signal.complete) {
            check(!signal.validated,"raw bits cannot become validated text");
            if(signal.text=="001")raw=true;
        }
        check(snapshot.received.empty(),"raw input has no source codec fallback");
        if(raw)break;
        std::this_thread::sleep_for(5ms);
    }
    check(raw,"raw bits retain leading zeros through the same physical end rule");
    Message large;large.data.resize(4096,0x5b);session.transmit(large);session.cancel_transmit();
    const auto cancel_end=std::chrono::steady_clock::now()+200ms;
    while(std::chrono::steady_clock::now()<cancel_end) {
        const auto snapshot=session.snapshot();check(snapshot.received.empty(),"cancelled processing does not release a message");
        std::this_thread::sleep_for(5ms);
    }
    session.stop();check(!session.snapshot().running,"session stops cleanly");
}
void short_keyed_stream_survives_epoch_refresh() {
    constexpr std::uint64_t origin=1800000000;
    std::atomic<std::uint64_t> epoch{origin};std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([&]{return static_cast<double>(epoch.load());},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=0;
    value.transfer.modem=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,true,1500).config;
    value.transfer.key.emplace(Bytes(32,0x45));value.receive_keys.emplace_back(Bytes(32,0x46));
    session.start(value);const std::string expected="01001000011001010110110001110000";
    Bytes bits;for(auto digit:expected)bits.push_back(static_cast<std::uint8_t>(digit-'0'));session.transmit_bits(bits);
    const auto deadline=std::chrono::steady_clock::now()+30s;bool jumped=false,computed=false;
    while(std::chrono::steady_clock::now()<deadline) {
        const auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        check(snapshot.received.empty(),"raw keyed input must not release source content");
        // The payload has ended, but the admitted stream is still waiting
        // for physical absence. Expire all ordinary unconfirmed epoch ages.
        if(!jumped && snapshot.transmitting && snapshot.transmission_fraction>.75 && snapshot.transmission_fraction<1) {
            epoch=origin+10;jumped=true;
        }
        if(snapshot.transmission_finished && snapshot.simulation_replay){computed=true;break;}
        std::this_thread::sleep_for(1ms);
    }
    check(jumped && computed,"short keyed epoch-refresh fixture did not cross the pending physical-end window");
    replay_milliseconds=3000;bool received=false;
    for(const auto& signal:session.snapshot().signals)
        received=received || (signal.binary && signal.complete && signal.text==expected && !signal.validated);
    check(received,"epoch refresh discarded a short admitted keyed stream before six-second physical completion");
    session.stop();
}
}
int main(int argc,char** argv){try{if(argc==1)run();short_keyed_stream_survives_epoch_refresh();std::cout<<"live fixed-interval lifecycle passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
