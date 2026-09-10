#include "datapump/streaming_modem.hpp"
#include "datapump/packet.hpp"
#include "datapump/crypto.hpp"
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
        std::cout<<"streaming modem tests passed\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
