#include "datapump/transfer.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/channel.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <utility>

namespace datapump::transfer {
namespace {
Options effective_options(const Options& input) {
    auto result=input;
    if(result.modem.spreading_mode==modem::SpreadingMode::tone) {
        result.key.reset();result.modem.data_key.reset();
        result.modem.scramble=false;result.modem.dsss=false;
        result.modem.spreading_seed.fill(0);result.modem.dsss_seed.fill(0);
        if(result.automatic_receive_profiles && !tuning::tone_mode(result.receive_pattern_mode))
            result.receive_pattern_mode=tuning::PatternMode::auto_tone;
    } else if(result.key) {
        // Encrypted on-air data must also select private pattern waveforms.
        result.modem.scramble=true;
    }
    return result;
}
void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("transfer cancelled");
}

void validate(const Options& options) {
    modem::validate(options.modem);
    if(options.capture_epoch && (!std::isfinite(*options.capture_epoch) || *options.capture_epoch<0 ||
       static_cast<long double>(*options.capture_epoch)>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())))
        throw Error("invalid capture epoch");
    if((options.modem.scramble || options.modem.dsss) && !options.key)
        throw Error("encrypted spreading requires a symmetric key");
    (void)interval_parity_bytes(options.fec);
    (void)source_storage_limit(options.content_limit);
    if(options.dsp_workspace_bytes<256*1024)throw Error("streaming DSP workspace must be at least 256 KiB");
}
void validate_message(const Message& message,const Options& options) {
    validate(options);
    if(message.data.size()>options.content_limit)throw Error("message exceeds content limit");
}
Options binary_options(std::span<const std::uint8_t> bits,const Options& options) {
    auto result=options;result.fec=FecMode::off;result.compression=false;
    validate(result);
    if(bits.empty() || bits.size()>result.content_limit)throw Error("invalid raw bit input size");
    if(std::any_of(bits.begin(),bits.end(),[](auto b){return b>1;}))throw Error("raw bits must be zero or one");
    return result;
}
IntervalOptions interval_options(const Options& options,std::uint64_t first_symbol) {
    IntervalOptions result;result.fec=options.fec;
    if(options.key) {
        const auto address=modem::symbol_stream_address(options.timestamp,options.modem.stream_phase_samples,
            first_symbol,modem::symbol_sample_count(options.modem),options.modem.sample_rate);
        Bytes domain{'D','P','-','I','N','T','E','R','V','A','L',2,
            static_cast<std::uint8_t>(options.fec),static_cast<std::uint8_t>(options.compression)};
        for(auto value:{address.epoch,address.ordinal})
            for(int i=7;i>=0;--i)domain.push_back(static_cast<std::uint8_t>(value>>(8*i)));
        result.authenticator=[key=*options.key,domain](const Bytes& data) {
            auto context=domain;context.insert(context.end(),data.begin(),data.end());return key.mac(context);
        };
        result.verifier=[key=*options.key,domain](const Bytes& data,const Bytes& tag) {
            auto context=domain;context.insert(context.end(),data.begin(),data.end());return key.verify(context,tag);
        };
    }
    return result;
}
bool better_reception(const Received& candidate,const Received& current) {
    if(candidate.content_validated!=current.content_validated)return candidate.content_validated;
    if(candidate.content_validated) {
        // A later acquisition can authenticate a shorter suffix of a stream.
        // Prefer the widest recovered source before comparing radio scores.
        if(candidate.content.consumed_bytes!=current.content.consumed_bytes)
            return candidate.content.consumed_bytes>current.content.consumed_bytes;
        if(candidate.content.message.data.size()!=current.content.message.data.size())
            return candidate.content.message.data.size()>current.content.message.data.size();
        if(candidate.observed_bits!=current.observed_bits)return candidate.observed_bits>current.observed_bits;
    }
    const auto score=candidate.diagnostics.pattern_score.value_or(-1),prior=current.diagnostics.pattern_score.value_or(-1);
    if(score!=prior)return score>prior;
    return candidate.stream_complete || !current.stream_complete;
}
Bytes encoded_intervals(const Message& message,const Options& options,StreamLayout* layout=nullptr) {
    validate_message(message,options);
    const auto capacity=interval_data_bytes(options.fec,options.key.has_value());
    auto source=encode_source(message.data,capacity,options.compression,source_storage_limit(options.content_limit));
    const auto count=source.size()/capacity;
    if(count>Bytes{}.max_size()/stream_interval_bytes)throw Error("encoded source exceeds address space");
    Bytes wire;wire.reserve(count*stream_interval_bytes);
    for(std::size_t i=0;i<count;++i) {
        const auto first=i*(boundary_sync::marker_bits+boundary_sync::interval_bits)+boundary_sync::marker_bits;
        auto block=encode_interval(std::span(source).subspan(i*capacity,capacity),interval_options(options,first));
        wire.insert(wire.end(),block.begin(),block.end());
    }
    if(layout) {
        layout->fec=options.fec;layout->compressed=options.compression;layout->authenticated=options.key.has_value();
        layout->source_bytes=message.data.size();layout->encoded_source_bytes=source.size();
        layout->wire_bytes=wire.size();layout->intervals=count;layout->data_bytes_per_interval=capacity;
        layout->source_bytes_per_interval=source_bytes_per_interval(capacity,options.compression);
        layout->parity_bytes_per_interval=interval_parity_bytes(options.fec);
        layout->integrity_bytes_per_interval=options.key?stream_mac_bytes:0;
    }
    return wire;
}
}
std::size_t source_storage_limit(std::size_t content_limit) {
    if(!content_limit || content_limit>(Bytes{}.max_size()-4096)/2)throw Error("invalid content limit");
    return content_limit*2+4096;
}
std::size_t pattern_bit_limit(std::size_t content_limit) {
    const auto limit=source_storage_limit(content_limit);
    if(limit>(Bytes{}.max_size()-boundary_sync::interval_bits+1)/32)throw Error("source bit limit exceeds address space");
    const auto intervals=(limit*32+boundary_sync::interval_bits-1)/boundary_sync::interval_bits;
    return boundary_sync::encoded_size(intervals*boundary_sync::interval_bits);
}
modem::Config seeded_config(const Options& input_options, std::uint64_t timestamp) {
    const auto options=effective_options(input_options);
    validate(options);
    auto result = options.modem;
    result.stream_epoch=timestamp;
    result.data_key=options.key;
    // Stable, purpose-separated roots. PatternCode selects the epoch at each
    // symbol start; a later independently acquired epoch must not depend on
    // the first timestamp of the original message.
    if (options.key && result.scramble) {
        const auto seed = options.key->stream(StreamPurpose::Scrambler, 0, 0, result.spreading_seed.size());
        std::copy(seed.begin(), seed.end(), result.spreading_seed.begin());
    }
    if (options.key && result.dsss) {
        const auto seed = options.key->stream(StreamPurpose::Dsss, 0, 0, result.dsss_seed.size());
        std::copy(seed.begin(), seed.end(), result.dsss_seed.begin());
    }
    return result;
}


