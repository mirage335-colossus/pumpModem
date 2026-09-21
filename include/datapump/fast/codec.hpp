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
class StreamDecoder;
namespace testing {
// Reproducible regression waveform only. Never selected by production settings.
StreamEncoder deterministic_encoder(Profile,const Crypto&,SourceReader,std::uint64_t seed);
// Incomplete receptions retain opaque fixed source areas, including flags and
// padding. Available only after physical end; null for a hole, an absent area,
// or a successfully completed stream whose areas were compacted into its file.
std::optional<Bytes> retained_source_area(const StreamDecoder&,std::uint64_t ordinal);
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
// Geometry of bytes supplied directly to StreamEncoder; production uses XZ
// bytes here. Use estimate_xz_transmission for original text or file sources.
TransmitEstimate estimate_transmission(const Profile&,bool encrypted,std::uint64_t encoded_bytes);

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
    // A corrupt source area makes the file incomplete, but later fixed areas
    // are still decoded. Fatal bootstrap, quota or internal failures stop work.
    bool decoding_stopped = false;
    bool encrypted = false, authenticated = false;
    std::uint64_t intervals = 0, authenticated_groups = 0, source_bytes = 0;
    std::uint64_t checksum_groups = 0;
    std::uint64_t corrected_bytes = 0, erased_bytes = 0, spool_bytes = 0;
    // Coding cycles include bootstrap. Spool bytes include fixed holes;
    // verified bytes count only retained integrity-checked opaque source areas.
    std::uint64_t coding_cycles = 0, failed_cycles = 0, verified_bytes = 0;
    // LDPC changes are diagnostics until whole-cycle integrity succeeds.
    std::uint64_t ldpc_frames = 0, ldpc_failed_frames = 0, ldpc_iterations = 0, ldpc_changed_bits = 0;
    std::string status = "Waiting for fast stream";
};

enum class SourceEncoding { raw, xz };

class StreamEncoder {
public:
    // Encryption is a matching local choice. There is no wire negotiation or
    // fallback between keyed HMAC and unkeyed SHA-256 checksum operation.
    // SourceEncoding binds source interpretation into bootstrap integrity;
    // callers supply already encoded bytes (xz_source performs compression).
    StreamEncoder(Profile, const std::optional<Crypto>&, SourceReader,
                  SourceEncoding = SourceEncoding::raw);
    ~StreamEncoder();
    StreamEncoder(StreamEncoder&&) noexcept;
    StreamEncoder& operator=(StreamEncoder&&) noexcept;
    bool next_interval(std::span<std::uint8_t> bits);
    std::uint64_t source_bytes() const;
    std::uint64_t intervals_emitted() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    StreamEncoder(Profile,const std::optional<Crypto>&,SourceReader,std::function<Bytes(std::size_t)>,
                  SourceEncoding = SourceEncoding::raw);
    friend StreamEncoder testing::deterministic_encoder(Profile,const Crypto&,SourceReader,std::uint64_t);
};

class StreamDecoder {
public:
    // Raw source mode preserves low-level coding vectors and studies; production
    // Session/WAV APIs explicitly request XZ without an automatic raw fallback.
    StreamDecoder(Profile, const std::optional<Crypto>&, std::uint64_t memory_quota = 256ULL*1024*1024,
                  SourceEncoding = SourceEncoding::raw);
    ~StreamDecoder();
    StreamDecoder(StreamDecoder&&) noexcept;
    StreamDecoder& operator=(StreamDecoder&&) noexcept;
    // Positive soft values mean one; zero means an unknown timed position.
    void push_interval(std::span<const float> soft_bits);
    // Only the DSP's observed six-second absence event may pass true here.
    // EOF/cancellation pass false and cannot make a source available.
    // XZ source decoding, when locally requested, also waits for this event.
    void finish(bool physical_end);
    DecodeSnapshot snapshot() const;
    std::shared_ptr<const ReceivedFile> result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend std::optional<Bytes> testing::retained_source_area(const StreamDecoder&,std::uint64_t);
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
