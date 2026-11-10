#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/tuning.hpp"
#include "triad_probe_json.hpp"
#include "triad_probe_spool.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_helper.hpp"
#include "triad_stream_resources.hpp"
#ifdef DATAPUMP_HAVE_TRIAD_TENSOR
#include "datapump/triad_acceleration.hpp"
#endif

#include <openssl/evp.h>
#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using datapump::triad::probe::Json;
using datapump::triad::probe::ObservationSpool;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;
using Clock = std::chrono::steady_clock;
using live::detail::Profile;
constexpr auto schema = "datapump.triad.native-probe.v1";
constexpr std::size_t input_chunk = 4096;
static_assert(input_chunk <= ObservationSpool::maximum_chunk_rows);
constexpr std::uint64_t fixture_epoch = 1793850000, fixture_ordinal = 7000037;
constexpr std::uint64_t default_sample_budget = 4 * 1024 * 1024;
constexpr double manual_thermal_noise = -164;

struct Incomplete : Error { using Error::Error; };
struct Unavailable : Error { using Error::Error; };

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

struct Options {
    std::string command, scenario, backend = "cpu", stratum = "active", tensor_precision = "automatic";
    std::string base = "exact-fixture", shift = "exact-fixture";
    std::string spread = "none", selective = "off", timing = "fixture";
    std::optional<unsigned> workers, owners;
    std::optional<std::size_t> memory_limit, device_memory_limit;
    std::optional<std::uint64_t> spool_limit;
    std::vector<unsigned> pilot_workers;
    std::uint64_t seed = 74021, sample_limit = default_sample_budget, deadline_ms = 30000;
    std::uint64_t slot_index = 0;
    std::size_t maximum_coarse_tiles = 4096;
    unsigned units = 1, sample_rate = 1500, added_symbols = 1, waits = 0;
    unsigned slots = 10, bit = 0, device_index = 0;
    double data_if_hz = 500, rate_hz = 120, rf_hz = 0, margin = 1;
    double data_w = .1, helper_w = 0, loss_db = 114, noise_dbm_hz = -174;
    std::optional<double> target_cn0, active_seconds, observed_cn0;
    bool encrypt = false, interleave = false, fhss = false, require_accelerator = false;
    bool helper = false, compression = true;
    bool work_only = false;
    FecMode fec = FecMode::off;
};

template<class T>
T number(std::string_view text, std::string_view flag) {
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(static_cast<long double>(value)))
        throw Error("Invalid value for " + std::string(flag));
    return value;
}

bool toggle(std::string_view text, std::string_view flag) {
    if (text == "on") return true;
    if (text == "off") return false;
    throw Error(std::string(flag) + " expects on or off");
}

template<class T>
bool one_of(const T& value, std::initializer_list<std::string_view> choices) {
    return std::ranges::find(choices, value) != choices.end();
}

Options parse(int argc, char** argv) {
    require(argc >= 2, "Specify a native probe command; see --help");
    Options o;
    o.command = argv[1];
    require(one_of(o.command, {"describe", "plan", "slot", "continuation", "message", "backlog", "regression", "codec"}),
            "Unknown native probe command");
    std::map<std::string_view, std::string_view> values;
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        require(flag.starts_with("--") && i + 1 < argc, "Native probe options require a value");
        if (!values.emplace(flag, argv[++i]).second) throw Error("Duplicate option " + std::string(flag));
    }
    auto take = [&](std::string_view flag) -> std::optional<std::string_view> {
        const auto found = values.find(flag);
        if (found == values.end()) return {};
        const auto value = found->second;
        values.erase(found);
        return value;
    };
    auto assign = [&](std::string_view flag, auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if (const auto input = take(flag)) {
            if constexpr (std::same_as<T, std::string>) value = *input;
            else if constexpr (std::same_as<T, bool>) value = toggle(*input, flag);
            else if constexpr (requires { typename T::value_type; }) value = number<typename T::value_type>(*input, flag);
            else value = number<T>(*input, flag);
        }
    };
    assign("--case", o.scenario);
    if (!o.scenario.empty()) {
        require(one_of(o.scenario, {"hf", "eme"}), "--case expects hf or eme");
        o.sample_rate = 6000;
        o.data_if_hz = 1725;
        o.timing = "broad";
        o.data_w = o.scenario == "eme" ? 40 : 4;
        o.helper_w = o.scenario == "eme" ? 15000 : 40;
        o.loss_db = o.scenario == "eme" ? 200 : 180;
        o.rate_hz = o.scenario == "eme" ? 360 : 36;
        o.rf_hz = o.scenario == "eme" ? 5.8e9 : 30e6;
        o.base = o.shift = o.scenario == "eme" ? "gpsdo-ocxo" : "gpsdo-xo";
        o.spread = o.scenario == "eme" ? "eme-focused" : "hf-worst";
        o.noise_dbm_hz = manual_thermal_noise;
        o.encrypt = true;
        o.helper = true;
    }
    assign("--backend", o.backend);
    assign("--tensor-precision", o.tensor_precision);
    assign("--require-accelerator", o.require_accelerator);
    assign("--device-index", o.device_index);
    assign("--device-memory-limit-bytes", o.device_memory_limit);
    assign("--workers", o.workers);
    assign("--slot-owners", o.owners);
    assign("--memory-limit-bytes", o.memory_limit);
    assign("--max-samples", o.sample_limit);
    assign("--max-spool-bytes", o.spool_limit);
    assign("--deadline-ms", o.deadline_ms);
    assign("--coarse-tiles", o.maximum_coarse_tiles);
    assign("--seed", o.seed);
    assign("--slot-index", o.slot_index);
    assign("--slots", o.slots);
    assign("--units", o.units);
    assign("--bit", o.bit);
    assign("--stratum", o.stratum);
    assign("--work-only", o.work_only);
    if (const auto pilot = take("--pilot-workers")) {
        auto remaining = *pilot;
        do {
            const auto comma = remaining.find(',');
            const auto worker = number<unsigned>(remaining.substr(0, comma), "--pilot-workers");
            require(worker && std::ranges::find(o.pilot_workers, worker) == o.pilot_workers.end() &&
                    o.pilot_workers.size() < 3, "Invalid repeated or excessive pilot worker allocation");
            o.pilot_workers.push_back(worker);
            if (comma == remaining.npos) break;
            remaining.remove_prefix(comma + 1);
        } while (true);
    }
    assign("--sample-rate", o.sample_rate);
    assign("--data-if", o.data_if_hz);
    assign("--rate", o.rate_hz);
    assign("--rf-hz", o.rf_hz);
    assign("--margin", o.margin);
    assign("--base-oscillator", o.base);
    assign("--shift-oscillator", o.shift);
    assign("--spread-preset", o.spread);
    assign("--selective-perturbation", o.selective);
    assign("--timing-policy", o.timing);
    assign("--data-power-w", o.data_w);
    const bool helper_power_supplied = values.contains("--helper-power-w");
    const bool helper_selected = values.contains("--helper");
    assign("--helper-power-w", o.helper_w);
    assign("--helper", o.helper);
    if (helper_power_supplied && !helper_selected) o.helper = o.helper_w > 0;
    if (!o.helper && !helper_power_supplied) o.helper_w = 0;
    assign("--path-loss-db", o.loss_db);
    assign("--noise-dbm-hz", o.noise_dbm_hz);
    const bool duration_supplied = values.contains("--seconds");
    assign("--seconds", o.active_seconds);
    assign("--target-cn0", o.target_cn0);
    if (o.target_cn0 && !duration_supplied) o.active_seconds.reset();
    assign("--cn0", o.observed_cn0);
    assign("--added-symbols", o.added_symbols);
    assign("--added-interleave", o.interleave);
    assign("--burst-wait-tiles", o.waits);
    assign("--fhss", o.fhss);
    assign("--encrypt", o.encrypt);
    if (o.command == "codec") {
        o.fec = FecMode::rs60;
        o.compression = false;
    }
    assign("--compression", o.compression);
    if (const auto fec = take("--fec")) {
        if (*fec == "rs20") o.fec = FecMode::rs20;
        else if (*fec == "rs60") o.fec = FecMode::rs60;
        else require(*fec == "off", "--fec expects off, rs20 or rs60");
    }
    if (!values.empty()) throw Error("Unknown native probe option " + std::string(values.begin()->first));
    require(one_of(o.backend, {"cpu", "tensor-cpu", "cuda"}), "Unknown native probe backend");
    require(one_of(o.tensor_precision, {"automatic", "fp64"}), "Unknown tensor precision");
    require(one_of(o.stratum, {"active", "noise", "cold-maximum", "terminal", "erasure"}),
            "Unknown native probe stratum");
    require(one_of(o.timing, {"broad", "fixture"}), "Unknown native probe timing policy");
    require(o.sample_limit > 0 && o.deadline_ms > 0 && o.deadline_ms <= 3'600'000 &&
            o.maximum_coarse_tiles > 0 && o.maximum_coarse_tiles <= 1'048'576,
            "Native probe requires bounded positive sample, time and tile budgets");
    require(o.units == 1 || o.units == 2 || o.units == 4, "--units expects 1, 2 or 4");
    require(o.command == "regression" || o.units == 1, "--units applies only to regression");
    require(o.bit <= 1 && o.slots >= 3 && o.slots <= 4096, "Invalid native probe bit or slot count");
    require(o.helper_w >= 0 && o.helper == (o.helper_w > 0) && o.data_w > 0, "Invalid native probe TX powers");
    if (o.command == "regression" || o.command == "slot")
        require(o.stratum == "active" || o.stratum == "noise", "Use continuation for diagnostic fine strata");
    require(!o.work_only || (o.command == "continuation" && o.stratum != "active"),
            "Work-only diagnostics require continuation and a nonnominal stratum");
    require(o.pilot_workers.empty() || o.command == "backlog", "Worker pilots require backlog replay");
    require(o.pilot_workers.empty() || o.backend == "cpu", "Worker allocation pilots require the native CPU backend");
    return o;
}

std::uint64_t maximum_spool_bytes(const Options& o) {
    return o.spool_limit.value_or(o.sample_limit > UINT64_MAX / ObservationSpool::row_bytes ?
        UINT64_MAX : o.sample_limit * ObservationSpool::row_bytes);
}

const char* fec_name(FecMode mode) {
    if (mode == FecMode::rs20) return "rs20";
    if (mode == FecMode::rs60) return "rs60";
    return "off";
}

live::Settings settings_for(const Options& o, std::size_t memory_limit) {
    live::Settings s;
    s.simulation = true;
    s.receiver_memory_limit = memory_limit;
    s.transfer.modem.sample_rate = o.sample_rate;
    s.transfer.modem.carrier_hz = o.data_if_hz;
    s.transfer.modem.bandwidth_hz = o.rate_hz;
    modem::OscillatorSearchConfig oscillator;
    oscillator.rf_shift_hz = o.rf_hz;
    oscillator.margin = o.margin;
    if (o.base == "exact-fixture") oscillator.lf = {0, 0};
    else {
        const auto base = tuning::parse_oscillator_preset(o.base);
        require(base.baseband_supported, "Base oscillator must support the native audio clock");
        oscillator.lf = tuning::oscillator_model(base);
    }
    if (o.shift == "baseband-clock") {
        oscillator.rf = oscillator.lf;
        oscillator.reference = modem::OscillatorReference::shared_radio;
    } else if (o.shift == "exact-fixture") oscillator.rf = {0, 0};
    else {
        oscillator.rf = tuning::oscillator_model(tuning::parse_oscillator_preset(o.shift));
    }
    s.transfer.modem.oscillator_search = oscillator;
    // An explicit broad region remains broad for FHSS. GUI Default is a
    // separate policy which can assume synchronized hop clocks.
    s.transfer.clock_sync = o.timing == "broad" ? clock_sync::Policy{0, 14, 0}
                                                : clock_sync::Policy{.001, .001, 0};
    s.transfer.audio_timing_error_seconds = o.timing == "broad" ? .07 : 0.;
    if (o.encrypt) s.transfer.key = Crypto::amateur();
    s.transfer.fec = o.fec;
    s.transfer.compression = o.compression;
    s.helper.power_w = o.helper_w;
    s.helper.timecode = true;
    s.helper.callsign = "TRIAD";
    s.tx_dbm = 10 * std::log10(o.data_w * 1000);
    s.path_loss_db = o.loss_db;
    s.noise_dbm_hz = o.observed_cn0 ? s.tx_dbm - s.path_loss_db - *o.observed_cn0 : o.noise_dbm_hz;
    s.target_cn0_db_hz = o.target_cn0.value_or(o.scenario.empty() ? 80. : 32.);
    s.spread_preset = o.spread;
    s.selective_perturbation = o.selective;
    s.added_symbols = o.added_symbols;
    s.added_interleave = o.interleave;
    s.fhss_profile = o.fhss ? "fake-fcc" : "off";
    s.symbol_burst_wait = o.waits;
    s.calendar = live::Calendar{fixture_epoch, fixture_ordinal};
    live::validate_settings(s);
    return s;
}

