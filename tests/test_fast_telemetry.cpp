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
    const auto p=classic_profile(Channel::wire);
    const auto id=next_diagnostics_stream_id();
    const auto initial=initial_diagnostics(p,true,id);
    require(initial && initial->stream_id==id && !initial->revision && !initial->waveform_count
            && !initial->constellation_count && !initial->input_count && !initial->waveform_rms && !initial->waveform_peak
            && !initial->spectrum_valid,"initial stream metadata contains stale samples");
    Telemetry telemetry(p,true,id);const auto memory=telemetry.workspace_bytes();
    std::array<float,2048> tone{};
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*32*static_cast<double>(i)/512));
    telemetry.record_samples(tone);
    for(unsigned i=0;i<700;++i)telemetry.record_symbol({static_cast<float>(i),-static_cast<float>(i)});
    for(unsigned i=0;i<900;++i)telemetry.record_input({-static_cast<float>(i),static_cast<float>(i)});
    const auto now=Telemetry::Clock::time_point{};
    const auto frame=telemetry.publish(false,now);
    require(frame && frame->revision==1 && frame->samples==tone.size(),"sample/revision diagnostics counters");
    require(frame->sample_rate==p.sample_rate && frame->constellation==p.constellation && frame->transmitting && !frame->acquired,
            "TX diagnostics mislabeled as acquired RX");
    require(frame->waveform_count==1024 && frame->constellation_count==512 && frame->input_count==512 && frame->spectrum_valid,"fixed diagnostic bounds");
    for(std::size_t i=0;i<frame->waveform_count;++i)require(frame->waveform[i]==tone[1024+i],"waveform is not latest actual PCM in time order");
    require(frame->constellation_points.front()==std::complex<float>(188,-188)
            && frame->constellation_points.back()==std::complex<float>(699,-699),"constellation ring order");
    require(frame->input_points.front()==std::complex<float>(-388,388)
            && frame->input_points.back()==std::complex<float>(-899,899),"input I/Q ring order or payload/input separation");
    require(std::abs(frame->waveform_rms-.5/std::sqrt(2.))<1e-6 && std::abs(frame->waveform_peak-.5)<1e-6,
            "PCM RMS/peak do not measure the retained audio");
    const auto maximum=std::max_element(frame->spectrum_db.begin(),frame->spectrum_db.end());
    require(maximum-frame->spectrum_db.begin()==32,"FFT frequency axis/bin location");
    require(std::abs(*maximum+6.0205999f)<.02f,"Hann spectrum amplitude dBFS calibration");
    require(!telemetry.publish(false,now+99ms),"diagnostic publication exceeded 10 Hz");
    for(unsigned i=0;i<100;++i) {telemetry.record_samples(tone);telemetry.record_symbol({7,8});telemetry.record_input({9,10});}
    const auto later=telemetry.publish(true,now+100ms);
    require(later && later->revision==2 && later->acquired,"diagnostic cadence/acquisition update");
    require(frame->samples==2048 && frame->constellation_points.back()==std::complex<float>(699,-699)
            && frame->input_points.back()==std::complex<float>(-899,899),"published frame mutated after publication");
    require(telemetry.workspace_bytes()==memory && later->waveform_count==1024 && later->constellation_count==512 && later->input_count==512,
            "diagnostic memory grew with stream duration");
    std::array<float,2> bad{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    telemetry.record_samples(bad);telemetry.record_symbol({bad[0],0});telemetry.record_input({0,bad[1]});
    const auto safe=telemetry.publish(false,now+200ms);
    require(safe && std::isfinite(safe->waveform_rms) && std::isfinite(safe->waveform_peak)
            && safe->input_points.back()==std::complex<float>(9,10)
            && std::all_of(safe->waveform.begin(),safe->waveform.end(),[](float f){return std::isfinite(f);}),
            "nonfinite display sample escaped sanitization");
    std::array<float,1024> silence{};telemetry.record_samples(silence);
    const auto quiet=telemetry.publish(false,now+300ms);
    require(quiet && !quiet->waveform_rms && !quiet->waveform_peak,"audio meters retained older loud samples");
}
void acoustic_batches() {
    const auto p=capacity_profile(Channel::acoustic);
    const auto id=next_diagnostics_stream_id();
    require(initial_diagnostics(p,false,id)->acoustic_ofdm,"OFDM diagnostic identity missing");
    Telemetry telemetry(p,false,id);
    const auto memory=telemetry.workspace_bytes();
    const auto now=Telemetry::Clock::time_point{};
    std::array<float,1024> pcm{};telemetry.record_samples(pcm);
    constexpr unsigned tones=11200;
    // A monotonic frequency ramp makes retaining only the final high bins
    // visibly wrong. Both observers must represent the whole publication batch.
    for(unsigned i=0;i<tones;++i) {
        telemetry.record_symbol({static_cast<float>(i),-static_cast<float>(i)});
        telemetry.record_input({-static_cast<float>(i),static_cast<float>(i)});
    }
    const auto frame=telemetry.publish(true,now);
    require(frame&&frame->acoustic_ofdm&&frame->constellation_count==512&&frame->input_count==512,
            "OFDM batch sampling changed diagnostic bounds or identity");
    const auto coverage=[](const auto& points,bool input) {
        std::array<unsigned,8> bands{};
        float low=tones,high=0;
        for(const auto point:points) {
            const auto bin=input?-point.real():point.real();
            require(bin>=0&&bin<tones&&bin==std::floor(bin)&&point.imag()==-point.real(),
                    "OFDM sampled point was synthesized or outside its batch");
            low=std::min(low,bin);high=std::max(high,bin);
            ++bands[static_cast<unsigned>(bin)*bands.size()/tones];
        }
        require(low<tones*.05&&high>tones*.95,"OFDM sampled plot omits the low or high frequency edge");
        for(const auto count:bands)require(count>20&&count<110,"OFDM sampled plot overrepresents one frequency region");
    };
    coverage(frame->constellation_points,false);coverage(frame->input_points,true);
    require(frame->constellation_sample==pcm.size()&&frame->input_sample==pcm.size(),
            "OFDM point freshness does not follow actual observations");
    require(!telemetry.publish(true,now+99ms),"OFDM batch sampling bypassed publication cadence");
    for(unsigned i=0;i<150;++i)telemetry.record_samples(pcm);
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    telemetry.record_symbol({nan,0});telemetry.record_input({0,nan});
    const auto quiet=telemetry.publish(true,now+100ms);
    require(quiet&&quiet->constellation_points==frame->constellation_points&&quiet->input_points==frame->input_points&&
            quiet->constellation_count==512&&quiet->input_count==512&&quiet->samples>frame->samples+2*p.sample_rate&&
            quiet->constellation_sample==frame->constellation_sample&&quiet->input_sample==frame->input_sample,
            "Empty OFDM batch discarded points or marked retained symbols fresh");
    telemetry.record_samples(pcm);
    for(unsigned i=0;i<17;++i)telemetry.record_symbol({-1000.F-i,1000.F+i});
    const auto next=telemetry.publish(true,now+200ms);
    require(next&&next->constellation_count==17&&next->input_count==512&&
            next->input_points==frame->input_points&&next->constellation_sample==next->samples,
            "New OFDM symbol batch mixed old points or discarded independent input history");
    for(unsigned i=0;i<17;++i)require(next->constellation_points[i]==std::complex<float>(-1000.F-i,1000.F+i),
            "Short OFDM batch did not preserve every actual observation");
    telemetry.record_input({.25F,.5F});
    const auto last=telemetry.publish(true,now+300ms);
    require(last&&last->input_count==1&&last->input_points.front()==std::complex<float>(.25F,.5F)&&
            last->constellation_count==17&&last->constellation_points==next->constellation_points,
            "New OFDM input batch did not independently replace its prior points");
    coverage(frame->constellation_points,false);coverage(frame->input_points,true);
    require(telemetry.workspace_bytes()==memory,"OFDM batch sampling grew diagnostic storage");
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
Reception receive(const Profile& p,std::span<const float> pcm,SymbolObserver observer={},SymbolObserver input_observer={}) {
    Reception result;
    Receiver receiver(p,[&](std::span<const float> interval){result.soft.insert(result.soft.end(),interval.begin(),interval.end());},std::move(observer),std::move(input_observer));
    for(std::size_t i=0;i<pcm.size();i+=197)receiver.push(pcm.subspan(i,std::min(std::size_t{197},pcm.size()-i)));
    std::array<float,2048> silence{};
    for(std::size_t left=p.sample_rate*7;left;) {
        const auto count=std::min(left,silence.size());receiver.push(std::span<const float>(silence).first(count));left-=count;
    }
    receiver.finish();result.progress=receiver.progress();return result;
}
void input_observations() {
    // A carrier tone cannot satisfy the independent random marker. Its actual
    // complex matched-filter response is still useful input instrumentation.
    for(const auto channel:{Channel::fm,Channel::acoustic}) {
        const auto p=classic_profile(channel);
        constexpr double amplitude=.25,phase=.7;
        const auto omega=2*std::numbers::pi*p.carrier_hz/p.sample_rate;
        std::vector<float> pcm(12000);
        for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(amplitude*std::cos(omega*static_cast<double>(i)+phase));
        const auto observe=[&](std::size_t chunk) {
            std::vector<std::complex<float>> values;std::size_t payload=0;
            Receiver receiver(p,[&](auto){++payload;},[&](auto){++payload;},[&](auto value){values.push_back(value);});
            const auto memory=receiver.workspace_bytes();
            for(std::size_t at=0;at<pcm.size();at+=chunk)receiver.push(std::span<const float>(pcm).subspan(at,std::min(chunk,pcm.size()-at)));
            require(!receiver.progress().acquired && !receiver.progress().physical_complete && !payload,
                    "input observations admitted tone payload or manufactured physical completion");
            require(receiver.workspace_bytes()==memory,"input tap grows receiver workspace");
            return values;
        };
        const auto values=observe(1);
        require(values==observe(197),"input I/Q depends on capture callback chunks");
        require(values.size()==static_cast<std::size_t>(std::ceil(2*static_cast<double>(pcm.size())*p.symbol_rate/p.sample_rate)),
                "input I/Q cadence is not two observations per nominal symbol");
        // Independent steady-state FIR frequency response, including the real-PCM
        // mixer image, establishes amplitude, quadrature and filter delay.
        const auto sps=p.sample_rate/p.symbol_rate;
        const auto half=static_cast<std::size_t>(std::ceil(8*sps));
        double dc=0;std::complex<double> image=0;
        for(std::size_t k=0;k<=2*half;++k) {
            const auto h=root_raised_cosine((static_cast<double>(k)-static_cast<double>(half))/sps,p.rolloff)/sps;
            dc+=h;image+=h*std::polar(1.,2*omega*static_cast<double>(k));
        }
        for(std::size_t j=40;j<values.size();++j) {
            // Fractional display intervals select integer PCM observations. Allow
            // either adjacent sample at an exact floating-point cadence boundary.
            const auto nominal=static_cast<double>(j)*sps*.5;
            double error=1;
            for(const auto sample:{std::ceil(nominal-1e-8),std::ceil(nominal+1e-8)}) {
                const auto expected=amplitude*(std::polar(dc,phase)+std::polar(1.,-2*omega*sample-phase)*image);
                error=std::min(error,std::abs(static_cast<std::complex<double>>(values[j])-expected));
            }
            require(error<1e-6,"input I/Q is not the actual uncorrected matched-filter response");
        }
        require(values.back().imag()>.1f && std::abs(values.back())<.3f,"input I/Q fabricated unit-energy constellation points");
    }
}
void observers() {
    auto p=classic_profile(Channel::wire);p.constellation=16;p.amplitude=.5;const auto bits=input();
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
    std::vector<std::complex<float>> input_points;
    const auto with_input=receive(p,noisy,{},[&](auto value){input_points.push_back(value);});
    std::size_t throwing_input=0;
    const auto failed_input=receive(p,noisy,{},[&](auto){++throwing_input;throw std::runtime_error("input display failed");});
    require(!input_points.empty() && throwing_input==input_points.size(),"input observers missing or emitted inconsistently");
    for(const auto* result:{&with_input,&failed_input}) {
        require(baseline.soft==result->soft && baseline.progress.acquired==result->progress.acquired
                && baseline.progress.physical_complete==result->progress.physical_complete
                && baseline.progress.symbols==result->progress.symbols && baseline.progress.intervals==result->progress.intervals
                && baseline.progress.erased_intervals==result->progress.erased_intervals && baseline.progress.evm==result->progress.evm
                && baseline.progress.carrier_error_hz==result->progress.carrier_error_hz
                && baseline.progress.clock_error_ppm==result->progress.clock_error_ppm,"input observer changed receiver evidence or progress");
    }
}
void sessions() {
    Settings settings;settings.device="telemetry fixture";settings.profile=classic_profile(Channel::wire);settings.profile.interleave_depth=1;
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
            && !fresh->waveform_count && !fresh->constellation_count && !fresh->input_count,"RX inherited previous TX diagnostic samples");
    fixture::capture_ready=true;
    await([&]{return session.poll().diagnostics->waveform_count>0;},"RX diagnostic PCM did not publish");
    const auto listening=session.poll().diagnostics;
    require(listening->spectrum_valid && !listening->transmitting && !listening->acquired && !listening->constellation_count,
            "unacquired RX invented constellation points");
    require(listening->input_count>0 && listening->input_count<=512 && listening->waveform_rms>0 && listening->waveform_peak>0,
            "unacquired RX did not publish actual input I/Q and PCM levels");
    session.cancel();await([&]{return !session.active();},"RX cancellation stalled with telemetry");
    require(session.poll().diagnostics->stream_id==fresh->stream_id && !session.poll().physical_complete,
            "RX cancellation lost retained diagnostics or manufactured end");
    fixture::capture_ready=false;
    Session other;other.configure(settings);other.listen();
    require(other.poll().diagnostics->stream_id!=fresh->stream_id,"two sessions reused diagnostic stream identity");
    other.close();await([&]{return other.ready_to_close();},"second diagnostic session did not close");
}
}
int main(){try{collector();acoustic_batches();input_observations();observers();sessions();std::cout<<"fast immutable telemetry, waveform/spectrum and observer isolation passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
