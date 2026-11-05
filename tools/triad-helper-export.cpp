// Isolated sampled fixture: production Helper transmitter + encrypted triad v3.
// No ordinary Robust receive path is instantiated here.
#include "datapump/helper_carrier.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/tuning.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr std::uint32_t default_sample_rate = 18000;
constexpr double data_start_seconds = 38;
constexpr double thermal_n0_dbm_hz = -164;
constexpr double spectral_skirt_allowance_hz = 200;
constexpr std::uint64_t maximum_export_bytes = 2ULL * 1024 * 1024 * 1024;
constexpr double tau = 2 * std::numbers::pi;

struct Options {
    std::string scenario = "hf", control = "selected", shaping = "auto";
    std::string power_reference = "whole-active";
    std::optional<std::string> base_oscillator, shift_oscillator;
    std::optional<std::string> spread_preset, selective_perturbation;
    std::string timing_policy = "case-default";
    std::optional<double> rf_hz, spread_margin, selective_modulation_rate_hz;
    std::optional<double> data_power_w, helper_power_w;
    std::filesystem::path output;
    std::uint64_t seed = 1, key_seed = 72001, epoch = 1793850000;
    std::optional<std::uint64_t> burst_wait_tiles;
    unsigned bit = 0;
    std::uint32_t sample_rate = default_sample_rate;
    double seconds = 16, data_if_hz = 1725;
    std::optional<double> inner_rate_hz, shaping_tail_fraction;
    std::optional<unsigned> added_symbols;
    std::optional<double> ramp_seconds;
    double receiver_tail_seconds = 0;
    std::optional<double> cn0, arrival;
    std::optional<bool> fhss;
    bool plan_only = false, power_plan_only = false;
    bool data = true, helper = true, templates = false, explicit_data_if = false;
};

double real(std::string_view text, const char* name) {
    std::size_t used = 0;
    const auto value = std::stod(std::string(text), &used);
    if (used != text.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string("invalid ") + name);
    return value;
}

std::uint64_t integer(std::string_view text, const char* name) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument(std::string("invalid ") + name);
    std::size_t used = 0;
    const auto value = std::stoull(std::string(text), &used);
    if (used != text.size()) throw std::invalid_argument(std::string("invalid ") + name);
    return value;
}

bool enabled(std::string_view text) {
    if (text == "on") return true;
    if (text == "off") return false;
    throw std::invalid_argument("component selection must be on or off");
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next = [&]() -> std::string_view {
            if (++i == argc) throw std::invalid_argument("missing value for " + std::string(arg));
            return argv[i];
        };
        if (arg == "--help") {
            std::cout << "triad_helper_export --case hf|eme --output NEW_DIRECTORY\n"
                "  [--seed N] [--key-seed N] [--epoch UTC_SECONDS] [--bit 0|1]\n"
                "  [--seconds 16] [--rate HZ] [--added-symbols N] [--burst-wait-tiles N]\n"
                "  [--fhss on|off] [--shaping auto|legacy (default: auto)] [--shaping-tail-fraction F]\n"
                "  [--data-if HZ] [--sample-rate INTEGER_HZ] [--cn0 DB_HZ] [--plan-only]\n"
                "  [--power-reference whole-active|legacy-window] [--power-plan-only]\n"
                "  [--base-oscillator ID] [--shift-oscillator ID] [--ramp-seconds POSITIVE]\n"
                "  [--rf-hz HZ] [--spread-preset ID] [--margin FACTOR]\n"
                "  [--selective-perturbation off|slow-N|fast-N|each-N]\n"
                "  [--selective-modulation-rate-hz HZ] [--timing-policy case-default|bounded|broad]\n"
                "  [--data-power-w W] [--helper-power-w W]\n"
                "  [--receiver-tail-seconds 0..1]\n"
                "  [--control selected|flat] [--data on|off] [--helper on|off]\n"
                "  [--arrival-seconds T] [--templates]\n"
                "Default 18000 Hz complex samples; sample rate must be a multiple of 5.\n"
                "Default DATA IF 1725 Hz HF, 4000 Hz legacy EME.\n"
                "Physical C/N0 follows powers/loss/N0 unless\n"
                "overridden. Templates contain all 13 public epoch alternatives and both bits.\n";
            std::exit(0);
        } else if (arg == "--case") o.scenario = next();
        else if (arg == "--output") o.output = std::string(next());
        else if (arg == "--seed") o.seed = integer(next(), "seed");
        else if (arg == "--key-seed") o.key_seed = integer(next(), "key seed");
        else if (arg == "--epoch") o.epoch = integer(next(), "epoch");
        else if (arg == "--bit") {
            const auto bit = integer(next(), "source bit");
            if (bit > 1) throw std::invalid_argument("source bit must be 0 or 1");
            o.bit = static_cast<unsigned>(bit);
        } else if (arg == "--seconds") o.seconds = real(next(), "duration");
        else if (arg == "--rate") o.inner_rate_hz = real(next(), "inner rate");
        else if (arg == "--sample-rate") {
            const auto rate = integer(next(), "sample rate");
            if (rate < 6000 || rate > std::numeric_limits<std::uint32_t>::max() || rate % 5)
                throw std::invalid_argument("sample rate must be a multiple of 5 in [6000,4294967295] Hz");
            o.sample_rate = static_cast<std::uint32_t>(rate);
        }
        else if (arg == "--added-symbols") {
            const auto added = integer(next(), "Added Symbols");
            if (added > std::numeric_limits<unsigned>::max())
                throw std::invalid_argument("Added Symbols outside unsigned range");
            o.added_symbols = static_cast<unsigned>(added);
        } else if (arg == "--burst-wait-tiles") {
            const auto waits = integer(next(), "burst wait tiles");
            if (waits > std::numeric_limits<unsigned>::max())
                throw std::invalid_argument("burst wait tiles outside unsigned range");
            o.burst_wait_tiles = waits;
        } else if (arg == "--fhss") o.fhss = enabled(next());
        else if (arg == "--shaping") o.shaping = next();
        else if (arg == "--shaping-tail-fraction")
            o.shaping_tail_fraction = real(next(), "spectral tail fraction");
        else if (arg == "--data-if") { o.data_if_hz = real(next(), "DATA IF"); o.explicit_data_if = true; }
        else if (arg == "--base-oscillator") o.base_oscillator = std::string(next());
        else if (arg == "--shift-oscillator") o.shift_oscillator = std::string(next());
        else if (arg == "--rf-hz") o.rf_hz = real(next(), "RF shift");
        else if (arg == "--spread-preset") o.spread_preset = std::string(next());
        else if (arg == "--margin") o.spread_margin = real(next(), "spread/search margin");
        else if (arg == "--selective-perturbation") o.selective_perturbation = std::string(next());
        else if (arg == "--selective-modulation-rate-hz")
            o.selective_modulation_rate_hz = real(next(), "selective modulation rate");
        else if (arg == "--timing-policy") o.timing_policy = next();
        else if (arg == "--data-power-w") o.data_power_w = real(next(), "DATA power");
        else if (arg == "--helper-power-w") o.helper_power_w = real(next(), "Helper power");
        else if (arg == "--ramp-seconds") o.ramp_seconds = real(next(), "ramp duration");
        else if (arg == "--receiver-tail-seconds") o.receiver_tail_seconds = real(next(), "receiver tail duration");
        else if (arg == "--cn0") o.cn0 = real(next(), "C/N0");
        else if (arg == "--arrival-seconds") o.arrival = real(next(), "arrival");
        else if (arg == "--control") o.control = next();
        else if (arg == "--data") o.data = enabled(next());
        else if (arg == "--helper") o.helper = enabled(next());
        else if (arg == "--templates") o.templates = true;
        else if (arg == "--plan-only") o.plan_only = true;
        else if (arg == "--power-plan-only") o.power_plan_only = true;
        else if (arg == "--power-reference") o.power_reference = next();
        else throw std::invalid_argument("unknown option: " + std::string(arg));
    }
    if (o.scenario != "hf" && o.scenario != "eme") throw std::invalid_argument("case must be hf or eme");
    if (!o.explicit_data_if && o.scenario == "eme") o.data_if_hz = 4000;
    if (o.ramp_seconds && *o.ramp_seconds <= 0)
        throw std::invalid_argument("ramp duration must be positive");
    if (o.shaping != "legacy" && o.shaping != "auto")
        throw std::invalid_argument("shaping must be legacy or auto");
    if (o.power_reference != "whole-active" && o.power_reference != "legacy-window")
        throw std::invalid_argument("power reference must be whole-active or legacy-window");
    if (o.plan_only && o.power_plan_only)
        throw std::invalid_argument("--plan-only and --power-plan-only are mutually exclusive");
    const bool fhss_enabled = o.fhss.value_or(o.scenario == "eme");
    if (o.ramp_seconds && !fhss_enabled)
        throw std::invalid_argument("--ramp-seconds requires FHSS on");
    if (o.receiver_tail_seconds < 0 || o.receiver_tail_seconds > 1)
        throw std::invalid_argument("receiver tail duration must be in [0,1] seconds");
    if (o.inner_rate_hz && (*o.inner_rate_hz <= 0 || *o.inner_rate_hz > 30e6))
        throw std::invalid_argument("rate must be in (0,30000000] Hz");
    if (o.added_symbols && (*o.added_symbols < 1 || *o.added_symbols > 1000))
        throw std::invalid_argument("Added Symbols must be in [1,1000]");
    if (o.burst_wait_tiles && *o.burst_wait_tiles > 1000)
        throw std::invalid_argument("burst wait tiles must be in [0,1000]");
    if (o.shaping_tail_fraction && (*o.shaping_tail_fraction <= 0 || *o.shaping_tail_fraction > .005))
        throw std::invalid_argument("spectral tail fraction must be in (0,0.005]");
    if (o.control != "selected" && o.control != "flat") throw std::invalid_argument("control must be selected or flat");
    if (o.rf_hz && *o.rf_hz < 0) throw std::invalid_argument("RF shift must be nonnegative");
    if (o.spread_margin && (*o.spread_margin <= 0 || *o.spread_margin > 1000))
        throw std::invalid_argument("spread/search margin must be in (0,1000]");
    if (o.selective_modulation_rate_hz && *o.selective_modulation_rate_hz < 0)
        throw std::invalid_argument("selective modulation rate must be nonnegative");
    for (const auto power : {o.data_power_w, o.helper_power_w})
        if (power && *power <= 0) throw std::invalid_argument("DATA and Helper powers must be positive");
    if (o.timing_policy != "case-default" && o.timing_policy != "bounded" && o.timing_policy != "broad")
        throw std::invalid_argument("timing policy must be case-default, bounded or broad");
    if (o.output.empty() && !o.plan_only && !o.power_plan_only)
        throw std::invalid_argument("--output is required unless a plan-only mode is selected");
    if (o.seconds <= 0 || o.seconds > 3600) throw std::invalid_argument("seconds must be in (0,3600]");
    if (o.data_if_hz < 1725 || o.data_if_hz >= o.sample_rate / 2.)
        throw std::invalid_argument("DATA IF must be at least 1725 Hz and below Nyquist");
    if (o.epoch < 6 || o.epoch > 4294967289ULL)
        throw std::invalid_argument("epoch must support all thirteen alternatives and Helper metadata");
    if (o.cn0 && (*o.cn0 < -100 || *o.cn0 > 200))
        throw std::invalid_argument("C/N0 must be in [-100,200] dB-Hz");
    return o;
}

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Crypto fixture_key(std::uint64_t seed) {
    // Same deterministic fixture-key construction as triad_long; the root is
    // independent of every physical random stream and never changes by trial.
    std::seed_seq seq{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32), 0U, 0U};
    std::mt19937 engine(seq);
    std::array<std::uint8_t, 32> material{};
    for (auto& b : material) b = static_cast<std::uint8_t>(engine());
    return Crypto(material);
}

