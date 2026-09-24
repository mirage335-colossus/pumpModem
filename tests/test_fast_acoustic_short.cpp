#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace datapump;
using namespace datapump::fast;
namespace {
double reference_snr_db=3;
constexpr double reference_bandwidth_hz=17500;
constexpr std::size_t target_source_bytes=2560;
constexpr std::size_t echo_delay_samples=240;
constexpr std::uint64_t memory_quota=1024*1024;

void require(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error(message);
}

Profile short_profile() {
    const auto preset=resolve_snr_preset(Channel::acoustic_short,reference_snr_db);
    const auto& p=preset.profile;
    require(preset.reference_bandwidth_hz==reference_bandwidth_hz&&
        default_expected_snr(Channel::acoustic_short)==-6&&preset.expected_snr_db==reference_snr_db,
        "short acoustic default or explicit original-band SNR selection changed");
    require(p.channel==Channel::acoustic_short&&p.capacity_mode&&
        (p.compact_convolutional||p.ldpc_frame_bits==16200||small_ldpc_frame(p.ldpc_frame_bits))&&
        (!p.acoustic_ofdm||p.ofdm_training_blocks<16)&&p.interleave_depth>=1&&p.interleave_depth<=16,
        "short acoustic regression no longer uses the separate short-frame profile");
    require(p.sample_rate==48000&&(!p.acoustic_ofdm||p.ofdm_prefix_samples>echo_delay_samples),
        "short acoustic echo must remain inside the cyclic prefix");
    if(reference_snr_db>=-6)require(estimate_transmission(p,true,60).seconds<=12.5,
        "short acoustic preset lost the 12.5-second minimum budget across supported SNR choices");
    return p;
}

double noise_sigma(const Profile& p) {
    // This is SNR over the ORIGINAL 17.5 kHz channel, not the narrowed
    // occupied band. Real white PCM noise contributes 2B/Fs of its power in
    // that band. Keep its density fixed through startup, echo and absence.
    const auto nominal_power=p.acoustic_ofdm?std::pow(p.amplitude/4.5,2):p.amplitude*p.amplitude/2;
    return std::sqrt(nominal_power*p.sample_rate/
        (2*reference_bandwidth_hz*std::pow(10.,reference_snr_db/10)));
}

Bytes random_content() {
    Bytes bytes(target_source_bytes);std::mt19937 random(0x61327368);
    for(auto& byte:bytes)byte=static_cast<std::uint8_t>(random());
    bytes.front()=bytes.back()=0;
    return bytes;
}

Bytes text_content(bool short_message) {
    if(short_message) {
        const std::string text="Short acoustic text: café, こんにちは.\n";
        return Bytes(text.begin(),text.end());
    }
    Bytes text(target_source_bytes);std::mt19937 random(0x74657874);
    for(auto& byte:text)byte=static_cast<std::uint8_t>(' '+random()%95);
    text.back()='\n';return text;
}

std::vector<float> resample(const std::vector<float>& input,double ppm) {
    if(ppm==0)return input;
    const auto ratio=1+ppm*1e-6;
    std::vector<float> output(static_cast<std::size_t>(std::ceil(static_cast<double>(input.size())*ratio)));
    // The same 48-tap Blackman-windowed sinc used by the existing acoustic
    // regression avoids introducing linear-interpolation treble attenuation.
    for(std::size_t i=0;i<output.size();++i) {
        const auto coordinate=static_cast<double>(i)/ratio;
        const auto center=static_cast<std::int64_t>(coordinate);
        double sum=0,weight=0;
        for(int j=-23;j<=24;++j) {
            const auto k=center+j;
            if(k<0||k>=static_cast<std::int64_t>(input.size()))continue;
            const auto t=static_cast<double>(k)-coordinate;
            const auto sinc=std::abs(t)<1e-12?1.:std::sin(std::numbers::pi*t)/(std::numbers::pi*t);
            const auto w=sinc*(.42+.5*std::cos(std::numbers::pi*t/24)+
                .08*std::cos(2*std::numbers::pi*t/24));
            sum+=input[static_cast<std::size_t>(k)]*w;weight+=w;
        }
        output[i]=weight?static_cast<float>(sum/weight):0;
    }
    return output;
}

struct Fixture {
    Profile profile=short_profile();
    Bytes source;
    std::string filename;
    std::optional<Crypto> key;
    TransmitEstimate estimate;
    std::vector<float> waveform;
    Bytes expected_bits;
    Fixture(bool encrypted,bool attachment,bool short_text=false):
        source(attachment?random_content():text_content(short_text)),
        filename(attachment?"café-2.5KiB.bin":""),
        key(encrypted?std::optional<Crypto>(Crypto(Bytes(32,0x69))):std::nullopt) {
        const auto prepared=attachment?
            prepare_xz_attachment(byte_source(source),filename,memory_quota):
            prepare_xz_source(byte_source(source),memory_quota);
        require(prepared.source_bytes==source.size(),"XZ source accounting included its attachment envelope");
        if(attachment)require(prepared.encoded.size()>target_source_bytes,
            "short acoustic target fixture stopped exercising an incompressible 2.5 KiB file");
        estimate=estimate_transmission(profile,encrypted,prepared.encoded.size());
        if(reference_snr_db==3&&source.size()==target_source_bytes)
            require(estimate.seconds<=12.,
                "short acoustic 2.5 KiB transfer exceeded its qualified 12-second total airtime");
        auto encoder=fast::testing::deterministic_encoder(profile,key,byte_source(prepared.encoded),
            0x73686f7274617564ULL,SourceEncoding::xz);
        Transmitter tx(profile,[&](std::span<std::uint8_t> bits) {
            const auto have=encoder.next_interval(bits);
            if(have)expected_bits.insert(expected_bits.end(),bits.begin(),bits.end());
            return have;
        });
        std::array<float,977> chunk{};
        while(!tx.finished()) {
            const auto count=tx.read(chunk);
            waveform.insert(waveform.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
        }
        require(encoder.intervals_emitted()==estimate.intervals&&
            encoder.intervals_emitted()%cycle_intervals(profile)==0&&
            waveform.size()+end_silence_samples(profile)==estimate.samples,
            "short acoustic estimate differs from actual complete sampled coding cycles and absence");
        require(end_silence_samples(profile)>=6ULL*profile.sample_rate,
            "short acoustic estimate omitted the mandatory six-second physical absence");
    }
};

std::vector<float> channel_signal(const Fixture& fixture,double echo,double ppm) {
    constexpr std::size_t startup_samples=1371;
    std::vector<float> input(startup_samples+fixture.waveform.size()+(echo?echo_delay_samples:0));
    for(std::size_t i=0;i<fixture.waveform.size();++i) {
        input[startup_samples+i]+=fixture.waveform[i];
        if(echo)input[startup_samples+i+echo_delay_samples]+=static_cast<float>(echo)*fixture.waveform[i];
    }
    // Retain the complete causal echo and resample only the clean channel.
    // Receiver noise is added afterward, so clock drift cannot color it.
    return resample(input,ppm);
}

void sampled_completion(const char* label,bool encrypted,bool attachment,bool short_text,
                        double echo,double ppm,std::uint32_t seed) {
    const Fixture f(encrypted,attachment,short_text);
    const auto signal=channel_signal(f,echo,ppm);
    StreamDecoder decoder(f.profile,f.key,memory_quota,SourceEncoding::xz);
    std::uint64_t intervals=0,hard_errors=0,unknown_bits=0;
    Receiver receiver(f.profile,[&](std::span<const float> soft) {
        require(++intervals<=f.estimate.intervals,"short acoustic receiver invented tail intervals");
        for(std::size_t i=0;i<soft.size();++i) {
            unknown_bits+=soft[i]==0;
            hard_errors+=(soft[i]>0)!=f.expected_bits[(intervals-1)*physical_interval_bits+i];
        }
        decoder.push_interval(soft);
    });
    std::mt19937 random(seed);
    std::normal_distribution<float> gaussian(0,static_cast<float>(noise_sigma(f.profile)));
    std::array<float,1201> chunk{};
    for(std::size_t at=0;at<signal.size();) {
        const auto count=std::min(chunk.size(),signal.size()-at);
        for(std::size_t i=0;i<count;++i)chunk[i]=signal[at+i]+gaussian(random);
        receiver.push(std::span<const float>(chunk).first(count));at+=count;
        require(!receiver.progress().physical_complete,
            std::string(label)+": continuing signal or its causal echo was classified complete");
        require(!decoder.snapshot().complete&&!decoder.result()&&!decoder.snapshot().source_bytes,
            "short acoustic source or metadata escaped before physical completion");
    }
    require(receiver.progress().acquired,std::string(label)+": valid short training was not acquired");
    decoder.finish(false);
    require(!decoder.result()&&!decoder.snapshot().complete,
        "short acoustic cancellation or codec completion substituted for physical absence");
    const auto feed_noise=[&](std::uint64_t samples) {
        while(samples) {
            const auto count=std::min<std::uint64_t>(samples,chunk.size());
            for(std::size_t i=0;i<count;++i)chunk[i]=gaussian(random);
            receiver.push(std::span<const float>(chunk).first(count));samples-=count;
        }
    };
    const auto partial=static_cast<std::uint64_t>(5.5*f.profile.sample_rate);
    feed_noise(partial);
    require(!receiver.progress().physical_complete,
        "short acoustic profile completed after fewer than six seconds of observed absence");
    decoder.finish(receiver.progress().physical_complete);
    require(!decoder.result()&&!decoder.snapshot().complete&&!decoder.snapshot().source_bytes,
        "short acoustic partial absence exposed source bytes or attachment metadata");
    feed_noise(end_silence_samples(f.profile)-partial);
    receiver.finish();decoder.finish(receiver.progress().physical_complete);
    const auto state=decoder.snapshot();const auto result=decoder.result();
    std::cout<<label<<" snr_db="<<reference_snr_db<<" source_bytes="<<f.source.size()<<" air_seconds="<<f.estimate.seconds
        <<" echo="<<echo<<" ppm="<<ppm<<" acquired="<<receiver.progress().acquired
        <<" complete="<<state.complete<<" intervals="<<intervals
        <<" hard_errors="<<hard_errors<<" unknown_bits="<<unknown_bits<<" changed_bits="<<state.ldpc_changed_bits<<" failed_frames="<<state.ldpc_failed_frames
        <<" status="<<state.status<<'\n';
    require(receiver.progress().physical_complete&&state.physical_end&&state.complete&&result,
        std::string(label)+": noisy short acoustic source did not complete exactly");
    require(result->size()==f.source.size()&&state.source_bytes==f.source.size()&&
        Bytes(result->bytes().begin(),result->bytes().end())==f.source,
        "short acoustic source bytes changed or include the attachment envelope");
    require(result->is_attachment()==attachment&&result->filename()==f.filename,
        "short acoustic text or attachment metadata changed");
    require(state.encrypted==encrypted&&state.authenticated==encrypted,
        "short acoustic keyed/public integrity state changed");
    require(intervals==f.estimate.intervals&&state.intervals==f.estimate.intervals&&
        !state.failed_cycles&&!state.ldpc_failed_frames&&hard_errors>0&&
        state.coding_cycles==f.estimate.intervals/cycle_intervals(f.profile),
        "short acoustic noisy coding fixture lost fixed positions or did not exercise error correction");
    if(f.profile.compact_convolutional)
        require(!state.ldpc_frames&&!state.ldpc_iterations&&!state.ldpc_changed_bits,
            "compact convolutional correction was mislabeled as LDPC evidence");
    else require(state.ldpc_changed_bits>0&&
        state.ldpc_frames==state.coding_cycles*f.profile.interleave_depth,
        "short acoustic LDPC correction lost its frame or changed-bit evidence");
    require(receiver.workspace_bytes()<8*1024*1024,"short acoustic receiver retained unbounded PCM");
}

void incomplete_controls() {
    const Fixture f(true,true);
    StreamDecoder decoder(f.profile,f.key,memory_quota,SourceEncoding::xz);
    Receiver receiver(f.profile,[&](std::span<const float> soft){decoder.push_interval(soft);});
    receiver.push(f.waveform);receiver.finish();decoder.finish(receiver.progress().physical_complete);
    require(receiver.progress().acquired&&!receiver.progress().physical_complete&&
        !decoder.snapshot().physical_end&&!decoder.snapshot().complete&&!decoder.result()&&
        !decoder.snapshot().source_bytes,
        "short acoustic sampled EOF substituted for physical absence");

    std::uint64_t intervals=0;
    Receiver truncated(f.profile,[&](std::span<const float>){++intervals;});
    const auto prefix=f.profile.acoustic_ofdm?
        (preamble_symbols(f.profile)-1)*(f.profile.ofdm_fft_size+f.profile.ofdm_prefix_samples):
        static_cast<std::uint64_t>((preamble_symbols(f.profile)-1)*f.profile.sample_rate/f.profile.symbol_rate);
    truncated.push(std::span<const float>(f.waveform).first(prefix));
    truncated.push(std::vector<float>(f.profile.sample_rate*8));truncated.finish();
    require(!truncated.progress().acquired&&!truncated.progress().physical_complete&&!intervals,
        "short acoustic incomplete training was accepted without its complete initial marker");
    std::cout<<"incomplete_controls EOF and incomplete training remain pending\n";
}

void noise_only() {
    const auto p=short_profile();std::uint64_t intervals=0;
    StreamDecoder decoder(p,std::nullopt,memory_quota,SourceEncoding::xz);
    Receiver receiver(p,[&](std::span<const float> soft){++intervals;decoder.push_interval(soft);});
    std::mt19937 random(0x6e6f6973);
    std::normal_distribution<float> gaussian(0,static_cast<float>(noise_sigma(p)));
    std::array<float,1201> chunk{};
    auto remaining=static_cast<std::uint64_t>(p.sample_rate)*20;
    while(remaining) {
        const auto count=std::min<std::uint64_t>(remaining,chunk.size());
        for(std::size_t i=0;i<count;++i)chunk[i]=gaussian(random);
        receiver.push(std::span<const float>(chunk).first(count));remaining-=count;
    }
    receiver.finish();decoder.finish(receiver.progress().physical_complete);
    require(!receiver.progress().acquired&&!receiver.progress().physical_complete&&!intervals&&
        !decoder.snapshot().complete&&!decoder.result(),
        "short acoustic noise-only recording invented acquisition, intervals or a source");
    require(receiver.workspace_bytes()<8*1024*1024,"short acoustic noise-only PCM grew without bound");
    std::cout<<"noise_only acquired=0 intervals=0 complete=0\n";
}
}

int main(int argc,char** argv) {try {
    std::string_view selected_case;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg.starts_with("--snr=")) {
            std::size_t consumed=0;
            const auto value=std::string(arg.substr(6));
            reference_snr_db=std::stod(value,&consumed);
            require(consumed==value.size()&&std::isfinite(reference_snr_db),"invalid test SNR");
        } else {
            require(selected_case.empty(),"only one short acoustic test case may be selected");
            selected_case=arg;
        }
    }
    unsigned executed=0,failed=0;
    const auto run=[&](const char* name,const std::function<void()>& test) {
        if(!selected_case.empty()&&selected_case!=name)return;
        ++executed;
        try{test();}catch(const std::exception& error){++failed;std::cerr<<name<<": "<<error.what()<<'\n';}
    };
    run("flat_text",[]{sampled_completion("flat_text",false,false,true,0,100,7101);});
    run("public_text",[]{sampled_completion("public_text",false,false,true,.30,100,7101);});
    run("keyed_text",[]{sampled_completion("keyed_text",true,false,false,.30,-100,7102);});
    run("public_attachment",[]{sampled_completion("public_attachment",false,true,false,.30,0,7103);});
    run("keyed_attachment",[]{sampled_completion("keyed_attachment",true,true,false,.30,100,7104);});
    run("incomplete_controls",incomplete_controls);
    run("noise_only",noise_only);
    require(executed>0,"unknown short acoustic case");
    if(failed){std::cerr<<failed<<" of "<<executed<<" short acoustic cases failed\n";return 1;}
    std::cout<<"Fast short acoustic sampled tests passed ("<<executed<<" cases)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
