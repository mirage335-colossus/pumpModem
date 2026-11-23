#include "datapump/robust_audio_frontend.hpp"
#include "datapump/robust_profile.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/tuning.hpp"
#include "search_fft.hpp"
#include "triad_probe_capture_cache.hpp"
#include "triad_probe_json.hpp"
#include "triad_probe_spool.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_receiver_helper.hpp"
#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"
#include "datapump/triad_acceleration.hpp"

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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <link.h>
#endif
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif
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
using datapump::triad::probe::CaptureCacheReader;
using datapump::triad::probe::CaptureCacheWriter;
using datapump::triad::probe::CaptureRecord;
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
    std::string tensor_coarse_math = "fft", tensor_fine_projection = "fft";
    bool cuda_segmented_timing = false;
    std::string vulkan_precision = "automatic", vulkan_fine_projection = "automatic";
    std::string vulkan_segmented_timing = "automatic";
    // Fine-stage device routes and the varying statistics' arithmetic.
    std::string vulkan_gain_rank = "automatic", vulkan_drift = "automatic", vulkan_varying_fine = "automatic";
    std::string vulkan_varying_arithmetic = "automatic";
    std::string opencl_coarse_templates = "device", opencl_fine_tiles = "device";
    bool opencl_coarse_templates_supplied = false, opencl_fine_tiles_supplied = false;
    std::size_t opencl_fine_window = 0, opencl_fine_stream = 4;
    // Arithmetic of the resident fine tiles and mapping, and of the coarse
    // rescoring. A stated choice must be the one executed. Unstated, they
    // are the library's defaults where the route runs those kernels.
    std::string opencl_fine_map = "fp64", opencl_coarse_rescore = "fp64";
    bool opencl_fine_map_supplied = false, opencl_coarse_rescore_supplied = false;
    std::optional<bool> opencl_profile;
    bool gpu_fp32_minimum = false;
    // Automatic Vulkan selection admits one-ray sessions to the dense search.
    // Unstated, it is the library's automatic default.
    bool opencl_coarse_one_ray = GpuReceiverOptions{}.opencl_coarse_one_ray;
    std::string native_coarse_precision = "automatic";
    std::string wrong_key_admission = "current";
    std::string base = "exact-fixture", shift = "exact-fixture";
    std::string spread = "none", selective = "off", timing = "fixture";
    std::string channel_model = "gui";
    std::optional<unsigned> workers, owners;
    std::optional<std::size_t> memory_limit, device_memory_limit;
    std::optional<std::uint64_t> spool_limit;
    std::vector<unsigned> pilot_workers;
    std::uint64_t seed = 74021, sample_limit = default_sample_budget, deadline_ms = 30000;
    std::uint64_t slot_index = 0;
    std::size_t maximum_coarse_tiles = 4096;
    unsigned units = 1, sample_rate = 1500, added_symbols = 1, waits = 0;
    unsigned slots = 10, bit = 0, device_index = 0;
    double data_if_hz = 500, rate_hz = 120, rf_hz = 0, margin = 1, doppler_hz = 0;
    double minimum_carrier_spacing_hz = 0;
    double data_w = .1, helper_w = 0, loss_db = 114, noise_dbm_hz = -174;
    std::optional<double> target_cn0, active_seconds, observed_cn0;
    std::optional<double> signal_gain_db, channel_severity;
    std::optional<double> interferer_cn0, interferer_seconds, interferer_rate;
    std::optional<double> interferer_offset_seconds, interferer_frequency_offset_hz;
    std::optional<std::uint64_t> interferer_seed;
    std::optional<unsigned> interferer_bit;
    std::optional<bool> interferer_uniform_window;
    bool encrypt = false, interleave = false, fhss = false, require_accelerator = false;
    bool helper = false, compression = true;
    bool work_only = false;
    bool helper_overlap = true;
    bool continuous_data = false, affine_continuation = true;
    bool scheduled_gap_fill = false, scheduled_gap_bridge = false;
    bool gap_fill_transmit = true, foreign_receiver_key = false;
    bool match_scheduled_gap_admission = false;
    bool protected_packed = false, protected_discovery = false, carrier_beat = false, carrier_beat_polarity = false;
    bool white_beat_evidence = false, colored_beat_evidence = false, project_colored_pcm = false;
    std::string adjacent_data;
    FecMode fec = FecMode::off;
    // --capture-cache: the directory of exact captures for later runs, and
    // the given options that can change a capture, sorted by flag.
    std::string capture_cache;
    std::vector<std::pair<std::string, std::string>> capture_arguments;

    bool fills_scheduled_gaps() const { return scheduled_gap_fill || scheduled_gap_bridge; }
};

// Options that select receiver arithmetic, backends, allocation, budgets or
// deadlines. None of them changes a generated capture, so a capture cache key
// leaves them out; every other given option is part of the key.
constexpr std::array<std::string_view, 45> receiver_only_options{
    "--backend", "--tensor-precision", "--tensor-coarse-math", "--tensor-fine-projection",
    "--cuda-segmented-timing", "--vulkan-precision", "--vulkan-fine-projection", "--vulkan-segmented-timing",
    "--vulkan-gain-rank", "--vulkan-drift", "--vulkan-varying-fine", "--vulkan-varying-arithmetic",
    "--opencl-coarse-templates", "--opencl-coarse-rescore", "--opencl-coarse-one-ray", "--opencl-fine-map",
    "--opencl-fine-tiles", "--opencl-fine-window", "--opencl-fine-stream", "--opencl-profile",
    "--gpu-fp32-minimum", "--native-coarse-precision", "--wrong-key-admission", "--require-accelerator",
    "--device-index", "--device-memory-limit-bytes", "--workers", "--slot-owners", "--memory-limit-bytes",
    "--max-samples", "--max-spool-bytes", "--deadline-ms", "--coarse-tiles", "--pilot-workers", "--units",
    "--helper-overlap", "--affine-continuation", "--protected-packed", "--protected-discovery",
    "--carrier-beat", "--carrier-beat-polarity", "--white-beat-evidence", "--colored-beat-evidence", "--capture-cache",
    "--foreign-receiver-key"};

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
    require(one_of(o.command, {"describe", "plan", "slot", "continuation", "message", "backlog", "regression", "admission", "codec"}),
            "Unknown native probe command");
    std::map<std::string_view, std::string_view> values;
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        require(flag.starts_with("--") && i + 1 < argc, "Native probe options require a value");
        if (!values.emplace(flag, argv[++i]).second) throw Error("Duplicate option " + std::string(flag));
    }
    for (const auto& [flag, value] : values)
        if (std::ranges::find(receiver_only_options, flag) == receiver_only_options.end())
            o.capture_arguments.emplace_back(flag, value);
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
    assign("--tensor-coarse-math", o.tensor_coarse_math);
    assign("--tensor-fine-projection", o.tensor_fine_projection);
    assign("--cuda-segmented-timing", o.cuda_segmented_timing);
    assign("--vulkan-precision", o.vulkan_precision);
    assign("--vulkan-fine-projection", o.vulkan_fine_projection);
    assign("--vulkan-segmented-timing", o.vulkan_segmented_timing);
    assign("--vulkan-gain-rank", o.vulkan_gain_rank);
    assign("--vulkan-drift", o.vulkan_drift);
    assign("--vulkan-varying-fine", o.vulkan_varying_fine);
    assign("--vulkan-varying-arithmetic", o.vulkan_varying_arithmetic);
    const bool coarse_templates_supplied = values.contains("--opencl-coarse-templates");
    assign("--opencl-coarse-templates", o.opencl_coarse_templates);
    o.opencl_coarse_templates_supplied = coarse_templates_supplied;
    const bool fine_tiles_supplied = values.contains("--opencl-fine-tiles");
    assign("--opencl-fine-tiles", o.opencl_fine_tiles);
    o.opencl_fine_tiles_supplied = fine_tiles_supplied;
    const bool fine_window_supplied = values.contains("--opencl-fine-window");
    assign("--opencl-fine-window", o.opencl_fine_window);
    const bool fine_stream_supplied = values.contains("--opencl-fine-stream");
    assign("--opencl-fine-stream", o.opencl_fine_stream);
    o.opencl_fine_map_supplied = values.contains("--opencl-fine-map");
    assign("--opencl-fine-map", o.opencl_fine_map);
    o.opencl_coarse_rescore_supplied = values.contains("--opencl-coarse-rescore");
    assign("--opencl-coarse-rescore", o.opencl_coarse_rescore);
    if (const auto profile = take("--opencl-profile")) o.opencl_profile = toggle(*profile, "--opencl-profile");
    const bool gpu_fp32_minimum_supplied = values.contains("--gpu-fp32-minimum");
    assign("--gpu-fp32-minimum", o.gpu_fp32_minimum);
    const bool coarse_one_ray_supplied = values.contains("--opencl-coarse-one-ray");
    assign("--opencl-coarse-one-ray", o.opencl_coarse_one_ray);
    assign("--native-coarse-precision", o.native_coarse_precision);
    assign("--wrong-key-admission", o.wrong_key_admission);
    (void)parse_wrong_key_admission(o.wrong_key_admission);
    assign("--require-accelerator", o.require_accelerator);
    const bool device_index_supplied = values.contains("--device-index");
    const bool device_memory_limit_supplied = values.contains("--device-memory-limit-bytes");
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
    assign("--helper-overlap", o.helper_overlap);
    assign("--continuous-data", o.continuous_data);
    o.match_scheduled_gap_admission = o.command == "admission" &&
        (values.contains("--scheduled-gap-fill") || values.contains("--scheduled-gap-bridge"));
    assign("--scheduled-gap-fill", o.scheduled_gap_fill);
    assign("--scheduled-gap-bridge", o.scheduled_gap_bridge);
    const bool gap_fill_transmit_supplied = values.contains("--gap-fill-transmit");
    assign("--gap-fill-transmit", o.gap_fill_transmit);
    assign("--foreign-receiver-key", o.foreign_receiver_key);
    const bool affine_continuation_supplied = values.contains("--affine-continuation");
    assign("--affine-continuation", o.affine_continuation);
    assign("--adjacent-data", o.adjacent_data);
    assign("--protected-packed", o.protected_packed);
    assign("--protected-discovery", o.protected_discovery);
    assign("--carrier-beat", o.carrier_beat);
    assign("--carrier-beat-polarity", o.carrier_beat_polarity);
    assign("--white-beat-evidence", o.white_beat_evidence);
    assign("--colored-beat-evidence", o.colored_beat_evidence);
    assign("--project-colored-pcm", o.project_colored_pcm);
    assign("--capture-cache", o.capture_cache);
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
    assign("--minimum-carrier-spacing", o.minimum_carrier_spacing_hz);
    assign("--rf-hz", o.rf_hz);
    assign("--doppler-hz", o.doppler_hz);
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
    assign("--signal-gain-db", o.signal_gain_db);
    assign("--channel-severity", o.channel_severity);
    assign("--channel-model", o.channel_model);
    assign("--interferer-cn0", o.interferer_cn0);
    assign("--interferer-seconds", o.interferer_seconds);
    assign("--interferer-rate", o.interferer_rate);
    assign("--interferer-offset-seconds", o.interferer_offset_seconds);
    assign("--interferer-frequency-offset-hz", o.interferer_frequency_offset_hz);
    assign("--interferer-seed", o.interferer_seed);
    assign("--interferer-bit", o.interferer_bit);
    if (const auto uniform = take("--interferer-uniform-window"))
        o.interferer_uniform_window = toggle(*uniform, "--interferer-uniform-window");
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
    require(one_of(o.backend, {"cpu", "tensor-cpu", "cuda", "vulkan", "gpu"}), "Unknown native probe backend");
    require(one_of(o.tensor_precision, {"automatic", "fp64"}), "Unknown tensor precision");
    require(one_of(o.tensor_coarse_math, {"fft", "fp16", "int8", "fp16-fft"}),
            "Unknown tensor coarse arithmetic");
    require(one_of(o.vulkan_precision, {"automatic", "fp32", "fp16-storage", "direct-fp32",
                                       "direct-fp16-storage", "direct-fp16", "nvfp4"}) &&
            (o.backend == "vulkan" || o.backend == "gpu" || o.vulkan_precision == "automatic"),
            "Invalid Vulkan precision policy or backend");
    require(one_of(o.vulkan_segmented_timing, {"automatic", "device", "native"}) &&
            (o.backend == "vulkan" || o.vulkan_segmented_timing == "automatic"),
            "--vulkan-segmented-timing expects automatic, device or native with explicit --backend vulkan");
    const std::array<std::pair<const char*, const std::string*>, 3> stage_routes{{
        {"--vulkan-gain-rank", &o.vulkan_gain_rank}, {"--vulkan-drift", &o.vulkan_drift},
        {"--vulkan-varying-fine", &o.vulkan_varying_fine}}};
    for (const auto& [flag, route] : stage_routes)
        if (!one_of(*route, {"automatic", "device", "native"}) || (o.backend != "vulkan" && *route != "automatic"))
            throw Error(std::string(flag) + " expects automatic, device or native with explicit --backend vulkan");
    // Automatic and native routes never apply a stated arithmetic.
    require(one_of(o.vulkan_varying_arithmetic, {"automatic", "fp64", "fp32-template"}) &&
            (o.vulkan_varying_arithmetic == "automatic" ||
             (o.backend == "vulkan" && o.vulkan_varying_fine == "device")),
            "--vulkan-varying-arithmetic expects automatic, fp64 or fp32-template with explicit --backend vulkan "
            "and --vulkan-varying-fine device");
    require(one_of(o.opencl_coarse_templates, {"device", "native"}) &&
            (!coarse_templates_supplied || (o.backend == "vulkan" && o.vulkan_precision == "nvfp4")),
            "--opencl-coarse-templates expects device or native with explicit Vulkan nvfp4 coarse search");
    const bool resident_fine = o.backend == "vulkan" &&
        one_of(o.vulkan_fine_projection, {"resident-nvfp4", "resident-fft"});
    require(one_of(o.opencl_fine_tiles, {"device", "native"}) && (!fine_tiles_supplied || resident_fine),
            "--opencl-fine-tiles expects device or native with an explicit Vulkan resident fine projection");
    // A window holds whole dispatches of two blocks.
    require(fine_window_supplied ? resident_fine && o.opencl_fine_window >= 2 && o.opencl_fine_window <= 64 &&
                                       o.opencl_fine_window % 2 == 0
                                 : o.opencl_fine_window == 0,
            "--opencl-fine-window expects an even number of 2 to 64 blocks with an explicit Vulkan resident fine projection");
    require(o.opencl_fine_stream <= 64 && (!fine_stream_supplied || resident_fine),
            "--opencl-fine-stream expects 0 to 64 blocks with an explicit Vulkan resident fine projection");
    require(one_of(o.opencl_fine_map, {"fp32", "fp64"}) && (!o.opencl_fine_map_supplied || resident_fine),
            "--opencl-fine-map expects fp32 or fp64 with an explicit Vulkan resident fine projection");
    require(one_of(o.opencl_coarse_rescore, {"fp32", "fp64"}) &&
            (!o.opencl_coarse_rescore_supplied || (o.backend == "vulkan" && o.vulkan_precision == "nvfp4")),
            "--opencl-coarse-rescore expects fp32 or fp64 with explicit Vulkan nvfp4 coarse search");
    require(!o.opencl_profile || o.backend == "vulkan", "--opencl-profile is valid only with --backend vulkan");
    require(!gpu_fp32_minimum_supplied || o.backend == "gpu",
            "--gpu-fp32-minimum is valid only with --backend gpu");
    {
        const VulkanReceiverOptions library;
        if (!o.opencl_fine_map_supplied && library.opencl_fine_fp32 && (o.backend == "gpu" || resident_fine))
            o.opencl_fine_map = "fp32";
        if (!o.opencl_coarse_rescore_supplied && library.opencl_coarse_fp32_rescore &&
            (o.backend == "gpu" || (o.backend == "vulkan" && o.vulkan_precision == "nvfp4")))
            o.opencl_coarse_rescore = "fp32";
    }
    // An explicit nvfp4 request always scores one-ray sessions.
    require(!coarse_one_ray_supplied || o.backend == "gpu",
            "--opencl-coarse-one-ray is valid only with --backend gpu");
    require(one_of(o.native_coarse_precision, {"automatic", "fp32", "fp64", "q15"}),
            "--native-coarse-precision expects automatic, fp32, fp64 or q15");
    require(one_of(o.stratum, {"active", "noise", "cold-maximum", "terminal", "erasure"}),
            "Unknown native probe stratum");
    require(one_of(o.channel_model, {"gui", "itu-disturbed"}),
            "--channel-model expects gui or itu-disturbed");
    if (o.channel_model == "itu-disturbed") {
        const bool supported_backend = one_of(o.backend, {"cpu", "cuda"}) || (o.backend == "gpu" && o.require_accelerator);
        require(one_of(o.command, {"plan", "admission", "message"}) && supported_backend &&
                o.scenario == "hf" && o.spread == "hf-worst" && o.margin == 3 &&
                o.rf_hz >= 1e6 && o.rf_hz <= 30e6 && o.doppler_hz == 0 &&
                !o.helper && !o.interferer_cn0 && !o.channel_severity,
                "ITU disturbed reference requires HF Worst with CPU, explicit CUDA or automatic GPU with "
                "--require-accelerator on, margin 3, zero explicit Doppler, "
                "1-30 MHz, Helper Off, no interferer or severity contrast, and plan/admission/message");
    }
    require(one_of(o.timing, {"broad", "fixture"}), "Unknown native probe timing policy");
    const auto maximum_deadline_ms = o.command == "admission" ? 5'400'000u : 3'600'000u;
    require(o.sample_limit > 0 && o.deadline_ms > 0 && o.deadline_ms <= maximum_deadline_ms &&
            o.maximum_coarse_tiles > 0 && o.maximum_coarse_tiles <= 1'048'576,
            "Native probe requires bounded positive sample, time and tile budgets");
    require(o.units == 1 || o.units == 2 || o.units == 4, "--units expects 1, 2 or 4");
    require(o.command == "regression" || o.units == 1, "--units applies only to regression");
    require(o.bit <= 1 && o.slots >= 3 && o.slots <= 4096, "Invalid native probe bit or slot count");
    require(o.helper_w >= 0 && o.helper == (o.helper_w > 0) && o.data_w > 0, "Invalid native probe TX powers");
    require(!o.continuous_data ||
            (one_of(o.command, {"plan", "message", "backlog", "admission"}) && !o.helper && !o.waits),
            "Continuous DATA qualification requires plan, message, backlog or admission with Helper and waits off");
    require(!o.scheduled_gap_fill || (one_of(o.command, {"plan", "message", "admission"}) &&
            !o.helper && !o.fhss && !o.continuous_data && o.capture_cache.empty()),
            "Scheduled gap fill requires plan, message or admission with Helper and FHSS off and no capture cache");
    require(!o.scheduled_gap_bridge || (one_of(o.command, {"plan", "message", "admission"}) &&
            !o.scheduled_gap_fill && !o.continuous_data && o.capture_cache.empty()),
            "Scheduled short transitions require plan, message or admission without guarded fill, Continuous DATA or capture cache");
    require(!gap_fill_transmit_supplied || (o.fills_scheduled_gaps() && one_of(o.command, {"message", "admission"})),
            "The gap-fill transmit control needs scheduled fill or short transitions in a message or admission probe");
    require(!o.foreign_receiver_key || (o.command == "message" && o.encrypt),
            "The foreign receiver key control needs an encrypted message probe");
    require(!affine_continuation_supplied || one_of(o.command, {"plan", "message", "backlog"}),
            "The affine continuation control requires a stream plan or replay");
    require(o.adjacent_data.empty() ||
            (o.command == "admission" && !o.helper && !o.waits && !o.interferer_cn0 &&
             o.adjacent_data.size() == 2 && o.adjacent_data.find_first_not_of("01q") == std::string::npos &&
             (o.slot_index > 0 || o.adjacent_data.front() == 'q')),
            "Adjacent DATA qualification requires admission, Helper and waits off, no interferer and a "
            "pair of 0, 1 or q (quiet). At slot zero the preceding state must be q");
    require(!o.match_scheduled_gap_admission ||
            (!o.continuous_data && o.stratum == "active" && o.slot_index > 0 && o.adjacent_data.size() == 2 &&
             o.adjacent_data.find_first_not_of("01") == std::string::npos),
            "Scheduled gap fill admission requires an active interior slot between two DATA bits");
    require(!o.protected_packed || (o.continuous_data && o.backend == "cpu" &&
            one_of(o.command, {"message", "backlog", "admission"}) &&
            !(affine_continuation_supplied && o.affine_continuation)),
            "Protected packed scoring requires a CPU packed replay or admission probe with affine continuation off");
    if (o.protected_packed) o.affine_continuation = false;
    require(!o.protected_discovery || o.protected_packed,
            "Protected discovery requires protected packed scoring");
    require(!o.carrier_beat || (o.command == "admission" && o.backend == "cpu" && !o.helper),
            "Carrier beat is an evaluator-only CPU admission comparison with Helper off");
    require(!o.carrier_beat_polarity || o.protected_packed,
            "Carrier-beat polarity requires experimental protected packed scoring");
    require(!o.white_beat_evidence || o.protected_packed,
            "White beat evidence requires a protected packed raw-simulator capture");
    require(!o.colored_beat_evidence || (o.protected_packed && !o.white_beat_evidence),
            "Colored beat evidence requires protected packed scoring and no white supplement");
    require(!o.project_colored_pcm || (o.command == "admission" && o.protected_packed &&
            !o.white_beat_evidence && o.capture_cache.empty()),
            "Colored PCM projection requires a protected CPU admission probe without the white supplement or capture cache");
    if (o.command == "regression" || o.command == "slot" || o.command == "admission")
        require(o.stratum == "active" || o.stratum == "noise", "Use continuation for diagnostic fine strata");
    if (o.interferer_cn0 || o.interferer_seconds || o.interferer_rate || o.interferer_offset_seconds ||
        o.interferer_frequency_offset_hz || o.interferer_seed || o.interferer_bit || o.interferer_uniform_window) {
        require(o.command == "admission" && o.encrypt && o.interferer_cn0 && o.interferer_seconds &&
                o.interferer_seed && o.interferer_bit && *o.interferer_seconds > 0 && *o.interferer_bit <= 1,
                "The admission interferer requires --encrypt on and explicit C/N0, duration, seed and bit");
    }
    if (o.signal_gain_db || o.channel_severity) {
        const bool cpu_contrast = o.backend == "cpu" && one_of(o.command, {"admission", "message", "plan"});
        const bool explicit_vulkan_fine = o.backend == "vulkan" && o.vulkan_fine_projection != "automatic";
        const bool gpu_control_command =
            (one_of(o.backend, {"cuda", "gpu"}) && one_of(o.command, {"admission", "plan"})) ||
            (explicit_vulkan_fine && one_of(o.command, {"admission", "plan"})) ||
            ((o.backend == "gpu" || explicit_vulkan_fine) && o.command == "message" &&
             o.slots == 5 && !o.active_seconds);
        const bool gpu_stable_control = gpu_control_command &&
            o.channel_severity == 0. && !o.signal_gain_db && o.scenario == "hf" && o.spread == "hf-worst" && o.margin == 3;
        require((cpu_contrast || gpu_stable_control) && !o.helper && !o.interferer_cn0,
                "A fixed receiver link contrast requires Helper and interferer off, with CPU admission/message/plan "
                "or accelerated admission/message/plan at HF Worst, margin 3, severity 0 and no gain override");
        require(!o.signal_gain_db || std::abs(*o.signal_gain_db) <= 100,
                "--signal-gain-db expects -100 to +100 dB");
        require(!o.channel_severity || (*o.channel_severity >= 0 && *o.channel_severity <= 1),
                "--channel-severity expects 0 to 1");
    }
    require(!o.interferer_uniform_window.value_or(false) || !o.interferer_offset_seconds,
            "Choose either uniform-window placement or a fixed interferer offset");
    require(o.command != "admission" || o.wrong_key_admission == "current",
            "Admission diagnostics score Current once and apply all policies to the resulting slot");
    require(!o.work_only || (o.command == "continuation" && o.stratum != "active"),
            "Work-only diagnostics require continuation and a nonnominal stratum");
    require(o.helper_overlap || o.command == "regression", "The Helper overlap lease applies only to regression");
    require(o.capture_cache.empty() || !one_of(o.command, {"describe", "plan", "codec"}),
            "--capture-cache applies to commands that generate a capture");
    require(o.pilot_workers.empty() || o.command == "backlog", "Worker pilots require backlog replay");
    require(o.pilot_workers.empty() || o.backend == "cpu", "Worker allocation pilots require the native CPU backend");
    require(o.backend != "vulkan" || o.tensor_precision == "automatic",
            "Vulkan coarse search uses its native FP32 path; tensor precision selection is unsupported");
    require(o.backend != "vulkan" || o.tensor_coarse_math == "fft",
            "Vulkan coarse search uses the full FFT; tensor matrix arithmetic is unsupported");
    require(o.vulkan_fine_projection == "automatic" ||
            (o.backend == "vulkan" && one_of(o.vulkan_fine_projection,
                {"native", "nvfp4", "resident-nvfp4", "resident-fft"})),
            "Fine projection qualification requires explicit Vulkan");
    require(o.backend != "gpu" || (!device_index_supplied && !device_memory_limit_supplied &&
            o.tensor_precision == "automatic" && o.tensor_coarse_math == "fft" &&
            o.vulkan_precision == "automatic"),
            "Automatic GPU selection does not accept pinned device, workspace or backend arithmetic overrides");
    require(o.tensor_coarse_math != "fp16-fft" || o.backend == "cuda",
            "fp16-fft requires explicit --backend cuda");
    require(o.tensor_fine_projection == "fft" ||
            (one_of(o.tensor_fine_projection, {"nvfp4", "cufft"}) && o.backend == "cuda" &&
             o.tensor_precision == "automatic"),
            "Fine projection qualification requires explicit CUDA and automatic tensor precision");
    require(!o.cuda_segmented_timing || (o.backend == "cuda" && o.tensor_precision == "automatic" &&
            o.tensor_coarse_math == "fft"), "CUDA segmented timing requires explicit CUDA and automatic FFT precision");
    require(o.native_coarse_precision == "automatic" || o.backend == "cpu",
            "Native coarse precision overrides require --backend cpu");
    require(tuning::valid_robust_minimum_carrier_spacing(o.minimum_carrier_spacing_hz),
            "--minimum-carrier-spacing expects 0, 3.5, 125 or 265 Hz");
    return o;
}

