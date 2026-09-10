#include "datapump/streaming_modem.hpp"
#include "datapump/crypto.hpp"
#include "datapump/packet.hpp"
#include "constellation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::modem {
namespace {
using Complex=std::complex<double>;
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t bootstrap_symbols=packet_prefix_size*2;
using detail::phase_steps;
using detail::nibble;
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("modem operation cancelled");}
std::uint64_t chip_count(const Config& c) {
    return static_cast<std::uint64_t>(std::llround(c.sample_rate/(c.bandwidth_hz/2)/4))*4;
}
std::vector<int> pattern(const Config& c) {
    const auto count=c.spreading_factor;
    std::vector<int> result(count,1);
    constexpr std::array<int,8> fixed{1,1,-1,1,-1,-1,1,-1};
    if(c.spreading_mode==SpreadingMode::pattern && count>1)
        for(std::size_t i=0;i<count;++i)result[i]=fixed[i%fixed.size()];
    if(c.scramble) {
        Crypto key(c.spreading_seed);const auto bytes=key.stream(StreamPurpose::Scrambler,0,0,(count+7)/8);
        for(std::size_t i=0;i<count;++i)result[i]=((bytes[i/8]>>(i%8))&1)?-1:1;
    }
    if(c.dsss) {
        Crypto key(c.dsss_seed);const auto bytes=key.stream(StreamPurpose::Dsss,0,0,(count+7)/8);
        for(std::size_t i=0;i<count;++i)if((bytes[i/8]>>(i%8))&1)result[i]=-result[i];
    }
    return result;
}
bool packet_bootstrap(const Bytes& prefix) {
    try {return packet_bootstrap_possible(prefix) && packet_frame_size(prefix).has_value();}catch(const Error&){return false;}
}
struct Candidate {
    std::uint64_t end=0,start=0;
    Complex sum{};
    std::array<Complex,bootstrap_symbols> points{};
    std::array<double,bootstrap_symbols> ordered_radii{};
    std::size_t count=0,cursor=0;
    Complex previous{1,0};
    double gain=1;
    unsigned partial=0;
    bool high=false;
    bool valid=false;
    Bytes validated_header;
    std::vector<Complex> following;
    double quality=std::numeric_limits<double>::infinity();
    void retain(Complex value) {
        auto length=count;
        if(length==points.size()) {
            const auto old=std::lower_bound(ordered_radii.begin(),ordered_radii.end(),std::norm(points[cursor]));
            std::move(old+1,ordered_radii.end(),old);--length;
        }
        const auto sorted_end=ordered_radii.begin()+static_cast<std::ptrdiff_t>(length);
        const auto position=std::lower_bound(ordered_radii.begin(),sorted_end,std::norm(value));
        std::move_backward(position,sorted_end,sorted_end+1);*position=std::norm(value);
        points[cursor]=value;cursor=(cursor+1)%points.size();count=std::min(count+1,points.size());
    }
    Complex at(std::size_t i)const{return points[(cursor+i)%points.size()];}
    Bytes header() {
        gain=(std::sqrt(ordered_radii[points.size()/4])/.35+std::sqrt(ordered_radii[points.size()*3/4])/.7)/2;
        if(!std::isfinite(gain) || gain<1e-12)return {};
        Bytes bytes(packet_prefix_size);
        Complex prior{1,0};
        for(std::size_t i=0;i<points.size();++i) {
            const auto value=at(i);const auto bits=nibble(value,prior,gain);
            prior=value;
            if((i&1)==0)bytes[i/2]=static_cast<std::uint8_t>(bits<<4);
            else bytes[i/2]|=static_cast<std::uint8_t>(bits);
        }
        previous=at(points.size()-1);
        return bytes;
    }
    void measure_quality() {
        double residual=0,power=0;
        for(std::size_t i=2;i<points.size();++i) {
            const auto value=at(i),prior=at(i-1);const auto bits=nibble(value,prior,gain);
            const auto ideal=std::polar(gain*((bits&8)? .7:.35),std::arg(prior)+tau*phase_steps[bits&7]/8);
            residual+=std::norm(value-ideal);power+=std::norm(value);
        }
        quality=residual/std::max(power,1e-20);
    }
};
}