class Deadline {
public:
    explicit Deadline(std::uint64_t milliseconds)
        : timer_([this, milliseconds](std::stop_token done) {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, done, std::chrono::milliseconds(milliseconds), [] { return false; });
            if (!done.stop_requested()) stop_.request_stop();
        }) {}
    std::stop_token token() const { return stop_.get_token(); }
private:
    std::stop_source stop_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::jthread timer_;
};

std::optional<double> cpu_seconds() {
    return datapump::triad::v3::detail::stream_process_cpu_seconds();
}

std::optional<double> thread_cpu_seconds() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return {};
    const auto ticks = [](FILETIME t) {
        return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    return (static_cast<double>(ticks(kernel)) + static_cast<double>(ticks(user))) * 1e-7;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return {};
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) * 1e-9;
#else
    return {};
#endif
}

struct Timing {
    double wall = 0;
    std::optional<double> cpu;
};
void add_time(Timing& total, Timing elapsed) {
    total.wall += elapsed.wall;
    if (total.cpu && elapsed.cpu) *total.cpu += *elapsed.cpu;
    else total.cpu.reset();
}

struct Timer {
    Clock::time_point wall = Clock::now();
    std::optional<double> cpu = cpu_seconds();
    Timing elapsed() const {
        const auto end = cpu_seconds();
        return {std::chrono::duration<double>(Clock::now() - wall).count(),
                end && cpu && *end >= *cpu ? std::optional{*end - *cpu} : std::nullopt};
    }
};

void phase(Json& report, std::string_view name, Timing time, double represented_seconds, Json work,
           unsigned unit = 0, std::string_view scope = "receiver") {
    report["phases"].array().push_back(object({
        {"name", name}, {"wall_seconds", time.wall}, {"process_cpu_seconds", time.cpu ? Json(*time.cpu) : Json{}},
        {"represented_seconds", represented_seconds}, {"scope", scope}, {"unit_index", unit}, {"work", std::move(work)}}));
}

class Digest {
public:
    Digest() {
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
            throw Error("Cannot initialize native observation digest");
    }
    void add(std::span<const std::byte> bytes) {
        if (!bytes.empty() && EVP_DigestUpdate(context_.get(), bytes.data(), bytes.size()) != 1)
            throw Error("Cannot hash native observation");
    }
    template<class T> void add_little(std::span<const T> values) {
        if constexpr (std::endian::native == std::endian::little) {
            add(std::as_bytes(values));
        } else {
            for (const auto& value : values) {
                auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
                std::ranges::reverse(bytes);
                add(bytes);
            }
        }
    }
    std::string finish() {
        std::array<unsigned char, 32> bytes{};
        unsigned size = 0;
        if (EVP_DigestFinal_ex(context_.get(), bytes.data(), &size) != 1 || size != bytes.size())
            throw Error("Cannot finish native observation digest");
        std::string result;
        for (const auto byte : bytes) {
            result += "0123456789abcdef"[byte >> 4];
            result += "0123456789abcdef"[byte & 0x0f];
        }
        return result;
    }
private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(), EVP_MD_CTX_free};
};

class CaptureHasher {
public:
    void add(std::span<const Complex> iq, std::span<const std::uint8_t> valid) {
        // std::complex<double> has an array-oriented two-double representation.
        iq_.add_little(std::span(reinterpret_cast<const double*>(iq.data()), 2 * iq.size()));
        valid_.add(std::as_bytes(valid));
    }
    std::string finish() {
        const auto pair = iq_.finish() + valid_.finish();
        Digest combined;
        combined.add(std::as_bytes(std::span<const char>(pair)));
        return combined.finish();
    }
private:
    Digest iq_, valid_;
};

struct Capture {
    std::uint64_t first = 0, generated_samples = 0;
    std::vector<Complex> iq;
    Bytes valid;
    std::unique_ptr<ObservationSpool> spool;
    std::string digest;
    Timing generation, spool_write{0, 0};
    StreamChannelReport channel;
    std::size_t generation_reserved_bytes = 0;
    std::uint64_t rows() const { return spool ? spool->rows() : iq.size(); }
};

Json allocation(const StreamReceiverResources& resources) {
    return object({{"hardware_threads", resources.hardware_threads}, {"workers", resources.workers},
                   {"slot_owners", resources.slot_owners}, {"memory_limit_bytes", resources.memory_limit_bytes},
                   {"receiver_numerical_threads_per_worker", 1}});
}

Json recurring_horizon(const PublicProfile& waveform) {
    const auto count = std::min<std::uint64_t>(24, waveform.last_slot());
    double shortest = std::numeric_limits<double>::infinity();
    std::uint64_t shortest_slot = 0, largest = 0, largest_slot = 0;
    const auto& calendar = waveform.calendar();
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto window = waveform.slot(i);
        const auto next = calendar.successor(window.tx);
        const auto interval = static_cast<double>(calendar.relative_seconds(window.tx.pattern_begin, next.pattern_begin));
        if (interval < shortest) {
            shortest = interval;
            shortest_slot = i;
        }
        if (window.observation.size() > largest) {
            largest = window.observation.size();
            largest_slot = i;
        }
    }
    return object({{"slots", count}, {"largest_window_samples", largest}, {"largest_window_slot", largest_slot},
                   {"shortest_interval_seconds", shortest}, {"shortest_interval_slot", shortest_slot},
                   {"complete_declared_horizon", count == 24}});
}

Json profile_json(const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
                  const StreamReceiverResources& resources) {
    const auto& config = profile.receiver;
    const auto& waveform = profile.waveform;
    const auto& geometry = waveform.geometry();
    const auto& channel = profile.channel;
    const auto& calendar = waveform.calendar();
    const auto current = waveform.transmit_slot(slot.index);
    const auto next = calendar.successor(current);
    const double stride = static_cast<double>(calendar.relative_seconds(current.pattern_begin, next.pattern_begin));
    auto cpu_config = config;
    cpu_config.bulk_backend.reset();
    auto result = object({
        {"scenario", o.scenario.empty() ? "bounded-native-fixture-v1" : o.scenario},
        {"sample_rate_hz", config.sample_rate_hz}, {"data_if_hz", config.data_if_hz},
        {"rf_hz", o.rf_hz}, {"inner_rate_hz", o.rate_hz}, {"data_power_w", o.data_w},
        {"requested_helper_power_w", o.helper_w}, {"effective_helper_power_w", config.helper.effective_power_w},
        {"path_loss_db", o.loss_db}, {"case_noise_dbm_hz", o.noise_dbm_hz},
        {"observed_cn0_override", o.observed_cn0 ? Json(*o.observed_cn0) : Json{}},
        {"actual_cn0_db_hz", 10 * std::log10(config.sample_rate_hz / config.noise_variance)},
        {"target_cn0_db_hz", o.target_cn0.value_or(o.scenario.empty() ? 80. : 32.)},
        {"duration_policy", o.active_seconds ? "explicit_fixed" : "native_target_model"},
        {"fixed_active_seconds", o.active_seconds ? Json(*o.active_seconds) : Json{}},
        {"base_oscillator", o.base}, {"shift_oscillator", o.shift},
        {"frequency_bound_hz", config.frequency_bound_hz}, {"clock_bound_ppm", config.clock_bound_ppm},
        {"spread_preset", o.spread}, {"spread_delay_seconds", config.spread_delay_seconds},
        {"spread_doppler_hz", config.spread_doppler_hz}, {"selective_perturbation", o.selective},
        {"selective_slow_depth", config.selective_slow_depth}, {"selective_fast_depth", config.selective_fast_depth},
        {"selective_modulation_rate_hz", config.selective_modulation_rate_hz}, {"margin", o.margin},
        {"arrival_lower_seconds", slot.arrival_lower_seconds}, {"arrival_upper_seconds", slot.arrival_upper_seconds},
        {"encrypt", waveform.config().format.encrypt}, {"helper_enabled", config.helper_enabled},
        {"added_symbols", o.added_symbols}, {"added_interleave", o.interleave},
        {"fhss", waveform.config().format.fhss}, {"physical_rf_retuning", channel.effects.fhss},
        {"symbol_burst_wait_bits", o.waits}, {"fec", fec_name(o.fec)}, {"compression", o.compression},
        {"bit_seconds", geometry.bit_seconds}, {"slot_interval_seconds", stride},
        {"nominal_payload_energy_seconds", geometry.nominal_payload_energy_seconds},
        {"nominal_continuation_eligible", geometry.nominal_payload_energy_seconds >= 6 &&
            slot.stop_sample - slot.first_sample >= 6 * config.sample_rate_hz},
        {"source_samples", geometry.source_samples}, {"public_window_samples", slot.stop_sample - slot.first_sample},
        {"public_window_seconds", (slot.stop_sample - slot.first_sample) / config.sample_rate_hz},
        {"quiet_first_sample", slot.quiet_first_sample}, {"quiet_stop_sample", slot.quiet_stop_sample},
        {"calendar_epoch", waveform.config().format.epoch},
        {"initial_symbol_ordinal", waveform.config().format.symbol_ordinal},
        {"recurring_horizon", recurring_horizon(waveform)},
        {"scope", "configured_public_calendar_and_keyed_ordinal"}});
    try {
        result["receiver_slot_workspace_bytes"] = receiver_slot_workspace(cpu_config, slot, resources.workers);
    } catch (const Error& error) {
        result["receiver_slot_workspace_bytes"] = Json{};
        result["receiver_slot_workspace_reason"] = error.what();
    }
    return result;
}

Json observation(const Capture& capture, std::uint64_t stop, const ReceiverSlotPlan& slot, const Options& o) {
    return object({{"input_sha256", capture.digest}, {"first_sample", capture.first}, {"stop_sample", stop},
                   {"input_hash_encoding", "sha256_of_iq_f64le_and_mask_sha256_hex"},
                   {"samples", capture.rows()}, {"generated_samples", capture.generated_samples},
                   {"retained_ram_rows", capture.iq.size()}, {"spool_bytes", capture.spool ? capture.spool->bytes() : 0},
                   {"spool_working_bytes", capture.spool ? capture.spool->working_bytes() : 0},
                   {"spool_backing_kind", capture.spool ? Json(capture.spool->backing_kind()) : Json{}},
                   {"spool_residency_reserved_bytes", capture.spool ? capture.spool->resident_reservation_bytes() : 0},
                   {"seed", o.seed}, {"public_slot", slot.index}, {"pattern_epoch", slot.format.epoch},
                   {"symbol_ordinal", slot.format.symbol_ordinal}, {"channel_erased_rows", capture.channel.erased_samples},
                   {"channel_workspace_bytes", capture.channel.working_bytes},
                   {"evaluator_bit", o.stratum == "noise" ? Json{} : Json(o.bit)}});
}

