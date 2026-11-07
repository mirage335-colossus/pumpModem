#include "triad_fixture_key.hpp"
#include "datapump/triad_modem_v2.hpp"
#include "datapump/triad_simulation.hpp"
#include "search_fft.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace datapump;
using namespace datapump::triad::v2;
using datapump::triad::SimulationConfig;
using datapump::triad::simulate;

struct Options {
    std::string which = "2", receiver = "all", control = "signal";
    std::string interleave = "on";
    std::string arithmetic = "automatic";
    unsigned trials = 1, threads = 4, bits = 1;
    unsigned bits_per_frame = 1, burst_wait_frames = 0;
    std::uint64_t seed = 910241;
    std::uint64_t epoch = 1'793'404'800ULL;
    std::size_t memory_mib = 256, maximum_hypotheses = 8'000'000;
    double cn0_override = std::numeric_limits<double>::quiet_NaN();
    double segment_seconds = 1.6;
    double frequency_step_hz = 0;
    double rf_hz = 30e6;
    double dropout_ms = 0, dropout_start_ms = 200;
    std::size_t dropout_every = 1;
    bool now = false, diagnostic_clean = false, agc = false, channel_none = false;
    bool epoch_supplied = false;
};

void usage() {
    std::cout <<
        "triad_examples [--case 1|2|all] [--trials N] [--bits N] [--threads N] [--seed N]\n"
        "  [--bits-per-frame N] [--burst-wait-frames N] [--cn0 DB-HZ]\n"
        "  [--segment-seconds 0|0.4|1.6|6.4] [--frequency-step HZ] [--rf-hz HZ]\n"
        "  [--receiver all|coherent|differential|amplitude-only]\n"
        "  [--control signal|null|wrong-key|wrong-epoch] [--memory-mib N]\n"
        "  [--max-hypotheses N]\n"
        "  [--interleave on|off] [--channel-none]\n"
        "  [--arithmetic automatic|fp32|fp64] [--epoch SECONDS|--now]\n"
        "  [--diagnostic-clean] [--dropout-ms N] [--dropout-start-ms N]\n"
        "  [--dropout-every N] [--agc]\n"
        "The defaults are the user's two RF/path-loss examples. --cn0 changes physical\n"
        "C/N0 only; planner target C/N0 remains 40 (case 1) or 8 dB-Hz (case 2).\n"
        "--diagnostic-clean explicitly removes channel/noise and narrows all search\n"
        "priors to the generated truth; it is a wiring check, not acquisition evidence.\n"
        "--epoch makes --now runs replayable. Each JSONL row is one receiver evaluation\n"
        "sharing a generated capture.\n";
}

double parse_double(const std::string& text, const char* name) {
    std::size_t end = 0;
    const double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value)) throw std::runtime_error(std::string("invalid finite value for ") + name);
    return value;
}

