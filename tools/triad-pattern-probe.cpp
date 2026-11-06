#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/triad_clock_follow.hpp"

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
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace datapump::triad::v3;
constexpr std::uint64_t maximum_rows = 500000;
constexpr std::uint64_t maximum_bytes = 64ULL * 1024 * 1024;
constexpr std::uint64_t maximum_coefficients = 4096;
constexpr std::size_t chunk_rows = 4096;

struct Options {
    Format format{};
    std::filesystem::path output;
    double window_seconds = 16;
    std::optional<double> start_seconds;
    std::optional<double> clock_correction_seconds;
    std::optional<double> clock_correction_at_seconds;
    std::optional<double> clock_correction_duration_seconds;
    std::uint64_t seed = 73001;
    bool auto_shaping = true;
    Options() {
        format.sample_rate = 6000;
        format.bit_seconds = 60;
        format.epoch = 1793850000;
    }
};

void require(bool value, const char* reason) {
    if (!value) throw std::invalid_argument(reason);
}

std::uint64_t integer(const std::string& text) {
    require(!text.empty() && text.front() != '-', "expected an unsigned integer");
    std::size_t used = 0;
    const auto value = std::stoull(text, &used);
    require(used == text.size(), "invalid unsigned integer");
    return value;
}

std::uint32_t integer32(const std::string& text) {
    const auto value = integer(text);
    require(value <= UINT32_MAX, "integer exceeds uint32 range");
    return static_cast<std::uint32_t>(value);
}

double number(const std::string& text) {
    std::size_t used = 0;
    const auto value = std::stod(text, &used);
    require(used == text.size() && std::isfinite(value), "expected a finite number");
    return value;
}

bool on_off(const std::string& text) {
    require(text == "on" || text == "off", "expected on or off");
    return text == "on";
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Source-only triad pattern diagnostic; no propagation or receiver.\n"
                "--output NEW_DIRECTORY [--rate Hz --added N --sample-rate Hz]\n"
                "[--sample-rate-denominator N --seconds FULL_DURATION --window-seconds N]\n"
                "[--start-seconds RELATIVE_TO_CALENDAR --seed N --epoch N --symbol-ordinal N]\n"
                "[--pattern-revision legacy-v1|independent-apsk-v2 --added-amplitude-depth D]\n"
                "[--fhss on|off --burst-wait N --interleave on|off --shaping auto|legacy]\n"
                "[--quiet-seconds N --ramp-seconds N --margin-seconds N]\n"
                "[--clock-correction-seconds SIGNED_OFFSET --clock-correction-at-seconds N]\n"
                "[--clock-correction-duration-seconds N] (bounded source-time diagnostic only)\n"
                "At most500000 sampled rows and4096 consecutive unshaped coefficient rows.\n";
            std::exit(0);
        }
        require(i + 1 < argc, "missing option value");
        const std::string value = argv[++i];
        if (arg == "--output") o.output = value;
        else if (arg == "--rate") o.format.inner_rate_hz = number(value);
        else if (arg == "--added") o.format.added_symbols = integer32(value);
        else if (arg == "--sample-rate") o.format.sample_rate = integer32(value);
        else if (arg == "--sample-rate-denominator") o.format.sample_rate_denominator = integer32(value);
        else if (arg == "--seconds") o.format.bit_seconds = number(value);
        else if (arg == "--window-seconds") o.window_seconds = number(value);
        else if (arg == "--start-seconds") o.start_seconds = number(value);
        else if (arg == "--clock-correction-seconds") o.clock_correction_seconds = number(value);
        else if (arg == "--clock-correction-at-seconds") o.clock_correction_at_seconds = number(value);
        else if (arg == "--clock-correction-duration-seconds") o.clock_correction_duration_seconds = number(value);
        else if (arg == "--seed") o.seed = integer(value);
        else if (arg == "--epoch") o.format.epoch = integer(value);
        else if (arg == "--symbol-ordinal") o.format.symbol_ordinal = integer(value);
        else if (arg == "--pattern-revision") {
            if (value == "legacy-v1") o.format.pattern_revision = PatternRevision::LegacyV1;
            else if (value == "independent-apsk-v2") o.format.pattern_revision = PatternRevision::IndependentApskV2;
            else throw std::invalid_argument("unknown pattern revision");
        }
        else if (arg == "--added-amplitude-depth") o.format.added_amplitude_depth = number(value);
        else if (arg == "--fhss") o.format.fhss = on_off(value);
        else if (arg == "--burst-wait") o.format.burst_wait_tiles = integer32(value);
        else if (arg == "--interleave") o.format.added_interleave = on_off(value);
        else if (arg == "--quiet-seconds") o.format.quiet_seconds = number(value);
        else if (arg == "--ramp-seconds") o.format.ramp_seconds = number(value);
        else if (arg == "--margin-seconds") o.format.margin_seconds = number(value);
        else if (arg == "--shaping") {
            require(value == "auto" || value == "legacy", "unknown shaping policy");
            o.auto_shaping = value == "auto";
        }
        else throw std::invalid_argument("unknown option: " + arg);
    }
    require(!o.output.empty(), "--output is required");
    require(o.window_seconds > 0, "window duration must be positive");
    require(o.format.added_amplitude_depth > 0 && o.format.added_amplitude_depth < 1,
            "Added amplitude depth must be strictly between0 and1");
    require(o.clock_correction_seconds || (!o.clock_correction_at_seconds && !o.clock_correction_duration_seconds),
            "clock timing options require --clock-correction-seconds");
    return o;
}