std::uint64_t maximum_spool_bytes(const Options& o) {
    return o.spool_limit.value_or(o.sample_limit > UINT64_MAX / ObservationSpool::row_bytes ?
        UINT64_MAX : o.sample_limit * ObservationSpool::row_bytes);
}

auto slot_workspace_for_options(const Options& o) {
    if (o.white_beat_evidence) return protected_white_beat_slot_workspace;
    if (o.colored_beat_evidence) return protected_colored_beat_slot_workspace;
    const bool protected_cadence = o.scheduled_gap_fill ||
        (o.continuous_data && one_of(o.command, {"message", "backlog", "plan"}));
    return o.protected_packed || protected_cadence ? protected_slot_workspace : receiver_slot_workspace;
}

const char* fec_name(FecMode mode) {
    if (mode == FecMode::rs20) return "rs20";
    if (mode == FecMode::rs60) return "rs60";
    return "off";
}

live::Settings settings_for(const Options& o, std::size_t memory_limit) {
    live::Settings s;
    s.data_cadence = live::DataCadence::scheduled;
    s.wrong_key_admission = o.wrong_key_admission;
    if (o.scheduled_gap_fill) s.data_cadence = live::DataCadence::scheduled_fill;
    if (o.scheduled_gap_bridge) s.data_cadence = live::DataCadence::scheduled_bridge;
    s.simulation = true;
    s.receiver_memory_limit = memory_limit;
    if (o.native_coarse_precision == "fp64") s.transfer.modem.search_arithmetic = modem::SearchArithmetic::fp64;
    if (o.native_coarse_precision == "fp32") s.transfer.modem.search_arithmetic = modem::SearchArithmetic::fp32;
    s.transfer.modem.sample_rate = o.sample_rate;
    s.transfer.modem.carrier_hz = o.data_if_hz;
    s.transfer.modem.bandwidth_hz = o.rate_hz;
    s.minimum_carrier_spacing_hz = o.minimum_carrier_spacing_hz;
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
    s.doppler_shift_hz = o.doppler_hz;
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

void configure_link_contrast(StreamChannelConfig& channel, const Options& o) {
    if (o.signal_gain_db) channel.signal_gain *= std::pow(10., *o.signal_gain_db / 20);
    if (!o.channel_severity || *o.channel_severity == 1) return;
    const auto severity = *o.channel_severity;
    auto& effects = channel.effects;
    auto& evolution = channel.path_evolution;
    effects.spread_delay_ms *= severity;
    effects.spread_doppler_hz *= severity;
    effects.perturbation_slow_depth *= severity;
    effects.perturbation_fast_depth *= severity;
    evolution.common_doppler_wander_hz *= severity;
    if (severity == 0) {
        evolution.bounded_spread_knot_seconds = 0;
        channel.selective_model.modulation_rate_hz = 0;
    }
}

// This opt-in reference is deliberately a separate channel stratum.  F.1487
// uses equal-power independent diffuse taps at 0/7 ms with a 30-Hz full 2-sigma
// width, rather than the GUI's bounded moving echo and common wander.  Keep the
// GUI receiver configuration and source waveform unchanged.  The receiver
// is therefore intentionally mismatched to this diffuse reference law.  The
// selected synthetic AM is an additional stress, not part of the ITU channel.
void configure_channel_model(Profile& profile, const Options& o) {
    if (o.channel_model == "gui") return;
    auto& evolution = profile.channel.path_evolution;
    auto& effects = profile.channel.effects;
    require(profile.receiver.spread_delay_seconds >= .007 &&
            profile.receiver.time_varying_common_doppler_hz >= 90 &&
            profile.receiver.time_varying_atmospheric_hz >= 90,
            "ITU reference needs public delay and Gaussian spectral-tail bounds");
    effects.spread_delay_ms = 7;
    effects.spread_doppler_hz = 0;
    evolution.bounded_spread_knot_seconds = 0;
    evolution.bounded_spread_direct_power_fraction.reset();
    evolution.common_doppler_wander_hz = 0;
    evolution.random_doppler_width_hz = 30;
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
    // The channel report. A reused capture restores the fields that the
    // receipts read: erased rows, workspace and both path realizations.
    StreamChannelReport channel;
    bool selective_filter = false;
    std::size_t generation_reserved_bytes = 0, generation_read_rows = 0, generation_stack_bytes = 0;
    unsigned generation_helpers = 0;
    struct GapFill {
        std::uint64_t samples = 0;
        long double wanted_energy = 0, energy = 0, cross_energy = 0, helper_energy = 0;
    };
    std::optional<GapFill> gap_fill;
    struct PcmProjection {
        std::uint64_t hilbert_halo = 0, first_simulator_row = 0, synthetic_prefix_rows = 0;
        std::uint64_t retained_valid_rows = 0;
    };
    std::optional<PcmProjection> pcm_projection;
    struct Cache {
        std::string key, file;
        bool reused = false, stored = false;
        std::uint64_t file_bytes = 0;
        Timing write{0, 0};
    };
    std::optional<Cache> cache;
    std::uint64_t rows() const { return spool ? spool->rows() : iq.size(); }
};

std::array<std::uint8_t, 32> digest_bytes(std::string_view hex) {
    std::array<std::uint8_t, 32> bytes{};
    require(hex.size() == 64, "Capture digest is not a SHA-256");
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        throw Error("Capture digest is not lowercase hexadecimal");
    };
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
    return bytes;
}

std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    std::string result;
    for (const auto byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0x0f];
    }
    return result;
}

// A 64-bit digest as 16 hex digits, most significant first.
std::string digest_hex(std::uint64_t digest) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(digest >> (56 - 8 * i));
    return hex_bytes(bytes);
}

// What a cached capture is bound to besides its arguments: the running
// probe executable and, on Linux, the loaded runtime libraries whose code
// can change sample values (C, math, C++ and GCC runtimes and the dynamic
// loader). Libraries loaded later, such as accelerator drivers, are not
// part of it. Elsewhere only the executable is covered.
struct CaptureBinaryIdentity {
    std::string executable_sha256, libc_version;
    // What selects glibc's libm variants at run time (ifunc): the x86 FMA,
    // FMA4 and AVX2 features and GLIBC_TUNABLES. The same files can
    // simulate different last bits on a CPU or mask without them.
    std::string math_dispatch;
    std::vector<std::pair<std::string, std::string>> runtime_libraries; // path, SHA-256
    bool covers_runtime_libraries = false;
};

std::string file_sha256(const std::filesystem::path& path) {
    struct CloseFile { void operator()(std::FILE* file) const noexcept { (void)std::fclose(file); } };
#ifdef _WIN32
    std::unique_ptr<std::FILE, CloseFile> file(_wfopen(path.c_str(), L"rb"));
#else
    std::unique_ptr<std::FILE, CloseFile> file(std::fopen(path.c_str(), "rb"));
#endif
    if (!file) throw Error("Cannot read " + path.string() + " for --capture-cache");
    Digest digest;
    std::vector<std::byte> chunk(64 * 1024);
    for (;;) {
        const auto count = std::fread(chunk.data(), 1, chunk.size(), file.get());
        digest.add(std::span(chunk).first(count));
        if (count < chunk.size()) break;
    }
    if (std::ferror(file.get())) throw Error("Cannot read " + path.string() + " for --capture-cache");
    return digest.finish();
}

const CaptureBinaryIdentity& capture_binary_identity() {
    static const CaptureBinaryIdentity value = [] {
        CaptureBinaryIdentity identity;
#if defined(__linux__)
        identity.executable_sha256 = file_sha256("/proc/self/exe");
        std::vector<std::string> paths;
        dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* data) {
            const std::string_view name = info->dlpi_name ? info->dlpi_name : "";
            const auto base = name.substr(name.rfind('/') + 1);
            for (const std::string_view prefix : {"libc.so", "libm.so", "libmvec.so", "libstdc++.so", "libgcc_s.so",
                                                  "ld-linux"})
                if (base.starts_with(prefix)) {
                    static_cast<std::vector<std::string>*>(data)->emplace_back(name);
                    break;
                }
            return 0;
        }, &paths);
        std::ranges::sort(paths);
        paths.erase(std::ranges::unique(paths).begin(), paths.end());
        for (const auto& path : paths) identity.runtime_libraries.emplace_back(path, file_sha256(path));
        identity.covers_runtime_libraries = true;
#if defined(__GLIBC__)
        identity.libc_version = gnu_get_libc_version();
#endif
#if defined(__x86_64__) || defined(__i386__)
        __builtin_cpu_init();
        identity.math_dispatch = std::string("fma ") + (__builtin_cpu_supports("fma") ? "1" : "0") + " fma4 " +
            (__builtin_cpu_supports("fma4") ? "1" : "0") + " avx2 " + (__builtin_cpu_supports("avx2") ? "1" : "0");
#endif
        const char* tunables = std::getenv("GLIBC_TUNABLES");
        identity.math_dispatch += std::string(" tunables ") + (tunables ? hex_bytes(std::span(
            reinterpret_cast<const std::uint8_t*>(tunables), std::strlen(tunables))) : "unset");
#elif defined(_WIN32)
        std::array<wchar_t, 32768> path{};
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) throw Error("Cannot locate the probe executable for --capture-cache");
        identity.executable_sha256 = file_sha256(std::filesystem::path(path.data()));
#else
        throw Unavailable("--capture-cache needs the running probe executable's identity on this platform");
#endif
        return identity;
    }();
    return value;
}

struct SecondaryEmission;

// The key and file of one capture under --capture-cache: the binary
// identity above, the command, every given option that can change a capture
// and the generating call's extent, episodes and secondary emission.
struct CaptureKey {
    std::array<std::uint8_t, 32> key{};
    std::string hex, path;

    CaptureKey(const Options& o, std::span<const EmissionOptions> episodes, std::uint64_t first, std::uint64_t end,
               bool spooled, const SecondaryEmission* secondary);
    std::string temporary() const {
        std::random_device entropy;
        std::array<std::uint8_t, 8> nonce{};
        for (auto& byte : nonce) byte = static_cast<std::uint8_t>(entropy());
        return path + ".tmp-" + hex_bytes(nonce);
    }
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
        {"minimum_carrier_spacing_hz", o.minimum_carrier_spacing_hz},
        {"effective_carrier_spacing_hz", geometry.carrier_spacing_hz},
        {"declared_doppler_hz", o.doppler_hz},
        {"requested_helper_power_w", o.helper_w}, {"effective_helper_power_w", config.helper.effective_power_w},
        {"path_loss_db", o.loss_db}, {"case_noise_dbm_hz", o.noise_dbm_hz},
        {"observed_cn0_override", o.observed_cn0 ? Json(*o.observed_cn0) : Json{}},
        {"actual_cn0_db_hz", 10 * std::log10(config.sample_rate_hz / config.noise_variance) +
            o.signal_gain_db.value_or(0)},
        {"target_cn0_db_hz", o.target_cn0.value_or(o.scenario.empty() ? 80. : 32.)},
        {"duration_policy", o.active_seconds ? "explicit_fixed" : "native_target_model"},
        {"fixed_active_seconds", o.active_seconds ? Json(*o.active_seconds) : Json{}},
        {"base_oscillator", o.base}, {"shift_oscillator", o.shift},
        {"frequency_bound_hz", config.frequency_bound_hz}, {"clock_bound_ppm", config.clock_bound_ppm},
        {"spread_preset", o.spread}, {"spread_delay_seconds", config.spread_delay_seconds},
        {"spread_doppler_hz", config.spread_doppler_hz}, {"selective_perturbation", o.selective},
        {"channel_model", o.channel_model}, {"receiver_random_doppler_width_hz", config.random_doppler_width_hz},
        {"channel_spread_delay_ms", channel.effects.spread_delay_ms},
        {"channel_spread_doppler_hz", channel.effects.spread_doppler_hz},
        {"channel_random_doppler_width_hz", channel.path_evolution.random_doppler_width_hz},
        {"time_varying_channel", config.time_varying_channel},
        {"time_varying_common_doppler_hz", config.time_varying_common_doppler_hz},
        {"time_varying_atmospheric_hz", config.time_varying_atmospheric_hz},
        {"time_varying_minimum_bit_log_separation", config.time_varying_minimum_bit_log_separation},
        {"wrong_key_admission", wrong_key_admission_name(config.wrong_key_admission)},
        {"channel_bounded_spread_knot_seconds", channel.path_evolution.bounded_spread_knot_seconds},
        {"channel_common_doppler_wander_hz", channel.path_evolution.common_doppler_wander_hz},
        {"channel_common_doppler_knot_seconds", channel.path_evolution.common_doppler_knot_seconds},
        {"channel_selective_coefficient_law", channel.selective_model.coefficient_law ==
            SelectiveCoefficientLaw::GaussianErf ? "gaussian-erf" : "sinusoidal"},
        {"channel_selective_modulation_rate_hz", channel.selective_model.modulation_rate_hz
            ? Json(*channel.selective_model.modulation_rate_hz) : Json{}},
        {"selective_slow_depth", config.selective_slow_depth}, {"selective_fast_depth", config.selective_fast_depth},
        {"selective_modulation_rate_hz", config.selective_modulation_rate_hz}, {"margin", o.margin},
        {"arrival_lower_seconds", slot.arrival_lower_seconds}, {"arrival_upper_seconds", slot.arrival_upper_seconds},
        {"encrypt", waveform.config().format.encrypt}, {"helper_enabled", config.helper_enabled},
        {"continuous_data", waveform.config().continuous_data},
        {"protected_packed", o.protected_packed},
        {"protected_discovery", o.protected_discovery},
        {"carrier_beat_polarity", o.carrier_beat_polarity},
        {"white_beat_evidence", o.white_beat_evidence},
        {"colored_beat_evidence", o.colored_beat_evidence},
        {"project_colored_pcm", o.project_colored_pcm},
        {"receiver_filter_halo_samples", waveform.config().receiver_filter_halo_samples},
        {"affine_continuation", one_of(o.command, {"plan", "message", "backlog"}) ?
            Json(o.affine_continuation) : Json{}},
        {"added_symbols", o.added_symbols}, {"added_interleave", o.interleave},
        {"fhss", waveform.config().format.fhss}, {"physical_rf_retuning", channel.effects.fhss},
        {"symbol_burst_wait_bits", o.waits}, {"fec", fec_name(o.fec)}, {"compression", o.compression},
        {"bit_seconds", geometry.bit_seconds}, {"slot_interval_seconds", stride},
        {"nominal_payload_energy_seconds", geometry.nominal_payload_energy_seconds},
        {"fine_component_omits_varying_operational_work", config.time_varying_channel},
        {"nominal_continuation_eligible", geometry.nominal_payload_energy_seconds >= 6 &&
            slot.stop_sample - slot.first_sample >= 6 * config.sample_rate_hz},
        {"source_samples", geometry.source_samples}, {"public_window_samples", slot.stop_sample - slot.first_sample},
        {"public_window_seconds", (slot.stop_sample - slot.first_sample) / config.sample_rate_hz},
        {"quiet_first_sample", slot.quiet_first_sample}, {"quiet_stop_sample", slot.quiet_stop_sample},
        {"calendar_epoch", waveform.config().format.epoch},
        {"initial_symbol_ordinal", waveform.config().format.symbol_ordinal},
        {"recurring_horizon", recurring_horizon(waveform)},
        {"scope", "configured_public_calendar_and_keyed_ordinal"}});
    if (o.channel_model == "itu-disturbed")
        result["channel_reference"] = object({
            {"law", "fixed-grid-gaussian-v1"},
            {"description", "ITU-R F.1487 high-latitude disturbed tap reference; selected selective AM is additional synthetic stress"},
            {"tap_delays_ms", Json::Array{0., 7.}}, {"mean_tap_powers", Json::Array{.5, .5}},
            {"tap_doppler_width_2sigma_hz", 30.}, {"tap_center_separation_hz", 0.},
            {"receiver_model", "unchanged-gui-prior-intentionally-mismatched"},
            {"additional_selective_am", channel.effects.perturbation_slow_depth > 0 ||
                                         channel.effects.perturbation_fast_depth > 0},
            {"path_seed_root", channel.path_seed.value_or(channel.effects.seed)},
            {"gaussian_spectral_capture_allowance_per_edge_hz", 90.},
            {"capture_allowance_is_hard_bandlimit", false}});
    if (o.signal_gain_db || o.channel_severity) {
        result["fixed_receiver_link_contrast"] = object({
            {"signal_gain_db", o.signal_gain_db.value_or(0)}, {"signal_gain", channel.signal_gain},
            {"channel_severity", o.channel_severity.value_or(1)},
            {"receiver_reference_cn0_db_hz", 10 * std::log10(config.sample_rate_hz / config.noise_variance)},
            {"receiver_noise_variance", config.noise_variance}, {"receiver_data_basis_gain", config.data_basis_gain},
            {"channel_noise_variance", channel.complex_noise_variance},
            {"channel_spread_delay_ms", channel.effects.spread_delay_ms},
            {"channel_spread_doppler_hz", channel.effects.spread_doppler_hz},
            {"channel_selective_slow_depth", channel.effects.perturbation_slow_depth},
            {"channel_selective_fast_depth", channel.effects.perturbation_fast_depth}});
    }
    try {
        result["receiver_slot_workspace_bytes"] = slot_workspace_for_options(o)(cpu_config, slot, resources.workers);
    } catch (const Error& error) {
        result["receiver_slot_workspace_bytes"] = Json{};
        result["receiver_slot_workspace_reason"] = error.what();
    }
    if (o.scheduled_gap_fill) {
        result["scheduled_gap_fill"] = object({{"receiver_enabled", waveform.config().scheduled_gap_fill},
            {"transmit_enabled", o.gap_fill_transmit}, {"foreign_receiver_key", o.foreign_receiver_key},
            {"protected_first_sample", slot.protected_data_first_sample},
            {"protected_stop_sample", slot.protected_data_stop_sample}});
        if (const auto fill = waveform.gap_fill_after(slot.index)) {
            auto& receipt = result["scheduled_gap_fill"];
            receipt["planned_source_start_sample"] = fill->first;
            receipt["planned_source_stop_sample"] = fill->stop;
            receipt["planned_source_samples"] = fill->size();
        }
    }
    if (o.scheduled_gap_bridge) {
        result["scheduled_gap_bridge"] = object({{"transmit_enabled", o.gap_fill_transmit},
            {"receiver", "ordinary_scheduled"}, {"foreign_receiver_key", o.foreign_receiver_key}});
    }
    return result;
}