struct StreamingTransmitter::Impl {
    Bytes wire;
    Config config;
    std::vector<int> code;
    std::uint64_t position=0,total=0,training=0,symbol=0,chip=0,segment_start=0,segment_end=0;
    std::size_t symbol_index=0;
    Complex phase{1,0},point{},last_point{};
    bool pcm=false,analytical=false;
    Impl(Bytes bytes,Config value,std::size_t workspace):wire(std::move(bytes)),config(value) {
        validate(config);
        if(wire.size()<32)throw Error("16APSK wire requires the 32-byte training prefix");
        if(workspace<65536+config.spreading_factor*sizeof(int))throw Error("streaming transmitter workspace is too small");
        code=pattern(config);training=training_sample_count(config);symbol=symbol_sample_count(config);chip=chip_count(config);
        const auto payload_symbols=wire.size()-32;
        if(payload_symbols>(std::numeric_limits<std::uint64_t>::max()-training)/2/symbol)throw Error("transmission duration exceeds 64-bit sample counter");
        total=training+static_cast<std::uint64_t>(payload_symbols)*2*symbol;
        advance();
    }
    void advance() {
        const auto index=symbol_index++;
        if(index>=wire.size()*2){segment_end=total;return;}
        const auto bits=static_cast<unsigned>((wire[index/2]>>((index&1)?0:4))&15);
        phase*=std::polar(1.,tau*phase_steps[bits&7]/8);
        point=phase*((bits&8)? .7:.35);
        segment_start=segment_end;
        segment_end=index<64?training*static_cast<std::uint64_t>(index+1)/64:segment_end+symbol;
    }
    int sign(std::uint64_t sample)const {
        return sample<training?1:code[static_cast<std::size_t>(((sample-segment_start)/chip)%code.size())];
    }
};
StreamingTransmitter::StreamingTransmitter(Bytes wire,Config c,std::size_t workspace):impl_(std::make_unique<Impl>(std::move(wire),c,workspace)){}
StreamingTransmitter::~StreamingTransmitter()=default;
StreamingTransmitter::StreamingTransmitter(StreamingTransmitter&&) noexcept=default;
StreamingTransmitter& StreamingTransmitter::operator=(StreamingTransmitter&&) noexcept=default;
bool StreamingTransmitter::finished()const{return impl_->position==impl_->total;}
std::uint64_t StreamingTransmitter::total_samples()const{return impl_->total;}
std::uint64_t StreamingTransmitter::samples_emitted()const{return impl_->position;}
std::size_t StreamingTransmitter::read(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.analytical)throw Error("cannot mix PCM and integrated reads on one transmitter");s.pcm=true;
    const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),s.total-s.position));
    const auto start_angle=std::remainder(static_cast<long double>(s.position)*tau*s.config.carrier_hz/s.config.sample_rate,static_cast<long double>(tau));
    Complex oscillator=std::polar(1.,static_cast<double>(start_angle));const auto step=std::polar(1.,tau*s.config.carrier_hz/s.config.sample_rate);
    for(std::size_t i=0;i<count;++i,++s.position) {
        if((i&4095U)==0)cancelled(stop);
        while(s.position>=s.segment_end && s.position<s.total)s.advance();
        output[i]=static_cast<float>((s.point*oscillator).real()*s.sign(s.position));
        s.last_point=s.point;oscillator*=step;
    }
    return count;
}
std::optional<SymbolObservation> StreamingTransmitter::next_symbol(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.pcm)throw Error("cannot mix integrated and PCM reads on one transmitter");s.analytical=true;
    if(finished())return std::nullopt;
    // Uniform integration bins deliberately cross training and data boundaries.
    // This is the matched-filter sufficient statistic; multiplying a known
    // unit-magnitude chip code twice leaves this same signal and AWGN variance.
    const auto quantum=std::max<std::uint64_t>(1,s.symbol/32);
    const auto count=std::min(quantum,s.total-s.position);
    auto remaining=count;Complex integrated{};
    while(remaining) {
        cancelled(stop);while(s.position>=s.segment_end)s.advance();
        const auto part=std::min(remaining,s.segment_end-s.position);
        integrated+=s.point*static_cast<double>(part);s.position+=part;remaining-=part;s.last_point=s.point;
    }
    return SymbolObservation{integrated/static_cast<double>(count),count};
}
void StreamingTransmitter::preview_last(std::span<float> output)const {
    const auto& s=*impl_;
    for(std::size_t i=0;i<output.size();++i)
        output[i]=static_cast<float>((s.last_point*std::polar(1.,tau*s.config.carrier_hz*static_cast<double>(i)/s.config.sample_rate)).real());
}

