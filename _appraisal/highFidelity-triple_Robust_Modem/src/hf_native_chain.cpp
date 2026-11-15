#include "appraisal/hf_native_chain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace appraisal {
namespace {
using datapump::triad::v3::StreamChannelConfig;
using datapump::triad::v3::StreamingChannel;
using Clock = std::chrono::steady_clock;
double seconds_since(Clock::time_point begin) { return std::chrono::duration<double>(Clock::now() - begin).count(); }

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("native HF chain rendering cancelled");
}
std::size_t bytes_add(std::size_t a, std::size_t b) {
    require(b <= SIZE_MAX - a, "native HF chain workspace overflow");
    return a + b;
}
std::uint64_t samples_ceil(long double count) {
    require(std::isfinite(count) && count >= 0 && count < UINT64_MAX,
            "native HF chain support exceeds sample coordinates");
    return static_cast<std::uint64_t>(std::ceil(count));
}
void check_band(const HfNativeChainBand& band, double fs) {
    require(std::isfinite(band.minimum_hz) && std::isfinite(band.maximum_hz) &&
            band.minimum_hz <= band.maximum_hz && band.minimum_hz > -fs / 2 &&
            band.maximum_hz < fs / 2, "native HF chain declared band is invalid or reaches Nyquist");
}
void contains(const HfNativeChainBand& band, double low, double high, const char* message) {
    require(std::isfinite(low) && std::isfinite(high) && band.minimum_hz <= low &&
            band.maximum_hz >= high, message);
}

