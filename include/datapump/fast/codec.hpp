#pragma once

#include "datapump/crypto.hpp"
#include "datapump/fast/profile.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace datapump::fast {

using SourceReader = std::function<std::size_t(std::span<std::uint8_t>)>;
class StreamEncoder;
namespace testing {
// Reproducible regression waveform only. Never selected by production settings.
StreamEncoder deterministic_encoder(Profile,const Crypto&,SourceReader,std::uint64_t seed);
}
// Readers return zero only at EOF. They never receive a remotely chosen size.
SourceReader file_source(const std::filesystem::path&);
// Owns the caller's local bytes (for text); each read stays within its span.
SourceReader byte_source(Bytes);
std::size_t cycle_intervals(const Profile&);
// Capacity mode uses interleave_depth LDPC frames per coding cycle. One source
// flag is subtracted here; a final cycle additionally needs one padding byte.
std::size_t capacity_source_bytes_per_cycle(const Profile&,bool encrypted);
// GF(65536) parity symbols (two bytes each), rounded up to even at >=0.3%.
std::size_t capacity_parity_symbols(const Profile&);
struct TransmitEstimate { std::uint64_t intervals=0,samples=0; double seconds=0,source_bps=0; };
TransmitEstimate estimate_transmission(const Profile&,bool encrypted,std::uint64_t source_bytes);

std::size_t ciphertext_bytes(const Profile&);
// In capacity mode there is one group per coding cycle; the returned area
// includes the protected continuation/final flag and final padding.
std::size_t source_bytes_per_group(const Profile&,bool encrypted);

class ReceivedFile {
public:
    ~ReceivedFile();
    std::uint64_t size() const;
    // Immutable raw bytes for programmatic consumers; no text interpretation.
    std::span<const std::uint8_t> bytes() const;
    // Exclusive creation: an existing destination is never overwritten.
    void save(const std::filesystem::path&) const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit ReceivedFile(std::shared_ptr<Impl>);
    friend class StreamDecoder;
};

struct DecodeSnapshot {
    bool physical_end = false, complete = false, failed = false;
    bool encrypted = false, authenticated = false;
    std::uint64_t intervals = 0, authenticated_groups = 0, source_bytes = 0;
    std::uint64_t checksum_groups = 0;
    std::uint64_t corrected_bytes = 0, erased_bytes = 0, spool_bytes = 0;
    // LDPC changes are diagnostics until whole-cycle integrity succeeds.
    std::uint64_t ldpc_frames = 0, ldpc_failed_frames = 0, ldpc_iterations = 0, ldpc_changed_bits = 0;
    std::string status = "Waiting for fast stream";
};

class StreamEncoder {
public:
    // Encryption is a matching local choice. There is no wire negotiation or
    // fallback between keyed HMAC and unkeyed SHA-256 checksum operation.
    StreamEncoder(Profile, const std::optional<Crypto>&, SourceReader);
    ~StreamEncoder();
    StreamEncoder(StreamEncoder&&) noexcept;
    StreamEncoder& operator=(StreamEncoder&&) noexcept;
    bool next_interval(std::span<std::uint8_t> bits);
    std::uint64_t source_bytes() const;
    std::uint64_t intervals_emitted() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    StreamEncoder(Profile,const std::optional<Crypto>&,SourceReader,std::function<Bytes(std::size_t)>);
    friend StreamEncoder testing::deterministic_encoder(Profile,const Crypto&,SourceReader,std::uint64_t);
};

class StreamDecoder {
public:
    StreamDecoder(Profile, const std::optional<Crypto>&, std::uint64_t memory_quota = 256ULL*1024*1024);
    ~StreamDecoder();
    StreamDecoder(StreamDecoder&&) noexcept;
    StreamDecoder& operator=(StreamDecoder&&) noexcept;
    // Positive soft values mean one; zero means an unknown timed position.
    void push_interval(std::span<const float> soft_bits);
    // Only the DSP's observed six-second absence event may pass true here.
    // EOF/cancellation pass false and cannot make a source available.
    void finish(bool physical_end);
    DecodeSnapshot snapshot() const;
    std::shared_ptr<const ReceivedFile> result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace coding {
// K=7, generators 0171 then 0133, MSB-first bytes, six zero tail bits.
// Puncture pairs: 1/2 [11], 3/4 [11,10,01], 7/8 [11,10,10,10,01,01,01].
// Each cycle restarts at state/phase zero and pads with zero coded bits.
Bytes encode(std::span<const std::uint8_t> bytes, CodeRate);
struct Decoded { Bytes bytes, unreliable; };
Decoded decode(std::span<const float> soft, std::size_t source_bytes, CodeRate);
}

namespace testing {
// Deterministic wire-vector hooks; production always obtains salt/IV from RAND.
Bytes capacity_whitening_mask(std::size_t bits,std::uint64_t cycle);
std::size_t capacity_interleave_rotation(const Profile&,std::size_t column);
Bytes seal_group(const Profile&, const Crypto&, std::span<const std::uint8_t> salt,
    std::uint64_t ordinal, std::span<const std::uint8_t> iv,
    std::span<const std::uint8_t> plaintext);
Bytes open_group(const Profile&, const Crypto&, std::span<const std::uint8_t> salt,
    std::uint64_t ordinal, std::span<const std::uint8_t> systematic);
}
}
