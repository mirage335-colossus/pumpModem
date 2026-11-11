#include "datapump/robust_profile.hpp"

#include "datapump/tuning.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <string_view>

namespace datapump::live {
namespace {
using namespace triad::v3;

void check(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

bool in_range(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

std::array<double, 2> selective_depths(std::string_view value) {
    if (value == "off") return {};
    const auto separator = value.find('-');
    if (separator == std::string_view::npos) throw Error("Invalid Robust selective amplitude setting");
    const auto name = value.substr(0, separator);
    value.remove_prefix(separator + 1);
    int depth = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), depth);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        (depth != 1 && depth != 5 && depth != 10 && depth != 25 && depth != 65) ||
        (name != "slow" && name != "fast" && name != "each"))
        throw Error("Invalid Robust selective amplitude setting");
    return {name == "fast" ? 0 : depth / 100., name == "slow" ? 0 : depth / 100.};
}

struct PhysicalPrior {
    double lower, upper, simulated_center, simulated_radius;
};
PhysicalPrior arrival_prior(const Settings& settings) {
    const auto lag = settings.transfer.audio_timing_error_seconds;
    const auto policy = clock_sync::effective_policy(settings.transfer.clock_sync, settings.fhss_profile != "off");
    const auto center = lag / 2 + (policy ? policy->offset_seconds : 0);
    // Default retains the broad public arrival search. Dated live capture
    // contributes its own actual provider uncertainty after profile planning.
    const auto radius = lag / 2 + (policy ? policy->half_window_seconds() : 7.);
    return {center - radius, center + radius, center, radius};
}

helper::Options helper_options(const Settings& s) {
    auto options = s.helper;
    options.data_power_w = std::pow(10., (s.tx_dbm - 30) / 10);
    options.power_w = s.helper.power_w > 0 ? options.data_power_w + s.helper.power_w : 0;
    return options;
}

// The canonical source bounds each coefficient vector's norm by sqrt(3).
// Piecewise-linear pulse tables let us bound the sum of |pulse| by checking
// the fractional table breakpoints. Envelope and phase cannot grow that sum.
double source_peak(PulseShape pulse) {
    double maximum = 0;
    for (unsigned step = 0; step <= 256; ++step) {
        double sum = 0;
        for (int chip = -8; chip <= 9; ++chip)
            sum += std::abs(shaping_pulse(pulse, step / 256. - chip));
        maximum = std::max(maximum, sum);
    }
    return maximum * std::sqrt(3.);
}

std::uint64_t simulation_seed() {
    std::random_device random;
    return (static_cast<std::uint64_t>(random()) << 32) ^ static_cast<std::uint64_t>(random());
}

std::vector<double> illustrated_hops(const Settings& settings, double bandwidth) {
    if (settings.fhss_profile == "off") return {};
    if (settings.fhss_profile == "fake-list") return settings.fhss_frequencies_hz;
    const auto audio = settings.transfer.modem.carrier_hz;
    const auto shift = settings.transfer.modem.oscillator_search.value_or(modem::OscillatorSearchConfig{}).rf_shift_hz;
    const auto rf = audio + shift;
    const auto guarded = 1.25 * bandwidth;
    double lower = 0, upper = 0, spacing = guarded;
    unsigned channels = 0;
    // These are the saved illustrative profile names and channel spacings.
    // None of them is a regulatory assertion or a request to tune a radio.
    if (rf < 800e6) {
        upper = 800e6;
        channels = 200;
    } else if (settings.fhss_profile == "fake-fcc") {
        spacing = std::max(25000., guarded);
        if (rf >= 902e6 && rf <= 928e6) {
            lower = 902e6; upper = 928e6; channels = guarded < 250000 ? 50 : 25;
        } else if (rf >= 2400e6 && rf <= 2483.5e6) {
            lower = 2400e6; upper = 2483.5e6; channels = 74 * spacing + guarded <= upper - lower ? 75 : 15;
        } else if (rf >= 5725e6 && rf <= 5850e6) {
            lower = 5725e6; upper = 5850e6; channels = 75;
        }
    } else {
        if (rf >= 863e6 && rf < 868.6e6) {
            lower = 863e6; upper = 868.6e6; channels = 47; spacing = std::max(100000., guarded);
        } else if (rf >= 2400e6 && rf <= 2483.5e6) {
            lower = 2400e6; upper = 2483.5e6; channels = 79; spacing = std::max(1e6, guarded);
        } else if (rf >= 5725e6 && rf <= 5875e6) {
            lower = 5725e6; upper = 5875e6; channels = 20; spacing = std::max(7.2e6, guarded);
        }
    }
    if (!channels) throw Error("No illustrative Robust FHSS profile matches this Carrier; choose Off or a custom list");
    const auto first_allowed = std::max(audio, lower + guarded / 2);
    const auto last_allowed = upper - guarded / 2;
    if (rf < 800e6 && last_allowed >= first_allowed)
        channels = static_cast<unsigned>(std::min(200., 1 + std::floor((last_allowed - first_allowed) / spacing)));
    const auto span = (channels - 1) * spacing;
    if (channels < 2 || span > last_allowed - first_allowed)
        throw Error("The complete illustrative Robust FHSS channel set does not fit the selected profile");
    const auto first = std::clamp(rf, first_allowed, last_allowed - span);
    std::vector<double> result;
    result.reserve(channels);
    for (unsigned i = 0; i < channels; ++i) result.push_back(first + i * spacing - audio);
    return result;
}
}