void validate(const HfNativeChainConfig& c, const Source& source) {
    const auto& e = c.endpoints.effects;
    require(source.geometry.source_samples > 0 && static_cast<bool>(source.sample_at),
            "native HF chain needs a finite nonempty source");
    require(source.format.sample_rate_denominator == 1 && e.sample_rate == source.format.sample_rate &&
            e.sample_rate >= 64 && e.rf_hz == c.propagation.rf_hz,
            "native HF chain needs one integer nominal clock and matching RF coordinates");
    require(c.bridge_chunk_samples > 0 && c.bridge_chunk_samples <= 4096 &&
            c.maximum_workspace_bytes > 0, "native HF chain needs bounded bridge and workspace sizes");
    require(c.tx_horizon_samples > 0 && c.propagation_horizon_samples > 0 && c.capture_horizon_samples > 0,
            "native HF chain needs three explicit nonzero horizons");
    require(e.timing_offset_seconds >= 0, "native HF chain negative TX timing offsets are not qualified");
    require(!e.fhss && e.hop_offsets_hz.empty() && e.retune_offset_seconds == 0 &&
            e.dropout_ms == 0 && e.dropout_start_ms == 0 &&
            c.endpoints.missed_receiver_dwell_indices.empty(),
            "native HF chain physical retuning and observation erasures are not qualified");
    require(e.static_doppler_hz == 0 && e.spread_delay_ms == 0 && e.spread_doppler_hz == 0 &&
            e.perturbation_slow_depth == 0 && e.perturbation_fast_depth == 0 &&
            c.endpoints.selective_tail_floor_seconds == 0 && c.endpoints.minimum_capture_samples == 0,
            "native HF chain propagation belongs only to the APP physical channel");
    const auto& p = c.endpoints.path_evolution;
    require(p.common_doppler_wander_hz == 0 && p.bounded_spread_knot_seconds == 0 &&
            p.ray_coherence_seconds == 0 && p.gaussian_doppler_width_hz == 0 &&
            p.random_doppler_width_hz == 0 && p.delay_excursion_seconds == 0 &&
            p.diurnal_phase_amplitude_radians == 0 && p.outage_start_seconds == -1 &&
            p.outage_end_seconds == -1, "native HF chain cannot also apply native path evolution");
    const auto& selective = c.endpoints.selective_model;
    require(selective.coefficient_law == datapump::triad::v3::SelectiveCoefficientLaw::Sinusoidal &&
            !selective.modulation_rate_hz && selective.correlation_spacing_hz == 0 && selective.center_hz == 0,
            "native HF chain native selective-model configuration must be off/default");
    require(!e.noise_enabled && c.endpoints.complex_noise_variance == 0 &&
            c.endpoints.diagnostic_iq_noise.mode == datapump::background_noise::Mode::Off,
            "native HF chain noise must use the explicit final receiver plane");
    require(!c.final_receiver_noise_variance || (std::isfinite(*c.final_receiver_noise_variance) &&
            *c.final_receiver_noise_variance >= 0), "native HF chain final noise variance is invalid");
    const auto& n = c.propagation.noise;
    require(n.white_variance == 0 && n.colored_variance == 0 && n.impulse_rate_hz == 0 && n.impulse_rms == 0,
            "native HF chain APP random noise is unqualified before RX interpolation");
    const auto& f = c.propagation.frontend;
    require(f.lowpass_hz == 0 && f.clip_magnitude == 0 && f.agc_target == 0 && f.adc_bits == 0,
            "native HF chain receiver frontend belongs to the native RX stage");
    require(!c.propagation.lunar, "native HF chain lunar output band is not qualified");
    require(std::isfinite(c.public_endpoint_clock_bound_ppm) && c.public_endpoint_clock_bound_ppm >= 0 &&
            c.public_endpoint_clock_bound_ppm < 1e6, "native HF chain public endpoint clock bound is invalid");
    if (c.continuing_rf_interference) {
        require(n.tone_amplitude > 0 && c.propagation_signal_stop_samples > 0 &&
                std::abs(e.tx.audio_ppm) <= c.public_endpoint_clock_bound_ppm &&
                std::abs(e.rx.audio_ppm) <= c.public_endpoint_clock_bound_ppm,
                "continuing RF interference needs a tone, wanted-support stop and containing public clocks");
    } else require(c.propagation_signal_stop_samples == 0,
                   "a separate wanted-support stop is only available for continuing RF interference");
    check_band(c.tx_band, e.sample_rate);
    check_band(c.propagation_band, e.sample_rate);
    const double tx_scale = 1 + e.tx.audio_ppm * 1e-6;
    require(std::isfinite(tx_scale) && tx_scale > 0, "native HF chain TX clock is invalid");
    const double seconds = static_cast<double>(source.geometry.source_samples) / (e.sample_rate * tx_scale);
    const double offset = e.rf_hz * (std::abs(e.tx.rf_ppm) + std::abs(e.tx.rf_drift_ppm_per_second) * seconds) * 1e-6 +
        e.nominal_conversion_if_hz * std::abs(tx_scale - 1) +
        std::abs(c.endpoints.tx_frequency_drift_hz_per_second) * seconds;
    contains(c.tx_band, source.minimum_frequency_hz * tx_scale - offset,
             source.maximum_frequency_hz * tx_scale + offset,
             "native HF chain TX band omits source or deterministic oscillator allowance");
    const double horizon = static_cast<double>(c.propagation_horizon_samples) / e.sample_rate;
    double low = c.tx_band.minimum_hz, high = c.tx_band.maximum_hz;
    const double source_band = std::max(std::abs(low), std::abs(high));
    for (const auto& path : c.propagation.paths) {
        const double shift0 = path.residual_doppler_hz - c.propagation.rf_hz * path.delay_rate;
        const double shift1 = shift0 + path.residual_drift_hz_s * horizon;
        const double wander = path.phase_wander_amplitude_radians > 0 ?
            (path.phase_wander_amplitude_radians + 1) / path.phase_wander_period_seconds : 0;
        const double margin = source_band * std::abs(path.delay_rate) + 3 * path.gaussian_width_2sigma_hz + wander +
            (path.hf ? detail::hf_mode_spectral_margin_hz(*path.hf, c.propagation.rf_hz, source_band) : 0);
        low = std::min(low, c.tx_band.minimum_hz + std::min(shift0, shift1) - margin);
        high = std::max(high, c.tx_band.maximum_hz + std::max(shift0, shift1) + margin);
    }
    if (n.tone_amplitude != 0) {
        low = std::min(low, std::min(n.tone_hz, n.tone_hz + n.tone_sweep_hz_s * horizon));
        high = std::max(high, std::max(n.tone_hz, n.tone_hz + n.tone_sweep_hz_s * horizon));
    }
    contains(c.propagation_band, low, high, "native HF chain propagated band omits a declared physical allowance");
}