struct Case {
    double data_w, helper_w, loss_db, rf_hz, planning_target_db;
    double margin, arrival_low, arrival_high, capture_lead, capture_trail;
    tuning::OscillatorPreset oscillator;
    tuning::SpreadPreset spread;
};

Case case_for(const Options& o) {
    auto result = o.scenario == "hf" ?
        Case{4, 40, 180, 30e6, 8, 3, -7.035, 7.035, 8, 8,
            tuning::parse_oscillator_preset("gpsdo-xo"), tuning::parse_spread_preset("hf-worst")} :
        Case{40, 15000, 200, 5.8e9, 1.5, 1, -.055, .125, .2, .3,
            tuning::parse_oscillator_preset("gpsdo-ocxo"), tuning::parse_spread_preset("eme-focused")};
    if (o.rf_hz) result.rf_hz = *o.rf_hz;
    if (o.data_power_w) result.data_w = *o.data_power_w;
    if (o.helper_power_w) result.helper_w = *o.helper_power_w;
    if (!std::isfinite(result.data_w + result.helper_w))
        throw std::invalid_argument("combined requested power exceeds numeric range");
    if (o.spread_margin) result.margin = *o.spread_margin;
    if (o.spread_preset) result.spread = tuning::parse_spread_preset(*o.spread_preset);
    if (o.timing_policy == "bounded") {
        // Dated-capture coordinate: TX causal lag midpoint +35 ms;
        // two 10-ms station bounds, total 1-ms region, and both 35-ms
        // audio radii. This constrains arrival, never the epoch bank.
        constexpr double center = .035, radius = .020 + .001 / 2 + .070;
        result.arrival_low = center - radius; result.arrival_high = center + radius;
        result.capture_lead = .2; result.capture_trail = .3;
    } else if (o.timing_policy == "broad") {
        result.arrival_low = -7.035; result.arrival_high = 7.035;
        result.capture_lead = result.capture_trail = 8;
    }
    return result;
}

Format format_for(const Options& o) {
    Format f;
    const bool eme = o.scenario == "eme";
    f.inner_rate_hz = o.inner_rate_hz.value_or(eme ? 360 : 36);
    f.added_symbols = o.added_symbols.value_or(eme ? 1 : 10);
    f.added_interleave = true;
    f.sample_rate = o.sample_rate;
    f.bit_seconds = o.seconds;
    f.fhss = o.fhss.value_or(eme);
    f.fhss_center_aligned = f.fhss;
    f.quiet_seconds = f.fhss ? .055 : 0;
    f.ramp_seconds = o.ramp_seconds.value_or(eme ? 2. / 3600 : .02);
    f.margin_seconds = .005;
    f.burst_wait_tiles = static_cast<unsigned>(o.burst_wait_tiles.value_or(0));
    f.encrypt = true;
    f.epoch = o.epoch;
    return f;
}

void json_string(std::ostream&, std::string_view);
const char* boolean(bool);

triad::v3::ShapingPlan shaped_format(const Options& o, const Format& input) {
    if (o.shaping == "auto") {
        triad::v3::ShapingOptions shaping;
        if (o.shaping_tail_fraction) shaping.tail_energy_fraction = *o.shaping_tail_fraction;
        return triad::v3::plan_shaping(input, shaping);
    }
    triad::v3::ShapingPlan result;
    result.format = input;
    result.geometry = triad::v3::geometry(input);
    result.spectrum.method = "legacy-conservative-ramp-band";
    result.spectrum.placement_bandwidth_hz = result.geometry.occupied_bandwidth_hz + 2 / input.ramp_seconds;
    result.spectrum.measured_bandwidth_hz = result.spectrum.placement_bandwidth_hz;
    result.spectrum.requested_tail_fraction = 0;
    result.spectrum.measured_tail_fraction = 0;
    result.spectrum.converged = false;
    result.spectrum.limitation = "No shaped-spectrum certificate requested; legacy 2/ramp planning allowance retained.";
    return result;
}

double data_if_for(const Options& o) {
    if (o.explicit_data_if) return o.data_if_hz;
    return (o.scenario == "eme" && o.shaping == "legacy") ? 4000 : 1725;
}

helper::Options helper_options_for(const Case& physical) {
    helper::Options result;
    result.power_w = physical.data_w + physical.helper_w;
    result.data_power_w = physical.data_w;
    result.timecode = true;
    result.callsign = "TRIAD";
    return result;
}

struct ResolvedPhysics {
    tuning::OscillatorPreset base, shift;
    bool shift_active = false, shared_shift_clock = false, helper_power_limited = false;
    std::string shift_id, requested_modulation_id;
    double rf_pair_ppm = 0, phase_diffusion = 0;
    double selected_delay_ms = 0, selected_doppler_hz = 0;
    double delay_ms = 0, doppler_hz = 0, slow_depth = 0, fast_depth = 0, modulation_rate_hz = 0;
    double physical_cn0 = 0, data_cn0 = 0, effective_helper_w = 0;
};

