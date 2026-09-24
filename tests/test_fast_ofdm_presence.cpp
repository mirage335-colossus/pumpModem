#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace datapump::fast;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

struct Fixture {
    Profile profile;
    std::vector<std::uint8_t> bits;
    std::vector<float> pcm;
    double noise_sigma;
    explicit Fixture(double reference_snr):profile(resolve_snr_preset(Channel::acoustic,reference_snr).profile) {
        // Reproduce the originally reported Auto 0/3 dB geometry. Shallower
        // future Auto defaults must not shorten this historical regression;
        // manually selected depth eight remains a supported physical profile.
        profile.interleave_depth=8;
        require(profile.acoustic_ofdm&&profile.constellation==4&&profile.code_rate==CodeRate::two_thirds&&
            profile.interleave_depth==8&&profile.ofdm_fft_size==32768&&profile.ofdm_prefix_samples==4096,
            "OFDM presence regression no longer exercises the reported Auto preset");
        bits.resize(cycle_intervals(profile)*physical_interval_bits);
        std::mt19937 random(947311);
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1);
        std::size_t sent=0;
        Transmitter tx(profile,[&](std::span<std::uint8_t> out) {
            if(sent==bits.size())return false;
            std::copy_n(bits.begin()+static_cast<std::ptrdiff_t>(sent),out.size(),out.begin());
            sent+=out.size();return true;
        });
        std::array<float,4096> chunk{};
        while(!tx.finished()) {
            const auto count=tx.read(chunk);
            pcm.insert(pcm.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
        }
        require(sent==bits.size()&&pcm.size()==transmission_samples(profile,cycle_intervals(profile)),
            "OFDM presence source is not a complete fixed raw cycle");
        // The menu's SNR reference is the ORIGINAL 17.5 kHz band. White real
        // PCM noise has half its two-sided density in each positive-frequency
        // bin. Keep this same noise floor through the entire trailing absence.
        // Echo changes increase received signal power; they do not reduce noise.
        const auto power=std::pow(profile.amplitude/4.5,2);
        noise_sigma=std::sqrt(power*profile.sample_rate/(2*17500*std::pow(10.,reference_snr/10)));
    }
    std::size_t block_samples()const {return profile.ofdm_fft_size+profile.ofdm_prefix_samples;}
    double sample(double coordinate)const {
        if(coordinate<0||coordinate>=pcm.size()-1)return 0;
        const auto index=static_cast<std::size_t>(coordinate);
        const auto fraction=coordinate-index;
        return pcm[index]*(1-fraction)+pcm[index+1]*fraction;
    }
};

struct Change {double echo=0,delay_samples=0,clock_ppm=0;};

void continuing_signal(const Fixture& f,Change change,const char* name) {
    const auto& p=f.profile;
    std::vector<float> received;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(received.size()+soft.size()<=f.bits.size(),"continuing OFDM signal invented extra interval positions");
        received.insert(received.end(),soft.begin(),soft.end());
    });
    constexpr std::uint64_t event_seconds=20;
    const auto event=event_seconds*p.sample_rate;
    const auto ratio=1+change.clock_ppm*1e-6;
    // A positive delay/clock step extends the last physical symbol. Retain it
    // completely rather than accidentally testing a truncated final block.
    const auto signal_end=event+static_cast<std::uint64_t>(std::ceil((f.pcm.size()-event+change.delay_samples)*ratio));
    std::mt19937 random(133831);
    std::normal_distribution<float> noise(0,static_cast<float>(f.noise_sigma));
    std::array<float,4096> chunk{};
    std::uint64_t at=0;
    while(at<signal_end) {
        const auto count=std::min<std::uint64_t>(chunk.size(),signal_end-at);
        for(std::size_t i=0;i<count;++i) {
            const auto position=at+i;
            const auto x=position<event?double(position):event+(position-event)/ratio-change.delay_samples;
            auto value=f.sample(x);
            if(position>=event)value+=change.echo*f.sample(x-240);
            chunk[i]=static_cast<float>(value)+noise(random);
        }
        rx.push(std::span<const float>(chunk).first(count));at+=count;
        if(rx.progress().physical_complete)
            std::cerr<<name<<" premature_end_seconds="<<double(at)/p.sample_rate
                <<" signal_seconds="<<double(signal_end)/p.sample_rate<<" delivered_bits="<<received.size()<<'\n';
        require(!rx.progress().physical_complete,"OFDM channel change ended a continuing transmitter");
    }
    require(rx.progress().acquired,"changed OFDM waveform never acquired its valid training");
    const auto feed_noise=[&](std::uint64_t samples) {
        while(samples) {
            const auto count=std::min<std::uint64_t>(samples,chunk.size());
            for(std::size_t i=0;i<count;++i)chunk[i]=noise(random);
            rx.push(std::span<const float>(chunk).first(count));samples-=count;
        }
    };
    const auto partial=static_cast<std::uint64_t>(p.sample_rate*5.5);
    feed_noise(partial);
    require(!rx.progress().physical_complete,"OFDM recovery counted fewer than six seconds of true absence");
    require(received.size()==f.bits.size(),"OFDM channel change deleted trailing fixed interval positions");
    std::size_t errors=0,erasures=0,checked=0;
    for(std::size_t begin=0;begin<received.size();begin+=physical_interval_bits) {
        std::size_t observed=0,wrong=0;
        for(std::size_t i=begin;i<begin+physical_interval_bits;++i) {
            if(received[i]==0){++erasures;continue;}
            ++observed;wrong+=(received[i]>0)!=bool(f.bits[i]);
        }
        errors+=wrong;
        // Random source intervals differ in about half their bits. An admitted
        // interval with substantial evidence must match its ORIGINAL ordinal,
        // even after missing blocks; we do not require uncoded AWGN perfection.
        if(observed>=512) {
            require(wrong*100<observed*35,"OFDM recovery shifted an observed interval to another source ordinal");
            ++checked;
        }
    }
    require(checked>cycle_intervals(p)/2,"OFDM recovery delivered mostly missing rather than observed payload");
    feed_noise(end_silence_samples(p)-partial);
    rx.finish();
    require(rx.progress().physical_complete,"continuing OFDM recovery failed to end after actual noisy absence");
    require(received.size()==f.bits.size(),"OFDM noisy tail appended phantom intervals");
    std::cout<<name<<" intervals="<<received.size()/physical_interval_bits<<" errors="<<errors
        <<" erasures="<<erasures<<" clock_ppm="<<rx.progress().clock_error_ppm<<'\n';
}

