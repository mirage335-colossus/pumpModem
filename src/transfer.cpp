#include "datapump/transfer.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/channel.hpp"
#include "datapump/compression.hpp"
#include "datapump/boundary_sync.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <string>
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
    if ((options.modem.scramble || options.modem.dsss) && !options.key)
        throw Error("encrypted spreading requires a symmetric key");
    if (options.fec != FecMode::off && options.fec != FecMode::rs20 && options.fec != FecMode::rs60)
        throw Error("unknown Reed-Solomon mode");
    if (!std::isfinite(options.repeat_policy.maximum_seconds) || options.repeat_policy.maximum_seconds < 0)
        throw Error("repeatable content airtime limit must be finite and nonnegative");
    (void)packet_workspace_limit(options.content_limit);
    if (options.dsp_workspace_bytes < 256*1024) throw Error("streaming DSP workspace must be at least 256 KiB");
}
std::size_t packet_budget(const Options& options) { return packet_workspace_limit(options.content_limit); }
void validate_message(const Message& message, const Options& options) {
    validate(options);
    if (message.data.size() > options.content_limit)
        throw Error("message exceeds content limit");
}
Options binary_options(std::span<const std::uint8_t> bits,const Options& options) {
    auto result=options;
    // These controls describe packets and cannot alter a raw bit sequence.
    result.fec=FecMode::off;result.compression=false;result.repeat_policy={};
    validate(result);
    if(bits.empty())throw Error("raw binary transmission requires at least one bit");
    if(bits.size()>result.content_limit)throw Error("raw binary input exceeds content limit");
    if(std::any_of(bits.begin(),bits.end(),[](auto bit){return bit>1;}))
        throw Error("raw binary input elements must be zero or one");
    return result;
}
Estimate estimate_encoded(const Message& message, const Options& options, std::size_t frame_size) {
    // Tiny actual packets use no FEC. A larger packet's hypothetical empty
    // baseline must retain that larger packet's effective code, otherwise
    // metadata parity would be wrongly charged as forwarded content.
    const auto overhead=packet_empty_layout(message,options.fec).wire_bytes;
    Estimate result;
    result.packet_bytes = frame_size;
    result.content_bytes = frame_size > overhead ? frame_size - overhead : 0;
    const auto symbols = modem::payload_symbol_count(frame_size, options.modem);
    const auto overhead_symbols = modem::payload_symbol_count(overhead, options.modem);
    const auto symbol_seconds = static_cast<double>(modem::symbol_sample_count(options.modem)) / options.modem.sample_rate;
    result.packet_seconds = static_cast<double>(symbols) * symbol_seconds;
    result.content_seconds = static_cast<double>(symbols > overhead_symbols ? symbols - overhead_symbols : 0) * symbol_seconds;
    result.repeatable_allowed = message.data.size() <= options.repeat_policy.minimum_payload_bytes ||
                               result.content_seconds <= options.repeat_policy.maximum_seconds;
    const auto training_bytes = modem::preamble(options.modem).size();
    if (frame_size > std::numeric_limits<std::size_t>::max() - training_bytes)
        throw Error("transmission length exceeds platform size limit");
    const auto wire_bytes = frame_size + training_bytes;
    result.total_seconds = static_cast<double>(modem::training_sample_count(options.modem))/options.modem.sample_rate + result.packet_seconds;
    try {
        result.waveform_samples = modem::waveform_sample_count(wire_bytes, options.modem);
        result.batch_memory_supported = modem::memory_supported(wire_bytes, training_bytes, options.modem);
    } catch (const Error&) { return result; }
    try {
        // Complete-frame matching retains measured pattern symbols under the
        // DSP budget, separately from content and a hypothetical PCM vector.
        modem::StreamingReceiver probe(options.modem,options.dsp_workspace_bytes);
        result.memory_supported = probe.frame_supported(frame_size);
    } catch (const Error&) {
        result.memory_supported = false;
    }
    return result;
}
void enforce_repeat_policy(const Message& message, const Options& options, std::size_t frame_size) {
    if (message.repeatable && !estimate_encoded(message, options, frame_size).repeatable_allowed)
        throw Error("repeatable content exceeds airtime limit");
}
Bytes epoch_context(const Bytes& data, std::uint64_t timestamp) {
    Bytes bound{'D', 'P', '-', 'E', 'P', 'O', 'C', 'H', 1};
    for (int i = 7; i >= 0; --i) bound.push_back(static_cast<std::uint8_t>(timestamp >> (i * 8)));
    bound.insert(bound.end(), data.begin(), data.end());
    return bound;
}

}