Json bulk_json(const BulkReceipt& r) {
    Json::Object fallbacks;
    for (std::size_t i = 0; i < r.direct_fallback_counts.size(); ++i)
        fallbacks.emplace(std::string(bulk_direct_fallback_reasons[i]), r.direct_fallback_counts[i]);
    return object({
        {"device", r.device}, {"device_name", r.device_name}, {"library_version", r.library_version},
        {"cuda_runtime_version", r.cuda_runtime_version}, {"cuda_driver_version", r.cuda_driver_version},
        {"numerical_threads", r.numerical_threads},
        {"precision", r.precision}, {"cuda", r.cuda}, {"executed", r.executed},
        {"workspace_limit_bytes", r.workspace_limit_bytes}, {"reserved_bytes", r.reserved_bytes},
        {"peak_reserved_bytes", r.peak_reserved_bytes}, {"allocator_live_bytes", r.allocator_live_bytes},
        {"allocator_reserved_bytes", r.allocator_reserved_bytes}, {"allocator_peak_live_bytes", r.allocator_peak_live_bytes},
        {"allocator_peak_reserved_bytes", r.allocator_peak_reserved_bytes}, {"total_device_bytes", r.total_device_bytes},
        {"initial_free_device_bytes", r.initial_free_device_bytes}, {"runtime_headroom_bytes", r.runtime_headroom_bytes},
        {"fft_plan_cache_count", r.fft_plan_cache_count}, {"fft_plan_cache_limit", r.fft_plan_cache_limit},
        {"upload_bytes", r.upload_bytes},
        {"download_bytes", r.download_bytes}, {"synchronizations", r.synchronizations},
        {"fft_calls", r.fft_calls}, {"fft_rows", r.fft_rows}, {"fir_input_rows", r.fir_input_rows},
        {"fir_output_contributions", r.fir_output_contributions}, {"score_rows", r.score_rows},
        {"statistics_blocks", r.statistics_blocks}, {"projected_values", r.projected_values},
        {"dc_reduction_values", r.dc_reduction_values},
        {"mapped_columns", r.mapped_columns}, {"mapped_values", r.mapped_values},
        {"cuda_fused_mapped_launches", r.cuda_fused_mapped_launches},
        {"cuda_fused_mapped_values", r.cuda_fused_mapped_values},
        {"cuda_direct_gram_values", r.cuda_direct_gram_values},
        {"cuda_bmm_gram_values", r.cuda_bmm_gram_values},
        {"uploaded_tiles", r.uploaded_tiles}, {"tile_hits", r.tile_hits},
        {"direct_columns", r.direct_columns}, {"direct_values", r.direct_values},
        {"direct_fallback_counts", std::move(fallbacks)}, {"wall_seconds", r.wall_seconds}});
}

std::shared_ptr<ReceiverBulkBackend> selected_backend(const Options& o) {
    if (o.backend == "cpu") {
        if (o.require_accelerator) throw Unavailable("An accelerated probe requires --backend cuda");
        return {};
    }
    if (o.require_accelerator && o.backend != "cuda")
        throw Unavailable("An accelerated probe requires --backend cuda");
#ifdef DATAPUMP_HAVE_TRIAD_TENSOR
    try {
        return make_tensor_receiver_backend({o.backend == "cuda" ? TensorDevice::cuda : TensorDevice::cpu,
            o.device_index, o.device_memory_limit.value_or(256 * 1024 * 1024),
            o.tensor_precision == "fp64" ? TensorPrecision::fp64 : TensorPrecision::automatic});
    } catch (const TensorBackendUnavailable& error) {
        throw Unavailable(error.what());
    }
#else
    throw Unavailable("This native probe build does not include the optional tensor backend");
#endif
}

std::size_t memory_remaining(const StreamReceiverResources& resources) {
    const auto footprint = datapump::triad::v3::detail::stream_controller_footprint_bytes();
    if (footprint >= resources.memory_limit_bytes)
        throw Incomplete("Current process footprint exhausts the local probe memory limit");
    return resources.memory_limit_bytes - footprint;
}

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw Incomplete("Native probe deadline reached");
}

Capture simulate(const Profile& profile, std::span<const EmissionOptions> episodes,
                 std::uint64_t first, std::uint64_t end, std::uint64_t& samples_left,
                 const StreamReceiverResources& resources, std::stop_token stop,
                 std::unique_ptr<ObservationSpool> spool = {}) {
    // The canonical channel and one emitted source have a fixed 65 MiB bound.
    // Reserve this before creating either; account extra independently owned
    // source objects when exercising a second episode in the same channel.
    constexpr std::size_t channel_source_bound = 65 * 1024 * 1024;
    if (end <= first || end > samples_left || (!spool && end - first > SIZE_MAX / (sizeof(Complex) + 1)))
        throw Incomplete("Canonical capture exceeds the declared probe sample budget");
    const auto rows = end - first;
    constexpr auto chunk_bytes = input_chunk * (sizeof(Complex) + sizeof(std::uint8_t));
    const auto capture_bytes = spool ? spool->working_bytes() + chunk_bytes + 4096 :
        static_cast<std::size_t>(rows) * (sizeof(Complex) + sizeof(std::uint8_t));
    const auto simultaneous_sources = std::max(std::size_t{1}, episodes.size());
    const auto reserve = simultaneous_sources * channel_source_bound;
    const auto remaining = memory_remaining(resources);
    if (reserve > remaining || capture_bytes > remaining - reserve)
        throw Incomplete("Canonical capture and source preflight exceed the local probe memory limit");
    check_stop(stop);
    const Timer timer;
    std::vector<Emission> emitters;
    std::vector<EmittedSource> inputs;
    std::vector<std::uint64_t> origins;
    for (const auto& episode : episodes) {
        emitters.emplace_back(profile.waveform, profile.key, episode);
        inputs.push_back(emitters.back().source());
        origins.push_back(episode.source_first_sample);
    }
    require(!inputs.empty(), "Native probe has no emission profile");
    // Each component is forward-only. The evaluator places whole locally
    // prepared episodes on one public calendar, and the channel advances
    // once through the entire sum, including quiet slots and delayed tails.
    auto composite = inputs.front();
    composite.geometry.source_samples = 0;
    composite.working_bytes = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        if (input.geometry.source_samples > UINT64_MAX - origins[i])
            throw Error("Probe emitter calendar overflow");
        composite.geometry.source_samples = std::max(composite.geometry.source_samples,
            origins[i] + input.geometry.source_samples);
        composite.working_bytes += input.working_bytes;
        composite.minimum_frequency_hz = std::min(composite.minimum_frequency_hz, input.minimum_frequency_hz);
        composite.maximum_frequency_hz = std::max(composite.maximum_frequency_hz, input.maximum_frequency_hz);
    }
    composite.sample_at = [&](std::uint64_t i) {
        Complex result{};
        for (std::size_t n = 0; n < inputs.size(); ++n)
            if (i >= origins[n] && i - origins[n] < inputs[n].geometry.source_samples)
                result += inputs[n].sample_at(i - origins[n]);
        return result;
    };
    auto configuration = profile.channel;
    configuration.minimum_capture_samples = end;
    StreamingChannel channel(std::move(composite), configuration);
    if (channel.total_samples() < end)
        throw Error("Native channel ended before the public observation");
    Capture capture;
    capture.first = first;
    capture.spool = std::move(spool);
    capture.generation_reserved_bytes = resources.memory_limit_bytes - remaining + reserve + capture_bytes;
    if (capture.spool) {
        // The caller removed this backing reservation before the preflight.
        capture.generation_reserved_bytes += capture.spool->resident_reservation_bytes();
    } else {
        capture.iq.resize(static_cast<std::size_t>(rows));
        capture.valid.resize(static_cast<std::size_t>(rows));
    }
    CaptureHasher digest;
    std::array<Complex, input_chunk> iq{};
    std::array<std::uint8_t, input_chunk> valid{};
    while (channel.position() < end) {
        check_stop(stop);
        const auto offset = channel.position();
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(end - offset, input_chunk));
        if (channel.read(std::span(iq).first(chunk), std::span(valid).first(chunk)) != chunk)
            throw Error("Native channel returned a partial public observation");
        if (offset + chunk > first) {
            const auto begin = static_cast<std::size_t>(std::max(offset, first) - offset);
            const auto samples = std::span<const Complex>(iq).subspan(begin, chunk - begin);
            const auto mask = std::span<const std::uint8_t>(valid).subspan(begin, chunk - begin);
            digest.add(samples, mask);
            if (capture.spool) {
                const Timer io;
                capture.spool->append(samples, mask);
                add_time(capture.spool_write, io.elapsed());
            } else {
                const auto target = static_cast<std::size_t>(offset + begin - first);
                std::ranges::copy(samples, capture.iq.begin() + target);
                std::ranges::copy(mask, capture.valid.begin() + target);
            }
        }
    }
    if (capture.spool) {
        const Timer io;
        capture.spool->seal();
        add_time(capture.spool_write, io.elapsed());
    }
    capture.generation = timer.elapsed();
    capture.channel = channel.report();
    capture.generated_samples = end;
    samples_left -= end;
    capture.digest = digest.finish();
    return capture;
}

class SpoolReplay {
public:
    explicit SpoolReplay(ObservationSpool& spool) : spool_(spool) {
        const Timer io;
        spool_.rewind();
        timing_ = io.elapsed();
    }

    // Backpressure retries borrow the same arrays and never read the chunk twice.
    void load(std::uint64_t first, std::size_t rows) {
        if (loaded_ == first) return;
        const Timer io;
        spool_.read(std::span(iq_).first(rows), std::span(valid_).first(rows));
        rows_ = rows;
        digest_.add(iq(), valid());
        loaded_ = first;
        add_time(timing_, io.elapsed());
    }

    std::span<const Complex> iq() const { return std::span(iq_).first(rows_); }
    std::span<const std::uint8_t> valid() const { return std::span(valid_).first(rows_); }
    Timing timing() const { return timing_; }
    std::string finish(const Capture& capture, std::uint64_t rows) {
        const Timer io;
        auto digest = digest_.finish();
        if (rows == capture.rows() && digest != capture.digest)
            throw Error("Native spool replay differed from its captured observation");
        add_time(timing_, io.elapsed());
        return digest;
    }

private:
    ObservationSpool& spool_;
    CaptureHasher digest_;
    Timing timing_;
    std::array<Complex, input_chunk> iq_{};
    std::array<std::uint8_t, input_chunk> valid_{};
    std::uint64_t loaded_ = UINT64_MAX;
    std::size_t rows_ = 0;
};

void frontend(Json& report, const Profile& profile, const Capture& capture, std::stop_token stop,
              unsigned unit = 0) {
    const auto rate = profile.waveform.config().format.sample_rate;
    const auto carrier = profile.receiver.data_if_hz;
    const Timer setup;
    live::detail::AudioInput projector(rate, carrier, profile.minimum_audio_hz, profile.maximum_audio_hz,
                                      capture.first);
    auto total = setup.elapsed();
    std::array<float, input_chunk> pcm{};
    std::vector<Complex> output;
    Bytes valid;
    std::optional<SpoolReplay> replay;
    if (capture.spool) replay.emplace(*capture.spool);
    std::size_t projected = 0, valid_rows = 0;
    for (std::uint64_t at = 0; at < capture.rows(); at += input_chunk) {
        check_stop(stop);
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(capture.rows() - at, input_chunk));
        if (replay) replay->load(at, n);
        const auto iq = replay ? replay->iq() : std::span(capture.iq).subspan(static_cast<std::size_t>(at), n);
        const auto mask = replay ? replay->valid() : std::span(capture.valid).subspan(static_cast<std::size_t>(at), n);
        for (std::size_t i = 0; i < n; ++i)
            pcm[i] = live::detail::real_pcm(iq[i], capture.first + at + i, carrier, rate);
        const Timer measure;
        projector.project(std::span(pcm).first(n), output, mask, &valid);
        add_time(total, measure.elapsed());
        projected += output.size();
        valid_rows += std::ranges::count(valid, std::uint8_t{1});
    }
    auto work = object({{"input_rows", capture.rows()}, {"output_rows", projected}, {"valid_rows", valid_rows},
                  {"halo_samples", projector.halo_samples()}, {"workspace_bytes", projector.working_bytes()}});
    if (replay) {
        work["verified_input_sha256"] = replay->finish(capture, capture.rows());
        work["spool_read_and_verification_seconds_excluded"] = replay->timing().wall;
    }
    phase(report, "audio_frontend", total, static_cast<double>(capture.rows()) / rate,
          std::move(work), unit, "native_pcm_projection_separate_from_iq_receive");
}

const char* decision_name(SlotDecision decision) {
    if (decision == SlotDecision::data) return "data";
    if (decision == SlotDecision::quiet) return "quiet";
    return "ambiguous";
}

Json scores(const std::array<double, 2>& values) {
    return Json::Array{values[0], values[1]};
}

