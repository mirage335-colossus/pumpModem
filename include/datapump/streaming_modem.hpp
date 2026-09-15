#pragma once
#include "datapump/modem.hpp"
#include "datapump/pattern_receiver.hpp"
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
// Newly observed payload physical baseband chip I/Q for pattern transport.
// Points are never snapped to decisions. A slow
// consumer receives the newest bounded set and a count
// of points displaced before it could consume them.
struct ConstellationBatch {
    std::vector<std::complex<double>> points;
    std::uint64_t dropped = 0;
};
// Unframed binary input: one meaningful 0/1 bit per element, including leading
// zeros. Every symbol carries exactly one bit, including the last symbol.
struct RawBits { Bytes bits; };

class StreamingTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit=2112;
    static constexpr std::size_t constellation_history_limit=2048; // Minimum retained history.
    static constexpr unsigned constellation_frame_rate=60;
    // Preserve the existing narrow-band history and at least one nominal
    // bitmap frame of physical chips, including a chip already in progress.
    static std::size_t constellation_history_capacity(const Config& config);
    // Both input forms include the rounded settling prefix and an exact
    // three-second suppression-noise tail, outside all payload symbol slots.
    StreamingTransmitter(Bytes wire, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    StreamingTransmitter(RawBits bits, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
    ~StreamingTransmitter();
    StreamingTransmitter(StreamingTransmitter&&) noexcept;
    StreamingTransmitter& operator=(StreamingTransmitter&&) noexcept;
    std::size_t read(std::span<float> output, std::stop_token stop = {});
    // The same sampled waveform before real projection, for physical channel
    // resampling and carrier rotation. Carries no symbol/framing metadata.
    std::size_t read_analytic(std::span<std::complex<double>> output,
                              std::stop_token stop = {});
    // Removed integrated-symbol interface: throws; use physical samples.
    std::optional<SymbolObservation> next_symbol(std::stop_token stop = {});
    bool finished() const;
    std::uint64_t total_samples() const;
    std::uint64_t samples_emitted() const;
    // Retained transmitter state, independent of streamed waveform duration.
    std::size_t working_bytes() const;
    // Actual payload points whose transmission has begun, oldest first.
    // Retains physical baseband chips, including all enabled waveform masks.
    // Settling and suppression noise are excluded.
    // Each point is retained once, regardless of PCM block size or duration.
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
    StreamingReceiver(Config config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                      PatternSearch pattern_search = {});
    ~StreamingReceiver();
    StreamingReceiver(StreamingReceiver&&) noexcept;
    StreamingReceiver& operator=(StreamingReceiver&&) noexcept;
    // Feed physical PCM, optionally with its shared carrier projection.
    Bytes push(std::span<const float> samples, std::stop_token stop = {});
    Bytes push(std::span<const float> samples, std::span<const std::complex<double>> projected,
               std::stop_token stop = {});
    // Removed integrated-symbol interface: throws; use physical samples.
    Bytes push_symbols(std::span<const SymbolObservation> observations, std::stop_token stop = {});
    // Finish a finite capture without inserting silence or extra symbols.
    // Only sufficiently supported symbols are flushed. Their chunks remain
    // incomplete unless sampled search absence already established stream end.
    // No synthetic tail samples or bits are inserted.
    Bytes finish(std::stop_token stop = {});
    bool synchronized() const;
    // Pattern candidates are accumulating.
    bool acquiring() const;
    // Bounded previews and drainable immutable decision chunks. Only observed
    // six-second search absence sets complete; EOF returns incomplete chunks.
    PatternBurst provisional_pattern() const;
    std::vector<PatternBurst> take_pattern_bursts();
    std::vector<PatternEvidence> pattern_candidates() const;
    std::vector<PatternEvidence> pattern_candidates(std::size_t limit) const;
    bool clock_windowed() const;
    Diagnostics diagnostics() const;
    // Physical chip observations for provisional and synchronized pattern fits.
    // Switching tentative fits does not replay already published samples.
    // Reset discards pending points and their overflow count.
    ConstellationBatch take_payload_constellation();
    std::size_t working_bytes() const;
    // Shared receiver banks can lend unused DSP space to an active recording.
    // The limit cannot be reduced below storage already in use.
    void set_workspace_bytes(std::size_t bytes);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

SymbolObservation add_awgn(SymbolObservation observation, double sample_snr_db,
                           std::mt19937_64& random);
}