std::size_t packet_workspace_limit(std::size_t content_limit) {
    // The encoder reserves two coded copies, two uncoded bodies, input and
    // fixed scratch. RS60 needs just over six input sizes; eight also covers
    // framing metadata, small-block rounding and the decoder's copies.
    if(!content_limit || content_limit>(std::numeric_limits<std::size_t>::max()-65568)/8)
        throw Error("invalid content limit");
    return content_limit*8+65536;
}

PacketOptions packet_options(const Options& input_options, std::uint64_t timestamp) {
    const auto options=effective_options(input_options);
    PacketOptions result;
    result.fec = options.fec;
    result.compression = options.compression;
    if (options.key) {
        result.authenticator = [key = *options.key, timestamp](const Bytes& data) {
            return key.mac(epoch_context(data, timestamp));
        };
        result.verifier = [key = *options.key, timestamp](const Bytes& data, const Bytes& tag) {
            return key.verify(epoch_context(data, timestamp), tag);
        };
    }
    return result;
}

modem::Config seeded_config(const Options& input_options, std::uint64_t timestamp) {
    const auto options=effective_options(input_options);
    validate(options);
    auto result = options.modem;
    result.stream_epoch=timestamp;
    result.data_key=options.key;
    if (options.key && result.scramble) {
        const auto seed = options.key->stream(StreamPurpose::Scrambler, timestamp, 0, result.spreading_seed.size());
        std::copy(seed.begin(), seed.end(), result.spreading_seed.begin());
    }
    if (options.key && result.dsss) {
        const auto seed = options.key->stream(StreamPurpose::Dsss, timestamp, 0, result.dsss_seed.size());
        std::copy(seed.begin(), seed.end(), result.dsss_seed.begin());
    }
    return result;
}

Estimate estimate(const Message& message, const Options& input_options) {
    const auto options=effective_options(input_options);
    return estimate(message,options,nullptr);
}