Json observation(const Capture& capture, std::uint64_t stop, const ReceiverSlotPlan& slot, const Options& o) {
    Json spread, common;
    if (const auto& r = capture.channel.bounded_spread) {
        spread = object({{"seed", r->seed}, {"processed_rows", r->processed_rows},
            {"excess_delay_seconds", r->excess_delay_seconds}, {"knot_seconds", r->knot_seconds},
            {"doppler_bound_hz", r->doppler_bound_hz},
            {"minimum_echo_ratio", r->minimum_echo_ratio ? Json(*r->minimum_echo_ratio) : Json{}},
            {"maximum_echo_ratio", r->maximum_echo_ratio ? Json(*r->maximum_echo_ratio) : Json{}},
            {"fixed_direct_power_fraction", r->fixed_direct_power_fraction ? Json(*r->fixed_direct_power_fraction) : Json{}},
            {"fixed_delayed_power_fraction", r->fixed_direct_power_fraction ? Json(1-*r->fixed_direct_power_fraction) : Json{}},
            {"minimum_doppler_hz", r->minimum_doppler_hz}, {"maximum_doppler_hz", r->maximum_doppler_hz},
            {"minimum_combined_physical_doppler_hz", r->minimum_combined_physical_doppler_hz},
            {"maximum_combined_physical_doppler_hz", r->maximum_combined_physical_doppler_hz}});
    }
    if (const auto& r = capture.channel.common_doppler_wander) {
        common = object({{"seed", r->seed}, {"processed_rows", r->processed_rows},
            {"bound_hz", r->bound_hz}, {"knot_seconds", r->knot_seconds},
            {"minimum_hz", r->minimum_hz}, {"maximum_hz", r->maximum_hz}});
    }
    auto result = object({{"input_sha256", capture.digest}, {"first_sample", capture.first}, {"stop_sample", stop},
                   {"input_hash_encoding", "sha256_of_iq_f64le_and_mask_sha256_hex"},
                   {"samples", capture.rows()}, {"generated_samples", capture.generated_samples},
                   {"retained_ram_rows", capture.iq.size()}, {"spool_bytes", capture.spool ? capture.spool->bytes() : 0},
                   {"spool_working_bytes", capture.spool ? capture.spool->working_bytes() : 0},
                   {"spool_backing_kind", capture.spool ? Json(capture.spool->backing_kind()) : Json{}},
                   {"spool_residency_reserved_bytes", capture.spool ? capture.spool->resident_reservation_bytes() : 0},
                   {"seed", o.seed}, {"public_slot", slot.index}, {"pattern_epoch", slot.format.epoch},
                   {"symbol_ordinal", slot.format.symbol_ordinal}, {"channel_erased_rows", capture.channel.erased_samples},
                   // A reused capture built no channel; the generating run's
                   // workspace stays in the capture_reuse phase.
                   {"channel_workspace_bytes", capture.cache && capture.cache->reused ? Json{} :
                        Json(capture.channel.working_bytes)},
                   {"bounded_spread_realization", std::move(spread)},
                   {"common_doppler_realization", std::move(common)},
                   {"evaluator_bit", o.stratum == "noise" ? Json{} : Json(o.bit)}});
    if (capture.pcm_projection) {
        const auto& pcm = *capture.pcm_projection;
        result["pcm_projection"] = object({{"model", "native_analytic_projection_of_fixed_ma1_real_pcm"},
            {"real_conversion_scale", 1. / std::numbers::sqrt2},
            {"covariance_bound_pcm_over_declared_iq_variance", .5},
            {"ma1_current", 2. / 3}, {"ma1_previous", 1. / 3},
            {"hilbert_halo_samples", pcm.hilbert_halo}, {"ma1_halo_samples", 1},
            {"first_simulator_input_row", pcm.first_simulator_row},
            {"synthetic_thermal_prefix_rows", pcm.synthetic_prefix_rows},
            {"retained_valid_rows", pcm.retained_valid_rows}});
    }
    if (capture.gap_fill) {
        const auto& fill = *capture.gap_fill;
        auto receipt = object({{"emitted_samples", fill.samples},
            {"wanted_energy_samples", static_cast<double>(fill.wanted_energy)},
            {"filler_energy_samples", static_cast<double>(fill.energy)}});
        if (o.scheduled_gap_bridge) {
            receipt["data_filler_cross_energy_samples"] = static_cast<double>(fill.cross_energy);
            receipt["combined_energy_samples"] = static_cast<double>(fill.wanted_energy + fill.energy + fill.cross_energy);
            receipt["combined_energy_scope"] = "transmitted_data_and_bridge";
            receipt["helper_energy_samples"] = static_cast<double>(fill.helper_energy);
        }
        result[o.scheduled_gap_bridge ? "scheduled_gap_bridge" : "scheduled_gap_fill"] = std::move(receipt);
    }
    if (capture.cache) {
        const auto& identity = capture_binary_identity();
        Json::Array libraries;
        for (const auto& [file, sha] : identity.runtime_libraries) libraries.push_back(file);
        result["capture_cache"] = object({{"source", capture.cache->reused ? "reused" : "generated"},
            {"key_sha256", capture.cache->key}, {"file", capture.cache->file}, {"file_bytes", capture.cache->file_bytes},
            {"stored", capture.cache->stored}, {"verified_before_receiver", capture.cache->reused},
            {"key_binds", identity.covers_runtime_libraries ? "executable_and_runtime_libraries" : "executable"},
            {"key_runtime_libraries", std::move(libraries)}, {"key_math_dispatch", identity.math_dispatch}});
    }
    return result;
}

Json bulk_json(const BulkReceipt& r) {
    Json::Object fallbacks;
    for (std::size_t i = 0; i < r.direct_fallback_counts.size(); ++i)
        fallbacks.emplace(std::string(bulk_direct_fallback_reasons[i]), r.direct_fallback_counts[i]);
    Json::Object half_fft_fallbacks;
    for (std::size_t i = 0; i < r.half_fft_fallback_counts.size(); ++i)
        half_fft_fallbacks.emplace(std::string(bulk_half_fft_fallback_reasons[i]), r.half_fft_fallback_counts[i]);
    Json::Object matrix_fallbacks;
    for (std::size_t i = 0; i < r.matrix_fft_fallback_counts.size(); ++i)
        matrix_fallbacks.emplace(std::string(bulk_matrix_fallback_reasons[i]), r.matrix_fft_fallback_counts[i]);
    Json::Object dense_coarse_fallbacks;
    for (std::size_t i = 0; i < r.opencl_coarse_fallback_counts.size(); ++i)
        dense_coarse_fallbacks.emplace(std::string(opencl_coarse_fallback_reasons[i]), r.opencl_coarse_fallback_counts[i]);
    auto result = object({
        {"device", r.device}, {"device_name", r.device_name}, {"library_version", r.library_version},
        {"cuda_runtime_version", r.cuda_runtime_version}, {"cuda_driver_version", r.cuda_driver_version},
        {"cuda_fft_version", r.cuda_fft_version},
        {"cuda_blas_version", r.cuda_blas_version},
        {"cuda_compute_major", r.cuda_compute_major}, {"cuda_compute_minor", r.cuda_compute_minor},
        {"numerical_threads", r.numerical_threads},
        {"precision", r.precision}, {"requested_coarse_math", r.requested_coarse_math}, {"cuda", r.cuda}, {"executed", r.executed},
        {"workspace_limit_bytes", r.workspace_limit_bytes}, {"reserved_bytes", r.reserved_bytes},
        {"peak_reserved_bytes", r.peak_reserved_bytes}, {"allocator_live_bytes", r.allocator_live_bytes},
        {"allocator_reserved_bytes", r.allocator_reserved_bytes}, {"allocator_peak_live_bytes", r.allocator_peak_live_bytes},
        {"allocator_peak_reserved_bytes", r.allocator_peak_reserved_bytes}, {"total_device_bytes", r.total_device_bytes},
        {"initial_free_device_bytes", r.initial_free_device_bytes}, {"runtime_headroom_bytes", r.runtime_headroom_bytes},
        {"vulkan_shared_workspace_limit_bytes", r.vulkan_shared_workspace_limit_bytes},
        {"vulkan_leased_bytes", r.vulkan_leased_bytes},
        {"vulkan_peak_leased_bytes", r.vulkan_peak_leased_bytes},
        {"vulkan_peak_owner_lease_bytes", r.vulkan_peak_owner_lease_bytes},
        {"vulkan_waiting_sessions", r.vulkan_waiting_sessions},
        {"native_coarse_workspace_sessions", r.native_coarse_workspace_sessions},
        {"device_admission_waiters", r.device_admission_waiters},
        {"fft_plan_cache_count", r.fft_plan_cache_count}, {"fft_plan_cache_limit", r.fft_plan_cache_limit},
        {"upload_bytes", r.upload_bytes},
        {"download_bytes", r.download_bytes}, {"synchronizations", r.synchronizations},
        {"fft_calls", r.fft_calls}, {"fft_rows", r.fft_rows}, {"fir_input_rows", r.fir_input_rows},
        {"fir_output_contributions", r.fir_output_contributions}, {"score_rows", r.score_rows},
        {"cuda_arrival_peak_rows", r.cuda_arrival_peak_rows},
        {"cuda_arrival_peak_values", r.cuda_arrival_peak_values},
        {"cuda_half_fft_forward_ffts", r.cuda_half_fft_forward_ffts},
        {"cuda_segmented_sessions", r.cuda_segmented_sessions},
        {"cuda_segmented_forward_ffts", r.cuda_segmented_forward_ffts},
        {"cuda_segmented_inverse_ffts", r.cuda_segmented_inverse_ffts},
        {"cuda_segmented_score_rows", r.cuda_segmented_score_rows},
        {"cuda_segmented_native_fallbacks", r.cuda_segmented_native_fallbacks},
        {"vulkan_segmented_sessions", r.vulkan_segmented_sessions},
        {"vulkan_segmented_forward_ffts", r.vulkan_segmented_forward_ffts},
        {"vulkan_segmented_inverse_ffts", r.vulkan_segmented_inverse_ffts},
        {"vulkan_segmented_score_rows", r.vulkan_segmented_score_rows},
        {"vulkan_segmented_qualified_device", r.vulkan_segmented_qualified_device},
        {"vulkan_segmented_planned_shapes", r.vulkan_segmented_planned_shapes},
        {"vulkan_segmented_declined_calls", r.vulkan_segmented_declined_calls},
        {"vulkan_segmented_decline_reason", r.vulkan_segmented_decline_reason},
        {"vulkan_segmented_native_fallbacks", r.vulkan_segmented_native_fallbacks},
        {"vulkan_segmented_fallback_reason", r.vulkan_segmented_fallback_reason},
        {"cuda_half_fft_inverse_ffts", r.cuda_half_fft_inverse_ffts},
        {"cuda_half_fft_padded_ffts", r.cuda_half_fft_padded_ffts},
        {"cuda_half_fft_score_rows", r.cuda_half_fft_score_rows},
        {"cuda_half_fft_peak_bank_bytes", r.cuda_half_fft_peak_bank_bytes},
        {"cuda_half_fft_peak_workspace_bytes", r.cuda_half_fft_peak_workspace_bytes},
        {"cuda_half_fft_peak_phase_bytes", r.cuda_half_fft_peak_phase_bytes},
        {"cuda_half_fft_plan_seconds", r.cuda_half_fft_plan_seconds},
        {"half_fft_fallback_counts", std::move(half_fft_fallbacks)},
        {"vulkan", r.vulkan}, {"vulkan_api_version", r.vulkan_api_version},
        {"vulkan_driver_version", r.vulkan_driver_version}, {"vulkan_vendor_id", r.vulkan_vendor_id},
        {"vulkan_device_id", r.vulkan_device_id}, {"vulkan_subgroup_size", r.vulkan_subgroup_size},
        {"vulkan_float16", r.vulkan_float16}, {"vulkan_int8", r.vulkan_int8},
        {"vulkan_integer_dot8_accelerated", r.vulkan_integer_dot8_accelerated},
        {"unified_memory", r.unified_memory}, {"workspace_scope", r.workspace_scope},
        {"vulkan_peak_owner_bytes", r.vulkan_peak_owner_bytes},
        {"vulkan_active_sessions", r.vulkan_active_sessions},
        {"vulkan_fft_plan_seconds", r.vulkan_fft_plan_seconds},
        {"vulkan_shader_compile_seconds", r.vulkan_shader_compile_seconds},
        {"host_cache_limit_bytes", r.host_cache_limit_bytes}, {"host_cache_bytes", r.host_cache_bytes},
        {"host_cache_entries", r.host_cache_entries},
        {"vulkan_fft_cache_hits", r.vulkan_fft_cache_hits},
        {"vulkan_fft_cache_misses", r.vulkan_fft_cache_misses},
        {"vulkan_dispatches", r.vulkan_dispatches},
        {"vulkan_timed_submissions", r.vulkan_timed_submissions},
        {"vulkan_device_command_seconds", r.vulkan_device_command_seconds},
        {"vulkan_shader_spirv_compiles", r.vulkan_shader_spirv_compiles},
        {"vulkan_shader_spirv_cache_hits", r.vulkan_shader_spirv_cache_hits},
        {"vulkan_arrival_peak_rows", r.vulkan_arrival_peak_rows},
        {"vulkan_arrival_peak_values", r.vulkan_arrival_peak_values},
        {"vulkan_host_scored_rows", r.vulkan_host_scored_rows},
        {"vulkan_fp32_score_rows", r.vulkan_fp32_score_rows},
        {"vulkan_fp16_storage_score_rows", r.vulkan_fp16_storage_score_rows},
        {"vulkan_direct_dot_terms", r.vulkan_direct_dot_terms},
        {"vulkan_direct_fp32_score_rows", r.vulkan_direct_fp32_score_rows},
        {"vulkan_direct_fp16_score_rows", r.vulkan_direct_fp16_score_rows},
        {"vulkan_direct_fp16_storage_score_rows", r.vulkan_direct_fp16_storage_score_rows},
        {"vulkan_direct_dot_seconds", r.vulkan_direct_dot_seconds},
        {"native_coarse_lag_sessions", r.native_coarse_lag_sessions},
        {"native_coarse_precision_sessions", r.native_coarse_precision_sessions},
        {"native_fine_sessions", r.native_fine_sessions},
        {"native_fine_fallback_reason", r.native_fine_fallback_reason},
        {"statistics_blocks", r.statistics_blocks}, {"projected_values", r.projected_values},
        {"dc_reduction_values", r.dc_reduction_values},
        {"mapped_columns", r.mapped_columns}, {"mapped_values", r.mapped_values},
        {"cuda_fused_mapped_launches", r.cuda_fused_mapped_launches},
        {"cuda_fused_mapped_values", r.cuda_fused_mapped_values},
        {"cuda_nvfp4_fine_batches", r.cuda_nvfp4_fine_batches},
        {"cuda_nvfp4_fine_modes", r.cuda_nvfp4_fine_modes},
        {"cuda_nvfp4_fine_workspace_fallbacks", r.cuda_nvfp4_fine_workspace_fallbacks},
        {"cuda_nvfp4_fine_algorithm_fallbacks", r.cuda_nvfp4_fine_algorithm_fallbacks},
        {"cuda_cufft_fine_modes", r.cuda_cufft_fine_modes},
        {"opencl_nvfp4_fine_batches", r.opencl_nvfp4_fine_batches},
        {"opencl_nvfp4_pinned_batches", r.opencl_nvfp4_pinned_batches},
        {"opencl_nvfp4_fine_modes", r.opencl_nvfp4_fine_modes},
        {"opencl_nvfp4_native_blocks", r.opencl_nvfp4_native_blocks},
        {"opencl_resident_fine_batches", r.opencl_resident_fine_batches},
        {"opencl_resident_fine_pairs", r.opencl_resident_fine_pairs},
        {"opencl_resident_nvfp4_modes", r.opencl_resident_nvfp4_modes},
        {"opencl_resident_fft_modes", r.opencl_resident_fft_modes},
        {"opencl_resident_upload_bytes", r.opencl_resident_upload_bytes},
        {"opencl_resident_download_bytes", r.opencl_resident_download_bytes},
        {"opencl_resident_direct_columns", r.opencl_resident_direct_columns},
        {"opencl_resident_dispatches", r.opencl_resident_dispatches},
        {"opencl_resident_blocking_writes", r.opencl_resident_blocking_writes},
        {"opencl_resident_async_writes", r.opencl_resident_async_writes},
        {"opencl_resident_mapping_dispatches", r.opencl_resident_mapping_dispatches},
        {"opencl_resident_fine_blocks", r.opencl_resident_fine_blocks},
        {"opencl_resident_rendered_tiles", r.opencl_resident_rendered_tiles},
        {"opencl_resident_keyed_lanes", r.opencl_resident_keyed_lanes},
        {"opencl_resident_window_blocks", r.opencl_resident_window_blocks},
        {"opencl_resident_reduced_windows", r.opencl_resident_reduced_windows},
        {"opencl_resident_fp32_mapped_columns", r.opencl_resident_fp32_mapped_columns},
        {"opencl_resident_fp64_mapped_columns", r.opencl_resident_fp64_mapped_columns},
        {"opencl_resident_fp32_rendered_tiles", r.opencl_resident_fp32_rendered_tiles},
        {"opencl_resident_fp64_rendered_tiles", r.opencl_resident_fp64_rendered_tiles},
        {"opencl_resident_host_source_seconds", r.opencl_resident_host_source_seconds},
        {"opencl_resident_host_staging_seconds", r.opencl_resident_host_staging_seconds},
        {"opencl_resident_host_transfer_seconds", r.opencl_resident_host_transfer_seconds},
        {"opencl_resident_host_wait_seconds", r.opencl_resident_host_wait_seconds},
        {"opencl_resident_device_transfer_seconds", r.opencl_resident_device_transfer_seconds},
        {"opencl_resident_device_render_seconds", r.opencl_resident_device_render_seconds},
        {"opencl_resident_device_map_seconds", r.opencl_resident_device_map_seconds},
        {"opencl_resident_device_pack_seconds", r.opencl_resident_device_pack_seconds},
        {"opencl_resident_device_product_seconds", r.opencl_resident_device_product_seconds},
        {"opencl_resident_device_statistics_seconds", r.opencl_resident_device_statistics_seconds},
        {"opencl_resident_device_span_seconds", r.opencl_resident_device_span_seconds},
        {"opencl_coarse_score_rows", r.opencl_coarse_score_rows},
        {"opencl_coarse_rescored_peaks", r.opencl_coarse_rescored_peaks},
        {"opencl_coarse_issued_products", r.opencl_coarse_issued_products},
        {"opencl_coarse_useful_products", r.opencl_coarse_useful_products},
        {"opencl_coarse_synthesized_columns", r.opencl_coarse_synthesized_columns},
        {"opencl_coarse_uploaded_columns", r.opencl_coarse_uploaded_columns},
        {"opencl_coarse_upload_bytes", r.opencl_coarse_upload_bytes},
        {"opencl_coarse_download_bytes", r.opencl_coarse_download_bytes},
        {"opencl_coarse_dispatches", r.opencl_coarse_dispatches},
        {"opencl_coarse_blocking_transfers", r.opencl_coarse_blocking_transfers},
        {"opencl_coarse_sessions", r.opencl_coarse_sessions},
        {"opencl_coarse_frequency_fallbacks", r.opencl_coarse_frequency_fallbacks},
        {"opencl_coarse_fallback_counts", std::move(dense_coarse_fallbacks)},
        {"opencl_coarse_rescore_fp32_partial_peaks", r.opencl_coarse_rescore_fp32_partial_peaks},
        {"opencl_coarse_rescore_fp64_peaks", r.opencl_coarse_rescore_fp64_peaks},
        {"opencl_coarse_product_seconds", r.opencl_coarse_product_seconds},
        {"opencl_coarse_packing_seconds", r.opencl_coarse_packing_seconds},
        {"opencl_coarse_selection_seconds", r.opencl_coarse_selection_seconds},
        {"opencl_coarse_synthesis_seconds", r.opencl_coarse_synthesis_seconds},
        {"opencl_coarse_render_seconds", r.opencl_coarse_render_seconds},
        {"opencl_coarse_filter_seconds", r.opencl_coarse_filter_seconds},
        {"opencl_coarse_gram_seconds", r.opencl_coarse_gram_seconds},
        {"opencl_coarse_rescore_seconds", r.opencl_coarse_rescore_seconds},
        {"cuda_direct_gram_values", r.cuda_direct_gram_values},
        {"cuda_bmm_gram_values", r.cuda_bmm_gram_values},
        {"cuda_matrix_fp16_calls", r.cuda_matrix_fp16_calls},
        {"cuda_matrix_int8_calls", r.cuda_matrix_int8_calls},
        {"cuda_matrix_inner_chunks", r.cuda_matrix_inner_chunks},
        {"cuda_matrix_lt_hmma_calls", r.cuda_matrix_lt_hmma_calls},
        {"cuda_matrix_lt_imma_calls", r.cuda_matrix_lt_imma_calls},
        {"cuda_matrix_unwitnessed_calls", r.cuda_matrix_unwitnessed_calls},
        {"cuda_matrix_padded_real_macs", r.cuda_matrix_padded_real_macs},
        {"cuda_matrix_packed_values", r.cuda_matrix_packed_values},
        {"cuda_matrix_prepared_b_groups", r.cuda_matrix_prepared_b_groups},
        {"cuda_matrix_prepared_b_bytes", r.cuda_matrix_prepared_b_bytes},
        {"cuda_matrix_prepared_b_values", r.cuda_matrix_prepared_b_values},
        {"cuda_matrix_prepared_b_uses", r.cuda_matrix_prepared_b_uses},
        {"cuda_matrix_output_values", r.cuda_matrix_output_values},
        {"cuda_matrix_maximum_rows", r.cuda_matrix_maximum_rows},
        {"cuda_matrix_maximum_columns", r.cuda_matrix_maximum_columns},
        {"cuda_matrix_maximum_inner", r.cuda_matrix_maximum_inner},
        {"cuda_matrix_nonfinite_values", r.cuda_matrix_nonfinite_values},
        {"cuda_matrix_saturated_values", r.cuda_matrix_saturated_values},
        {"cuda_matrix_maximum_a_scale", r.cuda_matrix_maximum_a_scale},
        {"cuda_matrix_maximum_b_scale", r.cuda_matrix_maximum_b_scale},
        {"projection_index_cache_hits", r.projection_index_cache_hits},
        {"projection_index_cache_uploads", r.projection_index_cache_uploads},
        {"mapping_offset_cache_hits", r.mapping_offset_cache_hits},
        {"mapping_offset_cache_uploads", r.mapping_offset_cache_uploads},
        {"matrix_fft_fallback_counts", std::move(matrix_fallbacks)},
        {"uploaded_tiles", r.uploaded_tiles}, {"tile_hits", r.tile_hits},
        {"direct_columns", r.direct_columns}, {"direct_values", r.direct_values},
        {"direct_fallback_counts", std::move(fallbacks)}, {"wall_seconds", r.wall_seconds},
        {"selection_reason", r.selection_reason}});
    // Fine-stage device families, vulkan_<family>_<field>.
    for (const auto family : stage_families) {
        const auto& stage = vulkan_stage_receipt(r, family);
        const auto prefix = "vulkan_" + std::string(stage_family_receipt_names[static_cast<std::size_t>(family)]) + "_";
        result[prefix + "sessions"] = stage.sessions;
        result[prefix + "forward_ffts"] = stage.forward_ffts;
        result[prefix + "inverse_ffts"] = stage.inverse_ffts;
        result[prefix + "fft_row_stages"] = stage.fft_row_stages;
        result[prefix + "score_rows"] = stage.score_rows;
        result[prefix + "planned_calls"] = stage.planned_calls;
        result[prefix + "declined_calls"] = stage.declined_calls;
        result[prefix + "native_fallbacks"] = stage.native_fallbacks;
        result[prefix + "upload_bytes"] = stage.upload_bytes;
        result[prefix + "device_calls"] = stage.device_calls;
        result[prefix + "download_bytes"] = stage.download_bytes;
        result[prefix + "submissions"] = stage.submissions;
        result[prefix + "timed_submissions"] = stage.timed_submissions;
        result[prefix + "batch_width"] = stage.batch_width;
        result[prefix + "mutex_wait_seconds"] = stage.mutex_wait_seconds;
        result[prefix + "submission_seconds"] = stage.submission_seconds;
        result[prefix + "device_seconds"] = stage.device_seconds;
        result[prefix + "plan_seconds"] = stage.plan_seconds;
        result[prefix + "shader_seconds"] = stage.shader_seconds;
        result[prefix + "decline_reason"] = stage.decline_reason;
        result[prefix + "fallback_reason"] = stage.fallback_reason;
        result[prefix + "qualified_device"] = stage.qualified_device;
    }
    result["vulkan_gain_rank_frontier_lags"] = r.vulkan_gain_rank_frontier_lags;
    result["vulkan_gain_rank_rescored_lags"] = r.vulkan_gain_rank_rescored_lags;
    result["vulkan_gain_rank_rescore_seconds"] = r.vulkan_gain_rank_rescore_seconds;
    result["vulkan_gain_rank_band_ratio"] = r.vulkan_gain_rank_band_ratio;
    result["vulkan_gain_rank_band_source"] = r.vulkan_gain_rank_band_source;
    result["vulkan_drift_ridge_cells"] = r.vulkan_drift_ridge_cells;
    result["vulkan_drift_slope_ffts"] = r.vulkan_drift_slope_ffts;
    result["vulkan_varying_blocks"] = r.vulkan_varying_blocks;
    result["vulkan_varying_batches"] = r.vulkan_varying_batches;
    return result;
}

