#include "datapump/transfer.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/channel.hpp"
#include "datapump/compression.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <utility>

namespace datapump::transfer {
namespace {
constexpr std::uint64_t audio_training_bytes=32;
const Crypto& public_whitening_stream() {
    // Protocol constant, deliberately public. HKDF purpose separation and a
    // 64-bit AES-CTR offset avoid short repeating masks on large transfers.
    static const Crypto stream([] {
        std::array<std::uint8_t,32> seed{};
        constexpr std::string_view domain="DataPump/audio/whitening/v0.5";
        std::copy(domain.begin(),domain.end(),seed.begin());
        return seed;
    }());
    return stream;
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
        modem::StreamingReceiver probe(options.modem,modem::preamble(options.modem),
            options.dsp_workspace_bytes-audio_validation_workspace);
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
modem::StreamingReceiver receiver(const Options& options,std::uint64_t timestamp) {
    const auto config=seeded_config(options,timestamp);
    auto expected=modem::preamble(config);
    if(options.key)expected=options.key->xor_data(expected,timestamp);
    auto validators=audio_validators(options,timestamp);
    return modem::StreamingReceiver(config,std::move(expected),options.dsp_workspace_bytes-audio_validation_workspace,
        std::move(validators.bootstrap),std::move(validators.packet));
}
Received verified(Bytes wire,modem::Diagnostics diagnostics,const Options& options,std::uint64_t timestamp) {
    const auto training=modem::preamble(options.modem).size();
    if(wire.size()<training)throw Error("packet bootstrap not acquired");
    xor_audio_whitening(wire);
    if(options.key)wire=options.key->xor_data(wire,timestamp);
    const Bytes frame(wire.begin()+static_cast<std::ptrdiff_t>(training),wire.end());
    auto packet=decode_packet(frame,packet_options(options,timestamp),packet_budget(options));
    if(packet.message.data.size()>options.content_limit)throw Error("received message exceeds content limit");
    return {std::move(packet),std::move(diagnostics),timestamp};
}
void append_wire(Bytes& target,const Bytes& bytes,const Options& options) {
    const auto limit=packet_budget(options)+modem::preamble(options.modem).size();
    if(bytes.size()>limit-target.size())throw Error("incoming packet exceeds content budget");
    target.insert(target.end(),bytes.begin(),bytes.end());
}
std::optional<std::size_t> declared_wire_size(const Bytes& wire,const Options& options,std::uint64_t timestamp) {
    const auto training=modem::preamble(options.modem).size();
    if(wire.size()<=training)return {};
    const auto available=std::min(wire.size()-training,packet_prefix_size);
    Bytes prefix(wire.begin()+static_cast<std::ptrdiff_t>(training),
                 wire.begin()+static_cast<std::ptrdiff_t>(training+available));
    xor_audio_whitening(prefix,training);
    const auto plain=options.key?options.key->xor_data(prefix,timestamp,training):prefix;
    const auto count=packet_frame_size(plain,packet_budget(options));
    return count?std::optional<std::size_t>(*count+training):std::nullopt;
}
}

void xor_audio_whitening(std::span<std::uint8_t> bytes,std::uint64_t wire_offset) {
    if(bytes.size()>std::numeric_limits<std::uint64_t>::max()-wire_offset)
        throw Error("audio whitening stream offset overflow");
    if(wire_offset<audio_training_bytes) {
        const auto skip=static_cast<std::size_t>(std::min<std::uint64_t>(audio_training_bytes-wire_offset,bytes.size()));
        bytes=bytes.subspan(skip);wire_offset+=skip;
    }
    if(bytes.empty())return;
    auto offset=wire_offset-audio_training_bytes;
    while(!bytes.empty()) {
        const auto count=std::min<std::size_t>(16384,bytes.size());
        const auto mask=public_whitening_stream().stream(StreamPurpose::Scrambler,0,offset,count);
        for(std::size_t i=0;i<count;++i)bytes[i]^=mask[i];
        bytes=bytes.subspan(count);offset+=count;
    }
}

Bytes audio_bootstrap_mask(const Options& options,std::uint64_t timestamp) {
    Bytes mask(audio_validation_limit);
    xor_audio_whitening(mask,audio_training_bytes);
    if(options.key)mask=options.key->xor_data(mask,timestamp,audio_training_bytes);
    return mask;
}

AudioValidators audio_validators(const Options& options,std::uint64_t timestamp) {
    const auto mask=std::make_shared<const Bytes>(audio_bootstrap_mask(options,timestamp));
    const auto limit=packet_budget(options);
    AudioValidators result;
    result.bootstrap=[mask,limit](const Bytes& bytes)->std::optional<std::size_t> {
        try {
            if(bytes.size()>mask->size())return {};
            auto plain=bytes;
            for(std::size_t i=0;i<plain.size();++i)plain[i]^=(*mask)[i];
            return packet_probe_frame_size(plain,limit);
        } catch(const Error&) {return {};}
    };
    result.packet=[mask,limit,key=options.key,timestamp,content_limit=options.content_limit,codec=packet_options(options,timestamp)](const Bytes& bytes) {
        try {
            if(bytes.size()>limit)return false;
            auto plain=bytes;
            const auto cached=std::min(plain.size(),mask->size());
            for(std::size_t i=0;i<cached;++i)plain[i]^=(*mask)[i];
            for(std::size_t offset=cached;offset<plain.size();) {
                const auto count=std::min(audio_validation_limit,plain.size()-offset);
                auto chunk=std::span(plain).subspan(offset,count);
                const auto wire_offset=audio_training_bytes+offset;
                xor_audio_whitening(chunk,wire_offset);
                if(key) {
                    const auto private_mask=key->stream(StreamPurpose::Data,timestamp,wire_offset,count);
                    for(std::size_t i=0;i<count;++i)chunk[i]^=private_mask[i];
                }
                offset+=count;
            }
            const auto decoded=decode_packet(plain,codec,limit);
            return decoded.consumed_bytes==plain.size()&&decoded.message.data.size()<=content_limit;
        } catch(const Error&) {return false;}
    };
    return result;
}

std::size_t packet_workspace_limit(std::size_t content_limit) {
    // The encoder reserves two coded copies, two uncoded bodies, input and
    // fixed scratch. RS60 needs just over six input sizes; eight also covers
    // framing metadata, small-block rounding and the decoder's copies.
    if(!content_limit || content_limit>(std::numeric_limits<std::size_t>::max()-65568)/8)
        throw Error("invalid content limit");
    return content_limit*8+65536;
}

PacketOptions packet_options(const Options& options, std::uint64_t timestamp) {
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

modem::Config seeded_config(const Options& options, std::uint64_t timestamp) {
    validate(options);
    auto result = options.modem;
    result.stream_epoch=timestamp;
    if(options.key && result.pattern_symbols) {
        constexpr std::string_view domain="DataPump/hardware-data-seed/v1";
        auto seed=options.key->mac(std::span(reinterpret_cast<const std::uint8_t*>(domain.data()),domain.size()));
        result.hardware_data_seed.emplace();
        std::copy(seed.begin(),seed.end(),result.hardware_data_seed->begin());
        OPENSSL_cleanse(seed.data(),seed.size());
    }
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

Estimate estimate(const Message& message, const Options& options) {
    return estimate(message,options,nullptr);
}

Estimate estimate(const Message& message,const Options& options,PacketLayout* layout) {
    validate_message(message, options);
    if(options.modem.pattern_symbols) {
        const auto bits=message_bits(message,options);
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
    std::size_t frame_size=0;
    {
        const auto frame=encode_packet(message,packet_options(options,options.timestamp),packet_budget(options));
        frame_size=frame.size();
        if(layout)*layout=packet_layout(frame,packet_budget(options));
    }
    return estimate_encoded(message, options, frame_size);
}

Estimate estimate_binary(std::span<const std::uint8_t> bits,const Options& options) {
    const auto value=binary_options(bits,options);
    const auto symbols=bits.size()/value.modem.constellation_bits+(bits.size()%value.modem.constellation_bits!=0);
    const auto symbol_samples=modem::symbol_sample_count(value.modem);
    if(symbols>std::numeric_limits<std::uint64_t>::max()/symbol_samples)
        throw Error("transmission duration exceeds 64-bit sample counter");
    const auto content_samples=static_cast<std::uint64_t>(symbols)*symbol_samples;
    const auto hardware_samples=value.modem.pattern_symbols?modem::training_sample_count(value.modem):0;
    if(hardware_samples>std::numeric_limits<std::uint64_t>::max()-content_samples)
        throw Error("transmission duration exceeds 64-bit sample counter");
    const auto samples=hardware_samples+content_samples;
    Estimate result;
    result.content_bytes=result.packet_bytes=bits.size()/8+(bits.size()%8!=0);
    result.content_seconds=result.packet_seconds=static_cast<double>(content_samples)/value.modem.sample_rate;
    result.total_seconds=static_cast<double>(samples)/value.modem.sample_rate;
    result.repeatable_allowed=true;
    std::size_t scratch=std::numeric_limits<std::size_t>::max();
    if(value.modem.pattern_symbols) {
        // PatternCode seeks through fixed stream caches; symbol duration must
        // not be charged as a retained chip array or sampled waveform.
        try {
            modem::StreamingTransmitter probe(modem::RawBits{Bytes(bits.begin(),bits.end())},
                value.modem,std::max(value.modem.memory_limit,value.dsp_workspace_bytes/4));
            scratch=probe.working_bytes();
            result.memory_supported=scratch<=value.dsp_workspace_bytes/4;
        } catch(const Error&) { result.memory_supported=false; }
    } else {
        scratch=65536+value.modem.spreading_factor*sizeof(int);
        result.memory_supported=scratch<=value.dsp_workspace_bytes/4;
    }
    if(samples<=std::numeric_limits<std::size_t>::max()) {
        result.waveform_samples=static_cast<std::size_t>(samples);
        const auto budget=value.modem.memory_limit;
        result.batch_memory_supported=scratch<=budget && result.content_bytes<=budget-scratch &&
            samples<=(budget-scratch-result.content_bytes)/sizeof(float);
    }
    return result;
}

std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits,const Options& options) {
    const auto value=binary_options(bits,options);
    modem::RawBits raw{Bytes(bits.begin(),bits.end())};
    xor_binary_bits(raw.bits,value);
    return std::make_unique<modem::StreamingTransmitter>(std::move(raw),seeded_config(value,value.timestamp),value.dsp_workspace_bytes/4);
}

Bytes message_bits(const Message& message,const Options& options) {
    validate_message(message,options);
    if(message.kind==MessageKind::text && message.data.size()<16)
        return compression::encode_short_bits(message.data,packet_budget(options));
    const auto packet=encode_packet(message,packet_options(options,options.timestamp),packet_budget(options));
    if(packet.size()>packet_budget(options)/8)throw Error("pattern packet bit storage exceeds content workspace");
    Bytes bits;bits.reserve(packet.size()*8);
    for(auto byte:packet)for(unsigned i=0;i<8;++i)bits.push_back(static_cast<std::uint8_t>((byte>>(7-i))&1));
    return bits;
}
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message& message,const Options& options) {
    if(options.modem.pattern_symbols) {
        auto bits=message_bits(message,options);
        if(message.repeatable && message.data.size()>options.repeat_policy.minimum_payload_bytes &&
           static_cast<double>(bits.size())*modem::symbol_seconds(options.modem)>options.repeat_policy.maximum_seconds)
            throw Error("repeatable content exceeds airtime limit");
        // Encoded packet bits have their own checked workspace limit. They
        // are not an untrusted raw UI draft's one-byte-per-bit quota.
        auto value=options;value.content_limit=std::max(value.content_limit,bits.size());
        return binary_transmitter(bits,value);
    }
    return std::make_unique<modem::StreamingTransmitter>(transmission_wire(message,options),seeded_config(options,options.timestamp),options.dsp_workspace_bytes/4);
}
Received interpret_pattern(modem::PatternBurst burst,const Options& options,std::uint64_t timestamp,modem::Diagnostics diagnostics) {
    auto context=options;context.timestamp=timestamp;
    context.content_limit=std::max(context.content_limit,burst.bits.size());
    if(burst.first_stream_symbol>std::numeric_limits<std::size_t>::max())throw Error("received pattern stream index exceeds bit address space");
    xor_binary_bits(burst.bits,context,static_cast<std::size_t>(burst.first_stream_symbol));
    Received result;result.timestamp=timestamp;result.diagnostics=std::move(diagnostics);
    result.diagnostics.pattern_score=burst.score;
    result.diagnostics.sample_offset=static_cast<std::size_t>(burst.first_sample);
    result.raw_bits=std::move(burst.bits);result.packet_validated=false;
    if(options.key && burst.first_stream_symbol)return result;
    // Content grammar is interpreted only after pattern acquisition. A bad
    // packet never changes the winning signal timing or discards its raw bits.
    if(result.raw_bits.size()%8==0) {
        Bytes bytes(result.raw_bits.size()/8);
        for(std::size_t i=0;i<result.raw_bits.size();++i)bytes[i/8]|=static_cast<std::uint8_t>(result.raw_bits[i]<<(7-i%8));
        try {
            auto packet=decode_packet(bytes,packet_options(options,timestamp),packet_budget(options));
            if(packet.consumed_bytes==bytes.size() && packet.message.data.size()<=options.content_limit) {
                result.packet=std::move(packet);result.packet_validated=true;return result;
            }
        } catch(const Error&) {}
    }
    try {
        auto decoded=compression::decode_short_bits(result.raw_bits,std::min<std::size_t>(15,options.content_limit));
        result.packet.message.data=std::move(decoded);
    } catch(const Error&) {} // Preserve uninterpreted or truncated raw bits.
    return result;
}

void xor_binary_bits(std::span<std::uint8_t> bits,const Options& options,std::size_t bit_offset) {
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

Bytes pack(const Message& message, const Options& options) {
    validate_message(message, options);
    auto frame = encode_packet(message, packet_options(options, options.timestamp), packet_budget(options));
    enforce_repeat_policy(message, options, frame.size());
    return options.key ? options.key->xor_data(frame, options.timestamp) : std::move(frame);
}

DecodedPacket unpack(const Bytes& wire, const Options& options) {
    validate(options);
    if (wire.size() > packet_budget(options)) throw Error("packet input exceeds content budget");
    const auto plaintext=options.key?options.key->xor_data(wire,options.timestamp):wire;
    auto packet=decode_packet(plaintext,packet_options(options,options.timestamp),packet_budget(options));
    if(packet.message.data.size()>options.content_limit)throw Error("received message exceeds content limit");
    return packet;
}

Bytes transmission_wire(const Message& message,const Options& options) {
    if(options.modem.pattern_symbols) {
        if(message.kind==MessageKind::text && message.data.size()<16)
            throw Error("short pattern text has an exact bit length; use message_transmitter");
        validate_message(message,options);
        auto bytes=encode_packet(message,packet_options(options,options.timestamp),packet_budget(options));
        return options.key?options.key->xor_data(bytes,options.timestamp):std::move(bytes);
    }
    validate_message(message, options);
    const auto config = seeded_config(options, options.timestamp);
    const auto frame = encode_packet(message, packet_options(options, options.timestamp), packet_budget(options));
    enforce_repeat_policy(message, options, frame.size());
    auto wire = modem::preamble(config);
    wire.insert(wire.end(), frame.begin(), frame.end());
    if (options.key) wire = options.key->xor_data(wire, options.timestamp);
    xor_audio_whitening(wire);
    return wire;
}

std::vector<float> transmit(const Message& message, const Options& options, std::stop_token stop) {
    check_cancelled(stop);
    if(options.modem.pattern_symbols) {
        auto source=message_transmitter(message,options);
        if(source->total_samples()>options.modem.memory_limit/sizeof(float))throw Error("pattern waveform exceeds batch memory limit; use streaming output");
        std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
        std::size_t position=0;while(position<samples.size())position+=source->read(std::span(samples).subspan(position,std::min<std::size_t>(4096,samples.size()-position)),stop);
        return samples;
    }
    auto wire=transmission_wire(message,options);
    check_cancelled(stop);
    auto samples = modem::modulate(wire, seeded_config(options,options.timestamp), stop);
    check_cancelled(stop);
    return samples;
}

Received receive(std::span<const float> samples, const Options& options, Progress progress, std::stop_token stop) {
    check_cancelled(stop);
    validate(options);
    if(options.modem.pattern_symbols) {
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
    auto candidates = drift_candidates(options.timestamp, options.search_seconds, options.key.has_value());
    if (!options.key) candidates = {options.timestamp};
    std::string last_error;
    for (const auto timestamp : candidates) {
        check_cancelled(stop);
        if (progress) progress(timestamp);
        check_cancelled(stop);
        try {
            auto decoder=receiver(options,timestamp);
            Bytes wire;
            std::optional<std::size_t> expected_size;
            for(std::size_t offset=0;offset<samples.size();) {
                const auto chunk=samples.subspan(offset,std::min<std::size_t>(4096,samples.size()-offset));
                append_wire(wire,decoder.push(chunk,stop),options);offset+=chunk.size();
                if(!expected_size)expected_size=declared_wire_size(wire,options,timestamp);
                if(expected_size && wire.size()>=*expected_size) {
                    wire.resize(*expected_size);
                    auto diagnostics=decoder.diagnostics();
                    const auto tail=samples.first(offset).last(std::min<std::size_t>(2048,offset));
                    diagnostics.waveform.assign(tail.begin(),tail.end());
                    check_cancelled(stop);
                    return verified(std::move(wire),std::move(diagnostics),options,timestamp);
                }
            }
            append_wire(wire,decoder.finish(stop),options);
            check_cancelled(stop);
            auto diagnostics=decoder.diagnostics();
            const auto tail=samples.last(std::min<std::size_t>(2048,samples.size()));
            diagnostics.waveform.assign(tail.begin(),tail.end());
            return verified(std::move(wire),std::move(diagnostics),options,timestamp);
        } catch (const Error& error) {
            check_cancelled(stop);
            last_error = error.what();
        }
    }
    throw Error("no validated packet in timing search: " + last_error);
}

Received simulate(const Message& message, const Options& options, const modem::ChannelConfig& channel,
                  Progress progress, std::stop_token stop) {
    check_cancelled(stop);
    validate_message(message,options);
    const auto config = seeded_config(options, options.timestamp);
    modem::validate_channel(config,channel);
    if(config.pattern_symbols) {
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
    const auto wire=transmission_wire(message,options);
    const auto receiver_center=channel.receiver_timestamp.value_or(options.timestamp);
    auto candidates=drift_candidates(receiver_center,options.search_seconds,options.key.has_value());
    if(!options.key)candidates={receiver_center};
    std::string last_error;
    for(const auto timestamp:candidates) {
        check_cancelled(stop);if(progress)progress(timestamp);check_cancelled(stop);
        try {
            modem::StreamingTransmitter source(wire,config,options.dsp_workspace_bytes);
            auto decoder=receiver(options,timestamp);
            modem::SampledSimulationChannel impairments(config,channel);
            Bytes received;
            std::array<float,2048> samples{},preview{};
            std::size_t preview_count=0;
            while(const auto count=impairments.read(source,samples,stop)) {
                // The receiver sees only its own clocked PCM, including idle
                // noise and arbitrary burst/carrier phase. It derives chip
                // correlation and packet timing from those samples.
                append_wire(received,decoder.push(std::span(samples).first(count),stop),options);
                const auto retained=std::min(preview_count,preview.size()-count);
                std::move(preview.begin()+static_cast<std::ptrdiff_t>(preview_count-retained),
                          preview.begin()+static_cast<std::ptrdiff_t>(preview_count),preview.begin());
                std::copy_n(samples.begin(),count,preview.begin()+static_cast<std::ptrdiff_t>(retained));
                preview_count=retained+count;
            }
            // Receiver integration continues after the source stops. Do not
            // finish the capture at a transmitter-provided symbol boundary.
            auto trailing=modem::symbol_sample_count(config);
            while(trailing) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,samples.size()));
                const auto tail=std::span(samples).first(count);
                impairments.read_noise(tail,stop);
                append_wire(received,decoder.push(tail,stop),options);
                trailing-=count;
            }
            append_wire(received,decoder.finish(stop),options);
            auto diagnostics=decoder.diagnostics();
            diagnostics.waveform.assign(preview.begin(),preview.begin()+static_cast<std::ptrdiff_t>(preview_count));
            return verified(std::move(received),std::move(diagnostics),options,timestamp);
        } catch(const Error& error) {check_cancelled(stop);last_error=error.what();}
    }
    throw Error("no validated packet in simulated timing search: "+last_error);
}
}
