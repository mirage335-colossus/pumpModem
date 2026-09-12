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
modem::PacketValidator masked_packet_validator(Bytes mask,PacketOptions options={}) {
    return [mask=std::move(mask),options=std::move(options)](const Bytes& wire) {
        auto packet=wire;
        for(std::size_t i=0;i<std::min(mask.size(),packet.size());++i)packet[i]^=mask[i];
        try{(void)decode_packet(packet,options);return true;}catch(const Error&){return false;}
    };
}
using Complex=std::complex<double>;
Complex decision_coordinates(Complex point,Complex previous) {
    return std::abs(previous)>1e-20?point*std::conj(previous)/std::abs(previous):point;
}
void raw_binary_transmitter() {
    for(unsigned width=2;width<=6;++width)for(std::size_t size=1;size<=17;++size) {
        modem::Config config;config.constellation_bits=width;config.spreading_factor=3;
        Bytes bits(size);for(std::size_t i=2;i<size;++i)bits[i]=static_cast<std::uint8_t>((i*7+i/3)&1);
        modem::StreamingTransmitter source(modem::RawBits{bits},config),pcm(modem::RawBits{bits},config);
        const auto symbols=size/width+(size%width!=0);
        const auto expected_samples=symbols*modem::symbol_sample_count(config);
        if(source.total_samples()!=expected_samples || source.samples_emitted()!=0 || !source.payload_constellation().empty())
            throw std::runtime_error("raw binary adds training, byte padding or untransmitted points");
        while(source.next_symbol()){}
        std::vector<float> waveform;std::array<float,17> block{};
        while(!pcm.finished()) {const auto count=pcm.read(block);waveform.insert(waveform.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));}
        if(waveform.size()!=expected_samples || pcm.payload_constellation()!=source.payload_constellation() || source.payload_constellation().size()!=symbols)
            throw std::runtime_error("raw binary PCM and integrated symbol streams disagree");
        std::array<float,31> preview{};pcm.preview_last(preview);
        for(std::size_t i=0;i<std::min(preview.size(),waveform.size());++i)
            if(std::abs(preview[preview.size()-1-i]-waveform[waveform.size()-1-i])>1e-5F)
                throw std::runtime_error("raw binary does not use the same carrier/spreading preview");
    }
    modem::Config config;
    for(const auto& [input,expected]:std::array<std::pair<std::uint8_t,Complex>,2>{{{0,{.35,0}},{1,{-.7,0}}}}) {
        modem::StreamingTransmitter source(modem::RawBits{{input}},config);
        const auto first=source.next_symbol();
        if(!first || std::abs(first->value-expected)>1e-12)
            throw std::runtime_error("one raw bit must select distinct phase and amplitude points, without DBPSK or training");
    }
    // Three meaningful bits in a four-bit modem select eight APSK points:
    // both rings and the four cardinal phases, without a fourth zero bit.
    std::vector<Complex> partial_points;
    for(unsigned value=0;value<8;++value) {
        Bytes bits{static_cast<std::uint8_t>((value>>2)&1),static_cast<std::uint8_t>((value>>1)&1),static_cast<std::uint8_t>(value&1)};
        modem::StreamingTransmitter source(modem::RawBits{bits},config);
        partial_points.push_back(source.next_symbol()->value);
    }
    if(std::abs(partial_points[2]-Complex{0,-.35})>1e-12 || std::abs(partial_points[4]-Complex{.7,0})>1e-12)
        throw std::runtime_error("partial raw symbol is zero padded instead of using its actual-width APSK subset");
    for(std::size_t i=0;i<partial_points.size();++i)for(std::size_t j=0;j<i;++j)
        if(std::abs(partial_points[i]-partial_points[j])<1e-12)throw std::runtime_error("partial raw bits map to duplicate constellation points");
    bool rejected=false;try{modem::StreamingTransmitter invalid(modem::RawBits{{0,2,1}},config);}catch(const Error&){rejected=true;}
    if(!rejected)throw std::runtime_error("raw transmitter accepted a non-bit element");
    rejected=false;try{modem::StreamingTransmitter invalid(modem::RawBits{},config);}catch(const Error&){rejected=true;}
    if(!rejected)throw std::runtime_error("raw transmitter accepted an empty transmission");
    config.integration_seconds=3600;config.memory_limit=1024;
    modem::StreamingTransmitter slow(modem::RawBits{{0,0,1}},config,65536+config.spreading_factor*sizeof(int));
    std::size_t observations=0;while(slow.next_symbol())++observations;
    if(slow.total_samples()!=static_cast<std::uint64_t>(config.sample_rate)*3600 || observations>33)
        throw std::runtime_error("raw long-symbol simulation scales work or storage with airtime");
}
void raw_binary_receiver() {
    for(unsigned width=2;width<=6;++width)for(std::size_t count=1;count<=17;++count) {
        modem::Config config;config.constellation_bits=width;config.spreading_factor=3;
        Bytes bits(count);for(std::size_t i=2;i<count;++i)bits[i]=static_cast<std::uint8_t>((i*7+i/3)&1);
        modem::StreamingTransmitter source(modem::RawBits{bits},config);
        modem::BinaryReceiver receiver(config,bits.size());Bytes received;
        while(auto observation=source.next_symbol()) {
            const auto part=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),part.begin(),part.end());
        }
        const auto last=receiver.finish();received.insert(received.end(),last.begin(),last.end());
        if(received!=bits || receiver.bits_received()!=bits.size() || !receiver.finish().empty())
            throw std::runtime_error("raw sample-derived receiver lost leading zeros or actual-width final bits");
        const auto points=receiver.take_payload_constellation();
        if(points.points.size()!=count/width+(count%width!=0) || !receiver.take_payload_constellation().points.empty())
            throw std::runtime_error("raw receiver constellation does not drain actual measured symbols");
        if(receiver.working_bytes()>65536)throw std::runtime_error("raw receiver retained input-sized DSP state");
    }
    modem::Config config;config.integration_seconds=1;
    const auto duration=modem::symbol_sample_count(config);
    const auto decode=[&](Complex point,std::uint64_t samples) {
        modem::BinaryReceiver receiver(config,1);const modem::SymbolObservation observation{point,samples};
        auto result=receiver.push_symbols(std::span(&observation,1));const auto last=receiver.finish();
        result.insert(result.end(),last.begin(),last.end());return result;
    };
    if(decode({.35,0},duration)!=Bytes{0} || decode({-.7,0},duration)!=Bytes{1})
        throw std::runtime_error("raw receiver does not make its decision from observed phase and amplitude");
    if(decode({-.7,0},duration-duration/200)!=Bytes{1} || !decode({-.7,0},duration/2).empty())
        throw std::runtime_error("raw receiver finish mishandles small clock mismatch or fabricates incomplete bits");
    modem::BinaryReceiver measured(config,4);const modem::SymbolObservation off_grid{{.413,.177},duration};
    measured.push_symbols(std::span(&off_grid,1));
    if(std::abs(measured.take_payload_constellation().points.at(0)-off_grid.value)>1e-12)
        throw std::runtime_error("raw constellation snaps received evidence to transmitted ideals");
    std::stop_source stopped;stopped.request_stop();bool interrupted=false;
    try{measured.finish(stopped.get_token());}catch(const Error&){interrupted=true;}
    if(!interrupted)throw std::runtime_error("raw receiver ignored cancellation");
    measured.finish();bool rejected=false;
    try{measured.push_symbols({});}catch(const Error&){rejected=true;}
    if(!rejected)throw std::runtime_error("finished raw receiver accepted more observations");
    config.integration_seconds=3600;config.memory_limit=1024;
    modem::StreamingTransmitter slow(modem::RawBits{{0,0,1}},config);
    modem::BinaryReceiver bounded(config,3,65536);Bytes result;
    while(auto observation=slow.next_symbol()) {const auto part=bounded.push_symbols(std::span(&*observation,1));result.insert(result.end(),part.begin(),part.end());}
    if(result!=Bytes({0,0,1}) || bounded.working_bytes()>65536)
        throw std::runtime_error("hour-long raw receiver grows with duration or loses bits");
    config.integration_seconds=0;
    modem::BinaryReceiver overflow(config,4*(2048+9));
    const modem::SymbolObservation many{{.35,0},modem::symbol_sample_count(config)*(2048+9)};
    if(overflow.push_symbols(std::span(&many,1)).size()!=4*(2048+9))throw std::runtime_error("raw receiver lost bits while draining long observation");
    const auto retained=overflow.take_payload_constellation();
    if(retained.points.size()!=2048 || retained.dropped!=9)throw std::runtime_error("raw receive constellation is not bounded");
    modem::StreamingTransmitter clean(modem::RawBits{Bytes(64)},config);
    modem::BinaryReceiver noisy(config,64);Bytes guesses;std::mt19937_64 random(719);
    while(auto observation=clean.next_symbol()) {
        const auto corrupted=modem::add_awgn(*observation,-100,random);
        const auto part=noisy.push_symbols(std::span(&corrupted,1));guesses.insert(guesses.end(),part.begin(),part.end());
    }
    if(guesses.size()!=64 || guesses==Bytes(64))throw std::runtime_error("raw receiver ignored noisy samples in favor of transmitted bit truth");
}
void received_preamble_evidence() {
    const auto capture=[](modem::Config config,bool pcm,unsigned prefix_mode,std::uint64_t crop=0,std::uint64_t delay=0) {
        Message message;message.id[0]=91;message.data={'t','r','a','i','n'};
        auto wire=modem::preamble(config);const auto packet=encode_packet(message);wire.insert(wire.end(),packet.begin(),packet.end());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,modem::preamble(config));
        if(receiver.diagnostics().preamble_reception)throw std::runtime_error("training reception is known before acquisition");
        const auto training=modem::training_sample_count(config);
        const auto blank=prefix_mode==1?training/2:prefix_mode==2 || prefix_mode==3?training:0;
        std::mt19937_64 random(917);std::normal_distribution<double> noise(0,.4);
        Bytes received;
        const auto retain=[&](Bytes bytes){received.insert(received.end(),bytes.begin(),bytes.end());};
        if(pcm) {
            if(delay)receiver.push(std::vector<float>(static_cast<std::size_t>(delay)));
            std::array<float,317> block{};
            while(!source.finished()) {
                const auto begin=source.samples_emitted(),count=source.read(block);
                for(std::size_t i=0;i<count;++i)if(begin+i<blank)block[i]=prefix_mode==3?static_cast<float>(noise(random)):0;
                if(prefix_mode==4)for(std::size_t i=0;i<count;++i)block[i]+=static_cast<float>(noise(random)*std::sqrt(modem::nominal_signal_power*std::pow(10.,-1.8))/.4);
                const auto skip=static_cast<std::size_t>(std::min<std::uint64_t>(count,crop>begin?crop-begin:0));
                retain(receiver.push(std::span(block).subspan(skip,count-skip)));
            }
        } else {
            if(delay) {const modem::SymbolObservation idle{{},delay};receiver.push_symbols(std::span(&idle,1));}
            Complex training_sum{};
            while(auto observation=source.next_symbol()) {
                const auto begin=source.samples_emitted()-observation->sample_count;
                if(begin<blank)observation->value=prefix_mode==3?Complex{noise(random),noise(random)}:Complex{};
                if(prefix_mode==4)*observation=modem::add_awgn(*observation,18,random);
                if(prefix_mode==5 && source.samples_emitted()<=training) {
                    training_sum+=observation->value*static_cast<double>(observation->sample_count);
                    if(source.samples_emitted()<training)continue;
                    observation=modem::SymbolObservation{training_sum/static_cast<double>(training),training};
                }
                if(source.samples_emitted()<=crop)continue;
                if(begin<crop)observation->sample_count-=crop-begin;
                retain(receiver.push_symbols(std::span(&*observation,1)));
            }
        }
        retain(receiver.finish());
        if(!receiver.synchronized() || received.size()<wire.size() ||
           decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(wire.size()))).message.data!=message.data)
            throw std::runtime_error("training evidence altered successful blind packet reception");
        if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("training evidence exceeds the receiver workspace");
        return receiver.diagnostics().preamble_reception;
    };
    modem::Config config;config.spreading_mode=modem::SpreadingMode::tone;
    for(const bool pcm:{false,true}) {
        for(const unsigned prefix_mode:{0U,1U,2U,3U,4U}) {
            const auto result=capture(config,pcm,prefix_mode);
            if(!result)throw std::runtime_error("ordinary preamble evidence unexpectedly unknown, mode "+std::to_string(prefix_mode)+", pcm "+std::to_string(pcm));
            const auto expected=prefix_mode==0 || prefix_mode==4?1.:prefix_mode==1?.5:0.;
            if(std::abs(result->received_fraction()-expected)>.04)
                throw std::runtime_error("incorrect independently measured preamble fraction "+std::to_string(result->received_fraction())+", mode "+std::to_string(prefix_mode)+", pcm "+std::to_string(pcm));
            if(result->expected_samples!=modem::training_sample_count(config) || result->matched_samples>result->observed_samples || result->observed_samples>result->expected_samples)
                throw std::runtime_error("training coverage counters violate their duration bounds");
        }
        const auto late=capture(config,pcm,0,modem::training_sample_count(config)/2);
        if(!late || std::abs(late->received_fraction()-.5)>.04 || std::abs(static_cast<double>(late->observed_samples)/static_cast<double>(late->expected_samples)-.5)>.04)
            throw std::runtime_error("late capture invented the missing first half of training");
        const auto delayed=capture(config,pcm,0,0,37);
        if(!delayed || delayed->received_fraction()<.96)throw std::runtime_error("silence before a real preamble polluted its reception percentage");
    }
    if(capture(config,false,5))throw std::runtime_error("one coarse mean falsely proved individual training symbols");
    config.integration_seconds=3600;
    const auto slow=capture(config,false,0);
    if(!slow || slow->received_fraction()<.96)throw std::runtime_error("hour-long bootstrap discarded independently recognized training");
    config.integration_seconds=0;
    for(const double magnitude:{1e200,1e308}) {
        modem::StreamingReceiver receiver(config,modem::preamble(config));
        const modem::SymbolObservation huge{{magnitude,magnitude},400};
        try {for(unsigned i=0;i<90;++i)receiver.push_symbols(std::span(&huge,1));}
        catch(const Error&) {} // The decoder may reject an overflowing sum.
        if(receiver.synchronized() || receiver.diagnostics().preamble_reception)
            throw std::runtime_error("huge finite observations manufactured training evidence");
    }
}
void consumable_transmit_constellation() {
    modem::Config config;config.constellation_bits=6;
    auto wire=modem::preamble(config);
    for(unsigned i=0;i<2101;++i)wire.push_back(static_cast<std::uint8_t>(i*79+37));
    modem::StreamingTransmitter source(wire,config),overflow(wire,config),pcm(wire,config);
    std::vector<Complex> observed,pcm_observed,expected;
    auto append=[&](auto& destination,modem::ConstellationBatch batch) {
        if(batch.dropped)throw std::runtime_error("regular constellation drains dropped points");
        destination.insert(destination.end(),batch.points.begin(),batch.points.end());
    };
    while(!source.finished()) {
        source.next_symbol();append(observed,source.take_payload_constellation());
        if(!source.take_payload_constellation().points.empty())throw std::runtime_error("TX constellation drain repeated points");
    }
    for(std::size_t i=0;i<modem::payload_symbol_count(wire.size()-32,config);++i) {
        const auto value=modem::detail::read_bits(std::span(wire).subspan(32),i*config.constellation_bits,config.constellation_bits);
        expected.push_back(modem::detail::mapped(value,config.constellation_bits,{1,0}));
    }
    auto matches=[](const auto& first,const auto& second) {
        if(first.size()!=second.size())return false;
        for(std::size_t i=0;i<first.size();++i)if(std::abs(first[i]-second[i])>1e-12)return false;
        return true;
    };
    if(!matches(observed,expected))throw std::runtime_error("TX frame points do not show transmitted differential decisions");
    std::array<float,317> block{};
    while(!pcm.finished()){pcm.read(block);append(pcm_observed,pcm.take_payload_constellation());}
    if(!matches(pcm_observed,expected))throw std::runtime_error("PCM frame constellation differs from integrated TX");
    while(!overflow.finished())overflow.next_symbol();
    const auto batch=overflow.take_payload_constellation();
    const auto limit=modem::StreamingTransmitter::constellation_history_limit;
    if(batch.dropped!=expected.size()-limit || !matches(batch.points,std::vector<Complex>(expected.end()-limit,expected.end())))
        throw std::runtime_error("TX pending constellation is not bounded with exact loss reporting");
    const auto empty=overflow.take_payload_constellation();
    if(!empty.points.empty() || empty.dropped)throw std::runtime_error("TX consumption did not reset pending counters");
    if(overflow.payload_constellation().size()!=limit)throw std::runtime_error("TX drain discarded legacy diagnostic history");
}
void consumable_receive_constellation() {
    modem::Config config;config.spreading_mode=modem::SpreadingMode::tone;
    Message message;message.kind=MessageKind::file;message.filename="plot.bin";message.data=Bytes(4096,0x73);
    PacketOptions options;options.compression=false;
    auto wire=modem::preamble(config);const auto packet=encode_packet(message,options);wire.insert(wire.end(),packet.begin(),packet.end());
    modem::StreamingTransmitter source(wire,config);
    modem::StreamingReceiver receiver(config,modem::preamble(config));
    const auto rotation=std::polar(2.6,.61);
    std::vector<Complex> observed;
    while(auto observation=source.next_symbol()) {
        observation->value*=rotation;receiver.push_symbols(std::span(&*observation,1));
        const auto batch=receiver.take_payload_constellation();
        if(batch.dropped || (!receiver.synchronized() && !batch.points.empty()))throw std::runtime_error("unlocked timing candidates leaked into payload constellation");
        observed.insert(observed.end(),batch.points.begin(),batch.points.end());
        if(!receiver.take_payload_constellation().points.empty())throw std::runtime_error("RX constellation drain repeated points");
        if(receiver.synchronized())break;
    }
    if(!receiver.synchronized())throw std::runtime_error("rotated RX plot fixture did not acquire");
    const auto history=receiver.diagnostics().constellation;
    if(observed.size()+1!=history.size())throw std::runtime_error("RX acquisition lost or repeated bootstrap constellation points");
    for(std::size_t i=0;i<observed.size();++i)
        if(std::abs(observed[i]-decision_coordinates(history[i+1],history[i]))>1e-12)
            throw std::runtime_error("RX plot retains absolute carrier rotation instead of measured decision coordinates");
    // An off-grid measured sample must remain off-grid; plotting ideal decoded
    // points would conceal actual phase noise and amplitude errors.
    const modem::SymbolObservation off_grid{{.413,.177},modem::symbol_sample_count(config)};
    receiver.push_symbols(std::span(&off_grid,1));
    const auto noisy=receiver.take_payload_constellation();
    const auto latest=receiver.diagnostics().constellation;
    if(noisy.points.size()!=1 || std::abs(noisy.points[0]-decision_coordinates(latest.back(),history.back()))>1e-12)
        throw std::runtime_error("RX display synthesized or repeated the measured off-grid symbol");
    for(unsigned i=0;i<2200;++i)receiver.push_symbols(std::span(&off_grid,1));
    const auto bounded=receiver.take_payload_constellation();
    if(bounded.points.size()!=2048 || bounded.dropped!=152)throw std::runtime_error("RX pending symbols are not bounded with exact overflow counts");
    for(const auto point:bounded.points)
        if(std::abs(point-Complex{std::abs(latest.back()),0})>1e-12)throw std::runtime_error("RX wrapped constellation lost its preceding phase reference");
    const auto empty=receiver.take_payload_constellation();
    if(!empty.points.empty() || empty.dropped)throw std::runtime_error("RX drain retained consumed points or loss counters");
    receiver.push_symbols(std::span(&off_grid,1));receiver.reset();
    const auto reset=receiver.take_payload_constellation();
    if(!reset.points.empty() || reset.dropped || receiver.synchronized())throw std::runtime_error("RX reset retained previous capture points");
    if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("consumable constellation exceeded receiver workspace");
}
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
void provisional_short_reception() {
    for(unsigned bits=2;bits<=6;++bits) {
        modem::Config config;config.constellation_bits=bits;config.spreading_mode=modem::SpreadingMode::tone;
        PacketOptions options;options.fec=FecMode::rs60;options.compression=false;
        Message message;message.id[0]=static_cast<std::uint8_t>(bits);message.data={'e'};
        const auto packet=encode_packet(message,options);
        const auto layout=packet_layout(packet);
        if(layout.header_parity_bytes || layout.body_parity_bytes)throw std::runtime_error("tiny packet contains Reed-Solomon redundancy");
        auto corrupt=packet;corrupt.back()^=1;
        modem::StreamingReceiver receiver(config,modem::preamble(config));
        bool tentative=false;Bytes recovered;
        for(const bool invalid:{true,false}) {
            auto wire=modem::preamble(config);const auto& frame=invalid?corrupt:packet;
            wire.insert(wire.end(),frame.begin(),frame.end());modem::StreamingTransmitter source(wire,config);
            while(auto point=source.next_symbol()) {
                if(source.samples_emitted()<=modem::training_sample_count(config))point->value={};
                point->value*=std::polar(.61,.73);
                const auto bytes=receiver.push_symbols(std::span(&*point,1));
                const auto dots=receiver.take_payload_constellation();
                tentative=tentative || (receiver.acquiring() && !receiver.synchronized() && !dots.points.empty());
                if(invalid && (!bytes.empty() || receiver.synchronized()))throw std::runtime_error("CRC-only header committed an invalid tiny packet");
                recovered.insert(recovered.end(),bytes.begin(),bytes.end());
                if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("provisional receivers exceeded workspace");
            }
        }
        const auto tail=receiver.finish();recovered.insert(recovered.end(),tail.begin(),tail.end());
        auto expected=modem::preamble(config);expected.insert(expected.end(),packet.begin(),packet.end());
        if(!tentative || recovered!=expected || !receiver.synchronized())throw std::runtime_error("invalid short frame prevented subsequent verified acquisition at "+std::to_string(bits)+" bits");
        if(!receiver.finish().empty())throw std::runtime_error("short-packet finish repeated frame bytes");
    }
    for(unsigned bits=2;bits<=6;++bits) {
        modem::Config config;config.constellation_bits=bits;config.spreading_mode=modem::SpreadingMode::tone;
        modem::StreamingReceiver receiver(config,modem::preamble(config));
        std::mt19937_64 random(819+bits);std::normal_distribution<float> noise(0,.12F);
        std::array<float,317> samples{};
        for(unsigned chunk=0;chunk<200;++chunk) {
            for(auto& sample:samples)sample=noise(random);
            if(!receiver.push(samples).empty() || receiver.synchronized())throw std::runtime_error("noise-only input acquired a packet at "+std::to_string(bits)+" bits");
            if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("noise acquisition exceeded bounded workspace");
        }
        if(!receiver.finish().empty() || receiver.synchronized())throw std::runtime_error("noise finish invented a packet");
    }
    for(const unsigned bits:{2U,6U}) {
        modem::Config config;config.constellation_bits=bits;config.spreading_mode=modem::SpreadingMode::tone;
        PacketOptions options;options.fec=FecMode::off;options.compression=false;
        Message message;message.id[0]=21;message.data=Bytes(4096,0x73);
        const auto packet=encode_packet(message,options);
        auto wire=modem::preamble(config);wire.insert(wire.end(),packet.begin(),packet.end());
        modem::StreamingTransmitter source(wire,config);modem::StreamingReceiver receiver(config,modem::preamble(config));
        while(auto point=source.next_symbol()) {
            receiver.push_symbols(std::span(&*point,1));
            if(receiver.acquiring() && !receiver.synchronized())break;
        }
        if(!receiver.acquiring() || receiver.synchronized())throw std::runtime_error("long-observation fixture missed provisional large-header selection");
        const modem::SymbolObservation enormous{{.35,0},std::uint64_t{1}<<42};
        const auto start=std::chrono::steady_clock::now();
        const auto bytes=receiver.push_symbols(std::span(&enormous,1));
        if(!receiver.synchronized() || bytes.size()>wire.size() || receiver.working_bytes()>8*1024*1024 ||
           std::chrono::steady_clock::now()-start>std::chrono::seconds(2))
            throw std::runtime_error("deferred large-header observation grew with virtual duration");
    }
}
void exact_pcm_boundaries() {
    // Ten- and nine-sample chips are deliberately not multiples of a four
    // sample I/Q integration quantum. Dense symbols must retain their exact
    // chip and symbol boundaries, including across arbitrary input chunks.
    for(const double bandwidth:{1200.,1499.,1499.25,1703.})
        for(unsigned bits=2;bits<=6;++bits)for(unsigned mode=0;mode<2;++mode) {
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
            modem::StreamingReceiver receiver(config,Bytes(wire.begin(),wire.begin()+32),8*1024*1024,[&](const Bytes& prefix)->std::optional<std::size_t>{
                // Isolate exact PCM boundaries from blind acquisition's
                // training-edge aliases, whose first two symbol decisions
                // can need bootstrap FEC. Other tests exercise that policy.
                if(prefix.size()!=packet_prefix_size)return {};
                for(std::size_t i=0;i<prefix.size();++i) {
                    const unsigned reference_bits=(delay || mode) && i==0?((1U<<modem::detail::phase_bits(bits))-1)<<(8-bits):0;
                    if(((prefix[i]^wire[32+i])&~reference_bits)!=0)return {};
                }
                return frame.size();
            },[&](const Bytes& packet){return packet==Bytes(wire.begin()+32,wire.end());});
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
                const unsigned reference_bits=(delay || mode) && i==32?((1U<<modem::detail::phase_bits(bits))-1)<<(8-bits):0;
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
    Message message;message.id[0]=0x71;message.data=Bytes(4096,0x73);
    PacketOptions options;options.compression=false;
    auto wire=modem::preamble(config);const auto frame=encode_packet(message,options);wire.insert(wire.end(),frame.begin(),frame.end());
    modem::StreamingTransmitter source(wire,config);modem::StreamingReceiver receiver(config,modem::preamble(config));
    while(const auto observation=source.next_symbol()) {receiver.push_symbols(std::span(&*observation,1));if(receiver.synchronized())break;}
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
    modem::Config config;config.sample_rate=6000;config.constellation_bits=2;config.spreading_factor=1024;
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
    const auto mask=key.stream(StreamPurpose::Data,epoch,32,frame.size());
    modem::StreamingTransmitter source(wire,config);
    const auto bootstrap=[mask](const Bytes& prefix){
        auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
        return packet_probe_frame_size(header);
    };
    modem::StreamingReceiver receiver(config,expected,8*1024*1024,bootstrap,masked_packet_validator(mask,options));
    auto wrong_config=config;wrong_config.spreading_seed[0]^=0x55;
    modem::StreamingReceiver wrong_code(wrong_config,expected,8*1024*1024,bootstrap,masked_packet_validator(mask,options));
    modem::StreamingReceiver noise_only(config,expected,8*1024*1024,bootstrap,masked_packet_validator(mask,options));
    // Neither the symbol start nor the receive carrier phase is supplied.
    // At 1500/6000 Hz, this delay rotates the carrier reference by 90 degrees.
    receiver.push(std::array<float,17>{});
    wrong_code.push(std::array<float,17>{});noise_only.push(std::array<float,17>{});
    std::array<float,317> block{},noise_block{};Bytes received;
    std::mt19937_64 random(8192);
    constexpr double sample_snr_db=-15;
    std::normal_distribution<float> noise(0,static_cast<float>(std::sqrt(modem::nominal_signal_power*std::pow(10.,-sample_snr_db/10))));
    double signal_energy=0,noise_energy=0;
    while(!source.finished()) {
        const auto begin=source.samples_emitted();
        const auto count=source.read(block);
        // Training provides no acquisition assistance for this fixture.
        for(std::size_t i=0;i<count;++i) {
            noise_block[i]=noise(random);
            if(begin+i<modem::training_sample_count(config))block[i]=0;
            else {
                signal_energy+=static_cast<double>(block[i])*block[i];
                noise_energy+=static_cast<double>(noise_block[i])*noise_block[i];
            }
            block[i]+=noise_block[i];
        }
        const auto bytes=receiver.push(std::span(block).first(count));received.insert(received.end(),bytes.begin(),bytes.end());
        if(!wrong_code.push(std::span(block).first(count)).empty() || wrong_code.synchronized())
            throw std::runtime_error("wrong spreading code acquired a weak PCM packet");
        if(!noise_only.push(std::span(noise_block).first(count)).empty() || noise_only.synchronized())
            throw std::runtime_error("pure PCM noise acquired a weak packet");
        if(receiver.working_bytes()>8*1024*1024)throw std::runtime_error("long keyed PCM exceeded workspace");
    }
    const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
    if(received.size()<plain.size())throw std::runtime_error("delayed long keyed PCM failed blind acquisition");
    received=key.xor_data(received,epoch);
    const auto packet=decode_packet(Bytes(received.begin()+32,received.end()),options);
    if(packet.message.data!=message.data || !packet.authenticated)throw std::runtime_error("long keyed PCM changed authenticated bytes");
    if(!wrong_code.finish().empty() || wrong_code.synchronized() || !noise_only.finish().empty() || noise_only.synchronized())
        throw std::runtime_error("weak PCM finish invented a wrong-code/noise packet");
    // The 10-sample chip has orthogonal quadratures at this carrier. AWGN
    // projection then gives chip Es/N0 = sample SNR + 10 log10(chip/2).
    // Check the measured signal/noise energy, not just the requested setting:
    // individual chip decisions are below noise while the full code acquires.
    const auto chip=std::ceil(2.*config.sample_rate/config.bandwidth_hz);
    const auto chip_esn0_db=10*std::log10(signal_energy/noise_energy)+10*std::log10(chip/2);
    if(!std::isfinite(chip_esn0_db) || chip_esn0_db>=-7)
        throw std::runtime_error("weak PCM acquisition fixture is not below chip noise");
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
        // Corrupt only the actual header correction allowance. Small headers
        // receive proportional parity; FEC-off headers receive no parity.
        const auto correctable=packet_layout(frame).header_parity_bytes/2;
        if(trial>=8)for(std::size_t i=0;i<correctable;++i)frame[i]^=static_cast<std::uint8_t>(91+i);
        auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
        const Crypto key(Bytes(32,0x53));constexpr std::uint64_t epoch=1800000000;
        const auto wire=trial?key.xor_data(plain,epoch):plain;
        const Bytes expected(wire.begin(),wire.begin()+32);
        const auto mask=key.stream(StreamPurpose::Data,epoch,32,frame.size());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,expected,8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;if(trial)for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            return packet_probe_frame_size(header);
        },masked_packet_validator(trial?mask:Bytes{},options));
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
        if(received.size()<plain.size())throw std::runtime_error("adaptive "+std::to_string(bits)+"-bit acquisition failed in trial "+std::to_string(trial));
        if(trial)received=key.xor_data(received,epoch);
        const auto result=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(plain.size())),options);
        if(result.message.data!=message.data)throw std::runtime_error("adaptive constellation changed payload");
        const auto expected_samples=modem::training_sample_count(config)+modem::payload_symbol_count(frame.size(),config)*modem::symbol_sample_count(config);
        if(source.total_samples()!=expected_samples)throw std::runtime_error("adaptive symbol padding changed estimated airtime");
    }
}
void two_ring_gain_aliases() {
    // A short whitened header need not populate both amplitude rings evenly.
    // Force valid XOR-stream fixtures with only one ring, and with a minority
    // of either ring. Header validation must resolve gain aliases without
    // assuming a balanced population or a known transmitter amplitude.
    for(unsigned bits=2;bits<=4;++bits)for(unsigned population=0;population<4;++population)
        for(const double gain:{.62,1.35}) {
        modem::Config config;config.constellation_bits=bits;config.spreading_factor=32;
        config.spreading_mode=modem::SpreadingMode::tone;
        PacketOptions options;options.fec=FecMode::off;options.compression=false;
        Message message;message.id[0]=static_cast<std::uint8_t>(bits);message.data={0,1,0x35};
        const auto frame=encode_packet(message,options);Bytes coded(frame.begin(),frame.begin()+packet_prefix_size);
        const auto symbols=(packet_prefix_size*8+bits-1)/bits;
        for(std::size_t symbol=0;symbol<symbols;++symbol) {
            const bool outer=population==0 || (population==2 && symbol%8!=0) || (population==3 && symbol%8==0);
            const auto bit=symbol*bits;
            coded[bit/8]=static_cast<std::uint8_t>((coded[bit/8]&~(1U<<(7-bit%8)))|(static_cast<unsigned>(outer)<<(7-bit%8)));
        }
        Bytes mask(packet_prefix_size);
        for(std::size_t i=0;i<mask.size();++i)mask[i]=coded[i]^frame[i];
        auto wire=modem::preamble(config);wire.insert(wire.end(),coded.begin(),coded.end());wire.insert(wire.end(),frame.begin()+packet_prefix_size,frame.end());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,modem::preamble(config),8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            return packet_probe_frame_size(header);
        },masked_packet_validator(mask,options));
        Bytes received;std::mt19937_64 random(0xabc+bits*17+population);
        const auto sample_snr=tuning::constellation_target_symbol_snr_db(bits)+12-10*std::log10(modem::symbol_seconds(config)*config.sample_rate/2);
        while(auto observation=source.next_symbol()) {
            if(source.samples_emitted()<=modem::training_sample_count(config))observation->value={};
            *observation=modem::add_awgn(*observation,sample_snr,random);observation->value*=gain;
            const auto bytes=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),bytes.begin(),bytes.end());
        }
        const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
        const auto context=" at "+std::to_string(bits)+" bits, ring population "+std::to_string(population)+", gain "+std::to_string(gain);
        if(received.size()<wire.size())throw std::runtime_error("two-ring gain alias prevented short header acquisition"+context);
        for(std::size_t i=0;i<mask.size();++i)received[32+i]^=mask[i];
        if(decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(wire.size())),options).message.data!=message.data)
            throw std::runtime_error("two-ring bootstrap chose an aliased gain"+context);
    }
}
void short_noisy_bootstraps() {
    // Even a one-byte status message must acquire without preamble evidence.
    // Exercise every payload width and body-FEC choice, both plain and
    // whitened, with an unknown gain and a non-symbol-aligned receive start.
    for(unsigned bits=2;bits<=6;++bits)for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})
        for(const bool encrypted:{false,true}) {
        modem::Config config;config.constellation_bits=bits;config.spreading_factor=32;
        config.spreading_mode=modem::SpreadingMode::tone;
        PacketOptions options;options.fec=fec;options.compression=false;
        Message message;message.id[0]=static_cast<std::uint8_t>(bits);message.data={0x65};
        const auto frame=encode_packet(message,options);
        auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
        const Crypto key(Bytes(32,0x53));constexpr std::uint64_t epoch=1800000000;
        const auto wire=encrypted?key.xor_data(plain,epoch):plain;
        const auto mask=key.stream(StreamPurpose::Data,epoch,32,frame.size());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,Bytes(wire.begin(),wire.begin()+32),8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;if(encrypted)for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            return packet_probe_frame_size(header);
        },masked_packet_validator(encrypted?mask:Bytes{},options));
        const modem::SymbolObservation delay{{},17};receiver.push_symbols(std::span(&delay,1));
        Bytes received;std::mt19937_64 random(0xdef+bits*19+static_cast<unsigned>(fec)*7+static_cast<unsigned>(encrypted));
        const auto sample_snr=tuning::constellation_target_symbol_snr_db(bits)+6-10*std::log10(modem::symbol_seconds(config)*config.sample_rate/2);
        while(auto observation=source.next_symbol()) {
            if(source.samples_emitted()<=modem::training_sample_count(config))observation->value={};
            *observation=modem::add_awgn(*observation,sample_snr,random);observation->value*=encrypted?.43:1.2;
            const auto bytes=receiver.push_symbols(std::span(&*observation,1));received.insert(received.end(),bytes.begin(),bytes.end());
        }
        const auto tail=receiver.finish();received.insert(received.end(),tail.begin(),tail.end());
        const auto context=" at "+std::to_string(bits)+" bits, FEC "+std::to_string(static_cast<unsigned>(fec))+", encrypted "+std::to_string(encrypted);
        if(received.size()<plain.size())throw std::runtime_error("short noisy bootstrap did not acquire"+context);
        if(encrypted)received=key.xor_data(received,epoch);
        try {
            const auto packet=decode_packet(Bytes(received.begin()+32,received.begin()+static_cast<std::ptrdiff_t>(plain.size())),options);
            if(packet.message.data!=message.data)throw std::runtime_error("short noisy bootstrap changed data"+context);
        } catch(const Error& error) {throw std::runtime_error(std::string(error.what())+context);}
    }
}
void dense_missing_outer_ring() {
    // Find a deterministic actual AES epoch stream whose short protected
    // bootstrap occupies only rings1..7. The complete64APSK constellation
    // has eight rings; gain acquisition must still succeed without ring8.
    modem::Config config;config.constellation_bits=6;config.spreading_mode=modem::SpreadingMode::tone;
    Message message;message.id[0]=81;
    for(unsigned i=0;i<29;++i)message.data.push_back(static_cast<std::uint8_t>(i*17+91));
    PacketOptions options;options.fec=FecMode::off;
    const auto frame=encode_packet(message,options);
    auto plain=modem::preamble(config);plain.insert(plain.end(),frame.begin(),frame.end());
    const Crypto key(Bytes(32,0x53));std::uint64_t epoch=1801378970;
    Bytes wire;unsigned maximum=0;
    for(unsigned attempt=0;attempt<1024;++attempt,++epoch) {
        wire=key.xor_data(plain,epoch);
        const auto encoded_header=std::span(wire).subspan(32,packet_prefix_size);maximum=0;
        for(std::size_t symbol=0;symbol<(packet_prefix_size*8+5)/6;++symbol)
            maximum=std::max(maximum,1+modem::detail::gray_decode(modem::detail::read_bits(encoded_header,symbol*6,6)>>3));
        if(maximum==7)break;
    }
    if(maximum!=7)throw std::runtime_error("missing outer-ring fixture no longer exercises the intended lattice");
    const Bytes expected(wire.begin(),wire.begin()+32);
    const auto mask=key.stream(StreamPurpose::Data,epoch,32,frame.size());
    modem::StreamingTransmitter source(wire,config);
    modem::StreamingReceiver receiver(config,expected,8*1024*1024,[&](const Bytes& prefix){
        auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
        return packet_probe_frame_size(header);
    },masked_packet_validator(mask,options));
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
        for(std::size_t symbol=0;symbol<(packet_prefix_size*8+5)/6;++symbol) {
            auto value=modem::detail::read_bits(coded,symbol*6,6);
            const auto ring=modem::detail::gray_decode(value>>3)%maximum;
            value=(value&7)|((ring^(ring>>1))<<3);
            // The final partial symbol has zero padding supplied by the
            // modem. Use the inner ring rather than writing beyond the
            // protected bytes or depending on unavailable padding bits.
            if(symbol*6+6>packet_prefix_size*8)value=0;
            for(unsigned position=0;position<6;++position) {
                const auto bit=symbol*6+position;
                if(bit>=packet_prefix_size*8)break;
                coded[bit/8]=static_cast<std::uint8_t>((coded[bit/8]&~(1U<<(7-bit%8)))|(((value>>(5-position))&1U)<<(7-bit%8)));
            }
        }
        Bytes mask(packet_prefix_size);
        for(std::size_t i=0;i<mask.size();++i)mask[i]=coded[i]^frame[i];
        auto wire=modem::preamble(config);wire.insert(wire.end(),coded.begin(),coded.end());wire.insert(wire.end(),frame.begin()+packet_prefix_size,frame.end());
        modem::StreamingTransmitter source(wire,config);
        modem::StreamingReceiver receiver(config,modem::preamble(config),8*1024*1024,[&](const Bytes& prefix){
            auto header=prefix;for(std::size_t i=0;i<header.size();++i)header[i]^=mask[i];
            return packet_probe_frame_size(header);
        },masked_packet_validator(mask,options));
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
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string_view(argv[1])=="--pattern-only") {
            long_keyed_pcm();std::cout<<"below-chip-noise PCM pattern acquisition tests passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--provisional-only") {
            provisional_short_reception();std::cout<<"provisional packet tests passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--gain-only") {
            two_ring_gain_aliases();std::cout<<"two-ring gain tests passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--bootstrap-only") {
            provisional_short_reception();
            two_ring_gain_aliases();short_noisy_bootstraps();
            adaptive_roundtrips();dense_missing_outer_ring();dense_gain_aliases();exact_pcm_boundaries();
            std::cout<<"compact bootstrap tests passed\n";return 0;
        }
        raw_binary_transmitter();
        raw_binary_receiver();
        if(argc>1 && std::string_view(argv[1])=="--binary-only") {std::cout<<"raw binary modem tests passed\n";return 0;}
        received_preamble_evidence();
        if(argc>1 && std::string_view(argv[1])=="--preamble-only") {std::cout<<"preamble evidence tests passed\n";return 0;}
        provisional_short_reception();
        two_ring_gain_aliases();
        short_noisy_bootstraps();
        consumable_transmit_constellation();
        consumable_receive_constellation();
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
            [](const Bytes& prefix){return packet_probe_frame_size(prefix);});
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