void muted_block_positions(const Fixture& f) {
    const auto& p=f.profile;
    const auto length=f.block_samples();
    const auto erased_block=preamble_symbols(p)+3;
    const auto first=static_cast<std::size_t>(std::ceil(p.ofdm_low_hz*p.ofdm_fft_size/p.sample_rate));
    const auto last=static_cast<std::size_t>(std::floor(p.ofdm_high_hz*p.ofdm_fft_size/p.sample_rate));
    const auto active=last-first+1;
    const auto stride=std::max<std::size_t>(2,std::min<std::size_t>(p.ofdm_pilot_stride,active/258));
    const auto data=active-(active+stride-1)/stride;
    // QPSK: exactly two coded bit positions per payload tone.
    const auto erased_begin=3*data*2,erased_end=4*data*2;
    std::vector<float> received;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(received.size()+soft.size()<=f.bits.size(),"muted OFDM block invented interval positions");
        received.insert(received.end(),soft.begin(),soft.end());
    });
    std::array<float,4096> chunk{};
    for(std::size_t at=0;at<f.pcm.size();) {
        const auto count=std::min(chunk.size(),f.pcm.size()-at);
        for(std::size_t i=0;i<count;++i)
            chunk[i]=(at+i>=erased_block*length&&at+i<(erased_block+1)*length)?0:f.pcm[at+i];
        rx.push(std::span<const float>(chunk).first(count));at+=count;
        require(!rx.progress().physical_complete,"one missing OFDM block ended the continuing source");
    }
    // Drain the final observation without reaching six seconds of absence.
    rx.push(std::vector<float>(p.sample_rate));
    require(received.size()==f.bits.size(),"muted OFDM block deleted fixed source coordinates");
    for(std::size_t i=0;i<received.size();++i) {
        if(i>=erased_begin&&i<erased_end)
            require(received[i]==0,"missing OFDM block did not retain neutral erasures");
        else require(received[i]!=0&&(received[i]>0)==bool(f.bits[i]),
            "OFDM positions before/after missing block are not exact");
    }
    require(!rx.progress().physical_complete,"one-second OFDM tail fabricated completion");
    rx.finish();
    require(!rx.progress().physical_complete,"acquired OFDM EOF substituted for observed absence");
    std::cout<<"muted_block_positions erased_bits="<<erased_end-erased_begin<<" EOF_incomplete=1\n";
}

void fft(std::vector<std::complex<double>>& values,bool inverse) {
    const auto size=values.size();
    for(std::size_t i=1,j=0;i<size;++i) {
        auto bit=size>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;if(i<j)std::swap(values[i],values[j]);
    }
    for(std::size_t length=2;length<=size;length*=2) {
        const auto step=std::polar(1.,(inverse?2:-2)*std::numbers::pi/length);
        for(std::size_t start=0;start<size;start+=length) {
            std::complex<double> phase=1.;
            for(std::size_t i=0;i<length/2;++i) {
                const auto a=values[start+i],b=phase*values[start+i+length/2];
                values[start+i]=a+b;values[start+i+length/2]=a-b;phase*=step;
            }
        }
    }
    if(inverse)for(auto& value:values)value/=static_cast<double>(size);
}

