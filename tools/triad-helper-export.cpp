// Isolated sampled fixture: production Helper transmitter + encrypted triad v3.
// No ordinary Robust receive path is instantiated here.
#include "datapump/helper_carrier.hpp"
#include "datapump/triad_modem_v3.hpp"
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
constexpr std::uint32_t sample_rate = 18000;
constexpr std::uint64_t dwell_samples = 7200;
constexpr double data_start_seconds = 38;
constexpr double thermal_n0_dbm_hz = -164;
constexpr double spectral_skirt_allowance_hz = 200;
constexpr std::uint64_t maximum_export_bytes = 2ULL * 1024 * 1024 * 1024;
constexpr double tau = 2 * std::numbers::pi;

struct Options {
    std::string scenario = "hf", control = "selected";
    std::filesystem::path output;
    std::uint64_t seed = 1, key_seed = 72001, epoch = 1793850000;
    unsigned bit = 0;
    double seconds = 16, data_if_hz = 1725;
    std::optional<double> cn0, arrival;
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
                "  [--seconds 16] [--data-if HZ] [--cn0 DB_HZ]\n"
                "  [--control selected|flat] [--data on|off] [--helper on|off]\n"
                "  [--arrival-seconds T] [--templates]\n"
                "18000 Hz complex samples; default DATA IF 1725 Hz HF, 4000 Hz EME.\n"
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
        else if (arg == "--data-if") { o.data_if_hz = real(next(), "DATA IF"); o.explicit_data_if = true; }
        else if (arg == "--cn0") o.cn0 = real(next(), "C/N0");
        else if (arg == "--arrival-seconds") o.arrival = real(next(), "arrival");
        else if (arg == "--control") o.control = next();
        else if (arg == "--data") o.data = enabled(next());
        else if (arg == "--helper") o.helper = enabled(next());
        else if (arg == "--templates") o.templates = true;
        else throw std::invalid_argument("unknown option: " + std::string(arg));
    }
    if (o.scenario != "hf" && o.scenario != "eme") throw std::invalid_argument("case must be hf or eme");
    if (!o.explicit_data_if && o.scenario == "eme") o.data_if_hz = 4000;
    if (o.control != "selected" && o.control != "flat") throw std::invalid_argument("control must be selected or flat");
    if (o.output.empty()) throw std::invalid_argument("--output is required");
    if (o.seconds <= 0 || o.seconds > 3600) throw std::invalid_argument("seconds must be in (0,3600]");
    if (o.data_if_hz < 1725 || o.data_if_hz >= sample_rate / 2.)
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
    if (o.scenario == "hf")
        return {4, 40, 180, 30e6, 8, 3, -7.035, 7.035, 8, 8,
            tuning::parse_oscillator_preset("gpsdo-xo"), tuning::parse_spread_preset("hf-worst")};
    return {40, 15000, 200, 5.8e9, 1.5, 1, -.055, .125, .2, .3,
        tuning::parse_oscillator_preset("gpsdo-ocxo"), tuning::parse_spread_preset("eme-focused")};
}