StreamChannelConfig tx_configuration(const HfNativeChainConfig& c) {
    auto result = c.endpoints;
    result.effects.rx = {};
    result.rx_spectrum = {};
    result.effects.capture_lead_seconds = 0;
    result.effects.capture_trail_seconds = 0;
    result.effects.slow_agc = false;
    result.frontend = {};
    result.frontend.dac = c.endpoints.frontend.dac;
    result.maximum_chunk_samples = c.bridge_chunk_samples;
    result.minimum_capture_samples = c.tx_horizon_samples;
    return result;
}
StreamChannelConfig rx_configuration(const HfNativeChainConfig& c) {
    auto result = c.endpoints;
    result.effects.tx = {};
    result.tx_spectrum = {};
    result.effects.timing_offset_seconds = 0;
    // TX silence still leaves external RF-plane interference present.
    result.effects.signal_enabled = true;
    result.tx_frequency_drift_hz_per_second = 0;
    result.signal_gain = 1;
    result.frontend.dac = {};
    result.effects.noise_enabled = c.final_receiver_noise_variance.has_value();
    result.complex_noise_variance = c.final_receiver_noise_variance.value_or(0);
    result.minimum_capture_samples = c.capture_horizon_samples;
    return result;
}
} // namespace

struct HfNativeChain::Impl {
    HfNativeChainConfig config;
    std::stop_token stop;
    std::vector<Complex> tx_buffer, propagation_buffer;
    std::size_t tx_position = 0, tx_count = 0, propagation_position = 0, propagation_count = 0;
    std::uint64_t tx_next = 0, propagation_next = 0, minimum_propagation_horizon = 0;
    std::uint64_t wanted_stop = 0, minimum_wanted_capture = 0;
    std::size_t fixed_overhead_bytes = 0, preparation_peak_bytes = 0;
    double tx_preparation_seconds = 0, propagation_preparation_seconds = 0, rx_preparation_seconds = 0;
    double tx_render_seconds = 0, propagation_render_seconds = 0, propagation_inclusive_seconds = 0, rx_render_seconds = 0;
    std::unique_ptr<StreamingChannel> tx;
    std::unique_ptr<Channel> propagation;
    std::unique_ptr<StreamingChannel> rx;