std::shared_ptr<ReceiverBulkBackend> selected_backend(const Options& o) {
    if (o.backend == "cpu") {
        if (o.tensor_coarse_math != "fft")
            throw Unavailable("Tensor coarse arithmetic requires --backend tensor-cpu or cuda");
        if (o.require_accelerator) throw Unavailable("An accelerated probe requires --backend cuda or vulkan");
        return {};
    }
    if (o.backend == "vulkan") {
        if (o.require_accelerator && (o.command == "codec" || o.work_only))
            throw Unavailable("Vulkan accelerator qualification requires a complete coarse-search workload");
#ifdef DATAPUMP_HAVE_TRIAD_VULKAN
        try {
            const auto precision = o.vulkan_precision == "fp16-storage" ? VulkanCoarsePrecision::fp16_storage :
                o.vulkan_precision == "direct-fp32" ? VulkanCoarsePrecision::direct_fp32 :
                o.vulkan_precision == "direct-fp16-storage" ? VulkanCoarsePrecision::direct_fp16_storage :
                o.vulkan_precision == "direct-fp16" ? VulkanCoarsePrecision::direct_fp16 :
                o.vulkan_precision == "nvfp4" ? VulkanCoarsePrecision::opencl_nvfp4 :
                o.vulkan_precision == "fp32" ? VulkanCoarsePrecision::fp32 : VulkanCoarsePrecision::automatic;
            VulkanReceiverOptions options{o.device_index,
                o.device_memory_limit.value_or(256 * 1024 * 1024), precision};
            // The explicit dense route reports its device command intervals.
            // The resident fine session is timed only on request: it reads
            // the clock around every staged column.
            options.profile_opencl = o.opencl_profile.value_or(precision == VulkanCoarsePrecision::opencl_nvfp4);
            options.profile_opencl_fine = o.opencl_profile.value_or(false);
            options.opencl_device_templates = o.opencl_coarse_templates == "device";
            options.opencl_fine_device_tiles = o.opencl_fine_tiles == "device";
            options.opencl_fine_window_blocks = o.opencl_fine_window;
            options.opencl_fine_stream_blocks = o.opencl_fine_stream;
            options.opencl_fine_fp32 = o.opencl_fine_map == "fp32";
            options.opencl_coarse_fp32_rescore = o.opencl_coarse_rescore == "fp32";
            if (o.vulkan_fine_projection == "nvfp4") options.fine_projection = VulkanFineProjection::opencl_nvfp4;
            else if (o.vulkan_fine_projection == "resident-nvfp4")
                options.fine_projection = VulkanFineProjection::opencl_resident_nvfp4;
            else if (o.vulkan_fine_projection == "resident-fft")
                options.fine_projection = VulkanFineProjection::opencl_resident_fft;
            else if (o.vulkan_fine_projection == "native") options.fine_projection = VulkanFineProjection::native;
            if (o.vulkan_segmented_timing == "device") options.segmented_timing = VulkanSegmentedTiming::device;
            else if (o.vulkan_segmented_timing == "native") options.segmented_timing = VulkanSegmentedTiming::native;
            const auto stage_route = [](const std::string& route) {
                return route == "device" ? VulkanStageRoute::device :
                    route == "native" ? VulkanStageRoute::native : VulkanStageRoute::automatic;
            };
            options.gain_rank = stage_route(o.vulkan_gain_rank);
            options.drift_proposals = stage_route(o.vulkan_drift);
            options.varying_fine = stage_route(o.vulkan_varying_fine);
            options.varying_arithmetic = o.vulkan_varying_arithmetic == "fp64" ? VulkanVaryingArithmetic::fp64 :
                o.vulkan_varying_arithmetic == "fp32-template" ? VulkanVaryingArithmetic::fp32_template
                                                               : VulkanVaryingArithmetic::automatic;
            return make_vulkan_receiver_backend(options);
        } catch (const VulkanBackendUnavailable& error) {
            throw Unavailable(error.what());
        }
#else
        throw Unavailable("This native probe build does not include the optional Vulkan backend");
#endif
    }
    if (o.backend == "gpu") {
        if (o.require_accelerator && (o.command == "codec" || o.work_only))
            throw Unavailable("GPU accelerator qualification requires a complete coarse-search workload");
        try {
            return make_gpu_receiver_backend({o.gpu_fp32_minimum, o.opencl_coarse_one_ray});
        } catch (const GpuBackendUnavailable& error) {
            throw Unavailable(error.what());
        } catch (const TensorBackendUnavailable& error) {
            throw Unavailable(error.what());
        } catch (const VulkanBackendUnavailable& error) {
            throw Unavailable(error.what());
        }
    }
    if (o.require_accelerator && o.backend != "cuda")
        throw Unavailable("An accelerated probe requires --backend cuda or vulkan");
#ifdef DATAPUMP_HAVE_TRIAD_TENSOR
    try {
        return make_tensor_receiver_backend({o.backend == "cuda" ? TensorDevice::cuda : TensorDevice::cpu,
            o.device_index, o.device_memory_limit.value_or(256 * 1024 * 1024),
            o.tensor_precision == "fp64" ? TensorPrecision::fp64 : TensorPrecision::automatic,
            o.tensor_coarse_math == "fp16-fft" ? TensorCoarseMath::fp16_fft :
                o.tensor_coarse_math == "fp16" ? TensorCoarseMath::fp16 :
                    o.tensor_coarse_math == "int8" ? TensorCoarseMath::int8 : TensorCoarseMath::fft,
            o.cuda_segmented_timing, o.tensor_fine_projection == "nvfp4", o.tensor_fine_projection == "cufft"});
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

struct SecondaryEmission {
    const PublicProfile& waveform;
    const Crypto& key;
    EmissionOptions options;
    std::uint64_t origin;
    Complex gain;
    double frequency_offset_hz;
};

CaptureKey::CaptureKey(const Options& o, std::span<const EmissionOptions> episodes, std::uint64_t first,
                       std::uint64_t end, bool spooled, const SecondaryEmission* secondary) {
    const auto bits = [](double value) { return std::to_string(std::bit_cast<std::uint64_t>(value)); };
    const auto& identity = capture_binary_identity();
    std::string text = "datapump.triad.probe-capture-key.v3\nexecutable_sha256 " + identity.executable_sha256 +
        "\nlibc_version " + identity.libc_version + "\nmath_dispatch " + identity.math_dispatch + "\n";
    for (const auto& [file, sha] : identity.runtime_libraries)
        text += "runtime_library " + std::filesystem::path(file).filename().string() + " " + sha + "\n";
    text += "command " + o.command + "\n";
    for (const auto& [flag, value] : o.capture_arguments) text += "argument " + flag + " " + value + "\n";
    text += "extent " + std::to_string(first) + " " + std::to_string(end) + " spool " + (spooled ? "1" : "0") + "\n";
    for (const auto& episode : episodes) {
        text += "episode " + std::to_string(static_cast<int>(episode.mode)) + " " + std::to_string(episode.first_slot) +
            " " + std::to_string(episode.slot_count) + " " + std::to_string(episode.source_first_sample) + " " +
            (episode.bit_at ? "drafted" : "fixed") + " " + hex_bytes(episode.bits) + "\n";
    }
    if (secondary)
        text += "secondary " + std::to_string(secondary->origin) + " " + bits(secondary->gain.real()) + " " +
            bits(secondary->gain.imag()) + " " + bits(secondary->frequency_offset_hz) + " " +
            hex_bytes(secondary->options.bits) + "\n";
    Digest digest;
    digest.add(std::as_bytes(std::span<const char>(text)));
    hex = digest.finish();
    key = digest_bytes(hex);
    path = o.capture_cache + "/" + hex + ".capture";
}

// The largest channel read in rows, a power-of-two multiple of input_chunk.
// A read prepares its first block, and passes its last block, alone; larger
// reads leave fewer such blocks. The spool still receives bounded chunks.
constexpr std::size_t channel_chunk = 65536;
static_assert(channel_chunk % input_chunk == 0);

// The probe's sum of emissions, one sample at a time or through the
// readers of every component. A reader evaluates the components in order and
// a range is accounted component by component.
class CompositeSource {
public:
    CompositeSource(std::vector<EmittedSource>& inputs, std::vector<std::uint64_t>& origins,
                    const SecondaryEmission* secondary, std::size_t secondary_index)
        : inputs_(inputs), origins_(origins), secondary_(secondary), secondary_index_(secondary_index) {}

    Complex sample(std::uint64_t i, const std::function<Complex(std::size_t, std::uint64_t)>& component) const {
        Complex result{};
        for (std::size_t n = 0; n < inputs_.size(); ++n) {
            if (i < origins_[n] || i - origins_[n] >= inputs_[n].geometry.source_samples) continue;
            const auto local = i - origins_[n];
            auto sample = component(n, local);
            if (secondary_ && n == secondary_index_) {
                sample = secondary_->gain * sample;
                if (secondary_->frequency_offset_hz != 0) {
                    const long double cycles = static_cast<long double>(local) * secondary_->frequency_offset_hz /
                        secondary_->waveform.effective_sample_rate();
                    sample *= std::polar(1., 2 * std::numbers::pi * static_cast<double>(std::remainder(cycles, 1.L)));
                }
            }
            result += sample;
        }
        return result;
    }
    bool parallel() const {
        return std::ranges::all_of(inputs_, [](const EmittedSource& input) { return input.readers != nullptr; });
    }

    class Readers final : public EmittedSourceReaders {
    public:
        explicit Readers(const CompositeSource& owner) : owner_(owner) {}
        std::size_t reader_bytes(std::size_t readers, std::uint64_t plan) const override {
            std::size_t bytes = readers * sizeof(std::uint64_t);
            for (const auto& input : owner_.inputs_) bytes += input.readers->reader_bytes(readers, plan);
            return bytes;
        }
        void open(std::size_t readers, std::uint64_t plan) override {
            for (auto& input : owner_.inputs_) input.readers->open(readers, plan);
            failed_.assign(readers, {});
            ranges_.assign(owner_.inputs_.size(), {});
        }
        void plan(std::uint64_t first, std::uint64_t count) override {
            first_ = first;
            for (std::size_t n = 0; n < owner_.inputs_.size(); ++n) {
                const auto origin = owner_.origins_[n];
                const auto stop = origin + owner_.inputs_[n].geometry.source_samples;
                const auto lo = std::max(first, origin), hi = std::min(first + count, stop);
                ranges_[n] = {lo, std::max(lo, hi)};
                if (lo < hi) owner_.inputs_[n].readers->plan(lo - origin, hi - lo);
            }
            for (auto& failure : failed_) failure = {};
        }
        Complex evaluate(std::size_t reader, std::uint64_t index) override {
            return owner_.sample(index, [&](std::size_t n, std::uint64_t local) {
                try {
                    return owner_.inputs_[n].readers->evaluate(reader, local);
                } catch (...) {
                    failed_[reader] = {index, n};
                    throw;
                }
            });
        }
        std::uint64_t accept(std::uint64_t count, bool failed, std::exception_ptr& error) override {
            // The component that failed the next sample; earlier components
            // generated it, later ones did not reach it.
            const auto next = first_ + count;
            auto component = owner_.inputs_.size();
            if (failed)
                for (const auto& failure : failed_)
                    if (failure.index == next) component = failure.component;
            auto stop = next;
            for (std::size_t n = 0; n < owner_.inputs_.size(); ++n) {
                const auto [lo, hi] = ranges_[n];
                const auto through = n < component && failed ? next + 1 : stop;
                const auto end = std::max(lo, std::min(hi, through));
                if (lo == hi) continue;
                std::exception_ptr check;
                const auto accepted = owner_.inputs_[n].readers->accept(end - lo,
                    n == component && next >= lo && next < hi, check);
                if (check) {
                    stop = std::min(stop, lo + accepted);
                    if (!error) error = check;
                }
            }
            first_ = stop;
            return stop - (next - count);
        }
    private:
        struct Failure { std::uint64_t index = UINT64_MAX; std::size_t component = 0; };
        const CompositeSource& owner_;
        std::vector<Failure> failed_;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges_;
        std::uint64_t first_ = 0;
    };

private:
    std::vector<EmittedSource>& inputs_;
    std::vector<std::uint64_t>& origins_;
    const SecondaryEmission* secondary_;
    std::size_t secondary_index_;
};

std::uint64_t maximum_spool_bytes(const Options& o);

// The rows a capture cache file may hold: those of the spool budget for a
// spooled capture, and of the sample budget for a capture held in RAM.
std::uint64_t capture_cache_row_bytes(const Options& o, bool spooled) {
    if (spooled) return maximum_spool_bytes(o);
    return o.sample_limit > UINT64_MAX / ObservationSpool::row_bytes ? UINT64_MAX :
        o.sample_limit * ObservationSpool::row_bytes;
}

// A capture that an earlier run of this binary stored under the same key:
// its rows are read in order and hashed, and the run stops unless every
// header field, every row and the SHA-256 equal what was generated. The
// rows are delivered exactly as generation delivers them.
Capture reuse_capture(CaptureCacheReader& reader, const CaptureKey& key, const Options& o, std::uint64_t first,
                      std::uint64_t end, std::unique_ptr<ObservationSpool> spool, std::stop_token stop) {
    const Timer timer;
    const auto rows = end - first;
    const auto& record = reader.header(key.key, first, rows,
        CaptureRecord::header_bytes + std::min(capture_cache_row_bytes(o, spool != nullptr),
                                               UINT64_MAX - CaptureRecord::header_bytes));
    Capture capture;
    capture.first = first;
    capture.spool = std::move(spool);
    if (!capture.spool) {
        capture.iq.resize(static_cast<std::size_t>(rows));
        capture.valid.resize(static_cast<std::size_t>(rows));
    }
    std::vector<Complex> iq(input_chunk);
    Bytes valid(input_chunk);
    CaptureHasher digest;
    for (std::uint64_t at = 0; at < rows;) {
        check_stop(stop);
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(rows - at, input_chunk));
        const auto samples = std::span(iq).first(n);
        const auto mask = std::span(valid).first(n);
        reader.read_rows(samples, mask);
        digest.add(samples, mask);
        if (capture.spool) {
            const Timer io;
            capture.spool->append(samples, mask);
            add_time(capture.spool_write, io.elapsed());
        } else {
            std::ranges::copy(samples, capture.iq.begin() + static_cast<std::ptrdiff_t>(at));
            std::ranges::copy(mask, capture.valid.begin() + static_cast<std::ptrdiff_t>(at));
        }
        at += n;
    }
    reader.finish();
    if (capture.spool) {
        const Timer io;
        capture.spool->seal();
        add_time(capture.spool_write, io.elapsed());
    }
    capture.digest = digest.finish();
    if (digest_bytes(capture.digest) != record.digest)
        throw Error("Capture cache rows differ from their stored SHA-256; remove " + key.path);
    capture.generated_samples = end;
    capture.channel.samples = end;
    capture.channel.erased_samples = record.erased_samples;
    capture.channel.working_bytes = static_cast<std::size_t>(record.working_bytes);
    capture.selective_filter = record.selective_filter;
    if (const auto& b = record.bounded_spread) {
        auto& r = capture.channel.bounded_spread.emplace();
        r.excess_delay_seconds = b->excess_delay_seconds;
        r.knot_seconds = b->knot_seconds;
        r.doppler_bound_hz = b->doppler_bound_hz;
        r.minimum_echo_ratio = b->minimum_echo_ratio;
        r.maximum_echo_ratio = b->maximum_echo_ratio;
        r.fixed_direct_power_fraction = b->fixed_direct_power_fraction;
        r.minimum_doppler_hz = b->minimum_doppler_hz;
        r.maximum_doppler_hz = b->maximum_doppler_hz;
        r.minimum_combined_physical_doppler_hz = b->minimum_combined_physical_doppler_hz;
        r.maximum_combined_physical_doppler_hz = b->maximum_combined_physical_doppler_hz;
        r.seed = b->seed;
        r.processed_rows = b->processed_rows;
    }
    if (const auto& c = record.common_doppler) {
        auto& r = capture.channel.common_doppler_wander.emplace();
        r.bound_hz = c->bound_hz;
        r.knot_seconds = c->knot_seconds;
        r.minimum_hz = c->minimum_hz;
        r.maximum_hz = c->maximum_hz;
        r.seed = c->seed;
        r.processed_rows = c->processed_rows;
    }
    capture.cache = Capture::Cache{key.hex, key.path, true, false, record.file_bytes(), {0, 0}};
    capture.generation = timer.elapsed();
    return capture;
}

CaptureRecord capture_record(const CaptureKey& key, const Capture& capture, std::uint64_t end) {
    CaptureRecord record;
    record.key = key.key;
    record.digest = digest_bytes(capture.digest);
    record.first = capture.first;
    record.rows = end - capture.first;
    record.generated = end;
    record.erased_samples = capture.channel.erased_samples;
    record.working_bytes = capture.channel.working_bytes;
    record.selective_filter = capture.selective_filter;
    if (const auto& r = capture.channel.bounded_spread)
        record.bounded_spread = CaptureRecord::BoundedSpread{r->excess_delay_seconds, r->knot_seconds,
            r->doppler_bound_hz, r->minimum_echo_ratio, r->maximum_echo_ratio, r->fixed_direct_power_fraction,
            r->minimum_doppler_hz, r->maximum_doppler_hz, r->minimum_combined_physical_doppler_hz,
            r->maximum_combined_physical_doppler_hz, r->seed, r->processed_rows};
    if (const auto& r = capture.channel.common_doppler_wander)
        record.common_doppler = CaptureRecord::CommonDoppler{r->bound_hz, r->knot_seconds, r->minimum_hz,
            r->maximum_hz, r->seed, r->processed_rows};
    return record;
}

// Fixed, blind probe transform. The first projected row precedes the public
// observation and may lack the initial MA predecessor. Each retained row
// has complete real PCM and analytic FIR support.
class ColoredPcmCapture {
public:
    // AudioInput allows at most 2048 samples on either side. Charge its
    // ring, taps and masks together with one PCM and projected chunk.
    static constexpr auto workspace_bound = 128 * 1024 +
        input_chunk * (sizeof(float) + sizeof(Complex) + 2 * sizeof(std::uint8_t));

    ColoredPcmCapture(const Profile& profile, std::uint64_t first)
        : rate_(profile.waveform.config().format.sample_rate), carrier_(profile.receiver.data_if_hz),
          projector_(rate_, carrier_, profile.minimum_audio_hz, profile.maximum_audio_hz),
          halo_(projector_.halo_samples()),
          first_simulator_row_(first > halo_ + 1 ? first - halo_ - 1 : 0),
          prefix_rows_(first < halo_ + 1 ? halo_ + 1 - first : 0) {
        output_.reserve(input_chunk);
        output_valid_.reserve(input_chunk);
    }

    auto halo() const { return halo_; }
    auto first_simulator_row() const { return first_simulator_row_; }
    auto prefix_rows() const { return prefix_rows_; }
    auto written() const { return written_; }
    auto valid_count() const { return valid_count_; }

    void begin(std::uint64_t seed, double variance, Capture& capture, CaptureHasher& digest) {
        if (!prefix_rows_) return;
        require(prefix_rows_ <= pcm_.size(), "Colored PCM thermal prefix exceeds its buffer");
        std::mt19937_64 random(seed ^ 0x50434d5052454649ULL);
        std::normal_distribution<double> thermal(0., std::sqrt(variance / 2));
        for (std::size_t i = 0; i < prefix_rows_; ++i)
            push(i, static_cast<float>(thermal(random)), 1);
        project(static_cast<std::size_t>(prefix_rows_), capture, digest);
    }

    void append(std::span<const Complex> iq, std::span<const std::uint8_t> valid,
                Capture& capture, CaptureHasher& digest) {
        constexpr double scale = 1. / std::numbers::sqrt2;
        require(iq.size() <= pcm_.size() && valid.size() == iq.size(), "Invalid colored PCM input chunk");
        for (std::size_t i = 0; i < iq.size(); ++i)
            push(i, live::detail::real_pcm(iq[i], local_input_ + i, carrier_, rate_, scale), valid[i]);
        project(iq.size(), capture, digest);
    }

private:
    void push(std::size_t i, float real, std::uint8_t valid) {
        pcm_[i] = static_cast<float>((static_cast<double>(real) + .5 * previous_) / 1.5);
        pcm_valid_[i] = valid && previous_valid_;
        previous_ = real;
        previous_valid_ = valid;
    }

    void project(std::size_t count, Capture& capture, CaptureHasher& digest) {
        const auto projected_first = projector_.project(std::span(pcm_).first(count), output_,
            std::span(pcm_valid_).first(count), &output_valid_);
        local_input_ += count;
        const auto wanted_first = halo_ + 1;
        if (output_.empty() || projected_first + output_.size() <= wanted_first) return;
        const auto skip = projected_first < wanted_first ? wanted_first - projected_first : 0;
        const auto iq = std::span(output_).subspan(skip);
        const auto valid = std::span(output_valid_).subspan(skip);
        require(projected_first + skip - wanted_first == written_ &&
                written_ <= capture.iq.size() && iq.size() <= capture.iq.size() - written_,
                "Colored PCM projection did not preserve the public capture extent");
        digest.add(iq, valid);
        std::ranges::copy(iq, capture.iq.begin() + written_);
        std::ranges::copy(valid, capture.valid.begin() + written_);
        written_ += iq.size();
        valid_count_ += std::ranges::count(valid, std::uint8_t{1});
    }

    std::uint32_t rate_;
    double carrier_;
    live::detail::AudioInput projector_;
    std::uint64_t halo_, first_simulator_row_, prefix_rows_;
    std::array<float, input_chunk> pcm_{};
    std::array<std::uint8_t, input_chunk> pcm_valid_{};
    std::vector<Complex> output_;
    Bytes output_valid_;
    double previous_ = 0;
    bool previous_valid_ = false;
    std::uint64_t local_input_ = 0;
    std::size_t written_ = 0, valid_count_ = 0;
};

Capture simulate(const Options& o, const Profile& profile, std::span<const EmissionOptions> episodes,
                 std::uint64_t first, std::uint64_t end, std::uint64_t& samples_left,
                 const StreamReceiverResources& resources, std::stop_token stop,
                 std::unique_ptr<ObservationSpool> spool = {}, const SecondaryEmission* secondary = nullptr) {
    // The canonical channel and one emitted source have a fixed 65 MiB bound.
    // Reserve this before creating either; account extra independently owned
    // source objects when exercising a second episode in the same channel.
    constexpr std::size_t channel_source_bound = 65 * 1024 * 1024;
    if (end <= first || end > samples_left || (!spool && end - first > SIZE_MAX / (sizeof(Complex) + 1)))
        throw Incomplete("Canonical capture exceeds the declared probe sample budget");
    const auto rows = end - first;
    constexpr auto row_bytes = sizeof(Complex) + sizeof(std::uint8_t);
    // A capture stored to --capture-cache also holds the writer's buffers;
    // reuse reads through smaller buffers inside the unused channel reserve.
    const auto retained_bytes = spool ? spool->working_bytes() + 4096 : static_cast<std::size_t>(rows) * row_bytes;
    const auto capture_overhead = (o.project_colored_pcm ? ColoredPcmCapture::workspace_bound : 0) +
        (o.capture_cache.empty() ? 0 : CaptureCacheWriter::buffer_bytes);
    if (capture_overhead > SIZE_MAX - retained_bytes)
        throw Incomplete("Canonical capture storage exceeds the local address range");
    const auto stored_bytes = retained_bytes + capture_overhead;
    const auto fill_sources = o.fills_scheduled_gaps() && o.gap_fill_transmit ? episodes.size() : 0;
    const auto simultaneous_sources = std::max(std::size_t{1}, episodes.size() + fill_sources + (secondary != nullptr));
    const auto reserve = simultaneous_sources * channel_source_bound;
    const auto remaining = memory_remaining(resources);
    // Two read buffers: one is delivered while the channel fills the other.
    // The capture needs at least two input_chunk reads.
    constexpr auto least_read_bytes = 2 * input_chunk * row_bytes;
    if (reserve > remaining || stored_bytes > remaining - reserve ||
        least_read_bytes > remaining - reserve - stored_bytes)
        throw Incomplete("Canonical capture and source preflight exceed the local probe memory limit");
    // The receiver has not started. Its workers evaluate the sources' power
    // references and prepare each block of the channel beside this thread's
    // sequential pass; the capture is the same for every allocation. Each
    // helper's stack is charged like a receiver worker's, and a limit without
    // room for one keeps this thread alone. The pool closes with this scope.
    // Helpers are admitted before larger reads: a read size never changes the
    // capture, so it only grows, up to channel_chunk rows, into what the
    // helpers' stacks leave.
    const auto stack = datapump::triad::v3::detail::stream_worker_stack_bytes();
    const auto spare = remaining - reserve - stored_bytes - least_read_bytes;
    const auto helpers = static_cast<unsigned>(std::min<std::size_t>(resources.workers ? resources.workers - 1 : 0,
        stack ? spare / stack : 0));
    const auto read_room = remaining - reserve - stored_bytes - helpers * stack;
    auto read_rows = input_chunk;
    while (read_rows < channel_chunk && 4 * read_rows * row_bytes <= read_room) read_rows *= 2;
    const auto capture_bytes = stored_bytes + 2 * read_rows * row_bytes;
    check_stop(stop);
    // --capture-cache: an earlier capture of this binary and these arguments
    // replaces generation; otherwise this one is stored as it is delivered.
    std::optional<CaptureKey> key;
    std::unique_ptr<CaptureCacheWriter> writer;
    if (!o.capture_cache.empty()) {
        key.emplace(o, episodes, first, end, spool != nullptr, secondary);
        if (rows > capture_cache_row_bytes(o, spool != nullptr) / ObservationSpool::row_bytes)
            throw Incomplete("Capture cache file exceeds the declared probe spool budget");
        if (auto reader = CaptureCacheReader::open(key->path)) {
            auto capture = reuse_capture(*reader, *key, o, first, end, std::move(spool), stop);
            capture.generation_reserved_bytes = resources.memory_limit_bytes - remaining + reserve + capture_bytes;
            if (capture.spool) capture.generation_reserved_bytes += capture.spool->resident_reservation_bytes();
            samples_left -= end;
            return capture;
        }
        writer = std::make_unique<CaptureCacheWriter>(key->temporary(), key->path, rows);
    }
    const Timer timer;
    Capture capture;
    capture.generation_helpers = helpers;
    capture.generation_stack_bytes = stack;
    capture.generation_read_rows = read_rows;
    CaptureHasher digest;
    std::unique_ptr<ColoredPcmCapture> pcm_projection;
    auto simulator_first = first;
    auto simulator_stop = end;
    if (o.project_colored_pcm) {
        pcm_projection = std::make_unique<ColoredPcmCapture>(profile, first);
        if (pcm_projection->halo() > samples_left - end)
            throw Incomplete("Colored PCM projection halo exceeds the declared probe sample budget");
        simulator_first = pcm_projection->first_simulator_row();
        simulator_stop += pcm_projection->halo();
    }
    Timing cache_write{0, 0};
    // The digest and the copy or spool of each read run in pieces in order,
    // one piece as an extra job of each lent block preparation of the next
    // read; this thread delivers what remains when that read returns.
    struct Delivery {
        std::span<const Complex> iq;
        std::span<const std::uint8_t> valid;
        std::uint64_t target = 0;
        std::size_t done = 0;
        std::exception_ptr error;
        bool pending() const { return done < iq.size() && !error; }
    } delivery;
    const auto deliver = [&] {
        const auto n = std::min(input_chunk, delivery.iq.size() - delivery.done);
        const auto samples = delivery.iq.subspan(delivery.done, n);
        const auto mask = delivery.valid.subspan(delivery.done, n);
        if (pcm_projection) {
            pcm_projection->append(samples, mask, capture, digest);
        } else {
            digest.add(samples, mask);
            if (writer) {
                const Timer io;
                writer->append(samples, mask);
                add_time(cache_write, io.elapsed());
            }
            if (capture.spool) {
                const Timer io;
                capture.spool->append(samples, mask);
                add_time(capture.spool_write, io.elapsed());
            } else {
                const auto target = static_cast<std::size_t>(delivery.target + delivery.done);
                std::ranges::copy(samples, capture.iq.begin() + target);
                std::ranges::copy(mask, capture.valid.begin() + target);
            }
        }
        delivery.done += n;
    };
    ReceiverExecutor lanes(helpers + 1);
    StreamChannelLanes lent;
    if (helpers)
        lent = {helpers + 1,
            [&](std::size_t jobs, const std::function<void(std::size_t)>& job) {
                const std::size_t extra = delivery.pending() ? 1 : 0;
                lanes.parallel(0, jobs + extra, helpers + 1, stop,
                    [&](unsigned, std::size_t index, std::stop_token) {
                        if (index < jobs) job(index);
                        else try { deliver(); } catch (...) { delivery.error = std::current_exception(); }
                    });
            }};
    std::vector<Emission> emitters;
    std::vector<EmittedSource> inputs;
    std::vector<std::uint64_t> origins;
    std::optional<PublicProfile> silent_transmitter;
    if (o.fills_scheduled_gaps() && !o.gap_fill_transmit) {
        auto config = profile.waveform.config();
        config.scheduled_gap_fill = false;
        config.scheduled_gap_bridge = false;
        silent_transmitter.emplace(std::move(config), profile.key);
    }
    const auto& transmit = silent_transmitter ? *silent_transmitter : profile.waveform;
    for (const auto& episode : episodes) {
        auto options = episode;
        options.lanes = lent;
        emitters.emplace_back(transmit, profile.key, options);
        inputs.push_back(emitters.back().source());
        origins.push_back(episode.source_first_sample);
    }
    if (secondary) {
        auto options = secondary->options;
        options.lanes = lent;
        emitters.emplace_back(secondary->waveform, secondary->key, options);
        inputs.push_back(emitters.back().source());
        // Emission's bounds are relative to its own configured IF. Move them
        // into the composite frame before the native channel checks Nyquist.
        inputs.back().minimum_frequency_hz += secondary->frequency_offset_hz;
        inputs.back().maximum_frequency_hz += secondary->frequency_offset_hz;
        origins.push_back(secondary->origin);
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
    const CompositeSource sum(inputs, origins, secondary, episodes.size());
    composite.sample_at = [&](std::uint64_t i) {
        return sum.sample(i, [&](std::size_t n, std::uint64_t local) { return inputs[n].sample_at(local); });
    };
    composite.readers = sum.parallel() ? std::make_shared<CompositeSource::Readers>(sum) : nullptr;
    auto configuration = profile.channel;
    configuration.minimum_capture_samples = simulator_stop;
    configuration.maximum_chunk_samples = read_rows;
    configuration.lanes = lent;
    StreamingChannel channel(std::move(composite), configuration);
    if (channel.total_samples() < simulator_stop)
        throw Error("Native channel ended before the public observation");
    capture.first = first;
    capture.spool = std::move(spool);
    capture.generation_reserved_bytes = resources.memory_limit_bytes - remaining + reserve + capture_bytes +
        helpers * stack;
    if (capture.spool) {
        // The caller removed this backing reservation before the preflight.
        capture.generation_reserved_bytes += capture.spool->resident_reservation_bytes();
    } else {
        capture.iq.resize(static_cast<std::size_t>(rows));
        capture.valid.resize(static_cast<std::size_t>(rows));
    }
    if (pcm_projection) pcm_projection->begin(o.seed, configuration.complex_noise_variance, capture, digest);
    std::array<std::vector<Complex>, 2> iq{std::vector<Complex>(read_rows), std::vector<Complex>(read_rows)};
    std::array<Bytes, 2> valid{Bytes(read_rows), Bytes(read_rows)};
    std::size_t buffer = 0;
    const auto finish_delivery = [&] {
        while (delivery.pending()) deliver();
        if (delivery.error) std::rethrow_exception(delivery.error);
    };
    while (channel.position() < simulator_stop) {
        check_stop(stop);
        const auto offset = channel.position();
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(simulator_stop - offset, read_rows));
        if (channel.read(std::span(iq[buffer]).first(chunk), std::span(valid[buffer]).first(chunk)) != chunk)
            throw Error("Native channel returned a partial public observation");
        finish_delivery();
        if (offset + chunk > simulator_first) {
            const auto begin = static_cast<std::size_t>(std::max(offset, simulator_first) - offset);
            delivery = {std::span<const Complex>(iq[buffer]).subspan(begin, chunk - begin),
                std::span<const std::uint8_t>(valid[buffer]).subspan(begin, chunk - begin),
                offset + begin - simulator_first, 0, {}};
            buffer ^= 1;
        }
    }
    finish_delivery();
    if (pcm_projection) {
        require(pcm_projection->written() == rows, "Colored PCM projection did not fill the public observation");
        capture.pcm_projection = Capture::PcmProjection{pcm_projection->halo(), simulator_first,
            pcm_projection->prefix_rows(), pcm_projection->valid_count()};
    }
    if (capture.spool) {
        const Timer io;
        capture.spool->seal();
        add_time(capture.spool_write, io.elapsed());
    }
    capture.generation = timer.elapsed();
    capture.channel = channel.report();
    capture.selective_filter = capture.channel.selective_perturbation.has_value();
    if (o.fills_scheduled_gaps()) {
        auto& filler = capture.gap_fill.emplace();
        for (const auto& emitter : emitters) {
            const auto& source = emitter.report();
            filler.samples += source.gap_fill_samples;
            filler.wanted_energy += source.data_energy;
            filler.energy += source.gap_fill_energy;
            filler.cross_energy += source.gap_bridge_cross_energy;
            filler.helper_energy += source.helper_energy;
        }
    }
    capture.generated_samples = simulator_stop;
    samples_left -= simulator_stop;
    capture.digest = digest.finish();
    if (writer) {
        const Timer io;
        const auto record = capture_record(*key, capture, end);
        writer->commit(record);
        add_time(cache_write, io.elapsed());
        capture.cache = Capture::Cache{key->hex, key->path, false, true, record.file_bytes(), cache_write};
    }
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

Json scheduled_fill_window_hashes(const Profile& profile, const Capture& capture, unsigned slots,
                                 std::stop_token stop) {
    struct Window {
        SampleRange observation, aperture;
        CaptureHasher full_hash, aperture_hash;
    };
    std::vector<Window> windows;
    windows.reserve(slots);
    for (unsigned index = 0; index < slots; ++index) {
        const auto slot = make_receiver_slot_plan(profile.waveform, profile.receiver, index);
        windows.push_back({{slot.first_sample, slot.stop_sample},
            {slot.protected_data_first_sample, slot.protected_data_stop_sample}, {}, {}});
    }
    SpoolReplay replay(*capture.spool);
    auto active = windows.begin();
    for (std::uint64_t first = 0; first < capture.rows();) {
        check_stop(stop);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(capture.rows() - first, input_chunk));
        replay.load(first, count);
        const auto end = first + count;
        while (active != windows.end() && active->observation.stop <= first) ++active;
        const auto add = [&](SampleRange range, CaptureHasher& hash) {
            const auto low = std::max(first, range.first), high = std::min(end, range.stop);
            if (low < high)
                hash.add(replay.iq().subspan(low - first, high - low), replay.valid().subspan(low - first, high - low));
        };
        for (auto it = active; it != windows.end() && it->observation.first < end; ++it) {
            add(it->observation, it->full_hash);
            add(it->aperture, it->aperture_hash);
        }
        first = end;
    }
    (void)replay.finish(capture, capture.rows());
    Json::Array receipts;
    for (auto& window : windows) {
        receipts.push_back(object({{"first_sample", window.observation.first},
            {"stop_sample", window.observation.stop}, {"input_sha256", window.full_hash.finish()},
            {"protected_first_sample", window.aperture.first}, {"protected_stop_sample", window.aperture.stop},
            {"protected_input_sha256", window.aperture.size() ? Json(window.aperture_hash.finish()) : Json{}}}));
    }
    return receipts;
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
    Json::Array gain_lags, gain_map_evidence;
    for (const auto lag : std::span(score.signed_lag_gain_proposal_lags).first(score.signed_lag_gain_proposal_maps))
        gain_lags.emplace_back(lag);
    for (const auto& map : std::span(score.signed_lag_gain_map_bit_evidence).first(score.signed_lag_gain_candidates))
        gain_map_evidence.emplace_back(scores(map));
    Json carrier_beat;
    if (const auto& beat = score.protected_carrier_beat) {
        Json::Array models;
        for (std::size_t i = 0; i < beat->explained_fraction.size(); ++i)
            models.push_back(object({{"explained_fraction", scores(beat->explained_fraction[i])},
                {"held_out_fraction", Json::Array{scores(beat->held_out_fraction[i][0]),
                                                 scores(beat->held_out_fraction[i][1])}}}));
        const auto& affine = beat->affine_support;
        carrier_beat = object({{"epoch_offset_seconds", beat->epoch_offset_seconds},
            {"arrival_seconds", beat->arrival_seconds}, {"clock_ppm", beat->clock_ppm},
            {"ordinary_bit", beat->ordinary_bit}, {"predicted_bit", beat->predicted_bit},
            {"affine_support", affine ? object({{"arrival_seconds", affine->arrival_seconds},
                {"frequency_hz", affine->frequency_hz}, {"clock_ppm", affine->clock_ppm},
                {"weighted_log_e", affine->log_e}}) : Json{}},
            {"direct_and_dispersive_models", std::move(models)}});
    }
    return object({
        {"index", score.index}, {"first_sample", score.first_sample}, {"stop_sample", score.stop_sample},
        {"decision", decision_name(score.decision)}, {"bit", score.bit ? Json(*score.bit) : Json{}},
        {"complete", score.complete}, {"all_valid", score.all_valid}, {"zero_h0_verified", score.zero_h0_verified},
        {"reason", score.reason}, {"operational_threshold", score.operational_threshold},
        {"admission_opportunity_scope", score.calendar_opportunities_per_day ? "fixed_public_calendar" : "unbound_reference"},
        {"admission_opportunities_per_day", score.calendar_opportunities_per_day ?
            score.calendar_opportunities_per_day : receiver_reference_opportunities_per_day},
        {"operational_log_e", score.operational_log_e}, {"selected_prior_mass", score.selected_prior_mass},
        {"operational_passes_conditional_bound", score.operational_passes_conditional_bound},
        {"affine_continuation_attempted", score.affine_continuation_attempted},
        {"affine_continuation_admitted", score.affine_continuation_admitted},
        {"protected_discovery_attempted", score.protected_discovery_attempted},
        {"protected_discovery_admitted", score.protected_discovery_admitted},
        {"protected_discovery_candidates", score.protected_discovery_candidates},
        {"protected_discovery_coarse_seconds", score.protected_discovery_coarse_seconds},
        {"protected_carrier_beat_corrected_polarity", score.protected_carrier_beat_corrected_polarity},
        {"protected_carrier_beat_search_coordinates", score.protected_carrier_beat_search_coordinates},
        {"protected_carrier_beat", std::move(carrier_beat)},
        {"bit_evidence", scores(score.bit_evidence)}, {"adaptive_diagnostics", score.adaptive_diagnostics},
        {"adaptive_bit_evidence", score.adaptive_diagnostics ? scores(score.adaptive_bit_evidence) : Json{}},
        {"combined_bit_evidence", score.adaptive_diagnostics ? scores(score.combined_bit_evidence) : Json{}},
        {"quiet_energy", score.quiet_energy},
        {"quiet_threshold", score.quiet_threshold}, {"quiet_modes", score.quiet_modes},
        {"fine_candidates", score.fine_candidates}, {"adaptive_peak_states", score.adaptive_peak_states},
        {"adaptive_transition_edges", score.adaptive_transition_edges}, {"basis_rows", score.basis_rows},
        {"varying_candidates", score.varying_candidates}, {"varying_blocks", score.varying_blocks},
        {"varying_block_rows", score.varying_block_rows}, {"varying_basis_rows", score.varying_basis_rows},
        {"independent_gain_emission_cells", score.independent_gain_emission_cells},
        {"nominal_independent_gain_frequency_states", score.nominal_independent_gain_frequency_states},
        {"nominal_independent_gain_frequency_status", score.nominal_independent_gain_frequency_status},
        {"varying_coarse_seconds", score.varying_coarse_seconds},
        {"varying_fine_seconds", score.varying_fine_seconds},
        {"varying_projection_seconds", score.varying_projection_seconds},
        {"varying_statistics_seconds", score.varying_statistics_seconds},
        {"varying_recursion_seconds", score.varying_recursion_seconds},
        {"varying_wait_seconds", score.varying_wait_seconds},
        {"varying_workspace_bytes", score.varying_workspace_bytes},
        {"varying_batch_blocks", score.varying_batch_blocks}, {"varying_batches", score.varying_batches},
        {"varying_bit_evidence", scores(score.varying_bit_evidence)}, {"varying_status", score.varying_status},
        {"varying_frequency_states", score.varying_frequency_states},
        {"varying_timing_blocks", score.varying_timing_blocks},
        {"varying_timing_frequencies", score.varying_timing_frequencies},
        {"varying_timing_status", score.varying_timing_status},
        {"varying_fine_route", score.varying_fine_route}, {"varying_fine_arithmetic", score.varying_fine_arithmetic},
        // The components mixed into varying_bit_evidence, so that a device
        // route of the varying statistics can be compared component by component.
        {"varying_components", object({
            {"fixed_bit_evidence", scores(score.fixed_varying_bit_evidence)},
            {"tracked_frequency_log_prior_adjustment", score.tracked_frequency_log_prior_adjustment},
            {"nominal_independent_gain_bit_evidence", scores(score.nominal_independent_gain_bit_evidence)},
            {"nominal_independent_gain_power_bit_evidence", [&] {
                Json::Array powers;
                for (const auto& power : score.nominal_independent_gain_power_bit_evidence)
                    powers.push_back(scores(power));
                return Json(std::move(powers));
            }()},
            {"nominal_independent_fixed_centroid_bit_evidence",
             scores(score.nominal_independent_fixed_centroid_bit_evidence)},
            {"nominal_independent_moving_centroid_bit_evidence",
             scores(score.nominal_independent_moving_centroid_bit_evidence)},
            {"nominal_independent_gain_log_prior_adjustment", score.nominal_independent_gain_log_prior_adjustment}})},
        {"varying_timing_proposals", [&] {
            Json::Array proposals;
            for (const auto& p : std::span(score.varying_timing_proposals).first(score.varying_timing_proposal_count))
                proposals.push_back(object({{"epoch_offset_seconds", p.epoch_offset_seconds},
                    {"arrival_seconds", p.arrival_seconds}, {"frequency_hz", p.frequency_hz},
                    {"clock_ppm", p.clock_ppm}}));
            return Json(std::move(proposals));
        }()},
        {"midpoint_timing", object({{"status", score.midpoint_timing_status},
            {"precision", score.midpoint_timing_precision}, {"public_lags", score.midpoint_timing_lags},
            {"candidates", score.midpoint_timing_candidates}, {"workspace_bytes", score.midpoint_timing_workspace_bytes},
            {"basis_rows", score.midpoint_timing_basis_rows}, {"widened_inverses", score.midpoint_timing_widened_inverses},
            {"scored_lags", score.midpoint_timing_scored_lags}})},
        {"signed_lag_gain", object({
            {"proposal_status", score.signed_lag_gain_proposal_status}, {"maps", score.signed_lag_gain_proposal_maps},
            {"fixed_maps", score.signed_lag_gain_proposal_fixed_maps}, {"lags", std::move(gain_lags)},
            {"map_bit_evidence", std::move(gain_map_evidence)},
            {"available_blocks", score.signed_lag_gain_proposal_available_blocks},
            {"sampled_blocks", score.signed_lag_gain_proposal_sampled_blocks},
            {"scored_blocks", score.signed_lag_gain_proposal_scored_blocks},
            {"direct_scored_blocks", score.signed_lag_gain_proposal_direct_scored_blocks},
            {"projected_rows", score.signed_lag_gain_proposal_projected_rows},
            {"fft_rows", score.signed_lag_gain_proposal_fft_rows},
            {"frequency_points", score.signed_lag_gain_proposal_frequency_points},
            {"fixed_frequency_points", score.signed_lag_gain_proposal_fixed_frequency_points},
            {"frequency_status", score.signed_lag_gain_proposal_frequency_status},
            {"fft_row_stages", score.signed_lag_gain_proposal_fft_stages},
            {"score_rows", score.signed_lag_gain_proposal_score_rows},
            {"fft_stage_bound", score.signed_lag_gain_proposal_fft_stage_bound},
            {"score_row_bound", score.signed_lag_gain_proposal_score_row_bound},
            {"workspace_bytes", score.signed_lag_gain_proposal_workspace_bytes},
            {"proposal_seconds", score.signed_lag_gain_proposal_seconds},
            {"proposal_route", score.signed_lag_gain_proposal_route},
            {"device_fft_row_stages", score.signed_lag_gain_proposal_device_fft_stages},
            {"rescore_fft_row_stages", score.signed_lag_gain_proposal_rescore_fft_stages},
            {"rescored_lags", score.signed_lag_gain_proposal_rescored_lags},
            {"top2_gap", score.signed_lag_gain_proposal_top2_gap},
            {"candidates", score.signed_lag_gain_candidates}, {"basis_rows", score.signed_lag_gain_basis_rows},
            {"bit_evidence", score.signed_lag_gain_candidates ? scores(score.signed_lag_gain_bit_evidence) : Json{}}})},
        {"tracked_frequency_bit_evidence", scores(score.tracked_frequency_bit_evidence)},
        {"operational_bit_evidence", scores(score.operational_bit_evidence)},
        {"operational_bit_log_separation", score.operational_bit_log_separation},
        {"operational_minimum_bit_log_separation", score.operational_minimum_bit_log_separation},
        {"wrong_key_admission", wrong_key_admission_name(score.wrong_key_admission)},
        {"wrong_key_required_log_separation", score.wrong_key_required_log_separation},
        {"wrong_key_rejected", score.wrong_key_rejected},
        {"varying_quiet_modes", score.varying_quiet_modes},
        {"varying_quiet_energy", score.varying_quiet_energy},
        {"varying_quiet_threshold", score.varying_quiet_threshold},
        {"varying_quiet_status", score.varying_quiet_status},
        {"varying_quiet_seconds", score.varying_quiet_seconds},
        {"varying_line_frequency_hz", score.varying_line_frequency_hz},
        {"varying_line_log_evidence", score.varying_line_log_evidence},
        {"coherent_gain_candidates", score.coherent_gain_candidates},
        {"coherent_gain_blocks", score.coherent_gain_blocks},
        {"coherent_gain_frequency_states", score.coherent_gain_frequency_states},
        {"coherent_gain_bit_evidence", scores(score.coherent_gain_bit_evidence)},
        {"coherent_gain_seconds", score.coherent_gain_seconds},
        {"coherent_gain_status", score.coherent_gain_status},
        {"slow_gain_coherence_seconds", scores(score.slow_gain_coherence_seconds)},
        {"affine_gain_model", score.affine_stationary_fading_gain ? "stationary_plus_ou" :
            (score.slow_gain_coherence_seconds[0] > 0 ? "slow_ou" : "original")},
        {"drift_timing_atoms", score.drift_timing_atoms}, {"drift_slopes", score.drift_slopes},
        {"drift_relative_doppler_models", score.drift_relative_doppler_models},
        {"drift_observation_rows", score.drift_observation_rows},
        {"drift_proposal_stride", score.drift_proposal_stride},
        {"drift_frequency_bins", score.drift_frequency_bins}, {"drift_seconds", score.drift_seconds},
        {"drift_render_seconds", score.drift_render_seconds}, {"drift_ridge_seconds", score.drift_ridge_seconds},
        {"drift_ridge_search_seconds", score.drift_ridge_search_seconds},
        {"drift_slope_seconds", score.drift_slope_seconds}, {"drift_exact_seconds", score.drift_exact_seconds},
        {"drift_bit_evidence", scores(score.drift_bit_evidence)}, {"drift_status", score.drift_status},
        {"drift_line_status", score.drift_line_status},
        {"drift_stage_workers", score.drift_stage_workers}, {"drift_line_stage_workers", score.drift_line_stage_workers},
        {"drift_planned_stage_workers", score.drift_planned_stage_workers},
        {"drift_line_planned_stage_workers", score.drift_line_planned_stage_workers},
        {"drift_coherent_timing_fft_rows", score.drift_coherent_timing_fft_rows},
        {"drift_coherent_timing_fft_stages", score.drift_coherent_timing_fft_stages},
        {"drift_proposal_route", score.drift_proposal_route}, {"drift_device_atoms", score.drift_device_atoms},
        {"drift_retained_digest", digest_hex(score.drift_retained_digest)},
        {"drift_line_proposal_route", score.drift_line_proposal_route},
        {"drift_line_device_atoms", score.drift_line_device_atoms},
        {"drift_line_retained_digest", digest_hex(score.drift_line_retained_digest)},
        {"coarse_workspaces", score.coarse_workspaces}, {"coarse_frequency_points", score.coarse_frequency_points},
        {"coarse_arrival_points", score.coarse_arrival_points},
        {"coarse_projected_rate_hz", score.coarse_projected_rate_hz},
        {"coarse_fft_precision", score.coarse_fft_precision}, {"coarse_precision_reason", score.coarse_precision_reason},
        {"affine_blocks", score.affine_blocks},
        {"adaptive_blocks", score.adaptive_blocks}, {"helper_status", score.helper_status},
        {"helper_seconds", score.helper_seconds}, {"coarse_seconds", score.coarse_seconds},
        {"helper_overlapped", score.helper_overlapped},
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
        {"fft_precision", p.bulk && !p.bulk->precision.empty() ? p.bulk->precision : g.fft_precision},
        {"precision_reason", g.precision_reason},
        {"minimum_eigenvalue_ratio", g.minimum_eigenvalue_ratio},
        {"observation_scale_exponent", g.observation_scale_exponent},
        {"scaled_template_workspaces", g.scaled_template_workspaces},
        {"workers", g.workers}, {"first_sample", g.first_sample}, {"stop_sample", g.stop_sample},
        {"public_slot", g.public_slot}, {"pattern_epoch", g.pattern_epoch}, {"symbol_ordinal", g.symbol_ordinal},
        {"rays", g.rays}, {"clocks", g.clocks}, {"workspaces", g.workspaces}, {"frequency_points", g.frequency_points},
        {"frequency_groups", g.frequency_groups}, {"arrival_points", g.arrival_points},
        {"source_rows", g.source_rows}, {"raw_template_rows", g.raw_template_rows},
        {"projected_template_rows", g.projected_template_rows}, {"projected_observation_rows", g.projected_observation_rows},
        {"exact_render_and_projected_rows", g.exact_render_and_projected_rows},
        {"exact_raw_column_rows", g.exact_raw_column_rows},
        {"exact_preparation_fft_row_stages", g.exact_preparation_fft_row_stages},
        {"template_sampling_step", g.template_sampling_step}, {"fir_taps", g.fir_taps}, {"fir_fft_rows", g.fir_fft_rows},
        {"correlation_fft_rows", g.correlation_fft_rows}, {"maximum_frequency_step_hz", g.maximum_frequency_step_hz},
        {"projected_sample_rate_hz", g.projected_sample_rate_hz}, {"clock_gap_ppm", g.clock_gap_ppm},
        {"arrival_lower_seconds", g.arrival_lower_seconds}, {"arrival_upper_seconds", g.arrival_upper_seconds},
        {"workspace_reserved_bytes", g.workspace_reserved_bytes}, {"retained_bytes", p.retained_bytes},
        {"proposals", p.proposals}, {"refined_coordinates", p.refined_coordinates},
        {"bulk_frequency_admission", g.bulk_frequency_admission ? object({
            {"tile_width", g.bulk_frequency_admission->tile_width},
            {"workspace_batch", g.bulk_frequency_admission->workspace_batch},
            {"device_workspace_bytes", g.bulk_frequency_admission->device_workspace_bytes},
            {"host_workspace_bytes", g.bulk_frequency_admission->host_workspace_bytes},
            {"native_fallback_reason", g.bulk_frequency_admission->native_fallback_reason}}) : Json{}},
        {"native_fallback_reason", p.native_fallback_reason},
        {"filter_setup", coarse_counts(p.filter_setup)}, {"mapped_fidelity", coarse_counts(p.mapped_fidelity)},
        {"template_bank", coarse_counts(p.template_bank)}, {"observation", coarse_counts(p.observation)},
        {"lag_setup", coarse_counts(p.lag_setup)}, {"bulk_prepare", coarse_counts(p.bulk_prepare)},
        {"search", coarse_counts(p.search)}, {"selection", coarse_counts(p.selection)},
        {"signed_gain_proposal", coarse_counts(p.signed_gain_proposal)},
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
        {"gaussian_null_skips", lane.gaussian_null_skips},
        {"projected_template_energy", static_cast<double>(lane.projected_template_energy)},
        {"retained_prior_mass", lane.retained_prior_mass}, {"bit_scores", scores(lane.bit_scores)}});
}

Json fine_json(const FineProgress& p) {
    const auto& cache = p.cache;
    const auto& t = p.times;
    const bool adaptive = p.observation.lanes == FineLanes::fixed_and_adaptive;
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
        {"finished", p.finished}, {"initial_affine", fine_lane(p.initial_affine)},
        {"adaptive_diagnostics", adaptive}, {"adaptive", adaptive ? fine_lane(p.adaptive) : Json{}},
        {"slow_gain_coherence_seconds", scores(p.slow_gain_coherence_seconds)},
        {"cache", object({{"calls", cache.calls}, {"interpolated_rows", cache.interpolated_rows}, {"direct_rows", cache.direct_rows},
            {"prepared_calls", cache.prepared_calls}, {"prepared_mapped_columns", cache.prepared_mapped_columns},
            {"prepared_direct_columns", cache.prepared_direct_columns}, {"prepared_empty_columns", cache.prepared_empty_columns},
            {"cache_hits", cache.cache_hits}, {"cache_misses", cache.cache_misses}, {"evictions", cache.evictions},
            {"native_preparation_calls", cache.native_preparation_calls}, {"native_preparation_samples", cache.native_preparation_samples},
            {"native_preparation_seconds", cache.native_preparation_seconds},
            {"addressed_calls", cache.addressed_calls}, {"addressed_columns", cache.addressed_columns},
            {"keyed_lanes", cache.keyed_lanes},
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
            {"worker_gaussian_seconds", t.worker_gaussian_seconds},
            {"worker_observation_seconds", t.worker_observation_seconds}})}});
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
    const bool full_data_prior = helper_transfer_requires_full_prior(config, slot);
    const auto helper_bound = full_data_prior ? 0 : helper_workspace(config, slot);
    if (helper_bound > memory) throw Incomplete("Native Helper preflight exceeds the local memory lease");
    // These phases keep the sequential order, each on the same lanes: their
    // walls add. A slot with a wider lease overlaps the first two instead.
    ReceiverExecutor executor(resources.workers);
    HelperResult helper;
    Timing helper_elapsed{0, 0.};
    if (full_data_prior) {
        check_stop(stop);
        helper.reason = "common_motion_requires_full_data_frequency_prior";
    } else {
        const Timer helper_time;
        helper = score_helper(config, slot, capture.iq, capture.valid, helper_bound, stop,
                              lane_wave(executor, slot.index, resources.workers, stop));
        helper_elapsed = helper_time.elapsed();
    }
    phase(report, "helper", helper_elapsed, seconds, object({
        {"reason", helper.reason}, {"narrowed", helper.narrowed}, {"prefix_samples", helper.prefix_samples},
        {"onset_apertures", helper.onset_apertures}, {"workspace_peak_bytes", helper.workspace_peak_bytes},
        {"retained_intervals", helper.intervals.size()}, {"observation_seconds", helper.frequency_observation_seconds}}));
    if (!all_valid) throw Incomplete("Native full coarse search cannot score an erased observation");
    FrequencyInterval fallback{};
    const auto intervals = public_frequency_intervals(config, helper, fallback);
    const Timer preparation_time;
    CoarseSession coarse(config, slot, profile.key, capture.iq, intervals, resources.workers, memory, stop,
                         &executor);
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
    const auto blocks = fine_block_geometry(profile.receiver, slot);
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
        const auto block_boundary = fine_block_geometry(config, slot).block_rows;
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
    std::uint64_t affine_positive = 0;
    bool complete_checkpoints = true;
    for (unsigned i = 0; i < (continued ? checkpoints.size() : 1); ++i) {
        const auto timer = i == 0 ? prefix_time : Timer{};
        const auto endpoint = checkpoints[i];
        fine->advance_to(endpoint);
        const auto elapsed = timer.elapsed();
        const auto progress = fine->progress();
        // A backend that renders the dense tiles fills them on the device.
        const auto fills = [](const FineProgress& work) {
            return work.cache.native_preparation_calls + (work.bulk ? work.bulk->opencl_resident_rendered_tiles : 0);
        };
        if (i == 0) prefix_fills = fills(progress);
        auto work = fine_json(progress);
        work["checkpoint_seconds"] = static_cast<double>(endpoint) / fs;
        work["stratum"] = o.stratum;
        work["input_sha256"] = digest;
        phase(report, continued ? (i == 0 ? "fine_prefix" : "fine_continuation") : "fine_work",
              elapsed, static_cast<double>(endpoint - previous) / fs, std::move(work));
        previous = endpoint;
        complete_checkpoints &= progress.processed_rows == endpoint && progress.representative_work &&
            progress.initial_affine.positive_basis_blocks > affine_positive;
        affine_positive = progress.initial_affine.positive_basis_blocks;
    }
    const Timer finish_time;
    ReceiverSlotScore score;
    fine->finish(score);
    classify_fine_slot(score, prepared.grid);
    apply_wrong_key_admission(score, profile.receiver.wrong_key_admission);
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
    const auto qualifying_cache_progress = final_progress.cache.native_preparation_calls +
        (final_progress.bulk ? final_progress.bulk->opencl_resident_rendered_tiles : 0) > prefix_fills;
    const bool cold_covered = final_progress.initial_affine.initial_states == 256 &&
        final_progress.initial_affine.peak_emission_states == 256 &&
        final_progress.segments == 2;
    const bool erased = final_progress.requested_invalid_rows == 2 && final_progress.erased_blocks >= 2;
    const bool work_complete = final_progress.observation_complete &&
        (final_progress.representative_work || (o.stratum == "terminal" && final_progress.blocks > 0)) &&
        (o.stratum != "active" || !continued || (complete_checkpoints && qualifying_cache_progress)) &&
        (o.stratum != "cold-maximum" || cold_covered) && (o.stratum != "erasure" || erased);
    auto result = object({{"score", score_json(score)}, {"stratum", o.stratum},
        {"work_complete", work_complete}, {"checkpoints_complete", complete_checkpoints},
        {"continued_cache_fill", qualifying_cache_progress}, {"maximum_fixed_affine_states_covered", cold_covered},
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
    if (capture.cache && capture.cache->reused) {
        // No simulator ran: the rows came from the cache file and matched
        // its SHA-256 before any receiver work.
        phase(report, "capture_reuse", capture.generation, capture.generated_samples / profile.receiver.sample_rate_hz,
            object({{"reused", true}, {"generated_rows", capture.generated_samples}, {"retained_rows", capture.rows()},
                    {"retained_ram_rows", capture.iq.size()}, {"spool_bytes", capture.spool ? capture.spool->bytes() : 0},
                    {"file_bytes", capture.cache->file_bytes}, {"key_sha256", capture.cache->key},
                    {"verified_input_sha256", capture.digest},
                    {"generation_preflight_reserved_bytes", capture.generation_reserved_bytes},
                    {"stored_channel_workspace_bytes", capture.channel.working_bytes},
                    {"has_selective_filter", capture.selective_filter}}), 0, "simulator_excluded");
        return;
    }
    phase(report, "simulator", capture.generation, capture.generated_samples / profile.receiver.sample_rate_hz,
        object({{"generated_rows", capture.generated_samples}, {"retained_rows", capture.rows()},
                {"retained_ram_rows", capture.iq.size()}, {"spool_bytes", capture.spool ? capture.spool->bytes() : 0},
                {"generation_preflight_reserved_bytes", capture.generation_reserved_bytes},
                {"generation_helper_workers", capture.generation_helpers},
                {"generation_stack_bytes_per_helper", capture.generation_stack_bytes},
                {"generation_read_rows", capture.generation_read_rows},
                {"channel_workspace_bytes", capture.channel.working_bytes},
                {"has_selective_filter", capture.selective_filter}}), 0, "simulator_excluded");
    if (capture.cache && capture.cache->stored)
        phase(report, "capture_cache_write", capture.cache->write, capture.rows() / profile.receiver.sample_rate_hz,
            object({{"rows", capture.rows()}, {"file_bytes", capture.cache->file_bytes},
                    {"key_sha256", capture.cache->key}}), 0, "subset_of_excluded_simulator_wall");
    if (capture.spool) phase(report, "capture_spool_write", capture.spool_write,
        capture.rows() / profile.receiver.sample_rate_hz,
        object({{"rows", capture.rows()}, {"spool_bytes", capture.spool->bytes()},
                {"spool_working_bytes", capture.spool->working_bytes()}, {"backing_kind", capture.spool->backing_kind()},
                {"spool_residency_reserved_bytes", capture.spool->resident_reservation_bytes()}}), 0,
        "subset_of_excluded_simulator_wall");
}

std::pair<ReceiverSlotScore, Timing> complete_probe_slot(const Options& o, const Profile& profile,
        const ReceiverSlotPlan& slot, const Capture& capture, const StreamReceiverResources& resources,
        std::stop_token stop) {
    const auto rows = slot.stop_sample - slot.first_sample;
    require(slot.first_sample >= capture.first && slot.stop_sample - capture.first <= capture.iq.size() &&
            capture.valid.size() == capture.iq.size(), "Probe capture does not contain the full native slot");
    const auto first = static_cast<std::size_t>(slot.first_sample - capture.first);
    const auto iq = std::span(capture.iq).subspan(first, rows);
    const auto valid = std::span(capture.valid).subspan(first, rows);
    // The public workspace includes the IQ lease already held by the probe.
    // The remaining process allowance covers DSP and stacks. Spare memory
    // grants the lease that lets Helper run beside coarse preparation.
    const auto workspace = slot_workspace_for_options(o)(profile.receiver, slot, resources.workers);
    const auto lease_bytes = iq.size() * (sizeof(Complex) + 1);
    const auto scratch = standalone_scratch(resources);
    if (workspace < lease_bytes || workspace - lease_bytes > scratch)
        throw Incomplete("Native full-slot probe exceeds its local memory lease");
    if (o.scheduled_gap_fill) {
        const Timer began;
        auto score = score_protected_data_slot(profile.receiver, slot, profile.key, iq, valid,
            resources.workers, workspace, stop, nullptr, nullptr);
        return {std::move(score), began.elapsed()};
    }
    if (o.protected_packed) {
        const Timer began;
        auto score = score_protected_slot(profile.receiver, slot, profile.key, iq, valid,
            resources.workers, workspace, stop, nullptr, o.protected_discovery, o.carrier_beat_polarity,
            o.white_beat_evidence, o.colored_beat_evidence);
        return {std::move(score), began.elapsed()};
    }
    const auto wider = receiver_slot_overlap_workspace(profile.receiver, slot, resources.workers);
    const auto lease = o.helper_overlap && wider - lease_bytes <= scratch ? wider : workspace;
    const Timer began;
    auto score = o.helper_overlap ?
        score_receiver_slot(profile.receiver, slot, profile.key, iq, valid,
                            resources.workers, lease, stop) :
        score_slot_in_sequence(profile.receiver, slot, profile.key, iq, valid,
                               resources.workers, lease, stop, nullptr);
    return {std::move(score), began.elapsed()};
}

std::array<std::uint8_t, 64> admission_digest(std::uint64_t seed, std::uint8_t purpose) {
    std::array<std::uint8_t, 64> result{};
    constexpr std::string_view tag = "datapump.triad.admission.v1";
    std::array<std::uint8_t, tag.size() + 9> input{};
    std::ranges::copy(tag, input.begin());
    input[tag.size()] = purpose;
    for (unsigned i = 0; i < 8; ++i) input[tag.size() + 1 + i] = static_cast<std::uint8_t>(seed >> (8 * i));
    unsigned bytes = 0;
    require(EVP_Digest(input.data(), input.size(), result.data(), &bytes, EVP_sha512(), nullptr) == 1 &&
            bytes == result.size(), "Cannot derive synthetic admission interferer");
    return result;
}

std::uint64_t admission_word(std::uint64_t seed, std::uint8_t purpose) {
    const auto bytes = admission_digest(seed, purpose);
    std::uint64_t word = 0;
    for (unsigned i = 0; i < 8; ++i) word |= std::uint64_t{bytes[i]} << (8 * i);
    return word;
}

void admission_run(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
                   const StreamReceiverResources& resources, std::uint64_t& samples_left, std::stop_token stop) {
    if (slot.stop_sample > samples_left)
        throw Incomplete("Admission capture exceeds the declared probe sample budget");
    const bool wanted = o.stratum == "active";
    const std::array wanted_bit{static_cast<std::uint8_t>(o.bit)};
    const std::array emissions{EmissionOptions{.mode = wanted ? EmissionMode::Message : EmissionMode::Quiet,
        .first_slot = slot.index, .bits = wanted ? std::span<const std::uint8_t>(wanted_bit) :
            std::span<const std::uint8_t>{}, .stop = stop}};
    const auto capture = [&] {
        if (!o.adjacent_data.empty()) {
            const auto& waveform = profile.waveform;
            require(slot.index < waveform.last_slot(), "Adjacent DATA qualification needs a following public slot");
            const std::array bits{static_cast<std::uint8_t>(o.adjacent_data[0] == '1'),
                static_cast<std::uint8_t>(o.bit), static_cast<std::uint8_t>(o.adjacent_data[1] == '1')};
            const auto source_begin = [&](std::uint64_t index) {
                return waveform.calendar().distance(waveform.origin(), waveform.transmit_slot(index).helper_begin);
            };
            if (o.match_scheduled_gap_admission) {
                const auto first = slot.index - 1;
                std::array sources{EmissionOptions{.mode = EmissionMode::Message, .first_slot = first, .bits = bits,
                    .source_first_sample = source_begin(first), .stop = stop}};
                if (o.fills_scheduled_gaps() && o.gap_fill_transmit)
                    sources.front().qualification_gap_fill_key = Crypto::from_independent_roots(
                        admission_digest(o.seed, 6), admission_digest(o.seed, 7));
                return simulate(o, profile, sources, slot.first_sample, slot.stop_sample, samples_left, resources, stop);
            }
            const auto emitter = [&](std::uint64_t index, bool active, std::span<const std::uint8_t> data) {
                return EmissionOptions{.mode = active ? EmissionMode::Message : EmissionMode::Quiet,
                    .first_slot = index, .bits = active ? data : std::span<const std::uint8_t>{},
                    .source_first_sample = source_begin(index), .stop = stop};
            };
            const std::array adjoining{
                emitter(slot.index ? slot.index - 1 : 0, o.adjacent_data[0] != 'q', std::span(bits).first<1>()),
                emitter(slot.index, wanted, std::span(bits).subspan<1, 1>()),
                emitter(slot.index + 1, o.adjacent_data[1] != 'q', std::span(bits).last<1>())};
            const auto sources = std::span(adjoining).subspan(slot.index ? 0 : 1);
            return simulate(o, profile, sources, slot.first_sample, slot.stop_sample, samples_left, resources, stop);
        }
        if (!o.interferer_cn0)
            return simulate(o, profile, emissions, slot.first_sample, slot.stop_sample, samples_left, resources, stop);

        const Timer began;
        const auto serpent = admission_digest(*o.interferer_seed, 0);
        const auto aes = admission_digest(*o.interferer_seed, 1);
        const auto key = Crypto::from_independent_roots(serpent, aes);
        Digest fingerprint;
        fingerprint.add(std::as_bytes(std::span(serpent)));
        fingerprint.add(std::as_bytes(std::span(aes)));
        const auto phase_word = admission_word(*o.interferer_seed, 2);
        const double phase_rad = std::numbers::pi * 2 * static_cast<double>(phase_word >> 11) * 0x1p-53;

        auto foreign = o;
        foreign.active_seconds = o.interferer_seconds;
        foreign.rate_hz = o.interferer_rate.value_or(o.rate_hz);
        const auto frequency_offset = o.interferer_frequency_offset_hz.value_or(0);
        foreign.data_if_hz += frequency_offset;
        foreign.helper = false;
        foreign.helper_w = 0;
        auto settings = settings_for(foreign, resources.memory_limit_bytes);
        settings.transfer.key = key;
        const auto tx = live::detail::prepare_profile(settings, *settings.calendar, stop,
            {.simulation_seed = o.seed, .fixture_active_seconds = o.interferer_seconds});

        const auto coordinate = [](const PublicProfile& w, std::uint64_t index) {
            return w.calendar().distance(w.origin(), w.transmit_slot(index).pattern_begin);
        };
        const auto rate = profile.receiver.sample_rate_hz;
        const auto aligned_origin = static_cast<long double>(coordinate(profile.waveform, slot.index)) -
            coordinate(tx.waveform, 0);
        const auto wanted_support = profile.waveform.slot(slot.index).data_support;
        const auto foreign_support = tx.waveform.slot(0).data_support;
        const bool uniform = o.interferer_uniform_window.value_or(false);
        std::uint64_t first = 0, onset_choices = 0;
        if (uniform) {
            // Uniform on the discrete origins with nonempty intersection of the
            // foreign nominal support and the complete configured scored window.
            require(slot.first_sample >= foreign_support.stop - 1 && slot.stop_sample > foreign_support.first,
                    "Uniform overlap needs a later public slot to include the complete early-origin range");
            const auto low = slot.first_sample - (foreign_support.stop - 1);
            const auto high = slot.stop_sample - foreign_support.first - 1;
            require(high >= low, "Empty admission overlap-onset range");
            onset_choices = high - low + 1;
            std::mt19937_64 random(admission_word(*o.interferer_seed, 3));
            const auto threshold = (std::uint64_t{0} - onset_choices) % onset_choices;
            auto draw = random();
            while (draw < threshold) draw = random();
            first = low + draw % onset_choices;
        } else {
            const auto offset_rows = std::round(static_cast<long double>(o.interferer_offset_seconds.value_or(0)) * rate);
            const auto origin = aligned_origin + offset_rows;
            require(origin >= 0 && origin < slot.stop_sample,
                    "Interferer placement is outside the native source timeline; use a later public slot for early arrivals");
            first = static_cast<std::uint64_t>(origin);
        }
        const double actual_cn0 = 10 * std::log10(rate / profile.receiver.noise_variance);
        const double gain = std::pow(10., (*o.interferer_cn0 - actual_cn0) / 20);
        require(std::isfinite(gain) && gain > 0 && gain <= 1e8, "Admission interferer gain is outside probe bounds");

        require(first <= UINT64_MAX - foreign_support.stop, "Interferer support exceeds native timeline");
        const auto overlap = [&](std::uint64_t begin, std::uint64_t end) {
            const auto overlap_first = std::max(begin, first + foreign_support.first);
            const auto overlap_stop = std::min(end, first + foreign_support.stop);
            return overlap_stop > overlap_first ? overlap_stop - overlap_first : 0;
        };
        const auto window_overlap = overlap(slot.first_sample, slot.stop_sample);
        require(window_overlap > 0, "Interferer has no overlap with the public scored window");
        report["configuration"]["interferer"] = object({{"model", "common_native_channel_separate_key_and_phase"},
            {"key_fingerprint_sha256", fingerprint.finish()}, {"seed", *o.interferer_seed},
            {"bit", *o.interferer_bit}, {"cn0_db_hz", *o.interferer_cn0}, {"relative_amplitude", gain},
            {"pattern_offset_seconds", static_cast<double>((first - aligned_origin) / rate)}, {"initial_phase_rad", phase_rad},
            {"placement", uniform ? "uniform_window_intersection" : "fixed_pattern_offset"},
            {"uniform_origin_count", onset_choices},
            {"inner_rate_hz", foreign.rate_hz}, {"requested_active_seconds", *o.interferer_seconds},
            {"bit_seconds", tx.waveform.config().format.bit_seconds}, {"first_source_sample", first},
            {"nominal_support_first_sample", first + foreign_support.first},
            {"nominal_support_stop_sample", first + foreign_support.stop},
            {"nominal_support_seconds", foreign_support.size() / rate},
            {"nominal_window_overlap_seconds", window_overlap / rate},
            {"nominal_data_overlap_seconds", overlap(wanted_support.first, wanted_support.stop) / rate},
            {"frequency_offset_hz", frequency_offset},
            {"configured_carrier_hz", tx.waveform.config().format.configured_carrier_hz}, {"helper_enabled", false}});
        phase(report, "interferer_preparation", began.elapsed(), 0,
              object({{"profile_ready", true}}), 0, "simulator_excluded");
        const std::array foreign_bit{static_cast<std::uint8_t>(*o.interferer_bit)};
        const SecondaryEmission secondary{tx.waveform, tx.key,
            {.bits = foreign_bit, .stop = stop}, first, std::polar(gain, phase_rad), frequency_offset};
        auto received = simulate(o, profile, emissions, slot.first_sample, slot.stop_sample, samples_left,
                                 resources, stop, {}, &secondary);
        // The slope in nominal RX sample seconds on the direct path. Exclude
        // stochastic phase noise, FHSS and moving echo delay from this center.
        const auto& effects = profile.channel.effects;
        const auto tx_audio = effects.tx.audio_ppm * 1e-6;
        const auto rx_audio = effects.rx.audio_ppm * 1e-6;
        const auto center = (effects.rf_hz * (effects.tx.rf_ppm - effects.rx.rf_ppm) * 1e-6 +
            effects.nominal_conversion_if_hz * (tx_audio - rx_audio) +
            frequency_offset * (1 + tx_audio) + effects.static_doppler_hz) / (1 + rx_audio);
        const auto& wander = received.channel.common_doppler_wander;
        auto& receipt = report["configuration"]["interferer"];
        receipt["direct_center_hz_excluding_phase_noise_fhss_and_echo"] = center;
        receipt["direct_center_low_hz_with_common_wander"] = center + (wander ? wander->minimum_hz : 0) / (1 + rx_audio);
        receipt["direct_center_high_hz_with_common_wander"] = center + (wander ? wander->maximum_hz : 0) / (1 + rx_audio);
        return received;
    }();
    report["observation"] = observation(capture, slot.stop_sample, slot, o);
    if (!o.adjacent_data.empty())
        report["observation"]["adjacent_data"] = object({{"bits", o.adjacent_data}, {"center_has_data", wanted}});
    if (o.scheduled_gap_fill && slot.protected_data_stop_sample > slot.protected_data_first_sample) {
        const auto offset = slot.protected_data_first_sample - capture.first;
        const auto rows = slot.protected_data_stop_sample - slot.protected_data_first_sample;
        CaptureHasher digest;
        digest.add(std::span(capture.iq).subspan(offset, rows), std::span(capture.valid).subspan(offset, rows));
        report["observation"]["scheduled_gap_fill"]["protected_input_sha256"] = digest.finish();
    }
    generation_phase(report, profile, capture);
    const auto [score, elapsed] = complete_probe_slot(o, profile, slot, capture, resources, stop);
    phase(report, "native_slot", elapsed, (slot.stop_sample - slot.first_sample) /
          profile.receiver.sample_rate_hz, score_json(score));
    if (!score.complete || !score.all_valid)
        throw Incomplete("Admission diagnostic did not score the complete valid public slot");

    if (o.carrier_beat) {
        const Timer began;
        const double arrival = std::clamp(0., slot.arrival_lower_seconds, slot.arrival_upper_seconds);
        std::vector<FineCoordinate> coordinates{{0, arrival, 0., 0.}};
        for (const auto& hint : score.affine_tracking) if (hint) {
            const FineCoordinate coordinate{hint->epoch_offset_seconds, hint->arrival_seconds,
                hint->frequency_hz, hint->clock_ppm};
            if (std::ranges::find(coordinates, coordinate) == coordinates.end())
                coordinates.push_back(coordinate);
        }
        const auto first = static_cast<std::size_t>(slot.first_sample - capture.first);
        const auto rows = static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
        const auto iq = std::span(capture.iq).subspan(first, rows);
        const auto valid = std::span(capture.valid).subspan(first, rows);
        const auto scratch = standalone_scratch(resources);
        const auto evaluate = [&](const Crypto& key, const FineCoordinate& coordinate) {
            const auto evidence = score_carrier_beat(profile.receiver, slot, key, iq, valid,
                coordinate, o.continuous_data, scratch, stop);
            const auto pair = [](const std::array<double, 2>& values) {
                return Json::Array{values[0], values[1]};
            };
            Json::Array models;
            for (const auto& model : evidence.profiles) {
                Json::Array correlations;
                for (const auto& bit : model.bit_correlations)
                    correlations.push_back(Json::Array{Json::Array{bit[0].real(), bit[0].imag()},
                                                       Json::Array{bit[1].real(), bit[1].imag()}});
                models.push_back(object({{"projection_fraction", pair(model.bit_projection_fraction)},
                    {"held_out_explained_fraction", Json::Array{pair(model.held_out_explained_fraction[0]),
                                                              pair(model.held_out_explained_fraction[1])}},
                                         {"correlations", std::move(correlations)}}));
            }
            return object({{"status", evidence.status}, {"blocks", evidence.blocks},
                {"block_rows", evidence.block_rows}, {"workspace_bytes", evidence.workspace_bytes},
                {"observed_harmonic_energy", pair(evidence.observed_harmonic_energy)},
                {"direct_and_dispersive_models", std::move(models)}});
        };
        const auto foreign = slot.format.encrypt ? std::optional(Crypto::from_independent_roots(
            admission_digest(o.seed, 4), admission_digest(o.seed, 5))) : std::nullopt;
        Json::Array comparisons;
        for (const auto& coordinate : coordinates) {
            comparisons.push_back(object({{"epoch_offset_seconds", coordinate.epoch_offset_seconds},
                {"arrival_seconds", coordinate.arrival_seconds}, {"frequency_hz", coordinate.frequency_hz},
                {"clock_ppm", coordinate.clock_ppm}, {"configured_key", evaluate(profile.key, coordinate)},
                {"independent_key_at_same_coordinate", foreign ? evaluate(*foreign, coordinate) : Json{}}}));
        }
        phase(report, "carrier_beat", began.elapsed(), rows / profile.receiver.sample_rate_hz,
              object({{"coordinates", std::move(comparisons)}, {"protected_only", o.continuous_data}}),
              0, "qualification_only");
    }

    auto policies = object({});
    for (const auto policy : wrong_key_admission_policies) {
        auto decision = score;
        apply_wrong_key_admission(decision, policy);
        const auto data = decision.decision == SlotDecision::data;
        const bool correct = wanted && data && decision.bit == o.bit;
        policies[wrong_key_admission_name(policy)] = object({{"decision", decision_name(decision.decision)},
            {"bit", decision.bit ? Json(*decision.bit) : Json{}}, {"reason", decision.reason},
            {"required_log_gap", decision.wrong_key_required_log_separation},
            {"policy_rejected", decision.wrong_key_rejected},
            {"endpoint", data ? (wanted ? (correct ? "correct_bit" : "wrong_bit") : "false_data") :
                (wanted ? "erasure" : decision_name(decision.decision))}});
    }
    report["result"] = object({{"one_native_score_for_all_policies", true},
        {"complete_public_slot", true}, {"complete_receiver_service", false},
        {"interferer_present", o.interferer_cn0.has_value()}, {"wanted_present", wanted},
        {"policies", std::move(policies)}});
}

void slot_run(Json& report, const Options& o, const Profile& profile, const ReceiverSlotPlan& slot,
              const StreamReceiverResources& resources, std::uint64_t& samples_left, std::stop_token stop) {
    const std::array bit{static_cast<std::uint8_t>(o.bit)};
    const bool quiet = o.stratum == "noise";
    const std::array emissions{EmissionOptions{.mode = quiet ? EmissionMode::Quiet : EmissionMode::Message,
        .first_slot = slot.index, .bits = quiet ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>(bit),
        .stop = stop, .bit_at = {}}};
    const auto capture = simulate(o, profile, emissions, slot.first_sample, slot.stop_sample, samples_left, resources, stop);
    report["observation"] = observation(capture, slot.stop_sample, slot, o);
    generation_phase(report, profile, capture);
    if (o.command == "regression") {
        const auto represented = capture.iq.size() / profile.receiver.sample_rate_hz;
        for (unsigned unit = 0; unit < o.units; ++unit) {
            check_stop(stop);
            const auto [score, elapsed] = complete_probe_slot(o, profile, slot, capture, resources, stop);
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
    auto capture = simulate(o, profile, emissions, 0, extent.rows, samples_left, resources, stop);
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
    bool physical_complete = false, decoded = false, authenticated = false, matches = false, long_form = false;
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
        stream.affine_continuation = o.affine_continuation;
        run.maximum_pending_messages = stream.maximum_pending_messages;
        const auto codec = message_profile(o, profile);
        const auto protected_score = [discovery = o.protected_discovery,
                                      polarity = o.carrier_beat_polarity,
                                      white_beat = o.white_beat_evidence,
                                      colored_beat = o.colored_beat_evidence](const ReceiverConfig& config,
                const ReceiverSlotPlan& slot, const Crypto& key, std::span<const Complex> iq,
                std::span<const std::uint8_t> valid, unsigned workers, std::size_t workspace,
                std::stop_token stop, ReceiverExecutor* executor) {
            return score_protected_slot(config, slot, key, iq, valid, workers, workspace, stop, executor,
                                        discovery, polarity, white_beat, colored_beat);
        };
        auto receiver = o.protected_packed ?
            datapump::triad::v3::detail::StreamReceiverFixture::make_with_executor(
                profile.waveform, profile.receiver, profile.key, stream, slot_workspace_for_options(o),
                protected_score, message_decoder(codec.fec), message_receiver_options(codec)) :
            CompleteStreamReceiver(profile.waveform, profile.receiver, profile.key, stream, codec);
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
                    decoded.has_value(), decoded && decoded->authenticated,
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
            {"physical_complete", row.physical_complete}, {"decoded_content_present", row.decoded},
            {"authenticated_content", row.authenticated}, {"expected_source_matches", row.matches},
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
        if (o.fills_scheduled_gaps() && o.gap_fill_transmit)
            emissions.back().qualification_gap_fill_key = Crypto::from_independent_roots(
                admission_digest(o.seed, 6), admission_digest(o.seed, 7));
    }
    const auto capture = simulate(o, profile, emissions, 0, last.stop_sample, samples_left, receiver_resources, stop,
                                  std::move(spool));
    const auto first = make_receiver_slot_plan(profile.waveform, profile.receiver, 0);
    report["observation"] = observation(capture, last.stop_sample, first, o);
    report["observation"]["evaluator_bit"] = Json{};
    report["observation"]["expected_episodes"] = episode_count;
    report["observation"]["wire_bits_per_episode"] = wire;
    generation_phase(report, profile, capture);
    frontend(report, profile, capture, stop);
    if (o.scheduled_gap_fill) {
        const Timer began;
        auto hashes = scheduled_fill_window_hashes(profile, capture, slot_count, stop);
        phase(report, "scheduled_fill_window_identity", began.elapsed(), 0,
            object({{"windows", std::move(hashes)}}), 0, "qualification_only_excluded_from_receiver_service");
    }
    auto receiver_profile = profile;
    if (o.foreign_receiver_key)
        receiver_profile.key = Crypto::from_independent_roots(admission_digest(o.seed, 8), admission_digest(o.seed, 9));
    const auto active = replay(o, receiver_profile, capture, last.stop_sample, slot_count, stream_source,
                               receiver_resources, stop);
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
    if (o.foreign_receiver_key) result["receive_key_control"] = "independent_root";
    if (o.fills_scheduled_gaps() && profile.receiver.wrong_key_admission == WrongKeyAdmission::current) {
        Json::Object policy_counts;
        for (const auto policy : wrong_key_admission_policies) {
            std::size_t admitted = 0;
            for (const auto& slot : active.slots) {
                auto score = slot.score;
                apply_wrong_key_admission(score, policy);
                admitted += score.decision == SlotDecision::data;
            }
            policy_counts.emplace(std::string(wrong_key_admission_name(policy)), admitted);
        }
        result["retrospective_data_counts_by_admission_policy"] = std::move(policy_counts);
    }
    bool eof_verified = true;
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
        const auto partial = replay(o, receiver_profile, capture, first.stop_sample, 1, stream_source,
                                    receiver_resources, stop);
        eof_verified = stream_work_complete(partial, profile, 1) &&
            (o.foreign_receiver_key || partial.slots.front().score.decision == SlotDecision::data) && partial.messages.empty();
        phase(report, "eof_control", partial.service, partial.input_rows / profile.receiver.sample_rate_hz,
            replay_json(partial, profile, 1, spool_reservation), 0, "separate_functional_control");
        result["eof_control_verified"] = eof_verified;
    }
    report["result"] = std::move(result);
    if (!work_complete) throw Incomplete("Native stream missed required valid scoring work or public chronology");
    if (!eof_verified || (!backlog && !o.foreign_receiver_key && !physical_correct))
        throw Incomplete("Native stream missed the declared message, quiet or EOF control");
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
    std::cout << "triad_receiver_probe describe|plan|slot|continuation|message|backlog|regression|admission|codec [--flag value ...]\n"
        "  --backend cpu|tensor-cpu|cuda|vulkan|gpu --require-accelerator on|off --tensor-precision automatic|fp64\n"
        "  --native-coarse-precision automatic|fp32|fp64|q15 (Q15 spectral products require native AVX2)\n"
        "  --tensor-coarse-math fft|fp16|int8|fp16-fft (fp16/int8 matrices; fp16-fft is explicit CUDA FFT)\n"
        "  --tensor-fine-projection fft|nvfp4|cufft (explicit CUDA qualification)\n"
        "  --vulkan-precision automatic|fp32|fp16-storage|direct-fp32|direct-fp16-storage|direct-fp16|nvfp4\n"
        "  --opencl-coarse-templates device|native (explicit Vulkan nvfp4 coarse search; device where the\n"
        "    session offers synthesis unless native is stated, and a stated source must be the one executed)\n"
        "  --vulkan-fine-projection automatic|native|nvfp4|resident-nvfp4|resident-fft (explicit Vulkan qualification)\n"
        "  --vulkan-segmented-timing automatic|device|native (explicit Vulkan changing-channel timing search;\n"
        "    device scores every admitted shape there and, with --require-accelerator on, every workspace\n"
        "    of a geometry that has segmented shapes)\n"
        "  --vulkan-gain-rank|--vulkan-drift|--vulkan-varying-fine automatic|device|native (explicit Vulkan fine-stage\n"
        "    routes: signed-lag gain direct rank, drift proposals and varying statistics; automatic runs only the\n"
        "    gain rank, only on the OpenCL-identified SM 12.0 class, and otherwise declines as unqualified-device;\n"
        "    device runs every admitted stage on the family's Vulkan kernels and,\n"
        "    with --require-accelerator on, requires a backend that can serve the route before any work and device\n"
        "    work for every planned stage; the varying fp32-template arithmetic has no kernels and declines as\n"
        "    unimplemented; the identity names the backend's decline of a stated route)\n"
        "  --vulkan-varying-arithmetic automatic|fp64|fp32-template (explicit Vulkan varying statistics arithmetic;\n"
        "    needs --vulkan-varying-fine device)\n"
        "  --opencl-fine-tiles device|native --opencl-fine-window 2..64, even (explicit resident fine projection;\n"
        "    native tiles in two-block batches reproduce the route before device tiles and windows; a stated\n"
        "    source or window must be the one executed)\n"
        "  --opencl-fine-stream 0..64 (blocks whose mappings device tiles submit while the next are staged;\n"
        "    default 4, 0 stages a whole batch first)\n"
        "  --opencl-fine-map fp32|fp64 (explicit resident fine projection: FP32 tile rendering and mapping with\n"
        "    FP64 tiles and exact fixed-point coordinates and phase, the default, or the FP64 kernels as the\n"
        "    control)\n"
        "  --opencl-coarse-rescore fp32|fp64 (explicit Vulkan nvfp4 coarse search: FP32 partial sums of 64 rows\n"
        "    added in FP64, the default, or every product in FP64 as the control; a stated choice must be the\n"
        "    one executed)\n"
        "  --opencl-profile on|off (Vulkan: record OpenCL device command intervals, on by default for explicit\n"
        "    nvfp4 coarse search; on also times the resident fine session's host spans and commands)\n"
        "  --gpu-fp32-minimum on|off (automatic GPU arithmetic floor; GPU backend only)\n"
        "  --opencl-coarse-one-ray on|off (GPU backend only: automatic Vulkan selection scores one-ray sessions\n"
        "    with the dense FP4 search within its measured cost bound; on by default, off keeps their FFT\n"
        "    sessions)\n"
        "  --device-index N --device-memory-limit-bytes N\n"
        "  --case hf|eme and the normalized manual-case options from triad_cases.py\n"
        "  --workers N --slot-owners N --memory-limit-bytes N --max-samples N --deadline-ms N\n"
        "    Deadlines default to 30000 ms. Maximum: 5400000 ms for admission, 3600000 ms otherwise.\n"
        "  --max-spool-bytes N --pilot-workers W1,W2,W3 (CPU backlog replays)\n"
        "  --capture-cache DIR stores each generated capture in DIR, keyed by this executable's SHA-256, on\n"
        "    Linux its runtime libraries', and every option that can change it; a later run with the same key\n"
        "    reads and verifies it instead of simulating, and its observation and capture_reuse phase say so.\n"
        "    Keep DIR private and temporary; the file is charged to the spool or sample budget.\n"
        "  --coarse-tiles N --seed N --slot-index N --stratum active|noise|cold-maximum|terminal|erasure\n"
        "  --work-only on uses a non-admitting local noise crop for nonnominal continuation strata\n"
        "  --helper-overlap off keeps regression in sequence: Helper, then coarse preparation\n"
        "  --continuous-data on|off qualifies packed DATA for plan, message, backlog or admission with\n"
        "    Helper and waits off (default off)\n"
        "  --scheduled-gap-fill on|off qualifies Scheduled + gap fill for plan, message or admission\n"
        "  --scheduled-gap-bridge on|off qualifies Scheduled + short transitions with the ordinary receiver\n"
        "    admission requires an active interior slot between two --adjacent-data bits\n"
        "    --gap-fill-transmit off omits only transmit fill in a matched message or admission probe\n"
        "  --foreign-receiver-key on compares an independent receive key in an encrypted message probe\n"
        "  --affine-continuation on|off compares stream replay with or without receiver state (default on)\n"
        "  admission scores one complete slot for all policies; --stratum noise omits wanted DATA\n"
        "    --adjacent-data PAIR selects adjoining 0, 1 or quiet (q) slots. N=0 requires leading q\n"
        "    --protected-packed on scores only protected samples in packed native CPU qualification\n"
        "    --protected-discovery on adds protected-aperture proposals after unresolved packed scoring\n"
        "    --carrier-beat on compares keyed carrier-beat correlations at fixed native coordinates (CPU)\n"
        "    --carrier-beat-polarity on nominates an extra public affine cell before final DATA admission\n"
        "    --white-beat-evidence on compares a fixed-prior native supplement on raw white simulator IQ\n"
        "    --colored-beat-evidence on compares the stationary Gaussian bound on the selected capture\n"
        "    --project-colored-pcm on scores native analytic projection of contractive MA(1) real PCM\n"
        "    on protected CPU admission, with capture caching disabled\n"
        "  optional encrypted admission interferer: --interferer-cn0 DB_HZ --interferer-seconds SECONDS\n"
        "    --interferer-seed N --interferer-bit 0|1 [--interferer-rate HZ --interferer-frequency-offset-hz HZ]\n"
        "    [--interferer-offset-seconds SECONDS | --interferer-uniform-window on]\n"
        "  --slots N --units 1|2|4 --bit 0|1 --sample-rate HZ --data-if HZ\n"
        "  --minimum-carrier-spacing 0|3.5|125|265 (Hz, default 0)\n"
        "  --doppler-hz HZ (bounded common wander; selected spread adds atmospheric support)\n"
        "  --target-cn0 DB_HZ --cn0 DB_HZ --seconds FIXED_ACTIVE_SECONDS --fec off|rs20|rs60\n"
        "  --signal-gain-db DB shifts actual C/N0 relative to --cn0 with fixed receiver gain and noise\n"
        "  --channel-model gui|itu-disturbed (Gaussian HF reference with cpu, cuda, or gpu --require-accelerator on)\n"
        "    itu-disturbed uses 0/7ms equal-power independent Gaussian taps, width 30Hz (2-sigma);\n"
        "    selected synthetic selective AM is additional to, not part of, the reference\n"
        "  --channel-severity 0..1 scales actual spread, common wander and selective AM depth\n"
        "    CPU admission/message/plan with Helper and interferer off preserves receiver bounds and source geometry.\n"
        "    CUDA/automatic GPU admission/plan also accepts severity 0 alone at HF Worst, margin 3.\n"
        "JSON output uses datapump.triad.native-probe.v1. Exit 2 indicates unavailable or incomplete work.\n";
}

Json identity() {
    return object({{"implementation", "native-production-receiver"},
        {"cpu_fft_kernel", datapump::modem::detail::search_fft::kernel_name()},
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
        }, {"configured_vulkan_backend",
#ifdef DATAPUMP_HAVE_TRIAD_VULKAN
            true
#else
            false
#endif
        }});
}

