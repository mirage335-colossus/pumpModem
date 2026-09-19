#include "datapump/fast/telemetry.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <thread>

using namespace datapump;
using namespace datapump::fast;
using namespace std::chrono_literals;
namespace fixture {
std::atomic<bool> playback_ready=false,capture_ready=false;
}
// Real samples, not modem side channels. The fixture controls only when its
// independent audio source/sink starts producing/consuming PCM.
namespace datapump::audio {
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,
              std::stop_token stop,StreamFormatCallback format,bool) {
    if(format)format({rate,rate,rate*.49,2048});
    std::array<float,2048> samples{};
    while(!stop.stop_requested()) {
        if(fixture::playback_ready && !source(samples))break;
        std::this_thread::sleep_for(5ms);
    }
}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,
             std::stop_token stop,StreamFormatCallback format) {
    if(format)format({rate,rate,rate*.49,2048});
    std::array<float,2048> samples{};std::uint64_t position=0;
    while(!stop.stop_requested()) {
        if(fixture::capture_ready) {
            for(auto& value:samples)value=static_cast<float>(.25*std::sin(2*std::numbers::pi*3000*static_cast<double>(position++)/rate));
            if(!consume(samples))break;
        }
        std::this_thread::sleep_for(5ms);
    }
}
}
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class P> void await(P predicate,const char* message) {
    const auto deadline=Telemetry::Clock::now()+5s;
    while(!predicate()) {
        if(Telemetry::Clock::now()>deadline)throw std::runtime_error(message);
        std::this_thread::sleep_for(2ms);
    }
}
void collector() {
    const auto p=profile(Channel::wire);
    const auto id=next_diagnostics_stream_id();
    const auto initial=initial_diagnostics(p,true,id);
    require(initial && initial->stream_id==id && !initial->revision && !initial->waveform_count
            && !initial->constellation_count && !initial->spectrum_valid,"initial stream metadata contains stale samples");
    Telemetry telemetry(p,true,id);const auto memory=telemetry.workspace_bytes();
    std::array<float,2048> tone{};
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*32*static_cast<double>(i)/512));
    telemetry.record_samples(tone);
    for(unsigned i=0;i<700;++i)telemetry.record_symbol({static_cast<float>(i),-static_cast<float>(i)});
    const auto now=Telemetry::Clock::time_point{};
    const auto frame=telemetry.publish(false,now);
    require(frame && frame->revision==1 && frame->samples==tone.size(),"sample/revision diagnostics counters");
    require(frame->sample_rate==p.sample_rate && frame->constellation==p.constellation && frame->transmitting && !frame->acquired,
            "TX diagnostics mislabeled as acquired RX");
    require(frame->waveform_count==1024 && frame->constellation_count==512 && frame->spectrum_valid,"fixed diagnostic bounds");
    for(std::size_t i=0;i<frame->waveform_count;++i)require(frame->waveform[i]==tone[1024+i],"waveform is not latest actual PCM in time order");
    require(frame->constellation_points.front()==std::complex<float>(188,-188)
            && frame->constellation_points.back()==std::complex<float>(699,-699),"constellation ring order");
    const auto maximum=std::max_element(frame->spectrum_db.begin(),frame->spectrum_db.end());
    require(maximum-frame->spectrum_db.begin()==32,"FFT frequency axis/bin location");
    require(std::abs(*maximum+6.0205999f)<.02f,"Hann spectrum amplitude dBFS calibration");
    require(!telemetry.publish(false,now+99ms),"diagnostic publication exceeded 10 Hz");
    for(unsigned i=0;i<100;++i) {telemetry.record_samples(tone);telemetry.record_symbol({7,8});}
    const auto later=telemetry.publish(true,now+100ms);
    require(later && later->revision==2 && later->acquired,"diagnostic cadence/acquisition update");
    require(frame->samples==2048 && frame->constellation_points.back()==std::complex<float>(699,-699),"published frame mutated after publication");
    require(telemetry.workspace_bytes()==memory && later->waveform_count==1024 && later->constellation_count==512,
            "diagnostic memory grew with stream duration");
    std::array<float,2> bad{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    telemetry.record_samples(bad);telemetry.record_symbol({bad[0],0});
    const auto safe=telemetry.publish(false,now+200ms);
    require(safe && std::all_of(safe->waveform.begin(),safe->waveform.end(),[](float f){return std::isfinite(f);}),
            "nonfinite display sample escaped sanitization");
}
using Interval=std::array<std::uint8_t,physical_interval_bits>;
std::vector<Interval> input() {
    std::vector<Interval> result(3);
    for(std::size_t row=0;row<result.size();++row)for(std::size_t i=0;i<result[row].size();++i)
        result[row][i]=static_cast<std::uint8_t>((i*7+i/17+row)&1);
    return result;
}
std::vector<float> transmit(const Profile& p,const std::vector<Interval>& data,SymbolObserver observer={}) {
    std::size_t index=0;
    Transmitter transmitter(p,[&](std::span<std::uint8_t> output) {
        if(index==data.size())return false;
        std::copy(data[index].begin(),data[index].end(),output.begin());++index;return true;
    },std::move(observer));
    std::vector<float> output;std::array<float,311> block{};
    while(!transmitter.finished()) {
        const auto count=transmitter.read(block);output.insert(output.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    }
    return output;
}
struct Reception {std::vector<float> soft;ModemProgress progress;};
Reception receive(const Profile& p,std::span<const float> pcm,SymbolObserver observer={}) {
    Reception result;
    Receiver receiver(p,[&](std::span<const float> interval){result.soft.insert(result.soft.end(),interval.begin(),interval.end());},std::move(observer));
    for(std::size_t i=0;i<pcm.size();i+=197)receiver.push(pcm.subspan(i,std::min(std::size_t{197},pcm.size()-i)));
    std::array<float,2048> silence{};
    for(std::size_t left=p.sample_rate*7;left;) {
        const auto count=std::min(left,silence.size());receiver.push(std::span<const float>(silence).first(count));left-=count;
    }
    receiver.finish();result.progress=receiver.progress();return result;
}
void observers() {
    const auto p=profile(Channel::wire);const auto bits=input();
    const auto plain=transmit(p,bits);
    std::vector<std::complex<float>> mapped;
    const auto tapped=transmit(p,bits,[&](auto symbol){mapped.push_back(symbol);});
    std::size_t throwing_tx=0;
    const auto throwing=transmit(p,bits,[&](auto){++throwing_tx;throw std::runtime_error("display failed");});
    require(plain==tapped && plain==throwing,"display observer changed transmitted PCM");
    require(mapped.size()==bits.size()*physical_interval_bits/4 && throwing_tx==mapped.size(),"TX diagnostic tap included training or pilots");
    const auto points=constellation(16);
    std::size_t at=0;
    for(const auto& interval:bits)for(std::size_t bit=0;bit<interval.size();bit+=4) {
        unsigned label=0;for(unsigned b=0;b<4;++b)label=(label<<1)|interval[bit+b];
        require(mapped[at++]==static_cast<std::complex<float>>(points[label]),"TX plot did not observe actual mapper symbols");
    }
    auto noisy=plain;std::mt19937 rng(473);std::normal_distribution<float> noise(0,.003f);
    for(auto& sample:noisy)sample=.71f*sample+noise(rng);
    noisy.insert(noisy.begin(),73,0);
    const auto baseline=receive(p,noisy);
    std::vector<std::complex<float>> observed;
    const auto with_points=receive(p,noisy,[&](auto symbol){observed.push_back(symbol);});
    std::size_t throwing_rx=0;
    const auto with_failure=receive(p,noisy,[&](auto){++throwing_rx;throw std::runtime_error("display failed");});
    require(!observed.empty() && throwing_rx==observed.size(),"RX observations missing or emitted inconsistently");
    require(baseline.soft==with_points.soft && baseline.soft==with_failure.soft,"display observer changed received soft/hard evidence");
    require(baseline.progress.physical_complete && with_points.progress.physical_complete && with_failure.progress.physical_complete,
            "display observer changed physical completion");
    bool unsliced=false;
    for(const auto value:observed) {
        double distance=1e9;
        for(const auto expected:points)distance=std::min(distance,std::norm(static_cast<std::complex<double>>(value)-expected));
        if(distance>1e-5)unsliced=true;
    }
    require(unsliced,"RX constellation was synthesized from hard decisions");
}
void sessions() {
    Settings settings;settings.device="telemetry fixture";settings.profile.interleave_depth=1;
    Session session;session.configure(settings);
    fixture::playback_ready=false;session.transmit_text("Actual fast diagnostics");
    const auto pending=session.poll().diagnostics;
    require(pending && pending->stream_id && pending->transmitting && !pending->revision && !pending->waveform_count,
            "new TX did not synchronously reset diagnostics identity");
    fixture::playback_ready=true;
    await([&]{const auto state=session.poll();return state.diagnostics && state.diagnostics->constellation_count>0;},"TX diagnostics did not publish mapped data");
    const auto transmitting=session.poll().diagnostics;
    require(transmitting->waveform_count && transmitting->spectrum_valid && transmitting->transmitting,"TX diagnostic waveform/spectrum missing");
    session.cancel();await([&]{return !session.active();},"TX cancellation stalled with telemetry");
    const auto stopped=session.poll();
    require(stopped.cancelled && !stopped.complete && !stopped.physical_complete && stopped.diagnostics
            && stopped.diagnostics->stream_id==pending->stream_id,"cancel cleared display or manufactured completion");
    require(transmitting->constellation_count>0 && transmitting->transmitting,"retained TX frame mutated on cancellation");
    fixture::capture_ready=false;session.listen();
    const auto fresh=session.poll().diagnostics;
    require(fresh && fresh->stream_id!=pending->stream_id && !fresh->transmitting && !fresh->revision
            && !fresh->waveform_count && !fresh->constellation_count,"RX inherited previous TX diagnostic samples");
    fixture::capture_ready=true;
    await([&]{return session.poll().diagnostics->waveform_count>0;},"RX diagnostic PCM did not publish");
    const auto listening=session.poll().diagnostics;
    require(listening->spectrum_valid && !listening->transmitting && !listening->acquired && !listening->constellation_count,
            "unacquired RX invented constellation points");
    session.cancel();await([&]{return !session.active();},"RX cancellation stalled with telemetry");
    require(session.poll().diagnostics->stream_id==fresh->stream_id && !session.poll().physical_complete,
            "RX cancellation lost retained diagnostics or manufactured end");
    fixture::capture_ready=false;
    Session other;other.configure(settings);other.listen();
    require(other.poll().diagnostics->stream_id!=fresh->stream_id,"two sessions reused diagnostic stream identity");
    other.close();await([&]{return other.ready_to_close();},"second diagnostic session did not close");
}
}
int main(){try{collector();observers();sessions();std::cout<<"fast immutable telemetry, waveform/spectrum and observer isolation passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
