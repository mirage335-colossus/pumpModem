#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace datapump;
using Complex=std::complex<double>;
void pattern_transmit_constellation() {
    for(const auto mode:{modem::SpreadingMode::pattern,modem::SpreadingMode::tone})for(const bool raw:{false,true}) {
        modem::Config config;
        config.spreading_mode=mode;config.dsss=mode==modem::SpreadingMode::pattern;config.dsss_seed[0]=73;
        config.scramble=mode==modem::SpreadingMode::pattern;
        config.stream_epoch=1800000031;config.integration_seconds=.0054;
        const auto chip=modem::pattern_chip_samples(config),symbol=modem::symbol_sample_count(config);
        if(symbol%chip==0)throw std::runtime_error("pattern constellation fixture needs partial final chips");
        Bytes wire(65),bits;
        for(std::size_t i=0;i<wire.size();++i) {
            wire[i]=static_cast<std::uint8_t>(i*79+37);
            for(unsigned bit=0;bit<8;++bit)bits.push_back(static_cast<std::uint8_t>((wire[i]>>(7-bit))&1U));
        }
        auto create=[&] {
            return raw?modem::StreamingTransmitter(modem::RawBits{bits},config):modem::StreamingTransmitter(wire,config);
        };
        auto source=create(),pcm=create(),overflow=create();
        if(!source.payload_constellation().empty() || !source.take_payload_constellation().points.empty())
            throw std::runtime_error("unstarted pattern TX invented constellation points");
        const auto training=modem::training_sample_count(config);
        std::vector<Complex> settling(training);std::vector<float> settling_pcm(training);
        source.read_analytic(settling);pcm.read(settling_pcm);
        if(!source.payload_constellation().empty() || !pcm.take_payload_constellation().points.empty())
            throw std::runtime_error("hardware settling audio polluted payload chip constellation");
        auto matches=[](const auto& first,const auto& second) {
            if(first.size()!=second.size())return false;
            for(std::size_t i=0;i<first.size();++i)if(std::abs(first[i]-second[i])>1e-10)return false;
            return true;
        };
        std::vector<Complex> expected;
        std::array<Complex,317> block{};std::array<float,317> pcm_block{};
        constexpr std::array<std::size_t,5> requests{1,3,317,10,71};std::size_t iteration=0;
        while(!source.finished()) {
            const auto start=source.samples_emitted();const auto requested=requests[iteration++%requests.size()];
            const auto count=source.read_analytic(std::span(block).first(requested));
            if(pcm.read(std::span(pcm_block).first(requested))!=count)throw std::runtime_error("pattern PCM sample count differs");
            std::vector<Complex> expected_batch;
            for(std::size_t i=0;i<count;++i) {
                if(std::abs(pcm_block[i]-static_cast<float>(block[i].real()))>1e-6F)
                    throw std::runtime_error("pattern constellation observation altered transmitted PCM");
                const auto position=start+i;
                if((position-training)%symbol%chip==0) {
                    // Recover baseband directly from the emitted analytic
                    // waveform, independently of the chip observer's values.
                    const auto angle=2*std::numbers::pi*static_cast<double>(position)*config.carrier_hz/config.sample_rate;
                    expected_batch.push_back(block[i]*std::polar(1.,-angle));
                }
            }
            const auto batch=source.take_payload_constellation(),pcm_batch=pcm.take_payload_constellation();
            if(batch.dropped || pcm_batch.dropped || !matches(batch.points,expected_batch) || !matches(pcm_batch.points,expected_batch))
                throw std::runtime_error("pattern TX constellation missed, duplicated or rotated actual emitted chip I/Q");
            expected.insert(expected.end(),expected_batch.begin(),expected_batch.end());
            std::array<Complex,17> preview{};source.preview_last_analytic(preview);
            if(!source.take_payload_constellation().points.empty() || !pcm.take_payload_constellation().points.empty())
                throw std::runtime_error("pattern constellation drain or waveform preview replayed chips");
        }
        const auto limit=modem::StreamingTransmitter::constellation_history_limit;
        if(expected.size()!=bits.size()*modem::pattern_chips_per_symbol(config) || expected.size()<=limit)
            throw std::runtime_error("pattern constellation did not retain each partial final chip");
        while(!overflow.finished())overflow.read(pcm_block);
        const auto batch=overflow.take_payload_constellation();
        const std::vector<Complex> newest(expected.end()-limit,expected.end());
        if(batch.dropped!=expected.size()-limit || !matches(batch.points,newest) ||
           !matches(source.payload_constellation(),newest) || !matches(overflow.payload_constellation(),newest))
            throw std::runtime_error("pattern constellation lost chronological history or exact overflow count");
        source.read_analytic(block);overflow.read(pcm_block);
        const auto empty=overflow.take_payload_constellation();
        if(!empty.points.empty() || empty.dropped || !source.take_payload_constellation().points.empty())
            throw std::runtime_error("finished pattern transmitter replayed constellation points");
        if(source.working_bytes()>128*1024)throw std::runtime_error("pattern constellation exceeded fixed DSP workspace");
    }
}

int main() {
    try {
        pattern_transmit_constellation();
        modem::Config config;config.memory_limit=1024; // Batch PCM ceiling does not limit explicit streaming DSP.
        const Bytes bits{0,0,1,1,0,1,0,1,1};
        modem::StreamingTransmitter source(modem::RawBits{bits},config);
        if(source.total_samples()!=modem::training_sample_count(config)+bits.size()*modem::symbol_sample_count(config))
            throw std::runtime_error("raw bitstream was padded after encryption");
        bool rejected=false;try{source.next_symbol();}catch(const Error&){rejected=true;}
        if(!rejected)throw std::runtime_error("pattern transmitter exposed symbol oracle");
        std::vector<float> settling(modem::training_sample_count(config));source.read(settling);
        modem::PatternSearch search;search.frequency_offsets_hz={0};search.initial_stream_symbols=1;
        modem::StreamingReceiver receiver(config,8*1024*1024,search);
        std::array<float,317> samples{};Bytes received;
        const auto harvest=[&]{for(const auto& burst:receiver.take_pattern_bursts())if(burst.bits.size()>received.size())received=burst.bits;};
        while(const auto count=source.read(samples)){receiver.push(std::span(samples).first(count));harvest();}
        std::vector<float> silence(2*modem::symbol_sample_count(config));receiver.push(silence);harvest();
        receiver.finish();harvest();
        if(received!=bits)throw std::runtime_error("pattern streaming PCM did not recover exact unpadded bits");
        const auto before=receiver.working_bytes();receiver.set_workspace_bytes(before+1024*1024);
        receiver.reset();
        if(receiver.synchronized() || receiver.acquiring())throw std::runtime_error("receiver reset retained acquisition state");
        std::stop_source stop;stop.request_stop();rejected=false;
        try{source.read(samples,stop.get_token());}catch(const Error&){rejected=true;}
        if(!rejected)throw std::runtime_error("streaming cancellation ignored");
        config.pattern_symbols=false;config.constellation_bits=6;rejected=false;
        try{modem::StreamingTransmitter obsolete(Bytes(33),config);}catch(const Error&){rejected=true;}
        if(!rejected)throw std::runtime_error("obsolete APSK transmitter accepted");
        std::cout<<"streaming pattern modem tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