template<class T> T parse_integer(const std::string& text, const char* name) {
    if (text.empty() || text.front() == '-') throw std::runtime_error(std::string("invalid nonnegative integer for ") + name);
    std::size_t end = 0;
    const auto value = std::stoull(text, &end);
    if (end != text.size() || value > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
        throw std::runtime_error(std::string("integer outside range for ") + name);
    return static_cast<T>(value);
}

Options parse(int argc, char** argv) {
    Options o;
    const auto value = [&](int& i, const char* name) {
        if (++i == argc) throw std::runtime_error(std::string("missing value for ") + name);
        return std::string(argv[i]);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { usage(); std::exit(0); }
        if (arg == "--now") { o.now = true; continue; }
        if (arg == "--diagnostic-clean") { o.diagnostic_clean = true; continue; }
        if (arg == "--agc") { o.agc = true; continue; }
        if (arg == "--channel-none") { o.channel_none = true; continue; }
        const auto v = value(i, arg.c_str());
        if (arg == "--case") o.which = v;
        else if (arg == "--receiver") o.receiver = v;
        else if (arg == "--control") o.control = v;
        else if (arg == "--interleave") o.interleave = v;
        else if (arg == "--arithmetic") o.arithmetic = v;
        else if (arg == "--trials") o.trials = parse_integer<unsigned>(v, "--trials");
        else if (arg == "--bits") o.bits = parse_integer<unsigned>(v, "--bits");
        else if (arg == "--bits-per-frame") o.bits_per_frame = parse_integer<unsigned>(v, "--bits-per-frame");
        else if (arg == "--burst-wait-frames") o.burst_wait_frames = parse_integer<unsigned>(v, "--burst-wait-frames");
        else if (arg == "--threads") o.threads = parse_integer<unsigned>(v, "--threads");
        else if (arg == "--seed") o.seed = parse_integer<std::uint64_t>(v, "--seed");
        else if (arg == "--epoch") { o.epoch = parse_integer<std::uint64_t>(v, "--epoch"); o.epoch_supplied = true; }
        else if (arg == "--memory-mib") o.memory_mib = parse_integer<std::size_t>(v, "--memory-mib");
        else if (arg == "--max-hypotheses") o.maximum_hypotheses = parse_integer<std::size_t>(v, "--max-hypotheses");
        else if (arg == "--cn0") o.cn0_override = parse_double(v, "--cn0");
        else if (arg == "--frequency-step") o.frequency_step_hz = parse_double(v, "--frequency-step");
        else if (arg == "--rf-hz") o.rf_hz = parse_double(v, "--rf-hz");
        else if (arg == "--segment-seconds") o.segment_seconds = parse_double(v, "--segment-seconds");
        else if (arg == "--dropout-ms") o.dropout_ms = parse_double(v, "--dropout-ms");
        else if (arg == "--dropout-start-ms") o.dropout_start_ms = parse_double(v, "--dropout-start-ms");
        else if (arg == "--dropout-every") o.dropout_every = parse_integer<std::size_t>(v, "--dropout-every");
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (o.which != "1" && o.which != "2" && o.which != "all") throw std::runtime_error("--case must be 1, 2 or all");
    if (o.receiver != "all" && o.receiver != "coherent" && o.receiver != "differential" && o.receiver != "amplitude-only")
        throw std::runtime_error("unknown --receiver");
    if (o.interleave != "on" && o.interleave != "off") throw std::runtime_error("--interleave must be on or off");
    if (o.control != "signal" && o.control != "null" && o.control != "wrong-key" && o.control != "wrong-epoch")
        throw std::runtime_error("unknown --control");
    if (o.now && o.epoch_supplied) throw std::runtime_error("use only one of --now and --epoch");
    constexpr auto epoch_min = std::uint64_t{6};
    constexpr auto epoch_max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - 3612);
    if (o.epoch < epoch_min || o.epoch > epoch_max)
        throw std::runtime_error("--epoch must leave room for signed ±6 s search and wrong-epoch control");
    if (o.arithmetic != "automatic" && o.arithmetic != "fp32" && o.arithmetic != "fp64")
        throw std::runtime_error("--arithmetic must be automatic, fp32 or fp64");
    if (o.trials == 0 || o.trials > 10000 || o.bits == 0 || o.bits > 64 ||
        o.bits_per_frame == 0 || o.bits_per_frame > 64 || o.burst_wait_frames > 10000 ||
        o.threads == 0 || o.threads > 256 || o.memory_mib == 0 || o.memory_mib > 16384 ||
        o.maximum_hypotheses == 0 || o.maximum_hypotheses > 100'000'000 ||
        !std::isfinite(o.segment_seconds) || o.segment_seconds < 0 || o.segment_seconds > 30 ||
        !std::isfinite(o.frequency_step_hz) || o.frequency_step_hz < 0 ||
        o.rf_hz < 1e6 || o.rf_hz > 10e9 || (o.rf_hz != 30e6 && o.which != "2") ||
        !std::isfinite(o.dropout_ms) || o.dropout_ms < 0 || !std::isfinite(o.dropout_start_ms) ||
        o.dropout_start_ms < 0 || o.dropout_every == 0)
        throw std::runtime_error("option outside supported range");
    return o;
}

struct Case {
    unsigned number;
    double rate, target_cn0, pathloss_db, relative_base_ppm;
    bool fake_fcc;
};

Case get_case(unsigned n) {
    if (n == 1) return {1, 360, 40, 60, 100, true};
    if (n == 2) return {2, 36, 8, 180, .0001, false};
    throw std::runtime_error("invalid example number");
}

Crypto key_for(std::uint64_t seed, unsigned trial) {
    Bytes bytes(32);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 7 + 3 + (seed >> ((i % 8) * 8)) + trial * 29) & 255U);
    return datapump::triad::fixture::key(bytes);
}