Json score_json(const ReceiverSlotScore& score) {
    return object({
        {"index", score.index}, {"first_sample", score.first_sample}, {"stop_sample", score.stop_sample},
        {"decision", decision_name(score.decision)}, {"bit", score.bit ? Json(*score.bit) : Json{}},
        {"complete", score.complete}, {"all_valid", score.all_valid}, {"zero_h0_verified", score.zero_h0_verified},
        {"reason", score.reason}, {"operational_threshold", score.operational_threshold},
        {"operational_log_e", score.operational_log_e}, {"selected_prior_mass", score.selected_prior_mass},
        {"operational_passes_conditional_bound", score.operational_passes_conditional_bound},
        {"bit_evidence", scores(score.bit_evidence)}, {"adaptive_bit_evidence", scores(score.adaptive_bit_evidence)},
        {"combined_bit_evidence", scores(score.combined_bit_evidence)}, {"quiet_energy", score.quiet_energy},
        {"quiet_threshold", score.quiet_threshold}, {"quiet_modes", score.quiet_modes},
        {"fine_candidates", score.fine_candidates}, {"adaptive_peak_states", score.adaptive_peak_states},
        {"adaptive_transition_edges", score.adaptive_transition_edges}, {"basis_rows", score.basis_rows},
        {"coarse_workspaces", score.coarse_workspaces}, {"coarse_frequency_points", score.coarse_frequency_points},
        {"coarse_arrival_points", score.coarse_arrival_points}, {"affine_blocks", score.affine_blocks},
        {"adaptive_blocks", score.adaptive_blocks}, {"helper_status", score.helper_status},
        {"helper_seconds", score.helper_seconds}, {"coarse_seconds", score.coarse_seconds},
        {"fine_seconds", score.fine_seconds}, {"workspace_peak_bytes", score.workspace_peak_bytes}});
}

Json coarse_counts(const CoarsePhaseWork& phase_work) {
    const auto& o = phase_work.operations;
    return object({{"wall_seconds", phase_work.wall_seconds}, {"forward_ffts", o.forward_ffts},
        {"inverse_ffts", o.inverse_ffts}, {"fft_rows", o.fft_rows}, {"fft_row_stages", o.fft_row_stages},
        {"source_render_calls", o.source_render_calls}, {"source_render_rows", o.source_render_rows},
        {"projected_columns", o.projected_columns}, {"fir_input_rows", o.fir_input_rows},
        {"fir_output_contributions", o.fir_output_contributions}, {"workspace_frequency_pairs", o.workspace_frequency_pairs},
        {"score_rows", o.score_rows}, {"singleton_dot_terms", o.singleton_dot_terms},
        {"retained_peaks", o.retained_peaks}, {"selection_peak_rows", o.selection_peak_rows}});
}

Json coarse_json(const CoarseProgress& p) {
    const auto& g = p.plan;
    return object({{"completed_tiles", p.completed_tiles}, {"planned_tiles", g.total_tiles},
        {"remaining_tiles", g.total_tiles - p.completed_tiles},
        {"tile_width", g.tile_width}, {"route", g.route == CoarseRoute::lag_groups ? "lag_groups" : "frequency_tiles"},
        {"workers", g.workers}, {"first_sample", g.first_sample}, {"stop_sample", g.stop_sample},
        {"public_slot", g.public_slot}, {"pattern_epoch", g.pattern_epoch}, {"symbol_ordinal", g.symbol_ordinal},
        {"rays", g.rays}, {"clocks", g.clocks}, {"workspaces", g.workspaces}, {"frequency_points", g.frequency_points},
        {"frequency_groups", g.frequency_groups}, {"arrival_points", g.arrival_points},
        {"source_rows", g.source_rows}, {"raw_template_rows", g.raw_template_rows},
        {"projected_template_rows", g.projected_template_rows}, {"projected_observation_rows", g.projected_observation_rows},
        {"template_sampling_step", g.template_sampling_step}, {"fir_taps", g.fir_taps}, {"fir_fft_rows", g.fir_fft_rows},
        {"correlation_fft_rows", g.correlation_fft_rows}, {"maximum_frequency_step_hz", g.maximum_frequency_step_hz},
        {"projected_sample_rate_hz", g.projected_sample_rate_hz}, {"clock_gap_ppm", g.clock_gap_ppm},
        {"arrival_lower_seconds", g.arrival_lower_seconds}, {"arrival_upper_seconds", g.arrival_upper_seconds},
        {"workspace_reserved_bytes", g.workspace_reserved_bytes}, {"retained_bytes", p.retained_bytes},
        {"proposals", p.proposals}, {"refined_coordinates", p.refined_coordinates},
        {"filter_setup", coarse_counts(p.filter_setup)}, {"mapped_fidelity", coarse_counts(p.mapped_fidelity)},
        {"template_bank", coarse_counts(p.template_bank)}, {"observation", coarse_counts(p.observation)},
        {"lag_setup", coarse_counts(p.lag_setup)}, {"bulk_prepare", coarse_counts(p.bulk_prepare)},
        {"search", coarse_counts(p.search)}, {"selection", coarse_counts(p.selection)},
        {"bulk", p.bulk ? bulk_json(*p.bulk) : Json{}}});
}

Json coordinate_json(const FineCoordinate& c) {
    return object({{"epoch_offset_seconds", c.epoch_offset_seconds}, {"arrival_seconds", c.arrival_seconds},
                   {"frequency_hz", c.frequency_hz}, {"clock_ppm", c.clock_ppm}});
}

Json coarse_selected(const CoarseResult& coarse) {
    auto result = coarse_json(coarse.work);
    Json::Array proposals, coordinates;
    for (const auto& p : std::span(coarse.proposals).first(coarse.proposal_count)) {
        proposals.push_back(object({{"coordinate", coordinate_json(p.coordinate)},
                                    {"bit", p.bit}, {"rank", p.rank}, {"score", p.score}}));
    }
    for (const auto& c : coarse.coordinates) coordinates.push_back(coordinate_json(c));
    result["proposals_detail"] = std::move(proposals);
    result["refined_coordinates_detail"] = std::move(coordinates);
    result["reason"] = coarse.reason;
    return result;
}

Json fine_lane(const FineLaneWork& lane) {
    Digest digest;
    for (const auto& key : lane.retained_states) {
        const std::array<std::int64_t, 3> coordinate{key.group, key.delay, key.rate};
        digest.add_little(std::span<const std::int64_t>(coordinate));
    }
    return object({{"states_sha256", digest.finish()}, {"states", lane.retained_states.size()},
        {"initial_states", lane.initial_states}, {"state_cap", lane.state_cap}, {"proposal_cap", lane.proposal_cap},
        {"last_emission_states", lane.last_emission_states}, {"peak_emission_states", lane.peak_emission_states},
        {"peak_retained_states", lane.peak_retained_states}, {"peak_transition_targets", lane.peak_transition_targets},
        {"pruning_events", lane.pruning_events}, {"states_dropped", lane.states_dropped},
        {"transition_edges", lane.transition_edges}, {"merged_channel_components", lane.merged_channel_components},
        {"basis_calls", lane.basis_calls}, {"basis_rows", lane.basis_rows}, {"positive_basis_blocks", lane.positive_basis_blocks},
        {"projection_modes", lane.projection_modes}, {"gaussian_updates", lane.gaussian_updates},
        {"projected_template_energy", static_cast<double>(lane.projected_template_energy)},
        {"retained_prior_mass", lane.retained_prior_mass}, {"bit_scores", scores(lane.bit_scores)}});
}

Json fine_json(const FineProgress& p) {
    const auto& cache = p.cache;
    const auto& t = p.times;
    return object({{"observation_first_sample", p.observation.first_sample}, {"observation_rows", p.observation.rows},
        {"available_rows", p.available_rows}, {"processed_rows", p.processed_rows}, {"segments", p.segments},
        {"blocks", p.blocks}, {"block_rows", p.block_rows}, {"segment_rows", p.segment_rows},
        {"last_block_rows", p.last_block_rows}, {"requested_invalid_rows", p.requested_invalid_rows},
        {"additional_discarded_rows", p.additional_discarded_rows}, {"erased_blocks", p.erased_blocks},
        {"observed_projection_modes", p.observed_projection_modes}, {"quiet_modes", p.quiet_modes},
        {"quiet_energy", p.quiet_energy}, {"quiet_threshold", p.quiet_threshold}, {"workspace_bytes", p.workspace_bytes},
        {"sample_rate_hz", p.sample_rate_hz}, {"workers", p.workers}, {"stage_workers", p.stage_workers},
        {"tap_count", p.tap_count}, {"bulk", p.bulk ? bulk_json(*p.bulk) : Json{}},
        {"delay_step_seconds", p.delay_step_seconds}, {"rate_step_ppm", p.rate_step_ppm}, {"rate_radius", p.rate_radius},
        {"public_slot", p.public_slot}, {"pattern_epoch", p.pattern_epoch}, {"symbol_ordinal", p.symbol_ordinal},
        {"observation_complete", p.observation_complete}, {"representative_work", p.representative_work},
        {"finished", p.finished}, {"initial_affine", fine_lane(p.initial_affine)}, {"adaptive", fine_lane(p.adaptive)},
        {"cache", object({{"calls", cache.calls}, {"interpolated_rows", cache.interpolated_rows}, {"direct_rows", cache.direct_rows},
            {"prepared_calls", cache.prepared_calls}, {"prepared_mapped_columns", cache.prepared_mapped_columns},
            {"prepared_direct_columns", cache.prepared_direct_columns}, {"prepared_empty_columns", cache.prepared_empty_columns},
            {"cache_hits", cache.cache_hits}, {"cache_misses", cache.cache_misses}, {"evictions", cache.evictions},
            {"native_preparation_calls", cache.native_preparation_calls}, {"native_preparation_samples", cache.native_preparation_samples},
            {"large_phase_fallback_rows", cache.large_phase_fallback_rows}, {"staging_fallback_rows", cache.staging_fallback_rows},
            {"cache_bytes", cache.cache_bytes}, {"peak_cache_bytes", cache.peak_cache_bytes}, {"peak_pinned_tiles", cache.peak_pinned_tiles},
            {"tile_samples", cache.tile_samples}, {"cache_limit_bytes", cache.cache_limit_bytes}, {"dense_factor", cache.dense_factor},
            {"dense_sample_rate_hz", cache.dense_sample_rate_hz}, {"oversampling_ratio", cache.oversampling_ratio},
            {"interpolation_amplitude_proxy", cache.interpolation_amplitude_proxy}, {"fallback_reason", cache.fallback_reason}})},
        {"times", object({{"preparation_seconds", t.preparation_seconds}, {"input_check_seconds", t.input_check_seconds},
            {"observation_seconds", t.observation_seconds}, {"transition_seconds", t.transition_seconds},
            {"bulk_statistics_seconds", t.bulk_statistics_seconds},
            {"dispatch_seconds", t.dispatch_seconds}, {"pruning_seconds", t.pruning_seconds}, {"finish_seconds", t.finish_seconds},
            {"worker_basis_statistics_seconds", t.worker_basis_statistics_seconds},
            {"worker_gaussian_seconds", t.worker_gaussian_seconds}})}});
}

std::size_t standalone_scratch(const StreamReceiverResources& resources) {
    auto remaining = memory_remaining(resources);
    const auto stack = datapump::triad::v3::detail::stream_worker_stack_bytes();
    if (stack && (resources.workers - 1) > remaining / stack)
        throw Incomplete("Native probe worker stacks exceed the local memory limit");
    return remaining - stack * (resources.workers - 1);
}