bool gpu_receiver_available() noexcept {
#if defined(DATAPUMP_HAVE_TRIAD_VULKAN)
    return true;
#else
    return false;
#endif
}

void validate_settings(const Settings& s) {
    check(!s.gpu || gpu_receiver_available(), "GPU reception is unavailable in this build (Vulkan support required)");
    modem::validate(s.transfer.modem);
    const auto fs = s.transfer.modem.sample_rate;
    check(fs <= 192000 && fs % 5 == 0, "Robust audio rate must be a multiple of five up to 192 kHz");
    check(s.transfer.modem.carrier_hz > 0 && s.transfer.modem.carrier_hz < fs / 2.,
          "Robust DATA audio frequency must be positive and below Nyquist");
    check(!s.device.empty() && s.device.size() <= 1024 && s.device.find('\0') == std::string::npos,
          "Select a valid Robust audio device");
    audio::Options audio_options;
    audio_options.transmit_gain = s.transmit_gain;
    audio_options.exclusive = s.exclusive;
    audio::validate_option_values(audio_options);
    check(in_range(s.tx_dbm, -200, 100) && in_range(s.path_loss_db, 0, 500) &&
          in_range(s.noise_dbm_hz, -250, 0) && in_range(s.target_cn0_db_hz, -200, 200),
          "Robust link budget is outside its supported range");
    check(std::isfinite(s.doppler_shift_hz), "Invalid Robust Doppler shift");
    if (s.calendar) check(s.calendar->epoch >= 6 && s.calendar->epoch <= 4294967289ULL,
                          "Robust calendar epoch must be between 6 and 4294967289 UTC seconds");
    if (s.live_noise_dbfs_hz)
        check(in_range(*s.live_noise_dbfs_hz, -250, 0), "Live input noise must be -250 to 0 dBFS/Hz");
    if (s.receiver_memory_limit)
        check(*s.receiver_memory_limit > 0, "Robust receiver memory limit must be positive");
    check(s.transfer.content_limit > 0, "Robust content quota must be positive");
    check(s.added_symbols > 0 && s.added_symbols <= 1000,
          "Robust Added Symbols must be between 1 and 1000");
    check(s.symbol_burst_wait == 0 || s.symbol_burst_wait == 1 || s.symbol_burst_wait == 2 ||
          s.symbol_burst_wait == 3 || s.symbol_burst_wait == 5 || s.symbol_burst_wait == 10,
          "Invalid Robust Symbol_Burst minimum wait");
    check(s.symbol_burst_random <= 3, "Robust Symbol_Burst random extra must be 0 to 3");
    check(s.fhss_profile == "off" || s.fhss_profile == "fake-fcc" ||
          s.fhss_profile == "fake-eu" || s.fhss_profile == "fake-list", "Invalid Robust FHSS profile");
    check(s.fhss_frequencies_hz.size() <= 200 &&
          (s.fhss_profile != "fake-list" || s.fhss_frequencies_hz.size() >= 2),
          "Robust FHSS List requires 2 to 200 frequencies");
    for (std::size_t i = 0; i < s.fhss_frequencies_hz.size(); ++i) {
        const auto hz = s.fhss_frequencies_hz[i];
        const auto previous = s.fhss_frequencies_hz.begin() + static_cast<std::ptrdiff_t>(i);
        check(std::isfinite(hz) && hz >= 0 &&
              std::find(s.fhss_frequencies_hz.begin(), previous, hz) == previous,
              "Robust FHSS List requires finite, distinct nonnegative Shift frequencies");
    }
    if (s.transfer.clock_sync) clock_sync::validate(*s.transfer.clock_sync);
    clock_sync::validate_audio_error(s.transfer.audio_timing_error_seconds);
    const auto osc = s.transfer.modem.oscillator_search.value_or(modem::OscillatorSearchConfig{});
    check(in_range(osc.rf_shift_hz, 0, 1e15) && in_range(osc.margin, 1, 1000),
          "Invalid Robust Shift or search margin");
    for (const auto model : {osc.lf, osc.rf})
        check(in_range(model.accuracy_ppm, 0, 100000) &&
              in_range(model.phase_noise_degrees_per_sqrt_second, 0, 360),
              "Invalid Robust oscillator model");
    (void)tuning::parse_spread_preset(s.spread_preset);
    (void)selective_depths(s.selective_perturbation);
    helper::validate(helper_options(s));
    check(std::isfinite(s.helper.power_w) && s.helper.power_w >= 0,
          "Robust Helper power must be finite and nonnegative");
    check(s.transfer.fec == FecMode::off || s.transfer.fec == FecMode::rs20 || s.transfer.fec == FecMode::rs60,
          "Invalid Robust error correction mode");
}