std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

datapump::Crypto fixture_key(std::uint64_t seed) {
    // Only an explicit reproducible test key. Operational keys are not derived
    // from this public seed; the production PatternSource still uses Crypto.
    std::array<std::uint8_t, 32> bytes{};
    for (unsigned i = 0; i < 4; ++i) {
        const auto word = mix(seed + i);
        for (unsigned j = 0; j < 8; ++j) bytes[i*8+j] = static_cast<std::uint8_t>(word >> (8*j));
    }
    return datapump::Crypto(bytes);
}

void write_values(std::ofstream& stream, const Complex* values, std::size_t count) {
    stream.write(reinterpret_cast<const char*>(values), static_cast<std::streamsize>(count * sizeof(Complex)));
    if (!stream) throw std::runtime_error("failed writing probe array");
}

void run(const Options& o) {
    static_assert(sizeof(Complex) == 2 * sizeof(double));
    require(std::endian::native == std::endian::little, "probe binary export requires a little-endian host");
    const auto wall_start = std::chrono::steady_clock::now();
    const auto cpu_start = std::clock();
    Format f = o.format;
    std::optional<ShapingPlan> plan;
    if (o.auto_shaping) {
        plan = plan_shaping(f);
        f = plan->format;
    }
    const auto g = geometry(f);
    const auto fs = effective_sample_rate(f);
    const auto correction_at = static_cast<long double>(o.clock_correction_at_seconds.value_or(0));
    const auto correction = static_cast<long double>(o.clock_correction_seconds.value_or(0));
    SourceClockFollower checked_clock;
    const auto correction_duration = o.clock_correction_duration_seconds ?
        static_cast<long double>(*o.clock_correction_duration_seconds) : checked_clock.minimum_duration(correction);
    if (o.clock_correction_seconds)
        checked_clock.update(correction_at, correction, o.clock_correction_duration_seconds);
    const auto desired_rows = std::ceil(static_cast<long double>(o.window_seconds) * fs);
    require(std::isfinite(desired_rows) && desired_rows >= 1 && desired_rows <= maximum_rows,
            "probe window exceeds500000 sampled-row budget");
    const auto rows = static_cast<std::uint64_t>(desired_rows);
    const auto payload_rows = g.source_samples - 2*g.calendar_origin_sample;
    require(rows <= payload_rows, "window exceeds full active calendar; increase --seconds");
    std::uint64_t first = g.calendar_origin_sample + (payload_rows - rows)/2;
    if (o.start_seconds) {
        const auto requested_first = std::ceil(static_cast<long double>(g.calendar_origin_sample) +
            static_cast<long double>(*o.start_seconds)*fs);
        require(std::isfinite(requested_first) && requested_first >= 0 && requested_first < g.source_samples,
                "window start is outside the complete finite source");
        first = static_cast<std::uint64_t>(requested_first);
    }
    require(first <= g.source_samples && rows <= g.source_samples-first,
            "window end is outside the complete finite source");
    const long double first_time = (static_cast<long double>(first)-g.calendar_origin_sample)/fs;
    const long double end_time = (static_cast<long double>(first+rows)-g.calendar_origin_sample)/fs;
    const long double offset = f.fhss_center_aligned ? 0.L : .5L;
    const auto first_cell = static_cast<std::uint64_t>(std::clamp(
        std::ceil(first_time*g.fine_chip_rate_hz-offset), 0.L, static_cast<long double>(g.fine_cells)));
    const auto end_cell = static_cast<std::uint64_t>(std::clamp(
        std::ceil(end_time*g.fine_chip_rate_hz-offset), 0.L, static_cast<long double>(g.fine_cells)));
    const auto coefficient_rows = std::min(maximum_coefficients, end_cell-first_cell);
    const auto output_bytes = (8*rows + 6*coefficient_rows)*sizeof(Complex) + rows*sizeof(double);
    require(output_bytes <= maximum_bytes, "probe arrays exceed64MiB artifact budget");
    require(!std::filesystem::exists(o.output), "output already exists; use a new directory");
    const auto prepared = std::chrono::steady_clock::now();
    require(std::filesystem::create_directories(o.output), "output directory already exists");
    std::ofstream composite(o.output/"composite.c128.partial", std::ios::binary);
    std::ofstream envelopes(o.output/"carrier_envelopes.c128.partial", std::ios::binary);
    std::ofstream coefficients(o.output/"coefficients.c128.partial", std::ios::binary);
    std::ofstream envelope(o.output/"envelope.f64.partial", std::ios::binary);
    if (!composite || !envelopes || !coefficients || !envelope) throw std::runtime_error("cannot create probe arrays");
    const auto key = fixture_key(o.seed);
    std::vector<Complex> sum(chunk_rows);
    std::vector<double> gate(chunk_rows);
    std::array<std::vector<Complex>, 3> carrier;
    for (auto& column : carrier) column.resize(chunk_rows);
    long double minimum_clock_offset = std::numeric_limits<long double>::infinity();
    long double maximum_clock_offset = -std::numeric_limits<long double>::infinity();
    long double minimum_clock_rate = std::numeric_limits<long double>::infinity();
    long double maximum_clock_rate = -std::numeric_limits<long double>::infinity();
    for (unsigned bit = 0; bit < 2; ++bit) {
        PatternSource source(f, key, static_cast<std::uint8_t>(bit));
        SourceClockFollower clock;
        bool correction_applied = false;
        for (std::uint64_t done = 0; done < rows;) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(chunk_rows, rows-done));
            for (std::size_t j = 0; j < count; ++j) {
                auto sample = static_cast<long double>(first+done+j);
                const auto physical_time = (sample-g.calendar_origin_sample)/fs;
                SourceClockFollower::Sample timing{physical_time, 0, 1, 0};
                if (o.clock_correction_seconds) {
                    if (!correction_applied && physical_time >= correction_at) {
                        clock.update(correction_at, correction, o.clock_correction_duration_seconds);
                        correction_applied = true;
                    }
                    timing = clock.sample(physical_time);
                    // Keep the integer physical origin; do not subtract/re-add
                    // a large epoch or round the sub-sample time correction.
                    sample += timing.offset_seconds*fs;
                }
                if (bit == 0) {
                    minimum_clock_offset = std::min(minimum_clock_offset, timing.offset_seconds);
                    maximum_clock_offset = std::max(maximum_clock_offset, timing.offset_seconds);
                    minimum_clock_rate = std::min(minimum_clock_rate, timing.rate_scale);
                    maximum_clock_rate = std::max(maximum_clock_rate, timing.rate_scale);
                }
                const auto cells = source.carrier_samples_at(sample);
                for (unsigned c = 0; c < 3; ++c) carrier[c][j] = cells[c];
                // Keep the exact public renderer as the composite reference.
                sum[j] = source.sample_at(sample);
                if (bit == 0) gate[j] = shaping_envelope(f, g, (sample-g.calendar_origin_sample)/fs);
            }
            write_values(composite, sum.data(), count);
            if (bit == 0) {
                envelope.write(reinterpret_cast<const char*>(gate.data()), static_cast<std::streamsize>(count*sizeof(double)));
                if (!envelope) throw std::runtime_error("failed writing source envelope");
            }
            for (unsigned c = 0; c < 3; ++c) {
                const auto address = ((bit*3ULL+c)*rows+done)*sizeof(Complex);
                envelopes.seekp(static_cast<std::streamoff>(address));
                write_values(envelopes, carrier[c].data(), count);
            }
            done += count;
        }
        for (std::uint64_t i = 0; i < coefficient_rows; ++i) {
            const auto value = source.pattern_coefficient_at(first_cell+i);
            write_values(coefficients, value.data(), value.size());
        }
    }
    composite.close(); envelopes.close(); coefficients.close(); envelope.close();
    if (!composite || !envelopes || !coefficients || !envelope) throw std::runtime_error("failed finalizing probe arrays");
    std::ofstream manifest(o.output/"manifest.json.partial");
    if (!manifest) throw std::runtime_error("cannot create probe manifest");
    const auto completed = std::chrono::steady_clock::now();
    manifest << std::setprecision(17)
        << "{\"schema\":\"triad-pattern-probe-v1\",\"source_only\":true,\"channel_applied\":false,"
        << "\"normalization\":\"raw PatternSource; no window or bit normalization\","
        << "\"byte_order\":\"little\",\"complex_type\":\"complex128\",\"dtype\":\"<c16\",\"seed\":" << o.seed
        << ",\"key_kind\":\"public test fixture only\",\"epoch\":" << f.epoch
        << ",\"symbol_ordinal\":" << f.symbol_ordinal << ",\"encrypted\":true,\"sample_rate_hz\":" << fs
        << ",\"first_sample\":" << first << ",\"rows\":" << rows
        << ",\"fine_rate_hz\":" << g.fine_chip_rate_hz << ",\"carrier_spacing_hz\":" << g.carrier_spacing_hz
        << ",\"source_samples\":" << g.source_samples << ",\"calendar_origin_sample\":" << g.calendar_origin_sample
        << ",\"tile_seconds\":" << g.tile_seconds
        << ",\"first_calendar_seconds\":" << static_cast<double>(first_time)
        << ",\"window_seconds\":" << static_cast<double>(rows)/fs
        << ",\"composite_file\":\"composite.c128\",\"composite_shape\":[2," << rows << "]"
        << ",\"carrier_file\":\"carrier_envelopes.c128\",\"carrier_shape\":[2,3," << rows << "]"
        << ",\"coefficients_file\":\"coefficients.c128\",\"coefficients_shape\":[2," << coefficient_rows << ",3]"
        << ",\"envelope_file\":\"envelope.f64\",\"envelope_dtype\":\"<f8\",\"envelope_shape\":[" << rows << "]"
        << ",\"envelope_scope\":\"common transmitted shaping envelope; preserves intended ramps, FHSS gates and burst waits\""
        << ",\"coefficient_first\":" << first_cell << ",\"coefficient_count\":" << coefficient_rows
        << ",\"coefficient_scope\":\"consecutive unshaped physical cells; includes cover/guard cells and waits; no envelope gate\""
        << ",\"format\":{\"pattern_revision\":\"" << pattern_revision_name(f.pattern_revision)
        << "\",\"added_amplitude_depth\":" << f.added_amplitude_depth
        << ",\"inner_rate_hz\":" << f.inner_rate_hz << ",\"added_symbols\":" << f.added_symbols
        << ",\"added_interleave\":" << (f.added_interleave ? "true" : "false")
        << ",\"sample_rate\":" << f.sample_rate << ",\"sample_rate_denominator\":" << f.sample_rate_denominator
        << ",\"requested_bit_seconds\":" << f.bit_seconds << ",\"minimum_inner_coefficients\":" << f.minimum_inner_coefficients
        << ",\"fhss\":" << (f.fhss ? "true" : "false")
        << ",\"fhss_center_aligned\":" << (f.fhss_center_aligned ? "true" : "false")
        << ",\"dwell_seconds\":" << f.dwell_seconds << ",\"quiet_seconds\":" << f.quiet_seconds
        << ",\"ramp_seconds\":" << f.ramp_seconds << ",\"margin_seconds\":" << f.margin_seconds
        << ",\"burst_wait_tiles\":" << f.burst_wait_tiles
        << ",\"pulse_shape\":\"" << pulse_shape_name(f.pulse_shape) << "\"}"
        << ",\"geometry\":{\"fine_rate_hz\":" << g.fine_chip_rate_hz
        << ",\"carrier_spacing_hz\":" << g.carrier_spacing_hz
        << ",\"occupied_bandwidth_hz\":" << g.occupied_bandwidth_hz
        << ",\"bit_seconds\":" << g.bit_seconds << ",\"source_samples\":" << g.source_samples
        << ",\"calendar_origin_sample\":" << g.calendar_origin_sample << ",\"fine_cells\":" << g.fine_cells
        << ",\"tile_seconds\":" << g.tile_seconds << ",\"tile_count\":" << g.tile_count
        << ",\"source_workspace_bytes\":" << g.source_workspace_bytes << "}"
        << ",\"shaping_policy\":\"" << (o.auto_shaping ? "auto" : "legacy") << "\""
        << ",\"clock_follow\":{\"enabled\":" << (o.clock_correction_seconds ? "true" : "false")
        << ",\"law\":\"bounded-source-quintic-v1\",\"target_offset_seconds\":" << static_cast<double>(correction)
        << ",\"correction_at_calendar_seconds\":" << static_cast<double>(correction_at)
        << ",\"transition_duration_seconds\":" << static_cast<double>(correction_duration)
        << ",\"maximum_absolute_offset_seconds\":2,\"maximum_rate_correction_ppm\":300"
        << ",\"maximum_acceleration_ppm_per_second\":0.2"
        << ",\"observed_offset_seconds\":[" << static_cast<double>(minimum_clock_offset) << ',' << static_cast<double>(maximum_clock_offset) << ']'
        << ",\"observed_rate_scale\":[" << static_cast<double>(minimum_clock_rate) << ',' << static_cast<double>(maximum_clock_rate) << ']'
        << ",\"scope\":\"source-time diagnostic; no OS clock or live tracking; all DATA source coordinates including ramps/gates are warped; final audio-center NCO is absent and unchanged; coefficients and epoch remain original; no receiver qualification\"}"
        << ",\"shaping_fallback\":" << (plan && plan->fallback ? "true" : "false")
        << ",\"artifact_bytes\":" << output_bytes
        << ",\"preparation_wall_seconds\":" << std::chrono::duration<double>(prepared-wall_start).count()
        << ",\"wall_seconds\":" << std::chrono::duration<double>(completed-wall_start).count()
        << ",\"process_cpu_seconds\":" << static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC
        << ",\"scope\":\"finite source window and actual unshifted carrier envelopes; not propagation, receiver sensitivity, a full-bit energy estimate, or AGC validation\"}\n";
    manifest.close();
    if (!manifest) throw std::runtime_error("failed finalizing probe manifest");
    for (const char* name : {"composite.c128", "carrier_envelopes.c128", "coefficients.c128", "envelope.f64", "manifest.json"})
        std::filesystem::rename(o.output/(std::string(name)+".partial"), o.output/name);
    std::cout << "{\"schema\":\"triad-pattern-probe-receipt-v1\",\"rows\":" << rows
        << ",\"coefficient_rows\":" << coefficient_rows << ",\"artifact_bytes\":" << output_bytes << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    try { run(parse(argc, argv)); return 0; }
    catch (const std::exception& error) {
        std::cerr << "triad-pattern-probe: " << error.what() << '\n'; return 2;
    }
}