Estimate estimate(const Message& message,const Options& options) {return estimate(message,options,nullptr);}
Estimate estimate(const Message& message,const Options& input,StreamLayout* layout) {
    const auto options=effective_options(input);
    auto coded=encoded_intervals(message,options,layout);
    Bytes bits;bits.reserve(boundary_sync::encoded_size(coded.size()*8));
    for(auto byte:coded)for(unsigned i=0;i<8;++i)bits.push_back((byte>>(7-i))&1);
    bits=boundary_sync::insert(bits,pattern_bit_limit(options.content_limit));
    auto context=options;context.content_limit=std::max(options.content_limit,bits.size());
    auto result=estimate_binary(bits,context);result.content_bytes=message.data.size();result.coded_bytes=coded.size();
    result.repeatable_allowed=message.data.size()<=options.repeat_policy.minimum_payload_bytes ||
        result.content_seconds<=options.repeat_policy.maximum_seconds;
    return result;
}
Estimate estimate_binary(std::span<const std::uint8_t> bits,const Options& input_options) {
    const auto options=effective_options(input_options);
    const auto value=binary_options(bits,options);
    const auto symbols=bits.size();
    const auto symbol_samples=modem::symbol_sample_count(value.modem);
    if(symbols>std::numeric_limits<std::uint64_t>::max()/symbol_samples)
        throw Error("transmission duration exceeds 64-bit sample counter");
    const auto content_samples=static_cast<std::uint64_t>(symbols)*symbol_samples;
    const auto hardware_samples=modem::training_sample_count(value.modem);
    if(hardware_samples>std::numeric_limits<std::uint64_t>::max()-content_samples)
        throw Error("transmission duration exceeds 64-bit sample counter");
    const auto padding=modem::pattern_pulse_padding_samples(value.modem);
    const auto unpadded=hardware_samples+content_samples;
    if(padding>(std::numeric_limits<std::uint64_t>::max()-unpadded)/2)
        throw Error("pulse tails exceed 64-bit sample counter");
    const auto samples=unpadded+2*padding;
    Estimate result;result.wire_bits=bits.size();
    result.content_bytes=result.coded_bytes=bits.size()/8+(bits.size()%8!=0);
    result.content_seconds=result.coded_seconds=static_cast<double>(content_samples)/value.modem.sample_rate;
    result.total_seconds=static_cast<double>(samples)/value.modem.sample_rate;
    result.repeatable_allowed=true;
    std::size_t scratch=std::numeric_limits<std::size_t>::max();

    // PatternCode seeks through fixed stream caches; symbol duration must
    // not be charged as a retained chip array or sampled waveform.
    try {
        modem::StreamingTransmitter probe(modem::RawBits{Bytes(bits.begin(),bits.end())},
            seeded_config(value,value.timestamp),std::max(value.modem.memory_limit,value.dsp_workspace_bytes/4));
        scratch=probe.working_bytes();
        result.memory_supported=scratch<=value.dsp_workspace_bytes/4;
    } catch(const Error&) { result.memory_supported=false; }

    if(samples<=std::numeric_limits<std::size_t>::max()) {
        result.waveform_samples=static_cast<std::size_t>(samples);
        const auto budget=value.modem.memory_limit;
        result.batch_memory_supported=scratch<=budget && result.content_bytes<=budget-scratch &&
            samples<=(budget-scratch-result.content_bytes)/sizeof(float);
    }
    return result;
}

