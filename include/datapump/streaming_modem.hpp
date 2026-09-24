#pragma once
#include "datapump/modem.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <random>

namespace datapump::modem {
// A uniformly timed complex baseband integration, with no framing labels.
struct SymbolObservation {
    std::complex<double> value;
    std::uint64_t sample_count = 0;
};
// Newly observed payload symbols in the decoder's differential-phase and
// gain-normalized amplitude coordinates. Points are measured, never snapped
// to decisions. A slow consumer receives the newest bounded set and a count
// of points displaced before it could consume them.
struct ConstellationBatch {
    std::vector<std::complex<double>> points;
    std::uint64_t dropped = 0;
};
// Validation is deterministic for a given prefix within one receiver instance;
// repeated identical prefixes may reuse the previous extent/rejection verdict.
using BootstrapValidator = std::function<std::optional<std::size_t>(const Bytes&)>;
// Exact complete packet bytes, without the synthetic training prefix. Short
// frames remain provisional until their digest/MAC passes this callback.
using PacketValidator = std::function<bool(const Bytes&)>;

// Unframed binary input: one meaningful 0/1 bit per element, including leading
// zeros. The final symbol uses only its actual number of remaining bits.
struct RawBits { Bytes bits; };

class StreamingTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit=2112;
    static constexpr std::size_t constellation_history_limit=2048;
    StreamingTransmitter(Bytes wire, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    StreamingTransmitter(RawBits bits, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    ~StreamingTransmitter();
    StreamingTransmitter(StreamingTransmitter&&) noexcept;
    StreamingTransmitter& operator=(StreamingTransmitter&&) noexcept;
    std::size_t read(std::span<float> output, std::stop_token stop = {});
    std::optional<SymbolObservation> next_symbol(std::stop_token stop = {});
    bool finished() const;
    std::uint64_t total_samples() const;
    std::uint64_t samples_emitted() const;
    // Actual payload symbols whose transmission has begun, oldest first.
    // Fixed training and spreading-chip signs are excluded. Each symbol is
    // retained once, regardless of PCM block size or integration duration.
    std::vector<std::complex<double>> payload_constellation() const;
    ConstellationBatch take_payload_constellation();
    // Reconstruct the actual recent PCM, including spreading and carrier phase.
    // At most 2048 samples; any portion before the transmission is zero-filled.
    void preview_last(std::span<float> output) const;
    // Same recent waveform before projecting its analytic carrier to real PCM;
    // up to analytic_preview_limit samples, including interpolation margins.
    void preview_last_analytic(std::span<std::complex<double>> output) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StreamingReceiver {
public:
    StreamingReceiver(Config config, Bytes expected_preamble,
                      std::size_t workspace_bytes = 8 * 1024 * 1024,
                      BootstrapValidator validator = {}, PacketValidator packet_validator = {});
    ~StreamingReceiver();
    StreamingReceiver(StreamingReceiver&&) noexcept;
    StreamingReceiver& operator=(StreamingReceiver&&) noexcept;
    // Choose PCM or integrated observations for a capture; reset before
    // changing input kinds. Empty spans do not select an input kind.
    Bytes push(std::span<const float> samples, std::stop_token stop = {});
    Bytes push_symbols(std::span<const SymbolObservation> observations, std::stop_token stop = {});
    // End a finite capture by completing only final integrals with at least
    // 50% observed coverage; integrity validation resolves its decisions.
    // Does not insert silence or extra data symbols.
    Bytes finish(std::stop_token stop = {});
    bool synchronized() const;
    // A plausible short header is collecting, but is not yet verified/locked.
    bool acquiring() const;
    // Bounded, unverified encoded bytes from the best provisional fit. These
    // are for a clearly tentative preview, never packet/file delivery.
    Bytes provisional_frame() const;
    Diagnostics diagnostics() const;
    // Available for the best provisional fit while acquiring(), then for the
    // validated receive timing after synchronization. The first
    // bootstrap symbol has no preceding received phase reference and is
    // excluded; subsequent points use the same reference as the decoder.
    // Switching tentative fits does not replay already published samples.
    // Reset discards pending points and their overflow count.
    ConstellationBatch take_payload_constellation();
    std::size_t working_bytes() const;
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Aligned, unframed APSK reception. The bit count and modem settings are known;
// carrier phase starts at zero and nominal channel gain is one. Decisions come
// only from observed samples, with no framing, integrity check or correction.
class BinaryReceiver {
public:
    BinaryReceiver(Config config, std::size_t bit_count,
                   std::size_t workspace_bytes = 8 * 1024 * 1024);
    ~BinaryReceiver();
    BinaryReceiver(BinaryReceiver&&) noexcept;
    BinaryReceiver& operator=(BinaryReceiver&&) noexcept;
    Bytes push_symbols(std::span<const SymbolObservation> observations, std::stop_token stop = {});
    // A final symbol with at least99% measured clock coverage may complete;
    // shorter/incomplete observations produce no invented trailing bits.
    Bytes finish(std::stop_token stop = {});
    ConstellationBatch take_payload_constellation();
    std::size_t bits_received() const;
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
SymbolObservation add_awgn(SymbolObservation observation, double sample_snr_db,
                           std::mt19937_64& random);
}
