#include "datapump/streaming_modem.hpp"
#include "datapump/packet.hpp"
#include "datapump/crypto.hpp"
#include "datapump/tuning.hpp"
#include "../src/constellation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
using namespace datapump;
void long_keyed_pcm() {
    modem::Config config;config.sample_rate=8000;config.spreading_factor=1024;
    config.scramble=true;config.spreading_seed[0]=29;
    config.memory_limit=1024; // Legacy waveform storage is deliberately unavailable.
    Message message;message.id[0]=31;message.data={'C','Q'};
    PacketOptions options;options.fec=FecMode::off;
    const Crypto key(Bytes(32,0x49));constexpr std::uint64_t epoch=1800000000;
    options.authenticator=[key](const Bytes& bytes){return key.mac(bytes);};
    options.verifier=[key](const Bytes& bytes,const Bytes& tag){return key.verify(bytes,tag);};
    const auto frame=encode_packet(message,options);
    auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
    auto wire=key.xor_data(plain,epoch);
    const Bytes expected(wire.begin(),wire.begin()+32);
    const auto mask=key.stream(StreamPurpose::Data,epoch,32,packet_prefix_size);
    modem::StreamingTransmitter source(wire,config);
    modem::StreamingReceiver receiver(config,expected,8*1024*1024,[mask](const Bytes& prefix){
        auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
        try{return packet_bootstrap_possible(header) && packet_frame_size(header).has_value();}catch(const Error&){return false;}
    });
    receiver.push(std::array<float,17>{});
    std::array<float,317> block{};Bytes received;
    std::mt19937_64 random(8192);
    std::normal_distribution<float> noise(0,static_cast<float>(std::sqrt(modem::nominal_signal_power*std::pow(10.,.5))));
    while(!source.finished()) {
        const auto begin=source.samples_emitted();
        const auto count=source.read(block);
        // Training provides no acquisition assistance for this fixture.
        for(std::size_t i=0;i<count;++i)if(begin+i<modem::training_sample_count(config))block[i]=0;
        for(std::size_t i=0;i<count;++i)block[i]+=noise(random);
        const auto bytes=receiver.push(std::span(block).first(count));received.insert(received.end(),bytes.begin(),bytes.end());
        if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("long keyed PCM exceeded workspace");
    }
    const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
    if(received.size()<plain.size())throw std::runtime_error("delayed long keyed PCM failed blind acquisition");
    received=key.xor_data(received,epoch);
    const auto packet=decode_packet(Bytes(received.begin()+32,received.end()),options);
    if(packet.message.data!=message.data || !packet.authenticated)throw std::runtime_error("long keyed PCM changed authenticated bytes");
}
void adaptive_roundtrips() {
    for(unsigned bits=2;bits<=6;++bits)for(unsigned trial=0;trial<(bits>=5?10U:2U);++trial) {
        modem::Config config;config.constellation_bits=bits;config.spreading_factor=32;
        config.spreading_mode=modem::SpreadingMode::tone;
        Message message;message.kind=MessageKind::file;message.filename="data.bin";message.id[0]=static_cast<std::uint8_t>(bits);
        std::mt19937_64 random(61+bits+trial*19);
        for(unsigned i=0;i<103;++i)message.data.push_back(static_cast<std::uint8_t>(random()));
        PacketOptions options;options.fec=trial<2?FecMode::off:FecMode::rs60;
        auto frame=encode_packet(message,options);
        // Exact lattice points carrying up to16 corrupted bootstrap bytes
        // remain within its Reed-Solomon correction radius.
        if(trial>=8)for(unsigned i=0;i<16;++i)frame[i]^=static_cast<std::uint8_t>(91+i);
        auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
        const Crypto key(Bytes(32,0x53));constexpr std::uint64_t epoch=1800000000;
        const auto wire=trial?key.xor_data(plain,epoch):plain;
        const Bytes expected(wire.begin(),wire.begin()+32);
        const auto mask=key.stream(StreamPurpose::Data,epoch,32,packet_prefix_size);
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,expected,8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;if(trial)for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            try{return packet_bootstrap_possible(header) && packet_frame_size(header).has_value();}catch(const Error&){return false;}
        });
        const auto sample_snr=tuning::constellation_target_symbol_snr_db(bits)+(trial>=2 && trial<8?0:8)-10*std::log10(modem::symbol_seconds(config)*config.sample_rate/2);
        const double gain=trial?.62:1.35;Bytes received;
        while(auto observation=source.next_symbol()) {
            if(source.samples_emitted()<=modem::training_sample_count(config))observation->value={};
            if(trial>=2) {
                const auto phase=std::numbers::pi/64*(static_cast<double>(source.samples_emitted())-.5*static_cast<double>(observation->sample_count))/static_cast<double>(modem::symbol_sample_count(config));
                observation->value*=std::polar(1.,std::remainder(phase,2*std::numbers::pi));
            }
            *observation=modem::add_awgn(*observation,sample_snr,random);observation->value*=gain;
            const auto bytes=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),bytes.begin(),bytes.end());
            if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("adaptive acquisition exceeded bounded workspace");
        }
        const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
        if(received.size()<plain.size())throw std::runtime_error("adaptive "+std::to_string(bits)+"-bit acquisition failed");
        if(trial)received=key.xor_data(received,epoch);
        const auto result=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(plain.size())),options);
        if(result.message.data!=message.data)throw std::runtime_error("adaptive constellation changed payload");
        const auto expected_samples=modem::training_sample_count(config)+modem::payload_symbol_count(frame.size(),config)*modem::symbol_sample_count(config);
        if(source.total_samples()!=expected_samples)throw std::runtime_error("adaptive symbol padding changed estimated airtime");
    }
}
void dense_missing_outer_ring() {
    // Pinned from a search of actual AES epoch streams. The protected
    // bootstrap occupies only rings1..7, although the complete64APSK
    // constellation has eight rings; gain acquisition must still succeed.
    modem::Config config;config.constellation_bits=6;config.spreading_mode=modem::SpreadingMode::tone;
    Message message;message.id[0]=81;
    for(unsigned i=0;i<29;++i)message.data.push_back(static_cast<std::uint8_t>(i*17+91));
    PacketOptions options;options.fec=FecMode::off;
    const auto frame=encode_packet(message,options);
    auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
    const Crypto key(Bytes(32,0x53));constexpr std::uint64_t epoch=1801378970;
    const auto wire=key.xor_data(plain,epoch);const Bytes expected(wire.begin(),wire.begin()+32);
    const auto encoded_header=std::span(wire).subspan(32,packet_prefix_size);
    unsigned maximum=0;
    for(unsigned symbol=0;symbol<96;++symbol)
        maximum=std::max(maximum,1+modem::detail::gray_decode(modem::detail::read_bits(encoded_header,symbol*6,6)>>3));
    if(maximum!=7)throw std::runtime_error("missing outer-ring fixture no longer exercises the intended lattice");
    const auto mask=key.stream(StreamPurpose::Data,epoch,32,packet_prefix_size);
    modem::StreamingTransmitter source(wire,config);
    modem::StreamingReceiver receiver(config,expected,8*1024*1024,[&](const Bytes& prefix){
        auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
        try{return packet_bootstrap_possible(header) && packet_frame_size(header).has_value();}catch(const Error&){return false;}
    });
    Bytes received;
    while(auto observation=source.next_symbol()) {
        if(source.samples_emitted()<=modem::training_sample_count(config))observation->value={};
        observation->value*=.62;
        const auto bytes=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),bytes.begin(),bytes.end());
    }
    const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
    if(received.size()<plain.size())throw std::runtime_error("missing outer-ring bootstrap did not acquire");
    received=key.xor_data(received,epoch);
    if(decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(plain.size())),options).message.data!=message.data)
        throw std::runtime_error("missing outer-ring bootstrap selected an incorrect gain");
}
void dense_gain_aliases() {
    // Artificial XOR-stream fixtures exercise rare but valid occupied-ring
    // subsets. Several gain hypotheses have an equally clean amplitude
    // lattice; the protected bootstrap must decide which gain is correct.
    modem::Config config;config.constellation_bits=6;config.spreading_factor=32;
    config.spreading_mode=modem::SpreadingMode::tone;
    PacketOptions options;options.fec=FecMode::off;
    for(unsigned maximum=2;maximum<=7;++maximum)for(unsigned variant=0;variant<2;++variant) {
        Message message;message.id[0]=static_cast<std::uint8_t>(variant);
        for(unsigned i=0;i<29;++i)message.data.push_back(static_cast<std::uint8_t>(i*17+variant));
        const auto frame=encode_packet(message,options);Bytes coded(frame.begin(),frame.begin()+packet_prefix_size);
        for(unsigned symbol=0;symbol<96;++symbol) {
            auto value=modem::detail::read_bits(coded,symbol*6,6);
            const auto ring=modem::detail::gray_decode(value>>3)%maximum;
            value=(value&7)|((ring^(ring>>1))<<3);
            for(unsigned position=0;position<6;++position) {
                const auto bit=symbol*6+position;
                coded[bit/8]=static_cast<std::uint8_t>((coded[bit/8]&~(1U<<(7-bit%8)))|(((value>>(5-position))&1U)<<(7-bit%8)));
            }
        }
        Bytes mask(packet_prefix_size);
        for(std::size_t i=0;i<mask.size();++i)mask[i]=coded[i]^frame[i];
        auto wire=modem::preamble(config);wire.insert(wire.end(),coded.begin(),coded.end());wire.insert(wire.end(),frame.begin()+packet_prefix_size,frame.end());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,modem::preamble(config),8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            try{return packet_bootstrap_possible(header) && packet_frame_size(header).has_value();}catch(const Error&){return false;}
        });
        Bytes received;
        while(auto observation=source.next_symbol()) {
            if(source.samples_emitted()<=modem::training_sample_count(config))observation->value={};
            observation->value*=1.35;
            const auto bytes=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),bytes.begin(),bytes.end());
        }
        const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
        if(received.size()<wire.size())throw std::runtime_error("sparse amplitude lattice failed bootstrap acquisition");
        for(std::size_t i=0;i<mask.size();++i)received[32+i]^=mask[i];
        if(decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(wire.size())),options).message.data!=message.data)
            throw std::runtime_error("bootstrap chose an aliased amplitude gain");
    }
}
void recent_pcm_preview() {
    for(unsigned mode=0;mode<3;++mode) {
        modem::Config config;config.constellation_bits=6;
        if(mode==0){config.sample_rate=48000;config.bandwidth_hz=24000;config.carrier_hz=12000;config.spreading_mode=modem::SpreadingMode::tone;}
        if(mode==1){config.spreading_factor=3;config.scramble=true;config.dsss=true;config.spreading_seed[0]=51;config.dsss_seed[0]=29;}
        if(mode==2){config.spreading_factor=32;config.integration_seconds=2.5;config.scramble=true;config.spreading_seed[0]=75;}
        auto wire=modem::preamble(config);
        for(unsigned i=0;i<(mode==2?10U:1800U);++i)wire.push_back(static_cast<std::uint8_t>(i*71+19));
        modem::StreamingTransmitter analytical(wire,config),pcm(wire,config);
        std::array<float,2048> recent{},actual{},direct{};
        const auto training=modem::training_sample_count(config);
        for(const auto target:{std::uint64_t{7},training-17,training+317,training+13000,analytical.total_samples()}) {
            while(analytical.samples_emitted()<target && !analytical.finished())analytical.next_symbol();
            while(pcm.samples_emitted()<analytical.samples_emitted()) {
                std::array<float,317> block{};
                const auto requested=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),analytical.samples_emitted()-pcm.samples_emitted()));
                const auto count=pcm.read(std::span(block).first(requested));
                std::move(recent.begin()+static_cast<std::ptrdiff_t>(count),recent.end(),recent.begin());
                std::copy_n(block.begin(),count,recent.end()-static_cast<std::ptrdiff_t>(count));
            }
            analytical.preview_last(actual);pcm.preview_last(direct);
            for(std::size_t i=0;i<recent.size();++i)
                if(std::abs(actual[i]-recent[i])>2e-5F || std::abs(direct[i]-recent[i])>2e-5F)
                    throw std::runtime_error("preview does not reconstruct recent transmitted PCM for mode "+std::to_string(mode));
        }
    }
}
int main() {
    try {
        modem::Config config;
        config.spreading_mode=modem::SpreadingMode::tone;
        config.spreading_factor=1024;
        Message message; message.id[0]=41; message.data=Bytes(128,0x73);
        const auto frame=encode_packet(message);
        auto wire=modem::preamble(config);wire.insert(wire.end(),frame.begin(),frame.end());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,modem::preamble(config),8*1024*1024,
            [](const Bytes& prefix){try{return packet_frame_size(prefix).has_value();}catch(const Error&){return false;}});
        Bytes recovered;
        std::mt19937_64 random(71);
        bool inner=false,outer=false;
        while(auto observation=source.next_symbol()) {
            const double radius=std::abs(observation->value);
            inner=inner || std::abs(radius-.35)<1e-8;
            outer=outer || std::abs(radius-.7)<1e-8;
            if(source.samples_emitted()<=modem::training_sample_count(config)) observation->value={0,0};
            auto noisy=modem::add_awgn(*observation,-15,random);
            const auto bytes=receiver.push_symbols(std::span(&noisy,1));
            recovered.insert(recovered.end(),bytes.begin(),bytes.end());
            if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("unbounded receive DSP");
        }
        const auto tail=receiver.finish();
        recovered.insert(recovered.end(),tail.begin(),tail.end());
        if(!receiver.finish().empty())throw std::runtime_error("capture finish is not idempotent");
        bool appended=false;try{receiver.push({});}catch(const Error&){appended=true;}
        if(!appended)throw std::runtime_error("finished capture accepted further input");
        std::stop_source stopped;stopped.request_stop();
        bool cancelled=false;try{receiver.finish(stopped.get_token());}catch(const Error& error){cancelled=std::string_view(error.what())=="modem operation cancelled";}
        if(!cancelled)throw std::runtime_error("streaming cancellation was ignored");
        if(!inner || !outer)throw std::runtime_error("16APSK needs two amplitude rings");
        if(recovered.size()<32+frame.size())throw std::runtime_error("blind acquisition did not recover complete frame");
        const Bytes payload(recovered.begin()+32,recovered.end());
        if(decode_packet(payload).message.data!=message.data)throw std::runtime_error("streaming payload mismatch");
        if(modem::training_sample_count(config)!=5*config.sample_rate)throw std::runtime_error("preamble is not exactly five seconds");
        modem::Config short_config;
        short_config.spreading_mode=modem::SpreadingMode::tone;
        modem::StreamingReceiver waiting(short_config,modem::preamble(short_config));
        std::stop_source interrupt;
        std::jthread request_stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));interrupt.request_stop();});
        const auto before=std::chrono::steady_clock::now();
        const modem::SymbolObservation enormous_idle{{},std::uint64_t{1}<<48};
        bool interrupted=false;
        try{waiting.push_symbols(std::span(&enormous_idle,1),interrupt.get_token());}
        catch(const Error& error){interrupted=std::string_view(error.what())=="modem operation cancelled";}
        if(!interrupted || std::chrono::steady_clock::now()-before>std::chrono::seconds(2))
            throw std::runtime_error("large integrated observation cannot be cancelled promptly");
        long_keyed_pcm();
        adaptive_roundtrips();
        dense_missing_outer_ring();
        dense_gain_aliases();
        recent_pcm_preview();
        std::cout<<"streaming modem tests passed\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