ResolvedPhysics resolve_physics(const Options& o, const Case& physical) {
    ResolvedPhysics r;
    r.base = tuning::parse_oscillator_preset(o.base_oscillator.value_or(std::string(physical.oscillator.id)));
    if (!r.base.baseband_supported)
        throw std::invalid_argument("base oscillator must support the independent audio clock");
    const auto shift_id = o.shift_oscillator.value_or(std::string(r.base.id));
    const bool requested_shared = shift_id == "baseband-clock";
    r.shift = requested_shared ? r.base : tuning::parse_oscillator_preset(shift_id);
    r.shift_id = requested_shared ? "baseband-clock" : std::string(r.shift.id);
    r.shift_active = physical.rf_hz > 0;
    r.shared_shift_clock = requested_shared && r.shift_active;
    r.rf_pair_ppm = r.shift_active ? r.shift.clock_error_ppm : 0;
    const auto base_phase = r.base.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
    const auto shift_phase = r.shift.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
    r.phase_diffusion = r.shift_active && !r.shared_shift_clock ? std::hypot(base_phase, shift_phase) : base_phase;
    r.selected_delay_ms = physical.spread.delay_ms * physical.margin;
    r.selected_doppler_hz = physical.spread.doppler_hz * physical.margin;
    r.requested_modulation_id = o.selective_perturbation.value_or(
        physical.spread.default_perturbation ? "each-65" : "off");
    double slow = 0, fast = 0;
    bool known = r.requested_modulation_id == "off";
    for (const auto* kind : {"slow", "fast", "each"}) for (const auto depth : {1, 5, 10, 25, 65})
        if (r.requested_modulation_id == std::string(kind) + "-" + std::to_string(depth)) {
            slow = std::string_view(kind) != "fast" ? depth / 100. : 0;
            fast = std::string_view(kind) != "slow" ? depth / 100. : 0;
            known = true;
        }
    if (!known) throw std::invalid_argument("selective perturbation must be off or slow/fast/each-N (1,5,10,25,65)");
    if (o.control == "selected") {
        r.delay_ms = r.selected_delay_ms; r.doppler_hz = r.selected_doppler_hz;
        r.slow_depth = slow; r.fast_depth = fast;
        r.modulation_rate_hz = o.selective_modulation_rate_hz.value_or(r.doppler_hz);
    }
    r.physical_cn0 = 10 * std::log10(physical.data_w * 1000) - physical.loss_db - thermal_n0_dbm_hz;
    r.data_cn0 = o.cn0.value_or(r.physical_cn0);
    r.effective_helper_w = helper::effective_helper_power(helper_options_for(physical));
    r.helper_power_limited = physical.helper_w > physical.data_w * 1e9;
    if (!std::isfinite(r.physical_cn0) || !std::isfinite(r.effective_helper_w) || r.effective_helper_w <= 0)
        throw std::invalid_argument("requested powers exceed the representable production Helper power range");
    return r;
}

void physics_json(std::ostream& out, const Options& o, const Case& physical,
                  const ResolvedPhysics& r, double data_if_hz) {
    const auto denominator = 1 - r.base.clock_error_ppm * .5e-6;
    out << "{\"rf_hz\":" << physical.rf_hz << ",\"data_if_hz\":" << data_if_hz
        << ",\"base_oscillator\":"; json_string(out, r.base.id);
    out << ",\"shift_oscillator\":"; json_string(out, r.shift_id);
    out << ",\"shift_active\":" << boolean(r.shift_active)
        << ",\"shared_shift_clock\":" << boolean(r.shared_shift_clock)
        << ",\"base_relative_pair_ppm_bound\":" << r.base.clock_error_ppm
        << ",\"rf_relative_pair_ppm_bound\":" << r.rf_pair_ppm
        << ",\"sample_clock_pair_ppm\":" << r.base.clock_error_ppm / denominator
        << ",\"deterministic_cfo_bound_hz\":" <<
            (physical.rf_hz * r.rf_pair_ppm + data_if_hz * r.base.clock_error_ppm) * 1e-6 / denominator
        << ",\"relative_phase_diffusion_rad_sqrt_s\":" << r.phase_diffusion
        << ",\"spread_preset\":"; json_string(out, physical.spread.id);
    out << ",\"spread_margin\":" << physical.margin << ",\"spread_delay_ms\":" << r.delay_ms
        << ",\"spread_doppler_hz\":" << r.doppler_hz
        << ",\"requested_selective_perturbation\":"; json_string(out, r.requested_modulation_id);
    out << ",\"selective_slow_depth\":" << r.slow_depth << ",\"selective_fast_depth\":" << r.fast_depth
        << ",\"selective_modulation_rate_hz\":" << r.modulation_rate_hz
        << ",\"selective_enabled\":" << boolean(r.modulation_rate_hz > 0 && (r.slow_depth > 0 || r.fast_depth > 0))
        << ",\"timing_policy\":"; json_string(out, o.timing_policy);
    out << ",\"arrival_prior_seconds\":[" << physical.arrival_low << ',' << physical.arrival_high << ']'
        << ",\"capture_lead_seconds\":" << physical.capture_lead << ",\"capture_trail_seconds\":" << physical.capture_trail
        << ",\"message_epoch_count\":13,\"data_w\":" << physical.data_w
        << ",\"requested_helper_w\":" << physical.helper_w << ",\"effective_helper_w\":" << r.effective_helper_w
        << ",\"helper_ratio_limit_db\":" << helper_options_for(physical).ratio_limit_db
        << ",\"helper_power_limited\":" << boolean(r.helper_power_limited)
        << ",\"physical_data_cn0_db_hz\":" << r.physical_cn0 << ",\"data_cn0_db_hz\":" << r.data_cn0
        << ",\"effective_helper_cn0_db_hz\":" << r.data_cn0 + 10 * std::log10(r.effective_helper_w / physical.data_w)
        << ",\"cn0_scope\":\"nominal active DATA reference and effective production Helper/DATA power ratio; actual finite-calendar component energies are reported separately\"}";
}

void print_plan_json(const Options& o, double data_if_hz,
                     const triad::v3::ShapingPlan& selected,
                     const helper::Options& helper_options, const Case& physical,
                     const ResolvedPhysics& physics) {
    const auto& f = selected.format;
    const auto& g = selected.geometry;
    const auto sample_rate = f.sample_rate;
    const auto nominal = g.occupied_bandwidth_hz;
    const auto gate = std::max(0., selected.spectrum.placement_bandwidth_hz - nominal);
    const auto minimum_data_if = std::max(1725., helper::auxiliary_frequency_hz + 450 +
                                                     selected.spectrum.placement_bandwidth_hz / 2);
    double helper_high = 0;
    std::string helper_error;
    try {
        const helper::PlacementBand placement{data_if_hz, nominal, gate, f.fhss};
        helper_high = helper::plan(helper_options, placement, sample_rate).frequency_hz;
    } catch (const std::exception& error) {
        helper_error = error.what();
    }
    std::cout << std::setprecision(17)
        << "{\"schema\":\"triad-helper-shaping-plan-v1\",\"geometry_feasible\":true"
        << ",\"scenario\":"; json_string(std::cout, o.scenario);
    std::cout << ",\"shaping\":"; json_string(std::cout, o.shaping);
    std::cout << ",\"format\":{\"inner_rate_hz\":" << f.inner_rate_hz
        << ",\"added_symbols\":" << f.added_symbols << ",\"fine_chip_rate_hz\":" << g.fine_chip_rate_hz
        << ",\"occupied_bandwidth_hz\":" << nominal << ",\"fhss\":" << boolean(f.fhss)
        << ",\"center_aligned\":" << boolean(f.fhss_center_aligned)
        << ",\"quiet_seconds\":" << f.quiet_seconds << ",\"ramp_seconds\":" << f.ramp_seconds
        << ",\"margin_seconds\":" << f.margin_seconds << ",\"pulse_shape\":";
    json_string(std::cout, triad::v3::pulse_shape_name(f.pulse_shape));
    std::cout << ",\"burst_wait_tiles\":" << f.burst_wait_tiles << ",\"tile_seconds\":" << g.tile_seconds
        << ",\"tile_count\":" << g.tile_count << ",\"elapsed_bit_seconds\":" << g.bit_seconds
        << ",\"fine_coefficients\":" << g.fine_coefficients << ",\"inner_coefficients\":" << g.inner_coefficients
        << ",\"payload_fine_cells_per_tile\":" << g.payload_fine_cells_per_tile << '}'
        << ",\"spectral_certificate\":{\"placement_bandwidth_hz\":" << selected.spectrum.placement_bandwidth_hz
        << ",\"measured_bandwidth_hz\":" << selected.spectrum.measured_bandwidth_hz
        << ",\"measured_tail_fraction\":" << selected.spectrum.measured_tail_fraction
        << ",\"requested_tail_fraction\":" << selected.spectrum.requested_tail_fraction
        << ",\"coarse_width_hz\":" << selected.spectrum.coarse_width_hz
        << ",\"refined_width_hz\":" << selected.spectrum.refined_width_hz
        << ",\"phase_refinement_width_hz\":" << selected.spectrum.phase_refinement_width_hz
        << ",\"phase_refinement_error_hz\":" << selected.spectrum.phase_refinement_error_hz
        << ",\"sampling_refinement_error_hz\":" << selected.spectrum.sampling_refinement_error_hz
        << ",\"frequency_bin_hz\":" << selected.spectrum.frequency_bin_hz
        << ",\"far_outside_energy_fraction\":" << selected.spectrum.far_outside_energy_fraction
        << ",\"normalized_pulse_energy\":" << selected.spectrum.normalized_pulse_energy
        << ",\"phase_samples\":" << selected.spectrum.phase_samples << ",\"fft_size\":" << selected.spectrum.fft_size
        << ",\"kernel_ffts\":" << selected.spectrum.kernel_ffts
        << ",\"total_kernel_ffts\":" << selected.spectrum.total_kernel_ffts
        << ",\"phase_refinement_attempts\":" << selected.spectrum.phase_refinement_attempts
        << ",\"converged\":" << boolean(selected.spectrum.converged) << ",\"method\":";
    json_string(std::cout, selected.spectrum.method);
    std::cout << ",\"limitation\":"; json_string(std::cout, selected.spectrum.limitation);
    std::cout << "},\"helper_placement\":{\"requested_data_if_hz\":" << data_if_hz
        << ",\"minimum_data_if_hz_from_fixed_low_tones\":" << minimum_data_if
        << ",\"effective_gate_bandwidth_hz\":" << gate << ",\"planned_high_hz\":" << helper_high
        << ",\"feasible_at_requested_if\":" << boolean(helper_error.empty())
        << ",\"unavailable_reason\":";
    if (helper_error.empty()) std::cout << "null";
    else json_string(std::cout, helper_error);
    std::cout << "},\"physics\":";
    physics_json(std::cout, o, physical, physics, data_if_hz);
    std::cout << ",\"export_constraints\":{\"sample_rate_hz\":" << sample_rate
        << ",\"maximum_export_bytes\":" << maximum_export_bytes << ",\"fixed_helper_lead_seconds\":38"
        << ",\"pulse_halo_seconds\":" << 8 / g.fine_chip_rate_hz
        << ",\"pulse_halo_fits_lead\":" << boolean(g.calendar_origin_sample <= data_start_seconds * sample_rate)
        << ",\"message_epoch_count\":13}"
        << ",\"planner\":{\"fallback\":" << boolean(selected.fallback)
        << ",\"reason\":";
    json_string(std::cout, selected.reason);
    std::cout << ",\"candidate_count\":" << selected.candidate_count
        << ",\"total_kernel_ffts\":" << selected.total_kernel_ffts
        << "},\"limitations\":[\"This is a key-independent expected-energy shaping certificate, not a worst-case encrypted pattern or sampled channel bound.\","
        << "\"The selected sampler and explicit export budget reject unsupported geometries; planner-only results do not imply receiver availability.\","
        << "\"Source capture still retains the conservative 2/ramp envelope bound and complete physical channel tails.\"]}\n";
}