struct StreamingReceiver::Impl {
    Config config;
    Bytes expected;
    BootstrapValidator validator;
    std::vector<int> code;
    std::vector<Candidate> candidates;
    std::uint64_t position=0,symbol=0,chip=0,training=0,earliest=0;
    std::size_t selected=0,workspace=0;
    bool synced=false;
    bool finished=false;
    std::uint64_t selection_deadline=0;
    Diagnostics diagnostic;
    Complex oscillator{1,0};
    double xc=0,xs=0,cc=0,ss=0,cs=0;
    std::uint64_t pcm_count=0;
    Impl(Config c,Bytes pre,std::size_t budget,BootstrapValidator check):config(c),expected(std::move(pre)),validator(std::move(check)),workspace(budget) {
        validate(c);if(expected.size()!=32)throw Error("16APSK expects a 32-byte training prefix");
        if(!validator)validator=packet_bootstrap;
        symbol=symbol_sample_count(c);training=training_sample_count(c);chip=chip_count(c);
        if(symbol>(std::numeric_limits<std::uint64_t>::max()-training)/bootstrap_symbols)throw Error("bootstrap acquisition exceeds 64-bit sample counter");
        earliest=training+symbol*(bootstrap_symbols-1);
        const bool fine=(c.scramble || c.dsss) && c.spreading_factor>=1024 && symbol>4*chip;
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(symbol,fine?224:c.spreading_mode==SpreadingMode::tone&&!c.dsss?64:256));
        if(count*sizeof(Candidate)+c.spreading_factor*sizeof(int)+65536>budget)throw Error("streaming receiver workspace is too small");
        code=pattern(c);
        std::vector<std::uint64_t> origins;origins.reserve(count);
        const auto coarse=fine?count/2:count;
        for(std::size_t i=0;i<coarse;++i)origins.push_back((symbol/coarse)*i+((symbol%coarse)*i)/coarse);
        if(fine) {
            // Preserve a finite coarse blind bank, while spending half the
            // existing budget on sample-level origins near the known fixed
            // training duration. This covers capture-aligned and nearby starts
            // even when a long keyed template has extremely narrow chip peaks.
            const auto anchor=static_cast<std::int64_t>(training%symbol);
            const auto slots=count-coarse;
            for(std::size_t i=0;i<slots;++i) {
                const auto delta=(static_cast<std::int64_t>(i)-static_cast<std::int64_t>(slots/2))*static_cast<std::int64_t>(4*chip)/static_cast<std::int64_t>(slots);
                auto origin=anchor+delta;
                if(origin<0)origin+=static_cast<std::int64_t>(symbol);
                if(origin>=static_cast<std::int64_t>(symbol))origin-=static_cast<std::int64_t>(symbol);
                origins.push_back(static_cast<std::uint64_t>(origin));
            }
        }
        std::sort(origins.begin(),origins.end());origins.erase(std::unique(origins.begin(),origins.end()),origins.end());
        candidates.resize(origins.size());
        for(std::size_t i=0;i<candidates.size();++i) {
            candidates[i].start=origins[i];candidates[i].end=origins[i]+symbol;
        }
        diagnostic.bit_rate=bit_rate(c);
    }
    void completed(std::size_t index,Complex point,Bytes& output) {
        auto& candidate=candidates[index];
        if(synced) {
            const auto value=nibble(point,candidate.previous,candidate.gain);candidate.previous=point;
            if(!candidate.high){candidate.partial=value<<4;candidate.high=true;}
            else {output.push_back(static_cast<std::uint8_t>(candidate.partial|value));candidate.high=false;}
            if(diagnostic.constellation.size()<2048)diagnostic.constellation.push_back(point/candidate.gain);
            return;
        }
        if(candidate.valid) {candidate.following.push_back(point);return;}
        candidate.retain(point);
        if(candidate.count<bootstrap_symbols || candidate.end<earliest)return;
        const auto header=candidate.header();if(header.empty())return;
        bool valid=false;try{valid=validator(header);}catch(const Error&){}
        if(!valid)return;
        candidate.measure_quality();
        candidate.valid=true;candidate.validated_header=header;
        if(!selection_deadline) {
            if(symbol>std::numeric_limits<std::uint64_t>::max()-candidate.end)throw Error("receiver selection counter overflow");
            selection_deadline=candidate.end+symbol;
        }
    }
    void select(Bytes& output) {
        double best=std::numeric_limits<double>::infinity();
        for(std::size_t index=0;index<candidates.size();++index)
            if(candidates[index].valid && candidates[index].quality<best){best=candidates[index].quality;selected=index;}
        auto& candidate=candidates[selected];
        synced=true;
        const auto payload_start=candidate.start-symbol*(bootstrap_symbols+candidate.following.size());
        diagnostic.sample_offset=static_cast<std::size_t>(payload_start>training?payload_start-training:0);
        output.insert(output.end(),expected.begin(),expected.end());output.insert(output.end(),candidate.validated_header.begin(),candidate.validated_header.end());
        double power=0,error=0;Complex prior{1,0};
        for(std::size_t i=0;i<bootstrap_symbols;++i) {
            const auto value=candidate.at(i)/candidate.gain;
            const auto bits=nibble(value,prior,1);const auto ideal=std::polar((bits&8)? .7:.35,std::arg(prior)+tau*phase_steps[bits&7]/8);
            power+=std::norm(ideal);error+=std::norm(value-ideal);prior=value;
            diagnostic.constellation.push_back(value);
        }
        diagnostic.snr_db=10*std::log10(power/std::max(error,1e-20));
        diagnostic.preamble_correlation=0; // Acquisition evidence is bootstrap validation, not training.
        const auto following=std::move(candidate.following);
        for(const auto point:following)completed(selected,point,output);
    }
    Bytes feed(SymbolObservation observation,bool matched,std::stop_token stop) {
        cancelled(stop);if(!observation.sample_count || !std::isfinite(observation.value.real()) || !std::isfinite(observation.value.imag()))throw Error("invalid integrated observation");
        if(observation.sample_count>std::numeric_limits<std::uint64_t>::max()-position)throw Error("receiver sample counter overflow");
        const auto finish=position+observation.sample_count;Bytes output;
        const auto first=synced?selected:0,last=synced?selected+1:candidates.size();
        for(std::size_t index=first;index<last;++index) {
            if((index&15U)==0)cancelled(stop);
            auto& candidate=candidates[index];auto cursor=std::max(position,candidate.start);
            while(cursor<finish) {
                cancelled(stop);
                const auto end=std::min(finish,candidate.end);
                double sign=1;
                if(!matched)sign=code[static_cast<std::size_t>(((cursor-candidate.start)/chip)%code.size())];
                candidate.sum+=observation.value*(static_cast<double>(end-cursor)*sign);cursor=end;
                if(!std::isfinite(candidate.sum.real()) || !std::isfinite(candidate.sum.imag()))throw Error("integrated sample magnitude overflow");
                if(cursor==candidate.end) {
                    const auto value=candidate.sum/static_cast<double>(symbol);candidate.sum={};
                    completed(index,value,output);
                    candidate.start=candidate.end;
                    if(symbol>std::numeric_limits<std::uint64_t>::max()-candidate.end)throw Error("receiver symbol counter overflow");
                    candidate.end+=symbol;
                    if(synced && selected!=index)break;
                }
            }
            if(synced)break;
        }
        position=finish;
        if(!synced && selection_deadline && position>=selection_deadline)select(output);
        return output;
    }
};
StreamingReceiver::StreamingReceiver(Config c,Bytes pre,std::size_t workspace,BootstrapValidator validator):impl_(std::make_unique<Impl>(c,std::move(pre),workspace,std::move(validator))){}
StreamingReceiver::~StreamingReceiver()=default;
StreamingReceiver::StreamingReceiver(StreamingReceiver&&) noexcept=default;
StreamingReceiver& StreamingReceiver::operator=(StreamingReceiver&&) noexcept=default;
bool StreamingReceiver::synchronized()const{return impl_->synced;}
Diagnostics StreamingReceiver::diagnostics()const{return impl_->diagnostic;}
std::size_t StreamingReceiver::working_bytes()const{
    std::size_t bytes=sizeof(Impl)+impl_->candidates.capacity()*sizeof(Candidate)+impl_->code.capacity()*sizeof(int)+impl_->diagnostic.constellation.capacity()*sizeof(Complex)+impl_->expected.capacity();
    for(const auto& candidate:impl_->candidates)bytes+=candidate.validated_header.capacity()+candidate.following.capacity()*sizeof(Complex);
    return bytes;
}
void StreamingReceiver::reset(){auto& s=*impl_;auto fresh=std::make_unique<Impl>(s.config,s.expected,s.workspace,s.validator);impl_=std::move(fresh);}
Bytes StreamingReceiver::push_symbols(std::span<const SymbolObservation> observations,std::stop_token stop) {
    if(impl_->finished)throw Error("capture already finished; reset before appending input");
    Bytes output;for(const auto& observation:observations){auto bytes=impl_->feed(observation,true,stop);output.insert(output.end(),bytes.begin(),bytes.end());}return output;
}
Bytes StreamingReceiver::push(std::span<const float> samples,std::stop_token stop) {
    auto& s=*impl_;Bytes output;
    if(s.finished)throw Error("capture already finished; reset before appending input");
    // Solve the real I/Q Gram system instead of assuming an integer number of
    // carrier cycles per integration. Chunk boundaries do not reset the sums.
    const auto quantum=std::max<std::uint64_t>(4,std::min<std::uint64_t>(s.chip/4,256));
    const auto step=std::polar(1.,tau*s.config.carrier_hz/s.config.sample_rate);
    for(std::size_t i=0;i<samples.size();++i) {
        if((i&4095U)==0)cancelled(stop);
        if(!std::isfinite(samples[i]))throw Error("non-finite audio sample");
        const auto c=s.oscillator.real(),q=-s.oscillator.imag();s.oscillator*=step;
        s.xc+=samples[i]*c;s.xs+=samples[i]*q;s.cc+=c*c;s.ss+=q*q;s.cs+=c*q;
        if(++s.pcm_count<quantum)continue;
        const auto determinant=s.cc*s.ss-s.cs*s.cs;
        const Complex value=determinant>1e-12?Complex{(s.xc*s.ss-s.xs*s.cs)/determinant,(s.xs*s.cc-s.xc*s.cs)/determinant}:Complex{};
        auto bytes=s.feed({value,s.pcm_count},false,stop);output.insert(output.end(),bytes.begin(),bytes.end());
        s.xc=s.xs=s.cc=s.ss=s.cs=0;s.pcm_count=0;
    }
    return output;
}
Bytes StreamingReceiver::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return {};
    Bytes output;
    if(s.pcm_count) {
        const auto determinant=s.cc*s.ss-s.cs*s.cs;
        const Complex value=determinant>1e-12?Complex{(s.xc*s.ss-s.xs*s.cs)/determinant,(s.xs*s.cc-s.xc*s.cs)/determinant}:Complex{};
        output=s.feed({value,s.pcm_count},false,stop);s.pcm_count=0;
    }
    const auto tail=s.feed({{},s.symbol},true,stop);output.insert(output.end(),tail.begin(),tail.end());s.finished=true;
    return output;
}
SymbolObservation add_awgn(SymbolObservation observation,double sample_snr_db,std::mt19937_64& random) {
    if(!observation.sample_count || !std::isfinite(sample_snr_db) || sample_snr_db < -300 || sample_snr_db > 300)throw Error("invalid integrated AWGN configuration");
    const auto sigma=std::sqrt(2*nominal_signal_power*std::pow(10.,-sample_snr_db/10)/static_cast<double>(observation.sample_count));
    std::normal_distribution<double> normal(0,sigma);
    observation.value+=Complex{normal(random),normal(random)};return observation;
}
}
