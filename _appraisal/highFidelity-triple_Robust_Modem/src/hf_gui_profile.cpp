#include "appraisal/hf_gui_profile.hpp"

#include <array>
#include <cmath>
#include <string_view>

namespace appraisal {
namespace {

void require(bool condition, const char* reason) {
    if (!condition) throw datapump::Error(reason);
}

bool bounded(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

void validate_options(const HfGuiProfileOptions& options) {
    constexpr std::array<std::string_view, 6> spreads{
        "none", "hf-best", "hf-moderate", "hf-worst", "aurora-best", "aurora-worst"};
    bool known_spread = false;
    for (const auto candidate : spreads) known_spread |= candidate == options.spread;
    require(known_spread, "HF GUI profile requires a declared HF/Aurora spread ID or none");
    require(options.arrival == "tight" || options.arrival == "default",
            "HF GUI arrival must be tight or default");
    require(bounded(options.margin, 1, 1000), "HF GUI Margin must be in [1,1000]");
    require(bounded(options.audio_ppm, 0, 100000) && bounded(options.rf_ppm, 0, 100000),
            "HF GUI oscillator accuracy must be in [0,100000] ppm");
    require(bounded(options.audio_phase_degrees_sqrt_s, 0, 360) &&
            bounded(options.rf_phase_degrees_sqrt_s, 0, 360),
            "HF GUI phase diffusion must be in [0,360] degrees/sqrt(second)");
    require(bounded(options.target_cn0_db_hz, -200, 200) &&
            bounded(options.actual_cn0_db_hz, -200, 200),
            "HF GUI target and actual C/N0 must be in [-200,200] dB-Hz");
    require(!options.fixture_bit_seconds || bounded(*options.fixture_bit_seconds, .001, 1e7),
            "HF GUI fixture duration must be in [.001,10000000] seconds");
    require(!options.memory_bytes || *options.memory_bytes > 0,
            "HF GUI memory limit must be positive when supplied");
}

} // namespace

datapump::live::detail::Profile prepare_hf_gui_profile(
    const HfGuiProfileOptions& options, std::stop_token stop) {
    validate_options(options);
    datapump::live::Settings settings;
    settings.simulation = true;
    settings.device = "contained-hf-native-gui";
    settings.calendar = hf_gui_calendar;
    settings.transfer.key = datapump::Crypto::amateur();
    settings.transfer.fec = datapump::FecMode::off;
    settings.transfer.content_limit = 16;
    settings.transfer.modem.sample_rate = options.helper ? 6000 : 1920;
    settings.transfer.modem.carrier_hz = options.helper ? 1725 : 500;
    settings.transfer.modem.bandwidth_hz = hf_gui_inner_rate_hz;
    settings.transfer.modem.oscillator_search = datapump::modem::OscillatorSearchConfig{
        .lf = {options.audio_ppm, options.audio_phase_degrees_sqrt_s},
        .rf = {options.rf_ppm, options.rf_phase_degrees_sqrt_s},
        .rf_shift_hz = hf_gui_rf_shift_hz,
        .margin = options.margin,
        .reference = datapump::modem::OscillatorReference::independent_audio,
        .sideband = datapump::modem::OscillatorSideband::upper};
    if (options.arrival == "tight") {
        settings.transfer.clock_sync = datapump::clock_sync::Policy{.001, .001, 0};
        settings.transfer.audio_timing_error_seconds = 0;
    } else {
        settings.transfer.clock_sync.reset();
        settings.transfer.audio_timing_error_seconds = datapump::clock_sync::default_audio_error_seconds;
    }
    settings.added_symbols = hf_gui_added_symbols;
    settings.spread_preset = options.spread;
    settings.selective_perturbation = options.selective;
    settings.target_cn0_db_hz = options.target_cn0_db_hz;
    // This exact link budget permits the entire supported actual-C/N0 range
    // while retaining a 1 W DATA reference. It is a normalized experiment,
    // not a measured site or a claim of absolute hardware power sensitivity.
    settings.tx_dbm = 30;
    settings.noise_dbm_hz = -170;
    settings.path_loss_db = 200 - options.actual_cn0_db_hz;
    settings.helper.power_w = options.helper ? hf_gui_helper_additional_power_w : 0;
    settings.helper.callsign = "TEST";
    settings.helper.timecode = true;
    settings.receiver_memory_limit = options.memory_bytes;
    datapump::live::validate_settings(settings);
    auto prepared = [&] {
        try {
            return datapump::live::detail::prepare_profile(settings, hf_gui_calendar, stop,
                {.simulation_seed = options.seed, .fixture_active_seconds = options.fixture_bit_seconds});
        } catch (const datapump::Error& error) {
            if (stop.stop_requested()) throw;
            throw HfGuiProfileUnavailable(error.what());
        }
    }();
    if (options.clock_edge) {
        prepared.channel.effects.tx.audio_ppm = options.audio_ppm / 2;
        prepared.channel.effects.rx.audio_ppm = -options.audio_ppm / 2;
        prepared.channel.effects.tx.rf_ppm = options.rf_ppm / 2;
        prepared.channel.effects.rx.rf_ppm = -options.rf_ppm / 2;
    }
    prepared.channel.effects.noise_enabled = true;
    prepared.channel.noise_seed = options.noise_seed;
    return prepared;
}

} // namespace appraisal