CoarseResult acquire(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
                     const Capture& capture, const StreamReceiverResources& resources, std::stop_token stop) {
    const auto& config = profile.receiver;
    const auto rows = static_cast<double>(capture.iq.size());
    const auto seconds = rows / config.sample_rate_hz;
    const Timer check;
    bool all_valid = true;
    for (std::size_t i = 0; i < capture.iq.size(); ++i) {
        if (!(i % input_chunk)) check_stop(stop);
        require(std::isfinite(capture.iq[i].real()) && std::isfinite(capture.iq[i].imag()) &&
                capture.valid[i] <= 1, "Native acquisition input is invalid");
        all_valid &= capture.valid[i] != 0;
    }
    phase(report, "input_validation", check.elapsed(), seconds,
          object({{"rows", capture.iq.size()}, {"all_valid", all_valid}}));
    const auto memory = standalone_scratch(resources);
    const auto helper_bound = helper_workspace(config, slot);
    if (helper_bound > memory) throw Incomplete("Native Helper preflight exceeds the local memory lease");
    const Timer helper_time;
    const auto helper = score_helper(config, slot, capture.iq, capture.valid, helper_bound, stop);
    phase(report, "helper", helper_time.elapsed(), seconds, object({
        {"reason", helper.reason}, {"narrowed", helper.narrowed}, {"prefix_samples", helper.prefix_samples},
        {"onset_apertures", helper.onset_apertures}, {"workspace_peak_bytes", helper.workspace_peak_bytes},
        {"retained_intervals", helper.intervals.size()}, {"observation_seconds", helper.frequency_observation_seconds}}));
    if (!all_valid) throw Incomplete("Native full coarse search cannot score an erased observation");
    FrequencyInterval fallback{};
    const auto intervals = public_frequency_intervals(config, helper, fallback);
    const Timer preparation_time;
    CoarseSession coarse(config, slot, profile.key, capture.iq, intervals, resources.workers, memory, stop);
    const auto preparation_elapsed = preparation_time.elapsed();
    auto progress = coarse.progress();
    auto prepared = coarse_json(progress);
    Digest grid;
    grid.add_little(coarse.frequency_grid());
    prepared["frequency_grid_sha256"] = grid.finish();
    report["result"]["acquisition"] = object({{"planned_tiles", progress.plan.total_tiles},
        {"completed_tiles", progress.completed_tiles}, {"remaining_tiles", progress.plan.total_tiles - progress.completed_tiles},
        {"selection_completed", false}});
    phase(report, "coarse_prepare", preparation_elapsed, seconds, std::move(prepared));
    const auto goal = std::min(o.maximum_coarse_tiles, progress.plan.total_tiles);
    unsigned step = 0;
    while (progress.completed_tiles < goal) {
        check_stop(stop);
        const auto tiles = std::min<std::size_t>(goal - progress.completed_tiles, step < 2 ? 1 : step == 2 ? 2 : 64);
        const Timer measuring;
        coarse.advance(tiles);
        const auto measured = measuring.elapsed();
        const auto previous = progress.completed_tiles;
        progress = coarse.progress();
        auto work = coarse_json(progress);
        work["step_tiles"] = progress.completed_tiles - previous;
        work["requested_step_tiles"] = tiles;
        work["step_complete"] = progress.completed_tiles == previous + tiles;
        auto& acquisition = report["result"]["acquisition"];
        acquisition["completed_tiles"] = progress.completed_tiles;
        acquisition["remaining_tiles"] = progress.plan.total_tiles - progress.completed_tiles;
        phase(report, "coarse_tiles", measured, seconds, std::move(work));
        if (progress.completed_tiles != previous + tiles)
            throw Incomplete("Native coarse search interrupted a declared tile");
        ++step;
    }
    if (progress.completed_tiles != progress.plan.total_tiles)
        throw Incomplete("The tile budget ended before complete public acquisition and selection");
    const Timer selection_time;
    auto selected = coarse.finish();
    const auto selected_elapsed = selection_time.elapsed();
    if (!selected) throw Incomplete("Native coarse selection produced no completed result");
    phase(report, "coarse_selection", selected_elapsed, seconds, coarse_selected(*selected));
    report["result"]["acquisition"]["selection_completed"] = true;
    if (selected->coordinates.empty()) throw Incomplete("Complete public acquisition yielded no fine coordinates");
    return std::move(*selected);
}

FineObservation fine_observation(const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
                                 const FineCoordinate& first) {
    if (o.command == "slot")
        return {slot.first_sample, static_cast<std::size_t>(slot.stop_sample - slot.first_sample),
                FineRunKind::operational, true};
    const auto& geometry = profile.waveform.geometry();
    const auto rate = profile.waveform.config().format.sample_rate;
    const auto blocks = fine_block_geometry(rate);
    const auto available = slot.stop_sample - slot.first_sample;
    if (o.stratum == "terminal") {
        // Retain one complete segment and the actual public terminal
        // segment. The crop stays on the production block calendar.
        const auto segment = static_cast<std::uint64_t>(blocks.segment_rows);
        const auto remainder = available % segment;
        const auto rows = std::min(available, segment + (remainder ? remainder : segment));
        return {slot.stop_sample - rows, static_cast<std::size_t>(rows), FineRunKind::probe, true};
    }
    const auto target = o.stratum == "cold-maximum" || o.stratum == "erasure" ?
        2 * static_cast<std::uint64_t>(blocks.segment_rows) : 6 * static_cast<std::uint64_t>(rate);
    const auto rows = o.work_only && o.stratum == "noise" ? std::min(available, target) : target;
    if (rows > available) throw Incomplete("Public observation cannot contain the declared fine work probe");
    if (o.stratum == "active" && geometry.nominal_payload_energy_seconds < 6)
        throw Incomplete("Planned DATA interval cannot support a six-second nominal continuation");
    const long double source_center = slot.source_start_seconds * rate + geometry.calendar_origin_sample +
        .5L * std::min(geometry.tile_seconds, geometry.bit_seconds) * rate;
    const long double center = (slot.window_capture_lead_seconds + first.arrival_seconds) * rate +
        source_center / (1 + first.clock_ppm * 1e-6L);
    const auto origin = static_cast<std::uint64_t>(std::clamp(std::round(center - rows / 2),
        0.L, static_cast<long double>(available - rows)));
    return {slot.first_sample + origin, static_cast<std::size_t>(rows),
            o.stratum == "cold-maximum" ? FineRunKind::cold_maximum : FineRunKind::probe, true};
}

void fine_run(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
              const Capture& capture, const FinePreparation& prepared, double projected_rate, double clock_gap,
              const StreamReceiverResources& resources, std::stop_token stop) {
    const auto& config = profile.receiver;
    const auto extent = fine_observation(o, profile, slot, prepared.coordinates.front());
    const auto offset = static_cast<std::size_t>(extent.first_sample - capture.first);
    const auto iq = std::span(capture.iq).subspan(offset, extent.rows);
    auto valid = std::span(capture.valid).subspan(offset, extent.rows);
    Bytes erasures;
    if (o.stratum == "erasure") {
        erasures.assign(valid.begin(), valid.end());
        const auto block_boundary = fine_block_geometry(config.sample_rate_hz).block_rows;
        require(block_boundary && block_boundary < erasures.size(), "Erasure probe lacks two public blocks");
        erasures[block_boundary - 1] = erasures[block_boundary] = 0;
        valid = erasures;
    }
    Digest input;
    input.add_little(std::span(reinterpret_cast<const double*>(iq.data()), 2 * iq.size()));
    input.add(std::as_bytes(valid));
    const auto digest = input.finish();
    const auto scratch = standalone_scratch(resources);
    const auto planned = fine_workspace(config, slot, extent, resources.workers);
    if (planned > scratch) throw Incomplete("Native fine preflight exceeds the local memory lease");
    const Timer prefix_time;
    auto fine = std::make_unique<FineSession>(config, slot, profile.key, extent, iq, valid,
        prepared.coordinates, projected_rate, clock_gap, resources.workers, planned, stop);
    const auto fs = static_cast<std::size_t>(config.sample_rate_hz);
    const bool continued = o.command == "continuation" && extent.rows == 6 * fs &&
        (o.stratum == "active" || o.stratum == "noise");
    std::size_t previous = 0;
    const std::array checkpoints{continued ? 2 * fs : extent.rows, 3 * fs, 4 * fs, 6 * fs};
    std::uint64_t prefix_fills = 0;
    std::uint64_t affine_positive = 0, adaptive_positive = 0;
    bool complete_checkpoints = true;
    for (unsigned i = 0; i < (continued ? checkpoints.size() : 1); ++i) {
        const auto timer = i == 0 ? prefix_time : Timer{};
        const auto endpoint = checkpoints[i];
        fine->advance_to(endpoint);
        const auto elapsed = timer.elapsed();
        const auto progress = fine->progress();
        if (i == 0) prefix_fills = progress.cache.native_preparation_calls;
        auto work = fine_json(progress);
        work["checkpoint_seconds"] = static_cast<double>(endpoint) / fs;
        work["stratum"] = o.stratum;
        work["input_sha256"] = digest;
        phase(report, continued ? (i == 0 ? "fine_prefix" : "fine_continuation") : "fine_work",
              elapsed, static_cast<double>(endpoint - previous) / fs, std::move(work));
        previous = endpoint;
        complete_checkpoints &= progress.processed_rows == endpoint && progress.representative_work &&
            progress.initial_affine.positive_basis_blocks > affine_positive &&
            progress.adaptive.positive_basis_blocks > adaptive_positive;
        affine_positive = progress.initial_affine.positive_basis_blocks;
        adaptive_positive = progress.adaptive.positive_basis_blocks;
    }
    const Timer finish_time;
    ReceiverSlotScore score;
    fine->finish(score);
    classify_fine_slot(score, prepared.grid);
    // Snapshot construction is diagnostics; exclude it from teardown service.
    const auto finish_elapsed = finish_time.elapsed();
    const auto final_progress = fine->progress();
    const Timer teardown_time;
    fine.reset();
    const auto teardown = teardown_time.elapsed();
    const Timing total_finish{finish_elapsed.wall + teardown.wall,
        finish_elapsed.cpu && teardown.cpu ? std::optional{*finish_elapsed.cpu + *teardown.cpu} : std::nullopt};
    auto final_work = fine_json(final_progress);
    final_work["stratum"] = o.stratum;
    final_work["input_sha256"] = digest;
    phase(report, "fine_finish", total_finish, 0, std::move(final_work));
    const auto qualifying_cache_progress = final_progress.cache.native_preparation_calls > prefix_fills;
    const bool cold_covered = final_progress.initial_affine.initial_states == 256 &&
        final_progress.adaptive.initial_states == 256 && final_progress.adaptive.transition_edges > 0 &&
        final_progress.segments == 2;
    const bool erased = final_progress.requested_invalid_rows == 2 && final_progress.erased_blocks >= 2;
    const bool work_complete = final_progress.observation_complete &&
        (final_progress.representative_work || (o.stratum == "terminal" && final_progress.blocks > 0)) &&
        (o.stratum != "active" || !continued || (complete_checkpoints && qualifying_cache_progress)) &&
        (o.stratum != "cold-maximum" || cold_covered) && (o.stratum != "erasure" || erased);
    auto result = object({{"score", score_json(score)}, {"stratum", o.stratum},
        {"work_complete", work_complete}, {"checkpoints_complete", complete_checkpoints},
        {"continued_cache_fill", qualifying_cache_progress}, {"maximum_state_transition_covered", cold_covered},
        {"explicit_erasure_covered", erased}, {"independent_holdout_requires_fresh_seed", true},
        {"coordinate_scope", o.work_only ? "declared_public_work_coordinate" : "complete_public_selection"},
        {"receiver_decision_eligible", extent.kind == FineRunKind::operational},
        {"complete_receiver_service", false}});
    if (extent.kind == FineRunKind::operational) {
        const bool expected = o.stratum == "noise" ? score.decision == SlotDecision::quiet :
            score.decision == SlotDecision::data && score.bit == o.bit;
        result["evaluator_expected_decision_observed"] = expected;
    }
    report["result"] = result;
    if (!work_complete)
        throw Incomplete("Native fine probe did not cover its declared work or continuation cache/state gate");
}

void generation_phase(Json& report, const Profile& profile, const Capture& capture) {
    phase(report, "simulator", capture.generation, capture.generated_samples / profile.receiver.sample_rate_hz,
        object({{"generated_rows", capture.generated_samples}, {"retained_rows", capture.rows()},
                {"retained_ram_rows", capture.iq.size()}, {"spool_bytes", capture.spool ? capture.spool->bytes() : 0},
                {"generation_preflight_reserved_bytes", capture.generation_reserved_bytes},
                {"channel_workspace_bytes", capture.channel.working_bytes},
                {"has_selective_filter", capture.channel.selective_perturbation.has_value()}}), 0, "simulator_excluded");
    if (capture.spool) phase(report, "capture_spool_write", capture.spool_write,
        capture.rows() / profile.receiver.sample_rate_hz,
        object({{"rows", capture.rows()}, {"spool_bytes", capture.spool->bytes()},
                {"spool_working_bytes", capture.spool->working_bytes()}, {"backing_kind", capture.spool->backing_kind()},
                {"spool_residency_reserved_bytes", capture.spool->resident_reservation_bytes()}}), 0,
        "subset_of_excluded_simulator_wall");
}

