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
void receiver_input_modes() {
    modem::Config config;
    modem::StreamingReceiver receiver(config,modem::preamble(config));
    const std::array<modem::SymbolObservation,1> integrated{{{{.1,.2},1}}};
    const std::array<float,1> pcm{.1F};
    receiver.push({});receiver.push_symbols(integrated);receiver.push({});
    bool rejected=false;try{receiver.push(pcm);}catch(const Error&){rejected=true;}
    if(!rejected)throw std::runtime_error("receiver silently mixed integrated and PCM partial symbols");
    receiver.reset();receiver.push_symbols({});receiver.push(pcm);receiver.push_symbols({});
    rejected=false;try{receiver.push_symbols(integrated);}catch(const Error&){rejected=true;}
    if(!rejected)throw std::runtime_error("receiver silently mixed PCM and integrated partial symbols");
    receiver.reset();receiver.push_symbols(integrated);receiver.finish();
}
void exact_pcm_boundaries() {
    // Ten- and nine-sample chips are deliberately not multiples of a four
    // sample I/Q integration quantum. Dense symbols must retain their exact
    // chip and symbol boundaries, including across arbitrary input chunks.
    for(const double bandwidth:{1200.,1499.,1499.25,1703.})
        for(unsigned bits=4;bits<=6;++bits)for(unsigned mode=0;mode<2;++mode) {
        modem::Config config;config.bandwidth_hz=bandwidth;
        config.sample_rate=static_cast<unsigned>(std::ceil(std::max(6000.,4*bandwidth)));
        config.carrier_hz=1500;config.constellation_bits=bits;
        config.spreading_mode=mode?modem::SpreadingMode::pattern:modem::SpreadingMode::tone;
        config.spreading_factor=mode?3:1;
        Message message;message.kind=MessageKind::file;message.filename="boundary.bin";message.id[0]=37;
        for(unsigned i=0;i<131;++i)message.data.push_back(static_cast<std::uint8_t>(i*71+19));
        PacketOptions options;options.fec=bits==5?FecMode::off:FecMode::rs60;
        const auto frame=encode_packet(message,options);
        auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
        const Crypto key(Bytes(32,0x3d));constexpr std::uint64_t epoch=1800000000;
        auto wire=key.xor_data(plain,epoch);
        // Fixed zero training ends on phase zero, giving the undelayed case
        // a known reference for a completely byte-exact pre-FEC comparison.
        std::fill_n(wire.begin(),32,0);
        for(const unsigned delay:{0U,17U}) {
            modem::StreamingTransmitter source(wire,config);
            modem::StreamingReceiver receiver(config,Bytes(wire.begin(),wire.begin()+32),8*1024*1024,[&](const Bytes& prefix){
                // Isolate exact PCM boundaries from blind acquisition's
                // training-edge aliases, whose first two symbol decisions
                // can need bootstrap FEC. Other tests exercise that policy.
                if(prefix.size()!=packet_prefix_size)return false;
                for(std::size_t i=0;i<prefix.size();++i) {
                    const unsigned reference_bits=(delay || mode) && i==0?7U<<(8-bits):0;
                    if(((prefix[i]^wire[32+i])&~reference_bits)!=0)return false;
                }
                return true;
            });
            receiver.push(std::vector<float>(delay));
            constexpr std::array<std::size_t,6> partitions{1,13,257,511,7,96};
            std::array<float,511> block{};Bytes received;std::size_t partition=0;
            while(!source.finished()) {
                const auto count=source.read(std::span(block).first(partitions[partition++%partitions.size()]));
                const auto bytes=receiver.push(std::span(block).first(count));received.insert(received.end(),bytes.begin(),bytes.end());
            }
            const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
            const auto context=" at "+std::to_string(bandwidth)+" Hz, "+std::to_string(bits)+" bits, mode "+std::to_string(mode)+", delay "+std::to_string(delay);
            if(received.size()<wire.size())throw std::runtime_error("PCM boundary acquisition failed"+context);
            for(std::size_t i=0;i<wire.size();++i) {
                // Capture delay or a phase-inverted pattern timing alias
                // leaves only the first symbol's differential phase unknown.
                // Its amplitude, every later header bit and all body bytes
                // must be exact before FEC. Undelayed tones compare every bit.
                const unsigned reference_bits=(delay || mode) && i==32?7U<<(8-bits):0;
                if(((wire[i]^received[i])&~reference_bits)!=0)
                    throw std::runtime_error("PCM boundary changed wire byte "+std::to_string(i)+" ("+std::to_string(wire[i])+" to "+std::to_string(received[i])+", offset "+std::to_string(receiver.diagnostics().sample_offset)+") before error correction"+context);
            }
            received=key.xor_data(received,epoch);
            try {
                const auto decoded=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(plain.size())),options);
                if(decoded.message.data!=message.data)throw std::runtime_error("PCM boundary payload mismatch"+context);
            } catch(const Error& error){throw std::runtime_error(std::string(error.what())+context);}
            if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("PCM boundary integration exceeded workspace");
        }
    }
    modem::Config slow;slow.spreading_factor=16384;slow.integration_seconds=3600;
    modem::StreamingReceiver idle(slow,modem::preamble(slow));
    idle.push(std::array<float,3>{.1F,.2F,-.1F});
    const auto before=std::chrono::steady_clock::now();
    if(!idle.finish().empty() || idle.synchronized() || std::chrono::steady_clock::now()-before>std::chrono::seconds(2))
        throw std::runtime_error("PCM end-of-capture padding scales with an hour-long symbol");
    if(!idle.finish().empty())throw std::runtime_error("PCM capture finish is not idempotent");
}
void transmitted_constellation_history() {
    modem::Config config;config.constellation_bits=6;
    auto wire=modem::preamble(config);
    for(unsigned i=0;i<2101;++i)wire.push_back(static_cast<std::uint8_t>(i*79+37));
    modem::StreamingTransmitter source(wire,config),pcm(wire,config);
    if(!source.payload_constellation().empty())throw std::runtime_error("unstarted TX constellation contains points");
    const auto training=modem::training_sample_count(config);
    while(source.samples_emitted()<training) {
        source.next_symbol();
        if(!source.payload_constellation().empty())throw std::runtime_error("fixed training polluted payload constellation");
    }
    std::vector<std::complex<double>> expected;
    const auto symbol=modem::symbol_sample_count(config);
    while(!source.finished()) {
        const auto start=source.samples_emitted();
        const auto observation=source.next_symbol();
        if((start-training)%symbol==0)expected.push_back(observation->value);
    }
    if(expected.size()<=modem::StreamingTransmitter::constellation_history_limit)
        throw std::runtime_error("TX ring fixture does not exercise overwrite");
    expected.erase(expected.begin(),expected.end()-modem::StreamingTransmitter::constellation_history_limit);
    const auto recent=source.payload_constellation();
    if(recent!=expected)throw std::runtime_error("TX constellation is not the bounded chronological payload history");
    std::array<float,317> block{};
    while(!pcm.finished())pcm.read(block);
    if(pcm.payload_constellation()!=recent)throw std::runtime_error("PCM and integrated TX histories differ");
    const auto unchanged=source.payload_constellation();source.next_symbol();
    if(source.payload_constellation()!=unchanged)throw std::runtime_error("finished transmitter appended a phantom symbol");

    config.spreading_mode=modem::SpreadingMode::tone;config.integration_seconds=3600;
    wire.resize(32+90);modem::StreamingTransmitter slow(wire,config);
    while(slow.samples_emitted()<modem::training_sample_count(config))slow.next_symbol();
    if(!slow.payload_constellation().empty())throw std::runtime_error("slow TX includes training points");
    slow.next_symbol();const auto first=slow.payload_constellation();
    if(first.size()!=1)throw std::runtime_error("first partial slow symbol is not visible");
    slow.next_symbol();
    if(slow.payload_constellation()!=first)throw std::runtime_error("partial integrations duplicate the slow symbol");
    while(!slow.finished())slow.next_symbol();
    if(slow.payload_constellation().size()!=modem::payload_symbol_count(wire.size()-32,config))
        throw std::runtime_error("TX history lost symbols spanning hours of media time");
}
void live_constellation_window() {
    modem::Config config;
    Message message;message.id[0]=0x71;message.data={'l','i','v','e'};
    auto wire=modem::preamble(config);const auto frame=encode_packet(message);wire.insert(wire.end(),frame.begin(),frame.end());
    modem::StreamingTransmitter source(wire,config);modem::StreamingReceiver receiver(config,modem::preamble(config));
    while(const auto observation=source.next_symbol())receiver.push_symbols(std::span(&*observation,1));
    if(!receiver.synchronized())throw std::runtime_error("live constellation fixture failed acquisition");
    const modem::SymbolObservation outer{{.7,0},modem::symbol_sample_count(config)};
    for(unsigned i=0;i<2200;++i)receiver.push_symbols(std::span(&outer,1));
    const modem::SymbolObservation inner{{.35,0},modem::symbol_sample_count(config)};
    for(unsigned i=0;i<2200;++i)receiver.push_symbols(std::span(&inner,1));
    const auto diagnostic=receiver.diagnostics();
    if(diagnostic.constellation.size()!=2048)throw std::runtime_error("live constellation lost its bounded accumulation");
    for(const auto point:diagnostic.constellation)
        if(std::abs(point-std::complex<double>{.35,0})>1e-8)throw std::runtime_error("live constellation retained old amplitude samples after its window advanced");
    const modem::SymbolObservation newest{{.63,.11},modem::symbol_sample_count(config)};
    receiver.push_symbols(std::span(&newest,1));
    const auto latest=receiver.diagnostics();
    if(std::abs(latest.constellation.back()-newest.value)>1e-8 || std::abs(latest.constellation.front()-inner.value)>1e-8)
        throw std::runtime_error("live constellation accumulation is not chronological");
    if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("live constellation ring exceeded receiver workspace");
}
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
        receiver_input_modes();
        exact_pcm_boundaries();
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
        bool interrupted=false;Bytes idle_output;
        try{idle_output=waiting.push_symbols(std::span(&enormous_idle,1),interrupt.get_token());}
        catch(const Error& error){interrupted=std::string_view(error.what())=="modem operation cancelled";}
        if(!idle_output.empty() || waiting.synchronized() || std::chrono::steady_clock::now()-before>std::chrono::seconds(2))
            throw std::runtime_error("constant idle observation was not skipped or cancelled promptly");
        if(!interrupted) {
            interrupt.request_stop();
            try{waiting.push_symbols(std::span(&enormous_idle,1),interrupt.get_token());}
            catch(const Error& error){interrupted=std::string_view(error.what())=="modem operation cancelled";}
            if(!interrupted)throw std::runtime_error("fast idle acquisition ignored requested cancellation");
        }
        long_keyed_pcm();
        adaptive_roundtrips();
        dense_missing_outer_ring();
        dense_gain_aliases();
        recent_pcm_preview();
        transmitted_constellation_history();
        live_constellation_window();
        std::cout<<"streaming modem tests passed\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