void rejected_acquisition(const Fixture& f) {
    const auto& p=f.profile;
    const auto size=p.ofdm_fft_size;
    const auto length=f.block_samples();
    auto pcm=f.pcm;
    // Training has independent final verification. Preserve its power, real
    // waveform and cyclic prefix but flip one QPSK sign on a quarter of the
    // active tones in that final block. Earlier fitting blocks remain pristine;
    // refitting the channel to this corrupted verification would hide the fault.
    const auto start=(preamble_symbols(p)-1)*length;
    std::vector<std::complex<double>> bins(size);
    for(std::size_t i=0;i<size;++i)bins[i]=pcm[start+p.ofdm_prefix_samples+i];
    fft(bins,false);
    const auto first=static_cast<std::size_t>(std::ceil(p.ofdm_low_hz*size/p.sample_rate));
    const auto last=static_cast<std::size_t>(std::floor(p.ofdm_high_hz*size/p.sample_rate));
    for(auto k=first;k<=last;k+=4) {
        bins[k]={-bins[k].real(),bins[k].imag()};bins[size-k]=std::conj(bins[k]);
    }
    fft(bins,true);
    for(std::size_t i=0;i<length;++i)
        pcm[start+i]=static_cast<float>(bins[(i+size-p.ofdm_prefix_samples)%size].real());
    // Only startup plus a few payload blocks are needed to rule out accepting
    // the corrupted independent marker. No later complete training is present.
    pcm.resize((preamble_symbols(p)+3)*length);
    std::size_t intervals=0;
    Receiver rx(p,[&](std::span<const float>){++intervals;});
    rx.push(pcm);rx.push(std::vector<float>(p.sample_rate*8));rx.finish();
    require(!rx.progress().acquired&&!rx.progress().physical_complete&&!intervals,
        "corrupted independent OFDM acquisition marker was fitted away or admitted");
    std::cout<<"corrupted_acquisition_marker rejected=1\n";
}

void current_preset_keyed_file(double reference_snr,unsigned expected_depth) {
    const auto p=resolve_snr_preset(Channel::acoustic,reference_snr).profile;
    require(p.acoustic_ofdm&&p.constellation==4&&p.code_rate==CodeRate::two_thirds&&
        p.interleave_depth==expected_depth,
        "coded OFDM regression no longer exercises the current Auto 0/3 dB preset");
    datapump::Bytes original(20000);
    for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<std::uint8_t>(i*19);
    original.back()=0;
    const datapump::Crypto key(datapump::Bytes(32,0x37));
    auto encoder=testing::deterministic_encoder(p,key,byte_source(original),947311);
    // Exercise the real streaming codec reader: the physical transmitter asks
    // for complete locally fixed cycles, including bootstrap and final padding.
    Transmitter tx(p,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
    const auto estimate=estimate_transmission(p,true,original.size());
    std::array<float,4096> chunk{};
    std::vector<float> pcm;
    while(!tx.finished()) {
        const auto count=tx.read(chunk);
        pcm.insert(pcm.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
    }
    require(encoder.intervals_emitted()==estimate.intervals&&
        encoder.intervals_emitted()%cycle_intervals(p)==0&&
        pcm.size()+end_silence_samples(p)==estimate.samples,
        "coded OFDM transmitter did not stream complete estimated coding cycles");
    StreamDecoder decoder(p,key);
    std::size_t intervals=0;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(++intervals<=estimate.intervals,"coded OFDM receiver appended phantom intervals");
        decoder.push_interval(soft);
    });
    const auto sigma=std::sqrt(std::pow(p.amplitude/4.5,2)*p.sample_rate/
        (2*17500*std::pow(10.,reference_snr/10)));
    std::mt19937 random(133831);
    std::normal_distribution<float> noise(0,static_cast<float>(sigma));
    const auto event=static_cast<std::uint64_t>(20)*p.sample_rate;
    constexpr std::size_t echo_delay=240; // Five milliseconds at 48 kHz.
    require(p.sample_rate==48000&&pcm.size()>event,"coded OFDM echo fixture is too short");
    // Retain the causal echo of the final source samples. The noise floor is
    // unchanged during transmission, the echo tail, and actual signal absence.
    const auto signal_end=pcm.size()+echo_delay;
    for(std::size_t at=0;at<signal_end;) {
        const auto count=std::min(chunk.size(),signal_end-at);
        for(std::size_t i=0;i<count;++i) {
            const auto position=at+i;
            double value=position<pcm.size()?pcm[position]:0;
            if(position>=event&&position>=echo_delay&&position-echo_delay<pcm.size())
                value+=.5*pcm[position-echo_delay];
            chunk[i]=static_cast<float>(value)+noise(random);
        }
        rx.push(std::span<const float>(chunk).first(count));at+=count;
        require(!rx.progress().physical_complete,"coded OFDM transfer ended while transmitter/echo continued");
        require(!decoder.snapshot().complete&&!decoder.result(),"keyed source escaped before physical completion");
    }
    require(rx.progress().acquired,"coded OFDM preset never acquired its valid training");
    decoder.finish(false);
    require(!decoder.snapshot().complete&&!decoder.result(),"coded OFDM input EOF completed the source");
    const auto feed_noise=[&](std::uint64_t samples) {
        while(samples) {
            const auto count=std::min<std::uint64_t>(samples,chunk.size());
            for(std::size_t i=0;i<count;++i)chunk[i]=noise(random);
            rx.push(std::span<const float>(chunk).first(count));samples-=count;
        }
    };
    const auto partial=static_cast<std::uint64_t>(p.sample_rate*5.5);
    feed_noise(partial);
    require(!rx.progress().physical_complete,"coded OFDM transfer ended before six seconds of observed absence");
    decoder.finish(rx.progress().physical_complete);
    require(!decoder.snapshot().complete&&!decoder.result(),"partial OFDM absence exposed the keyed source");
    feed_noise(end_silence_samples(p)-partial);
    rx.finish();decoder.finish(rx.progress().physical_complete);
    const auto state=decoder.snapshot();const auto result=decoder.result();
    require(rx.progress().physical_complete&&state.physical_end&&state.complete&&state.authenticated&&result,
        "current acoustic preset failed the complete keyed file under noise and changing echo");
    require(result->size()==original.size()&&std::equal(original.begin(),original.end(),result->bytes().begin()),
        "current acoustic preset recovered different keyed source bytes");
    require(intervals==estimate.intervals&&state.intervals==estimate.intervals&&
        !state.failed_cycles&&!state.ldpc_failed_frames&&
        state.ldpc_frames==estimate.intervals/cycle_intervals(p)*p.interleave_depth,
        "coded OFDM file lost fixed positions or failed an LDPC frame");
    require(state.ldpc_changed_bits>0,"coded OFDM noise fixture did not exercise LDPC correction");
    std::cout<<"current_auto"<<reference_snr<<"_keyed_file bytes="<<result->size()
        <<" depth="<<p.interleave_depth<<" intervals="<<intervals<<" ldpc_frames="<<state.ldpc_frames
        <<" changed_bits="<<state.ldpc_changed_bits<<" failed_frames="<<state.ldpc_failed_frames<<'\n';
}