std::string bits_text(std::span<const std::uint8_t> bits) {
    std::string s; s.reserve(bits.size());
    for (auto b : bits) s.push_back(b ? '1' : '0');
    return s;
}

struct Spectral {
    double outside = 0, bandwidth99 = 0, crest_db = 0, kurtosis = 0;
};

Spectral spectral(const Waveform& w, std::uint32_t sample_rate, double occupied) {
    Spectral s;
    constexpr std::size_t n = 4096;
    if (w.samples.size() < n) return s;
    modem::detail::search_fft::DoublePlan plan(n);
    std::vector<Complex> block(n);
    std::vector<double> power(n);
    std::size_t blocks = 0;
    for (std::size_t start = 0; start + n <= w.samples.size(); start += n) {
        for (std::size_t j = 0; j < n; ++j)
            block[j] = w.samples[start + j] * (.5 - .5 * std::cos(2 * std::numbers::pi * static_cast<double>(j) /
                static_cast<double>(n - 1)));
        modem::detail::search_fft::fft(block, false, plan);
        for (std::size_t j = 0; j < n; ++j) power[j] += std::norm(block[j]);
        ++blocks;
    }
    if (blocks == 0) return s;
    double total = 0;
    std::vector<std::pair<double, double>> radial;
    radial.reserve(n);
    for (std::size_t j = 0; j < n; ++j) {
        total += power[j];
        const auto signed_bin = j <= n / 2 ? static_cast<long long>(j) : static_cast<long long>(j) - static_cast<long long>(n);
        const double f = std::abs(static_cast<double>(signed_bin) * sample_rate / n);
        if (f > occupied / 2) s.outside += power[j];
        radial.emplace_back(f, power[j]);
    }
    s.outside /= std::max(total, 1e-300);
    std::sort(radial.begin(), radial.end());
    double cumulative = 0;
    for (const auto& [f, p] : radial) { cumulative += p; if (cumulative >= .99 * total) { s.bandwidth99 = 2 * f; break; } }
    double e2 = 0, e4 = 0; std::size_t active = 0;
    for (const auto x : w.samples) if (std::norm(x) > 1e-20) { const double e = std::norm(x); e2 += e; e4 += e * e; ++active; }
    s.kurtosis = active ? e4 * static_cast<double>(active) / (e2 * e2) : 0;
    s.crest_db = 10 * std::log10(w.peak_power / std::max(w.mean_power, 1e-300));
    return s;
}

double uniform(std::mt19937_64& rng, double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}

std::vector<double> clock_search_grid(double relative_bound_ppm, double fine_rate_hz, double frame_seconds) {
    const double half_width = 3 * relative_bound_ppm;
    const double largest_step = 1e6 / (4 * fine_rate_hz * frame_seconds);
    const auto intervals = std::max<std::size_t>(2, static_cast<std::size_t>(
        std::ceil(2 * half_width / largest_step)));
    std::vector<double> values;
    values.reserve(intervals + 1);
    for (std::size_t i = 0; i <= intervals; ++i)
        values.push_back(-half_width + 2 * half_width * static_cast<double>(i) / static_cast<double>(intervals));
    return values;
}

