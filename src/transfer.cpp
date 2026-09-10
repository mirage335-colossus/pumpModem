#include "datapump/transfer.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <utility>

namespace datapump::transfer {
namespace {
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
Estimate estimate_encoded(const Message& message, const Options& options, std::size_t frame_size) {
    Message empty;
    empty.kind = message.kind;
    empty.filename = message.filename;
    empty.callsign = message.callsign;
    empty.grid = message.grid;
    empty.repeatable = message.repeatable;
    empty.id = message.id;
    const auto overhead = encode_packet(empty, packet_options(options, options.timestamp), packet_budget(options)).size();
    Estimate result;
    result.packet_bytes = frame_size;
    result.content_bytes = frame_size > overhead ? frame_size - overhead : 0;
    const auto rate = modem::bit_rate(options.modem);
    result.packet_seconds = static_cast<double>(frame_size) * 8 / rate;
    result.content_seconds = static_cast<double>(result.content_bytes) * 8 / rate;
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
        // A real streaming receiver allocates only its finite hypothesis bank.
        // Check that same allocation contract, not the hypothetical PCM vector.
        modem::StreamingReceiver probe(options.modem,modem::preamble(options.modem),options.dsp_workspace_bytes);
        result.memory_supported = probe.working_bytes() <= options.dsp_workspace_bytes;
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
    const auto prefix=expected.size(),limit=packet_budget(options);
    if(options.key)expected=options.key->xor_data(expected,timestamp);
    const auto key=options.key;
    return modem::StreamingReceiver(config,std::move(expected),options.dsp_workspace_bytes,
        [key,timestamp,prefix,limit](const Bytes& bytes) {
            try {
                const auto plain=key?key->xor_data(bytes,timestamp,prefix):bytes;
                return packet_bootstrap_possible(plain,limit) && packet_frame_size(plain,limit).has_value();
            } catch(const Error&) {return false;}
        });
}
Received verified(Bytes wire,modem::Diagnostics diagnostics,const Options& options,std::uint64_t timestamp) {
    const auto training=modem::preamble(options.modem).size();
    if(wire.size()<training)throw Error("packet bootstrap not acquired");
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
    if(wire.size()<training+packet_prefix_size)return {};
    const Bytes prefix(wire.begin()+static_cast<std::ptrdiff_t>(training),
                       wire.begin()+static_cast<std::ptrdiff_t>(training+packet_prefix_size));
    const auto plain=options.key?options.key->xor_data(prefix,timestamp,training):prefix;
    const auto count=packet_frame_size(plain,packet_budget(options));
    return count?std::optional<std::size_t>(*count+training):std::nullopt;
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
    validate_message(message, options);
    const auto frame_size = encode_packet(message, packet_options(options, options.timestamp), packet_budget(options)).size();
    return estimate_encoded(message, options, frame_size);
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
    validate_message(message, options);
    const auto config = seeded_config(options, options.timestamp);
    const auto frame = encode_packet(message, packet_options(options, options.timestamp), packet_budget(options));
    enforce_repeat_policy(message, options, frame.size());
    auto wire = modem::preamble(config);
    wire.insert(wire.end(), frame.begin(), frame.end());
    if (options.key) wire = options.key->xor_data(wire, options.timestamp);
    return wire;
}

std::vector<float> transmit(const Message& message, const Options& options, std::stop_token stop) {
    check_cancelled(stop);
    auto wire=transmission_wire(message,options);
    check_cancelled(stop);
    auto samples = modem::modulate(wire, seeded_config(options,options.timestamp), stop);
    check_cancelled(stop);
    return samples;
}

Received receive(std::span<const float> samples, const Options& options, Progress progress, std::stop_token stop) {
    check_cancelled(stop);
    validate(options);
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
    if(!std::isfinite(channel.snr_db) || std::abs(channel.snr_db)>300 || !std::isfinite(channel.frequency_offset_hz))
        throw Error("invalid simulation channel");
    const auto config = seeded_config(options, options.timestamp);
    // Frequency-offset experiments retain the raw PCM channel model. The
    // accelerated channel explicitly assumes an ideal coherent carrier.
    if(channel.frequency_offset_hz!=0) {
        auto noisy=modem::simulate(transmit(message,options,stop),config,channel);
        return receive(noisy,options,std::move(progress),stop);
    }
    const auto wire=transmission_wire(message,options);
    auto candidates=drift_candidates(options.timestamp,options.search_seconds,options.key.has_value());
    if(!options.key)candidates={options.timestamp};
    std::string last_error;
    for(const auto timestamp:candidates) {
        check_cancelled(stop);if(progress)progress(timestamp);check_cancelled(stop);
        try {
            modem::StreamingTransmitter source(wire,config,options.dsp_workspace_bytes);
            auto decoder=receiver(options,timestamp);
            std::mt19937_64 random(channel.seed);
            Bytes received;
            auto delay=static_cast<std::uint64_t>(channel.delay_samples);
            const auto quantum=std::max<std::uint64_t>(1,modem::symbol_sample_count(config)/32);
            while(delay) {
                check_cancelled(stop);
                const auto count=std::min(delay,quantum);
                const auto noise=modem::add_awgn({{},count},channel.snr_db,random);
                append_wire(received,decoder.push_symbols(std::span(&noise,1),stop),options);
                delay-=count;
            }
            while(const auto observation=source.next_symbol(stop)) {
                const auto noisy=modem::add_awgn(*observation,channel.snr_db,random);
                append_wire(received,decoder.push_symbols(std::span(&noisy,1),stop),options);
            }
            append_wire(received,decoder.finish(stop),options);
            auto diagnostics=decoder.diagnostics();diagnostics.waveform.resize(2048);
            source.preview_last(diagnostics.waveform);
            std::normal_distribution<double> noise(0,std::sqrt(modem::nominal_signal_power*std::pow(10.,-channel.snr_db/10)));
            for(auto& sample:diagnostics.waveform)sample+=static_cast<float>(noise(random));
            return verified(std::move(received),std::move(diagnostics),options,timestamp);
        } catch(const Error& error) {check_cancelled(stop);last_error=error.what();}
    }
    throw Error("no validated packet in simulated timing search: "+last_error);
}
}
