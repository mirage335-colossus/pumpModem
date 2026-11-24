#include "datapump/robust_profile.hpp"

#include "datapump/audio.hpp"
#include "datapump/robust_audio_frontend.hpp"
#include "datapump/triad_acceleration.hpp"
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

void validate_data_cadence(const Settings& settings, bool helper_enabled) {
    const auto cadence = settings.data_cadence;
    const auto without_helper_or_hops = !helper_enabled && settings.fhss_profile == "off";
    check(cadence == DataCadence::scheduled || cadence == DataCadence::continuous ||
          cadence == DataCadence::scheduled_fill || cadence == DataCadence::scheduled_bridge,
          "Invalid Robust DATA cadence");
    if (cadence == DataCadence::continuous)
        check(without_helper_or_hops && !settings.symbol_burst_wait && !settings.symbol_burst_random,
              "Continuous Robust DATA requires Helper, both Symbol Burst waits, and FHSS off");
    if (cadence == DataCadence::scheduled_fill)
        check(without_helper_or_hops,
              "Scheduled Robust gap fill requires Helper and FHSS off");
}

bool in_range(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

void validate_minimum_spacing(double hz) {
    check(tuning::valid_robust_minimum_carrier_spacing(hz),
          "Robust Minimum carrier spacing must be 0, 3.5, 125, or 265 Hz");
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

std::uint32_t compact_scheduled_stride(const PublicProfileConfig& config, const Geometry& geometry,
                                       const helper::MonoPassband& passband, double audio_timing_seconds) {
    constexpr std::uint32_t longest_compact_stride = 30;
    constexpr std::uint32_t guard_age_seconds = 600;
    const auto rate = config.format.sample_rate;
    if (geometry.source_samples > static_cast<std::uint64_t>(longest_compact_stride) * rate) return 0;

    auto guard = config;
    // Measure the separation using the real receive geometry, independently
    // of the chosen epoch, both endpoints' device rates, and startup time.
    guard.format.epoch = 60;
    guard.format.symbol_ordinal = 0;
    guard.receiver_filter_halo_samples = detail::AudioInput::required_halo_samples(rate,
        config.data_if_hz, passband.lower_hz, passband.upper_hz) + audio::native_roundtrip_filter_halo_samples(rate);
    const auto uncertainty = audio_timing_seconds / 2 + 1. / rate +
        longest_compact_stride * config.audio_pair_ppm * 1e-6;
    guard.capture_lead_seconds += uncertainty;
    guard.arrival_low_seconds -= uncertainty;
    guard.arrival_high_seconds += uncertainty;

    // Keep adjacent 13-epoch hypothesis sets separate even on a tight clock.
    const auto support_seconds = (geometry.source_samples + rate - 1) / rate;
    const auto shortest = std::max<std::uint32_t>(static_cast<std::uint32_t>(receiver_epoch_offsets.size()),
        static_cast<std::uint32_t>(support_seconds));
    for (auto seconds = shortest; seconds <= longest_compact_stride; ++seconds) {
        guard.compact_stride_seconds = seconds;
        const PublicProfile trial(guard);
        // Cover the clock growth of a short exchange. The live receiver
        // continues to apply its full elapsed clock and timestamp bounds.
        const auto slot = trial.slot(guard_age_seconds / seconds + 1);
        if (slot.previous_signal_stop <= slot.observation.first &&
            slot.observation.stop <= slot.next_signal_first && slot.quiet_aperture.size())
            return seconds;
    }
    return 0;
}

helper::Options helper_options(const Settings& s) {
    auto options = s.helper;
    options.data_power_w = std::pow(10., (s.tx_dbm - 30) / 10);
    options.power_w = s.helper.power_w > 0 ? options.data_power_w + s.helper.power_w : 0;
    return options;
}

// Receiver and preview use the same minimum public waveform, including
// finite pulse endpoints and the selected FHSS dwell gates. Its spectrum
// depends on geometry, not the RF address, encryption, or calendar epoch.
ShapingPlan minimum_shaping(const Settings& settings) {
    struct Shape {
        double rate;
        unsigned added_symbols;
        double minimum_carrier_spacing_hz;
        std::uint32_t sample_rate;
        bool fhss;
        bool operator==(const Shape&) const = default;
    };
    using Entry = std::pair<Shape, ShapingPlan>;
    thread_local std::optional<Entry> last;
    const auto& modem = settings.transfer.modem;
    const Shape shape{modem.bandwidth_hz, settings.added_symbols, settings.minimum_carrier_spacing_hz,
                      modem.sample_rate, settings.fhss_profile != "off"};
    validate_minimum_spacing(shape.minimum_carrier_spacing_hz);
    if (!last || last->first != shape) {
        Format format;
        format.inner_rate_hz = shape.rate;
        format.added_symbols = shape.added_symbols;
        format.minimum_carrier_spacing_hz = shape.minimum_carrier_spacing_hz;
        format.sample_rate = shape.sample_rate;
        format.bit_seconds = .001;
        format.fhss = shape.fhss;
        format.quiet_seconds = shape.fhss ? .055 : 0;
        last = Entry{shape, plan_shaping(format)};
    }
    auto result = last->second;
    result.format.added_interleave = settings.added_interleave;
    result.format.encrypt = settings.transfer.key.has_value();
    return result;
}

double gate_bandwidth(const ShapingPlan& shaping) {
    return std::max(0., shaping.spectrum.placement_bandwidth_hz - shaping.geometry.occupied_bandwidth_hz);
}

double allowance_scale(const modem::OscillatorSearchConfig& osc) {
    return 1e-6 / (1 - osc.lf.accuracy_ppm * .5e-6) * osc.margin;
}

double shift_accuracy(const modem::OscillatorSearchConfig& osc) {
    return osc.reference == modem::OscillatorReference::shared_radio ? osc.lf.accuracy_ppm : osc.rf.accuracy_ppm;
}

double modulation_allowance(const std::array<double, 2>& selective, double spread_hz) {
    return (selective[0] || selective[1]) ? (spread_hz > 0 ? spread_hz : .1) : 0;
}

helper::MonoPassband guarded_audio_band(helper::MonoPassband data_band, double high_helper_hz,
                                     double allowance_hz, std::uint32_t sample_rate) {
    if (high_helper_hz > 0) {
        data_band.lower_hz = std::min(data_band.lower_hz, helper::low_pilot_frequency_hz - 100);
        data_band.upper_hz = std::max(data_band.upper_hz, high_helper_hz + 100);
    }
    data_band.lower_hz -= allowance_hz;
    data_band.upper_hz += allowance_hz;
    check(data_band.lower_hz > 0 && data_band.upper_hz < sample_rate / 2.,
          "Robust audio band including oscillator/channel allowance exceeds DC or Nyquist; adjust Rate, Spreading, Minimum carrier spacing, Carrier, Shift, or Helper power");
    return data_band;
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
    return triad::v3::gpu_receiver_backend_available();
}

void validate_settings(const Settings& s) {
    check(!s.finite_duplex || (!s.simulation && s.full_duplex),
          "Finite Robust duplex requires live full duplex");
    (void)triad::v3::parse_wrong_key_admission(s.wrong_key_admission);
    check(!s.gpu || gpu_receiver_available(), "GPU reception is unavailable in this build (CUDA or Vulkan support required)");
    modem::validate(s.transfer.modem);
    check(!s.gpu || s.transfer.modem.search_arithmetic != modem::SearchArithmetic::fp64,
          "FP64 force cannot be combined with the GPU receiver; disable GPU or select automatic/FP32 minimum");
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
    check(!s.auto_calendar || (!s.calendar && !s.finite_duplex),
          "Automatic Robust calendar requires ordinary operation with no explicit anchor");
    check(!s.auto_live_noise || (!s.finite_duplex && !s.live_noise_dbfs_hz),
          "Automatic Robust live noise needs ordinary live reception with no manual density");
    if (s.live_noise_dbfs_hz)
        check(in_range(*s.live_noise_dbfs_hz, -250, 0), "Live input noise must be -250 to 0 dBFS/Hz");
    if (s.receiver_memory_limit)
        check(*s.receiver_memory_limit > 0, "Robust receiver memory limit must be positive");
    check(s.transfer.content_limit > 0, "Robust content quota must be positive");
    check(s.added_symbols > 0 && s.added_symbols <= 1000,
          "Robust Added Symbols must be between 1 and 1000");
    validate_minimum_spacing(s.minimum_carrier_spacing_hz);
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
    validate_data_cadence(s, helper::effective_helper_power(helper_options(s)) > 0);
    check(std::isfinite(s.helper.power_w) && s.helper.power_w >= 0,
          "Robust Helper power must be finite and nonnegative");
    check(s.transfer.fec == FecMode::off || s.transfer.fec == FecMode::rs20 || s.transfer.fec == FecMode::rs60,
          "Invalid Robust error correction mode");
}

namespace detail {
CarrierBand plan_carrier_band(const Settings& settings, std::optional<double> doppler_ratio) {
    const auto& modem = settings.transfer.modem;
    auto geometry_options = modem;
    // Recommendations must be available even if the old carrier is invalid.
    geometry_options.carrier_hz = 0;
    modem::validate(geometry_options);
    check(modem.sample_rate <= 192000 && modem.sample_rate % 5 == 0,
          "Robust audio rate must be a multiple of five up to 192 kHz");
    const auto osc = modem.oscillator_search.value_or(modem::OscillatorSearchConfig{});
    check(osc.sideband == modem::OscillatorSideband::upper,
          "Robust supports only upper-sideband carrier translation");
    check(in_range(osc.rf_shift_hz, 0, 1e15) && in_range(osc.margin, 1, 1000) &&
          in_range(osc.lf.accuracy_ppm, 0, 100000) && in_range(osc.rf.accuracy_ppm, 0, 100000),
          "Invalid Robust Shift, oscillator model, or search margin");
    check(doppler_ratio ? std::isfinite(*doppler_ratio) && *doppler_ratio > 0 :
          std::isfinite(settings.doppler_shift_hz), "Invalid Robust Doppler shift");

    const auto minimum = minimum_shaping(settings);
    CarrierBand result;
    result.sample_rate = modem.sample_rate;
    result.nominal_bandwidth_hz_ = minimum.geometry.occupied_bandwidth_hz;
    result.waveform_bandwidth_hz = result.nominal_bandwidth_hz_ + gate_bandwidth(minimum);
    result.pilot_dwell_gating_ = minimum.format.fhss;
    const auto helper_options_for_band = helper_options(settings);
    result.helper_enabled = helper::effective_helper_power(helper_options_for_band) > 0;
    if(result.helper_enabled) {
        auto format = minimum.format;
        // On the odd phase, the pilot gates fall within the active DATA
        // dwell, instead of inside its quiet time. Preview that stricter
        // phase when the simulation can advance beyond its configured epoch.
        const auto epoch = settings.calendar && !settings.simulation ?
            settings.calendar->epoch : (format.epoch | 1U);
        format.epoch = epoch;
        const auto reference = public_helper_reference(format);
        result.helper_guard_.emplace(helper_options_for_band, format.fhss, &reference);
    }
    const auto spread = tuning::parse_spread_preset(settings.spread_preset);
    const auto relative_doppler = doppler_ratio ? std::abs(*doppler_ratio - 1) : 0.;
    const auto fixed_doppler = doppler_ratio ? relative_doppler * osc.rf_shift_hz :
        std::abs(settings.doppler_shift_hz);
    const auto spread_hz = spread.doppler_hz * osc.margin;
    const bool gaussian_rays = spread.doppler_law == tuning::SpreadPreset::DopplerLaw::gaussian_rays;
    const auto modulation_hz = modulation_allowance(selective_depths(settings.selective_perturbation), spread_hz);
    const auto scale = allowance_scale(osc);
    if (gaussian_rays) {
        result.fixed_allowance_hz_ = osc.rf_shift_hz * shift_accuracy(osc) * scale +
            fixed_doppler + 3 * spread_hz + modulation_hz;
    } else {
        // Preserve the historical bounded preset arithmetic and its capture
        // hashes; Gaussian tails use their distinct 3D-per-edge support.
        result.fixed_allowance_hz_ = osc.rf_shift_hz * shift_accuracy(osc) * scale +
            fixed_doppler + std::abs(spread_hz) + spread_hz + modulation_hz;
    }
    result.proportional_allowance_ = osc.lf.accuracy_ppm * scale + relative_doppler;
    return result;
}

helper::MonoPassband CarrierBand::data_passband(double carrier_hz) const {
    const auto half = .5 * waveform_bandwidth_hz + fixed_allowance_hz_ + proportional_allowance_ * carrier_hz;
    return {carrier_hz - half, carrier_hz + half};
}

double CarrierBand::minimum_carrier_hz(double lower_edge_hz) const {
    if (proportional_allowance_ >= 1) return std::numeric_limits<double>::infinity();
    return (lower_edge_hz + .5 * waveform_bandwidth_hz + fixed_allowance_hz_) / (1 - proportional_allowance_);
}

void CarrierBand::validate(double carrier_hz, std::optional<double> audio_upper_limit_hz) const {
    check(std::isfinite(carrier_hz) && carrier_hz > 0 && carrier_hz < sample_rate / 2.,
          "Robust DATA audio frequency must be positive and below Nyquist");
    check(carrier_hz >= nominal_bandwidth_hz_ / 2,
          "Robust shaped DATA band crosses DC; raise Carrier or lower Minimum carrier spacing");
    const helper::PlacementBand placement{carrier_hz, nominal_bandwidth_hz_,
        waveform_bandwidth_hz - nominal_bandwidth_hz_, pilot_dwell_gating_};
    const auto data = helper::mono_passband(placement);
    const auto high_helper_hz = helper_guard_ ? helper_guard_->high_pilot_hz(placement, sample_rate) : 0;
    const auto allowance = fixed_allowance_hz_ + proportional_allowance_ * carrier_hz;
    const auto guarded = guarded_audio_band(data, high_helper_hz, allowance, sample_rate);
    if (audio_upper_limit_hz)
        check(guarded.upper_hz <= *audio_upper_limit_hz, "Robust profile exceeds the planned audio converter passband");
}

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
    const bool helper_enabled = helper::effective_helper_power(helper) > 0;
    validate_data_cadence(s, helper_enabled);

    check(osc.sideband == modem::OscillatorSideband::upper,
          "Robust supports only upper-sideband carrier translation");

    const bool shifted = osc.rf_shift_hz > 0;
    const bool shared = shifted && osc.reference == modem::OscillatorReference::shared_radio;
    const auto shift_ppm = shifted ? shift_accuracy(osc) : 0;
    const auto denominator = 1 - osc.lf.accuracy_ppm * .5e-6;
    const auto base_diffusion = osc.lf.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
    const auto shift_diffusion = osc.rf.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;

    ReceiverConfig rx;
    rx.wrong_key_admission = parse_wrong_key_admission(s.wrong_key_admission);
    if (modem.search_arithmetic == modem::SearchArithmetic::fp64)
        rx.native_coarse_precision = NativeCoarsePrecision::fp64;
    else if (modem.search_arithmetic == modem::SearchArithmetic::fp32)
        rx.native_coarse_precision = NativeCoarsePrecision::fp32;
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
    const bool gaussian_rays = spread.doppler_law == tuning::SpreadPreset::DopplerLaw::gaussian_rays;
    // The receiver models the nominal two-ray geometry. Margin enlarges the
    // public delay and spectral search support below, not the physical rays.
    rx.spread_delay_seconds = spread.delay_ms * .001 * (gaussian_rays ? 1 : osc.margin);
    rx.spread_doppler_hz = gaussian_rays ? 0 : spread.doppler_hz * osc.margin;
    rx.random_doppler_width_hz = gaussian_rays ? spread.doppler_hz : 0;
    rx.relative_phase_diffusion_rad_sqrt_s = shifted && !shared
        ? std::hypot(base_diffusion, shift_diffusion) : base_diffusion;
    rx.selective_slow_depth = slow_depth;
    rx.selective_fast_depth = fast_depth;
    // Explicit amplitude modulation with no ray spread still has a separate
    // synthetic rate. Off bypasses the stage even when ray Doppler is present.
    rx.selective_modulation_rate_hz = modulation_allowance({slow_depth, fast_depth},
        gaussian_rays ? spread.doppler_hz * osc.margin : rx.spread_doppler_hz);
    rx.time_varying_channel = rx.spread_delay_seconds > 0 || rx.spread_doppler_hz != 0 ||
        rx.random_doppler_width_hz > 0 || rx.selective_modulation_rate_hz > 0 || s.doppler_shift_hz != 0;
    // A selected atmospheric spread must also allow the whole signal to move,
    // including with GPS-disciplined oscillators and the explicit Doppler at
    // zero. Keep this allowance outside the legacy constant-oscillator grid;
    // the bounded changing-channel proposal searches its public support.
    // For the Gaussian law, one edge receives 3D: D is the full 2-sigma
    // width, so this is six sigma per edge. The intrinsic 3D core is already
    // accounted for by ReceiverConfig.random_doppler_width_hz.
    rx.time_varying_atmospheric_hz = gaussian_rays
        ? 3 * rx.random_doppler_width_hz * (osc.margin - 1)
        : std::abs(rx.spread_doppler_hz);
    const auto simulated_common_doppler_hz = std::abs(s.doppler_shift_hz) +
        (gaussian_rays ? 0 : rx.time_varying_atmospheric_hz);
    rx.time_varying_common_doppler_hz = simulated_common_doppler_hz;
    if (gaussian_rays) {
        // Offer a narrow linear-drift fallback within the already reserved
        // public center allowance. Keep the standard simulated rays centered.
        rx.time_varying_common_doppler_hz += std::min(rx.time_varying_atmospheric_hz,
            .5 * rx.random_doppler_width_hz);
    }
    rx.helper_enabled = helper_enabled;

    auto minimum = minimum_shaping(s);
    minimum.format.configured_carrier_hz = modem.carrier_hz + osc.rf_shift_hz;
    minimum.format.epoch = calendar.epoch;
    minimum.format.symbol_ordinal = calendar.initial_ordinal;
    if (stop.stop_requested()) throw Error("Robust preparation cancelled");
    DurationOptions duration;
    const auto bit_budget = (1 - duration.correct_probability) / (2 * duration.planning_bits);
    rx.time_varying_minimum_bit_log_separation = std::log((1 - bit_budget) / bit_budget);
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

    PublicProfileConfig configuration;
    configuration.format = shaping.format;
    configuration.data_if_hz = modem.carrier_hz;
    configuration.gate_bandwidth_hz = gate_bandwidth(shaping);
    configuration.helper = std::move(helper);
    configuration.helper_enabled = rx.helper_enabled;
    configuration.wait_bits = s.symbol_burst_wait;
    configuration.random_extra_bits = s.symbol_burst_random;
    configuration.continuous_data = s.data_cadence == DataCadence::continuous;
    configuration.scheduled_gap_fill = s.data_cadence == DataCadence::scheduled_fill;
    configuration.scheduled_gap_bridge = s.data_cadence == DataCadence::scheduled_bridge;
    configuration.arrival_low_seconds = arrival.lower;
    configuration.arrival_high_seconds = arrival.upper;
    configuration.capture_lead_seconds = std::max(.25, -arrival.lower + .25);
    configuration.capture_trail_seconds = .25;
    configuration.audio_pair_ppm = 2 * nominal_endpoint_ppm;
    configuration.clock_bound_ppm = rx.clock_bound_ppm;
    configuration.cfo_bound_hz = rx.frequency_bound_hz + rx.time_varying_atmospheric_hz;
    configuration.spread_delay_ms = gaussian_rays ? spread.delay_ms * osc.margin :
        1000 * rx.spread_delay_seconds;
    configuration.spread_doppler_hz = rx.spread_doppler_hz;
    configuration.selective_signal_tail = rx.selective_modulation_rate_hz > 0;
    rx.source_spectral_skirt_allowance_hz = configuration.gate_bandwidth_hz / 2;
    const auto data_passband = helper::mono_passband({configuration.data_if_hz,
        geometry.occupied_bandwidth_hz, configuration.gate_bandwidth_hz, configuration.format.fhss});
    double skirts = rx.frequency_bound_hz + rx.time_varying_atmospheric_hz;
    if (gaussian_rays) skirts += 3 * rx.random_doppler_width_hz;
    skirts += rx.spread_doppler_hz + rx.selective_modulation_rate_hz;
    if (configuration.scheduled_gap_fill) {
        // Reserve the audio projection and both endpoint transport filters in
        // the shared source guard. The simulator captures complex I/Q directly.
        const auto passband = guarded_audio_band(data_passband, 0, skirts, fs);
        const auto halo = AudioInput::required_halo_samples(fs, configuration.data_if_hz,
            passband.lower_hz, passband.upper_hz) + audio::native_roundtrip_filter_halo_samples(fs);
        configuration.gap_fill_bounds = ScheduledGapFillBounds{configuration.capture_lead_seconds,
            configuration.arrival_low_seconds, configuration.arrival_high_seconds, halo};
    }
    if (s.auto_calendar && !shifted && !rx.time_varying_channel && !configuration.helper_enabled &&
        !configuration.format.fhss && !configuration.continuous_data && !configuration.scheduled_gap_fill &&
        !configuration.wait_bits && !configuration.random_extra_bits) {
        const auto passband = guarded_audio_band(data_passband, 0, skirts, fs);
        configuration.compact_stride_seconds = compact_scheduled_stride(configuration, geometry,
            passband, s.transfer.audio_timing_error_seconds);
    }
    PublicProfile waveform(std::move(configuration), s.transfer.key);
    if (s.receiver_memory_limit && waveform.emission_source_workspace_bytes() >= *s.receiver_memory_limit)
        throw Error("Robust native waveform workspace exceeds the configured session memory limit");
    rx.helper = waveform.helper_plan();
    const auto power = data_power_reference(waveform.config().format, key, stop);
    const auto reference_gain = 1 / std::sqrt(power.mean_norm);

    const auto passband = guarded_audio_band(data_passband, rx.helper.frequency_hz, skirts, fs);

    StreamChannelConfig channel;
    auto& effects = channel.effects;
    effects.sample_rate = fs;
    effects.occupied_bandwidth_hz = geometry.occupied_bandwidth_hz;
    effects.rf_hz = osc.rf_shift_hz;
    effects.nominal_conversion_if_hz = modem.carrier_hz;
    effects.capture_lead_seconds = waveform.config().capture_lead_seconds;
    effects.capture_trail_seconds = waveform.config().capture_trail_seconds;
    // The GUI declares support, not the exact realization received by DATA.
    // Common atmospheric shift evolves independently of the echo trajectory;
    // its fixed diagnostic counterpart remains available in StreamingChannel.
    effects.static_doppler_hz = 0;
    channel.path_evolution.common_doppler_wander_hz = simulated_common_doppler_hz;
    const auto knot_seconds = [](double bound) {
        return std::max(1., 1. / std::max(bound, 1. / 3600.));
    };
    channel.path_evolution.common_doppler_knot_seconds = knot_seconds(simulated_common_doppler_hz);
    effects.spread_delay_ms = gaussian_rays ? spread.delay_ms : waveform.config().spread_delay_ms;
    effects.spread_doppler_hz = rx.spread_doppler_hz;
    if (!gaussian_rays && (rx.spread_delay_seconds > 0 || rx.spread_doppler_hz != 0))
        channel.path_evolution.bounded_spread_knot_seconds = knot_seconds(std::abs(rx.spread_doppler_hz));
    channel.path_evolution.random_doppler_width_hz = rx.random_doppler_width_hz;
    effects.perturbation_slow_depth = slow_depth;
    effects.perturbation_fast_depth = fast_depth;
    // Gaussian-erf coefficients renew randomly. The native channel admits ten
    // spectral widths per edge, so retain the same declared receive allowance
    // rather than silently multiplying the GUI's required audio passband.
    channel.selective_model.coefficient_law = SelectiveCoefficientLaw::GaussianErf;
    channel.selective_model.modulation_rate_hz = rx.selective_modulation_rate_hz / 10;
    // The carrier spacing control moves the waveform through the same channel.
    channel.selective_model.correlation_spacing_hz = geometry.nominal_carrier_bandwidth_hz;
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
    rx.data_basis_gain = reference_gain;
    if (s.simulation) {
        rx.noise_variance = channel.complex_noise_variance;
    } else if (s.live_noise_dbfs_hz) {
        apply_live_noise_model(rx, s.target_cn0_db_hz, *s.live_noise_dbfs_hz);
    }
    return {std::move(waveform), rx, std::move(channel), std::move(key), passband.lower_hz, passband.upper_hz,
            data_passband.lower_hz - skirts, data_passband.upper_hz + skirts, maximum_unscaled_data, std::move(hops)};
}

void apply_live_noise_model(triad::v3::ReceiverConfig& receiver, double target_cn0_db_hz,
                            double noise_dbfs_hz) {
    // The analytic projection is unit norm on real PCM. Use the one-sided
    // density for the proper-complex covariance and the coupled DATA gain.
    const auto density = std::pow(10., noise_dbfs_hz / 10);
    receiver.noise_variance = receiver.sample_rate_hz * density;
    receiver.data_basis_gain *= std::sqrt(std::pow(10., target_cn0_db_hz / 10) * density);
}

Profile with_capture_uncertainty(const Profile& input, double provider_seconds, double anchor_seconds,
                                 std::uint64_t filter_halo_samples,
                                 std::optional<triad::v3::HelperBurstCoordinate> receive_origin) {
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
    if (receive_origin) config.receive_origin = receive_origin;
    if (config.scheduled_gap_fill || config.scheduled_gap_bridge)
        config.gap_fill_transport_halo_samples = audio::native_roundtrip_filter_halo_samples(config.format.sample_rate);
    Profile result = input;
    const auto settling_delay_seconds = result.receiver.random_doppler_width_hz > 0
        ? .001 * config.spread_delay_ms : result.receiver.spread_delay_seconds;
    result.receiver.capture_tail_bound_seconds = std::max(result.receiver.capture_tail_bound_seconds,
        settling_delay_seconds + static_cast<double>(filter_halo_samples) /
            input.waveform.effective_sample_rate());
    result.waveform = triad::v3::PublicProfile(std::move(config),
        input.waveform.config().format.encrypt ? std::optional<Crypto>{input.key} : std::nullopt);
    return result;
}

} // namespace detail
} // namespace datapump::live