std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits,const Options& input_options) {
    const auto options=effective_options(input_options);
    const auto value=binary_options(bits,options);
    modem::RawBits raw{Bytes(bits.begin(),bits.end())};
    xor_binary_bits(raw.bits,value);
    return std::make_unique<modem::StreamingTransmitter>(std::move(raw),seeded_config(value,value.timestamp),value.dsp_workspace_bytes/4);
}


Bytes message_bits(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    const auto coded=encoded_intervals(message,options);
    Bytes bits;bits.reserve(coded.size()*8);
    for(auto byte:coded)for(unsigned i=0;i<8;++i)bits.push_back((byte>>(7-i))&1);
    return bits;
}
Bytes message_wire_bits(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    auto bits=boundary_sync::insert(message_bits(message,options),pattern_bit_limit(options.content_limit));
    auto context=options;context.content_limit=std::max(context.content_limit,bits.size());xor_binary_bits(bits,context);
    return bits;
}
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    auto bits=message_wire_bits(message,options);
    if(message.repeatable && message.data.size()>options.repeat_policy.minimum_payload_bytes &&
       static_cast<double>(bits.size())*modem::symbol_seconds(options.modem)>options.repeat_policy.maximum_seconds)
        throw Error("repeatable content exceeds airtime limit");
    return std::make_unique<modem::StreamingTransmitter>(modem::RawBits{std::move(bits)},
        seeded_config(options,options.timestamp),options.dsp_workspace_bytes/4);
}
void xor_binary_bits(std::span<std::uint8_t> bits,const Options& input_options,std::size_t bit_offset) {
    const auto options=effective_options(input_options);
    if(bits.empty())return;
    const auto value=binary_options(bits,options);
    if(bits.size()>std::numeric_limits<std::size_t>::max()-bit_offset)
        throw Error("raw binary data-stream offset overflow");
    if(value.key) {
        // Data and pattern positions use the same symbol-start clock. A new
        // second restarts its local bit positions even after missed symbols.
        // Keep a bounded cache; never allocate a duration-sized keystream.
        constexpr std::size_t cache_size=512;
        Bytes mask;
        std::uint64_t cached_epoch=0,cached_begin=0;
        bool cached=false;
        const auto samples=modem::symbol_sample_count(value.modem);
        std::size_t offset=0;
        while(offset<bits.size()) {
            const auto address=modem::symbol_stream_address(value.timestamp,value.modem.stream_phase_samples,
                bit_offset+offset,samples,value.modem.sample_rate);
            const auto byte=address.ordinal/8,begin=byte-byte%cache_size;
            if(!cached || address.epoch!=cached_epoch || begin!=cached_begin) {
                mask=value.key->stream(StreamPurpose::Data,address.epoch,begin,cache_size);
                cached_epoch=address.epoch;cached_begin=begin;cached=true;
            }
            bits[offset]^=static_cast<std::uint8_t>((mask[static_cast<std::size_t>(byte-begin)]>>(7-address.ordinal%8))&1);
            ++offset;
        }
    }
}