Format format_for(const Options& o) {
    Format f;
    const bool eme = o.scenario == "eme";
    f.inner_rate_hz = eme ? 360 : 36;
    f.added_symbols = eme ? 1 : 10;
    f.added_interleave = true;
    f.sample_rate = sample_rate;
    f.bit_seconds = o.seconds;
    f.fhss = eme;
    f.fhss_center_aligned = eme;
    f.quiet_seconds = eme ? .055 : 0;
    f.ramp_seconds = eme ? 2. / 3600 : .02;
    f.margin_seconds = .005;
    f.encrypt = true;
    f.epoch = o.epoch;
    return f;
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
    if (o.arrival && (*o.arrival < physical.arrival_low || *o.arrival > physical.arrival_high))
        throw std::invalid_argument("diagnostic arrival override must stay inside the public arrival prior");
    const auto f = format_for(o);
    const auto g = geometry(f);
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

    helper::Options helper_options;
    helper_options.power_w = physical.data_w + physical.helper_w;
    helper_options.data_power_w = physical.data_w;
    helper_options.timecode = true;
    helper_options.callsign = "TRIAD";
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
    // Cover remains nonzero on envelope ramps even when protected payload
    // cells fit wholly inside the dwell. Preserve the full engineering gate
    // allowance; it is not compact support. EME needs a higher audio IF to
    // accommodate this band and the production Helper separation guard.
    const auto gate_bandwidth = 2 / f.ramp_seconds;
    const auto complete_data_bandwidth = g.occupied_bandwidth_hz + gate_bandwidth;
    const helper::PlacementBand placement{o.data_if_hz, g.occupied_bandwidth_hz, gate_bandwidth, f.fhss};
    helper::Plan plan;
    try { plan = helper::plan(helper_options, placement, sample_rate, 0, 1, &reference); }
    catch (const std::exception& error) {
        throw std::runtime_error("production Helper plan rejected DATA IF " + std::to_string(o.data_if_hz) +
                                 " Hz: " + error.what());
    }
    // One public bit-0 reference freezes the gain for both bit choices, all
    // epochs, and every control. Never normalize a received block or a trial.
    const auto composite_gain = 1 / (plan.data_gain * std::sqrt(reference_mean_norm));
    const auto data_gain = plan.data_gain * composite_gain;
    helper::Generator generator(helper_options, plan, f.epoch, true, key);
    long double measured_helper_norm = 0;
    for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
        const auto seconds = helper::power_reference_node(reference, i);
        if (reference.active(seconds))
            measured_helper_norm += std::norm(generator.mix_analytic({}, seconds, sample_rate, helper_end, 0) * composite_gain);
    }
    const auto reference_helper_ratio = static_cast<double>(measured_helper_norm / reference_nodes);

    const auto physical_cn0 = 10 * std::log10(physical.data_w * 1000) - physical.loss_db - thermal_n0_dbm_hz;
    const auto cn0 = o.cn0.value_or(physical_cn0);
    StreamChannelConfig config;
    auto& effects = config.effects;
    effects.sample_rate = sample_rate;
    effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    effects.rf_hz = physical.rf_hz;
    effects.nominal_conversion_if_hz = o.data_if_hz;
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
    const auto half_ppm = physical.oscillator.clock_error_ppm / 2;
    const auto endpoint_draw = [&]() { return std::uniform_real_distribution<double>(-half_ppm, half_ppm)(static_rng); };
    effects.tx.audio_ppm = endpoint_draw(); effects.rx.audio_ppm = endpoint_draw();
    effects.tx.rf_ppm = endpoint_draw(); effects.rx.rf_ppm = endpoint_draw();
    const auto one_component_phase = physical.oscillator.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
    const auto relative_phase = std::hypot(one_component_phase, one_component_phase);
    effects.tx.phase_noise_rad_per_sqrt_second = relative_phase / std::sqrt(2.);
    effects.rx.phase_noise_rad_per_sqrt_second = relative_phase / std::sqrt(2.);
    const auto arrival_draw = std::uniform_real_distribution<double>(physical.arrival_low, physical.arrival_high)(arrival_rng);
    effects.timing_offset_seconds = o.arrival.value_or(arrival_draw);
    const auto selected_delay_ms = physical.spread.delay_ms * physical.margin;
    const auto selected_doppler_hz = physical.spread.doppler_hz * physical.margin;
    if (o.control == "selected") {
        effects.spread_delay_ms = selected_delay_ms;
        effects.spread_doppler_hz = selected_doppler_hz;
        effects.perturbation_slow_depth = effects.perturbation_fast_depth = .65;
    }
    config.selective_model.model = SelectiveModel::DispersiveDft;
    config.selective_model.correlation_spacing_hz = g.carrier_spacing_hz;
    config.selective_model.center_hz = 0;
    config.signal_gain = 1;
    config.complex_noise_variance = sample_rate / std::pow(10., cn0 / 10);
    config.maximum_chunk_samples = 4096;
    // A fixed, public complete observation calendar prevents the random
    // arrival/clock realization from choosing the exported file length.
    const auto tx_min = 1 - half_ppm * 1e-6;
    const auto rx_max = 1 + half_ppm * 1e-6;
    const auto tail_bound_seconds = selected_delay_ms * .001 + 2 / (3 * g.carrier_spacing_hz) + 32. / sample_rate;
    config.minimum_capture_samples = static_cast<std::uint64_t>(std::ceil(sample_rate * rx_max *
        (physical.capture_lead + static_cast<double>(helper_end) / tx_min + physical.arrival_high +
         physical.capture_trail + tail_bound_seconds)));
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
        helper::low_pilot_frequency_hz - o.data_if_hz) - spectral_skirt_allowance_hz;
    emitted.maximum_frequency_hz = std::max(complete_data_bandwidth / 2,
        plan.frequency_hz - o.data_if_hz) + spectral_skirt_allowance_hz;
    emitted.working_bytes = data_source.working_bytes() + sizeof(generator);
    emitted.sample_at = [&](std::uint64_t n) {
        const auto seconds = static_cast<long double>(n) / sample_rate;
        Complex raw_data;
        if (o.data && n >= data_offset && n - data_offset < g.source_samples)
            raw_data = data_source.sample_at(static_cast<long double>(n - data_offset));
        const auto up = rotation(o.data_if_hz, seconds);
        const auto data = raw_data * data_gain;
        const auto audio = o.helper ?
            generator.mix_analytic(raw_data * up, seconds, sample_rate, helper_end, 0) * composite_gain : data * up;
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
        << ",\"sample_time_definition\":\"nominal capture time n/Fs-capture_lead_seconds; unknown physical RX clock retained in channel\"}"
        << ",\"helper_plan\":{\"high_hz\":" << plan.frequency_hz << ",\"low_hz\":" << plan.low_frequency_hz
        << ",\"auxiliary_hz\":" << plan.auxiliary_frequency_hz << ",\"data_gain\":" << plan.data_gain
        << ",\"helper_gain\":" << plan.helper_gain << ",\"effective_power_w\":" << plan.effective_power_w
        << ",\"pilot_dwell_gating\":" << boolean(plan.pilot_dwell_gating)
        << ",\"placement_waveform_bandwidth_hz\":" << placement.waveform_bandwidth_hz
        << ",\"placement_gate_bandwidth_hz\":" << placement.gate_bandwidth_hz << '}'
        << ",\"power\":{\"data_w\":" << physical.data_w << ",\"helper_w\":" << physical.helper_w
        << ",\"composite_w\":" << helper_options.power_w << ",\"path_loss_db\":" << physical.loss_db
        << ",\"noise_n0_dbm_hz\":" << thermal_n0_dbm_hz << ",\"physical_data_cn0_db_hz\":" << physical_cn0
        << ",\"data_cn0_db_hz\":" << cn0 << ",\"cn0_override\":" << boolean(o.cn0.has_value())
        << ",\"planning_target_db_metadata_only\":" << physical.planning_target_db
        << ",\"complex_noise_variance\":" << config.complex_noise_variance
        << ",\"reference_bit\":0,\"reference_epoch_seconds\":" << f.epoch
        << ",\"reference_window_seconds\":[39,41],\"reference_accepted_nodes\":" << reference_nodes
        << ",\"reference_data_mean_norm\":" << reference_mean_norm
        << ",\"reference_data_mean_real_power\":" << *reference.data_mean_power
        << ",\"post_plan_composite_gain\":" << composite_gain << ",\"template_data_gain\":" << data_gain
        << ",\"reference_normalized_data_mean_norm\":1,\"reference_measured_helper_to_data_ratio\":" << reference_helper_ratio
        << ",\"nominal_helper_to_data_ratio\":" << physical.helper_w / physical.data_w
        << ",\"reference_scope\":\"fixed public bit0, active DATA samples including nonzero ramps; no Helper-only lead; all controls and epochs retain the same gain\"}"
        << ",\"channel\":{\"rf_hz\":" << physical.rf_hz << ",\"nominal_conversion_if_hz\":" << o.data_if_hz
        << ",\"base_oscillator\":"; json_string(manifest, physical.oscillator.id);
    manifest << ",\"shift_oscillator\":"; json_string(manifest, physical.oscillator.id);
    manifest << ",\"independent_base_and_shift\":true,\"relative_pair_ppm_bound\":" << physical.oscillator.clock_error_ppm
        << ",\"endpoint_uniform_half_bound_ppm\":" << half_ppm << ",\"relative_phase_diffusion_rad_sqrt_s\":" << relative_phase
        << ",\"phase_convention\":\"triad_long: hypot(base,shift) relative pair diffusion divided by sqrt(2) per endpoint\""
        << ",\"deterministic_cfo_bound_hz\":" << (physical.rf_hz + o.data_if_hz) * physical.oscillator.clock_error_ppm * 1e-6 / tx_min
        << ",\"spread_preset\":"; json_string(manifest, physical.spread.id);
    manifest << ",\"spread_margin\":" << physical.margin << ",\"spread_delay_ms\":" << effects.spread_delay_ms
        << ",\"spread_doppler_hz\":" << effects.spread_doppler_hz
        << ",\"selective_slow_depth\":" << effects.perturbation_slow_depth << ",\"selective_fast_depth\":" << effects.perturbation_fast_depth
        << ",\"selective_model\":\"dispersive-dft-v1\",\"selective_scale_hz\":" << g.carrier_spacing_hz
        << ",\"selective_center_hz\":0,\"source_frequency_bounds_hz\":[" << emitted.minimum_frequency_hz << ',' << emitted.maximum_frequency_hz
        << "],\"source_spectral_skirt_allowance_hz\":" << spectral_skirt_allowance_hz
        << ",\"bandwidth_scope\":\"nominal RRC design band plus full 2/ramp gate allowance and declared Helper skirt allowance; finite pulses, gates and stochastic spectra are not compact support; wideband refinement and Helper-only interference controls remain necessary\""
        << ",\"data_if_default_reason\":\"EME uses 4000 Hz to retain the conservative fast-gate band in the production Helper placement geometry; HF uses 1725 Hz\""
        << ",\"composition\":\"production Helper Generator and DATA summed before shared TX oscillator, spread, dispersive selective stage, RX oscillator and noise; no post-channel hop gate\"}"
        << ",\"templates\":{\"enabled\":" << boolean(o.templates) << ",\"file\":";
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