void print_infeasible_plan(const Options& o, std::string_view stage, std::string_view reason,
                           const Case& physical, const ResolvedPhysics& physics, double data_if_hz) {
    std::cout << "{\"schema\":\"triad-helper-shaping-plan-v1\",\"geometry_feasible\":false,\"stage\":";
    json_string(std::cout, stage);
    std::cout << ",\"reason\":";
    json_string(std::cout, reason);
    std::cout << ",\"scenario\":";
    json_string(std::cout, o.scenario);
    std::cout << ",\"shaping\":";
    json_string(std::cout, o.shaping);
    std::cout << ",\"physics\":";
    physics_json(std::cout, o, physical, physics, data_if_hz);
    std::cout << "}\n";
}

Complex rotation(double frequency, long double seconds) {
    return std::polar(1., tau * static_cast<double>(std::remainder(frequency * seconds, 1.L)));
}

std::ofstream output_file(const std::filesystem::path& path, bool binary = false) {
    std::ofstream out(path, std::ios::out | (binary ? std::ios::binary : std::ios::openmode{}));
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(17);
    return out;
}

void write_complex(std::ostream& out, std::span<const Complex> samples) {
    // Explicit interleaved little-endian binary64, independent of the C++
    // std::complex object representation. Bound memory by one output chunk.
    std::vector<double> raw(samples.size() * 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        raw[2 * i] = samples[i].real(); raw[2 * i + 1] = samples[i].imag();
    }
    out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(double)));
}

void json_string(std::ostream& out, std::string_view text) {
    out << '"';
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                            << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
        else out << c;
    }
    out << '"';
}

const char* boolean(bool value) { return value ? "true" : "false"; }

struct DataPowerReference {
    double active_seconds = 0, mean_norm = 0, refinement_relative_difference = 0;
    unsigned nodes = 0;
    const char* method = "stratified-fractional-v1";
    bool sampled_complete = false;
};

DataPowerReference whole_active_power(PatternSource& source, const Format& f, const Geometry& g) {
    const auto sample_rate = f.sample_rate;
    // Integrate the complete bit-0 source in active-time coordinates. Quiet
    // FHSS intervals and burst waits cost elapsed time, never extra transmit
    // power; nonzero ramps remain in the energy and the active denominator.
    // This is source calibration only: no propagation/outage/noise is observed.
    const long double active_dwell = f.dwell_seconds - 2 * f.quiet_seconds;
    const long double active_tile = g.tile_seconds * (f.fhss ? active_dwell / f.dwell_seconds : 1.L);
    const long double active_seconds = active_tile * g.tile_count;
    if (!(active_seconds > 0) || !std::isfinite(active_seconds))
        throw std::runtime_error("invalid whole-active DATA reference extent");
    const auto estimate = [&](unsigned nodes) {
        long double sum = 0;
        for (unsigned i = 0; i < nodes; ++i) {
            // Deterministic stratification avoids a periodic sampling lattice
            // accidentally aliasing the three-carrier beat frequencies.
            const auto jitter = mix64(f.epoch ^ (static_cast<std::uint64_t>(i) + 0x504f574552ULL));
            const long double fraction = static_cast<long double>(jitter >> 11) / 9007199254740992.L;
            auto coordinate = active_seconds * (i + fraction) / nodes;
            const auto tile = std::min<std::uint64_t>(g.tile_count - 1,
                static_cast<std::uint64_t>(std::floor(coordinate / active_tile)));
            coordinate -= tile * active_tile;
            long double within_tile = coordinate;
            if (f.fhss) {
                const auto dwell = std::floor(coordinate / active_dwell);
                within_tile = dwell * f.dwell_seconds + f.quiet_seconds + coordinate - dwell * active_dwell;
            }
            const long double seconds = tile * (static_cast<long double>(f.burst_wait_tiles) + 1) *
                                        g.tile_seconds + within_tile;
            sum += std::norm(source.sample_at(g.calendar_origin_sample + seconds * sample_rate));
        }
        return static_cast<double>(sum / nodes);
    };
    DataPowerReference result;
    result.active_seconds = static_cast<double>(active_seconds);
    const auto sampled_reference = [&](double fractional_estimate) {
        // A rich pattern can appear converged on two undersampled fractional
        // grids. Calibrate such cases on EVERY emitted integer source sample,
        // using bounded scalar state and the actual selected sample clock.
        // This is exact for the sampled transmit-power definition; it is not
        // a continuous-time quadrature or propagation normalization.
        constexpr std::uint64_t maximum_power_samples = 4ULL * 1024 * 1024;
        if (g.source_samples > maximum_power_samples)
            throw std::runtime_error("whole-active DATA power requires complete sampled calibration beyond the explicit 4194304-source-sample work budget");
        long double energy = 0;
        for (std::uint64_t row = 0; row < g.source_samples; ++row)
            energy += std::norm(source.sample_at(static_cast<long double>(row)));
        auto exact = result;
        exact.mean_norm = static_cast<double>(energy / (active_seconds * sample_rate));
        if (!(exact.mean_norm > 0) || !std::isfinite(exact.mean_norm))
            throw std::runtime_error("empty complete sampled DATA power reference");
        exact.nodes = static_cast<unsigned>(g.source_samples);
        exact.method = "complete-integer-source-v1";
        exact.sampled_complete = true;
        exact.refinement_relative_difference = fractional_estimate > 0 ?
            std::abs(exact.mean_norm - fractional_estimate) / exact.mean_norm : 0.;
        return exact;
    };
    // Sixteen fractional nodes per fine period is an engineering guard against
    // accepting accidental coarse-grid agreement, not an integration theorem.
    // The complete sampled calculation supplies the power definition when the
    // first refinement would not meet it. Established smaller fixtures retain
    // their historical gain and bytes.
    if (active_seconds * g.fine_chip_rate_hz > helper::power_reference_nodes / 8.L)
        return sampled_reference(0.);
    auto previous = estimate(helper::power_reference_nodes);
    for (unsigned nodes = 2 * helper::power_reference_nodes; nodes <= 8 * helper::power_reference_nodes; nodes *= 2) {
        const auto current = estimate(nodes);
        if (!(current > 0) || !std::isfinite(current))
            throw std::runtime_error("empty whole-active DATA power reference");
        result.mean_norm = current;
        result.nodes = nodes;
        result.refinement_relative_difference = std::abs(current - previous) / current;
        if (result.refinement_relative_difference <= .001) return result;
        previous = current;
    }
    return sampled_reference(previous);
}