Bytes transmission_wire(const Message& message,const Options& options) {
    const auto bits=message_wire_bits(message,options);Bytes bytes(bits.size()/8);
    for(std::size_t i=0;i<bits.size();++i)bytes[i/8]|=static_cast<std::uint8_t>(bits[i]<<(7-i%8));
    return bytes;
}

struct StreamReceiver::Impl {
    struct FileCloser { void operator()(std::FILE* file)const {if(file)std::fclose(file);} };
    struct State {
        boundary_sync::Collector collector;
        std::unique_ptr<std::FILE,FileCloser> spool;
        Options options;
        Received result;
        std::uint64_t next_symbol=0;
        std::size_t stored=0;
        bool failed=false,seen_marker=false;
        State(const Options& value,const modem::PatternBurst& burst)
            :collector(burst.first_stream_symbol,0,true),options(value),next_symbol(burst.first_stream_symbol) {
            options.modem.stream_phase_samples=burst.stream_phase_samples;
            result.timestamp=options.timestamp;
            const std::array<std::uint64_t,2> identity{options.timestamp,burst.stream_first_sample};
            for(std::size_t j=0;j<2;++j)for(std::size_t i=0;i<8;++i)
                result.content.message.local_id[j*8+i]=static_cast<std::uint8_t>(identity[j]>>(8*i));
        }
    };
    Options options;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::unique_ptr<State>> states;
    std::shared_ptr<ReceiveStorageQuota> quota;
    explicit Impl(Options value,std::uint64_t timestamp,std::shared_ptr<ReceiveStorageQuota> budget)
        :options(effective_options(value)),quota(std::move(budget)) {
        options.timestamp=timestamp;validate(options);
        if(!quota)quota=std::make_shared<ReceiveStorageQuota>(ReceiveStorageQuota{source_storage_limit(options.content_limit),0});
        if(quota->used>quota->limit)throw Error("Invalid receive source storage quota");
    }
    ~Impl() {for(auto& [id,state]:states)quota->used-=state->stored;}

