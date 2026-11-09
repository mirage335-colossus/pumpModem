#include "triad_fixture_key.hpp"
// Bounded source/basis bridge for the canonical Robust backend.
// It contains no receiver, campaign runner or complete-capture export path.
#include "datapump/helper_carrier.hpp"
#include "datapump/compression.hpp"
#include "datapump/received_text.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/triad_helper_burst.hpp"
#include "datapump/triad_transmission_keys.hpp"
#include "datapump/tuning.hpp"
#include "diagnostic_iq_noise_options.hpp"

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
#include <memory>
#include <map>
#include <sstream>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr std::uint32_t default_sample_rate = 18000;
constexpr double data_start_seconds = 38;
constexpr double thermal_n0_dbm_hz = -164;
constexpr double spectral_skirt_allowance_hz = 200;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t stream_chunk_samples = 4096;
constexpr std::size_t maximum_sequence_bits = 256;
constexpr std::size_t maximum_short_source_bytes = 16;
constexpr std::size_t maximum_short_bits = 13 * maximum_short_source_bytes;
struct Options {
    double added_amplitude_depth = .25;
    std::string scenario = "hf", control = "selected", shaping = "auto";
    std::string power_reference = "bounded-tiles";
    bool stream_server = false, explicit_key_seed = false, explicit_epoch = false, now = false;
    std::string stream_kind = "channel";
    std::filesystem::path key_file;
    std::optional<std::string> key_name;
    std::optional<std::filesystem::path> key_pad;
    std::string channel_evolution = "legacy";
    std::optional<double> random_doppler_width_hz, delay_excursion_ms, delay_knot_seconds;
    std::optional<std::string> base_oscillator, shift_oscillator;
    std::optional<std::string> spread_preset, selective_perturbation;
    std::string timing_policy = "case-default";
    std::optional<double> rf_hz, spread_margin, selective_modulation_rate_hz;
    std::optional<double> data_power_w, helper_power_w, path_loss_db;
    std::optional<std::string> short_message;
    std::optional<std::string> data_bits, short_encode, short_decode;
    std::uint64_t seed = 1, key_seed = 72001, epoch = 1793850000, symbol_ordinal = 0;
    std::optional<std::uint64_t> burst_wait_tiles;
    unsigned whole_bit_count = 1, whole_bit_wait_count = 0;
    std::optional<unsigned> observation_slots;
    bool whole_bit_calendar = false;
    bool explicit_whole_bit_count = false, explicit_bit = false;
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
    bool data = true, helper = true, encrypt = true, added_interleave = true, thermal_noise = true, explicit_data_if = false;
    bool oscillator_effects = true;
    datapump::triad::cli::DiagnosticIqNoiseOptions diagnostic_iq_noise;
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
            std::cout << "triad_helper_export --case hf|eme --stream-server | --plan-only\n"
                "  [--seed N] [--key-seed N] [--epoch UTC_SECONDS] [--bit 0|1]\n"
                "  [--diagnostic-iq-noise-bits N | --diagnostic-iq-noise-obscured-bits N | --diagnostic-iq-noise-db D]\n"
                "  [--diagnostic-iq-noise-watts W] (relative to strongest active DATA or Helper watts)\n"
                "  [--diagnostic-iq-noise-reference-watts W --diagnostic-iq-noise-reference-rms RMS]\n"
                "  Diagnostic noise is confined to sampled complex-IQ Nyquist; it is not full-rate DAC/audio noise.\n"
                "  [--seconds 16] [--rate HZ] [--added-symbols N] [--burst-wait-tiles N]\n"
                "  [--added-amplitude-depth D (0<D<1)]\n"
                "  [--whole-bit-count 1..16] [--whole-bit-wait-count 0..1000]\n"
                "  [--data-bits BITSTRING] (1..256 DATA bits in one continuous native channel)\n"
                "  [--observation-slots 1..1024] (public receive horizon; requires a whole-bit calendar)\n"
                "  [--fhss on|off] [--shaping auto|legacy (default: auto)] [--shaping-tail-fraction F]\n"
                "  [--data-if HZ] [--sample-rate INTEGER_HZ] [--cn0 DB_HZ] [--plan-only]\n"
                "  --plan-only checks long integer geometry without generating waveform samples or templates.\n"
                "  Source and basis requests retain bounded workspace independently of duration.\n"
                "  [--power-reference bounded-tiles] [--power-plan-only]\n"
                "  [--stream-server --stream-kind emitted|channel --key-file KEYFILE [--key-name NAME] [--pad PATH] | --key-seed TEST_SEED]\n"
                "  [--now] Streaming defaults to the current epoch and bounded-tiles calibration.\n"
                "  [--base-oscillator ID] [--shift-oscillator ID] [--ramp-seconds POSITIVE]\n"
                "  [--oscillator-effects on|off] (off is an explicit diagnostic control)\n"
                "  [--rf-hz HZ] [--spread-preset ID] [--margin FACTOR]\n"
                "  [--selective-perturbation off|slow-N|fast-N|each-N]\n"
                "  [--selective-modulation-rate-hz HZ] [--timing-policy case-default|bounded|broad]\n"
                "  [--channel-evolution legacy|stochastic] [--random-doppler-width-hz B]\n"
                "  [--delay-excursion-ms A] [--delay-knot-seconds T]\n"
                "  [--data-power-w W] [--helper-power-w W] [--path-loss-db DB]\n"
                "  [--encrypt on|off] [--added-interleave on|off]\n"
                "  [--thermal-noise on|off] (receiver/channel thermal noise; default on)\n"
                "  [--short-message TEXT] (plan metadata only; at most256 source bytes)\n"
                "  --short-encode TEXT | --short-decode BITSTRING (standalone codec,16 bytes/208 bits maximum)\n"
                "  [--receiver-tail-seconds 0..1]\n"
                "  [--control selected|flat] [--data on|off] [--helper on|off]\n"
                "  [--arrival-seconds T] [--symbol-ordinal N]\n"
                "Default 18000 Hz complex samples; sample rate must be a multiple of 5.\n"
                "Default DATA IF 1725 Hz HF, 4000 Hz legacy EME.\n"
                "Physical C/N0 follows powers/loss/N0 unless\n"
                "overridden. Templates contain all 13 public epoch alternatives and both bits.\n";
            std::exit(0);
        } else if (arg == "--case") o.scenario = next();
        else if (arg == "--seed") o.seed = integer(next(), "seed");
        else if (arg == "--key-seed") { o.key_seed = integer(next(), "key seed"); o.explicit_key_seed = true; }
        else if (arg == "--key-file") o.key_file = std::string(next());
        else if (arg == "--key-name") o.key_name = std::string(next());
        else if (arg == "--pad") o.key_pad = std::filesystem::path(std::string(next()));
        else if (arg == "--stream-server") o.stream_server = true;
        else if (arg == "--stream-kind") o.stream_kind = next();
        else if (arg == "--now") o.now = true;
        else if (arg == "--diagnostic-iq-noise-bits" || arg == "--diagnostic-iq-noise-obscured-bits" ||
                 arg == "--diagnostic-iq-noise-db" || arg == "--diagnostic-iq-noise-watts" ||
                 arg == "--diagnostic-iq-noise-reference-watts" || arg == "--diagnostic-iq-noise-reference-rms")
            o.diagnostic_iq_noise.consume(arg, next());
        else if (arg.starts_with("--background-noise-"))
            throw std::invalid_argument("--background-noise-* denotes shared full-rate audio noise and is not a triad complex-I/Q option; use --diagnostic-iq-noise-* only for the narrower diagnostic (not a full-rate DAC/audio floor)");
        else if (arg == "--epoch") { o.epoch = integer(next(), "epoch"); o.explicit_epoch = true; }
        else if (arg == "--symbol-ordinal") o.symbol_ordinal = integer(next(), "symbol ordinal");
        else if (arg == "--bit") {
            const auto bit = integer(next(), "source bit");
            if (bit > 1) throw std::invalid_argument("source bit must be 0 or 1");
            o.bit = static_cast<unsigned>(bit);
            o.explicit_bit = true;
        } else if (arg == "--seconds") o.seconds = real(next(), "duration");
        else if (arg == "--rate") o.inner_rate_hz = real(next(), "inner rate");
        else if (arg == "--added-amplitude-depth") {
            o.added_amplitude_depth = real(next(), "Added Symbols amplitude depth");
            if (!(o.added_amplitude_depth > 0 && o.added_amplitude_depth < 1))
                throw std::invalid_argument("Added Symbols amplitude depth must be in (0,1)");
        }
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
        } else if (arg == "--whole-bit-count" || arg == "--whole-bit-wait-count") {
            const auto value = integer(next(), "whole-bit calendar control");
            if (value > 1000) throw std::invalid_argument("whole-bit calendar control exceeds its bounded fixture range");
            if (arg == "--whole-bit-count") { o.whole_bit_count = static_cast<unsigned>(value); o.whole_bit_calendar = true; o.explicit_whole_bit_count = true; }
            else o.whole_bit_wait_count = static_cast<unsigned>(value);
        } else if (arg == "--observation-slots") {
            const auto value = integer(next(), "public observation slots");
            if (value < 1 || value > 1024)
                throw std::invalid_argument("public observation slots must be in[1,1024]");
            o.observation_slots = static_cast<unsigned>(value);
        } else if (arg == "--fhss") o.fhss = enabled(next());
        else if (arg == "--shaping") o.shaping = next();
        else if (arg == "--shaping-tail-fraction")
            o.shaping_tail_fraction = real(next(), "spectral tail fraction");
        else if (arg == "--data-if") { o.data_if_hz = real(next(), "DATA IF"); o.explicit_data_if = true; }
        else if (arg == "--base-oscillator") o.base_oscillator = std::string(next());
        else if (arg == "--shift-oscillator") o.shift_oscillator = std::string(next());
        else if (arg == "--oscillator-effects") o.oscillator_effects = enabled(next());
        else if (arg == "--rf-hz") o.rf_hz = real(next(), "RF shift");
        else if (arg == "--spread-preset") o.spread_preset = std::string(next());
        else if (arg == "--margin") o.spread_margin = real(next(), "spread/search margin");
        else if (arg == "--selective-perturbation") o.selective_perturbation = std::string(next());
        else if (arg == "--selective-modulation-rate-hz")
            o.selective_modulation_rate_hz = real(next(), "selective modulation rate");
        else if (arg == "--channel-evolution") o.channel_evolution = next();
        else if (arg == "--random-doppler-width-hz")
            o.random_doppler_width_hz = real(next(), "random Doppler width");
        else if (arg == "--delay-excursion-ms") o.delay_excursion_ms = real(next(), "delay excursion");
        else if (arg == "--delay-knot-seconds") o.delay_knot_seconds = real(next(), "delay knot interval");
        else if (arg == "--timing-policy") o.timing_policy = next();
        else if (arg == "--data-power-w") o.data_power_w = real(next(), "DATA power");
        else if (arg == "--helper-power-w") o.helper_power_w = real(next(), "Helper power");
        else if (arg == "--path-loss-db") o.path_loss_db = real(next(), "path loss");
        else if (arg == "--short-message") o.short_message = std::string(next());
        else if (arg == "--data-bits") {
            const auto value = next();
            if (value.empty() || value.size() > maximum_sequence_bits ||
                value.find_first_not_of("01") != std::string_view::npos)
                throw std::invalid_argument("DATA sequence must contain1..256 binary bits");
            o.data_bits = std::string(value);
        }
        else if (arg == "--short-encode") {
            const auto value = next();
            if (value.size() > maximum_short_source_bytes)
                throw std::invalid_argument("short encoder input exceeds16 source bytes");
            o.short_encode = std::string(value);
        }
        else if (arg == "--short-decode") {
            const auto value = next();
            if (value.size() > maximum_short_bits || value.find_first_not_of("01") != std::string_view::npos)
                throw std::invalid_argument("short decoder requires at most208 binary bits");
            o.short_decode = std::string(value);
        }
        else if (arg == "--ramp-seconds") o.ramp_seconds = real(next(), "ramp duration");
        else if (arg == "--receiver-tail-seconds") o.receiver_tail_seconds = real(next(), "receiver tail duration");
        else if (arg == "--cn0") o.cn0 = real(next(), "C/N0");
        else if (arg == "--arrival-seconds") o.arrival = real(next(), "arrival");
        else if (arg == "--control") o.control = next();
        else if (arg == "--data") o.data = enabled(next());
        else if (arg == "--helper") o.helper = enabled(next());
        else if (arg == "--encrypt") o.encrypt = enabled(next());
        else if (arg == "--added-interleave") o.added_interleave = enabled(next());
        else if (arg == "--thermal-noise") o.thermal_noise = enabled(next());
        else if (arg == "--plan-only") o.plan_only = true;
        else if (arg == "--power-plan-only") o.power_plan_only = true;
        else if (arg == "--power-reference") o.power_reference = next();
        else throw std::invalid_argument("unknown option: " + std::string(arg));
    }
    if (o.short_encode || o.short_decode) {
        if (argc != 3 || (o.short_encode && o.short_decode))
            throw std::invalid_argument("short codec operation must be used alone with exactly one input");
        return o;
    }
    if (o.data_bits) {
        if (o.explicit_bit || o.short_message)
            throw std::invalid_argument("--data-bits cannot be combined with --bit or --short-message metadata");
        if (o.explicit_whole_bit_count && o.whole_bit_count != o.data_bits->size())
            throw std::invalid_argument("explicit whole-bit count differs from DATA sequence");
        o.whole_bit_count = static_cast<unsigned>(o.data_bits->size());
        o.whole_bit_calendar = true;
    }
    if (o.now && o.explicit_epoch) throw std::invalid_argument("--now and --epoch are mutually exclusive");
    if (o.now || (o.stream_server && !o.explicit_epoch)) {
        const auto now = std::time(nullptr);
        if (now < 0) throw std::runtime_error("system clock is unavailable");
        o.epoch = static_cast<std::uint64_t>(now);
    }
    if (o.stream_kind != "emitted" && o.stream_kind != "channel")
        throw std::invalid_argument("stream kind must be emitted or channel");
    if (o.stream_server && (o.plan_only || o.power_plan_only))
        throw std::invalid_argument("stream service and plan-only output are mutually exclusive");
    if (o.stream_server && o.key_file.empty() && !o.explicit_key_seed)
        throw std::invalid_argument("stream server requires --key-file or explicit --key-seed for a reproducible test fixture");
    if (!o.key_file.empty() && o.explicit_key_seed)
        throw std::invalid_argument("--key-file and --key-seed are mutually exclusive");
    if (o.key_file.empty() && (o.key_name || o.key_pad))
        throw std::invalid_argument("--key-name and --pad require --key-file");
    if (o.scenario != "hf" && o.scenario != "eme") throw std::invalid_argument("case must be hf or eme");
    if (!o.explicit_data_if && o.scenario == "eme") o.data_if_hz = 4000;
    if (o.ramp_seconds && *o.ramp_seconds <= 0)
        throw std::invalid_argument("ramp duration must be positive");
    if (o.shaping != "legacy" && o.shaping != "auto")
        throw std::invalid_argument("shaping must be legacy or auto");
    if (o.power_reference != "bounded-tiles")
        throw std::invalid_argument("power reference must be bounded-tiles");
    if (o.plan_only && o.power_plan_only)
        throw std::invalid_argument("--plan-only and --power-plan-only are mutually exclusive");
    if (o.diagnostic_iq_noise.enabled() && (o.plan_only || o.power_plan_only))
        throw std::invalid_argument("diagnostic-IQ noise settings apply to sampled exports, not plan-only modes");
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
    if (!o.data_bits && (o.whole_bit_count < 1 || o.whole_bit_count > 16))
        throw std::invalid_argument("whole-bit count must be in [1,16]");
    if (o.symbol_ordinal > UINT64_MAX - (o.whole_bit_count - 1))
        throw std::invalid_argument("whole-bit symbol ordinal overflows");
    if (o.observation_slots && (!o.whole_bit_calendar || *o.observation_slots < o.whole_bit_count))
        throw std::invalid_argument("public observation slots require a whole-bit calendar and must contain its transmission");
    if (o.observation_slots && o.symbol_ordinal > UINT64_MAX - (*o.observation_slots - 1))
        throw std::invalid_argument("public observation symbol ordinal overflows");
    if (o.whole_bit_wait_count && !o.whole_bit_calendar)
        throw std::invalid_argument("whole-bit wait count requires explicit --whole-bit-count");
    if (o.whole_bit_calendar && o.plan_only)
        throw std::invalid_argument("whole-bit calendar requires an emitted fixture, not shaping-only planning");
    if (o.shaping_tail_fraction && (*o.shaping_tail_fraction <= 0 || *o.shaping_tail_fraction > .005))
        throw std::invalid_argument("spectral tail fraction must be in (0,0.005]");
    if (o.control != "selected" && o.control != "flat") throw std::invalid_argument("control must be selected or flat");
    if (o.rf_hz && *o.rf_hz < 0) throw std::invalid_argument("RF shift must be nonnegative");
    if (o.spread_margin && (*o.spread_margin <= 0 || *o.spread_margin > 1000))
        throw std::invalid_argument("spread/search margin must be in (0,1000]");
    if (o.selective_modulation_rate_hz && *o.selective_modulation_rate_hz < 0)
        throw std::invalid_argument("selective modulation rate must be nonnegative");
    if (o.channel_evolution != "legacy" && o.channel_evolution != "stochastic")
        throw std::invalid_argument("channel evolution must be legacy or stochastic");
    if (o.channel_evolution == "stochastic" && !o.random_doppler_width_hz)
        throw std::invalid_argument("stochastic evolution requires explicit --random-doppler-width-hz (B=2sigma; zero disables ray fading)");
    if (o.channel_evolution == "legacy" &&
        (o.random_doppler_width_hz || o.delay_excursion_ms || o.delay_knot_seconds))
        throw std::invalid_argument("random Doppler and delay motion controls require --channel-evolution stochastic");
    if (o.random_doppler_width_hz && *o.random_doppler_width_hz < 0)
        throw std::invalid_argument("random Doppler width must be nonnegative");
    if (o.delay_excursion_ms && *o.delay_excursion_ms < 0)
        throw std::invalid_argument("delay excursion must be nonnegative");
    if (o.delay_knot_seconds && *o.delay_knot_seconds <= 0)
        throw std::invalid_argument("delay knot interval must be positive");
    if (o.delay_excursion_ms.value_or(0) > 0 && !o.delay_knot_seconds)
        throw std::invalid_argument("nonzero delay excursion requires explicit --delay-knot-seconds");
    if (o.data_power_w && *o.data_power_w <= 0) throw std::invalid_argument("DATA power must be positive");
    if (o.helper_power_w && (*o.helper_power_w < 0 || (*o.helper_power_w == 0 && o.helper)))
        throw std::invalid_argument("Helper power must be positive when Helper is enabled; zero requires --helper off");
    if (o.path_loss_db && *o.path_loss_db < 0) throw std::invalid_argument("path loss must be nonnegative");
    if (o.short_message && o.short_message->size() > 256)
        throw std::invalid_argument("short message must contain at most256 source bytes");
    if (o.timing_policy != "case-default" && o.timing_policy != "bounded" && o.timing_policy != "broad")
        throw std::invalid_argument("timing policy must be case-default, bounded or broad");
    if (!o.plan_only && !o.power_plan_only && !o.stream_server)
        throw std::invalid_argument("--stream-server or --plan-only is required");
    if (!std::isfinite(o.seconds) || o.seconds <= 0)
        throw std::invalid_argument("seconds must be finite and positive");
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
    return datapump::triad::fixture::key(material);
}