void data_power_json(std::ostream& out, const Options& o, const DataPowerReference& power,
                     double calibration_gain, double data_gain) {
    out << "\"data_power_reference_policy\":"; json_string(out, o.power_reference);
    out << ",\"data_power_reference_version\":2,\"data_reference_bit\":0"
        << ",\"data_reference_integration_method\":"; json_string(out, power.method);
    out << ",\"data_reference_complete_sampled_source\":" << boolean(power.sampled_complete)
        << ",\"data_reference_active_seconds\":" << power.active_seconds
        << ",\"data_reference_mean_norm\":" << power.mean_norm
        << ",\"data_reference_nodes\":" << power.nodes
        << ",\"data_reference_refinement_relative_difference\":" << power.refinement_relative_difference
        << ",\"data_reference_refinement_tolerance\":0.001"
        << ",\"post_plan_data_calibration_gain\":" << calibration_gain
        << ",\"template_data_gain\":" << data_gain
        << ",\"data_reference_scope\":";
    json_string(out, o.power_reference == "whole-active" ?
        "fixed public bit0 before channel; full active tiles including nonzero ramps; excluding FHSS quiet and burst waits; all bits, epochs and controls retain this gain; refinement difference is not a proven integration bound" :
        "historical39-41s active preview only, potentially inside low-rate birth ramp; active_seconds reports full-source denominator but is not the calibration extent; all bits, epochs and controls retain this gain");
}

void endpoint_json(std::ostream& out, const triad::Oscillator& endpoint) {
    out << "{\"audio_ppm\":" << endpoint.audio_ppm << ",\"rf_ppm\":" << endpoint.rf_ppm
        << ",\"phase_noise_rad_per_sqrt_second\":" << endpoint.phase_noise_rad_per_sqrt_second
        << ",\"rf_drift_ppm_per_second\":" << endpoint.rf_drift_ppm_per_second << '}';
}

void selective_json(std::ostream& out, const std::optional<SelectivePerturbationReport>& selected) {
    if (!selected) { out << "null"; return; }
    const auto& r = *selected;
    out << "{\"model\":\"dispersive-dft-v1\",\"sample_rate_hz\":" << r.sample_rate_hz
        << ",\"correlation_spacing_hz\":" << r.correlation_spacing_hz << ",\"center_hz\":" << r.center_hz
        << ",\"response_period_hz\":" << r.response_period_hz << ",\"memory_samples\":" << r.memory_samples
        << ",\"decay_tail_samples\":" << r.decay_tail_samples << ",\"seed\":" << r.seed
        << ",\"processed_rows\":" << r.processed_rows
        << ",\"pre_selective_energy_samples\":" << r.pre_selective_energy_samples
        << ",\"post_selective_energy_samples\":" << r.post_selective_energy_samples
        << ",\"component_depths\":[" << r.component_depths[0] << ',' << r.component_depths[1]
        << "],\"component_rates_hz\":[" << r.component_rates_hz[0] << ',' << r.component_rates_hz[1]
        << "],\"branch_delays_seconds\":[";
    for (unsigned i = 0; i < 3; ++i) { if (i) out << ','; out << r.branch_delays_seconds[i]; }
    out << "],\"initial_phases_radians\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i) out << ',';
        out << '[' << r.initial_phases_radians[i][0] << ',' << r.initial_phases_radians[i][1] << ']';
    }
    out << "]}";
}