    Impl(Source source, HfNativeChainConfig c, std::stop_token token) : config(std::move(c)), stop(token) {
        cancelled(stop);
        validate(config, source);
        const auto retained_configuration = bytes_add(
            config.endpoints.effects.hop_offsets_hz.capacity() * sizeof(double),
            config.endpoints.missed_receiver_dwell_indices.capacity() * sizeof(std::uint64_t));
        fixed_overhead_bytes = bytes_add(sizeof(Impl), retained_configuration);
        if (bytes_add(fixed_overhead_bytes, 2 * config.bridge_chunk_samples * sizeof(Complex)) >=
                config.maximum_workspace_bytes)
            throw ChannelWorkspaceError("native HF chain fixed storage exhausts workspace");
        const double fs = config.endpoints.effects.sample_rate;
        const double physical_seconds = static_cast<double>(config.propagation_horizon_samples) / fs;
        double physical_tail = channel_tail_seconds(config.propagation, physical_seconds) +
            channel_interpolation_tail_seconds(config.propagation, fs);
        if(config.propagation.vector_two_hop) {
            const double complete_fir_tail=channel_tail_seconds(config.propagation,physical_seconds)-
                static_cast<double>(HfVectorTwoHopTable::first_lag_samples())/fs+
                static_cast<double>(HfVectorTwoHopTable::last_lag_samples())/fs;
            physical_tail=std::max(physical_tail,complete_fir_tail);
        }
        minimum_propagation_horizon = samples_ceil(static_cast<long double>(config.tx_horizon_samples) +
                                                   static_cast<long double>(physical_tail) * fs);
        require(config.propagation_horizon_samples >= minimum_propagation_horizon,
                "native HF chain propagated horizon truncates the declared complete physical tail");
        wanted_stop = config.continuing_rf_interference ? config.propagation_signal_stop_samples :
            config.propagation_horizon_samples;
        require(wanted_stop >= minimum_propagation_horizon && wanted_stop <= config.propagation_horizon_samples,
                "native HF chain wanted-support stop truncates the complete physical tail");
        if (config.continuing_rf_interference) {
            const long double bound = config.public_endpoint_clock_bound_ppm * 1e-6L;
            const long double fastest = 1 + bound, slowest = 1 - bound;
            long double tail = std::ceil(8 * fastest) + 2;
            if (config.endpoints.frontend.lowpass_hz > 0)
                tail += std::ceil(std::log(1e6L) * fs * fastest /
                    (2 * std::numbers::pi_v<long double> * config.endpoints.frontend.lowpass_hz));
            minimum_wanted_capture = samples_ceil(wanted_stop * fastest + tail +
                (config.endpoints.effects.capture_lead_seconds + config.endpoints.effects.capture_trail_seconds) * fs * fastest);
            require(config.capture_horizon_samples >= minimum_wanted_capture,
                    "native HF chain capture truncates wanted support at the public RX clock bound");
            require(config.propagation_horizon_samples >= samples_ceil(config.capture_horizon_samples / slowest + 8),
                    "native HF chain continuing RF field does not cover the final RX lookahead");
        }
        tx_buffer.resize(config.bridge_chunk_samples);
        propagation_buffer.resize(config.bridge_chunk_samples);
        const auto tx_bridge_bytes = tx_buffer.capacity() * sizeof(Complex);
        const auto propagation_bridge_bytes = propagation_buffer.capacity() * sizeof(Complex);
        const auto outside_propagation_bytes = bytes_add(fixed_overhead_bytes, propagation_bridge_bytes);
        if (bytes_add(outside_propagation_bytes, tx_bridge_bytes) >= config.maximum_workspace_bytes)
            throw ChannelWorkspaceError("native HF chain bridge capacities exhaust workspace");
        auto prepared = Clock::now();
        tx = std::make_unique<StreamingChannel>(source, tx_configuration(config));
        tx_preparation_seconds = seconds_since(prepared);
        require(tx->total_samples() == config.tx_horizon_samples,
                "native HF chain TX horizon truncates the native clock/interpolation tail");
        auto physical_source = source;
        physical_source.geometry.source_samples = config.tx_horizon_samples;
        physical_source.minimum_frequency_hz = config.tx_band.minimum_hz;
        physical_source.maximum_frequency_hz = config.tx_band.maximum_hz;
        physical_source.working_bytes = bytes_add(tx->report().working_bytes, tx_bridge_bytes);
        physical_source.sample_at = [this](std::uint64_t index) { return tx_sample(index); };
        auto physical_config = std::move(config.propagation);
        physical_config.maximum_workspace_bytes = std::min(physical_config.maximum_workspace_bytes,
            config.maximum_workspace_bytes - outside_propagation_bytes);
        prepared = Clock::now();
        propagation = std::make_unique<Channel>(std::move(physical_source), std::move(physical_config),
            config.propagation_seed, config.propagation_horizon_samples, stop);
        propagation_preparation_seconds = seconds_since(prepared);
        preparation_peak_bytes = bytes_add(propagation->report().workspace_bytes, outside_propagation_bytes);
        auto receiver_source = source;
        receiver_source.geometry.source_samples = config.propagation_horizon_samples;
        receiver_source.minimum_frequency_hz = config.propagation_band.minimum_hz;
        receiver_source.maximum_frequency_hz = config.propagation_band.maximum_hz;
        receiver_source.working_bytes = bytes_add(propagation->report().resident_workspace_bytes,
            propagation_bridge_bytes);
        receiver_source.sample_at = [this](std::uint64_t index) { return propagation_sample(index); };
        prepared = Clock::now();
        rx = std::make_unique<StreamingChannel>(std::move(receiver_source), rx_configuration(config));
        rx_preparation_seconds = seconds_since(prepared);
        require(config.continuing_rf_interference || rx->total_samples() == config.capture_horizon_samples,
                "native HF chain capture horizon truncates the native RX clock/frontend tail");
        if (!config.continuing_rf_interference) minimum_wanted_capture = rx->report().natural_complete_samples;
        const auto resident_bytes = bytes_add(rx->report().working_bytes, fixed_overhead_bytes);
        if (std::max(preparation_peak_bytes, resident_bytes) > config.maximum_workspace_bytes)
            throw ChannelWorkspaceError("native HF chain exceeds the combined source/channel/bridge workspace limit");
        cancelled(stop);
    }

    Complex tx_sample(std::uint64_t index) {
        require(index == tx_next && index < config.tx_horizon_samples,
                "native HF chain TX bridge requires consecutive samples");
        if (tx_position == tx_count) {
            cancelled(stop);
            const auto begin = Clock::now();
            tx_count = tx->read(tx_buffer);
            tx_render_seconds += seconds_since(begin);
            tx_position = 0;
            require(tx_count != 0, "native HF chain TX bridge ended before its declared extent");
        }
        ++tx_next;
        return tx_buffer[tx_position++];
    }
    Complex propagation_sample(std::uint64_t index) {
        require(index == propagation_next && index < config.propagation_horizon_samples,
                "native HF chain physical bridge requires consecutive samples");
        if (propagation_position == propagation_count) {
            cancelled(stop);
            const auto begin = Clock::now();
            const double upstream = tx_render_seconds;
            propagation_count = propagation->read(propagation_buffer, stop);
            const double elapsed = seconds_since(begin);
            propagation_inclusive_seconds += elapsed;
            propagation_render_seconds += std::max(0., elapsed - (tx_render_seconds - upstream));
            propagation_position = 0;
            require(propagation_count != 0, "native HF chain physical bridge ended before its declared extent");
        }
        ++propagation_next;
        return propagation_buffer[propagation_position++];
    }
};