Crypto selected_key(const Options& o) {
    if (o.key_file.empty()) return fixture_key(o.key_seed);
    const auto entries=load_keyring(o.key_file,o.key_pad);
    if (o.key_name) {
        const auto found=std::find_if(entries.begin(),entries.end(),[&](const KeyEntry& entry){return entry.name==*o.key_name;});
        if(found==entries.end())throw std::invalid_argument("named key is not present in keyfile");
        return found->key;
    }
    if(entries.size()!=1)throw std::invalid_argument("--key-name is required for a keyfile with multiple entries");
    return entries.front().key;
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
    if (o.path_loss_db) result.loss_db = *o.path_loss_db;
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
    f.added_amplitude_depth = o.added_amplitude_depth;
    const bool eme = o.scenario == "eme";
    f.inner_rate_hz = o.inner_rate_hz.value_or(eme ? 360 : 36);
    f.added_symbols = o.added_symbols.value_or(eme ? 1 : 10);
    f.added_interleave = o.added_interleave;
    f.sample_rate = o.sample_rate;
    f.bit_seconds = o.seconds;
    f.fhss = o.fhss.value_or(eme);
    f.fhss_center_aligned = f.fhss;
    f.quiet_seconds = f.fhss ? .055 : 0;
    f.ramp_seconds = o.ramp_seconds.value_or(eme ? 2. / 3600 : .02);
    f.margin_seconds = .005;
    f.burst_wait_tiles = static_cast<unsigned>(o.burst_wait_tiles.value_or(0));
    f.encrypt = o.encrypt;
    f.epoch = o.epoch;
    f.symbol_ordinal = o.symbol_ordinal;
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
    double requested_base_pair_ppm = 0, requested_rf_pair_ppm = 0;
    double selected_delay_ms = 0, selected_doppler_hz = 0;
    double delay_ms = 0, doppler_hz = 0, slow_depth = 0, fast_depth = 0, modulation_rate_hz = 0;
    double physical_cn0 = 0, data_cn0 = 0, effective_helper_w = 0;
    bool stochastic = false;
    double random_doppler_width_hz = 0, delay_excursion_seconds = 0, delay_knot_seconds = 1;
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
    r.requested_base_pair_ppm = r.base.clock_error_ppm;
    r.requested_rf_pair_ppm = r.rf_pair_ppm;
    if (!o.oscillator_effects) {
        r.base.clock_error_ppm = r.shift.clock_error_ppm = 0;
        r.rf_pair_ppm = r.phase_diffusion = 0;
    }
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
        r.stochastic = o.channel_evolution == "stochastic";
        if (r.stochastic) {
            r.random_doppler_width_hz = o.random_doppler_width_hz.value_or(0);
            r.delay_excursion_seconds = o.delay_excursion_ms.value_or(0) * .001;
            r.delay_knot_seconds = o.delay_knot_seconds.value_or(1);
            if (r.delay_excursion_seconds > r.delay_ms * .001)
                throw std::invalid_argument("delay excursion exceeds nominal excess delay and would permit negative delay");
            if (3.75 * r.delay_excursion_seconds / r.delay_knot_seconds >= 1)
                throw std::invalid_argument("delay motion must have absolute slope below one");
        }
    }
    r.physical_cn0 = 10 * std::log10(physical.data_w * 1000) - physical.loss_db - thermal_n0_dbm_hz;
    r.data_cn0 = o.cn0.value_or(r.physical_cn0);
    r.effective_helper_w = o.helper ? helper::effective_helper_power(helper_options_for(physical)) : 0.;
    r.helper_power_limited = o.helper && physical.helper_w > physical.data_w * 1e9;
    if (!std::isfinite(r.physical_cn0) || !std::isfinite(r.effective_helper_w) || r.effective_helper_w < 0)
        throw std::invalid_argument("requested powers exceed the representable production Helper power range");
    return r;
}