    void fail(State& state,const std::string& error) {
        state.failed=true;state.result.error=error;
        quota->used-=state.stored;state.stored=0;state.spool.reset();
    }
    void interval(State& state,const boundary_sync::Interval& interval) {
        state.seen_marker|=interval.marker_recognized;
        if(!state.seen_marker){fail(state,"alignment marker not established");return;}
        try {
            std::array<std::size_t,stream_interval_bytes> erasures{};std::size_t count=0;
            for(std::size_t i=0;i<interval.erasures.size();++i)if(interval.erasures[i])erasures[count++]=i;
            auto decoded=decode_interval(interval.bytes,interval_options(state.options,interval.first_stream_symbol),
                std::span(erasures).first(count));
            auto& content=state.result.content;
            content.corrected_bytes+=decoded.corrected_bytes;content.consumed_bytes+=stream_interval_bytes;
            content.authenticated=decoded.authenticated;
            if(!content.pre_fec_accuracy && content.consumed_bytes==stream_interval_bytes)
                content.pre_fec_accuracy=StreamBitAccuracy{};
            if(content.pre_fec_accuracy && decoded.pre_fec_accuracy) {
                content.pre_fec_accuracy->received_data_bits+=decoded.pre_fec_accuracy->received_data_bits;
                content.pre_fec_accuracy->corrected_data_bits+=decoded.pre_fec_accuracy->corrected_data_bits;
            } else content.pre_fec_accuracy.reset();
            if(state.failed)return;
            const auto limit=quota->limit;
            if(quota->used>limit || decoded.data.size()>limit-quota->used){fail(state,"received source storage quota exhausted");return;}
            if(!state.spool)state.spool.reset(std::tmpfile());
            if(!state.spool || std::fwrite(decoded.data.data(),1,decoded.data.size(),state.spool.get())!=decoded.data.size()) {
                fail(state,"cannot store received source");return;
            }
            state.stored+=decoded.data.size();quota->used+=decoded.data.size();
        } catch(const Error& error) {fail(state,error.what());}
    }
    Received push(modem::PatternBurst burst,modem::Diagnostics diagnostics) {
        if(burst.missing_slots) {
            if(!burst.bits.empty())throw Error("mixed bit and missing-run event");
            auto remaining=burst.missing_slots;
            const auto complete=burst.complete;
            burst.missing_slots=0;burst.complete=false;
            Received result;
            while(remaining) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,2048));
                burst.bits.assign(count,modem::missing_pattern_bit);
                result=push(burst,diagnostics);
                if(count>std::numeric_limits<std::uint64_t>::max()-burst.first_stream_symbol)
                    throw Error("missing-run symbol counter exhausted");
                burst.first_stream_symbol+=count;remaining-=count;
            }
            if(complete) {burst.bits.clear();burst.complete=true;return push(std::move(burst),std::move(diagnostics));}
            return result;
        }
        const auto id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
        auto it=states.find(id);
        if(it==states.end()) {
            if(states.size()>=16) {
                Received unavailable;unavailable.error="receiver content candidate quota exhausted";
                unavailable.stream_complete=burst.complete;return unavailable;
            }
            it=states.emplace(id,std::make_unique<State>(options,burst)).first;
        }
        auto& state=*it->second;
        state.options.modem.stream_phase_samples=burst.stream_phase_samples;
        state.result.diagnostics=std::move(diagnostics);state.result.diagnostics.pattern_score=burst.score;
        state.result.diagnostics.sample_offset=static_cast<std::size_t>(burst.stream_first_sample);
        if(burst.first_stream_symbol!=state.next_symbol && !burst.bits.empty())fail(state,"noncontiguous received source");
        auto& bits=burst.bits;
        std::size_t begin=0;
        for(std::size_t i=0;i<=bits.size();++i) {
            if(i<bits.size() && bits[i]!=modem::missing_pattern_bit)continue;
            if(i>begin) {
                auto context=state.options;context.content_limit=std::max(context.content_limit,i-begin);
                xor_binary_bits(std::span(bits).subspan(begin,i-begin),context,
                    static_cast<std::size_t>(burst.first_stream_symbol)+begin);
            }
            if(i<bits.size())++state.result.missing_symbols;
            begin=i+1;
        }
        if(bits.size()>std::numeric_limits<std::size_t>::max()-state.result.observed_bits)fail(state,"source observation counter exhausted");
        else state.result.observed_bits+=bits.size();
        if(bits.size()>std::numeric_limits<std::uint64_t>::max()-burst.first_stream_symbol)fail(state,"source symbol counter exhausted");
        else state.next_symbol=burst.first_stream_symbol+bits.size();
        const auto preview=std::min<std::size_t>(4096-state.result.raw_bits.size(),bits.size());
        for(std::size_t i=0;i<preview;++i)state.result.raw_bits.push_back(bits[i]==1?1:0);
        {
            try {
                const auto sink=[&](const boundary_sync::Interval& interval){this->interval(state,interval);};
                state.collector.push(bits,sink);
                if(burst.complete)state.collector.finish(true,sink);
            } catch(const Error& error){fail(state,error.what());}
        }
        state.result.stream_complete=burst.complete;
        // The sole decompression gate. No EOF, size, codeword or MAC can enter it.
        if(burst.complete && !state.failed && state.seen_marker && state.stored) {
            try {
                Bytes source(state.stored);
                if(std::fflush(state.spool.get()) || std::fseek(state.spool.get(),0,SEEK_SET) ||
                   std::fread(source.data(),1,source.size(),state.spool.get())!=source.size())
                    throw Error("cannot read sealed received source");
                state.result.content.message.data=decode_source(source,
                    interval_data_bytes(options.fec,options.key.has_value()),options.compression,options.content_limit);
                state.result.content.message.filename="received.bin";
                state.result.content_validated=true;
            } catch(const Error& error){fail(state,error.what());}
        }
        if(burst.complete) {
            auto result=std::move(state.result);quota->used-=state.stored;states.erase(it);return result;
        }
        return state.result;
    }
};
StreamReceiver::StreamReceiver(Options options,std::uint64_t timestamp,std::shared_ptr<ReceiveStorageQuota> quota):impl_(std::make_unique<Impl>(std::move(options),timestamp,std::move(quota))){}
StreamReceiver::~StreamReceiver()=default;
StreamReceiver::StreamReceiver(StreamReceiver&&) noexcept=default;
StreamReceiver& StreamReceiver::operator=(StreamReceiver&&) noexcept=default;
Received StreamReceiver::push(modem::PatternBurst burst,modem::Diagnostics diagnostics) {return impl_->push(std::move(burst),std::move(diagnostics));}
std::size_t StreamReceiver::working_bytes()const {
    auto bytes=sizeof(StreamReceiver)+sizeof(Impl)+impl_->options.receive_targets_db_hz.capacity()*sizeof(double);
    for(const auto& [id,state]:impl_->states) {
        const auto& result=state->result;const auto& message=result.content.message;
        // Include map links, the actual diagnostic/preview allocations and
        // the standard I/O buffer reserved by a live temporary file.
        bytes+=sizeof(Impl::State)+sizeof(id)+sizeof(state)+4*sizeof(void*)+
            state->collector.working_bytes()+state->options.receive_targets_db_hz.capacity()*sizeof(double)+
            result.raw_bits.capacity()+result.diagnostics.constellation.capacity()*sizeof(std::complex<double>)+
            result.diagnostics.waveform.capacity()*sizeof(float)+result.error.capacity()+
            message.data.capacity()+message.filename.capacity()+message.callsign.capacity()+message.grid.capacity();
        if(state->spool)bytes+=sizeof(std::FILE)+BUFSIZ;
    }
    return bytes;
}
Received interpret_pattern(modem::PatternBurst burst,const Options& options,std::uint64_t timestamp,modem::Diagnostics diagnostics) {
    StreamReceiver receiver(options,timestamp);return receiver.push(std::move(burst),std::move(diagnostics));
}

