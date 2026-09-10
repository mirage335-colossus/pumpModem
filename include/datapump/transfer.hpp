#pragma once
#include "datapump/crypto.hpp"
#include "datapump/modem.hpp"
#include "datapump/packet.hpp"
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

namespace datapump::transfer {
struct Options {
    modem::Config modem;
    FecMode fec = FecMode::rs20;
    bool compression = true;
    std::optional<Crypto> key;
    std::uint64_t timestamp = 0;
    unsigned search_seconds = 6;
};
struct Received {
    DecodedPacket packet;
    modem::Diagnostics diagnostics;
    std::uint64_t timestamp = 0;
};
using Progress = std::function<void(std::uint64_t)>;

// Callbacks own their key material and remain valid after Options is destroyed.
PacketOptions packet_options(const Options& options, std::uint64_t timestamp);
modem::Config seeded_config(const Options& options, std::uint64_t timestamp);
Bytes pack(const Message& message, const Options& options);
DecodedPacket unpack(const Bytes& wire, const Options& options);
std::vector<float> transmit(const Message& message, const Options& options,
                            std::stop_token stop = {});
// Progress runs synchronously on the caller's thread, before each candidate.
// Cancellation is checked between candidates and processing stages, not inside
// a single modem FFT/correction call. Cancelled work raises datapump::Error.
Received receive(std::span<const float> samples, const Options& options,
                 Progress progress = {}, std::stop_token stop = {});
Received simulate(const Message& message, const Options& options,
                  const modem::ChannelConfig& channel, Progress progress = {},
                  std::stop_token stop = {});
}
