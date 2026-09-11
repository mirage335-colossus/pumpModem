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
// Validation is deterministic for a given prefix within one receiver instance;
// repeated identical rejected prefixes may reuse the previous verdict.
using BootstrapValidator = std::function<bool(const Bytes&)>;

class StreamingTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit=2112;
    static constexpr std::size_t constellation_history_limit=2048;
    StreamingTransmitter(Bytes wire, Config config, std::size_t workspace_bytes = 8 * 1024 * 1024);
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
                      BootstrapValidator validator = {});
    ~StreamingReceiver();
    StreamingReceiver(StreamingReceiver&&) noexcept;
    StreamingReceiver& operator=(StreamingReceiver&&) noexcept;
    Bytes push(std::span<const float> samples, std::stop_token stop = {});
    Bytes push_symbols(std::span<const SymbolObservation> observations, std::stop_token stop = {});
    // End a finite capture: complete its partial I/Q bin, then observe up to
    // one symbol of silence. This does not add transmitted framing or airtime.
    Bytes finish(std::stop_token stop = {});
    bool synchronized() const;
    Diagnostics diagnostics() const;
    std::size_t working_bytes() const;
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
SymbolObservation add_awgn(SymbolObservation observation, double sample_snr_db,
                           std::mt19937_64& random);
}