std::vector<float> transmit(const Message& message, const Options& input_options, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    auto source=message_transmitter(message,options);
    if(source->total_samples()>options.modem.memory_limit/sizeof(float))throw Error("pattern waveform exceeds batch memory limit; use streaming output");
    std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
    std::size_t position=0;while(position<samples.size())position+=source->read(std::span(samples).subspan(position,std::min<std::size_t>(4096,samples.size()-position)),stop);
    return samples;
}

Received receive(std::span<const float> samples, const Options& input_options, Progress progress, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    validate(options);
    std::optional<Received> best;
    std::string last_error;
    auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(auto epoch:drift_candidates(options.timestamp,options.search_seconds,options.key.has_value())) {
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto value=options;value.modem=profile;
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(epoch)-options.capture_epoch.value_or(static_cast<double>(options.timestamp));
        if(!options.capture_epoch)*search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(profile))+
            static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate;
        search.bit_limit=pattern_bit_limit(value.content_limit);

        search.search_stream_phases=options.key.has_value();
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        StreamReceiver content(value,epoch);
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min<std::size_t>(4096,samples.size()-offset);decoder.push(samples.subspan(offset,count),stop);offset+=count;
            for(auto& burst:decoder.take_bursts()){
                auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
            }
        }
        decoder.finish(stop);
        for(auto& burst:decoder.take_bursts()){
            auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
        }
        } catch(const Error& error){check_cancelled(stop);last_error=error.what();}
    }
    if(best) {
        const auto tail=samples.last(std::min<std::size_t>(2048,samples.size()));
        best->diagnostics.waveform.assign(tail.begin(),tail.end());
        return std::move(*best);
    }
    throw Error("no sufficiently confident pattern in timing search"+(last_error.empty()?std::string{}:": "+last_error));
}