void slot_run(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
              const StreamReceiverResources& resources, std::uint64_t& samples_left, std::stop_token stop) {
    const std::array bit{static_cast<std::uint8_t>(o.bit)};
    const bool quiet = o.stratum == "noise";
    const std::array emissions{EmissionOptions{.mode = quiet ? EmissionMode::Quiet : EmissionMode::Message,
        .first_slot = slot.index, .bits = quiet ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>(bit),
        .stop = stop, .bit_at = {}}};
    const auto capture = simulate(profile, emissions, slot.first_sample, slot.stop_sample, samples_left, resources, stop);
    report["observation"] = observation(capture, slot.stop_sample, slot, o);
    generation_phase(report, profile, capture);
    if (o.command == "regression") {
        const auto represented = capture.iq.size() / profile.receiver.sample_rate_hz;
        for (unsigned unit = 0; unit < o.units; ++unit) {
            check_stop(stop);
            // The public workspace includes the IQ lease already held by the
            // probe. The remaining process allowance covers DSP and stacks.
            const auto workspace = receiver_slot_workspace(profile.receiver, slot, resources.workers);
            const auto lease_bytes = capture.iq.size() * (sizeof(Complex) + 1);
            if (workspace < lease_bytes || workspace - lease_bytes > standalone_scratch(resources))
                throw Incomplete("Native regression unit exceeds its local memory lease");
            const Timer began;
            const auto score = score_receiver_slot(profile.receiver, slot, profile.key, capture.iq,
                capture.valid, resources.workers, workspace, stop);
            const auto elapsed = began.elapsed();
            auto work = score_json(score);
            Digest digest;
            const std::array<std::uint64_t, 14> fixed{score.index, score.consumed_samples, score.coarse_workspaces,
                score.coarse_frequency_points, score.coarse_arrival_points, score.fine_candidates,
                score.affine_blocks, score.adaptive_blocks, score.adaptive_peak_states, score.adaptive_transition_edges,
                score.basis_rows, score.quiet_modes, static_cast<std::uint64_t>(score.decision), score.bit.value_or(2)};
            digest.add_little(std::span<const std::uint64_t>(fixed));
            work["work_identity_sha256"] = digest.finish();
            phase(report, "native_slot", elapsed, represented, std::move(work), unit);
            const bool matches = quiet ? score.decision == SlotDecision::quiet :
                score.decision == SlotDecision::data && score.bit == o.bit;
            if (!score.complete || !matches)
                throw Incomplete("Native regression fixture missed its declared DATA or QUIET decision");
            frontend(report, profile, capture, stop, unit);
        }
        report["result"] = object({{"units", o.units}, {"stratum", o.stratum}, {"work_complete", true},
                                    {"evaluator_expected_decision_observed", true}, {"complete_receiver_service", false}});
        return;
    }
    frontend(report, profile, capture, stop);
    const auto coarse = acquire(report, o, profile, slot, capture, resources, stop);
    const auto prepared = prepare_fine_coordinates(profile.receiver, slot, coarse);
    fine_run(report, o, profile, slot, capture, prepared, coarse.projected_sample_rate_hz,
        coarse.clock_gap_ppm, resources, stop);
}

void diagnostic_fine(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
                     const StreamReceiverResources& resources, std::uint64_t& samples_left,
                     std::stop_token stop) {
    const auto& config = profile.receiver;
    const auto geometry = coarse_fine_geometry(config, slot);
    AdmissionGrid grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds, config.frequency_bound_hz,
        config.clock_bound_ppm, std::min(.01, geometry.maximum_frequency_step_hz));
    const double arrival = grid.arrival_intervals == 0 ? grid.lower :
        grid.lower + std::floor(grid.arrival_intervals / 2) * (grid.upper - grid.lower) / grid.arrival_intervals;
    const FineCoordinate coordinate{0, arrival, 0, 0};
    const FinePreparation prepared{grid, {coordinate}};
    const auto extent = fine_observation(o, profile, slot, coordinate);
    // A short native noise draw supplies this independently scoped work
    // probe. It carries no purported channel history from previous slots.
    const std::array emissions{EmissionOptions{.mode = EmissionMode::Quiet, .bits = {}, .stop = stop, .bit_at = {}}};
    auto capture = simulate(profile, emissions, 0, extent.rows, samples_left, resources, stop);
    capture.first = extent.first_sample;
    report["observation"] = observation(capture, extent.first_sample + extent.rows, slot, o);
    report["observation"]["channel_scope"] = "independently_seeded_native_thermal_noise_at_local_crop";
    report["observation"]["evaluator_bit"] = Json{};
    report["observation"]["work_coordinate"] = coordinate_json(coordinate);
    generation_phase(report, profile, capture);
    frontend(report, profile, capture, stop);
    fine_run(report, o, profile, slot, capture, prepared, geometry.projected_sample_rate_hz,
        geometry.clock_gap_ppm, resources, stop);
}

NativeMessageProfile message_profile(const Options& o, const Profile& profile) {
    return {o.fec, o.compression, native_message_editor_limit,
            o.encrypt ? std::optional{profile.key} : std::nullopt};
}

constexpr std::array<std::uint8_t, 1> stream_source{'e'};

Json stream_plan(const Options& o, const Profile& profile, const StreamReceiverResources& resources) {
    const auto wire = prepare_text_message(stream_source, message_profile(o, profile)).wire_bits();
    const auto message_slots = wire + 2;
    const auto minimum = 2 * message_slots;
    const auto declared = o.command == "message" ? message_slots : o.slots;
    const auto generated = make_receiver_slot_plan(profile.waveform, profile.receiver, declared - 1).stop_sample;
    const auto begin = declared / 3;
    const auto end = 2 * declared / 3;
    const auto maximum_bytes = maximum_spool_bytes(o);
    const auto queue = datapump::triad::v3::detail::stream_uncommitted_slot_limit(resources.slot_owners);
    return object({{"requested_slots", o.slots}, {"planned_scored_slots", declared},
        {"minimum_backlog_slots", minimum}, {"message_slots", message_slots},
        {"generated_samples", generated}, {"retained_ram_rows", 0},
        {"spool_bytes", generated <= UINT64_MAX / ObservationSpool::row_bytes ?
            Json(generated * ObservationSpool::row_bytes) : Json{}},
        {"maximum_spool_bytes", maximum_bytes},
        {"fits_sample_budget", generated <= o.sample_limit},
        {"fits_spool_budget", generated <= maximum_bytes / ObservationSpool::row_bytes},
        {"backlog_begin", begin}, {"backlog_end", end}, {"interval_commits", end - begin},
        {"maximum_startup_carryover_slots", queue - 1}});
}

const char* stream_state(StreamReceiverState state) {
    switch (state) {
    case StreamReceiverState::receiving: return "receiving";
    case StreamReceiverState::input_ended: return "input_ended";
    case StreamReceiverState::cancelled: return "cancelled";
    case StreamReceiverState::resource_limit: return "resource_limit";
    case StreamReceiverState::failed: return "failed";
    }
    return "unknown";
}

struct MessageCheck {
    std::uint64_t first = 0, stop = 0;
    bool physical_complete = false, matches = false, long_form = false;
    std::size_t observed_bits = 0, missing_slots = 0, trailing_quiet = 0;
    double codec_seconds = 0;
};

struct Replay {
    Timing service, spool_read;
    StreamReceiverReport final;
    std::vector<StreamSlotReceipt> slots;
    std::vector<MessageCheck> messages;
    std::size_t backpressure = 0, peak_uncommitted = 0, maximum_pending_messages = 0;
    std::size_t peak_owners = 0;
    std::uint64_t input_rows = 0;
    std::string verified_input_sha256;
    std::optional<double> controller_thread_cpu_seconds;
};

Replay replay(const Options& o, const Profile& profile, const Capture& capture, std::uint64_t count,
              unsigned slot_count, std::span<const std::uint8_t> expected, const StreamReceiverResources& resources,
              std::stop_token stop) {
    require(capture.first == 0 && capture.spool && count <= capture.rows(), "Invalid replay prefix");
    const Timer timer;
    const auto controller_cpu_start = thread_cpu_seconds();
    SpoolReplay input(*capture.spool);
    Replay run;
    run.input_rows = count;
    run.slots.reserve(slot_count);
    run.messages.reserve(2);
    {
        StreamReceiverOptions stream;
        stream.workers = resources.workers;
        stream.slot_owners = resources.slot_owners;
        stream.memory_limit_bytes = resources.memory_limit_bytes;
        stream.maximum_input_chunk_samples = input_chunk;
        stream.maximum_gap_samples = input_chunk;
        stream.recent_slot_receipts = std::min(1024U, std::max(32U, slot_count));
        stream.record_commit_timing = true;
        run.maximum_pending_messages = stream.maximum_pending_messages;
        CompleteStreamReceiver receiver(profile.waveform, profile.receiver, profile.key, stream, message_profile(o, profile));
        std::stop_callback interrupt(stop, [&] { receiver.request_cancel(); });
        const auto drain = [&] {
            const auto state = receiver.report();
            run.peak_uncommitted = std::max(run.peak_uncommitted, state.uncommitted_slots);
            run.peak_owners = std::max(run.peak_owners, state.active_owners);
            for (auto& row : receiver.drain_recent_slots()) {
                if (run.slots.size() == slot_count)
                    throw Error("Native stream scored beyond the declared observation horizon");
                if (row.commit_timing) {
                    run.peak_uncommitted = std::max(run.peak_uncommitted, row.commit_timing->uncommitted_slots);
                    run.peak_owners = std::max(run.peak_owners, static_cast<std::size_t>(row.commit_timing->active_owners));
                }
                run.slots.push_back(std::move(row));
            }
            for (const auto& event : receiver.drain_messages()) {
                const auto& message = event.message;
                const auto& decoded = message.decoded_content;
                run.messages.push_back({event.first_public_slot, event.stop_public_slot, message.physical_complete,
                    decoded && std::ranges::equal(decoded->message.data, expected), message.long_form,
                    message.observed_bits, message.missing_slots, message.trailing_quiet_slots.size(),
                    message.message_service_seconds});
            }
        };
        for (std::uint64_t first = 0; first < count;) {
            check_stop(stop);
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(count - first, input_chunk));
            input.load(first, n);
            if (receiver.push(first, input.iq(), input.valid()) == StreamPushResult::accepted) {
                first += n;
                drain();
            } else {
                ++run.backpressure;
                // push() can make progress while discovering backpressure;
                // snapshot the rejection itself before draining anything.
                const auto rejected = receiver.report();
                drain();
                auto after = receiver.report();
                if (!rejected.output_backpressured) {
                    after = receiver.wait_for_slot_completion();
                    drain();
                    after = receiver.report();
                }
                const bool output_drained = rejected.output_backpressured && !after.output_backpressured;
                if (!output_drained && rejected.state == after.state && after.active_owners == 0 &&
                    after.next_public_slot == rejected.next_public_slot &&
                    after.uncommitted_slots == rejected.uncommitted_slots &&
                    !after.output_backpressured)
                    throw Incomplete("Native stream cannot admit the next bounded input chunk");
            }
            const auto state = receiver.report();
            if (state.state != StreamReceiverState::receiving) throw Incomplete(state.reason);
        }
        run.final = receiver.finish_input();
        drain();
        while (run.final.state == StreamReceiverState::receiving) {
            check_stop(stop);
            const auto previous = run.final.next_public_slot;
            run.final = receiver.resume(true);
            drain();
            if (run.final.state == StreamReceiverState::receiving && run.final.next_public_slot == previous &&
                !run.final.active_owners) throw Incomplete("Native stream drain made no progress");
        }
    }
    run.verified_input_sha256 = input.finish(capture, count);
    run.spool_read = input.timing();
    run.service = timer.elapsed();
    const auto controller_cpu_end = thread_cpu_seconds();
    if (controller_cpu_start && controller_cpu_end && *controller_cpu_end >= *controller_cpu_start)
        run.controller_thread_cpu_seconds = *controller_cpu_end - *controller_cpu_start;
    check_stop(stop);
    return run;
}

bool stream_work_complete(const Replay& run, const Profile& profile, std::size_t slots) {
    if (run.final.state != StreamReceiverState::input_ended || run.final.input_stop != run.input_rows ||
        run.slots.size() != slots || run.final.next_public_slot != slots) return false;
    for (std::size_t i = 0; i < run.slots.size(); ++i) {
        const auto& row = run.slots[i];
        const auto address = message_address(profile.waveform, i);
        if (!row.score.complete || !row.score.all_valid || row.score.index != i ||
            row.pattern_epoch != address.epoch || row.symbol_ordinal != address.ordinal) return false;
    }
    return true;
}