Receiver receiver_from(const std::string& x) {
    if (x == "coherent") return Receiver::coherent;
    if (x == "differential") return Receiver::differential;
    return Receiver::amplitude_only;
}

std::vector<std::string> modes(const Options& o) {
    if (o.receiver == "all") return {"coherent", "differential", "amplitude-only"};
    return {o.receiver};
}

void run_one(const Case& c, const Options& o, unsigned trial) {
    const std::uint64_t run_seed = o.seed + 0x9e3779b97f4a7c15ULL * (trial + 1U) + c.number * 0x100000001b3ULL;
    std::mt19937_64 random(run_seed);
    auto key = key_for(o.seed, trial);
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    Format f;
    f.inner_rate_hz = c.rate;
    f.added_symbols = 10;
    f.added_interleave = o.interleave == "on";
    f.bits_per_frame = o.bits_per_frame;
    f.burst_wait_frames = o.burst_wait_frames;
    f.minimum_dwells = 20;
    f.minimum_bit_energy_seconds = c.number == 1 ? .35555555555555557 : 14.22222222222222;
    f.sample_rate = c.number == 1 ? 9'600 : 960;
    f.dwell_seconds = .4;
    f.encrypt = true;
    f.fhss = c.fake_fcc;
    f.quiet_seconds = c.fake_fcc ? .055 : 0;
    f.ramp_seconds = c.fake_fcc ? 2.0 / 3600.0 : .02;
    f.margin_seconds = .005;
    f.epoch = o.now ? static_cast<std::uint64_t>(std::max<std::int64_t>(0, now)) : o.epoch;
    constexpr auto epoch_max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - 3612);
    if (f.epoch < 6 || f.epoch > epoch_max) throw std::runtime_error("resolved stream epoch is outside safe signed search range");
    const auto g = geometry(f);
    std::vector<std::uint8_t> bits(o.bits);
    for (auto& b : bits) b = static_cast<std::uint8_t>(random() & 1U);
    const auto generation_start = std::chrono::steady_clock::now();
    const auto tx = transmit(bits, f, key);
    const double generation_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - generation_start).count();

    constexpr double tx_dbm = 36.020599913279625; // 4 W total = 36.0206 dBm
    constexpr double noise_density_dbm_hz = -164;
    const double cn0 = std::isnan(o.cn0_override) ? tx_dbm - c.pathloss_db - noise_density_dbm_hz : o.cn0_override;
    const double physical_snr = cn0 - 10 * std::log10(g.occupied_bandwidth_hz);
    const double lead = c.number == 1 ? 1 : 8;
    const double arrival_low = c.number == 1 ? -.055 : -7.035;
    const double arrival_high = c.number == 1 ? .125 : 7.035;

    SimulationConfig channel;
    channel.sample_rate = f.sample_rate;
    channel.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    channel.rf_hz = o.rf_hz;
    channel.nominal_conversion_if_hz = 1500;
    channel.seed = run_seed;
    channel.capture_lead_seconds = lead;
    // Case 2's ±7.035 s search window is wider than its roughly 14.4 s
    // transmit frame; capture both endpoints for every public arrival candidate.
    channel.capture_trail_seconds = c.number == 1 ? .5 : 16;
    channel.fhss = false; // Fake FCC is a transmit envelope/calendar; no RF retuning.
    channel.dwell_seconds = .4;
    channel.snr_db = physical_snr;
    channel.noise_enabled = true;
    channel.signal_enabled = o.control != "null";
    channel.spread_delay_ms = o.channel_none ? 0 : 1.5;
    channel.spread_doppler_hz = o.channel_none ? 0 : .3;
    channel.perturbation_slow_depth = o.channel_none ? 0 : .05;
    channel.perturbation_fast_depth = o.channel_none ? 0 : .05;
    channel.dropout_ms = o.dropout_ms;
    channel.dropout_start_ms = o.dropout_start_ms;
    channel.dropout_every_n_dwells = o.dropout_every;
    channel.slow_agc = o.agc;

    // The effective GUI bounds are relative endpoint bounds. Split them
    // symmetrically to draw independent endpoint errors without doubling them.
    const double per_end_base = c.relative_base_ppm / 2;
    const double per_end_rf = .0001 / 2;
    const double per_end_audio_lag_ms = uniform(random, 0, 70);
    const double per_rx_audio_lag_ms = uniform(random, 0, 70);
    const double tx_gps_ms = uniform(random, -10, 10);
    const double rx_gps_ms = uniform(random, -10, 10);
    const double broad_clock_start_seconds = c.fake_fcc ? 0 : uniform(random, -6.91, 6.91);
    channel.timing_offset_seconds = c.fake_fcc ? .035 +
        (per_end_audio_lag_ms - 35) * .001 - (per_rx_audio_lag_ms - 35) * .001 +
        (tx_gps_ms - rx_gps_ms) * .001 : broad_clock_start_seconds +
        (per_end_audio_lag_ms - per_rx_audio_lag_ms + tx_gps_ms - rx_gps_ms) * .001;
    channel.tx.audio_ppm = uniform(random, -per_end_base, per_end_base);
    channel.rx.audio_ppm = uniform(random, -per_end_base, per_end_base);
    channel.tx.rf_ppm = uniform(random, -per_end_rf, per_end_rf);
    channel.rx.rf_ppm = uniform(random, -per_end_rf, per_end_rf);
    // .5 degree/sqrt(s) at each end yields .707 degree/sqrt(s) relative.
    channel.tx.phase_noise_rad_per_sqrt_second = .5 * std::numbers::pi / 180;
    channel.rx.phase_noise_rad_per_sqrt_second = .5 * std::numbers::pi / 180;

    SearchPrior prior;
    prior.frequency_half_width_hz = o.rf_hz*.0001e-6*3 +
        1500*c.relative_base_ppm*1e-6*3 + .3;
    prior.frequency_step_hz = o.frequency_step_hz;
    prior.arrival_min_seconds = lead + arrival_low;
    prior.arrival_max_seconds = lead + arrival_high;
    prior.timing_step_seconds = 0;
    prior.clock_offsets_ppm = clock_search_grid(c.relative_base_ppm, g.fine_chip_rate_hz, g.frame_seconds);
    prior.epoch_offsets_seconds = {-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6};
    prior.segment_seconds = o.segment_seconds;
    prior.threads = o.threads;
    prior.memory_limit_bytes = o.memory_mib * 1024 * 1024;
    prior.maximum_hypotheses = o.maximum_hypotheses;
    prior.independent_hop_phase = false;
    prior.arithmetic = o.arithmetic == "automatic" ? Arithmetic::automatic :
        o.arithmetic == "fp32" ? Arithmetic::fp32 : Arithmetic::fp64;
    const auto receiver_epoch_offset = std::uniform_int_distribution<int>(-6, 6)(random);

    if (o.diagnostic_clean) {
        channel.noise_enabled = false;
        channel.spread_delay_ms = channel.spread_doppler_hz = 0;
        channel.perturbation_slow_depth = channel.perturbation_fast_depth = 0;
        channel.tx = {}; channel.rx = {};
        channel.timing_offset_seconds = 0;
        channel.capture_lead_seconds = lead;
        prior.frequency_half_width_hz = 0;
        prior.frequency_step_hz = 0;
        prior.arrival_min_seconds = prior.arrival_max_seconds = lead;
        prior.clock_offsets_ppm = {0};
        prior.epoch_offsets_seconds = {0};
        prior.segment_seconds = 0;
    }

    const auto channel_start = std::chrono::steady_clock::now();
    const auto capture = simulate(tx.samples, channel);
    const double channel_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - channel_start).count();
    Format rx_format = f;
    const int applied_epoch_offset = o.diagnostic_clean ? 0 : receiver_epoch_offset;
    rx_format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(f.epoch) + applied_epoch_offset);
    if (o.control == "wrong-epoch") rx_format.epoch += 3600; // outside the receiver's ±6 s epoch search
    Crypto rx_key = o.control == "wrong-key" ? key_for(o.seed + 31337, trial) : key;
    const auto sp = spectral(tx, f.sample_rate, g.occupied_bandwidth_hz);
    bool diagnostic_failed = false;

    for (const auto& mode : modes(o)) {
        const auto result = receive(capture.iq, bits.size(), rx_format, rx_key, prior, receiver_from(mode));
        std::size_t errors = 0;
        for (std::size_t i = 0; i < std::min(bits.size(), result.bits.size()); ++i) errors += bits[i] != result.bits[i];
        errors += bits.size() > result.bits.size() ? bits.size() - result.bits.size() : 0;
        const auto decode_ok = o.control == "signal" && result.accepted && result.complete_observation && result.bits == bits;
        const auto false_accept = o.control != "signal" && result.accepted;
        if (o.diagnostic_clean && o.control == "signal" && !decode_ok) diagnostic_failed = true;
        std::cout << std::setprecision(17)
            << "{\"schema\":\"triad-examples-v2\",\"case\":" << c.number
            << ",\"requested_case\":\"" << o.which << "\",\"expected_case_count\":" << (o.which == "all" ? 2 : 1)
            << ",\"trials_requested\":" << o.trials << ",\"requested_receiver\":\"" << o.receiver << "\""
            << ",\"trial\":" << trial << ",\"seed\":" << run_seed << ",\"seed_base\":" << o.seed
            << ",\"stream_epoch\":" << f.epoch << ",\"receiver_local_epoch\":" << rx_format.epoch
            << ",\"receiver_epoch_offset_seconds\":" << applied_epoch_offset
            << ",\"epoch_candidates\":" << prior.epoch_offsets_seconds.size()
            << ",\"now\":" << (o.now ? "true" : "false")
            << ",\"control\":\"" << o.control << "\",\"receiver\":\"" << mode << "\""
            << ",\"rate_hz\":" << c.rate << ",\"added_symbols\":10,\"interleave\":" << (f.added_interleave ? "true" : "false")
            << ",\"channel_none\":" << (o.channel_none ? "true" : "false")
            << ",\"encrypted\":true,\"bits_per_frame\":" << f.bits_per_frame
            << ",\"burst_wait_frames\":" << f.burst_wait_frames
            << ",\"bits\":" << bits.size() << ",\"source_bits\":\"" << bits_text(bits) << "\""
            << ",\"ordinary_planned_symbol_seconds\":" << (c.number == 1 ? .35555555555555557 : 14.22222222222222)
            << ",\"minimum_bit_energy_seconds\":" << f.minimum_bit_energy_seconds
            << ",\"payload_energy_seconds_per_bit\":" << g.payload_energy_seconds_per_bit
            << ",\"frame_seconds\":" << g.frame_seconds << ",\"dwells\":" << g.dwells_per_frame
            << ",\"generation_seconds\":" << generation_seconds << ",\"channel_seconds\":" << channel_seconds
            << ",\"format_quiet_seconds\":" << f.quiet_seconds << ",\"format_ramp_seconds\":" << f.ramp_seconds
            << ",\"format_margin_seconds\":" << f.margin_seconds << ",\"format_fake_fhss_envelope\":" << (f.fhss ? "true" : "false")
            << ",\"sample_rate\":" << f.sample_rate << ",\"fine_chip_rate_hz\":" << g.fine_chip_rate_hz
            << ",\"spacing_hz\":" << g.carrier_spacing_hz << ",\"occupied_bandwidth_hz\":" << g.occupied_bandwidth_hz
            << ",\"sample_count\":" << tx.samples.size() << ",\"mean_power\":" << tx.mean_power
            << ",\"peak_power\":" << tx.peak_power << ",\"bw99_hz\":" << sp.bandwidth99
            << ",\"outside_ideal_fraction\":" << sp.outside << ",\"crest_db\":" << sp.crest_db
            << ",\"kurtosis\":" << sp.kurtosis
            << ",\"tx_dbm_total\":" << tx_dbm << ",\"path_loss_db\":" << c.pathloss_db
            << ",\"noise_density_dbm_hz\":" << noise_density_dbm_hz
            << ",\"target_cn0_db_hz\":" << c.target_cn0
            << ",\"physical_cn0_db_hz\":" << cn0 << ",\"inband_snr_db\":" << physical_snr
            << ",\"channel_fhss_rf_retune\":false,\"fake_fcc_metadata_channels\":" << (c.fake_fcc ? 200 : 0)
            << ",\"fake_fcc_channel_spacing_hz\":" << (c.fake_fcc ? 4700 : 0)
            << ",\"fake_fcc_first_center_hz\":" << (c.fake_fcc ? 30001500 : 0)
            << ",\"fake_fcc_last_center_hz\":" << (c.fake_fcc ? 30936800 : 0)
            << ",\"default_audio_lag_tx_ms\":" << per_end_audio_lag_ms
            << ",\"default_audio_lag_rx_ms\":" << per_rx_audio_lag_ms
            << ",\"default_gps_error_tx_ms\":" << tx_gps_ms << ",\"default_gps_error_rx_ms\":" << rx_gps_ms
            << ",\"actual_arrival_seconds\":" << channel.timing_offset_seconds
            << ",\"broad_clock_start_error_seconds\":" << broad_clock_start_seconds
            << ",\"audio_timing_difference_seconds\":" << (per_end_audio_lag_ms - per_rx_audio_lag_ms) * .001
            << ",\"gps_timing_difference_seconds\":" << (tx_gps_ms - rx_gps_ms) * .001
            << ",\"capture_lead_seconds\":" << channel.capture_lead_seconds
            << ",\"capture_trail_seconds\":" << channel.capture_trail_seconds
            << ",\"public_arrival_min_capture_seconds\":" << prior.arrival_min_seconds
            << ",\"public_arrival_max_capture_seconds\":" << prior.arrival_max_seconds
            << ",\"base_relative_bound_ppm\":" << c.relative_base_ppm
            << ",\"rf_relative_bound_ppm\":0.0001,\"audio_clock_tx_ppm\":" << channel.tx.audio_ppm
            << ",\"audio_clock_rx_ppm\":" << channel.rx.audio_ppm
            << ",\"rf_tx_ppm\":" << channel.tx.rf_ppm << ",\"rf_rx_ppm\":" << channel.rx.rf_ppm
            << ",\"rf_hz\":" << channel.rf_hz << ",\"if_hz\":1500,\"realized_cfo_bound_hz\":" << capture.expected_max_cfo_hz
            << ",\"search_frequency_half_width_hz\":" << prior.frequency_half_width_hz
            << ",\"search_frequency_step_hz\":" << prior.frequency_step_hz
            << ",\"search_clock_offsets_ppm\":[";
        for (std::size_t i = 0; i < prior.clock_offsets_ppm.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << prior.clock_offsets_ppm[i];
        }
        std::cout << "]"
            << ",\"arithmetic_requested\":\"" << o.arithmetic << "\",\"arithmetic_used\":\""
            << triad::arithmetic_name(result.arithmetic_used) << "\""
            << ",\"epoch_offsets_seconds\":" << prior.epoch_offsets_seconds.size()
            << ",\"search_segment_seconds\":" << prior.segment_seconds
            << ",\"search_minimum_score\":" << prior.minimum_score
            << ",\"search_minimum_margin\":" << prior.minimum_margin
            << ",\"maximum_hypotheses\":" << prior.maximum_hypotheses
            << ",\"requested_threads\":" << prior.threads << ",\"workspace_limit_bytes\":" << prior.memory_limit_bytes
            << ",\"dropout_ms\":" << channel.dropout_ms << ",\"dropout_start_ms\":" << channel.dropout_start_ms
            << ",\"dropout_every_n_dwells\":" << channel.dropout_every_n_dwells
            << ",\"slow_agc\":" << (channel.slow_agc ? "true" : "false")
            << ",\"noise_enabled\":" << (channel.noise_enabled ? "true" : "false")
            << ",\"signal_enabled\":" << (channel.signal_enabled ? "true" : "false")
            << ",\"spread_delay_ms\":" << channel.spread_delay_ms
            << ",\"spread_doppler_hz\":" << channel.spread_doppler_hz
            << ",\"perturbation_slow_depth\":" << channel.perturbation_slow_depth
            << ",\"perturbation_fast_depth\":" << channel.perturbation_fast_depth
            << ",\"tx_phase_noise_rad_sqrt_second\":" << channel.tx.phase_noise_rad_per_sqrt_second
            << ",\"rx_phase_noise_rad_sqrt_second\":" << channel.rx.phase_noise_rad_per_sqrt_second
            << ",\"noise_power\":" << capture.noise_power << ",\"sample_noise_variance\":" << capture.noise_power
            << ",\"observed_power\":" << capture.observed_power << ",\"capture_samples\":" << capture.iq.size()
            << ",\"signal_erased_samples\":" << capture.erased_samples
            << ",\"rx_bits\":\"" << bits_text(result.bits) << "\",\"bit_errors\":" << errors
            << ",\"accepted\":" << (result.accepted ? "true" : "false")
            << ",\"complete_observation\":" << (result.complete_observation ? "true" : "false")
            << ",\"decode_ok\":" << (decode_ok ? "true" : "false")
            << ",\"false_accept\":" << (false_accept ? "true" : "false")
            << ",\"score\":" << result.score << ",\"margin\":" << result.margin
            << ",\"bit_scores\":[";
        for (std::size_t i = 0; i < result.bit_scores.size(); ++i) { if (i) std::cout << ','; std::cout << result.bit_scores[i]; }
        std::cout << "],\"bit_margins\":[";
        for (std::size_t i = 0; i < result.bit_margins.size(); ++i) { if (i) std::cout << ','; std::cout << result.bit_margins[i]; }
        std::cout << "]"
            << ",\"frequency_hz\":" << result.frequency_hz << ",\"timing_seconds\":" << result.timing_seconds
            << ",\"clock_ppm\":" << result.clock_ppm << ",\"epoch_offset_seconds\":" << result.epoch_offset_seconds
            << ",\"coarse_hypotheses\":" << result.coarse_hypotheses << ",\"refined_hypotheses\":" << result.refined_hypotheses
            << ",\"total_hypotheses\":" << result.hypotheses
            << ",\"fine_likelihood_exhaustive\":" << (result.fine_likelihood_exhaustive ? "true" : "false")
            << ",\"frequency_banks\":" << result.frequency_banks << ",\"fft_count\":" << result.fft_count
            << ",\"projection_values\":" << result.projection_values << ",\"working_bytes\":" << result.working_bytes
            << ",\"retained_samples\":" << result.retained_samples << ",\"threads_used\":" << result.threads_used
            << ",\"cpu_seconds\":" << result.cpu_seconds << ",\"wall_seconds\":" << result.wall_seconds
            << ",\"diagnostic_clean\":" << (o.diagnostic_clean ? "true" : "false") << "}\n";
    }
    if (diagnostic_failed) throw std::runtime_error("diagnostic-clean signal failed exact receiver recovery");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto o = parse(argc, argv);
        const unsigned first = o.which == "2" ? 2 : 1;
        const unsigned last = o.which == "1" ? 1 : 2;
        for (unsigned n = first; n <= last; ++n) {
            for (unsigned trial = 0; trial < o.trials; ++trial) run_one(get_case(n), o, trial);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "triad_examples: " << e.what() << '\n';
        return 2;
    }
}