void evolution_json(std::ostream& out, const Options& o, const ResolvedPhysics& r,
                    double propagation_reference_hz) {
    const auto slope = 3.75 * r.delay_excursion_seconds / r.delay_knot_seconds;
    const auto selective_width = r.stochastic && (r.slow_depth > 0 || r.fast_depth > 0) ?
        r.modulation_rate_hz * (r.fast_depth > 0 ? 1. : .1) : 0.;
    out << "\"requested_channel_evolution\":"; json_string(out, o.channel_evolution);
    out << ",\"channel_evolution\":"; json_string(out, r.stochastic ? "fixed-time-gaussian-v1" : "legacy");
    out << ",\"random_doppler_width_hz\":" << r.random_doppler_width_hz
        << ",\"random_doppler_width_definition\":\"B=2sigma of each ray Gaussian spectrum; independent of ray center separation\""
        << ",\"selective_coefficient_law\":"; json_string(out, r.stochastic ? "gaussian-erf-v1" : "sinusoidal");
    out << ",\"selective_depth_definition\":";
    json_string(out, r.stochastic ? "bounded peak excursion; each component has uniform marginal and RMS depth/sqrt(3)" :
                                  "bounded sinusoidal peak excursion; component RMS depth/sqrt(2)");
    out << ",\"delay_excursion_seconds\":" << r.delay_excursion_seconds
        << ",\"delay_knot_seconds\":" << r.delay_knot_seconds
        << ",\"maximum_excess_delay_seconds\":" << r.delay_ms * .001 + r.delay_excursion_seconds
        << ",\"maximum_delay_slope\":" << slope
        << ",\"nominal_delay_motion_phase_bound_hz\":" << std::abs(propagation_reference_hz) * slope
        << ",\"delay_motion_doppler_bound_hz\":" << (std::abs(propagation_reference_hz) + o.sample_rate / 2.) * slope
        << ",\"delay_motion_doppler_bound_scope\":\"nominal carrier plus entire source Nyquist interval; includes envelope time-warp as well as propagation phase\""
        << ",\"nominal_propagation_reference_hz\":" << propagation_reference_hz
        << ",\"random_ray_total_bandwidth_allowance_hz\":" << 6 * r.random_doppler_width_hz
        << ",\"random_selective_total_bandwidth_allowance_hz\":" << 20 * selective_width
        << ",\"random_sampling_policy\":\"fixed physical-time knots at16*B; variance-normalized cubic interpolation; stationary indexed innovations independent of receiver sample rate and chunk boundaries\""
        << ",\"random_spectral_allowance_scope\":\"engineering spectral-tail allowance, not compact support or a physical measurement; delay-motion Doppler is additional and is counted once\"";
}

void physics_json(std::ostream& out, const Options& o, const Case& physical,
                  const ResolvedPhysics& r, double data_if_hz) {
    const auto denominator = 1 - r.base.clock_error_ppm * .5e-6;
    out << "{\"rf_hz\":" << physical.rf_hz << ",\"data_if_hz\":" << data_if_hz
        << ",\"base_oscillator\":"; json_string(out, r.base.id);
    out << ",\"shift_oscillator\":"; json_string(out, r.shift_id);
    out << ",\"oscillator_effects\":" << boolean(o.oscillator_effects)
        << ",\"requested_base_relative_pair_ppm_bound\":" << r.requested_base_pair_ppm
        << ",\"requested_rf_relative_pair_ppm_bound\":" << r.requested_rf_pair_ppm;
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
        << ',';
    evolution_json(out, o, r, physical.rf_hz + data_if_hz);
    out << ",\"timing_policy\":"; json_string(out, o.timing_policy);
    out << ",\"arrival_prior_seconds\":[" << physical.arrival_low << ',' << physical.arrival_high << ']'
        << ",\"capture_lead_seconds\":" << physical.capture_lead << ",\"capture_trail_seconds\":" << physical.capture_trail
        << ",\"message_epoch_count\":13,\"path_loss_db\":" << physical.loss_db
        << ",\"noise_floor_dbm_hz\":" << thermal_n0_dbm_hz << ",\"data_w\":" << physical.data_w
        << ",\"requested_helper_w\":" << physical.helper_w << ",\"effective_helper_w\":" << r.effective_helper_w
        << ",\"helper_ratio_limit_db\":" << helper_options_for(physical).ratio_limit_db
        << ",\"helper_power_limited\":" << boolean(r.helper_power_limited)
        << ",\"physical_data_cn0_db_hz\":" << r.physical_cn0 << ",\"data_cn0_db_hz\":" << r.data_cn0
        << ",\"thermal_noise_enabled\":" << boolean(o.thermal_noise)
        << ",\"nominal_noise_variance\":" << o.sample_rate / std::pow(10., r.data_cn0 / 10)
        << ",\"effective_helper_cn0_db_hz\":";
    if (r.effective_helper_w > 0) out << r.data_cn0 + 10 * std::log10(r.effective_helper_w / physical.data_w);
    else out << "null";
    out
        << ",\"cn0_scope\":\"nominal active DATA reference and effective production Helper/DATA power ratio; actual finite-calendar component energies are reported separately\"}";
}