Estimate estimate(const Message& message,const Options& input_options,PacketLayout* layout) {
    const auto options=effective_options(input_options);
    validate_message(message, options);
    const auto bits=message_wire_bits(message,options);
    if(layout) {
        *layout={};layout->original_bytes=message.data.size();
        if(message.kind!=MessageKind::text || message.data.size()>=16)
            *layout=packet_layout(encode_packet(message,packet_options(options,options.timestamp),packet_budget(options)),packet_budget(options));
    }
    if(bits.empty()) { Estimate empty;empty.memory_supported=empty.batch_memory_supported=empty.repeatable_allowed=true;return empty; }
    auto value=options;value.content_limit=std::max(value.content_limit,bits.size());
    auto result=estimate_binary(bits,value);
    result.content_bytes=message.data.size();
    result.repeatable_allowed=message.data.size()<=options.repeat_policy.minimum_payload_bytes || result.content_seconds<=options.repeat_policy.maximum_seconds;
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
    const auto samples=hardware_samples+content_samples;
    Estimate result;
    result.content_bytes=result.packet_bytes=bits.size()/8+(bits.size()%8!=0);
    result.content_seconds=result.packet_seconds=static_cast<double>(content_samples)/value.modem.sample_rate;
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

Bytes message_bits(const Message& message,const Options& input_options) {
    const auto options=effective_options(input_options);
    validate_message(message,options);
    if(message.kind==MessageKind::text && message.data.size()<16)
        return compression::encode_short_bits(message.data,packet_budget(options));
    const auto packet=encode_packet(message,packet_options(options,options.timestamp),packet_budget(options));
    if(packet.size()>packet_budget(options)/8)throw Error("pattern packet bit storage exceeds content workspace");
    Bytes bits;bits.reserve(packet.size()*8);
    for(auto byte:packet)for(unsigned i=0;i<8;++i)bits.push_back(static_cast<std::uint8_t>((byte>>(7-i))&1));
    return bits;
}
Bytes message_wire_bits(const Message& message,const Options& input_options) {
    const auto options=effective_options(input_options);
    auto bits=message_bits(message,options);
    if(message.kind!=MessageKind::text || message.data.size()>=16)
        bits=boundary_sync::insert(bits,packet_budget(options));
    auto context=options;context.content_limit=std::max(context.content_limit,bits.size());
    xor_binary_bits(bits,context);
    return bits;
}
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message& message,const Options& input_options) {
    const auto options=effective_options(input_options);
    auto bits=message_wire_bits(message,options);
    if(message.repeatable && message.data.size()>options.repeat_policy.minimum_payload_bytes &&
       static_cast<double>(bits.size())*modem::symbol_seconds(options.modem)>options.repeat_policy.maximum_seconds)
        throw Error("repeatable content exceeds airtime limit");
    // The entire stream, including alignment markers, is already masked.
    // Passing through binary_transmitter would apply that mask twice.
    return std::make_unique<modem::StreamingTransmitter>(modem::RawBits{std::move(bits)},
        seeded_config(options,options.timestamp),options.dsp_workspace_bytes/4);
}
Received interpret_pattern(modem::PatternBurst burst,const Options& input_options,std::uint64_t timestamp,modem::Diagnostics diagnostics) {
    const auto options=effective_options(input_options);
    auto context=options;context.timestamp=timestamp;
    context.content_limit=std::max(context.content_limit,burst.bits.size());
    if(burst.first_stream_symbol>std::numeric_limits<std::size_t>::max())throw Error("received pattern stream index exceeds bit address space");
    Received result;result.timestamp=timestamp;result.diagnostics=std::move(diagnostics);
    result.diagnostics.pattern_score=burst.score;
    result.diagnostics.sample_offset=static_cast<std::size_t>(burst.first_sample);
    result.raw_bits=std::move(burst.bits);result.packet_validated=false;
    xor_binary_bits(result.raw_bits,context,static_cast<std::size_t>(burst.first_stream_symbol));
    if(options.key && burst.first_stream_symbol)return result;
    // Recovery has one fixed cadence anchored to this burst, never a search
    // for embedded packets. Decryption and its constellation-supplied stream
    // position are unchanged; only the decrypted byte grouping is recovered.
    std::optional<Bytes> candidate;
    try { candidate=boundary_sync::recover(result.raw_bits,packet_budget(options)); }
    catch(const Error&) {} // Invalid recovery leaves only raw evidence.
    // Content grammar is interpreted only after pattern acquisition. A bad
    // packet never changes the winning signal timing or discards its raw bits.
    if(candidate && candidate->size()%8==0) {
        Bytes bytes(candidate->size()/8);
        for(std::size_t i=0;i<candidate->size();++i)bytes[i/8]|=static_cast<std::uint8_t>((*candidate)[i]<<(7-i%8));
        try {
            auto packet=decode_packet(bytes,packet_options(options,timestamp),packet_budget(options));
            if(packet.consumed_bytes==bytes.size() && packet.message.data.size()<=options.content_limit) {
                result.packet=std::move(packet);result.packet_validated=true;return result;
            }
        } catch(const Error&) {}
    }
    if(result.raw_bits.size()>15*13)return result;
    try {
        auto decoded=compression::decode_short_bits(result.raw_bits,std::min<std::size_t>(15,options.content_limit));
        result.packet.message.data=std::move(decoded);
    } catch(const Error&) {} // Preserve uninterpreted or truncated raw bits.
    return result;
}