Json replay_json(const Replay& run, const Profile& profile, std::size_t expected_slots,
                 std::size_t spool_reservation) {
    Json::Array slots, messages;
    for (const auto& receipt : run.slots) {
        auto row = score_json(receipt.score);
        row["pattern_epoch"] = receipt.pattern_epoch;
        row["symbol_ordinal"] = receipt.symbol_ordinal;
        row["workers"] = receipt.workers;
        if (receipt.commit_timing) {
            const auto& time = *receipt.commit_timing;
            row["commit"] = object({{"wall_seconds", time.wall_seconds}, {"input_stop", time.input_stop},
                {"uncommitted_slots", time.uncommitted_slots}, {"active_owners", time.active_owners},
                {"next_ready", time.next_ready},
                {"process_cpu_seconds", time.process_cpu_seconds ? Json(*time.process_cpu_seconds) : Json{}}});
        }
        slots.push_back(std::move(row));
    }
    for (const auto& row : run.messages) {
        messages.push_back(object({{"first_public_slot", row.first}, {"stop_public_slot", row.stop},
            {"physical_complete", row.physical_complete}, {"expected_source_matches", row.matches},
            {"long_form", row.long_form}, {"observed_bits", row.observed_bits}, {"missing_slots", row.missing_slots},
            {"trailing_quiet_slots", row.trailing_quiet}, {"message_service_seconds", row.codec_seconds}}));
    }
    const double baseband = run.input_rows / profile.receiver.sample_rate_hz;
    return object({{"state", stream_state(run.final.state)}, {"reason", run.final.reason},
        {"work_complete", stream_work_complete(run, profile, expected_slots)},
        {"input_rows", run.input_rows}, {"unique_baseband_seconds", baseband},
        {"verified_input_sha256", run.verified_input_sha256},
        {"spool_read_and_verification_seconds", run.spool_read.wall},
        {"spool_read_bytes", run.input_rows * ObservationSpool::row_bytes},
        {"slots", std::move(slots)}, {"messages", std::move(messages)},
        {"peak_reserved_bytes", run.final.peak_reserved_bytes + spool_reservation},
        {"receiver_peak_reserved_bytes", run.final.peak_reserved_bytes},
        {"spool_residency_reserved_bytes", spool_reservation},
        {"receiver_memory_limit_bytes", run.final.resources.memory_limit_bytes},
        {"total_application_memory_limit_bytes", run.final.resources.memory_limit_bytes + spool_reservation},
        {"peak_uncommitted_slots", run.peak_uncommitted},
        {"peak_active_owners", run.peak_owners},
        {"peak_running_owners", run.final.peak_running_owners},
        {"peak_executor_workers", run.final.peak_executor_workers},
        {"maximum_pending_messages", run.maximum_pending_messages},
        {"backpressure_retries", run.backpressure}, {"next_public_slot", run.final.next_public_slot},
        {"committed_slots", run.slots.size()}, {"input_closed", run.final.input_closed},
        {"wall_seconds", run.service.wall}, {"process_cpu_seconds", run.service.cpu ? Json(*run.service.cpu) : Json{}},
        {"controller_thread_cpu_seconds", run.controller_thread_cpu_seconds ? Json(*run.controller_thread_cpu_seconds) : Json{}},
        {"controller_cv_wait_seconds", run.final.controller_wait_seconds ? Json(*run.final.controller_wait_seconds) : Json{}},
        {"mean_busy_hardware_fraction", run.service.cpu && run.service.wall > 0 ?
            Json(*run.service.cpu / run.service.wall / run.final.resources.hardware_threads) : Json{}}});
}

Json backlog_interval(const Replay& run, const Profile& profile, unsigned begin, unsigned end) {
    auto result = object({{"start_after_public_slot", begin}, {"stop_after_public_slot", end},
        {"complete", false}, {"process_cpu_seconds", Json{}}, {"reason", "predeclared_commit_boundaries_not_observed"}});
    if (begin >= end || end >= run.slots.size()) return result;
    const auto& a = run.slots[begin];
    const auto& b = run.slots[end];
    if (a.score.index != begin || b.score.index != end || !a.commit_timing || !b.commit_timing) return result;
    const auto& start = *a.commit_timing;
    const auto& stop = *b.commit_timing;
    if (start.input_stop < b.score.stop_sample) {
        result["reason"] = "all_interval_input_was_not_ready_at_start";
        return result;
    }
    if (end - begin <= start.uncommitted_slots) {
        result["reason"] = "interval_could_be_explained_by_startup_carryover";
        return result;
    }
    const double elapsed = stop.wall_seconds - start.wall_seconds;
    if (!(elapsed > 0)) {
        result["reason"] = "invalid_commit_elapsed";
        return result;
    }
    const auto& calendar = profile.waveform.calendar();
    const auto from = profile.waveform.transmit_slot(begin);
    const auto to = profile.waveform.transmit_slot(end);
    const auto represented = static_cast<double>(calendar.relative_seconds(from.pattern_begin, to.pattern_begin));
    result["complete"] = true;
    result["reason"] = "predeclared_ready_input_commit_interval";
    result["wall_seconds"] = elapsed;
    result["unique_baseband_seconds"] = represented;
    result["service_seconds_per_baseband_second"] = elapsed / represented;
    result["commits"] = end - begin;
    result["initial_uncommitted_slots"] = start.uncommitted_slots;
    if (start.process_cpu_seconds && stop.process_cpu_seconds &&
        *stop.process_cpu_seconds >= *start.process_cpu_seconds && run.final.resources.hardware_threads) {
        const auto cpu = *stop.process_cpu_seconds - *start.process_cpu_seconds;
        result["process_cpu_seconds"] = cpu;
        result["busy_hardware_fraction"] = cpu / elapsed / run.final.resources.hardware_threads;
    }
    return result;
}

void stream_run(Json& report, const Options& o, const Profile& profile, const StreamReceiverResources& resources,
                std::uint64_t& samples_left, std::stop_token stop) {
    require(o.slot_index == 0, "Whole-stream probes use the configured calendar anchor");
    const auto draft = prepare_text_message(stream_source, message_profile(o, profile));
    const auto wire = draft.wire_bits();
    const bool backlog = o.command == "backlog";
    const unsigned episode_count = backlog ? 2 : 1;
    const auto expected_slots = episode_count * (wire + 2);
    if (expected_slots > o.slots) throw Incomplete("Message and two quiet slots exceed the declared slot budget");
    const auto slot_count = backlog ? o.slots : static_cast<unsigned>(expected_slots);
    const unsigned backlog_begin = slot_count / 3, backlog_end = 2 * slot_count / 3;
    const auto last = make_receiver_slot_plan(profile.waveform, profile.receiver, slot_count - 1);
    if (last.stop_sample > samples_left)
        throw Incomplete("Canonical capture exceeds the declared probe sample budget");
    const auto maximum_bytes = maximum_spool_bytes(o);
    if (last.stop_sample > maximum_bytes / ObservationSpool::row_bytes)
        throw Incomplete("Canonical capture exceeds the declared spool byte budget");
    if (sizeof(ObservationSpool) > memory_remaining(resources))
        throw Incomplete("Temporary capture spool buffers exceed the local probe memory limit");
    auto spool = std::make_unique<ObservationSpool>(last.stop_sample, maximum_bytes);
    const auto resident_bytes = spool->resident_reservation_bytes();
    report["budget"]["spool_backing_kind"] = spool->backing_kind();
    report["budget"]["spool_residency_reserved_bytes"] = resident_bytes;
    report["budget"]["receiver_memory_limit_bytes"] = Json{};
    if (resident_bytes >= resources.memory_limit_bytes)
        throw Incomplete("Temporary capture backing exceeds the total application memory limit");
    const auto spool_reservation = static_cast<std::size_t>(resident_bytes);
    const auto after_spool = [spool_reservation](StreamReceiverResources allocation) {
        allocation.memory_limit_bytes -= spool_reservation;
        return allocation;
    };
    const auto receiver_resources = after_spool(resources);
    report["budget"]["receiver_memory_limit_bytes"] = receiver_resources.memory_limit_bytes;
    std::vector<StreamReceiverResources> pilots;
    for (const auto workers : o.pilot_workers) {
        if (workers == resources.workers) continue;  // The base run measures this allocation.
        StreamReceiverOptions pilot;
        pilot.workers = workers;
        pilot.slot_owners = o.owners;
        pilot.memory_limit_bytes = resources.memory_limit_bytes;
        pilots.push_back(resolve_stream_receiver_resources(pilot));
    }
    // Leave space for slot/evaluator receipts and bounded projection/replay
    // buffers outside the receiver's separately admitted DSP leases.
    const auto retained_bound = slot_count * (1 + pilots.size()) * 16 * 1024;
    if (retained_bound > memory_remaining(receiver_resources))
        throw Incomplete("Native stream receipt preflight exceeds the local probe memory limit");
    std::vector<EmissionOptions> emissions;
    for (unsigned episode = 0; episode < episode_count; ++episode) {
        const std::uint64_t first = episode * (wire + 2);
        const auto tx = profile.waveform.transmit_slot(first);
        const auto source_first = profile.waveform.calendar().distance(profile.waveform.origin(), tx.helper_begin);
        emissions.push_back({.mode = EmissionMode::Message, .first_slot = first, .bits = {}, .slot_count = wire,
            .source_first_sample = source_first, .stop = stop,
            .bit_at = draft.bit_source(first, [&](std::uint64_t index) { return message_address(profile.waveform, index); })});
    }
    const auto capture = simulate(profile, emissions, 0, last.stop_sample, samples_left, receiver_resources, stop,
                                  std::move(spool));
    const auto first = make_receiver_slot_plan(profile.waveform, profile.receiver, 0);
    report["observation"] = observation(capture, last.stop_sample, first, o);
    report["observation"]["evaluator_bit"] = Json{};
    report["observation"]["expected_episodes"] = episode_count;
    report["observation"]["wire_bits_per_episode"] = wire;
    generation_phase(report, profile, capture);
    frontend(report, profile, capture, stop);
    const auto active = replay(o, profile, capture, last.stop_sample, slot_count, stream_source, receiver_resources, stop);
    auto receipt = replay_json(active, profile, slot_count, spool_reservation);
    const auto measured = object({{"committed_slots", active.slots.size()}, {"input_rows", active.input_rows},
        {"verified_input_sha256", active.verified_input_sha256}, {"allocation", allocation(resources)},
        {"spool_read_and_verification_seconds", active.spool_read.wall}});
    phase(report, "receiver_service", active.service, active.input_rows / profile.receiver.sample_rate_hz, measured,
        0, "native_iq_replay_including_bounded_spool_reads_and_verification");
    const auto excluded = active.final.controller_wait_seconds.value_or(0) + active.spool_read.wall;
    if (active.final.controller_wait_seconds && excluded <= active.service.wall) {
        const auto active_wall = active.service.wall - excluded;
        // Thread CPU includes reads. Only the disjoint caller wall scopes
        // above are subtracted, and remaining wall can overlap worker DSP.
        phase(report, "controller_service", {active_wall, {}}, active.input_rows / profile.receiver.sample_rate_hz,
            object({{"committed_slots", active.slots.size()}, {"input_rows", active.input_rows},
                {"cv_wait_seconds", *active.final.controller_wait_seconds},
                {"spool_read_and_verification_seconds", active.spool_read.wall},
                {"thread_cpu_seconds", active.controller_thread_cpu_seconds ? Json(*active.controller_thread_cpu_seconds) : Json{}},
                {"thread_cpu_includes_spool_work", true}, {"may_overlap_slot_dsp", true}}),
            0, "controller_wall_excluding_instrumented_cv_wait_and_spool_io");
    }
    const bool work_complete = stream_work_complete(active, profile, slot_count);
    bool physical_correct = active.messages.size() == episode_count && work_complete;
    for (std::size_t i = 0; i < active.slots.size(); ++i) {
        const auto& row = active.slots[i];
        const bool data = i < expected_slots && i % (wire + 2) < wire;
        physical_correct &= row.score.decision == (data ? SlotDecision::data : SlotDecision::quiet);
    }
    for (std::size_t i = 0; i < active.messages.size(); ++i) {
        const auto& row = active.messages[i];
        physical_correct &= row.first == i * (wire + 2) && row.stop == (i + 1) * (wire + 2) &&
            row.physical_complete && row.matches && row.observed_bits == wire && row.trailing_quiet == 2;
    }
    auto result = object({{"stream", std::move(receipt)}, {"expected_message_count", episode_count},
        {"message_and_physical_quiet_verified", physical_correct}, {"configured_calendar_reacquired", backlog && physical_correct},
        {"complete_receiver_service", work_complete}, {"scope", "configured_public_iq_stream"}});
    if (backlog) {
        result["backlog_interval"] = backlog_interval(active, profile, backlog_begin, backlog_end);
        Json::Array replays;
        for (const auto& pilot : pilots) {
            try {
                const auto run = replay(o, profile, capture, last.stop_sample, slot_count, stream_source, after_spool(pilot), stop);
                replays.push_back(object({{"allocation", allocation(pilot)},
                    {"stream", replay_json(run, profile, slot_count, spool_reservation)},
                    {"backlog_interval", backlog_interval(run, profile, backlog_begin, backlog_end)}}));
            } catch (const Incomplete& error) {
                replays.push_back(object({{"allocation", allocation(pilot)}, {"stream", object({
                    {"work_complete", false}, {"reason", error.what()}})}, {"backlog_interval", object({{"complete", false}})}}));
            }
        }
        result["allocation_pilots"] = std::move(replays);
    } else {
        // Stop after one actual DATA observation and before the first quiet
        // observation. Decoder and scheduler must leave this episode open.
        const auto partial = replay(o, profile, capture, first.stop_sample, 1, stream_source, receiver_resources, stop);
        const bool eof_rejected = stream_work_complete(partial, profile, 1) &&
            partial.slots.front().score.decision == SlotDecision::data && partial.messages.empty();
        phase(report, "eof_control", partial.service, partial.input_rows / profile.receiver.sample_rate_hz,
            replay_json(partial, profile, 1, spool_reservation), 0, "separate_functional_control");
        result["eof_control_verified"] = eof_rejected;
        physical_correct &= eof_rejected;
    }
    report["result"] = std::move(result);
    if (!work_complete) throw Incomplete("Native stream missed required valid scoring work or public chronology");
    if (!backlog && !physical_correct) throw Incomplete("Native stream missed the declared message, quiet or EOF control");
}