void run(Json& report, const Options& o, std::stop_token stop) {
    report["identity"]["backend"] = o.backend;
    report["identity"]["receiver_arithmetic"] = o.backend == "vulkan"
        ? "native_vulkan_selected_frequency_search_with_fp64_coordinates_scores_and_state"
        : o.backend == "gpu" ? "automatic_cuda_or_vulkan_bulk_with_fp64_sensitive_stages"
        : o.backend != "cpu" ? "compiled_tensor_bulk_with_native_fp64_coordinates_and_state"
        : o.native_coarse_precision == "fp64" ? "native_fp64_coarse_fft_state_and_admission"
        : o.native_coarse_precision == "q15" ? "native_q15_spectral_products_fp32_fft_fp64_score_and_state"
        : o.native_coarse_precision == "fp32" ? "native_fp32_coarse_products_with_fp64_guards"
        : "native_automatic_q15_or_fp32_coarse_products_with_fp64_guards";
    report["identity"]["require_accelerator"] = o.require_accelerator;
    report["identity"]["requested_gpu_fp32_minimum"] = o.gpu_fp32_minimum;
    // The choice exists only for automatic GPU selection.
    report["identity"]["requested_opencl_coarse_one_ray"] = o.backend == "gpu" && o.opencl_coarse_one_ray;
    report["identity"]["requested_vulkan_precision"] = o.vulkan_precision;
    report["identity"]["requested_vulkan_fine_projection"] = o.vulkan_fine_projection;
    report["identity"]["requested_opencl_fine_tiles"] = o.opencl_fine_tiles;
    report["identity"]["requested_opencl_fine_window"] = o.opencl_fine_window;
    report["identity"]["requested_opencl_fine_stream"] = o.opencl_fine_stream;
    report["identity"]["requested_opencl_fine_map"] = o.opencl_fine_map;
    report["identity"]["requested_opencl_coarse_rescore"] = o.opencl_coarse_rescore;
    report["identity"]["requested_opencl_profile"] =
        o.opencl_profile ? (*o.opencl_profile ? "on" : "off") : "default";
    report["identity"]["requested_tensor_precision"] = o.tensor_precision;
    report["identity"]["requested_tensor_coarse_math"] = o.tensor_coarse_math;
    report["identity"]["requested_tensor_fine_projection"] = o.tensor_fine_projection;
    report["identity"]["requested_cuda_segmented_timing"] = o.cuda_segmented_timing;
    report["identity"]["requested_vulkan_segmented_timing"] = o.vulkan_segmented_timing;
    report["identity"]["requested_vulkan_gain_rank"] = o.vulkan_gain_rank;
    report["identity"]["requested_vulkan_drift"] = o.vulkan_drift;
    report["identity"]["requested_vulkan_varying_fine"] = o.vulkan_varying_fine;
    report["identity"]["requested_vulkan_varying_arithmetic"] = o.vulkan_varying_arithmetic;
    report["identity"]["requested_native_coarse_precision"] = o.native_coarse_precision;
    const auto backend = selected_backend(o);
    // Each stated fine-stage route names the backend's shape-independent
    // decline, empty when the backend can serve it. The probe cannot state
    // explicit FP64 coarse arithmetic with a Vulkan backend
    // (--native-coarse-precision needs --backend cpu), so its queries never
    // do; a receiver configuration can.
    const std::array<std::pair<StageFamily, const std::string*>, 3> stage_routes{{
        {StageFamily::gain_rank, &o.vulkan_gain_rank}, {StageFamily::drift, &o.vulkan_drift},
        {StageFamily::varying, &o.vulkan_varying_fine}}};
    const auto stage_name = [](StageFamily family) {
        return std::string(stage_family_receipt_names[static_cast<std::size_t>(family)]);
    };
    for (const auto& [family, route] : stage_routes)
        if (backend && *route != "automatic")
            report["identity"]["vulkan_" + stage_name(family) + "_route_decline"] =
                std::string(backend->stage_decline(StageShape{family}, false));
    const auto describe_backend = [&] {
        const auto receipt = backend ? backend->receipt() : BulkReceipt{};
        report["identity"]["backend_receipt"] = backend ? bulk_json(receipt) : Json{};
        if (backend && o.backend == "gpu")
            report["identity"]["selected_accelerator_backend"] = receipt.cuda ? "cuda" :
                receipt.vulkan ? "vulkan" : "unknown";
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
        // A requested device route that the backend declines for every shape
        // cannot be served on any geometry: report it before any work.
        if (backend && o.backend == "vulkan" && o.require_accelerator && o.command != "plan")
            for (const auto& [family, route] : stage_routes) {
                if (*route != "device") continue;
                const auto decline = backend->stage_decline(StageShape{family}, false);
                if (decline.empty()) continue;
                report["identity"]["vulkan_" + stage_name(family) + "_requirement"] = "unservable";
                throw Incomplete("Requested Vulkan " + stage_name(family) + " route is unavailable on this backend: " +
                                 std::string(decline));
            }
        auto settings = settings_for(o, resources.memory_limit_bytes);
        const Timer preparation;
        auto profile = live::detail::prepare_profile(settings, *settings.calendar, stop,
            {.simulation_seed = o.seed, .fixture_active_seconds = o.active_seconds});
        if (o.project_colored_pcm) {
            const live::detail::AudioInput input(profile.waveform.config().format.sample_rate,
                profile.receiver.data_if_hz, profile.minimum_audio_hz, profile.maximum_audio_hz);
            profile = live::detail::with_capture_uncertainty(profile, 0, 0, input.halo_samples() + 1);
        }
        if (o.continuous_data) {
            auto config = profile.waveform.config();
            config.continuous_data = true;
            profile.waveform = PublicProfile(std::move(config), settings.transfer.key);
        }
        configure_link_contrast(profile.channel, o);
        configure_channel_model(profile, o);
        if (o.white_beat_evidence || o.colored_beat_evidence || o.project_colored_pcm)
            require(profile.channel.effects.noise_enabled && !profile.channel.effects.slow_agc &&
                    !profile.channel.frontend.active() &&
                    profile.channel.diagnostic_iq_noise.mode == background_noise::Mode::Off &&
                    profile.channel.complex_noise_variance == profile.receiver.noise_variance,
                    "Beat evidence and PCM projection need unfiltered simulator IQ at the configured thermal variance");
        const auto preparation_elapsed = preparation.elapsed();
        profile.receiver.bulk_backend = backend;
        if (o.native_coarse_precision == "q15")
            profile.receiver.native_coarse_precision = NativeCoarsePrecision::q15;
        const auto slot = make_receiver_slot_plan(profile.waveform, profile.receiver, o.slot_index);
        report["configuration"] = profile_json(o, profile, slot, resources);
        const auto retained = slot.stop_sample - slot.first_sample;
        const auto bytes = retained <= UINT64_MAX / ObservationSpool::row_bytes ?
            Json(retained * ObservationSpool::row_bytes) : Json{};
        auto generated = slot.stop_sample;
        if (o.project_colored_pcm) {
            const auto halo = profile.waveform.config().receiver_filter_halo_samples - 1;
            if (halo > UINT64_MAX - generated)
                throw Incomplete("Colored PCM projection halo exceeds the declared probe sample budget");
            generated += halo;
        }
        report["configuration"]["slot_plan"] = object({{"generated_samples", generated},
            {"retained_samples", retained}, {"retained_bytes", bytes}, {"fits_sample_budget", generated <= o.sample_limit},
            {"fits_capture_ram_budget", retained <= resources.memory_limit_bytes / ObservationSpool::row_bytes}});
        if (o.command == "plan" || o.command == "message" || o.command == "backlog")
            report["configuration"]["stream_plan"] = stream_plan(o, profile, resources);
        phase(report, "profile_preparation", preparation_elapsed, 0, object({{"profile_ready", true}}),
              0, "one_time_profile_preparation");
        if (o.command == "plan") {
            auto& configuration = report["configuration"];
            try {
                FrequencyInterval fallback{};
                const auto intervals = public_frequency_intervals(profile.receiver, HelperResult{}, fallback);
                const auto plan = coarse_work_plan(profile.receiver, slot, intervals, resources.workers);
                configuration["coarse_full_prior"] = object({
                    {"route", plan.route == CoarseRoute::lag_groups ? "lag_groups" : "frequency_tiles"},
                    {"projected_sample_rate_hz", plan.projected_sample_rate_hz},
                    {"projected_template_rows", plan.projected_template_rows},
                    {"template_sampling_step", plan.template_sampling_step},
                    {"frequency_points", plan.frequency_points}, {"frequency_groups", plan.frequency_groups},
                    {"arrival_points", plan.arrival_points}, {"workspaces", plan.workspaces},
                    {"rays", plan.rays}, {"correlation_fft_rows", plan.correlation_fft_rows},
                    {"tile_width", plan.tile_width}, {"planned_tiles", plan.total_tiles},
                    {"workspace_reserved_bytes", plan.workspace_reserved_bytes},
                    {"bulk_frequency_admission", plan.bulk_frequency_admission ? object({
                        {"tile_width", plan.bulk_frequency_admission->tile_width},
                        {"workspace_batch", plan.bulk_frequency_admission->workspace_batch},
                        {"device_workspace_bytes", plan.bulk_frequency_admission->device_workspace_bytes},
                        {"host_workspace_bytes", plan.bulk_frequency_admission->host_workspace_bytes},
                        {"native_fallback_reason", plan.bulk_frequency_admission->native_fallback_reason}}) : Json{}}});
            } catch (const Error& error) {
                configuration["coarse_full_prior"] = Json{};
                configuration["coarse_full_prior_reason"] = error.what();
            }
            try {
                const auto midpoint = coarse_midpoint_timing_support(profile.receiver, slot, resources.workers);
                configuration["signed_gain_rank_plan"] = object({
                    {"status", midpoint.available ? midpoint.gain_status : midpoint.status},
                    {"available_blocks", midpoint.gain_available_blocks}, {"sampled_blocks", midpoint.gain_sampled_blocks},
                    {"direct_blocks", midpoint.gain_direct_blocks}, {"fft_rows", midpoint.gain_fft_rows},
                    {"frequency_points", midpoint.gain_frequency_points},
                    {"fixed_frequency_points", midpoint.gain_fixed_frequency_points},
                    {"frequency_status", midpoint.gain_frequency_status},
                    {"workspace_bytes", midpoint.gain_workspace_bytes},
                    {"fft_stage_bound", midpoint.gain_fft_stage_bound}, {"score_row_bound", midpoint.gain_score_row_bound}});
            } catch (const Error& error) {
                configuration["signed_gain_rank_plan"] = object({{"status", error.what()}});
            }
            report["result"] = object({{"planned", true}, {"actual_device_work", false},
                {"duration_is_provisional", true}});
            return;
        }
        std::uint64_t samples_left = o.sample_limit;
        if (o.command == "message" || o.command == "backlog")
            stream_run(report, o, profile, resources, samples_left, stop);
        else if (o.command == "admission") admission_run(report, o, profile, slot, resources, samples_left, stop);
        else if (o.command == "codec") codec_run(report, o, profile, stop);
        else if (o.work_only) diagnostic_fine(report, o, profile, slot, resources, samples_left, stop);
        else slot_run(report, o, profile, slot, resources, samples_left, stop);
        describe_backend();
        const auto actual_backend = backend ? backend->receipt() : BulkReceipt{};
        if (o.vulkan_fine_projection == "nvfp4" && o.command != "codec" && !actual_backend.opencl_nvfp4_fine_modes)
            throw Incomplete("Requested OpenCL NVFP4 fine projection did not execute on this geometry");
        if (o.command != "codec" &&
            ((o.vulkan_fine_projection == "resident-nvfp4" && !actual_backend.opencl_resident_nvfp4_modes) ||
             (o.vulkan_fine_projection == "resident-fft" && !actual_backend.opencl_resident_fft_modes)))
            throw Incomplete("Requested resident OpenCL fine statistics did not execute on this geometry");
        // A stated tile source and window are held to the route executed.
        if (o.command != "codec" && actual_backend.opencl_resident_fine_batches) {
            if (o.opencl_fine_tiles_supplied && o.opencl_fine_tiles == "device" &&
                actual_backend.uploaded_tiles && !actual_backend.opencl_resident_rendered_tiles)
                throw Incomplete("Requested device-rendered fine tiles did not execute: the session uploaded native tiles");
            if (o.opencl_fine_window && actual_backend.opencl_resident_reduced_windows)
                throw Incomplete("Requested OpenCL resident fine window did not execute: its device lease was refused");
            // Every mapped column and rendered tile names its kernels.
            const bool fp32 = o.opencl_fine_map == "fp32";
            const auto& a = actual_backend;
            if ((fp32 ? a.opencl_resident_fp64_mapped_columns || a.opencl_resident_fp64_rendered_tiles ||
                            a.opencl_resident_fp32_mapped_columns != a.mapped_columns ||
                            a.opencl_resident_fp32_rendered_tiles != a.opencl_resident_rendered_tiles
                      : a.opencl_resident_fp32_mapped_columns || a.opencl_resident_fp32_rendered_tiles ||
                            a.opencl_resident_fp64_mapped_columns != a.mapped_columns ||
                            a.opencl_resident_fp64_rendered_tiles != a.opencl_resident_rendered_tiles))
                throw Incomplete("Requested OpenCL resident fine arithmetic differs from the kernels executed");
        }
        if (o.backend == "cuda" && o.command != "codec") {
            const auto& actual = actual_backend;
            if (o.cuda_segmented_timing && o.require_accelerator &&
                (!actual.cuda_segmented_sessions || !actual.cuda_segmented_inverse_ffts ||
                 !actual.cuda_segmented_score_rows))
                throw Incomplete("Requested CUDA segmented timing did not score any changing-channel frequency/lag rows");
            if (!actual.cuda || !actual.executed || (!o.work_only && (!actual.fft_calls || !actual.score_rows)) ||
                (!actual.fft_calls && !actual.dc_reduction_values) || !actual.statistics_blocks ||
                (!actual.mapped_values && o.stratum != "terminal"))
                throw Incomplete("CUDA receiver work did not execute all declared accelerator stages");
            if (!actual.allocator_peak_reserved_bytes ||
                actual.allocator_peak_reserved_bytes > actual.workspace_limit_bytes)
                throw Incomplete("CUDA allocator peak did not satisfy the declared device workspace");
        }
        if (o.backend == "gpu" && actual_backend.cuda && o.command != "codec") {
            const auto& actual = actual_backend;
            const bool coarse_gpu_work = actual.cuda && actual.executed && actual.fft_calls &&
                actual.score_rows;
            const bool terminal_gpu_fine = actual.mapped_values == 0 && o.stratum == "terminal";
            const bool gpu_fine_work = actual.statistics_blocks > 0 &&
                (actual.mapped_values > 0 || terminal_gpu_fine);
            const bool qualified_native_fine = actual.native_fine_sessions > 0 &&
                actual.native_fine_fallback_reason == "small-chronological-blocks";
            if (!coarse_gpu_work || !(gpu_fine_work || qualified_native_fine))
                throw Incomplete("Automatic CUDA receiver did not complete GPU coarse work and a recognized GPU/native fine route");
            if (!actual.allocator_peak_reserved_bytes ||
                actual.allocator_peak_reserved_bytes > actual.workspace_limit_bytes)
                throw Incomplete("Automatic CUDA allocator peak did not satisfy the declared device workspace");
        }
        // Dense rows come from the OpenCL Tensor Core search of the same
        // device: every row of an explicit request, or the sessions that
        // automatic selection admitted.
        const bool dense_requested = o.vulkan_precision == "nvfp4";
        const auto dense_rows = actual_backend.opencl_coarse_score_rows;
        // Every dense coarse fallback names one reason.
        std::uint64_t named_fallbacks = 0;
        for (const auto count : actual_backend.opencl_coarse_fallback_counts) named_fallbacks += count;
        if (named_fallbacks != actual_backend.opencl_coarse_frequency_fallbacks)
            throw Incomplete("OpenCL NVFP4 coarse fallback reasons do not cover every fallback");
        if ((dense_requested || dense_rows) && o.command != "codec" && !o.work_only) {
            // The search must have rescored its retained peaks. Automatic
            // selection renders its templates. A required session also
            // scores host-prepared columns where the receiver offers no
            // synthesis, so only a stated template source is held to.
            const auto& actual = actual_backend;
            const auto synthesized = actual.opencl_coarse_synthesized_columns;
            const auto uploaded = actual.opencl_coarse_uploaded_columns;
            if (!actual.vulkan || !actual.executed || !dense_rows || dense_rows > actual.score_rows ||
                (dense_requested && (dense_rows != actual.score_rows || actual.opencl_coarse_frequency_fallbacks)) ||
                !actual.opencl_coarse_useful_products ||
                actual.opencl_coarse_issued_products < actual.opencl_coarse_useful_products ||
                !actual.opencl_coarse_rescored_peaks || !actual.opencl_coarse_sessions ||
                !(synthesized || uploaded) || (!dense_requested && uploaded) ||
                !actual.vulkan_peak_owner_lease_bytes ||
                actual.vulkan_peak_owner_lease_bytes > actual.workspace_limit_bytes)
                throw Incomplete(dense_requested
                    ? "Requested OpenCL NVFP4 coarse search did not score this geometry on the device"
                    : "Automatic OpenCL NVFP4 coarse search reported incomplete device work");
            if (dense_requested && o.opencl_coarse_templates_supplied &&
                (o.opencl_coarse_templates == "device" ? uploaded != 0 : synthesized != 0))
                throw Incomplete("Requested OpenCL NVFP4 coarse template source differs from the one executed: "
                                 "device templates need the exact frequency-tile route");
            // Every rescored peak names its accumulation.
            const auto partial = actual.opencl_coarse_rescore_fp32_partial_peaks;
            const auto control = actual.opencl_coarse_rescore_fp64_peaks;
            if (partial + control != actual.opencl_coarse_rescored_peaks ||
                (o.opencl_coarse_rescore == "fp32" ? control != 0 : partial != 0))
                throw Incomplete("Requested OpenCL NVFP4 coarse rescoring differs from the accumulation executed");
        }
        if ((o.backend == "vulkan" || (o.backend == "gpu" && actual_backend.vulkan)) && !dense_requested &&
            o.require_accelerator && o.command != "codec") {
            const auto& actual = actual_backend;
            // The FFT sessions account for every row the dense search left.
            const auto fft_rows = actual.score_rows - std::min(dense_rows, actual.score_rows);
            const auto host_scored = actual.vulkan_host_scored_rows == fft_rows &&
                actual.vulkan_host_scored_rows > 0 && actual.vulkan_arrival_peak_rows == 0 &&
                actual.vulkan_arrival_peak_values == 0;
            const auto device_frontier = actual.vulkan_host_scored_rows == 0 &&
                actual.vulkan_arrival_peak_rows > 0;
            const bool automatic = o.backend == "gpu";
            const bool shared_workspace_bounded = !automatic ||
                (actual.vulkan_shared_workspace_limit_bytes > 0 &&
                 actual.workspace_limit_bytes == actual.vulkan_shared_workspace_limit_bytes &&
                 actual.vulkan_peak_owner_bytes <= actual.vulkan_peak_owner_lease_bytes &&
                 actual.vulkan_peak_owner_lease_bytes <= actual.vulkan_peak_leased_bytes &&
                 actual.vulkan_peak_leased_bytes <= actual.vulkan_shared_workspace_limit_bytes &&
                 actual.vulkan_leased_bytes <= actual.vulkan_peak_leased_bytes &&
                 actual.reserved_bytes <= actual.vulkan_leased_bytes &&
                 actual.allocator_peak_reserved_bytes <= actual.vulkan_peak_leased_bytes);
            const bool fft_executed = (actual.fft_calls || actual.vulkan_direct_dot_terms) &&
                actual.vulkan_dispatches && (host_scored || device_frontier) && actual.vulkan_peak_owner_bytes;
            if (!actual.vulkan || !actual.executed || !actual.score_rows || (dense_rows && !automatic) ||
                (fft_rows && !fft_executed) ||
                (actual.vulkan_host_scored_rows != 0 && !host_scored) ||
                actual.vulkan_fp32_score_rows > fft_rows ||
                actual.vulkan_fp16_storage_score_rows != fft_rows - actual.vulkan_fp32_score_rows ||
                actual.vulkan_direct_fp32_score_rows > actual.vulkan_fp32_score_rows ||
                actual.vulkan_direct_fp16_score_rows > actual.vulkan_fp16_storage_score_rows ||
                actual.vulkan_direct_fp16_storage_score_rows >
                    actual.vulkan_fp16_storage_score_rows - actual.vulkan_direct_fp16_score_rows ||
                actual.vulkan_direct_dot_terms < actual.vulkan_direct_fp32_score_rows +
                    actual.vulkan_direct_fp16_score_rows + actual.vulkan_direct_fp16_storage_score_rows ||
                actual.workspace_scope != (automatic ? "backend_shared" : "per_coarse_session") ||
                !shared_workspace_bounded ||
                actual.vulkan_peak_owner_bytes > actual.workspace_limit_bytes ||
                actual.allocator_peak_reserved_bytes < actual.vulkan_peak_owner_bytes)
                throw Incomplete("Vulkan receiver work did not execute bounded full-frequency scoring");
        }
        if (o.backend == "vulkan" && o.vulkan_segmented_timing == "device" && o.require_accelerator &&
            o.command != "codec") {
            // Only a geometry that offers changing-channel segmented shapes
            // to its sessions has this work: not a fixed channel, diffuse
            // Doppler or explicit FP64 coarse arithmetic.
            const auto& actual = actual_backend;
            const bool applicable = actual.vulkan_segmented_planned_shapes != 0;
            report["identity"]["vulkan_segmented_requirement"] = applicable ? "required" : "not_applicable";
            if (applicable && (!actual.vulkan_segmented_sessions || !actual.vulkan_segmented_inverse_ffts ||
                    !actual.vulkan_segmented_score_rows || actual.vulkan_segmented_declined_calls ||
                    actual.vulkan_segmented_native_fallbacks))
                throw Incomplete("Requested Vulkan segmented timing did not score every changing-channel workspace on the device");
        }
        if (o.backend == "vulkan" && o.require_accelerator && o.command != "codec") {
            // A requested fine-stage route must run every stage planned for
            // it on the device. Any recorded call, decline or fallback makes
            // the family applicable; a route the backend cannot serve failed
            // before the work.
            for (const auto& [family, route] : stage_routes) {
                if (*route != "device") continue;
                const auto name = stage_name(family);
                const auto& stage = vulkan_stage_receipt(actual_backend, family);
                const bool applicable = stage.planned_calls || stage.declined_calls || stage.native_fallbacks;
                report["identity"]["vulkan_" + name + "_requirement"] = applicable ? "required" : "not_applicable";
                if (applicable && (!stage.sessions || !stage.score_rows || stage.declined_calls || stage.native_fallbacks))
                    throw Incomplete("Requested Vulkan " + name + " route did not run every planned stage on the device: " +
                        (!stage.fallback_reason.empty() ? stage.fallback_reason :
                         !stage.decline_reason.empty() ? stage.decline_reason : std::string("no device work")));
            }
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