void xor_binary_bits(std::span<std::uint8_t> bits,const Options& input_options,std::size_t bit_offset) {
    const auto options=effective_options(input_options);
    if(bits.empty())return;
    const auto value=binary_options(bits,options);
    if(bits.size()>std::numeric_limits<std::size_t>::max()-bit_offset)
        throw Error("raw binary data-stream offset overflow");
    if(value.key) {
        // A received fragment can start at any bit, not only a byte boundary.
        // Keep crypto scratch bounded while preserving the exact TX stream.
        std::size_t offset=0;
        while(offset<bits.size()) {
            const auto position=bit_offset+offset,skip=position%8;
            const auto count=std::min<std::size_t>(16384*8-skip,bits.size()-offset);
            const auto mask=value.key->stream(StreamPurpose::Data,value.timestamp,position/8,(skip+count)/8+((skip+count)%8!=0));
            for(std::size_t i=0;i<count;++i)
                bits[offset+i]^=static_cast<std::uint8_t>((mask[(skip+i)/8]>>(7-(skip+i)%8))&1);
            offset+=count;
        }
    }
}

Bytes pack(const Message& message, const Options& input_options) {
    const auto options=effective_options(input_options);
    validate_message(message, options);
    auto frame = encode_packet(message, packet_options(options, options.timestamp), packet_budget(options));
    enforce_repeat_policy(message, options, frame.size());
    return options.key ? options.key->xor_data(frame, options.timestamp) : std::move(frame);
}

DecodedPacket unpack(const Bytes& wire, const Options& input_options) {
    const auto options=effective_options(input_options);
    validate(options);
    if (wire.size() > packet_budget(options)) throw Error("packet input exceeds content budget");
    const auto plaintext=options.key?options.key->xor_data(wire,options.timestamp):wire;
    auto packet=decode_packet(plaintext,packet_options(options,options.timestamp),packet_budget(options));
    if(packet.message.data.size()>options.content_limit)throw Error("received message exceeds content limit");
    return packet;
}

Bytes transmission_wire(const Message& message,const Options& input_options) {
    const auto options=effective_options(input_options);
    if(message.kind==MessageKind::text && message.data.size()<16)
        throw Error("short pattern text has an exact bit length; use message_transmitter");
    const auto bits=message_wire_bits(message,options);
    if(bits.size()%8)throw Error("pattern wire has an exact partial byte; use message_transmitter");
    Bytes bytes(bits.size()/8);
    for(std::size_t i=0;i<bits.size();++i)bytes[i/8]|=static_cast<std::uint8_t>(bits[i]<<(7-i%8));
    return bytes;
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
    std::optional<Received> best;double best_score=-1;
    std::string last_error;
    auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(auto epoch:drift_candidates(options.timestamp,options.search_seconds,options.key.has_value())) {
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto value=options;value.modem=profile;
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(epoch)-static_cast<double>(options.timestamp)+
            static_cast<double>(modem::training_sample_count(profile))/profile.sample_rate;
        search.bit_limit=packet_budget(value);
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min<std::size_t>(4096,samples.size()-offset);decoder.push(samples.subspan(offset,count),stop);offset+=count;
            for(auto& burst:decoder.take_bursts())if(burst.score>best_score) {
                best_score=burst.score;best=interpret_pattern(std::move(burst),value,epoch,decoder.diagnostics());
            }
        }
        decoder.finish(stop);
        for(auto& burst:decoder.take_bursts())if(burst.score>best_score) {
            best_score=burst.score;best=interpret_pattern(std::move(burst),value,epoch,decoder.diagnostics());
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
    std::optional<Received> best;double best_score=-1;
    std::string last_error;
    const auto center=channel.receiver_timestamp.value_or(options.timestamp);
    const auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(auto epoch:drift_candidates(center,options.search_seconds,options.key.has_value())) {
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto source=message_transmitter(message,options);auto value=options;value.modem=profile;
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(epoch)-static_cast<double>(center)+
            static_cast<double>(modem::training_sample_count(profile))/profile.sample_rate;
        search.bit_limit=packet_budget(value);
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        modem::SampledSimulationChannel impairments(config,channel);std::array<float,2048> samples{};
        std::size_t preview_count=0;
        const auto harvest=[&] {
            for(auto& burst:decoder.take_bursts())if(burst.score>best_score) {
                best_score=burst.score;best=interpret_pattern(std::move(burst),value,epoch,decoder.diagnostics());
                best->diagnostics.waveform.assign(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(preview_count));
            }
        };
        while(const auto count=impairments.read(*source,samples,stop)){preview_count=count;decoder.push(std::span(samples).first(count),stop);harvest();}
        auto trailing=2*modem::symbol_sample_count(profile);
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