Received simulate(const Message& message, const Options& input_options, const modem::ChannelConfig& channel,
                  Progress progress, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    validate_message(message,options);
    const auto config = seeded_config(options, options.timestamp);
    modem::validate_channel(config,channel);
    std::optional<Received> best;
    std::string last_error;
    const auto center=channel.receiver_timestamp.value_or(options.timestamp);
    const auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(auto epoch:drift_candidates(center,options.search_seconds,options.key.has_value())) {
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto source=message_transmitter(message,options);auto value=options;value.modem=profile;
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(epoch)-static_cast<double>(center)+
            (static_cast<double>(modem::training_sample_count(profile))+
             static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate;
        search.bit_limit=pattern_bit_limit(value.content_limit);

        search.search_stream_phases=options.key.has_value();
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        StreamReceiver content(value,epoch);
        modem::SampledSimulationChannel impairments(config,channel);std::array<float,2048> samples{};
        std::size_t preview_count=0;
        const auto harvest=[&] {
            for(auto& burst:decoder.take_bursts()){
                auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
                best->diagnostics.waveform.assign(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(preview_count));
            }
        };
        while(const auto count=impairments.read(*source,samples,stop)){preview_count=count;decoder.push(std::span(samples).first(count),stop);harvest();}
        auto trailing=modem::pattern_absence_samples(profile)+profile.sample_rate+2*modem::pattern_pulse_padding_samples(profile);
        while(trailing) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,samples.size()));
            auto tail=std::span(samples).first(count);impairments.read_noise(tail,stop);preview_count=count;decoder.push(tail,stop);trailing-=count;harvest();
        }
        decoder.finish(stop);harvest();
        }catch(const Error& error){check_cancelled(stop);last_error=error.what();}
    }
    if(best)return std::move(*best);
    throw Error("no sufficiently confident pattern in simulated timing search"+(last_error.empty()?std::string{}:": "+last_error));
}
}
