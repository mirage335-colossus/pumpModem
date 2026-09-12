#include "datapump/streaming_modem.hpp"
#include "datapump/crypto.hpp"
#include "datapump/packet.hpp"
#include "datapump/tuning.hpp"
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
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("modem operation cancelled");}
// The wire encodes phase changes, not absolute carrier phase. Retain the
// measured amplitude and rotate by the preceding measurement's unit phase;
// do not round either axis to the decoder's ideal symbol decision.
Complex decision_coordinates(Complex point,Complex previous) {
    return std::abs(previous)>1e-20?point*std::conj(previous)/std::abs(previous):point;
}
struct PendingConstellation {
    std::size_t count=0;
    std::uint64_t dropped=0;
    Complex previous{1,0}; // Measurement immediately before the history ring.
    void append(std::size_t capacity) {
        if(count<capacity)++count;
        else if(dropped<std::numeric_limits<std::uint64_t>::max())++dropped;
    }
    template<typename At>
    ConstellationBatch take(std::size_t history_size,At at) {
        ConstellationBatch result;result.points.reserve(count);result.dropped=dropped;
        const auto begin=history_size-count;
        auto prior=begin?at(begin-1):previous;
        for(std::size_t i=begin;i<history_size;++i) {
            const auto point=at(i);result.points.push_back(decision_coordinates(point,prior));prior=point;
        }
        count=0;dropped=0;return result;
    }
};
std::uint64_t chip_count(const Config& c) {
    return static_cast<std::uint64_t>(std::ceil(2.*c.sample_rate/c.bandwidth_hz));
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
struct PcmProjection {
    double xc=0,xs=0,cc=0,ss=0,cs=0;
    PcmProjection operator-(const PcmProjection& other)const {
        return {xc-other.xc,xs-other.xs,cc-other.cc,ss-other.ss,cs-other.cs};
    }
    void add(const PcmProjection& other,double sign=1) {
        xc+=sign*other.xc;xs+=sign*other.xs;
        cc+=other.cc;ss+=other.ss;cs+=other.cs;
    }
    Complex value()const {
        const auto determinant=cc*ss-cs*cs;
        return determinant>1e-12?Complex{(xc*ss-xs*cs)/determinant,(xs*cc-xc*cs)/determinant}:Complex{};
    }
    static PcmProjection silence(std::uint64_t start,std::uint64_t count,const Config& config) {
        // A zero-valued tail still contributes the oscillator's Gram matrix.
        // Its closed form keeps finish bounded even for hour-long symbols.
        const long double angle=static_cast<long double>(tau)*config.carrier_hz/config.sample_rate;
        const auto length=static_cast<long double>(count);
        const auto scale=std::sin(std::remainder(length*angle,static_cast<long double>(tau)))/std::sin(angle);
        const auto phase=std::remainder((2*static_cast<long double>(start)+length-1)*angle,static_cast<long double>(tau));
        const auto cosine=scale*std::cos(phase),sine=scale*std::sin(phase);
        return {0,0,static_cast<double>((length+cosine)/2),static_cast<double>((length-cosine)/2),static_cast<double>(-sine/2)};
    }
};
struct Candidate {
    std::uint64_t end=0,start=0;
    Complex sum{};
    PcmProjection pcm_sum;
    std::vector<Complex> points;
    std::vector<double> ordered_radii;
    unsigned bits=4;
    std::size_t count=0,cursor=0;
    std::size_t repeated_points=0;
    Complex last_point{};
    Complex previous{1,0};
    double gain=1;
    std::array<double,8> gain_hypotheses{};
    unsigned gain_count=0;
    double residual_limit=0;
    unsigned partial=0;
    unsigned partial_bits=0;
    bool valid=false;
    Bytes validated_header;
    Bytes last_failed_header;
    std::vector<Complex> following;
    double quality=std::numeric_limits<double>::infinity();
    void retain(Complex value) {
        repeated_points=value==last_point?std::min(repeated_points+1,points.size()):1;
        last_point=value;
        auto length=count;
        if(length==points.size()) {
            const auto old=std::lower_bound(ordered_radii.begin(),ordered_radii.end(),(bits>=5?std::abs(points[cursor]):std::norm(points[cursor])));
            std::move(old+1,ordered_radii.end(),old);--length;
        }
        const auto amplitude=bits>=5?std::abs(value):std::norm(value);
        const auto sorted_end=ordered_radii.begin()+static_cast<std::ptrdiff_t>(length);
        const auto position=std::lower_bound(ordered_radii.begin(),sorted_end,amplitude);
        std::move_backward(position,sorted_end,sorted_end+1);*position=amplitude;
        points[cursor]=value;if(++cursor==points.size())cursor=0;count=std::min(count+1,points.size());
    }
    Complex at(std::size_t i)const{const auto index=cursor+i;return points[index<points.size()?index:index-points.size()];}
    void estimate_gain() {
        gain_count=0;
        if(detail::rings(bits)==2) {
            gain=(std::sqrt(ordered_radii[points.size()/4])/.35+std::sqrt(ordered_radii[points.size()*3/4])/.7)/2;
            if(std::isfinite(gain) && gain>=1e-12)gain_hypotheses[gain_count++]=gain;
            return;
        }
        const auto spacing=detail::radius_step(bits);
        const auto levels=detail::rings(bits);
        const auto radii=std::span(ordered_radii);
        if(!std::isfinite(radii.back()) || radii.back()<1e-12)return;
        // At the planner's geometric Es/N0 margin the radial noise standard
        // deviation is sqrt(E/(2*Es/N0)). Admit three times that RMS residual
        // before expensive bootstrap decoding; pure noise has no such lattice.
        // Try every possible highest occupied ring, so absent outer rings do
        // not imply a transmitter-side gain assumption. Bootstrap validation
        // resolves remaining gain aliases (e.g. only even rings occupied).
        std::array<std::pair<double,double>,8> plausible{};unsigned plausible_count=0;
        for(unsigned highest=1;highest<=levels;++highest) {
            double candidate=radii.back()/(spacing*highest);
            double initial_residual=0;const auto initial_inverse=1/(spacing*candidate);
            for(const auto actual:radii) {
                const auto scaled=actual*initial_inverse;
                const auto ring=static_cast<double>(static_cast<unsigned>(std::clamp(scaled+.5,1.,static_cast<double>(levels))));
                initial_residual+=(scaled-ring)*(scaled-ring);
            }
            if(initial_residual>residual_limit*static_cast<double>(radii.size()))continue;
            for(unsigned iteration=0;iteration<2;++iteration) {
                double numerator=0,denominator=0;
                const auto inverse=1/(spacing*candidate);
                for(const auto actual:radii) {
                    const auto ring=static_cast<double>(static_cast<unsigned>(std::clamp(actual*inverse+.5,1.,static_cast<double>(levels))));
                    numerator+=actual*ring;denominator+=ring*ring;
                }
                candidate=numerator/(spacing*denominator);
                if(!std::isfinite(candidate) || candidate<1e-12)break;
                // The first refinement removes the observed-maximum's noise
                // bias; reject incompatible lattices before another fit pass.
                double residual=0;const auto refined_inverse=1/(spacing*candidate);
                for(const auto actual:radii) {
                    const auto scaled=actual*refined_inverse;
                    const auto ring=static_cast<double>(static_cast<unsigned>(std::clamp(scaled+.5,1.,static_cast<double>(levels))));
                    residual+=(scaled-ring)*(scaled-ring);
                }
                residual/=static_cast<double>(radii.size());
                if(residual>residual_limit)break;
                if(iteration==1) {
                    if(plausible_count==plausible.size())throw Error("gain hypothesis capacity exceeded");
                    const std::pair<double,double> fit{residual,candidate};
                    // At most eight entries: insert directly in residual order
                    // instead of invoking a general-purpose introsort.
                    auto slot=plausible_count;
                    while(slot>0 && fit<plausible[slot-1]) {
                        plausible[slot]=plausible[slot-1];--slot;
                    }
                    plausible[slot]=fit;++plausible_count;
                }
            }
        }
        for(unsigned i=0;i<plausible_count;++i) {
            const auto candidate=plausible[i].second;
            bool duplicate=false;
            for(unsigned j=0;j<gain_count;++j)duplicate=duplicate || std::abs(candidate-gain_hypotheses[j])<1e-4*candidate;
            if(!duplicate)gain_hypotheses[gain_count++]=candidate;
        }
    }
    Bytes header(const BootstrapValidator& validator) {
        estimate_gain();
        for(unsigned hypothesis=0;hypothesis<gain_count;++hypothesis) {
            gain=gain_hypotheses[hypothesis];
            Bytes bytes;bytes.reserve(packet_prefix_size);
            Complex prior{1,0};unsigned pending=0,available=0;
            for(std::size_t i=0;i<points.size();++i) {
                const auto value=at(i);const auto decoded=detail::decision(value,prior,gain,bits);
                prior=value;pending=(pending<<bits)|decoded;available+=bits;
                if(available>=8){available-=8;bytes.push_back(static_cast<std::uint8_t>(pending>>available));}
            }
            if(bytes==last_failed_header)continue;
            bool accepted=false;try{accepted=validator(bytes);}catch(const Error&){}
            if(accepted){previous=at(points.size()-1);return bytes;}
            last_failed_header=std::move(bytes);
        }
        return {};
    }
    void measure_quality() {
        double residual=0,power=0;
        for(std::size_t i=2;i<points.size();++i) {
            const auto value=at(i),prior=at(i-1);const auto decoded=detail::decision(value,prior,gain,bits);
            const auto ideal=gain*detail::mapped(decoded,bits,prior);
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
    std::size_t symbol_index=0,payload_symbols=0,bootstrap_count=0;
    Complex phase{1,0},point{};
    struct Segment {std::uint64_t begin=0,end=0;Complex value{};};
    // At least four samples/symbol: cover every symbol intersecting the
    // analytic preview and its interpolation margins, including boundaries.
    std::array<Segment,StreamingTransmitter::analytic_preview_limit/4+2> history{};
    std::size_t history_begin=0,history_count=0;
    std::array<Complex,StreamingTransmitter::constellation_history_limit> constellation{};
    std::size_t constellation_begin=0,constellation_count=0;
    PendingConstellation pending_constellation;
    bool pcm=false,analytical=false;
    Impl(Bytes bytes,Config value,std::size_t workspace):wire(std::move(bytes)),config(value) {
        static_assert(sizeof(Impl)<=65536,"transmitter state exceeds its fixed workspace reservation");
        validate(config);
        if(wire.size()<32)throw Error("APSK wire requires the 32-byte training prefix");
        if(workspace<65536+config.spreading_factor*sizeof(int))throw Error("streaming transmitter workspace is too small");
        code=pattern(config);training=training_sample_count(config);symbol=symbol_sample_count(config);chip=chip_count(config);
        payload_symbols=payload_symbol_count(wire.size()-32,config);
        bootstrap_count=(std::min(wire.size()-32,packet_prefix_size)*8+config.constellation_bits-1)/config.constellation_bits;
        if(payload_symbols>(std::numeric_limits<std::uint64_t>::max()-training)/symbol)throw Error("transmission duration exceeds 64-bit sample counter");
        total=training+static_cast<std::uint64_t>(payload_symbols)*symbol;
        advance();
    }
    void advance() {
        const auto index=symbol_index++;
        if(index>=64+payload_symbols){segment_end=total;return;}
        unsigned bits=4,value=0;
        if(index<64)value=detail::read_bits(std::span(wire).first(32),index*4,4);
        else {
            bits=config.constellation_bits;const auto payload_index=index-64;
            if(payload_index<bootstrap_count)
                value=detail::read_bits(std::span(wire).subspan(32,std::min(wire.size()-32,packet_prefix_size)),payload_index*bits,bits);
            else value=detail::read_bits(std::span(wire).subspan(32+packet_prefix_size),(payload_index-bootstrap_count)*bits,bits);
        }
        const auto previous=phase;
        point=detail::mapped(value,bits,phase);phase=point/std::abs(point);
        if(index>=64) {
            if(!constellation_count)pending_constellation.previous=previous;
            if(constellation_count==constellation.size()) {
                pending_constellation.previous=constellation[constellation_begin];
                constellation_begin=(constellation_begin+1)%constellation.size();--constellation_count;
            }
            constellation[(constellation_begin+constellation_count++)%constellation.size()]=point;
            pending_constellation.append(constellation.size());
        }
        segment_start=segment_end;
        segment_end=index<64?training*static_cast<std::uint64_t>(index+1)/64:segment_end+symbol;
        if(history_count==history.size()){history_begin=(history_begin+1)%history.size();--history_count;}
        history[(history_begin+history_count++)%history.size()]={segment_start,segment_end,point};
        const auto oldest=position>StreamingTransmitter::analytic_preview_limit?position-StreamingTransmitter::analytic_preview_limit:0;
        while(history_count>1 && history[history_begin].end<=oldest) {
            history_begin=(history_begin+1)%history.size();--history_count;
        }
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
std::vector<Complex> StreamingTransmitter::payload_constellation()const {
    const auto& s=*impl_;
    std::vector<Complex> result;result.reserve(s.constellation_count);
    for(std::size_t i=0;i<s.constellation_count;++i)
        result.push_back(s.constellation[(s.constellation_begin+i)%s.constellation.size()]);
    return result;
}
ConstellationBatch StreamingTransmitter::take_payload_constellation() {
    auto& s=*impl_;
    return s.pending_constellation.take(s.constellation_count,[&](std::size_t i){
        return s.constellation[(s.constellation_begin+i)%s.constellation.size()];
    });
}
std::size_t StreamingTransmitter::read(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.analytical)throw Error("cannot mix PCM and integrated reads on one transmitter");s.pcm=true;
    const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),s.total-s.position));
    const auto start_angle=std::remainder(static_cast<long double>(s.position)*tau*s.config.carrier_hz/s.config.sample_rate,static_cast<long double>(tau));
    Complex oscillator=std::polar(1.,static_cast<double>(start_angle));const auto step=std::polar(1.,tau*s.config.carrier_hz/s.config.sample_rate);
    for(std::size_t i=0;i<count;++i,++s.position) {
        if((i&4095U)==0)cancelled(stop);
        while(s.position>=s.segment_end && s.position<s.total)s.advance();
        output[i]=static_cast<float>((s.point*oscillator).real()*s.sign(s.position));
        oscillator*=step;
    }
    return count;
}
std::optional<SymbolObservation> StreamingTransmitter::next_symbol(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.pcm)throw Error("cannot mix integrated and PCM reads on one transmitter");s.analytical=true;
    if(finished())return std::nullopt;
    // Training has64 fixed waveform segments, regardless of the payload clock.
    // Integrate each segment directly so a future SDR clock does not require
    // hundreds of millions of synthetic preamble observations. Payload bins
    // still cross symbol boundaries and carry no framing labels to the decoder.
    while(s.position>=s.segment_end)s.advance();
    const auto quantum=std::max<std::uint64_t>(1,s.symbol/32);
    const auto count=s.position<s.training?s.segment_end-s.position:std::min(quantum,s.total-s.position);
    auto remaining=count;Complex integrated{};
    while(remaining) {
        cancelled(stop);while(s.position>=s.segment_end)s.advance();
        const auto part=std::min(remaining,s.segment_end-s.position);
        integrated+=s.point*static_cast<double>(part);s.position+=part;remaining-=part;
    }
    return SymbolObservation{integrated/static_cast<double>(count),count};
}
void StreamingTransmitter::preview_last(std::span<float> output)const {
    if(output.size()>2048)throw Error("streaming preview is limited to 2048 samples");
    std::array<Complex,2048> analytic{};
    preview_last_analytic(std::span(analytic).first(output.size()));
    for(std::size_t i=0;i<output.size();++i)output[i]=static_cast<float>(analytic[i].real());
}
void StreamingTransmitter::preview_last_analytic(std::span<Complex> output)const {
    const auto& s=*impl_;
    if(output.size()>analytic_preview_limit)throw Error("analytic streaming preview exceeds its bounded history");
    const auto count=std::min<std::uint64_t>(output.size(),s.position);
    const auto leading=output.size()-static_cast<std::size_t>(count);
    std::fill(output.begin(),output.end(),Complex{});
    if(!count)return;
    const auto start=s.position-count;
    const auto angle=std::remainder(static_cast<long double>(start)*tau*s.config.carrier_hz/s.config.sample_rate,static_cast<long double>(tau));
    Complex oscillator=std::polar(1.,static_cast<double>(angle));
    const auto rotation=std::polar(1.,tau*s.config.carrier_hz/s.config.sample_rate);
    std::size_t entry=0;
    for(std::uint64_t i=0;i<count;++i) {
        const auto position=start+i;
        while(entry+1<s.history_count && s.history[(s.history_begin+entry)%s.history.size()].end<=position)++entry;
        const auto& segment=s.history[(s.history_begin+entry)%s.history.size()];
        if(position>=segment.begin && position<segment.end) {
            const auto sign=position<s.training?1:s.code[static_cast<std::size_t>(((position-segment.begin)/s.chip)%s.code.size())];
            output[leading+static_cast<std::size_t>(i)]=segment.value*oscillator*static_cast<double>(sign);
        }
        oscillator*=rotation;
    }
}