void codec_run(Json& report, const Options& o, const Profile& profile, std::stop_token stop) {
    Bytes bytes(180);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>('A' + (7 * i + i / 13) % 26);
    const auto policy = message_profile(o, profile);
    const Timer prepare;
    const auto draft = prepare_text_message(bytes, policy);
    const auto encoded = prepare.elapsed();
    if (!draft.long_form() || draft.wire_bits() % message_interval_bits)
        throw Error("Native long-codec fixture did not select fixed intervals");
    const auto address = [&](std::uint64_t index) { return message_address(profile.waveform, index); };
    const Timer transmission;
    auto bit_at = draft.bit_source(0, address);
    Bytes wire;
    wire.reserve(draft.wire_bits());
    for (std::size_t i = 0; i < draft.wire_bits(); ++i) {
        if (!(i % message_marker_bits)) check_stop(stop);
        wire.push_back(bit_at(i));
    }
    const auto tx = transmission.elapsed();
    const Timer decoding;
    ContinuousMessageReceiver receiver(policy, address, 0, 1);
    for (std::uint64_t i = 0; i < wire.size() + 2; ++i) {
        if (!(i % message_marker_bits)) check_stop(stop);
        // This isolated codec check supplies local fixed test receipts. It
        // measures marker, RS, source and quiet-commit work with no DSP.
        const auto data = i < wire.size();
        ReceiverSlotScore slot;
        slot.index = i;
        slot.first_sample = i * 100;
        slot.stop_sample = slot.first_sample + 100;
        slot.quiet_first_sample = slot.first_sample + 10;
        slot.quiet_stop_sample = slot.stop_sample - 10;
        slot.consumed_samples = 100;
        slot.sample_rate_hz = 100;
        slot.complete = slot.all_valid = slot.zero_h0_verified = slot.noise_statistics_valid = true;
        slot.operational_passes_conditional_bound = data;
        slot.operational_log_e = 30;
        slot.operational_threshold = 20;
        slot.quiet_energy = 1;
        slot.quiet_threshold = 2;
        slot.quiet_modes = 20;
        slot.decision = data ? SlotDecision::data : SlotDecision::quiet;
        if (data) slot.bit = wire[static_cast<std::size_t>(i)];
        if (!receiver.consume(slot)) throw Error("Native long-codec fixture unexpectedly backpressured");
    }
    const auto messages = receiver.drain_messages();
    const auto rx = decoding.elapsed();
    const bool correct = messages.size() == 1 && messages.front().message.physical_complete &&
        messages.front().message.long_form && messages.front().message.decoded_content &&
        messages.front().message.decoded_content->message.data == bytes;
    auto result = object({{"wire_bits", wire.size()}, {"marker_intervals", wire.size() / message_interval_bits},
        {"source_bytes", bytes.size()}, {"fec", fec_name(o.fec)}, {"decoded_after_two_quiet", correct},
        {"complete_receiver_service", false}, {"scope", "isolated_native_long_codec_receipts"}});
    phase(report, "codec_prepare", encoded, 0, result, 0, "isolated_codec");
    phase(report, "codec_emit", tx, 0, result, 0, "isolated_codec");
    phase(report, "codec_receive", rx, 0, result, 0, "isolated_codec");
    report["result"] = std::move(result);
    if (!correct) throw Incomplete("Native long codec failed its declared fixed-interval fixture");
}

void help() {
    std::cout << "triad_receiver_probe describe|plan|slot|continuation|message|backlog|regression|codec [--flag value ...]\n"
        "  --backend cpu|tensor-cpu|cuda --require-accelerator on|off --tensor-precision automatic|fp64\n"
        "  --device-index N --device-memory-limit-bytes N\n"
        "  --case hf|eme and the normalized manual-case options from triad_cases.py\n"
        "  --workers N --slot-owners N --memory-limit-bytes N --max-samples N --deadline-ms N\n"
        "  --max-spool-bytes N --pilot-workers W1,W2,W3 (CPU backlog replays)\n"
        "  --coarse-tiles N --seed N --slot-index N --stratum active|noise|cold-maximum|terminal|erasure\n"
        "  --work-only on uses a non-admitting local noise crop for nonnominal continuation strata\n"
        "  --slots N --units 1|2|4 --bit 0|1 --sample-rate HZ --data-if HZ\n"
        "  --target-cn0 DB_HZ --cn0 DB_HZ --seconds FIXED_ACTIVE_SECONDS --fec off|rs20|rs60\n"
        "JSON output uses datapump.triad.native-probe.v1. Exit 2 indicates unavailable or incomplete work.\n";
}

Json identity() {
    return object({{"implementation", "native-production-receiver"},
        {"source_manifest_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
        {"source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},
        {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER}, {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG},
        {"openssl_runtime", OpenSSL_version(OPENSSL_VERSION)},
        {"build_options_sha256", DATAPUMP_TRIAD_PROBE_BUILD_SHA},
        {"configured_tensor_backend",
#ifdef DATAPUMP_HAVE_TRIAD_TENSOR
            true
#else
            false
#endif
        }});
}

void run(Json& report, const Options& o, std::stop_token stop) {
    report["identity"]["backend"] = o.backend;
    report["identity"]["receiver_arithmetic"] = "native_fp64_state_and_admission";
    report["identity"]["requested_tensor_precision"] = o.tensor_precision;
    const auto backend = selected_backend(o);
    const auto describe_backend = [&] {
        report["identity"]["backend_receipt"] = backend ? bulk_json(backend->receipt()) : Json{};
    };
    describe_backend();
    StreamReceiverOptions allocation_options;
    allocation_options.workers = o.workers;
    allocation_options.slot_owners = o.owners;
    allocation_options.memory_limit_bytes = o.memory_limit;
    const auto resources = resolve_stream_receiver_resources(allocation_options);
    report["allocation"] = allocation(resources);
    report["budget"] = object({{"maximum_generated_samples", o.sample_limit}, {"deadline_ms", o.deadline_ms},
        {"maximum_spool_bytes", maximum_spool_bytes(o)},
        {"maximum_coarse_tiles", o.maximum_coarse_tiles}, {"maximum_stream_slots", o.slots},
        {"maximum_repeated_units", o.units}, {"configured_calendar_scope", true}});
    if (o.command == "describe") {
        report["result"] = object({{"available", true}, {"actual_device_work", false}});
        return;
    }
    try {
        auto settings = settings_for(o, resources.memory_limit_bytes);
        const Timer preparation;
        auto profile = live::detail::prepare_profile(settings, *settings.calendar, stop,
            {.simulation_seed = o.seed, .fixture_active_seconds = o.active_seconds});
        const auto preparation_elapsed = preparation.elapsed();
        profile.receiver.bulk_backend = backend;
        const auto slot = make_receiver_slot_plan(profile.waveform, profile.receiver, o.slot_index);
        report["configuration"] = profile_json(o, profile, slot, resources);
        const auto retained = slot.stop_sample - slot.first_sample;
        const auto bytes = retained <= UINT64_MAX / ObservationSpool::row_bytes ?
            Json(retained * ObservationSpool::row_bytes) : Json{};
        report["configuration"]["slot_plan"] = object({{"generated_samples", slot.stop_sample},
            {"retained_samples", retained}, {"retained_bytes", bytes}, {"fits_sample_budget", slot.stop_sample <= o.sample_limit},
            {"fits_capture_ram_budget", retained <= resources.memory_limit_bytes / ObservationSpool::row_bytes}});
        if (o.command == "plan" || o.command == "message" || o.command == "backlog")
            report["configuration"]["stream_plan"] = stream_plan(o, profile, resources);
        phase(report, "profile_preparation", preparation_elapsed, 0, object({{"profile_ready", true}}),
              0, "one_time_profile_preparation");
        if (o.command == "plan") {
            report["result"] = object({{"planned", true}, {"actual_device_work", false},
                {"duration_is_provisional", true}});
            return;
        }
        std::uint64_t samples_left = o.sample_limit;
        if (o.command == "message" || o.command == "backlog")
            stream_run(report, o, profile, resources, samples_left, stop);
        else if (o.command == "codec") codec_run(report, o, profile, stop);
        else if (o.work_only) diagnostic_fine(report, o, profile, slot, resources, samples_left, stop);
        else slot_run(report, o, profile, slot, resources, samples_left, stop);
        describe_backend();
        if (o.backend == "cuda" && o.command != "codec") {
            const auto actual = backend->receipt();
            if (!actual.cuda || !actual.executed || (!o.work_only && (!actual.fft_calls || !actual.score_rows)) ||
                (!actual.fft_calls && !actual.dc_reduction_values) || !actual.statistics_blocks ||
                (!actual.mapped_values && o.stratum != "terminal"))
                throw Incomplete("CUDA receiver work did not execute all declared accelerator stages");
            if (!actual.allocator_peak_reserved_bytes ||
                actual.allocator_peak_reserved_bytes > actual.workspace_limit_bytes)
                throw Incomplete("CUDA allocator peak did not satisfy the declared device workspace");
        }
        if (o.require_accelerator && o.command == "codec")
            throw Incomplete("Isolated codec work does not execute accelerator stages");
    } catch (...) {
        describe_backend();
        throw;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        help();
        return 0;
    }
    auto report = object({{"schema", schema}, {"command", Json{}}, {"status", "error"}, {"reason", Json{}},
        {"identity", identity()}, {"configuration", object({})}, {"allocation", object({})},
        {"budget", object({})}, {"observation", Json{}}, {"phases", Json::Array{}}, {"result", object({})}});
    int code = 1;
    try {
        const auto options = parse(argc, argv);
        report["command"] = options.command;
        const Deadline deadline(options.deadline_ms);
        try {
            run(report, options, deadline.token());
            check_stop(deadline.token());
        } catch (const Error&) {
            check_stop(deadline.token());
            throw;
        }
        report["status"] = "complete";
        code = 0;
    } catch (const Unavailable& error) {
        report["status"] = "unavailable";
        report["reason"] = error.what();
        code = 2;
    } catch (const ReceiverWorkspaceError& error) {
        report["status"] = "incomplete";
        report["reason"] = error.what();
        code = 2;
    } catch (const Incomplete& error) {
        report["status"] = "incomplete";
        report["reason"] = error.what();
        code = 2;
    } catch (const std::bad_alloc&) {
        report["status"] = "incomplete";
        report["reason"] = "Local allocation failed during the bounded probe";
        code = 2;
    } catch (const std::exception& error) {
        report["reason"] = error.what();
    }
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    write_json(std::cout, report);
    std::cout << '\n';
    return code;
}
