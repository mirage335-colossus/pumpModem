#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump::fast;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
using Bits=std::array<std::uint8_t,physical_interval_bits>;
std::vector<Bits> data(std::size_t count) {
    std::mt19937 generator(5719);std::vector<Bits> values(count);
    for(auto& span:values)for(auto& bit:span)bit=static_cast<std::uint8_t>(generator()&1);
    return values;
}
std::vector<float> transmit(Profile p,const std::vector<Bits>& input,std::size_t chunk) {
    std::size_t index=0;
    Transmitter tx(p,[&](std::span<std::uint8_t> out) {
        require(out.size()==physical_interval_bits,"TX asked for variable interval size");
        if(index==input.size())return false;
        std::copy(input[index].begin(),input[index].end(),out.begin());++index;return true;
    });
    const auto workspace=tx.workspace_bytes();
    std::vector<float> result,block(chunk);
    while(!tx.finished()) {
        const auto count=tx.read(block);
        result.insert(result.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        require(tx.workspace_bytes()==workspace,"TX workspace grows with file");
    }
    return result;
}
struct Reception {std::vector<Bits> output;ModemProgress progress;std::size_t erasures=0;};
Reception receive(Profile p,std::span<const float> pcm,std::size_t chunk,bool silence) {
    Reception result;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(soft.size()==physical_interval_bits,"RX delivered a variable interval");
        Bits bits{};
        for(std::size_t i=0;i<bits.size();++i) {bits[i]=soft[i]>0;result.erasures+=soft[i]==0;}
        result.output.push_back(bits);
    });
    const auto workspace=rx.workspace_bytes();
    for(std::size_t i=0;i<pcm.size();i+=chunk)rx.push(pcm.subspan(i,std::min(chunk,pcm.size()-i)));
    require(!rx.progress().physical_complete,"EOF substituted for observed absence");
    if(silence) {
        std::vector<float> zero(p.sample_rate*7);
        for(std::size_t i=0;i<zero.size();i+=chunk)rx.push(std::span<const float>(zero).subspan(i,std::min(chunk,zero.size()-i)));
        require(rx.progress().physical_complete,"six seconds of whole-symbol absence failed to complete");
    }
    rx.finish();
    require(rx.workspace_bytes()==workspace,"RX workspace grows with stream");
    result.progress=rx.progress();return result;
}
}
int main() {try {
    // Independent numerical filter and wire mapping vectors, not a shared
    // encoder/decoder round-trip assertion.
    require(std::abs(root_raised_cosine(0,.2)-1.0546479089470325)<1e-12,"RRC origin vector");
    require(std::abs(root_raised_cosine(1,.2)+.0525461921179600)<1e-12,"RRC one-symbol vector");
    const auto qpsk=constellation(4);
    require(std::abs(qpsk[0]-std::complex<double>(.7071067811865476,.7071067811865476))<1e-12,"QPSK label zero vector");
    require(std::abs(qpsk[2]-std::complex<double>(.7071067811865476,-.7071067811865476))<1e-12,"QPSK Gray label vector");
    require(sync_symbol(0)==std::complex<double>(.7071067811865475244,.7071067811865475244),"fixed marker vector");
    const auto input=data(5);
    {
        const auto cable=classic_profile(Channel::wire);
        require(cable.amplitude==.35,"cable pulse-shaping headroom default changed");
        require(classic_profile(Channel::ssb).amplitude==.5 && classic_profile(Channel::fm).amplitude==.5 &&
            classic_profile(Channel::acoustic).amplitude==.35,"other channel output levels changed");
        auto changed_level=cable;changed_level.amplitude=.5;
        require(profile_id(cable)==profile_id(changed_level),"local output level entered the peer integrity context");
        double maximum_radius=1; // Training, markers and pilots use unit QPSK.
        for(const auto point:constellation(cable.constellation))maximum_radius=std::max(maximum_radius,std::abs(point));
        double pulse_envelope=0;
        // The current 16-symbol pulse uses a linearly interpolated 2048-point
        // per-symbol table. Sum |h(t-k)| over every table phase; between table
        // nodes the absolute sum is convex, so its maximum is at a node. This
        // triangle bound covers arbitrary payload symbols and carrier phase,
        // including training/payload transitions, before device resampling.
        for(unsigned phase=0;phase<=2048;++phase) {
            double envelope=0;
            for(int symbol=-8;symbol<=8;++symbol) {
                const double time=static_cast<double>(phase)/2048-symbol;
                if(std::abs(time)<=8)envelope+=std::abs(root_raised_cosine(time,cable.rolloff));
            }
            pulse_envelope=std::max(pulse_envelope,envelope);
        }
        const auto peak_bound=cable.amplitude*maximum_radius*pulse_envelope;
        require(peak_bound<=.95,"cable pulse-shaped PCM can clip before device playback");
        for(const auto sample:transmit(cable,input,173))
            require(std::abs(sample)<=peak_bound+1e-6,"cable PCM exceeds its pulse-envelope bound");
    }
    for(const auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        auto p=classic_profile(channel);
        if(channel==Channel::wire)p.amplitude=.5; // Preserve original PCM fixtures.
        for(const auto order:{4u,16u,64u,256u}) {
            p.constellation=order;
            auto pcm=transmit(p,input,173);
            pcm.insert(pcm.begin(),137,0);
            auto result=receive(p,pcm,191,true);
            if(result.output!=input) {
                std::size_t wrong=0;for(std::size_t j=0;j<std::min(input.size(),result.output.size());++j)
                    for(std::size_t k=0;k<input[j].size();++k)wrong+=input[j][k]!=result.output[j][k];
                std::cerr<<"channel "<<static_cast<int>(channel)<<" order "<<order<<" got "<<result.output.size()<<" wrong "<<wrong<<" erasures "<<result.erasures<<" evm "<<result.progress.evm<<'\n';
            }
            require(result.output==input,"clean sampled APSK failed");
        }
    }
    auto p=classic_profile(Channel::wire);p.sample_rate=44100;p.constellation=16;p.amplitude=.5;
    auto pcm=transmit(p,input,1);
    require(pcm==transmit(p,input,4096),"TX PCM depends on callback chunks");
    // Fractional start and carrier phase arise independently by shifting TX's
    // configured carrier phase through a non-integer sample-delay channel.
    std::vector<float> delayed(pcm.size()+151);
    for(std::size_t i=16;i+16<pcm.size();++i) {
        double value=0;
        for(int k=-15;k<=15;++k) {
            const auto t=static_cast<double>(k)-.39;
            const auto kernel=std::sin(std::acos(-1.)*t)/(std::acos(-1.)*t)*(.5+.5*std::cos(std::acos(-1.)*t/16));
            value+=pcm[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i)-k)]*kernel;
        }
        delayed[i+149]=static_cast<float>(value);
    }
    delayed.resize(delayed.size()+4410,0);
    auto fine=receive(p,delayed,1,false),large=receive(p,delayed,503,false);
    require(fine.output==large.output,"RX depends on callback chunks");
    if(fine.output!=input) {
        std::size_t wrong=0;for(std::size_t j=0;j<std::min(input.size(),fine.output.size());++j)
            for(std::size_t k=0;k<input[j].size();++k)wrong+=input[j][k]!=fine.output[j][k];
        std::cerr<<"fractional got "<<fine.output.size()<<" wrong "<<wrong<<" erased "<<fine.erasures<<" evm "<<fine.progress.evm<<'\n';
    }
    require(fine.output==input,"44.1 kHz fractional timing/phase failed");
    require(!fine.progress.physical_complete,"finish fabricated physical completion");
    for(const auto order:{4u,64u,256u}) {
        auto dense_profile=p;dense_profile.constellation=order;
        auto dense_pcm=transmit(dense_profile,input,461);
        dense_pcm.insert(dense_pcm.begin(),81,0);
        const auto dense_result=receive(dense_profile,dense_pcm,379,true);
        require(dense_result.output==input,"44.1 kHz dense constellation failed");
    }
    // Actual noisy PCM is demodulated without any transmitter decisions or
    // clock/phase side channel. The known pilot sequence is independent evidence.
    p=classic_profile(Channel::wire);p.constellation=16;p.amplitude=.5;
    auto noisy=transmit(p,input,257);
    std::mt19937 noise_rng(7123);std::normal_distribution<float> added_noise(0,.008f);
    for(auto& value:noisy) {
        value+=added_noise(noise_rng);
        const auto integer=static_cast<std::int16_t>(std::lround(std::clamp(value,-1.f,1.f)*32767.f));
        value=static_cast<float>(integer)/32768.f;
    }
    noisy.insert(noisy.begin(),83,0);
    const auto noisy_result=receive(p,noisy,127,true);
    require(noisy_result.output==input,"noisy sampled 16-APSK failed");
    auto hum=transmit(p,input,257);
    for(std::size_t i=0;i<hum.size();++i) {
        const auto time=static_cast<double>(i)/p.sample_rate;
        hum[i]+=static_cast<float>(.08*std::sin(2*std::acos(-1.)*60*time)+.025*std::sin(2*std::acos(-1.)*120*time)
            +.008*std::cos(2*std::acos(-1.)*6100*time));
    }
    require(receive(p,hum,173,true).output==input,"hum/harmonics and narrowband interference defeated training");
    auto phase_step=transmit(p,input,257);
    const auto step=static_cast<std::size_t>(static_cast<double>(training_symbols+2*interval_symbols(p)+sync_symbols+40)*p.sample_rate/p.symbol_rate);
    for(std::size_t i=step;i<phase_step.size();++i)phase_step[i]=-phase_step[i];
    const auto slipped=receive(p,phase_step,179,true);
    require(slipped.output.size()==input.size() && slipped.output.back()==input.back(),"scheduled training did not recover a pi phase slip");
    // An erased interval retains its physical position until training returns;
    // neither a failed alignment word nor a failed FEC interval shortens time.
    const auto burst_input=data(12);
    auto burst=transmit(p,burst_input,251);
    const auto samples_per_symbol=p.sample_rate/p.symbol_rate;
    const auto mute_begin=static_cast<std::size_t>(static_cast<double>(training_symbols+4*interval_symbols(p))*samples_per_symbol);
    const auto mute_end=static_cast<std::size_t>(static_cast<double>(training_symbols+5*interval_symbols(p))*samples_per_symbol);
    std::fill(burst.begin()+static_cast<std::ptrdiff_t>(mute_begin),burst.begin()+static_cast<std::ptrdiff_t>(mute_end),0);
    const auto burst_result=receive(p,burst,223,true);
    require(burst_result.output.size()==burst_input.size(),"interior absence changed interval positions");
    require(burst_result.erasures>=physical_interval_bits,"interruption omitted neutral soft erasures");
    require(burst_result.output.back()==burst_input.back(),"training did not restore lock after interruption");
    const auto clock_input=data(24);
    const auto nominal=transmit(p,clock_input,509);
    std::vector<float> drifted(static_cast<std::size_t>(static_cast<double>(nominal.size())*1.0001)+64);
    for(std::size_t i=20;i+20<drifted.size();++i) {
        const auto coordinate=static_cast<double>(i)/1.0001;
        const auto center=static_cast<std::ptrdiff_t>(coordinate);
        double value=0;
        for(int k=-15;k<=15;++k) {
            const auto source=center+k;
            if(source<0 || static_cast<std::size_t>(source)>=nominal.size())continue;
            const auto t=coordinate-static_cast<double>(source);
            const auto kernel=std::abs(t)<1e-9?1:std::sin(std::acos(-1.)*t)/(std::acos(-1.)*t)*(.5+.5*std::cos(std::acos(-1.)*t/16));
            value+=nominal[static_cast<std::size_t>(source)]*kernel;
        }
        drifted[i]=static_cast<float>(value);
    }
    const auto clock_result=receive(p,drifted,419,true);
    require(clock_result.output==clock_input,"100 ppm sampled clock/carrier drift failed");
    // Pure noise and unrelated tones cannot acquire by nearest-point EVM.
    std::mt19937 rng(317);std::normal_distribution<float> noise(0,.2f);
    std::vector<float> unrelated(p.sample_rate*7);
    for(std::size_t i=0;i<unrelated.size();++i)unrelated[i]=noise(rng)+static_cast<float>(.3*std::cos(static_cast<double>(i)*.179));
    const auto rejected=receive(p,unrelated,311,false);
    require(!rejected.progress.acquired && rejected.output.empty(),"noise/tones acquired an APSK reception");
    std::size_t interval_count=0;
    Receiver presence(p,[&](std::span<const float>){++interval_count;});
    presence.push(transmit(p,input,503));
    presence.push(unrelated);
    require(presence.progress().physical_complete,"noise/tones kept resetting physical absence through EVM");
    require(interval_count==input.size(),"trailing interference became phantom received intervals");
    auto acoustic=classic_profile(Channel::acoustic);
    std::vector<float> acoustic_noise(acoustic.sample_rate*7);
    for(std::size_t i=0;i<acoustic_noise.size();++i)
        acoustic_noise[i]=noise(rng)+static_cast<float>(.3*std::cos(2*std::acos(-1.)*acoustic.carrier_hz*i/acoustic.sample_rate));
    const auto acoustic_rejected=receive(acoustic,acoustic_noise,311,false);
    require(!acoustic_rejected.progress.acquired&&acoustic_rejected.output.empty(),"acoustic noise/tone acquired through relaxed marker threshold");
    Receiver acoustic_presence(acoustic,[](std::span<const float>){});
    acoustic_presence.push(transmit(acoustic,input,503));acoustic_presence.push(acoustic_noise);
    require(acoustic_presence.progress().physical_complete,"acoustic interference kept a reception alive");
    std::cout<<"fast sampled modem tests passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