void noise_only(const Fixture& f) {
    std::mt19937 random(311481);std::normal_distribution<float> noise(0,static_cast<float>(f.noise_sigma));
    std::size_t intervals=0;Receiver rx(f.profile,[&](std::span<const float>){++intervals;});
    std::array<float,4096> chunk{};
    auto left=static_cast<std::uint64_t>(f.profile.sample_rate)*20;
    while(left) {
        const auto count=std::min<std::uint64_t>(left,chunk.size());
        for(std::size_t i=0;i<count;++i)chunk[i]=noise(random);
        rx.push(std::span<const float>(chunk).first(count));left-=count;
    }
    rx.finish();
    require(!rx.progress().acquired&&!rx.progress().physical_complete&&!intervals,
        "noise-only OFDM input invented acquisition or physical completion");
    std::cout<<"noise_only acquired=0 complete=0\n";
}
}

int main(int argc,char** argv) {try {
    const Fixture zero(0),three(3);
    unsigned failed=0,executed=0;
    const auto run=[&](const char* name,const std::function<void()>& test) {
        if(argc>1&&std::string_view(argv[1])!=name)return;
        ++executed;
        try {test();}
        catch(const std::exception& error) {++failed;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("auto0_echo",[&]{continuing_signal(zero,{.5,0,0},"auto0_echo");});
    run("auto3_delay",[&]{continuing_signal(three,{0,16,0},"auto3_delay");});
    run("auto0_clock",[&]{continuing_signal(zero,{0,0,100},"auto0_clock");});
    run("muted_block_positions",[&]{muted_block_positions(three);});
    run("corrupted_acquisition",[&]{rejected_acquisition(three);});
    run("noise_only",[&]{noise_only(zero);});
    run("current_auto3_keyed_file",[&]{current_preset_keyed_file(3,2);});
    run("current_auto0_keyed_file",[&]{current_preset_keyed_file(0,1);});
    require(executed>0,"unknown OFDM presence test case");
    if(failed){std::cerr<<failed<<" of "<<executed<<" OFDM presence cases failed\n";return 1;}
    std::cout<<"Fast OFDM presence tests passed ("<<executed<<" cases)\n";return 0;
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
