#include "datapump/transfer.hpp"
#include "datapump/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
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
}
void validate_message(const Message& message, const Options& options) {
    validate(options);
    if (message.data.size() > options.modem.memory_limit)
        throw Error("message exceeds memory budget");
}
Estimate estimate_encoded(const Message& message, const Options& options, std::size_t frame_size) {
    Message empty;
    empty.kind = message.kind;
    empty.filename = message.filename;
    empty.callsign = message.callsign;
    empty.grid = message.grid;
    empty.repeatable = message.repeatable;
    empty.id = message.id;
    const auto overhead = encode_packet(empty, packet_options(options, options.timestamp), options.modem.memory_limit).size();
    Estimate result;
    result.packet_bytes = frame_size;
    result.content_bytes = frame_size > overhead ? frame_size - overhead : 0;
    const auto rate = modem::bit_rate(options.modem);
    result.packet_seconds = static_cast<double>(frame_size) * 8 / rate;
    result.content_seconds = static_cast<double>(result.content_bytes) * 8 / rate;
    result.repeatable_allowed = message.data.size() <= options.repeat_policy.minimum_payload_bytes ||
                               result.content_seconds <= options.repeat_policy.maximum_seconds;
    // This is exactly the byte rounding used by modem::preamble, without
    // allocating a potentially large training sequence for an estimate.
    const auto training_bytes = std::max<std::size_t>(12, static_cast<std::size_t>(
        std::ceil(options.modem.training_seconds * rate / 8)));
    if (frame_size > std::numeric_limits<std::size_t>::max() - training_bytes)
        throw Error("transmission length exceeds platform size limit");
    const auto wire_bytes = frame_size + training_bytes;
    result.total_seconds = static_cast<double>(wire_bytes) * 8 / rate;
    try {
        result.waveform_samples = modem::waveform_sample_count(wire_bytes, options.modem);
        result.memory_supported = modem::memory_supported(wire_bytes, training_bytes, options.modem);
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
    const auto frame_size = encode_packet(message, packet_options(options, options.timestamp), options.modem.memory_limit).size();
    return estimate_encoded(message, options, frame_size);
}

Bytes pack(const Message& message, const Options& options) {
    validate_message(message, options);
    auto frame = encode_packet(message, packet_options(options, options.timestamp), options.modem.memory_limit);
    enforce_repeat_policy(message, options, frame.size());
    return options.key ? options.key->xor_data(frame, options.timestamp) : std::move(frame);
}

DecodedPacket unpack(const Bytes& wire, const Options& options) {
    validate(options);
    if (wire.size() > options.modem.memory_limit) throw Error("packet input exceeds memory budget");
    if (options.key) {
        const auto plaintext = options.key->xor_data(wire, options.timestamp);
        return decode_packet(plaintext, packet_options(options, options.timestamp), options.modem.memory_limit);
    }
    return decode_packet(wire, packet_options(options, options.timestamp), options.modem.memory_limit);
}

std::vector<float> transmit(const Message& message, const Options& options, std::stop_token stop) {
    check_cancelled(stop);
    validate_message(message, options);
    const auto config = seeded_config(options, options.timestamp);
    const auto frame = encode_packet(message, packet_options(options, options.timestamp), config.memory_limit);
    enforce_repeat_policy(message, options, frame.size());
    check_cancelled(stop);
    auto wire = modem::preamble(config);
    wire.insert(wire.end(), frame.begin(), frame.end());
    if (options.key) wire = options.key->xor_data(wire, options.timestamp);
    check_cancelled(stop);
    auto samples = modem::modulate(wire, config, stop);
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
            const auto config = seeded_config(options, timestamp);
            const auto training = modem::preamble(config);
            const auto expected = options.key ? options.key->xor_data(training, timestamp) : training;
            auto decoded = modem::demodulate(samples, config, expected, stop);
            check_cancelled(stop);
            if (decoded.bytes.size() < training.size()) throw Error("truncated training sequence");
            const auto plain = options.key ? options.key->xor_data(decoded.bytes, timestamp) : std::move(decoded.bytes);
            const Bytes frame(plain.begin() + static_cast<std::ptrdiff_t>(training.size()), plain.end());
            auto packet = decode_packet(frame, packet_options(options, timestamp), config.memory_limit);
            check_cancelled(stop);
            return {std::move(packet), std::move(decoded.diagnostics), timestamp};
        } catch (const Error& error) {
            check_cancelled(stop);
            last_error = error.what();
        }
    }
    throw Error("no validated packet in timing search: " + last_error);
}

Received simulate(const Message& message, const Options& options, const modem::ChannelConfig& channel,
                  Progress progress, std::stop_token stop) {
    auto samples = transmit(message, options, stop);
    check_cancelled(stop);
    const auto config = seeded_config(options, options.timestamp);
    auto noisy = modem::simulate(samples, config, channel);
    check_cancelled(stop);
    return receive(noisy, options, std::move(progress), stop);
}
}