HfNativeChain::HfNativeChain(Source source, HfNativeChainConfig config, std::stop_token stop)
    : impl_(std::make_unique<Impl>(std::move(source), std::move(config), stop)) {}
HfNativeChain::~HfNativeChain() = default;
HfNativeChain::HfNativeChain(HfNativeChain&&) noexcept = default;
HfNativeChain& HfNativeChain::operator=(HfNativeChain&&) noexcept = default;
std::size_t HfNativeChain::read(std::span<Complex> values, std::span<std::uint8_t> validity) {
    cancelled(impl_->stop);
    require(validity.empty() || validity.size() == values.size(), "native HF chain validity size differs from output");
    const auto remaining = impl_->config.capture_horizon_samples - impl_->rx->position();
    const auto admitted = static_cast<std::size_t>(std::min<std::uint64_t>(values.size(), remaining));
    values = values.first(admitted);
    if (!validity.empty()) validity = validity.first(admitted);
    const auto begin = Clock::now();
    const double upstream = impl_->propagation_inclusive_seconds;
    const auto count = impl_->rx->read(values, validity);
    impl_->rx_render_seconds += std::max(0., seconds_since(begin) - (impl_->propagation_inclusive_seconds - upstream));
    cancelled(impl_->stop);
    return count;
}
std::uint64_t HfNativeChain::total_samples() const { return impl_->config.capture_horizon_samples; }
std::uint64_t HfNativeChain::position() const { return impl_->rx->position(); }
HfNativeChainReport HfNativeChain::report() const {
    HfNativeChainReport result;
    const auto& c = impl_->config;
    result.tx_horizon_samples = c.tx_horizon_samples;
    result.propagation_horizon_samples = c.propagation_horizon_samples;
    result.capture_horizon_samples = c.capture_horizon_samples;
    result.minimum_propagation_horizon_samples = impl_->minimum_propagation_horizon;
    result.propagation_signal_stop_samples = impl_->wanted_stop;
    result.minimum_wanted_capture_samples = impl_->minimum_wanted_capture;
    result.continuing_rf_interference_at_capture_end = c.continuing_rf_interference;
    result.public_endpoint_clock_bound_ppm = c.public_endpoint_clock_bound_ppm;
    result.tx_band = c.tx_band;
    result.propagation_band = c.propagation_band;
    result.propagation_band_declared_not_analytically_proved =
        c.propagation.fullwave_reflection.has_value() || c.propagation.magnetoionic_reflection.has_value() ||
        c.propagation.wave_optics.has_value() || c.propagation.phase_screens.has_value() ||
        c.propagation.vector_oblique.has_value() || c.propagation.vector_two_hop.has_value();
    result.bridge_chunk_samples = c.bridge_chunk_samples;
    result.fixed_overhead_bytes = impl_->fixed_overhead_bytes;
    result.tx_bridge_bytes = impl_->tx_buffer.capacity() * sizeof(Complex);
    result.propagation_bridge_bytes = impl_->propagation_buffer.capacity() * sizeof(Complex);
    result.preparation_peak_bytes = impl_->preparation_peak_bytes;
    result.tx_preparation_seconds = impl_->tx_preparation_seconds;
    result.propagation_preparation_seconds = impl_->propagation_preparation_seconds;
    result.rx_preparation_seconds = impl_->rx_preparation_seconds;
    result.tx_render_seconds = impl_->tx_render_seconds;
    result.propagation_render_seconds = impl_->propagation_render_seconds;
    result.rx_render_seconds = impl_->rx_render_seconds;
    result.tx = impl_->tx->report();
    result.propagation = impl_->propagation->report();
    result.rx = impl_->rx->report();
    result.resident_bytes = bytes_add(result.rx.working_bytes, result.fixed_overhead_bytes);
    result.workspace_bytes = std::max(result.preparation_peak_bytes, result.resident_bytes);
    return result;
}

} // namespace appraisal
