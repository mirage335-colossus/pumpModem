#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace datapump::modem {
namespace {
using Complex=std::complex<double>;
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("modem operation cancelled");}
}

struct StreamingTransmitter::Impl {
    std::unique_ptr<PatternTransmitter> pattern;
    std::vector<Complex> constellation;
    std::size_t constellation_begin=0,constellation_count=0,pending_count=0;
    std::uint64_t dropped=0;
    std::size_t initialize_constellation(const Config& config,std::size_t workspace) {
        const auto count=StreamingTransmitter::constellation_history_capacity(config);
        const auto fixed=sizeof(StreamingTransmitter)+sizeof(Impl);
        if(fixed>workspace || count>(workspace-fixed)/sizeof(Complex))
            throw Error("pattern transmitter workspace is too small");
        constellation.resize(count);
        const auto bytes=fixed+constellation.capacity()*sizeof(Complex);
        if(bytes>workspace)throw Error("pattern transmitter workspace is too small");
        return workspace-bytes;
    }
    Impl(Bytes wire,Config config,std::size_t workspace) {
        validate(config);
        const auto remaining=initialize_constellation(config,workspace);
        if(wire.size()>std::numeric_limits<std::size_t>::max()/8 ||
           wire.capacity()>remaining || wire.size()>(remaining-wire.capacity())/8)
            throw Error("pattern transmitter workspace is too small");
        config.memory_limit=remaining;
        Bytes bits;bits.reserve(wire.size()*8);
        for(const auto byte:wire)for(unsigned bit=0;bit<8;++bit)
            bits.push_back(static_cast<std::uint8_t>((byte>>(7-bit))&1U));
        pattern=std::make_unique<PatternTransmitter>(std::move(bits),config,config.stream_epoch);
        if(pattern->working_bytes()>remaining)throw Error("pattern transmitter workspace is too small");
    }
    Impl(RawBits input,Config config,std::size_t workspace) {
        validate(config);
        if(input.bits.empty())throw Error("raw binary transmission requires at least one bit");
        const auto remaining=initialize_constellation(config,workspace);
        if(input.bits.capacity()>remaining)throw Error("pattern transmitter workspace is too small");
        config.memory_limit=remaining;
        pattern=std::make_unique<PatternTransmitter>(std::move(input.bits),config,config.stream_epoch);
        if(pattern->working_bytes()>remaining)throw Error("pattern transmitter workspace is too small");
    }
    void retain_constellation(Complex value) {
        if(constellation_count==constellation.size()) {
            constellation_begin=(constellation_begin+1)%constellation.size();--constellation_count;
        }
        constellation[(constellation_begin+constellation_count++)%constellation.size()]=value;
        if(pending_count<constellation.size())++pending_count;
        else if(dropped<std::numeric_limits<std::uint64_t>::max())++dropped;
    }
};
std::size_t StreamingTransmitter::constellation_history_capacity(const Config& config) {
    const auto chip=pattern_chip_samples(config),symbol=symbol_sample_count(config);
    const auto chips=symbol/chip+(symbol%chip!=0);
    const auto samples=config.sample_rate/constellation_frame_rate+(config.sample_rate%constellation_frame_rate!=0);
    const auto remainder=samples%symbol;
    // Every complete symbol contributes all of its chips. A leftover span
    // can straddle a shortened final chip, and its first chip may have begun
    // before the frame. Keep both boundary observations as well.
    const auto partial=remainder?std::min(chips,remainder/chip+(remainder%chip!=0)+1):0;
    return std::max(constellation_history_limit,static_cast<std::size_t>((samples/symbol)*chips+partial+1));
}
StreamingTransmitter::StreamingTransmitter(Bytes wire,Config c,std::size_t workspace):impl_(std::make_unique<Impl>(std::move(wire),c,workspace)){}
StreamingTransmitter::StreamingTransmitter(RawBits bits,Config c,std::size_t workspace):impl_(std::make_unique<Impl>(std::move(bits),c,workspace)){}
StreamingTransmitter::~StreamingTransmitter()=default;
StreamingTransmitter::StreamingTransmitter(StreamingTransmitter&&) noexcept=default;
StreamingTransmitter& StreamingTransmitter::operator=(StreamingTransmitter&&) noexcept=default;
bool StreamingTransmitter::finished()const{return impl_->pattern->finished();}
std::uint64_t StreamingTransmitter::total_samples()const{return impl_->pattern->total_samples();}
std::uint64_t StreamingTransmitter::samples_emitted()const{return impl_->pattern->samples_emitted();}
std::size_t StreamingTransmitter::working_bytes()const{return sizeof(StreamingTransmitter)+sizeof(Impl)+
    impl_->constellation.capacity()*sizeof(Complex)+impl_->pattern->working_bytes();}