namespace detail {
Profile prepare_profile(const Settings& s, Calendar calendar, std::stop_token stop,
                        ProfilePreparationOptions options) {
    using namespace triad::v3;
    if (stop.stop_requested()) throw Error("Robust preparation cancelled");
    const auto& modem = s.transfer.modem;
    if (modem.receiver_strategy != modem::ReceiverStrategy::automatic)
        throw Error("The complete native Robust receiver requires the Automatic receiver strategy");
    const auto osc = modem.oscillator_search.value_or(modem::OscillatorSearchConfig{});
    const auto fs = modem.sample_rate;
    const auto spread = tuning::parse_spread_preset(s.spread_preset);
    const auto [slow_depth, fast_depth] = selective_depths(s.selective_perturbation);
    const auto arrival = arrival_prior(s);
    auto key = s.transfer.key.value_or(Crypto::amateur());
    auto helper = helper_options(s);

    Format format;
    format.inner_rate_hz = modem.bandwidth_hz;
    format.added_symbols = s.added_symbols;
    format.added_interleave = s.added_interleave;
    format.sample_rate = fs;
    format.bit_seconds = .001; // native minimum protected tile
    format.fhss = s.fhss_profile != "off";
    format.quiet_seconds = format.fhss ? .055 : 0;
    format.encrypt = s.transfer.key.has_value();
    format.epoch = calendar.epoch;
    format.symbol_ordinal = calendar.initial_ordinal;

    const bool shifted = osc.rf_shift_hz > 0;
    const bool shared = shifted && osc.reference == modem::OscillatorReference::shared_radio;
    const auto shift_ppm = shifted ? (shared ? osc.lf.accuracy_ppm : osc.rf.accuracy_ppm) : 0;
    const auto denominator = 1 - osc.lf.accuracy_ppm * .5e-6;
    const auto base_diffusion = osc.lf.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
    const auto shift_diffusion = osc.rf.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;

    ReceiverConfig rx;
    rx.sample_rate_hz = fs;
    rx.data_if_hz = modem.carrier_hz;
    rx.clock_bound_ppm = osc.lf.accuracy_ppm / denominator * osc.margin;
    const auto nominal_endpoint_ppm = osc.lf.accuracy_ppm / 2;
    rx.endpoint_clock_bound_ppm = nominal_endpoint_ppm * osc.margin;
    rx.frequency_bound_hz = (osc.rf_shift_hz * shift_ppm + modem.carrier_hz * osc.lf.accuracy_ppm) *
        1e-6 / denominator * osc.margin;
    // Actual radial motion is an impairment, not a silent TX retune. The
    // public search includes either direction within the declared magnitude.
    rx.frequency_bound_hz += std::abs(s.doppler_shift_hz);
    rx.spread_delay_seconds = spread.delay_ms * .001 * osc.margin;
    rx.spread_doppler_hz = spread.doppler_hz * osc.margin;
    rx.relative_phase_diffusion_rad_sqrt_s = shifted && !shared
        ? std::hypot(base_diffusion, shift_diffusion) : base_diffusion;
    rx.selective_slow_depth = slow_depth;
    rx.selective_fast_depth = fast_depth;
    // Explicit amplitude modulation with no ray spread still has a separate
    // synthetic rate. Off bypasses the stage even when ray Doppler is present.
    rx.selective_modulation_rate_hz = (slow_depth || fast_depth)
        ? (rx.spread_doppler_hz > 0 ? rx.spread_doppler_hz : .1) : 0;
    rx.helper_enabled = helper::effective_helper_power(helper) > 0;

    const auto minimum = plan_shaping(format);
    if (stop.stop_requested()) throw Error("Robust preparation cancelled");
    DurationOptions duration;
    duration.target_cn0_db_hz = s.target_cn0_db_hz;
    duration.arrival_lower_seconds = arrival.lower;
    duration.arrival_upper_seconds = arrival.upper;
    duration.helper_lead_seconds = rx.helper_enabled ? helper_lead_seconds : 0;
    duration.pulse_halo_seconds = static_cast<double>(minimum.geometry.calendar_origin_sample) / fs;
    duration.manual_active_seconds = options.fixture_active_seconds;
    // Limit the public model to a locally representable source interval. The
    // receiver separately admits its exact per-window workspace.
    const auto plan = plan_duration(minimum, rx, duration);
    if (stop.stop_requested()) throw Error("Robust preparation cancelled");
    if (!plan.shaping) throw Error("Robust target SNR has no bounded native duration: " + plan.reason);
    const auto& shaping = *plan.shaping;
    const auto& geometry = shaping.geometry;
    if (s.receiver_memory_limit && geometry.source_workspace_bytes >= *s.receiver_memory_limit)
        throw Error("Robust native waveform workspace exceeds the configured session memory limit");

    PublicProfileConfig configuration;
    configuration.format = shaping.format;
    configuration.data_if_hz = modem.carrier_hz;
    configuration.gate_bandwidth_hz = std::max(0., shaping.spectrum.placement_bandwidth_hz - geometry.occupied_bandwidth_hz);
    configuration.helper = std::move(helper);
    configuration.helper_enabled = rx.helper_enabled;
    configuration.wait_bits = s.symbol_burst_wait;
    configuration.random_extra_bits = s.symbol_burst_random;
    configuration.arrival_low_seconds = arrival.lower;
    configuration.arrival_high_seconds = arrival.upper;
    configuration.capture_lead_seconds = std::max(.25, -arrival.lower + .25);
    configuration.capture_trail_seconds = .25;
    configuration.audio_pair_ppm = 2 * nominal_endpoint_ppm;
    configuration.clock_bound_ppm = rx.clock_bound_ppm;
    configuration.cfo_bound_hz = rx.frequency_bound_hz;
    configuration.spread_delay_ms = 1000 * rx.spread_delay_seconds;
    configuration.spread_doppler_hz = rx.spread_doppler_hz;
    configuration.selective_signal_tail = rx.selective_modulation_rate_hz > 0;
    rx.source_spectral_skirt_allowance_hz = configuration.gate_bandwidth_hz / 2;
    PublicProfile waveform(std::move(configuration), s.transfer.key);
    rx.helper = waveform.helper_plan();
    const auto power = data_power_reference(waveform.config().format, key, stop);
    const auto reference_gain = 1 / std::sqrt(power.mean_norm);

    auto passband = helper::mono_passband(waveform.placement());
    const auto data_passband = passband;
    if (rx.helper_enabled) {
        passband.lower_hz = std::min(passband.lower_hz, helper::low_pilot_frequency_hz - 100);
        passband.upper_hz = std::max(passband.upper_hz, rx.helper.frequency_hz + 100);
    }
    const double skirts = rx.frequency_bound_hz + rx.spread_doppler_hz + rx.selective_modulation_rate_hz;
    const auto minimum_hz = passband.lower_hz - skirts;
    const auto maximum_hz = passband.upper_hz + skirts;
    check(minimum_hz > 0 && maximum_hz < fs / 2.,
          "Robust audio band including oscillator/channel allowance exceeds DC or Nyquist; adjust Rate, Spreading, Carrier, Shift, or Helper power");

    StreamChannelConfig channel;
    auto& effects = channel.effects;
    effects.sample_rate = fs;
    effects.occupied_bandwidth_hz = geometry.occupied_bandwidth_hz;
    effects.rf_hz = osc.rf_shift_hz;
    effects.nominal_conversion_if_hz = modem.carrier_hz;
    effects.capture_lead_seconds = waveform.config().capture_lead_seconds;
    effects.capture_trail_seconds = waveform.config().capture_trail_seconds;
    effects.static_doppler_hz = s.doppler_shift_hz;
    effects.spread_delay_ms = waveform.config().spread_delay_ms;
    effects.spread_doppler_hz = rx.spread_doppler_hz;
    effects.perturbation_slow_depth = slow_depth;
    effects.perturbation_fast_depth = fast_depth;
    channel.selective_model.modulation_rate_hz = rx.selective_modulation_rate_hz;
    channel.selective_model.correlation_spacing_hz = geometry.carrier_spacing_hz;
    channel.maximum_chunk_samples = 4096;
    if (s.simulation) {
        effects.seed = options.simulation_seed ? *options.simulation_seed : simulation_seed();
        std::mt19937_64 random(effects.seed);
        const auto draw = [&](double radius) {
            return std::uniform_real_distribution<double>(-radius, radius)(random);
        };
        effects.tx.audio_ppm = draw(nominal_endpoint_ppm);
        effects.rx.audio_ppm = draw(nominal_endpoint_ppm);
        effects.tx.rf_ppm = shared ? effects.tx.audio_ppm : draw(shift_ppm / 2);
        effects.rx.rf_ppm = shared ? effects.rx.audio_ppm : draw(shift_ppm / 2);
        effects.tx.phase_noise_rad_per_sqrt_second = rx.relative_phase_diffusion_rad_sqrt_s / std::sqrt(2.);
        effects.rx.phase_noise_rad_per_sqrt_second = effects.tx.phase_noise_rad_per_sqrt_second;
        effects.timing_offset_seconds = arrival.simulated_center + draw(arrival.simulated_radius);
    }
    const auto actual_cn0 = s.tx_dbm - s.path_loss_db - s.noise_dbm_hz;
    channel.complex_noise_variance = fs / std::pow(10., actual_cn0 / 10);
    const auto maximum_unscaled_data = source_peak(shaping.format.pulse_shape);
    auto hops = illustrated_hops(s, shaping.spectrum.placement_bandwidth_hz);
    if (s.simulation) {
        rx.noise_variance = channel.complex_noise_variance;
        rx.data_basis_gain = reference_gain;
    } else if (s.live_noise_dbfs_hz) {
        // One-sided PSD bound for normalized real PCM, propagated through the
        // unit-norm analytic projection (see AudioInput below). Reserve the
        // proper-complex covariance enclosing both real component axes.
        rx.noise_variance = fs * std::pow(10., *s.live_noise_dbfs_hz / 10);
        rx.data_basis_gain = std::sqrt(std::pow(10., s.target_cn0_db_hz / 10) *
                                       rx.noise_variance / fs) * reference_gain;
    }
    return {std::move(waveform), rx, std::move(channel), std::move(key), minimum_hz, maximum_hz,
            data_passband.lower_hz - skirts, data_passband.upper_hz + skirts, maximum_unscaled_data, std::move(hops)};
}

Profile with_capture_uncertainty(const Profile& input, double provider_seconds, double anchor_seconds,
                                 std::uint64_t filter_halo_samples) {
    if (!std::isfinite(provider_seconds) || provider_seconds < 0 || provider_seconds > 60)
        throw Error("Robust audio timestamp uncertainty exceeds the supported 60-second bound");
    if (!std::isfinite(anchor_seconds) || anchor_seconds < 0)
        throw Error("Invalid Robust capture calendar offset bound");
    const auto seconds = provider_seconds + anchor_seconds;
    auto config = input.waveform.config();
    config.arrival_low_seconds -= seconds;
    config.arrival_high_seconds += seconds;
    config.capture_lead_seconds += seconds;
    config.receiver_filter_halo_samples = filter_halo_samples;
    Profile result = input;
    result.receiver.capture_tail_bound_seconds = std::max(result.receiver.capture_tail_bound_seconds,
        result.receiver.spread_delay_seconds + static_cast<double>(filter_halo_samples) /
            input.waveform.effective_sample_rate());
    result.waveform = triad::v3::PublicProfile(std::move(config),
        input.waveform.config().format.encrypt ? std::optional<Crypto>{input.key} : std::nullopt);
    return result;
}

} // namespace detail
} // namespace datapump::live
