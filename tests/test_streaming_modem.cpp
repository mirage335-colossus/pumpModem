#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace datapump;
using Complex=std::complex<double>;

void exact_suppression_noise() {
    for(const auto mode:{modem::SpreadingMode::pattern,modem::SpreadingMode::tone})
    for(const bool shaped:{false,true})for(const bool keyed:{false,true}) {
        if(mode==modem::SpreadingMode::tone && keyed)continue;
        modem::Config config;
        config.sample_rate=512;config.carrier_hz=128;config.bandwidth_hz=128;
        config.integration_seconds=1;config.spreading_mode=mode;config.pulse_shaping=shaped;
        config.scramble=keyed;config.dsss=keyed;config.spreading_seed[0]=13;config.dsss_seed[0]=29;
        config.stream_epoch=1800000031;
        if(keyed)config.data_key.emplace(Bytes(32,0x57));
        const Bytes wire{0xa5},bits{1,0,1,0,0,1,0,1};
        modem::StreamingTransmitter packed(wire,config),raw(modem::RawBits{bits},config);
        const auto training=modem::training_sample_count(config),padding=modem::pattern_pulse_padding_samples(config);
        const auto noise=modem::suppression_sample_count(config);
        const auto content=training+2*padding+bits.size()*modem::symbol_sample_count(config);
        if(noise!=2*config.sample_rate || packed.total_samples()!=content+noise || raw.total_samples()!=content+noise ||
           modem::waveform_sample_count(wire.size(),config)!=content+noise)
            throw std::runtime_error("all nonempty input forms need exactly two seconds of trailing noise");
        std::vector<Complex> a(packed.total_samples()),b(a.size());
        packed.read_analytic(a);raw.read_analytic(b);
        if(a!=b || a[content]!=Complex{} || a.back()!=Complex{})
            throw std::runtime_error("raw and byte inputs must share tapered suppression noise");
        double energy=0;Complex circularity{};
        for(std::size_t i=content;i<a.size();++i) {
            const auto value=a[i]*std::polar(1.,-2*std::numbers::pi*i*config.carrier_hz/config.sample_rate);
            energy+=std::norm(value);circularity+=value*value;
            if(std::norm(value)>=1)throw std::runtime_error("suppression noise exceeded PCM headroom");
        }
        if(energy/noise<modem::nominal_signal_power || energy/noise>3*modem::nominal_signal_power ||
           std::abs(circularity)/energy>.4)
            throw std::runtime_error("suppression must contain independent circular noise at useful power");
        modem::PatternTransmitter bare(bits,config,config.stream_epoch,0,false);
        std::vector<Complex> reference(bare.total_samples());bare.read_analytic(reference);
        const auto rotation=std::polar(1.,2*std::numbers::pi*training*config.carrier_hz/config.sample_rate);
        for(std::size_t i=2*padding;i<reference.size();++i)
            if(std::abs(a[training+i]-reference[i]*rotation)>1e-9)
                throw std::runtime_error("surrounding noise changed payload or its final filter samples");
        if(std::equal(a.begin()+static_cast<std::ptrdiff_t>(content),a.end(),a.begin()))
            throw std::runtime_error("suppression reused the preamble waveform");
    }
    modem::Config long_config;
    long_config.sample_rate=64;long_config.carrier_hz=16;long_config.bandwidth_hz=32;
    long_config.integration_seconds=3600;long_config.pulse_shaping=false;
    modem::PatternTransmitter long_source({0},long_config);
    const auto content=modem::symbol_sample_count(long_config);
    if(modem::training_sample_count(long_config) || long_source.total_samples()!=content+128 ||
       long_source.working_bytes()>16384)
        throw std::runtime_error("hour-long symbols must retain a two-second tail with bounded state");
    std::array<Complex,317> block{};
    while(long_source.samples_emitted()<content) {
        const auto count=std::min<std::uint64_t>(block.size(),content-long_source.samples_emitted());
        long_source.read_analytic(std::span(block).first(static_cast<std::size_t>(count)));
    }
    const auto count=long_source.read_analytic(block);
    if(count!=128 || !long_source.finished() ||
       std::none_of(block.begin(),block.begin()+128,[](auto value){return value!=Complex{};}))
        throw std::runtime_error("long-symbol tail was rounded, omitted or replaced by silence");
}