void print_plan_json(const Options& o, double data_if_hz,
                     const triad::v3::ShapingPlan& selected,
                     const helper::Options& helper_options, const Case& physical,
                     const ResolvedPhysics& physics) {
    const auto& f = selected.format;
    const auto& g = selected.geometry;
    const auto sample_rate = f.sample_rate;
    // Geometry has already checked full elapsed tile/dwell coordinates using
    // bounded uint64 calculations. Keep the published counts exact and O(1).
    const auto physical_tiles = g.tile_count + (g.tile_count - 1) * f.burst_wait_tiles;
    const auto tile_dwells = static_cast<std::uint64_t>(std::llround(g.tile_seconds / f.dwell_seconds));
    const auto active_dwells = g.tile_count * tile_dwells;
    const auto active_seconds = static_cast<double>(g.tile_count * static_cast<long double>(g.tile_seconds));
    const auto nominal = g.occupied_bandwidth_hz;
    const auto gate = std::max(0., selected.spectrum.placement_bandwidth_hz - nominal);
    const auto minimum_data_if = std::max(1725., helper::auxiliary_frequency_hz + 450 +
                                                     selected.spectrum.placement_bandwidth_hz / 2);
    std::optional<Bytes> short_bits;
    if (o.short_message) {
        Bytes source;
        source.reserve(o.short_message->size());
        for (const unsigned char byte : *o.short_message) source.push_back(byte);
        short_bits = compression::encode_short_bits(source);
    }
    const auto bit_samples_real = static_cast<long double>(g.bit_seconds) * sample_rate;
    if (bit_samples_real < 1 || bit_samples_real > UINT64_MAX)
        throw std::runtime_error("plan transmit calendar extent overflow");
    const auto bit_samples = static_cast<std::uint64_t>(std::llround(bit_samples_real));
    HelperBurstPlan transmit_calendar({sample_rate, bit_samples, g.source_samples,
        g.calendar_origin_sample, o.whole_bit_wait_count, o.helper});
    const HelperBurstCoordinate calendar_origin{f.epoch, 0};
    const auto initial_lead = o.helper ? static_cast<std::uint64_t>(data_start_seconds * sample_rate) : 0;
    const auto first_slot = transmit_calendar.first_slot(transmit_calendar.add(calendar_origin, initial_lead));
    const auto single_bit_seconds = static_cast<double>(transmit_calendar.distance(
        first_slot.helper_begin, first_slot.helper_end)) / sample_rate;
    std::optional<double> short_message_seconds;
    if (short_bits) {
        if (short_bits->empty()) short_message_seconds = 0.;
        else {
            auto last = first_slot;
            for (std::size_t i = 1; i < short_bits->size(); ++i)
                last = transmit_calendar.successor(last);
            short_message_seconds = static_cast<double>(transmit_calendar.distance(
                first_slot.helper_begin, last.helper_end)) / sample_rate;
        }
    }
    double helper_high = 0;
    std::string helper_error;
    try {
        if (!o.helper) throw std::logic_error("Helper disabled");
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
        << ",\"symbol_ordinal\":" << f.symbol_ordinal
        << ",\"pattern_revision\":\"" << pattern_revision_name()
        << "\",\"added_amplitude_depth\":" << f.added_amplitude_depth
        << ",\"added_symbols\":" << f.added_symbols << ",\"added_interleave\":" << boolean(f.added_interleave)
        << ",\"encrypt\":" << boolean(f.encrypt) << ",\"fine_chip_rate_hz\":" << g.fine_chip_rate_hz
        << ",\"occupied_bandwidth_hz\":" << nominal << ",\"fhss\":" << boolean(f.fhss)
        << ",\"center_aligned\":" << boolean(f.fhss_center_aligned)
        << ",\"quiet_seconds\":" << f.quiet_seconds << ",\"ramp_seconds\":" << f.ramp_seconds
        << ",\"margin_seconds\":" << f.margin_seconds << ",\"pulse_shape\":";
    json_string(std::cout, triad::v3::pulse_shape_name(f.pulse_shape));
    std::cout << ",\"burst_wait_tiles\":" << f.burst_wait_tiles << ",\"tile_seconds\":" << g.tile_seconds
        << ",\"tile_count\":" << g.tile_count << ",\"elapsed_bit_seconds\":" << g.bit_seconds
        << ",\"fine_coefficients\":" << g.fine_coefficients << ",\"inner_coefficients\":" << g.inner_coefficients
        << ",\"payload_fine_cells_per_tile\":" << g.payload_fine_cells_per_tile << '}'
        << ",\"geometry\":{\"status\":\"checked-integer-geometry-only\",\"active_tiles\":" << g.tile_count
        << ",\"physical_tiles_including_waits\":" << physical_tiles
        << ",\"tile_seconds\":" << g.tile_seconds << ",\"active_data_seconds\":" << active_seconds
        << ",\"elapsed_data_seconds\":" << g.bit_seconds
        << ",\"burst_wait_tiles_between_active_tiles\":" << f.burst_wait_tiles
        << ",\"active_fine_coefficients\":" << g.fine_coefficients
        << ",\"active_dwells\":" << active_dwells
        << ",\"elapsed_fine_cells\":" << g.fine_cells << ",\"elapsed_dwells\":" << g.dwells
        << ",\"padded_source_samples\":" << g.source_samples
        << ",\"source_calendar_origin_sample\":" << g.calendar_origin_sample
        << ",\"bounded_source_workspace_bytes\":" << g.source_workspace_bytes
        << ",\"allocates_samples_or_templates\":false}"
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
        << ",\"effective_gate_bandwidth_hz\":" << gate << ",\"planned_high_hz\":";
    if (o.helper && helper_error.empty()) std::cout << helper_high;
    else std::cout << "null";
    std::cout << ",\"enabled\":" << boolean(o.helper)
        << ",\"feasible_at_requested_if\":" << boolean(!o.helper || helper_error.empty())
        << ",\"unavailable_reason\":";
    if (helper_error.empty()) std::cout << "null";
    else json_string(std::cout, helper_error);
    std::cout << "},\"physics\":";
    physics_json(std::cout, o, physical, physics, data_if_hz);
    if (short_bits) {
        const auto& source = *o.short_message;
        std::cout << ",\"short_message\":{\"text\":";
        json_string(std::cout, source);
        std::cout << ",\"source_bytes\":" << source.size()
            << ",\"encoded_bits\":" << short_bits->size()
            << ",\"codec\":\"short-prefix\"}";
    }
    std::cout << ",\"transmit_calendar\":{\"single_bit_seconds\":" << single_bit_seconds
        << ",\"first_support_start_seconds\":" << transmit_calendar.relative_seconds(calendar_origin, first_slot.support_begin)
        << ",\"first_helper_start_seconds\":" << transmit_calendar.relative_seconds(calendar_origin, first_slot.helper_begin)
        << ",\"first_helper_end_seconds\":" << transmit_calendar.relative_seconds(calendar_origin, first_slot.helper_end)
        << ",\"bits\":";
    if (short_bits) std::cout << short_bits->size(); else std::cout << "null";
    std::cout << ",\"short_message_seconds\":";
    if (short_message_seconds) std::cout << *short_message_seconds; else std::cout << "null";
    std::cout << ",\"scope\":\"Native complete support including pulse halos, Helper schedule and inter-bit spacing; serialization only, no message receiver or added framing\"}";
    std::cout << ",\"export_constraints\":{\"sample_rate_hz\":" << sample_rate
        << ",\"fixed_helper_lead_seconds\":" << (o.helper ? data_start_seconds : 0.)
        << ",\"maximum_sampled_active_seconds\":null"
        << ",\"sampled_duration_constraint\":\"Bounded pull requests and keyed-tile power work independent of complete duration.\""
        << ",\"plan_only_duration_limit\":\"checked uint64 geometry with no sampled-duration cap\""
        << ",\"power_reference_policy\":"; json_string(std::cout, o.power_reference);
    std::cout << ",\"stream_supported\":true"
        << ",\"power_reference_maximum_tiles\":16"
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
    std::cout << ",\"pattern_revision\":\"" << pattern_revision_name()
        << "\",\"added_amplitude_depth\":" << o.added_amplitude_depth;
    std::cout << ",\"physics\":";
    physics_json(std::cout, o, physical, physics, data_if_hz);
    std::cout << "}\n";
}

Complex rotation(double frequency, long double seconds) {
    return std::polar(1., tau * static_cast<double>(std::remainder(frequency * seconds, 1.L)));
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
    double active_seconds = 0, mean_norm = 0;
    unsigned nodes = 0;
    const char* method = "bounded-keyed-tile-quadrature-v1";
    unsigned sampled_tiles = 0;
    double tile_relative_standard_error = 0;
};

DataPowerReference bounded_tile_power(PatternSource& source, const Format& f, const Geometry& g) {
    // Sample at most sixteen independently keyed active tiles. No duration-sized
    // pass or per-chunk gain is used. This is a statistical power reference, not
    // a certificate of a particular complete waveform's energy.
    constexpr unsigned maximum_tiles = 16, nodes_per_tile = 2048;
    const auto tiles = static_cast<unsigned>(std::min<std::uint64_t>(maximum_tiles, g.tile_count));
    const long double active_dwell = f.dwell_seconds - 2 * f.quiet_seconds;
    const long double active_tile = g.tile_seconds * (f.fhss ? active_dwell / f.dwell_seconds : 1.L);
    DataPowerReference result;
    result.active_seconds = static_cast<double>(active_tile * g.tile_count);
    result.sampled_tiles = tiles;
    result.nodes = tiles * nodes_per_tile;
    long double sum = 0, squares = 0;
    for (unsigned j = 0; j < tiles; ++j) {
        const auto begin = static_cast<std::uint64_t>(static_cast<long double>(g.tile_count) * j / tiles);
        const auto end = static_cast<std::uint64_t>(static_cast<long double>(g.tile_count) * (j + 1) / tiles);
        const auto tile = begin + mix64(f.epoch ^ (0x54494c45504f57ULL + j)) % (end - begin);
        long double energy = 0;
        for (unsigned i = 0; i < nodes_per_tile; ++i) {
            const auto random = mix64(f.epoch ^ tile ^ (0x4e4f4445504f57ULL + i));
            const auto fraction = static_cast<long double>(random >> 11) / 9007199254740992.L;
            const auto coordinate = active_tile * (i + fraction) / nodes_per_tile;
            long double within = coordinate;
            if (f.fhss) {
                const auto dwell = std::floor(coordinate / active_dwell);
                within = dwell * f.dwell_seconds + f.quiet_seconds + coordinate - dwell * active_dwell;
            }
            const auto seconds = tile * (static_cast<long double>(f.burst_wait_tiles) + 1) * g.tile_seconds + within;
            energy += std::norm(source.sample_at(g.calendar_origin_sample + seconds * f.sample_rate));
        }
        const auto mean = energy / nodes_per_tile;
        sum += mean; squares += mean * mean;
    }
    result.mean_norm = static_cast<double>(sum / tiles);
    if (!(result.mean_norm > 0) || !std::isfinite(result.mean_norm))
        throw std::runtime_error("invalid bounded tile power reference");
    if (tiles > 1) {
        const auto variance = std::max(0.L, (squares - sum * sum / tiles) / (tiles - 1));
        result.tile_relative_standard_error = static_cast<double>(std::sqrt(variance / tiles) / result.mean_norm);
    }
    return result;
}

void data_power_json(std::ostream& out, const Options& o, const DataPowerReference& power,
                     double calibration_gain, double data_gain) {
    out << "\"data_power_reference_policy\":"; json_string(out, o.power_reference);
    out << ",\"pattern_revision\":\"" << pattern_revision_name()
        << "\",\"added_amplitude_depth\":" << o.added_amplitude_depth
        << ",\"data_power_reference_version\":2,\"data_reference_bit\":0"
        << ",\"data_reference_integration_method\":"; json_string(out, power.method);
    out << ",\"data_reference_complete_sampled_source\":false"
        << ",\"data_reference_active_seconds\":" << power.active_seconds
        << ",\"data_reference_mean_norm\":" << power.mean_norm
        << ",\"data_reference_nodes\":" << power.nodes
        << ",\"data_reference_sampled_tiles\":" << power.sampled_tiles
        << ",\"data_reference_tile_relative_standard_error\":" << power.tile_relative_standard_error
        << ",\"data_reference_statistical_error_is_bound\":false"
        << ",\"post_plan_data_calibration_gain\":" << calibration_gain
        << ",\"template_data_gain\":" << data_gain
        << ",\"data_reference_scope\":";
    json_string(out, "fixed public bit0; at most16 stratified keyed tiles with2048 fractional nodes each; active-time mean omits finite outer halo; between-tile standard error is diagnostic, not a confidence bound or integration guarantee; one unchanged gain for the entire stream");
}

int run(const Options& o) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    if constexpr (std::endian::native != std::endian::little)
        throw std::runtime_error("fixture export requires a little-endian host");
    if (o.short_encode) {
        const Bytes source(o.short_encode->begin(), o.short_encode->end());
        const auto bits = compression::encode_short_bits(source, maximum_short_bits);
        std::string encoded;
        encoded.reserve(bits.size());
        for (const auto bit : bits) encoded.push_back(static_cast<char>('0' + bit));
        std::cout << "{\"schema\":\"triad-short-codec-v1\",\"operation\":\"encode\",\"codec\":\"short-prefix\",\"source_bytes\":"
            << source.size() << ",\"encoded_bits\":" << bits.size() << ",\"bits\":";
        json_string(std::cout, encoded);
        std::cout << "}\n";
        return 0;
    }
    if (o.short_decode) {
        Bytes bits;
        bits.reserve(o.short_decode->size());
        for (const auto bit : *o.short_decode) bits.push_back(static_cast<std::uint8_t>(bit - '0'));
        const auto decoded = compression::decode_short_bits(bits, maximum_short_source_bytes);
        std::cout << "{\"schema\":\"triad-short-codec-v1\",\"operation\":\"decode\",\"codec\":\"short-prefix\",\"received_bits\":"
            << bits.size() << ",\"decoded_bytes\":" << decoded.size() << ",\"text\":";
        json_string(std::cout, received_text(decoded));
        std::cout << ",\"scope\":\"Exact supplied bit endpoint; caller must establish physical completion before decoding. Text uses the shared received-content filter.\"}\n";
        return 0;
    }
    const auto wall_start = std::chrono::steady_clock::now();
    const auto physical = case_for(o);
    const auto physics = resolve_physics(o, physical);
    const auto& base_oscillator = physics.base;
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
    auto f = selected_shaping.format;
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
    const auto selected = selected_key(o);
    const auto keys = datapump::triad::resolve_transmission_keys(
        selected, f.encrypt, datapump::triad::TransmissionMode::Message);
    f.encrypt = keys.encrypt_data;
    const auto& key = keys.data;
    const auto& helper_key = keys.helper;
    PatternSource reference_source(f, key, 0);
    PatternSource data_source(f, key, static_cast<std::uint8_t>(o.bit));
    const auto lead_seconds = o.helper ? data_start_seconds : 0.;
    const auto lead_samples = static_cast<std::uint64_t>(std::llround(lead_seconds * sample_rate));
    if (!o.whole_bit_calendar && o.helper && g.calendar_origin_sample > lead_samples)
        throw std::runtime_error("DATA pulse padding exceeds the fixed Helper lead");
    // Power planning retains its public historical preview coordinate. The
    // opt-in emitted calendar below instead places the entire DATA support
    // after a fresh38-second Helper lead.
    const auto preview_data_offset = static_cast<long double>(data_start_seconds) * sample_rate - g.calendar_origin_sample;
    auto data_offset = o.whole_bit_calendar || !o.helper ? std::uint64_t{0} : lead_samples - g.calendar_origin_sample;
    if (g.source_samples > UINT64_MAX - data_offset - dwell_samples)
        throw std::runtime_error("composite source extent overflow");
    const auto data_end = data_offset + g.source_samples;
    auto composite_samples = ((data_end + dwell_samples - 1) / dwell_samples) * dwell_samples;
    std::vector<HelperBurstSlot> whole_slots;
    std::optional<HelperBurstPlan> burst_plan;
    const HelperBurstCoordinate calendar_origin{f.epoch, 0};
    if (o.whole_bit_calendar) {
        const auto bit_samples_real = static_cast<long double>(g.bit_seconds) * sample_rate;
        if (bit_samples_real < 1 || bit_samples_real > UINT64_MAX)
            throw std::runtime_error("whole-bit logical calendar extent overflow");
        burst_plan.emplace(HelperBurstConfig{sample_rate, static_cast<std::uint64_t>(std::llround(bit_samples_real)),
            g.source_samples, g.calendar_origin_sample, o.whole_bit_wait_count, o.helper});
        auto slot = burst_plan->first_slot(burst_plan->add(calendar_origin, o.helper ? lead_samples : 0));
        whole_slots.reserve(o.whole_bit_count);
        for (unsigned i = 0; i < o.whole_bit_count; ++i) {
            if (slot.pattern_begin.epoch < 6 || slot.pattern_begin.epoch > 4294967289ULL ||
                slot.helper_end.epoch >= 4294967296ULL)
                throw std::runtime_error("whole-bit calendar exceeds keyed epoch or Helper metadata range");
            whole_slots.push_back(slot);
            if (i + 1 < o.whole_bit_count) slot = burst_plan->successor(slot);
        }
        data_offset = burst_plan->distance(calendar_origin, whole_slots.front().support_begin);
        composite_samples = burst_plan->distance(calendar_origin, whole_slots.back().helper_end);
    }
    const auto public_slot = [&](std::uint64_t index) {
        if (!burst_plan || index > 1023)
            throw std::invalid_argument("slot-plan requires a whole-bit calendar and INDEX in[0,1023]");
        if (f.symbol_ordinal > UINT64_MAX - index)
            throw std::invalid_argument("public slot symbol ordinal overflows");
        auto slot = burst_plan->first_slot(burst_plan->add(calendar_origin, o.helper ? lead_samples : 0));
        for (std::uint64_t i = 0; i < index; ++i) slot = burst_plan->successor(slot);
        if (slot.pattern_begin.epoch < 6 || slot.pattern_begin.epoch > 4294967289ULL ||
            slot.helper_end.epoch >= 4294967296ULL)
            throw std::runtime_error("public slot exceeds keyed epoch or Helper metadata range");
        return slot;
    };
    const long double helper_end = static_cast<long double>(composite_samples) / sample_rate;
    long double observation_end = helper_end;
    if (o.observation_slots) {
        // This caller-selected observation budget is independent of DATA
        // contents/count. The finite source still stops at its actual support;
        // one channel continues through the remaining quiet scheduled slots.
        const auto final_observation_slot = public_slot(*o.observation_slots - 1);
        observation_end = burst_plan->relative_seconds(calendar_origin, final_observation_slot.helper_end);
    }
    if (o.helper && static_cast<long double>(f.epoch) + std::ceil(helper_end) >= 4294967296.L)
        throw std::runtime_error("source endpoint exceeds Helper32-bit epoch calendar");

    auto helper_options = helper_options_for(physical);
    helper::PowerReference reference;
    reference.epoch = f.epoch;
    reference.begin_seconds = 39;
    reference.end_seconds = 41;
    reference.time_key = helper_key;
    reference.helper_start_seconds = 0;
    reference.helper_end_seconds = o.whole_bit_calendar ? std::max(helper_end, 42.L) : helper_end;
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
        const auto value = reference_source.sample_at(seconds * sample_rate - preview_data_offset);
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
    helper::Plan plan{};
    if (o.helper) {
        try { plan = helper::plan(helper_options, placement, sample_rate, 0, 1, &reference); }
        catch (const std::exception& error) {
            throw std::runtime_error("production Helper plan rejected DATA IF " + std::to_string(data_if_hz) +
                                     " Hz: " + error.what());
        }
    }
    // Preserve the production Helper preview and composite gain exactly. The
    // former two-second DATA window can sit inside a long low-rate birth ramp,
    // so it is not a general-rate nominal-power reference. A separate DATA-only
    // correction enforces configured active transmit power without changing a
    // Helper sample, duty cycle, waveform, or propagation effect.
    const auto data_reference = bounded_tile_power(reference_source, f, g);
    const auto data_calibration_gain = std::sqrt(reference_mean_norm / data_reference.mean_norm);
    const auto composite_gain = o.helper ? 1 / (plan.data_gain * std::sqrt(reference_mean_norm)) : 1.;
    const auto data_gain = o.helper ? plan.data_gain * composite_gain * data_calibration_gain
                                    : 1 / std::sqrt(data_reference.mean_norm);
    std::unique_ptr<helper::Generator> generator;
    if (o.helper) generator = std::make_unique<helper::Generator>(helper_options, plan, f.epoch, true, helper_key);
    long double measured_helper_norm = 0;
    for (unsigned i = 0; o.helper && i < helper::power_reference_nodes; ++i) {
        const auto seconds = helper::power_reference_node(reference, i);
        if (reference.active(seconds))
            measured_helper_norm += std::norm(generator->mix_analytic({}, seconds, sample_rate,
                reference.helper_end_seconds, 0) * composite_gain);
    }
    const auto reference_helper_ratio = o.helper ? static_cast<double>(measured_helper_norm / reference_nodes) : 0.;

    if (o.power_plan_only) {
        std::cout << std::setprecision(17)
            << "{\"schema\":\"triad-helper-power-plan-v1\",\"sample_rate_hz\":" << sample_rate
            << ",\"key_source\":\"" << (o.key_file.empty()?"explicit-test-fixture-seed":"keyfile") << "\",\"key_seed\":";
        if(o.key_file.empty())std::cout<<o.key_seed;else std::cout<<"null";
        std::cout << ",\"public_epoch_seconds\":" << f.epoch
            << ",\"format\":{\"inner_rate_hz\":" << f.inner_rate_hz << ",\"added_symbols\":" << f.added_symbols
            << ",\"pattern_revision\":\"" << pattern_revision_name()
            << "\",\"added_amplitude_depth\":" << f.added_amplitude_depth
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

    const auto cn0 = physics.data_cn0;
    StreamChannelConfig config;
    const double reference_data_mean_square = data_gain * data_gain * data_reference.mean_norm;
    const double active_helper_watts = o.helper ? physics.effective_helper_w : 0.;
    const double active_data_watts = o.data ? physical.data_w : 0.;
    const double strongest_reference_watts = std::max(active_helper_watts, active_data_watts);
    const double strongest_reference_mean_square = active_helper_watts >= active_data_watts
        ? reference_helper_ratio : reference_data_mean_square;
    if (o.diagnostic_iq_noise.enabled() && o.diagnostic_iq_noise.mode_name() == "watts-relative-to-strongest-signal" &&
        (strongest_reference_watts <= 0 || strongest_reference_mean_square <= 0))
        throw std::invalid_argument("relative diagnostic-IQ noise watts requires at least one active, power-referenced DATA or Helper signal");
    config.diagnostic_iq_noise = o.diagnostic_iq_noise.resolved(1., strongest_reference_mean_square,
                                                          strongest_reference_watts);
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
    effects.noise_enabled = o.thermal_noise;
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
    config.selective_model.correlation_spacing_hz = g.carrier_spacing_hz;
    config.selective_model.center_hz = 0;
    if (o.selective_modulation_rate_hz && o.control == "selected")
        config.selective_model.modulation_rate_hz = *o.selective_modulation_rate_hz;
    if (physics.stochastic) {
        config.selective_model.coefficient_law = SelectiveCoefficientLaw::GaussianErf;
        config.path_evolution.random_doppler_width_hz = physics.random_doppler_width_hz;
        config.path_evolution.delay_excursion_seconds = physics.delay_excursion_seconds;
        config.path_evolution.delay_knot_seconds = physics.delay_knot_seconds;
    }
    config.signal_gain = 1;
    config.complex_noise_variance = sample_rate / std::pow(10., cn0 / 10);
    config.maximum_chunk_samples = stream_chunk_samples;
    // A fixed, public complete observation calendar prevents the random
    // arrival/clock realization from choosing the exported file length.
    const auto tx_min = 1 - base_half_ppm * 1e-6;
    const auto rx_max = 1 + base_half_ppm * 1e-6;
    const auto tx_max = 1 + base_half_ppm * 1e-6;
    const auto rx_min = 1 - base_half_ppm * 1e-6;
    const auto tail_bound_seconds = selected_delay_ms * .001 + physics.delay_excursion_seconds +
        2 / (3 * g.carrier_spacing_hz) + 32. / sample_rate;
    const auto receiver_tail_samples = static_cast<std::uint64_t>(std::ceil(sample_rate * o.receiver_tail_seconds));
    const auto public_capture_stop = [&](long double source_end_seconds) {
        const auto samples = std::ceil(sample_rate * rx_max *
            (physical.capture_lead + static_cast<double>(source_end_seconds) / tx_min + physical.arrival_high +
             physical.capture_trail + tail_bound_seconds));
        if (!std::isfinite(samples) || samples < 0 ||
            static_cast<long double>(samples) > static_cast<long double>(UINT64_MAX - receiver_tail_samples))
            throw std::runtime_error("public receive window overflows sample coordinates");
        return static_cast<std::uint64_t>(samples) + receiver_tail_samples;
    };
    const auto public_capture_first = [&](long double source_begin_seconds) {
        // Keep one public capture-lead interval of preroll before the earliest
        // possible Helper/DATA support. Global physical RX coordinates remain
        // authoritative when this bounded per-slot observation is sliced.
        const auto samples = std::floor(sample_rate * rx_min *
            (static_cast<double>(source_begin_seconds) / tx_max + physical.arrival_low));
        if (!std::isfinite(samples) || static_cast<long double>(samples) > UINT64_MAX)
            throw std::runtime_error("public receive window overflows sample coordinates");
        return samples > 0 ? static_cast<std::uint64_t>(samples) : std::uint64_t{0};
    };
    const bool selective_signal_tail = physics.modulation_rate_hz > 0 &&
        (physics.slow_depth > 0 || physics.fast_depth > 0);
    const auto signal_tail_seconds = physics.delay_ms * .001 + physics.delay_excursion_seconds +
        (selective_signal_tail ? 2 / (3 * g.carrier_spacing_hz) : 0.) + 32. / sample_rate;
    const auto public_signal_first = [&](long double source_begin_seconds) {
        // The finite source interpolation can reach slightly before the
        // nominal edge. Keep the same32-sample engineering guard as the
        // public channel envelope, including the slowest TX clock.
        const auto samples = std::floor(sample_rate * rx_min *
            (physical.capture_lead + static_cast<double>(source_begin_seconds) / tx_max +
             physical.arrival_low - 32. / (sample_rate * tx_min)));
        if (!std::isfinite(samples) || static_cast<long double>(samples) > UINT64_MAX)
            throw std::runtime_error("public signal envelope overflows sample coordinates");
        return samples > 0 ? static_cast<std::uint64_t>(samples) : std::uint64_t{0};
    };
    const auto public_signal_stop = [&](long double source_end_seconds) {
        // Signal support excludes observation-only capture trail/padding.
        // The selective drain exists only when that public filter is enabled.
        const auto samples = std::ceil(sample_rate * rx_max *
            (physical.capture_lead + static_cast<double>(source_end_seconds) / tx_min +
             physical.arrival_high + signal_tail_seconds));
        if (!std::isfinite(samples) || samples < 0 || static_cast<long double>(samples) > UINT64_MAX)
            throw std::runtime_error("public signal envelope overflows sample coordinates");
        return static_cast<std::uint64_t>(samples);
    };
    config.minimum_capture_samples = public_capture_stop(observation_end);
    // Only the current slot owns a DATA pattern cache. The complete transmitter
    // callback and StreamingChannel remain alive across DATA, Helper and quiet
    // intervals, so changing the source bit never resets propagation state.
    std::unique_ptr<PatternSource> whole_source;
    std::size_t whole_source_index = std::numeric_limits<std::size_t>::max();

    long double source_data_energy = 0, source_helper_energy = 0;
    std::uint64_t source_rows = 0;
    EmittedSource emitted;
    emitted.format = f;
    emitted.geometry = g;
    emitted.geometry.source_samples = composite_samples;
    emitted.geometry.calendar_origin_sample = 0;
    emitted.minimum_frequency_hz = o.helper ? std::min(-complete_data_bandwidth / 2,
        helper::low_pilot_frequency_hz - data_if_hz) - spectral_skirt_allowance_hz :
        -complete_data_bandwidth / 2 - spectral_skirt_allowance_hz;
    emitted.maximum_frequency_hz = std::max(complete_data_bandwidth / 2,
        o.helper ? plan.frequency_hz - data_if_hz : 0.) + spectral_skirt_allowance_hz;
    emitted.working_bytes = data_source.working_bytes() + (generator ? sizeof(helper::Generator) : 0) +
        whole_slots.capacity() * sizeof(HelperBurstSlot) + (o.data_bits ? o.data_bits->capacity() : 0) +
        (o.whole_bit_calendar ? g.source_workspace_bytes : 0);
    std::size_t whole_slot_index = 0;
    emitted.sample_at = [&](std::uint64_t n) {
        const auto seconds = static_cast<long double>(n) / sample_rate;
        Complex raw_data;
        long double active_helper_begin = 0, active_helper_end = helper_end;
        bool helper_active = o.helper;
        if (o.whole_bit_calendar) {
            while (whole_slot_index + 1 < whole_slots.size() &&
                   n >= burst_plan->distance(calendar_origin, whole_slots[whole_slot_index].helper_end))
                ++whole_slot_index;
            const auto& slot = whole_slots[whole_slot_index];
            const auto offset = burst_plan->distance(calendar_origin, slot.support_begin);
            if (o.data && n >= offset && n - offset < g.source_samples) {
                if (whole_source_index != whole_slot_index) {
                    auto source_format = f;
                    source_format.epoch = slot.pattern_begin.epoch;
                    source_format.symbol_ordinal = f.symbol_ordinal + slot.index;
                    const auto bit = o.data_bits ? (*o.data_bits)[whole_slot_index] - '0' :
                        static_cast<int>(o.bit ^ (slot.index & 1));
                    // Free the previous private cache before allocating its
                    // replacement, preserving the one-active-source bound.
                    whole_source.reset();
                    whole_source = std::make_unique<PatternSource>(source_format, key,
                        static_cast<std::uint8_t>(bit));
                    whole_source_index = whole_slot_index;
                }
                raw_data = whole_source->sample_at(static_cast<long double>(n - offset));
            }
            active_helper_begin = burst_plan->relative_seconds(calendar_origin, slot.helper_begin);
            active_helper_end = burst_plan->relative_seconds(calendar_origin, slot.helper_end);
            helper_active = o.helper && seconds >= active_helper_begin && seconds < active_helper_end;
        } else if (o.data) {
            if (n >= data_offset && n - data_offset < g.source_samples)
                raw_data = data_source.sample_at(static_cast<long double>(n - data_offset));
        }
        const auto up = rotation(data_if_hz, seconds);
        const auto data = raw_data * data_gain;
        const auto helper_input = raw_data * data_calibration_gain;
        auto audio = helper_active ?
            generator->mix_analytic(helper_input * up, seconds, sample_rate, active_helper_end, active_helper_begin) * composite_gain : data * up;
        auto helper_component = helper_active ? audio - data * up : Complex{};
        const auto value = audio * std::conj(up);
        source_data_energy += std::norm(data);
        source_helper_energy += std::norm(helper_component);
        ++source_rows;
        return value;
    };
    std::unique_ptr<StreamingChannel> channel;
    if (o.stream_kind == "channel")
        channel = std::make_unique<StreamingChannel>(emitted, config);
    if (channel && channel->total_samples() != config.minimum_capture_samples)
        throw std::runtime_error("public observation bound did not cover the complete channel tail");
    if (o.stream_server) {
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            throw std::runtime_error("cannot select binary standard output");
#endif
        // The protocol is intentionally pull-based: pipes/backends provide
        // backpressure, and neither observations nor templates are accumulated.
        // Emission and basis generation own separate PatternSource instances.
        constexpr std::size_t stream_budget = 64ULL * 1024 * 1024;
        constexpr std::size_t maximum_rows = 16384, maximum_columns = 128;
        const auto total = o.stream_kind == "channel" ? channel->total_samples() : composite_samples;
        auto candidate_format = f;
        if (o.whole_bit_calendar) candidate_format.epoch = whole_slots.front().pattern_begin.epoch;
        std::map<std::pair<int, unsigned>, std::unique_ptr<PatternSource>> templates;
        std::size_t template_bytes = 0;
        std::size_t template_cache_evictions = 0;
        std::uint64_t basis_address_generation = 0;
        std::uint64_t cursor = 0;
        std::vector<Complex> samples(maximum_rows);
        std::vector<std::uint8_t> valid(maximum_rows, 1);
        const auto fixed_bytes = emitted.working_bytes + (channel ? channel->report().working_bytes : 0) +
            reference_source.working_bytes() + samples.size() * sizeof(Complex) * 2 + valid.size();
        if (fixed_bytes > stream_budget) throw std::runtime_error("stream source exceeds64MiB workspace budget");
        std::cout.exceptions(std::ios::badbit | std::ios::failbit);
        print_plan_json(o, data_if_hz, selected_shaping, helper_options, physical, physics);
        std::cout << "{\"schema\":\"triad-stream-source-v1\",\"sample_rate_hz\":" << sample_rate
            << ",\"carrier_spacing_hz\":" << g.carrier_spacing_hz
            << ",\"fine_symbol_rate_hz\":" << g.fine_chip_rate_hz
            << ",\"nominal_triplet_bandwidth_hz\":" << g.occupied_bandwidth_hz
            << ",\"emitted_samples\":" << composite_samples << ",\"capture_samples\":" << config.minimum_capture_samples
            << ",\"simulation_channel_constructed\":" << boolean(static_cast<bool>(channel))
            << ",\"total_samples\":" << total << ",\"data_offset_samples\":" << data_offset
            << ",\"source_samples\":" << g.source_samples
            << ",\"capture_lead_seconds\":" << (o.stream_kind == "channel" ? physical.capture_lead : 0)
            << ",\"nominal_noise_variance\":" << config.complex_noise_variance
            << ",\"noise_variance\":" << (o.stream_kind == "channel" && o.thermal_noise ? config.complex_noise_variance : 0)
            << ",\"thermal_noise_enabled\":" << boolean(o.stream_kind == "channel" && o.thermal_noise)
            << ",\"template_data_gain\":" << data_gain << ",\"data_if_hz\":" << data_if_hz
            << ",\"epoch\":" << candidate_format.epoch
            << ",\"symbol_ordinal\":" << candidate_format.symbol_ordinal
            << ",\"pattern_revision\":\"" << pattern_revision_name()
            << "\",\"added_amplitude_depth\":" << f.added_amplitude_depth
            << ",\"helper_enabled\":" << boolean(o.helper)
            << ",\"conditional_gaussian_null_supported\":"
            << boolean(o.stream_kind == "channel" && !o.helper && !o.diagnostic_iq_noise.enabled())
            << ",\"helper_high_hz\":";
        if (o.helper) std::cout << plan.frequency_hz; else std::cout << "null";
        std::cout << ",\"helper_low_hz\":";
        if (o.helper) std::cout << plan.low_frequency_hz; else std::cout << "null";
        std::cout
            << ",\"maximum_rows\":" << maximum_rows << ",\"maximum_columns\":" << maximum_columns
            << ",\"mapped_basis_supported\":true,\"mapped_basis_maximum_offset_samples\":1048576"
            << ",\"basis_address_supported\":true"
            << ",\"slot_plan_supported\":" << boolean(o.whole_bit_calendar)
            << ",\"public_observation_slots\":";
        if (o.observation_slots) std::cout << *o.observation_slots; else std::cout << "null";
        std::cout
            << ",\"workspace_budget_bytes\":" << stream_budget << ",\"fixed_workspace_bytes\":" << fixed_bytes
            << ",\"stream_kind\":"; json_string(std::cout, o.stream_kind);
        std::cout << ",\"key_source\":"; json_string(std::cout, o.key_file.empty() ? "explicit-test-fixture-seed" : "keyfile");
        std::cout << ",\"power\":{"; data_power_json(std::cout, o, data_reference, data_calibration_gain, data_gain);
        std::cout << "},\"basis_scope\":\"Public DATA pattern only; arrival is absolute seconds in the requested observation coordinate. Key is fixed; basis-address selects public epoch/ordinal independently of emitted payload and channel. All three carriers retained.\""
            << ",\"scope\":\"Bounded pull source, not a receiver or real-time guarantee. Emitted kind has no channel or thermal noise. Physical null and full-duration power accuracy require qualification.\"}\n" << std::flush;
        const auto read_line = []() {
            std::string line;
            line.reserve(1024);
            for (;;) {
                const auto next = std::cin.get();
                if (next == std::char_traits<char>::eof())
                    throw std::runtime_error("stream protocol ended without quit; observation remains incomplete");
                if (next == '\n') return line;
                // Reject before appending byte1025, rather than allowing getline
                // to allocate an arbitrarily large command before validation.
                if (line.size() == 1024) throw std::invalid_argument("stream command exceeds1024bytes");
                line.push_back(static_cast<char>(next));
            }
        };
        const auto no_extra = [](std::istringstream& line) {
            std::string extra;
            if (line >> extra) throw std::invalid_argument("extra stream command fields");
        };
        while (true) {
            std::istringstream command(read_line());
            std::string name;
            command >> name;
            if (name == "quit") {
                no_extra(command);
                std::cout << "{\"stopped\":true,\"complete\":" << boolean(cursor == total)
                    << ",\"consumed_samples\":" << cursor << ",\"total_samples\":" << total
                    << ",\"source_generated_samples\":" << source_rows
                    << ",\"source_data_energy\":" << static_cast<double>(source_data_energy)
                    << ",\"source_helper_energy\":" << static_cast<double>(source_helper_energy)
                    << ",\"workspace_bytes\":" << fixed_bytes + template_bytes
                    << ",\"template_cache_evictions\":" << template_cache_evictions << "}\n" << std::flush;
                return 0;
            }
            if (name == "slot-plan") {
                std::string index_text;
                if (!(command >> index_text)) throw std::invalid_argument("slot-plan requires INDEX");
                no_extra(command);
                const auto index = integer(index_text, "public slot index");
                const auto slot = public_slot(index);
                const auto next = burst_plan->successor(slot);
                const auto source_begin = burst_plan->distance(calendar_origin, slot.support_begin);
                const auto next_source_begin = burst_plan->distance(calendar_origin, next.support_begin);
                const auto helper_begin = burst_plan->distance(calendar_origin, slot.helper_begin);
                const auto helper_stop = burst_plan->distance(calendar_origin, slot.helper_end);
                const auto pattern_begin = burst_plan->distance(calendar_origin, slot.pattern_begin);
                const auto pattern_stop = burst_plan->distance(calendar_origin, slot.pattern_end);
                const auto source_stop = burst_plan->distance(calendar_origin, slot.support_end);
                const auto first = public_capture_first(burst_plan->relative_seconds(calendar_origin, slot.helper_begin));
                const auto stop = public_capture_stop(burst_plan->relative_seconds(calendar_origin, slot.helper_end));
                const auto data_first = public_signal_first(burst_plan->relative_seconds(calendar_origin, slot.support_begin));
                const auto data_stop = public_signal_stop(burst_plan->relative_seconds(calendar_origin, slot.support_end));
                const auto previous_stop = index ? public_signal_stop(burst_plan->relative_seconds(
                    calendar_origin, public_slot(index - 1).helper_end)) : std::uint64_t{0};
                const auto next_first = public_signal_first(burst_plan->relative_seconds(calendar_origin, next.helper_begin));
                if (stop <= first) throw std::runtime_error("public receive window is empty");
                const auto quiet_first = std::min(stop, std::max({first, data_first, previous_stop}));
                const auto quiet_stop = std::max(quiet_first, std::min({stop, data_stop, next_first}));
                std::cout << "{\"schema\":\"triad-public-slot-plan-v1\",\"index\":" << index
                    << ",\"sample_rate_hz\":" << sample_rate
                    << ",\"pattern_epoch\":" << slot.pattern_begin.epoch
                    << ",\"symbol_ordinal\":" << f.symbol_ordinal + index
                    << ",\"source_begin_samples\":" << source_begin
                    << ",\"next_source_begin_samples\":" << next_source_begin
                    << ",\"source_samples\":" << g.source_samples
                    << ",\"source_calendar_origin_sample\":" << g.calendar_origin_sample
                    << ",\"helper_begin_samples\":" << helper_begin
                    << ",\"pattern_begin_samples\":" << pattern_begin
                    << ",\"pattern_end_samples\":" << pattern_stop
                    << ",\"source_end_samples\":" << source_stop
                    << ",\"helper_end_samples\":" << helper_stop
                    << ",\"minimum_source_step_samples\":" << static_cast<std::uint64_t>(sample_rate) * (o.helper ? 76 : 60)
                    << ",\"receiver_window_first_sample\":" << first
                    << ",\"receiver_window_stop_sample\":" << stop
                    << ",\"receiver_window_physical_origin_sample\":" << first
                    << ",\"receiver_window_samples\":" << stop - first
                    << ",\"receiver_data_first_sample\":" << data_first
                    << ",\"receiver_data_stop_sample\":" << data_stop
                    << ",\"receiver_previous_signal_stop_sample\":" << previous_stop
                    << ",\"receiver_next_signal_first_sample\":" << next_first
                    << ",\"quiet_aperture_first_sample\":" << quiet_first
                    << ",\"quiet_aperture_stop_sample\":" << quiet_stop
                    << ",\"quiet_aperture_available\":" << boolean(quiet_first < quiet_stop)
                    << ",\"quiet_aperture_truncated\":" << boolean(quiet_first != data_first || quiet_stop != data_stop)
                    << ",\"capture_lead_seconds\":" << physical.capture_lead
                    << ",\"window_capture_lead_seconds\":" << physical.capture_lead - static_cast<long double>(first) / sample_rate
                    << ",\"scope\":\"Public nominal source calendar and bounded RX window only; independent of transmitted payload/count, realized channel and physical message end. Source endpoints are relative to the fixed transmitter epoch; RX indices retain the global observation origin.\"}\n" << std::flush;
            } else if (name == "basis-address") {
                std::string epoch_text, ordinal_text;
                if (!(command >> epoch_text >> ordinal_text))
                    throw std::invalid_argument("basis-address requires EPOCH ORDINAL");
                no_extra(command);
                const auto epoch = integer(epoch_text, "basis public epoch");
                const auto ordinal = integer(ordinal_text, "basis public symbol ordinal");
                if (epoch < 6 || epoch > 4294967289ULL)
                    throw std::invalid_argument("basis epoch must support all thirteen public alternatives");
                if (basis_address_generation == UINT64_MAX)
                    throw std::runtime_error("basis address generation overflow");
                templates.clear();
                template_bytes = 0;
                ++template_cache_evictions;
                candidate_format.epoch = epoch;
                candidate_format.symbol_ordinal = ordinal;
                ++basis_address_generation;
                std::cout << "{\"basis_epoch\":" << epoch << ",\"basis_symbol_ordinal\":" << ordinal
                    << ",\"template_cache_cleared\":true,\"generation\":" << basis_address_generation << "}\n" << std::flush;
            } else if (name == "next") {
                std::string count_text;
                if (!(command >> count_text)) throw std::invalid_argument("next requires COUNT");
                no_extra(command);
                const auto requested = integer(count_text, "stream count");
                if (!requested || requested > maximum_rows) throw std::invalid_argument("next COUNT must be in[1,16384]");
                const auto first = cursor;
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(requested, total - cursor));
                if (o.stream_kind == "emitted") {
                    for (std::size_t i = 0; i < count; ++i) samples[i] = emitted.sample_at(cursor + i);
                    std::fill_n(valid.begin(), count, std::uint8_t{1});
                } else {
                    std::size_t received = 0;
                    while (received < count) {
                        const auto chunk = std::min<std::size_t>(config.maximum_chunk_samples, count - received);
                        const auto got = channel->read(std::span(samples).subspan(received, chunk), std::span(valid).subspan(received, chunk));
                        if (!got) throw std::runtime_error("channel ended before declared stream endpoint");
                        received += got;
                    }
                }
                cursor += count;
                std::cout << "{\"count\":" << count << ",\"first_sample\":" << first
                    << ",\"complete\":" << boolean(cursor == total) << "}\n";
                write_complex(std::cout, std::span(samples).first(count));
                std::cout.write(reinterpret_cast<const char*>(valid.data()), static_cast<std::streamsize>(count));
                std::cout.flush();
            } else if (name == "basis" || name == "basis-map") {
                const bool mapped = name == "basis-map";
                std::string first_text, count_text, columns_text, center_text;
                if (!(command >> first_text >> count_text >> columns_text)) throw std::invalid_argument("basis requires FIRST COUNT N");
                double center_hz = 0;
                if (mapped) {
                    if (!(command >> center_text)) throw std::invalid_argument("basis-map requires FIRST COUNT N IF_HZ");
                    center_hz = real(center_text, "basis audio center");
                    if (center_hz < 0 || center_hz > 120e6) throw std::invalid_argument("mapped basis audio center outside[0,120MHz]");
                }
                no_extra(command);
                const auto first = integer(first_text, "basis first");
                const auto rows64 = integer(count_text, "basis count");
                const auto columns64 = integer(columns_text, "basis columns");
                if (!rows64 || rows64 > maximum_rows || !columns64 || columns64 > maximum_columns || first > UINT64_MAX - rows64)
                    throw std::invalid_argument("basis count/column/coordinate exceeds bounded protocol extent");
                const auto rows = static_cast<std::size_t>(rows64), columns = static_cast<std::size_t>(columns64);
                std::vector<Candidate> requests;
                requests.reserve(columns);
                for (std::size_t c = 0; c < columns; ++c) {
                    std::istringstream line(read_line());
                    std::array<std::string, 7> fields;
                    for (auto& field : fields) if (!(line >> field)) throw std::invalid_argument("basis row needs7fields");
                    no_extra(line);
                    const auto bit = integer(fields[0], "basis bit");
                    const auto epoch_real = real(fields[1], "basis epoch offset");
                    if (bit > 1 || std::floor(epoch_real) != epoch_real || epoch_real < -6 || epoch_real > 6)
                        throw std::invalid_argument("basis bit/epoch outside public alternatives");
                    Candidate candidate;
                    candidate.source_bit = static_cast<std::uint8_t>(bit);
                    candidate.epoch_offset_seconds = static_cast<int>(epoch_real);
                    candidate.arrival_seconds = real(fields[2], "basis arrival");
                    candidate.frequency_hz = real(fields[3], "basis frequency");
                    candidate.clock_ppm = real(fields[4], "basis clock");
                    candidate.channel.taps = {{real(fields[5], "basis delay"), real(fields[6], "basis Doppler")}};
                    if (candidate.clock_ppm <= -1e6 || candidate.channel.taps.front().delay_seconds < 0)
                        throw std::invalid_argument("basis clock must be positive-rate and delay nonnegative");
                    requests.push_back(candidate);
                }
                const bool raw_bank = !mapped && std::all_of(requests.begin(), requests.end(), [](const Candidate& c) {
                    return c.arrival_seconds == 0 && c.frequency_hz == 0 && c.clock_ppm == 0 &&
                        c.channel.taps.front().delay_seconds == 0 && c.channel.taps.front().doppler_hz == 0;
                });
                const auto bank_scratch = raw_bank ? PatternSource::render_bank_scratch_bytes : 0;
                const auto output_bytes = columns * rows * sizeof(Complex);
                const auto map_bytes = mapped ? rows * sizeof(double) : 0;
                const auto command_bytes = columns * (sizeof(Candidate) + sizeof(PatternSource*) + sizeof(Tap));
                // Account for parsed candidate storage, one tap allocation per
                // candidate, optional mapped coordinates, render scratch, the
                // complex output and write_complex's packed binary64 copy.
                const auto request_bytes = command_bytes + 2 * output_bytes + map_bytes + bank_scratch;
                const auto evict_templates = [&]() {
                    if (!templates.empty()) {
                        templates.clear();
                        template_bytes = 0;
                        ++template_cache_evictions;
                    }
                };
                if (fixed_bytes + template_bytes + request_bytes > stream_budget) evict_templates();
                if (fixed_bytes + request_bytes > stream_budget)
                    throw std::runtime_error("basis batch exceeds64MiB workspace budget");
                std::vector<double> offsets;
                if (mapped) {
                    offsets.resize(rows);
                    // The exporter has already rejected a non-little-endian
                    // host. Candidate lines consume their newline; the next
                    // bytes are exactly COUNT binary64 little-endian offsets.
                    std::cin.read(reinterpret_cast<char*>(offsets.data()), static_cast<std::streamsize>(map_bytes));
                    if (!std::cin) throw std::runtime_error("mapped basis stream ended inside coordinate payload");
                    double previous = -1;
                    for (const auto offset : offsets) {
                        if (!std::isfinite(offset) || offset < 0 || offset > 1048576 || offset <= previous ||
                            static_cast<long double>(offset) > static_cast<long double>(UINT64_MAX - first))
                            throw std::invalid_argument("mapped basis coordinates outside bounded increasing extent");
                        previous = offset;
                    }
                }
                std::vector<Complex> output(columns * rows);
                std::vector<PatternSource*> bank_sources;
                if (raw_bank) bank_sources.reserve(columns);
                std::size_t bank_group_first = 0;
                const auto render_bank_group = [&]() {
                    if (bank_sources.empty()) return;
                    auto destination = std::span(output).subspan(bank_group_first * rows, bank_sources.size() * rows);
                    PatternSource::render_bank(bank_sources, static_cast<long double>(first), 1, rows, destination);
                    bank_sources.clear();
                };
                for (std::size_t c = 0; c < columns; ++c) {
                    const auto& candidate = requests[c];
                    const auto address = std::make_pair(static_cast<int>(candidate.epoch_offset_seconds), static_cast<unsigned>(candidate.source_bit));
                    auto found = templates.find(address);
                    if (found == templates.end()) {
                        const auto required = g.source_workspace_bytes;
                        if (templates.size() >= 26 || fixed_bytes + template_bytes + request_bytes + required > stream_budget) {
                            if (raw_bank) render_bank_group();
                            evict_templates();
                        }
                        if (fixed_bytes + request_bytes + required > stream_budget)
                            throw std::runtime_error("one basis source exceeds64MiB workspace budget");
                        auto format = candidate_format;
                        format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(format.epoch) + candidate.epoch_offset_seconds);
                        auto source = std::make_unique<PatternSource>(format, key, candidate.source_bit);
                        const auto actual_bytes = source->working_bytes();
                        if (fixed_bytes + template_bytes + request_bytes + actual_bytes > stream_budget) {
                            if (raw_bank) render_bank_group();
                            evict_templates();
                            if (fixed_bytes + request_bytes + actual_bytes > stream_budget)
                                throw std::runtime_error("one actual basis source exceeds64MiB workspace budget");
                        }
                        template_bytes += actual_bytes;
                        found = templates.emplace(address, std::move(source)).first;
                    }
                    const auto destination = std::span(output).subspan(c * rows, rows);
                    if (raw_bank) {
                        if (bank_sources.empty()) bank_group_first = c;
                        bank_sources.push_back(found->second.get());
                    }
                    else {
                        if (mapped) shaped_basis_mapped(*found->second, candidate, sample_rate, first, offsets, destination, center_hz);
                        else shaped_basis(*found->second, candidate, sample_rate, first, rows, destination);
                    }
                }
                if (raw_bank) render_bank_group();
                for (auto& value : output) value *= data_gain;
                std::cout << "{\"count\":" << rows << ",\"first_sample\":" << first
                    << ",\"columns\":" << columns << ",\"workspace_bytes\":" << fixed_bytes + template_bytes + request_bytes
                    << ",\"template_cache_evictions\":" << template_cache_evictions
                    << ",\"shared_geometry_bank\":" << boolean(raw_bank)
                    << ",\"mapped\":" << boolean(mapped) << "}\n";
                write_complex(std::cout, output);
                std::cout.flush();
            } else throw std::invalid_argument("unknown stream command");
        }
    }
    throw std::logic_error("native source requires a stream request");
}

} // namespace

int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& error) {
        std::cerr << "triad_helper_export: " << error.what() << '\n';
        return 1;
    }
}