std::vector<Complex> StreamingTransmitter::payload_constellation()const {
    const auto& s=*impl_;std::vector<Complex> result;result.reserve(s.constellation_count);
    for(std::size_t i=0;i<s.constellation_count;++i)result.push_back(s.constellation[(s.constellation_begin+i)%s.constellation.size()]);
    return result;
}
ConstellationBatch StreamingTransmitter::take_payload_constellation() {
    auto& s=*impl_;ConstellationBatch result;result.points.reserve(s.pending_count);result.dropped=s.dropped;
    for(std::size_t i=s.constellation_count-s.pending_count;i<s.constellation_count;++i)
        result.points.push_back(s.constellation[(s.constellation_begin+i)%s.constellation.size()]);
    s.pending_count=0;s.dropped=0;return result;
}
std::size_t StreamingTransmitter::read(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);
    return s.pattern->read(output,stop,[&](Complex point){s.retain_constellation(point);});
}
std::size_t StreamingTransmitter::read_analytic(std::span<Complex> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);
    return s.pattern->read_analytic(output,stop,[&](Complex point){s.retain_constellation(point);});
}
std::optional<SymbolObservation> StreamingTransmitter::next_symbol(std::stop_token stop) {
    cancelled(stop);
    throw Error("pattern reception requires physical PCM or analytic samples, not despread symbol observations");
}
void StreamingTransmitter::preview_last(std::span<float> output)const {
    if(output.size()>2048)throw Error("streaming preview is limited to 2048 samples");
    std::array<Complex,2048> analytic{};preview_last_analytic(std::span(analytic).first(output.size()));
    for(std::size_t i=0;i<output.size();++i)output[i]=static_cast<float>(analytic[i].real());
}
void StreamingTransmitter::preview_last_analytic(std::span<Complex> output)const {
    if(output.size()>analytic_preview_limit)throw Error("analytic streaming preview exceeds its bounded history");
    impl_->pattern->preview_last_analytic(output);
}

struct StreamingReceiver::Impl {
    static Config dsp_config(Config config,std::size_t budget) {
        validate(config);
        if(budget<=sizeof(Impl))throw Error("pattern receiver workspace is too small");
        config.memory_limit=budget-sizeof(Impl);
        return config;
    }
    Config config;
    PatternSearch search;
    std::size_t workspace;
    PatternReceiver pattern;
    Impl(Config c,std::size_t budget,PatternSearch value):config(dsp_config(c,budget)),search(std::move(value)),workspace(budget),
        pattern(config,budget-sizeof(Impl),search) {
        validate(c);
        if(budget<=sizeof(Impl))throw Error("pattern receiver workspace is too small");
    }
};
StreamingReceiver::StreamingReceiver(Config c,std::size_t workspace,PatternSearch search):impl_(std::make_unique<Impl>(c,workspace,std::move(search))){}
StreamingReceiver::~StreamingReceiver()=default;
StreamingReceiver::StreamingReceiver(StreamingReceiver&&) noexcept=default;
StreamingReceiver& StreamingReceiver::operator=(StreamingReceiver&&) noexcept=default;
bool StreamingReceiver::synchronized()const{return impl_->pattern.synchronized();}
bool StreamingReceiver::clock_windowed()const{return impl_->pattern.clock_windowed();}
bool StreamingReceiver::acquiring()const{return impl_->pattern.acquiring();}
Diagnostics StreamingReceiver::diagnostics()const{return impl_->pattern.diagnostics();}
ConstellationBatch StreamingReceiver::take_payload_constellation(){return {impl_->pattern.take_chip_constellation(),0};}
std::size_t StreamingReceiver::working_bytes()const{return sizeof(StreamingReceiver)+sizeof(Impl)+impl_->pattern.working_bytes();}
void StreamingReceiver::set_workspace_bytes(std::size_t bytes) {
    if(bytes<working_bytes())throw Error("DSP workspace is smaller than retained receiver history");
    impl_->pattern.set_workspace_bytes(bytes-sizeof(Impl));impl_->workspace=bytes;
}
void StreamingReceiver::reset(){auto& s=*impl_;auto fresh=std::make_unique<Impl>(s.config,s.workspace,s.search);impl_=std::move(fresh);}
Bytes StreamingReceiver::push_symbols(std::span<const SymbolObservation>,std::stop_token stop) {
    cancelled(stop);throw Error("pattern acquisition requires measured PCM, not despread symbol observations");
}
Bytes StreamingReceiver::push(std::span<const float> samples,std::stop_token stop){impl_->pattern.push(samples,stop);return {};}
Bytes StreamingReceiver::push(std::span<const float> samples,std::span<const Complex> projected,std::stop_token stop){impl_->pattern.push(samples,projected,stop);return {};}
PatternBurst StreamingReceiver::provisional_pattern()const{return impl_->pattern.provisional();}
std::vector<PatternBurst> StreamingReceiver::take_pattern_bursts(){return impl_->pattern.take_bursts();}
std::vector<PatternEvidence> StreamingReceiver::pattern_candidates()const{return impl_->pattern.candidates();}
std::vector<PatternEvidence> StreamingReceiver::pattern_candidates(std::size_t limit)const{return impl_->pattern.candidates(limit);}
Bytes StreamingReceiver::finish(std::stop_token stop){impl_->pattern.finish(stop);return {};}

SymbolObservation add_awgn(SymbolObservation observation,double sample_snr_db,std::mt19937_64& random) {
    if(!observation.sample_count || !std::isfinite(sample_snr_db) || sample_snr_db < -300 || sample_snr_db > 300)throw Error("invalid integrated AWGN configuration");
    const auto sigma=std::sqrt(2*nominal_signal_power*std::pow(10.,-sample_snr_db/10)/static_cast<double>(observation.sample_count));
    std::normal_distribution<double> normal(0,sigma);
    observation.value+=Complex{normal(random),normal(random)};return observation;
}
}