struct StreamingReceiver::Impl {
    Config config;
    Bytes expected;
    BootstrapValidator validator;
    std::vector<int> code;
    std::vector<Candidate> candidates;
    std::uint64_t position=0,symbol=0,chip=0,training=0,earliest=0;
    std::size_t selected=0,workspace=0,bootstrap_symbols=0;
    bool synced=false;
    bool finished=false;
    std::uint64_t selection_deadline=0;
    Diagnostics diagnostic;
    std::size_t constellation_cursor=0;
    PendingConstellation pending_constellation;
    Complex oscillator{1,0};
    enum class Input { none,pcm,integrated };
    Input input=Input::none;
    Impl(Config c,Bytes pre,std::size_t budget,BootstrapValidator check):config(c),expected(std::move(pre)),validator(std::move(check)),workspace(budget) {
        validate(c);if(expected.size()!=32)throw Error("APSK expects a 32-byte training prefix");
        bootstrap_symbols=(packet_prefix_size*8+c.constellation_bits-1)/c.constellation_bits;
        if(!validator)validator=packet_bootstrap;
        symbol=symbol_sample_count(c);training=training_sample_count(c);chip=chip_count(c);
        if(symbol>(std::numeric_limits<std::uint64_t>::max()-training)/bootstrap_symbols)throw Error("bootstrap acquisition exceeds 64-bit sample counter");
        // Bootstrap validation is the synchronization evidence. A capture can
        // start after training, and crystal error changes its received length.
        earliest=0;
        const bool fine=(c.scramble || c.dsss) && c.spreading_factor>=1024 && symbol>4*chip;
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(symbol,fine?224:c.spreading_mode==SpreadingMode::tone&&!c.dsss?64:256));
        if(count*(sizeof(Candidate)+packet_prefix_size+bootstrap_symbols*(sizeof(Complex)+sizeof(double)))+c.spreading_factor*sizeof(int)+65536>budget)throw Error("streaming receiver workspace is too small");
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
            candidates[i].points.resize(bootstrap_symbols);candidates[i].ordered_radii.resize(bootstrap_symbols);candidates[i].bits=c.constellation_bits;
            candidates[i].last_failed_header.reserve(packet_prefix_size);
            if(c.constellation_bits>=5) {
                const auto spacing=detail::radius_step(c.constellation_bits);
                const auto esn0=std::pow(10.,tuning::constellation_target_symbol_snr_db(c.constellation_bits)/10);
                candidates[i].residual_limit=9*.30625/(2*esn0*spacing*spacing);
            }
        }
        diagnostic.bit_rate=bit_rate(c);diagnostic.constellation.reserve(2048);
    }
    void retain_constellation(Complex point,bool has_reference=true) {
        if(diagnostic.constellation.size()<2048)diagnostic.constellation.push_back(point);
        else {
            pending_constellation.previous=diagnostic.constellation[constellation_cursor];
            diagnostic.constellation[constellation_cursor]=point;
            if(++constellation_cursor==diagnostic.constellation.size())constellation_cursor=0;
        }
        if(has_reference)pending_constellation.append(2048);
    }
    void completed(std::size_t index,Complex point,Bytes& output) {
        auto& candidate=candidates[index];
        if(synced) {
            const auto value=detail::decision(point,candidate.previous,candidate.gain,config.constellation_bits);candidate.previous=point;
            candidate.partial=(candidate.partial<<config.constellation_bits)|value;candidate.partial_bits+=config.constellation_bits;
            if(candidate.partial_bits>=8){candidate.partial_bits-=8;output.push_back(static_cast<std::uint8_t>(candidate.partial>>candidate.partial_bits));}
            retain_constellation(point/candidate.gain);
            return;
        }
        if(candidate.valid) {candidate.following.push_back(point);return;}
        candidate.retain(point);
        if(candidate.count<bootstrap_symbols || candidate.end<earliest)return;
        const auto header=candidate.header(validator);if(header.empty())return;
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
            const auto bits=detail::decision(value,prior,1,config.constellation_bits);const auto ideal=detail::mapped(bits,config.constellation_bits,prior);
            power+=std::norm(ideal);error+=std::norm(value-ideal);prior=value;
            retain_constellation(value,i!=0);
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
                    if(matched && !synced && !candidate.valid && candidate.repeated_points==bootstrap_symbols) {
                        // A complete identical-point bootstrap has just failed
                        // validation. Every further full-symbol window inside
                        // this same constant observation is identical, so skip
                        // those windows without repeating the same search.
                        const auto advance=((finish-cursor)/symbol)*symbol;
                        if(advance>std::numeric_limits<std::uint64_t>::max()-candidate.end)throw Error("receiver symbol counter overflow");
                        candidate.start+=advance;candidate.end+=advance;cursor+=advance;
                    }
                }
            }
            if(synced)break;
        }
        position=finish;
        if(!synced && selection_deadline && position>=selection_deadline)select(output);
        return output;
    }
    template<class Projection>
    Bytes feed_pcm(std::uint64_t count,const Projection& projection,bool split_chips,std::stop_token stop) {
        cancelled(stop);
        if(count>std::numeric_limits<std::uint64_t>::max()-position)throw Error("receiver sample counter overflow");
        const auto finish=position+count;Bytes output;
        const auto first=synced?selected:0,last=synced?selected+1:candidates.size();
        for(std::size_t index=first;index<last;++index) {
            if((index&15U)==0)cancelled(stop);
            auto& candidate=candidates[index];auto cursor=std::max(position,candidate.start);
            while(cursor<finish) {
                cancelled(stop);
                auto end=std::min(finish,candidate.end);
                double sign=1;
                if(split_chips) {
                    const auto offset=cursor-candidate.start;
                    sign=code[static_cast<std::size_t>((offset/chip)%code.size())];
                    end=cursor+std::min(end-cursor,chip-offset%chip);
                }
                // De-spreading changes the two signal projections only: the
                // sign appears twice in each Gram product and cancels out.
                candidate.pcm_sum.add(projection(cursor-position,end-cursor),sign);
                cursor=end;
                if(cursor==candidate.end) {
                    const auto value=candidate.pcm_sum.value();candidate.pcm_sum={};
                    if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))throw Error("integrated sample magnitude overflow");
                    completed(index,value,output);
                    candidate.start=candidate.end;
                    if(symbol>std::numeric_limits<std::uint64_t>::max()-candidate.end)throw Error("receiver symbol counter overflow");
                    candidate.end+=symbol;
                }
            }
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
Diagnostics StreamingReceiver::diagnostics()const{
    auto result=impl_->diagnostic;
    if(impl_->constellation_cursor)
        std::rotate(result.constellation.begin(),result.constellation.begin()+static_cast<std::ptrdiff_t>(impl_->constellation_cursor),result.constellation.end());
    return result;
}
ConstellationBatch StreamingReceiver::take_payload_constellation() {
    auto& s=*impl_;
    return s.pending_constellation.take(s.diagnostic.constellation.size(),[&](std::size_t i){
        return s.diagnostic.constellation[(s.constellation_cursor+i)%s.diagnostic.constellation.size()];
    });
}
std::size_t StreamingReceiver::working_bytes()const{
    std::size_t bytes=sizeof(Impl)+impl_->candidates.capacity()*sizeof(Candidate)+impl_->code.capacity()*sizeof(int)+impl_->diagnostic.constellation.capacity()*sizeof(Complex)+impl_->expected.capacity();
    for(const auto& candidate:impl_->candidates)bytes+=candidate.validated_header.capacity()+candidate.last_failed_header.capacity()+(candidate.following.capacity()+candidate.points.capacity())*sizeof(Complex)+candidate.ordered_radii.capacity()*sizeof(double);
    return bytes;
}
void StreamingReceiver::reset(){auto& s=*impl_;auto fresh=std::make_unique<Impl>(s.config,s.expected,s.workspace,s.validator);impl_=std::move(fresh);}
Bytes StreamingReceiver::push_symbols(std::span<const SymbolObservation> observations,std::stop_token stop) {
    auto& s=*impl_;
    if(s.finished)throw Error("capture already finished; reset before appending input");
    if(observations.empty())return {};
    cancelled(stop);
    if(s.input==Impl::Input::pcm)throw Error("cannot mix PCM and integrated input; reset receiver first");
    s.input=Impl::Input::integrated;
    Bytes output;for(const auto& observation:observations){auto bytes=s.feed(observation,true,stop);output.insert(output.end(),bytes.begin(),bytes.end());}return output;
}
Bytes StreamingReceiver::push(std::span<const float> samples,std::stop_token stop) {
    auto& s=*impl_;Bytes output;
    if(s.finished)throw Error("capture already finished; reset before appending input");
    if(samples.empty())return {};
    cancelled(stop);
    if(s.input==Impl::Input::integrated)throw Error("cannot mix PCM and integrated input; reset receiver first");
    s.input=Impl::Input::pcm;
    // Prefix sums let each timing candidate integrate its exact chip edges
    // without repeating the oscillator work for every candidate and sample.
    // Solving shorter arbitrary quanta first mixes adjacent symbol values;
    // solve the full candidate symbol's Gram system only after de-spreading.
    constexpr std::size_t block_size=256;
    std::array<PcmProjection,block_size+1> prefix{};
    const auto step=std::polar(1.,tau*s.config.carrier_hz/s.config.sample_rate);
    for(std::size_t offset=0;offset<samples.size();) {
        cancelled(stop);
        const auto count=std::min(block_size,samples.size()-offset);
        prefix[0]={};
        for(std::size_t i=0;i<count;++i) {
            const auto sample=samples[offset+i];
            if(!std::isfinite(sample))throw Error("non-finite audio sample");
            const auto c=s.oscillator.real(),q=-s.oscillator.imag();s.oscillator*=step;
            prefix[i+1]=prefix[i];prefix[i+1].add({sample*c,sample*q,c*c,q*q,c*q});
        }
        auto bytes=s.feed_pcm(count,[&](std::uint64_t begin,std::uint64_t length){
            return prefix[static_cast<std::size_t>(begin+length)]-prefix[static_cast<std::size_t>(begin)];
        },true,stop);
        output.insert(output.end(),bytes.begin(),bytes.end());offset+=count;
    }
    return output;
}
Bytes StreamingReceiver::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return {};
    Bytes output;
    if(s.input==Impl::Input::pcm)output=s.feed_pcm(s.symbol,[&](std::uint64_t begin,std::uint64_t length){
        return PcmProjection::silence(s.position+begin,length,s.config);
    },false,stop);
    else output=s.feed({{},s.symbol},true,stop);
    s.finished=true;
    return output;
}
SymbolObservation add_awgn(SymbolObservation observation,double sample_snr_db,std::mt19937_64& random) {
    if(!observation.sample_count || !std::isfinite(sample_snr_db) || sample_snr_db < -300 || sample_snr_db > 300)throw Error("invalid integrated AWGN configuration");
    const auto sigma=std::sqrt(2*nominal_signal_power*std::pow(10.,-sample_snr_db/10)/static_cast<double>(observation.sample_count));
    std::normal_distribution<double> normal(0,sigma);
    observation.value+=Complex{normal(random),normal(random)};return observation;
}
}