void suppression_hides_delayed_echo() {
    modem::Config config;
    config.sample_rate=512;config.carrier_hz=128;config.bandwidth_hz=128;
    config.integration_seconds=1;config.pulse_shaping=false;
    const Bytes bits{1,0,0,1,1,0,1,0};
    modem::StreamingTransmitter source(modem::RawBits{bits},config);
    const auto training=modem::training_sample_count(config);
    std::vector<float> audio(source.total_samples());source.read(audio);
    // A weaker copy delayed by the complete two-second guard. Supply actual
    // received samples through the echo's end and the six-second absence.
    const std::size_t delay=2*config.sample_rate;
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.initial_stream_symbols=1;
    for(const bool suppress:{false,true}) {
        std::vector<float> received(audio.size()-training+delay+7*config.sample_rate);
        const auto emitted=suppress?audio.size():audio.size()-modem::suppression_sample_count(config);
        for(std::size_t i=0;i<emitted;++i) {
            if(i>=training)received[i-training]+=audio[i];
            if(i+delay>=training)received[i-training+delay]+=.2F*audio[i];
        }
        modem::PatternReceiver receiver(config,8*1024*1024,search);
        Bytes decoded;bool complete=false;
        const auto harvest=[&] {
            for(const auto& burst:receiver.take_bursts()) {
                if(burst.bits.size()>decoded.size())decoded=burst.bits;
                complete=complete || burst.complete;
            }
        };
        for(std::size_t offset=0;offset<received.size();) {
            const auto count=std::min<std::size_t>(317,received.size()-offset);
            receiver.push(std::span(received).subspan(offset,count));offset+=count;harvest();
        }
        receiver.finish();harvest();
        if(suppress?(decoded!=bits || !complete):(decoded.size()<=bits.size()))
            throw std::runtime_error(suppress?"two-second delayed echo or suppression noise added decoded payload bits":
                "echo control must expose delayed symbols without suppression noise");
    }
}