int run(const Options& o) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    if constexpr (std::endian::native != std::endian::little)
        throw std::runtime_error("fixture export requires a little-endian host");
    const auto wall_start = std::chrono::steady_clock::now();
    const auto cpu_start = std::clock();
    const auto physical = case_for(o);
    const auto physics = resolve_physics(o, physical);
    const auto& base_oscillator = physics.base;
    const auto& shift_oscillator = physics.shift;
    if (o.arrival && (*o.arrival < physical.arrival_low || *o.arrival > physical.arrival_high))
        throw std::invalid_argument("diagnostic arrival override must stay inside the public arrival prior");
    const auto data_if_hz = data_if_for(o);
    auto initial_format = format_for(o);
    triad::v3::ShapingPlan selected_shaping;
    try {
        selected_shaping = shaped_format(o, initial_format);
    } catch (const std::exception& error) {
        if (!o.plan_only) throw;
        print_infeasible_plan(o, "bounded-layout-or-spectrum-search", error.what(), physical, physics, data_if_hz);
        return 0;
    }
    const auto f = selected_shaping.format;
    const auto g = selected_shaping.geometry;
    const auto sample_rate = f.sample_rate;
    // Exactly 400 ms for the accepted integer multiple-of-five sample clocks.
    const std::uint64_t dwell_samples = 2ULL * sample_rate / 5;
    // Keep the complete finite-envelope source bound until sampled validation
    // has justified reducing it. The certificate affects only Helper placement.
    const auto legacy_source_bandwidth = g.occupied_bandwidth_hz + 2 / f.ramp_seconds;
    if (o.plan_only) {
        print_plan_json(o, data_if_hz, selected_shaping, helper_options_for(physical), physical, physics);
        return 0;
    }
    const auto key = fixture_key(o.key_seed);
    PatternSource reference_source(f, key, 0);
    PatternSource data_source(f, key, static_cast<std::uint8_t>(o.bit));
    const auto lead_samples = static_cast<std::uint64_t>(std::llround(data_start_seconds * sample_rate));
    if (g.calendar_origin_sample > lead_samples)
        throw std::runtime_error("DATA pulse padding exceeds the fixed Helper lead");
    const auto data_offset = lead_samples - g.calendar_origin_sample;
    if (g.source_samples > UINT64_MAX - data_offset - dwell_samples)
        throw std::runtime_error("composite source extent overflow");
    const auto data_end = data_offset + g.source_samples;
    const auto composite_samples = ((data_end + dwell_samples - 1) / dwell_samples) * dwell_samples;
    const long double helper_end = static_cast<long double>(composite_samples) / sample_rate;

    auto helper_options = helper_options_for(physical);
    helper::PowerReference reference;
    reference.epoch = f.epoch;
    reference.begin_seconds = 39;
    reference.end_seconds = 41;
    reference.time_key = key;
    reference.helper_start_seconds = 0;
    reference.helper_end_seconds = helper_end;
    reference.active = [f](long double seconds) {
        if (!f.fhss) return true;
        auto within = std::fmod(seconds - data_start_seconds, static_cast<long double>(f.dwell_seconds));
        if (within < 0) within += f.dwell_seconds;
        return std::min(within, static_cast<long double>(f.dwell_seconds) - within) > f.quiet_seconds;
    };
    long double reference_norm = 0;
    unsigned reference_nodes = 0;
    for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
        const auto seconds = helper::power_reference_node(reference, i);
        if (!reference.active(seconds)) continue;
        const auto value = reference_source.sample_at(seconds * sample_rate - data_offset);
        reference_norm += std::norm(value);
        ++reference_nodes;
    }
    if (!reference_nodes || reference_norm <= 0) throw std::runtime_error("empty DATA power reference");
    const auto reference_mean_norm = static_cast<double>(reference_norm / reference_nodes);
    reference.data_mean_power = reference_mean_norm / 2;
    // The finite pulse/gate spectral certificate selects Helper placement.
    // Sample bounds below retain the full 2/ramp envelope and all tails.
    const auto gate_bandwidth = std::max(0., selected_shaping.spectrum.placement_bandwidth_hz -
                                             g.occupied_bandwidth_hz);
    const auto complete_data_bandwidth = legacy_source_bandwidth;
    const helper::PlacementBand placement{data_if_hz, g.occupied_bandwidth_hz, gate_bandwidth, f.fhss};
    helper::Plan plan;
    try { plan = helper::plan(helper_options, placement, sample_rate, 0, 1, &reference); }
    catch (const std::exception& error) {
        throw std::runtime_error("production Helper plan rejected DATA IF " + std::to_string(data_if_hz) +
                                 " Hz: " + error.what());
    }
    // Preserve the production Helper preview and composite gain exactly. The
    // former two-second DATA window can sit inside a long low-rate birth ramp,
    // so it is not a general-rate nominal-power reference. A separate DATA-only
    // correction enforces configured active transmit power without changing a
    // Helper sample, duty cycle, waveform, or propagation effect.
    const auto composite_gain = 1 / (plan.data_gain * std::sqrt(reference_mean_norm));
    auto data_reference = o.power_reference == "whole-active" ? whole_active_power(reference_source, f, g) :
        DataPowerReference{g.tile_count * g.tile_seconds * (f.fhss ? 1 - 2 * f.quiet_seconds / f.dwell_seconds : 1),
                           reference_mean_norm, 0, reference_nodes};
    if (o.power_reference == "legacy-window") data_reference.method = "legacy-window-v1";
    const auto data_calibration_gain = o.power_reference == "whole-active" ?
        std::sqrt(reference_mean_norm / data_reference.mean_norm) : 1.;
    const auto data_gain = plan.data_gain * composite_gain * data_calibration_gain;
    helper::Generator generator(helper_options, plan, f.epoch, true, key);
    long double measured_helper_norm = 0;
    for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
        const auto seconds = helper::power_reference_node(reference, i);
        if (reference.active(seconds))
            measured_helper_norm += std::norm(generator.mix_analytic({}, seconds, sample_rate, helper_end, 0) * composite_gain);
    }
    const auto reference_helper_ratio = static_cast<double>(measured_helper_norm / reference_nodes);

    if (o.power_plan_only) {
        std::cout << std::setprecision(17)
            << "{\"schema\":\"triad-helper-power-plan-v1\",\"sample_rate_hz\":" << sample_rate
            << ",\"key_seed\":" << o.key_seed << ",\"public_epoch_seconds\":" << f.epoch
            << ",\"format\":{\"inner_rate_hz\":" << f.inner_rate_hz << ",\"added_symbols\":" << f.added_symbols
            << ",\"bit_seconds\":" << g.bit_seconds << ",\"tile_seconds\":" << g.tile_seconds
            << ",\"tile_count\":" << g.tile_count << ",\"burst_wait_tiles\":" << f.burst_wait_tiles
            << ",\"fhss\":" << boolean(f.fhss) << ",\"dwell_seconds\":" << f.dwell_seconds
            << ",\"quiet_seconds\":" << f.quiet_seconds << ",\"ramp_seconds\":" << f.ramp_seconds
            << ",\"source_samples\":" << g.source_samples << "},\"power\":{";
        data_power_json(std::cout, o, data_reference, data_calibration_gain, data_gain);
        std::cout << ",\"reference_data_mean_norm\":" << reference_mean_norm
            << ",\"post_plan_composite_gain\":" << composite_gain
            << ",\"helper_preview_window_seconds\":[39,41],\"helper_gain_unchanged\":true"
            << ",\"reference_measured_helper_to_data_ratio\":" << reference_helper_ratio
            << "},\"wall_seconds\":"
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count() << "}\n";
        return 0;
    }

    const auto physical_cn0 = physics.physical_cn0;
    const auto cn0 = physics.data_cn0;
    StreamChannelConfig config;
    auto& effects = config.effects;
    effects.sample_rate = sample_rate;
    effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    effects.rf_hz = physical.rf_hz;
    effects.nominal_conversion_if_hz = data_if_hz;
    effects.capture_lead_seconds = physical.capture_lead;
    effects.capture_trail_seconds = physical.capture_trail;
    effects.fhss = false; // Public source envelope only: no physical RF retuning.
    effects.seed = mix64(o.seed ^ 0x4348414e4e454cULL);
    config.oscillator_seed = mix64(o.seed ^ 0x4f5343494c4cULL);
    config.noise_seed = mix64(o.seed ^ 0x4e4f495345ULL);
    config.frontend_seed = mix64(o.seed ^ 0x46524f4e54ULL);
    config.path_seed = mix64(o.seed ^ 0x50415448ULL);
    const auto static_seed = mix64(o.seed ^ 0x535441544943ULL);
    const auto arrival_seed = mix64(o.seed ^ 0x4152524956414cULL);
    std::mt19937_64 static_rng(static_seed), arrival_rng(arrival_seed);
    const auto base_half_ppm = base_oscillator.clock_error_ppm / 2;
    const auto shift_half_ppm = physics.rf_pair_ppm / 2;
    const auto endpoint_draw = [&](double half_ppm) {
        return std::uniform_real_distribution<double>(-half_ppm, half_ppm)(static_rng);
    };
    effects.tx.audio_ppm = endpoint_draw(base_half_ppm); effects.rx.audio_ppm = endpoint_draw(base_half_ppm);
    effects.tx.rf_ppm = endpoint_draw(shift_half_ppm); effects.rx.rf_ppm = endpoint_draw(shift_half_ppm);
    if (physics.shared_shift_clock) {
        effects.tx.rf_ppm = effects.tx.audio_ppm;
        effects.rx.rf_ppm = effects.rx.audio_ppm;
    }
    const auto relative_phase = physics.phase_diffusion;
    effects.tx.phase_noise_rad_per_sqrt_second = relative_phase / std::sqrt(2.);
    effects.rx.phase_noise_rad_per_sqrt_second = relative_phase / std::sqrt(2.);
    const auto arrival_draw = std::uniform_real_distribution<double>(physical.arrival_low, physical.arrival_high)(arrival_rng);
    effects.timing_offset_seconds = o.arrival.value_or(arrival_draw);
    const auto selected_delay_ms = physics.selected_delay_ms;
    effects.spread_delay_ms = physics.delay_ms;
    effects.spread_doppler_hz = physics.doppler_hz;
    effects.perturbation_slow_depth = physics.slow_depth;
    effects.perturbation_fast_depth = physics.fast_depth;
    config.selective_model.model = SelectiveModel::DispersiveDft;
    config.selective_model.correlation_spacing_hz = g.carrier_spacing_hz;
    config.selective_model.center_hz = 0;
    if (o.selective_modulation_rate_hz && o.control == "selected")
        config.selective_model.modulation_rate_hz = *o.selective_modulation_rate_hz;
    config.signal_gain = 1;
    config.complex_noise_variance = sample_rate / std::pow(10., cn0 / 10);
    config.maximum_chunk_samples = 4096;
    // A fixed, public complete observation calendar prevents the random
    // arrival/clock realization from choosing the exported file length.
    const auto tx_min = 1 - base_half_ppm * 1e-6;
    const auto rx_max = 1 + base_half_ppm * 1e-6;
    const auto pair_denominator = 1 - base_oscillator.clock_error_ppm * .5e-6;
    const auto deterministic_cfo_bound_hz = (physical.rf_hz * physics.rf_pair_ppm +
        data_if_hz * base_oscillator.clock_error_ppm) * 1e-6 / pair_denominator;
    const auto sample_clock_pair_ppm = base_oscillator.clock_error_ppm / pair_denominator;
    const auto tail_bound_seconds = selected_delay_ms * .001 + 2 / (3 * g.carrier_spacing_hz) + 32. / sample_rate;
    config.minimum_capture_samples = static_cast<std::uint64_t>(std::ceil(sample_rate * rx_max *
        (physical.capture_lead + static_cast<double>(helper_end) / tx_min + physical.arrival_high +
         physical.capture_trail + tail_bound_seconds))) +
        static_cast<std::uint64_t>(std::ceil(sample_rate * o.receiver_tail_seconds));
    const auto template_tail = static_cast<std::uint64_t>(std::ceil(sample_rate * tail_bound_seconds));
    const auto template_samples = g.source_samples + template_tail;
    const auto export_bytes = static_cast<long double>(config.minimum_capture_samples) * 17 +
        (o.templates ? static_cast<long double>(template_samples) * 26 * 16 : 0);
    if (export_bytes > maximum_export_bytes) throw std::runtime_error("export exceeds the explicit 2 GiB fixture budget");

    long double source_data_energy = 0, source_helper_energy = 0, source_composite_energy = 0;
    std::uint64_t source_rows = 0;
    EmittedSource emitted;
    emitted.format = f;
    emitted.geometry = g;
    emitted.geometry.source_samples = composite_samples;
    emitted.geometry.calendar_origin_sample = 0;
    emitted.minimum_frequency_hz = std::min(-complete_data_bandwidth / 2,
        helper::low_pilot_frequency_hz - data_if_hz) - spectral_skirt_allowance_hz;
    emitted.maximum_frequency_hz = std::max(complete_data_bandwidth / 2,
        plan.frequency_hz - data_if_hz) + spectral_skirt_allowance_hz;
    emitted.working_bytes = data_source.working_bytes() + sizeof(generator);
    emitted.sample_at = [&](std::uint64_t n) {
        const auto seconds = static_cast<long double>(n) / sample_rate;
        Complex raw_data;
        if (o.data && n >= data_offset && n - data_offset < g.source_samples)
            raw_data = data_source.sample_at(static_cast<long double>(n - data_offset));
        const auto up = rotation(data_if_hz, seconds);
        const auto data = raw_data * data_gain;
        const auto helper_input = o.power_reference == "legacy-window" ? raw_data : raw_data * data_calibration_gain;
        const auto audio = o.helper ?
            generator.mix_analytic(helper_input * up, seconds, sample_rate, helper_end, 0) * composite_gain : data * up;
        const auto helper_component = o.helper ? audio - data * up : Complex{};
        const auto value = audio * std::conj(up);
        source_data_energy += std::norm(data);
        source_helper_energy += std::norm(helper_component);
        source_composite_energy += std::norm(value);
        ++source_rows;
        return value;
    };
    StreamingChannel channel(emitted, config);
    if (channel.total_samples() != config.minimum_capture_samples)
        throw std::runtime_error("public observation bound did not cover the complete channel tail");
    if (!std::filesystem::create_directory(o.output))
        throw std::runtime_error("output must be a fresh directory; existing outputs are never overwritten");
    auto observation = output_file(o.output / "observation.c128", true);
    auto validity_file = output_file(o.output / "validity.u8", true);
    std::vector<Complex> buffer(config.maximum_chunk_samples);
    std::vector<std::uint8_t> validity(buffer.size());
    std::uint64_t written = 0;
    while (const auto count = channel.read(buffer, validity)) {
        write_complex(observation, std::span(buffer).first(count));
        validity_file.write(reinterpret_cast<const char*>(validity.data()), static_cast<std::streamsize>(count));
        written += count;
    }
    observation.close(); validity_file.close();
    if (written != channel.total_samples() || source_rows != composite_samples)
        throw std::runtime_error("incomplete composite/capture export");

    if (o.templates) {
        auto templates = output_file(o.output / "templates.c128", true);
        for (int epoch_offset = -6; epoch_offset <= 6; ++epoch_offset) {
            auto candidate_format = f;
            candidate_format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(f.epoch) + epoch_offset);
            for (std::uint8_t bit = 0; bit < 2; ++bit) {
                PatternSource candidate(candidate_format, key, bit);
                for (std::uint64_t first = 0; first < template_samples;) {
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), template_samples - first));
                    for (std::size_t i = 0; i < count; ++i)
                        buffer[i] = candidate.sample_at(static_cast<long double>(first + i)) * data_gain;
                    write_complex(templates, std::span(buffer).first(count));
                    first += count;
                }
            }
        }
        templates.close();
    }

    const auto& report = channel.report();
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    const auto cpu = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    auto truth = output_file(o.output / "evaluator-truth.json");
    truth << "{\"schema\":\"triad-helper-evaluator-truth-v1\",\"receiver_input\":false,\"source_bit\":" << o.bit
        << ",\"source_epoch_offset_seconds\":0,\"arrival_seconds\":" << effects.timing_offset_seconds
        << ",\"arrival_override\":" << boolean(o.arrival.has_value()) << ",\"unmodified_arrival_draw_seconds\":" << arrival_draw
        << ",\"tx\":"; endpoint_json(truth, effects.tx);
    truth << ",\"rx\":"; endpoint_json(truth, effects.rx);
    truth << ",\"random_seed_roots\":{\"fixture\":" << o.seed << ",\"channel\":" << effects.seed
        << ",\"static_endpoint\":" << static_seed << ",\"arrival\":" << arrival_seed
        << ",\"oscillator\":" << *config.oscillator_seed << ",\"noise\":" << *config.noise_seed
        << ",\"frontend\":" << *config.frontend_seed << ",\"path\":" << *config.path_seed
        << "},\"source_energy\":{\"rows\":" << source_rows
        << ",\"data_energy_samples\":" << source_data_energy << ",\"helper_energy_samples\":" << source_helper_energy
        << ",\"composite_energy_samples\":" << source_composite_energy
        << ",\"data_full_support_mean_norm\":" << source_data_energy / g.source_samples
        << ",\"data_calendar_mean_norm\":" << source_data_energy / (g.bit_seconds * sample_rate)
        << ",\"data_active_mean_norm\":" << source_data_energy / (data_reference.active_seconds * sample_rate)
        << ",\"scope\":\"emitted components before shared propagation; one fixed public reference gain; no per-bit normalization\"}"
        << ",\"channel_report\":{\"samples\":" << report.samples << ",\"source_samples\":" << report.source_samples
        << ",\"tail_samples\":" << report.tail_samples << ",\"natural_complete_samples\":" << report.natural_complete_samples
        << ",\"observation_padding_samples\":" << report.observation_padding_samples
        << ",\"erased_samples\":" << report.erased_samples << ",\"out_of_passband_samples\":" << report.out_of_passband_samples
        << ",\"signal_energy_samples\":" << report.signal_energy << ",\"observed_energy_samples\":" << report.observed_energy
        << ",\"working_bytes\":" << report.working_bytes << ",\"selective_perturbation\":";
    selective_json(truth, report.selective_perturbation);
    truth << "}}\n";
    truth.close();

    auto manifest = output_file(o.output / "manifest.json");
    manifest << "{\"schema\":\"triad-helper-public-v1\",\"case\":"; json_string(manifest, o.scenario);
    manifest << ",\"control\":"; json_string(manifest, o.control);
    manifest << ",\"sample_rate_hz\":" << sample_rate
        << ",\"observation\":{\"file\":\"observation.c128\",\"dtype\":\"<c16\",\"shape\":[" << written
        << "],\"frame\":\"zero-centered DATA; entire audio composite derotated by nominal DATA IF before TX oscillator\"}"
        << ",\"validity\":{\"file\":\"validity.u8\",\"dtype\":\"u1\",\"shape\":[" << written << "]}"
        << ",\"evaluator_truth_file\":\"evaluator-truth.json\",\"evaluator_truth_is_receiver_input\":false"
        << ",\"data_enabled\":" << boolean(o.data) << ",\"helper_enabled\":" << boolean(o.helper)
        << ",\"public_epoch_seconds\":" << f.epoch << ",\"key_seed\":" << o.key_seed
        << ",\"encrypted_source\":true,\"encrypted_helper_metadata\":true,\"helper_callsign\":\"TRIAD\""
        << ",\"format\":{\"inner_rate_hz\":" << f.inner_rate_hz << ",\"added_symbols\":" << f.added_symbols
        << ",\"added_interleave\":true,\"requested_bit_seconds\":" << f.bit_seconds
        << ",\"bit_seconds\":" << g.bit_seconds << ",\"fine_chip_rate_hz\":" << g.fine_chip_rate_hz
        << ",\"carrier_spacing_hz\":" << g.carrier_spacing_hz << ",\"occupied_bandwidth_hz\":" << g.occupied_bandwidth_hz
        << ",\"inner_coefficients\":" << g.inner_coefficients << ",\"fine_coefficients\":" << g.fine_coefficients
        << ",\"tile_seconds\":" << g.tile_seconds << ",\"tile_count\":" << g.tile_count
        << ",\"fhss_envelope_only\":" << boolean(f.fhss) << ",\"fhss_center_aligned\":" << boolean(f.fhss_center_aligned)
        << ",\"pulse_shape\":";
    json_string(manifest, triad::v3::pulse_shape_name(f.pulse_shape));
    manifest
        << ",\"physical_rf_retuning\":false,\"dwell_seconds\":" << f.dwell_seconds
        << ",\"quiet_seconds\":" << f.quiet_seconds << ",\"ramp_seconds\":" << f.ramp_seconds
        << ",\"margin_seconds\":" << f.margin_seconds << '}'
        << ",\"calendar\":{\"data_calendar_start_seconds\":38,\"data_sample_offset\":" << data_offset
        << ",\"data_calendar_origin_sample\":" << g.calendar_origin_sample << ",\"data_source_samples\":" << g.source_samples
        << ",\"composite_source_samples\":" << composite_samples << ",\"composite_calendar_origin_sample\":0"
        << ",\"helper_start_seconds\":0,\"helper_end_seconds\":" << helper_end
        << ",\"capture_lead_seconds\":" << physical.capture_lead << ",\"capture_trail_seconds\":" << physical.capture_trail
        << ",\"arrival_prior_seconds\":[" << physical.arrival_low << ',' << physical.arrival_high << ']'
        << ",\"capture_samples\":" << written << ",\"fixed_capture_tail_bound_seconds\":" << tail_bound_seconds
        << ",\"receiver_tail_seconds\":" << o.receiver_tail_seconds
        << ",\"sample_time_definition\":\"nominal capture time n/Fs-capture_lead_seconds; unknown physical RX clock retained in channel\"}"
        << ",\"helper_plan\":{\"high_hz\":" << plan.frequency_hz << ",\"low_hz\":" << plan.low_frequency_hz
        << ",\"auxiliary_hz\":" << plan.auxiliary_frequency_hz << ",\"data_gain\":" << plan.data_gain
        << ",\"helper_gain\":" << plan.helper_gain << ",\"effective_power_w\":" << plan.effective_power_w
        << ",\"pilot_dwell_gating\":" << boolean(plan.pilot_dwell_gating)
        << ",\"placement_waveform_bandwidth_hz\":" << placement.waveform_bandwidth_hz
        << ",\"placement_gate_bandwidth_hz\":" << placement.gate_bandwidth_hz << '}'
        << ",\"shaping_plan\":{\"enabled\":" << boolean(o.shaping == "auto")
        << ",\"selected_pulse_shape\":";
    json_string(manifest, triad::v3::pulse_shape_name(f.pulse_shape));
    manifest << ",\"selected_ramp_seconds\":" << f.ramp_seconds << ",\"certificate_method\":";
    json_string(manifest, selected_shaping.spectrum.method);
    manifest << ",\"certificate_limitation\":";
    json_string(manifest, selected_shaping.spectrum.limitation);
    manifest << ",\"placement_bandwidth_hz\":" << selected_shaping.spectrum.placement_bandwidth_hz
        << ",\"measured_bandwidth_hz\":" << selected_shaping.spectrum.measured_bandwidth_hz
        << ",\"measured_tail_fraction\":" << selected_shaping.spectrum.measured_tail_fraction
        << ",\"requested_tail_fraction\":" << selected_shaping.spectrum.requested_tail_fraction
        << ",\"coarse_width_hz\":" << selected_shaping.spectrum.coarse_width_hz
        << ",\"refined_width_hz\":" << selected_shaping.spectrum.refined_width_hz
        << ",\"phase_refinement_width_hz\":" << selected_shaping.spectrum.phase_refinement_width_hz
        << ",\"phase_refinement_error_hz\":" << selected_shaping.spectrum.phase_refinement_error_hz
        << ",\"sampling_refinement_error_hz\":" << selected_shaping.spectrum.sampling_refinement_error_hz
        << ",\"frequency_bin_hz\":" << selected_shaping.spectrum.frequency_bin_hz
        << ",\"far_outside_energy_fraction\":" << selected_shaping.spectrum.far_outside_energy_fraction
        << ",\"normalized_pulse_energy\":" << selected_shaping.spectrum.normalized_pulse_energy
        << ",\"phase_samples\":" << selected_shaping.spectrum.phase_samples
        << ",\"fft_size\":" << selected_shaping.spectrum.fft_size
        << ",\"kernel_ffts\":" << selected_shaping.spectrum.kernel_ffts
        << ",\"total_kernel_ffts\":" << selected_shaping.spectrum.total_kernel_ffts
        << ",\"phase_refinement_attempts\":" << selected_shaping.spectrum.phase_refinement_attempts
        << ",\"converged\":" << boolean(selected_shaping.spectrum.converged)
        << ",\"fallback\":" << boolean(selected_shaping.fallback) << ",\"fallback_reason\":";
    json_string(manifest, selected_shaping.reason);
    manifest << ",\"candidates_evaluated\":" << selected_shaping.candidate_count
        << ",\"total_planner_kernel_ffts\":" << selected_shaping.total_kernel_ffts
        << ",\"capture_placement_remains_conservative\":true}"
        << ",\"power\":{\"data_w\":" << physical.data_w << ",\"helper_w\":" << physical.helper_w
        << ",\"composite_w\":" << helper_options.power_w << ",\"path_loss_db\":" << physical.loss_db
        << ",\"requested_helper_w\":" << physical.helper_w << ",\"effective_helper_w\":" << physics.effective_helper_w
        << ",\"effective_composite_w\":" << physical.data_w + physics.effective_helper_w
        << ",\"helper_power_limited\":" << boolean(physics.helper_power_limited)
        << ",\"helper_ratio_limit_db\":" << helper_options.ratio_limit_db
        << ",\"effective_helper_cn0_db_hz\":" << cn0 + 10 * std::log10(physics.effective_helper_w / physical.data_w)
        << ",\"noise_n0_dbm_hz\":" << thermal_n0_dbm_hz << ",\"physical_data_cn0_db_hz\":" << physical_cn0
        << ",\"data_cn0_db_hz\":" << cn0 << ",\"cn0_override\":" << boolean(o.cn0.has_value())
        << ",\"planning_target_db_metadata_only\":" << physical.planning_target_db
        << ",\"complex_noise_variance\":" << config.complex_noise_variance
        << ",\"reference_bit\":0,\"reference_epoch_seconds\":" << f.epoch
        << ",\"reference_window_seconds\":[39,41],\"reference_accepted_nodes\":" << reference_nodes
        << ",\"reference_data_mean_norm\":" << reference_mean_norm
        << ",\"reference_data_mean_real_power\":" << *reference.data_mean_power
        << ",\"post_plan_composite_gain\":" << composite_gain << ',';
    data_power_json(manifest, o, data_reference, data_calibration_gain, data_gain);
    manifest
        << ",\"reference_normalized_data_mean_norm\":1,\"reference_measured_helper_to_data_ratio\":" << reference_helper_ratio
        << ",\"helper_preview_normalized_data_mean_norm\":" << data_gain * data_gain * reference_mean_norm
        << ",\"nominal_helper_to_data_ratio\":" << physics.effective_helper_w / physical.data_w
        << ",\"reference_scope\":\"39-41s preview belongs to production Helper planning only; measured Helper norm is relative to nominal active DATA norm1; DATA normalization follows data_power_reference_policy\"}"
        << ",\"channel\":{\"rf_hz\":" << physical.rf_hz << ",\"nominal_conversion_if_hz\":" << data_if_hz
        << ",\"base_oscillator\":"; json_string(manifest, base_oscillator.id);
    manifest << ",\"shift_oscillator\":"; json_string(manifest, physics.shift_id);
    // The shared-clock request is a topology, not a separate oscillator preset.
    // Keep the public request ID instead of silently substituting the base ID.
    manifest << ",\"shift_reference\":"; json_string(manifest, physics.shift_id);
    manifest << ",\"shift_model_preset\":"; json_string(manifest, shift_oscillator.id);
    manifest << ",\"shift_active\":" << boolean(physics.shift_active)
        << ",\"shared_shift_clock\":" << boolean(physics.shared_shift_clock)
        << ",\"independent_base_and_shift\":" << boolean(physics.shift_active && !physics.shared_shift_clock)
        << ",\"base_relative_pair_ppm_bound\":" << base_oscillator.clock_error_ppm
        << ",\"rf_relative_pair_ppm_bound\":" << physics.rf_pair_ppm
        << ",\"relative_pair_ppm_bound\":" << base_oscillator.clock_error_ppm
        << ",\"endpoint_uniform_half_bound_ppm\":" << base_half_ppm
        << ",\"rf_endpoint_uniform_half_bound_ppm\":" << shift_half_ppm
        << ",\"sample_clock_pair_ppm\":" << sample_clock_pair_ppm
        << ",\"nominal_shift_ppm_bound\":" << physics.rf_pair_ppm
        << ",\"nominal_shift_hz_bound\":" << physical.rf_hz * physics.rf_pair_ppm * 1e-6
        << ",\"relative_phase_diffusion_rad_sqrt_s\":" << relative_phase
        << ",\"phase_convention\":\"triad_long: independent active RF uses hypot(base,shift); zero RF or shared reference uses base; relative diffusion divided by sqrt(2) per endpoint\""
        << ",\"deterministic_cfo_bound_hz\":" << deterministic_cfo_bound_hz
        << ",\"spread_preset\":"; json_string(manifest, physical.spread.id);
    manifest << ",\"spread_margin\":" << physical.margin << ",\"spread_delay_ms\":" << effects.spread_delay_ms
        << ",\"spread_doppler_hz\":" << effects.spread_doppler_hz
        << ",\"selective_slow_depth\":" << effects.perturbation_slow_depth << ",\"selective_fast_depth\":" << effects.perturbation_fast_depth
        << ",\"selective_perturbation\":"; json_string(manifest, physics.requested_modulation_id);
    manifest << ",\"selective_modulation_rate_hz\":" << physics.modulation_rate_hz
        << ",\"selective_enabled\":" << boolean(physics.modulation_rate_hz > 0 && (physics.slow_depth > 0 || physics.fast_depth > 0))
        << ",\"selective_model\":\"dispersive-dft-v1\",\"selective_scale_hz\":" << g.carrier_spacing_hz
        << ",\"selective_center_hz\":0,\"source_frequency_bounds_hz\":[" << emitted.minimum_frequency_hz << ',' << emitted.maximum_frequency_hz
        << "],\"source_spectral_skirt_allowance_hz\":" << spectral_skirt_allowance_hz
        << ",\"source_capture_design_bandwidth_hz\":" << complete_data_bandwidth
        << ",\"bandwidth_scope\":\"Source/capture preflight retains nominal design width plus the complete legacy 2/ramp allowance and declared 200-Hz skirt; only the Helper placement band uses the key-independent shaping certificate. Finite pulse, gated transmission and stochastic channel spectra are not compact support.\""
        << ",\"data_if_default_reason\":\"Legacy fixtures preserve HF1725Hz and EME4000Hz defaults. Auto shaping starts at standardized1725Hz; explicit DATA IF takes precedence.\""
        << ",\"composition\":\"production Helper Generator and DATA summed before shared TX oscillator, spread, dispersive selective stage, RX oscillator and noise; no post-channel hop gate\"}"
        << ",\"physics\":";
    physics_json(manifest, o, physical, physics, data_if_hz);
    manifest << ",\"templates\":{\"enabled\":" << boolean(o.templates) << ",\"file\":";
    if (o.templates) manifest << "\"templates.c128\""; else manifest << "null";
    manifest << ",\"dtype\":\"<c16\",\"shape\":[13,2," << template_samples
        << "],\"epoch_offsets_seconds\":[-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6],\"bits\":[0,1]"
        << ",\"source_support_samples\":" << g.source_samples << ",\"zero_tail_samples\":" << template_tail
        << ",\"frame\":\"zero-centered DATA source; insert at calendar.data_sample_offset in composite source\""
        << ",\"scope\":\"public encrypted source alternatives only; fixed shared calibration; no Helper, arrival or channel realization\"}"
        << ",\"cost\":{\"wall_seconds\":" << elapsed << ",\"process_cpu_seconds\":" << cpu
        << ",\"binary_export_bytes\":" << static_cast<std::uint64_t>(export_bytes)
        << ",\"channel_working_bytes\":" << report.working_bytes
        << ",\"source_reference_working_bytes\":" << reference_source.working_bytes()
        << ",\"maximum_export_bytes\":" << maximum_export_bytes << "}}\n";
    manifest.close();
    std::cout << "Exported " << written << " samples to " << o.output.string()
              << "; C/N0=" << std::setprecision(10) << cn0 << " dB-Hz; high pilot=" << plan.frequency_hz << " Hz\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& error) {
        std::cerr << "triad_helper_export: " << error.what() << '\n';
        return 1;
    }
}
