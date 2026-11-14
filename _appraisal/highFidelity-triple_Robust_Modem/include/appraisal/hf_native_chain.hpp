#pragma once

#include "appraisal/channel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace appraisal {

struct HfNativeChainBand {
    double minimum_hz = 0, maximum_hz = 0;
};

struct HfNativeChainConfig {
    // Endpoint impairments only. Native spread/selectivity/path evolution,
    // physical FHSS, dropouts and observation padding must be off. Timing
    // offset is nonnegative and belongs to TX; capture lead/trail belong to RX.
    // The source may already contain public preroll; it is never recentered.
    // nominal_conversion_if_hz is used unchanged by BOTH endpoint stages.
    datapump::triad::v3::StreamChannelConfig endpoints = [] {
        datapump::triad::v3::StreamChannelConfig result;
        result.effects.noise_enabled = false;
        result.complex_noise_variance = 0;
        return result;
    }();
    ChannelSpec propagation;
    std::uint64_t propagation_seed = 1;

    // One explicit thermal-noise plane, after native RX clock interpolation
    // and oscillator mixing. Unset disables it. endpoints' noise fields must
    // stay off; its noise_seed still selects this independent realization.
    std::optional<double> final_receiver_noise_variance;

    // Caller-declared public/resource bounds, never chosen from realized
    // receiver scores or seeded oscillator truth. Each includes the preceding
    // stage's complete finite tail. Construction rejects undersized bounds.
    std::uint64_t tx_horizon_samples = 0;
    std::uint64_t propagation_horizon_samples = 0;
    std::uint64_t capture_horizon_samples = 0;
    // Explicit exception for an external deterministic RF tone that persists
    // beyond evaluator EOF. The wanted waveform's complete support remains
    // independently bounded, including the public worst RX endpoint clock.
    // No generic source/frontend tail may be cut under this option.
    bool continuing_rf_interference = false;
    std::uint64_t propagation_signal_stop_samples = 0;
    double public_endpoint_clock_bound_ppm = 0;
    HfNativeChainBand tx_band, propagation_band;
    std::size_t bridge_chunk_samples = 256;
    std::size_t maximum_workspace_bytes = 64u * 1024u * 1024u;
};

struct HfNativeChainReport {
    std::uint64_t tx_horizon_samples = 0, propagation_horizon_samples = 0,
        capture_horizon_samples = 0, minimum_propagation_horizon_samples = 0;
    std::uint64_t propagation_signal_stop_samples = 0, minimum_wanted_capture_samples = 0;
    bool continuing_rf_interference_at_capture_end = false;
    double public_endpoint_clock_bound_ppm = 0;
    HfNativeChainBand tx_band, propagation_band;
    // Table-channel output sidebands have no analytic support certificate.
    // Their caller-declared band remains an engineering qualification input.
    bool propagation_band_declared_not_analytically_proved = false;
    std::size_t bridge_chunk_samples = 0, workspace_bytes = 0;
    double tx_preparation_seconds = 0, propagation_preparation_seconds = 0, rx_preparation_seconds = 0;
    // Exclusive wall time: nested upstream reads are subtracted from each
    // downstream stage. TX includes the original source callback. These are
    // renderer diagnostics, not complete-receiver CPU capacity measurements.
    double tx_render_seconds = 0, propagation_render_seconds = 0, rx_render_seconds = 0;
    datapump::triad::v3::StreamChannelReport tx, rx;
    ChannelReport propagation;
};

// Canonical Emission::source() -> native TX clock/oscillator/DAC -> APP
// physical Channel -> native RX clock/oscillator/frontend -> receiver input.
// The renderer owns all intermediate buffers, with one monotone reader. The
// original Emission/source owner must outlive it. The complete composite is
// propagated together; this adapter does not qualify Helper outside a physical
// model's admitted source band. Initially qualified tests are Helper-off.
//
// APP additive white/colored/impulsive noise and APP receiver frontend are
// rejected. APP deterministic tone is an RF-plane blocker before RX clock and
// mixing; native frontend.tone_* is a separate receiver-coordinate blocker.
// Neither a render horizon nor this diagnostic report establishes receiver
// completion or supplies receiver priors. Feed read() to the ordinary receiver
// with its independently prepared public profile and quiet-slot rules.
class HfNativeChain {
public:
    HfNativeChain(Source, HfNativeChainConfig, std::stop_token = {});
    ~HfNativeChain();
    HfNativeChain(HfNativeChain&&) noexcept;
    HfNativeChain& operator=(HfNativeChain&&) noexcept;
    HfNativeChain(const HfNativeChain&) = delete;
    HfNativeChain& operator=(const HfNativeChain&) = delete;

    std::size_t read(std::span<Complex>, std::span<std::uint8_t> validity = {});
    std::uint64_t total_samples() const;
    std::uint64_t position() const;
    HfNativeChainReport report() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace appraisal