void pattern_transmit_constellation() {
    for(const auto mode:{modem::SpreadingMode::pattern,modem::SpreadingMode::tone})
    for(unsigned layers=0;layers<(mode==modem::SpreadingMode::pattern?4U:1U);++layers)
    for(const bool raw:{false,true}) {
        modem::Config config;
        config.spreading_mode=mode;config.dsss=(layers&2U)!=0;config.dsss_seed[0]=73;
        config.scramble=(layers&1U)!=0;
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
                if(position<training+bits.size()*symbol && (position-training)%symbol%chip==0) {
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

void frame_wide_transmit_constellation() {
    modem::Config config;
    config.sample_rate=1200003;config.bandwidth_hz=300000;config.carrier_hz=450000;
    config.integration_seconds=11;config.scramble=true;config.spreading_seed[0]=37;
    // The rounded hardware prefix is empty for this long-symbol fixture, so
    // it can exercise wideband history without rendering two seconds of PCM.
    if(modem::training_sample_count(config))throw std::runtime_error("frame history fixture unexpectedly has a preamble");
    const auto frame=config.sample_rate/modem::StreamingTransmitter::constellation_frame_rate+
        (config.sample_rate%modem::StreamingTransmitter::constellation_frame_rate!=0);
    const auto chip=modem::pattern_chip_samples(config);
    const auto capacity=modem::StreamingTransmitter::constellation_history_capacity(config);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    const auto endpoint=padding+4*frame+chip/2;
    const auto total_chips=(endpoint-padding)/chip+((endpoint-padding)%chip!=0);
    const auto first_frame_chip=(endpoint-padding-frame)/chip;
    if(total_chips-first_frame_chip<=modem::StreamingTransmitter::constellation_history_limit)
        throw std::runtime_error("wideband fixture must exceed the old chip history");
    modem::StreamingTransmitter fragmented(modem::RawBits{Bytes{0,1}},config),whole(modem::RawBits{Bytes{0,1}},config);
    std::vector<float> pcm(endpoint);
    if(whole.read(pcm)!=pcm.size())throw std::runtime_error("frame history PCM fixture ended early");
    modem::PatternCode code(config);
    std::vector<Complex> expected;
    for(std::uint64_t i=0;i<total_chips;++i)
        expected.push_back(std::sqrt(2*modem::nominal_signal_power)*code.value(i,0));
    std::array<Complex,317> output{};
    constexpr std::array<std::size_t,5> requests{1,3,317,10,71};
    std::size_t iteration=0,observed=0;
    while(fragmented.samples_emitted()<endpoint) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(requests[iteration++%requests.size()],
            endpoint-fragmented.samples_emitted()));
        fragmented.read_analytic(std::span(output).first(count));
        const auto batch=fragmented.take_payload_constellation();
        if(batch.dropped || observed+batch.points.size()>expected.size() ||
           !std::equal(batch.points.begin(),batch.points.end(),expected.begin()+static_cast<std::ptrdiff_t>(observed)))
            throw std::runtime_error("wideband callback fragmentation lost or repeated chip observations");
        observed+=batch.points.size();
    }
    const auto history=whole.payload_constellation();
    if(observed!=expected.size() || history.size()!=capacity || history!=fragmented.payload_constellation() ||
       !std::equal(history.begin(),history.end(),expected.end()-static_cast<std::ptrdiff_t>(capacity)))
        throw std::runtime_error("wideband history did not preserve its chronological frame capacity");
    const auto frame_points=static_cast<std::size_t>(total_chips-first_frame_chip);
    if(frame_points>history.size() || !std::equal(history.end()-static_cast<std::ptrdiff_t>(frame_points),history.end(),
                                               expected.begin()+static_cast<std::ptrdiff_t>(first_frame_chip)))
        throw std::runtime_error("wideband history lost the chip overlapping the start of the bitmap frame");
    const auto pending=whole.take_payload_constellation();
    if(pending.points!=history || pending.dropped!=expected.size()-capacity || !whole.take_payload_constellation().points.empty())
        throw std::runtime_error("wideband bounded history reported incorrect omitted points");
    const auto bytes=whole.working_bytes();
    if(bytes<capacity*sizeof(Complex))throw std::runtime_error("wideband history allocation is absent from DSP accounting");
    modem::StreamingTransmitter exact(modem::RawBits{Bytes{0,1}},config,bytes);
    bool rejected=false;
    try{modem::StreamingTransmitter too_small(modem::RawBits{Bytes{0,1}},config,bytes-1);}
    catch(const Error&){rejected=true;}
    if(!rejected || exact.working_bytes()!=bytes)throw std::runtime_error("wideband history did not enforce its exact workspace budget");

    // Short final chips can put more physical points in a frame than a
    // bandwidth-only estimate. Check every possible frame boundary phase.
    config.integration_seconds=10.5/config.sample_rate;
    const auto symbol=modem::symbol_sample_count(config),chips=modem::pattern_chips_per_symbol(config);
    const auto partial_capacity=modem::StreamingTransmitter::constellation_history_capacity(config);
    if(symbol%chip==0)throw std::runtime_error("frame boundary fixture needs shortened final chips");
    for(std::uint64_t start=0;start<symbol;++start) {
        const auto end=start+frame;
        const auto first=start/symbol*chips+start%symbol/chip;
        const auto last=end/symbol*chips+(end%symbol)/chip+((end%symbol)%chip!=0);
        if(last-first>partial_capacity)throw std::runtime_error("short final chips exceed the reserved bitmap frame history");
    }
}

int main() {
    try {
        exact_suppression_noise();
        suppression_hides_delayed_echo();
        pattern_transmit_constellation();
        frame_wide_transmit_constellation();
        modem::Config config;config.memory_limit=1024; // Batch PCM ceiling does not limit explicit streaming DSP.
        const Bytes bits{0,0,1,1,0,1,0,1,1};
        modem::StreamingTransmitter source(modem::RawBits{bits},config);
        if(source.total_samples()!=modem::training_sample_count(config)+2*modem::pattern_pulse_padding_samples(config)+
            bits.size()*modem::symbol_sample_count(config)+modem::suppression_sample_count(config))
            throw std::runtime_error("raw waveform length differs from exact bits, settling and pulse tails");
        bool rejected=false;try{source.next_symbol();}catch(const Error&){rejected=true;}
        if(!rejected)throw std::runtime_error("pattern transmitter exposed symbol oracle");
        std::vector<float> settling(modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config));source.read(settling);
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
