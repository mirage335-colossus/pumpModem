#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace datapump::gui::launch_command {
// Omitted values preserve the destination's current settings. Parsing creates
// a complete, validated patch before the application changes any state.
struct Patch {
    std::optional<double> tx_dbm,path_loss_db,noise_dbm_hz,target_db_hz;
    std::optional<double> helper_power_w,helper_ratio_limit_db;
    std::optional<std::string> helper_callsign;
    std::optional<double> rate_hz,carrier_hz;
    std::optional<std::uint32_t> audio_sample_rate_hz; // 0 selects automatic waveform-based sizing.
    // Carrier is the absolute physical frequency; rf_shift_hz stores the
    // translation Shift. Doppler adjusts physical Carrier before subtracting
    // Shift; the configured Carrier and Shift are saved unchanged.
    std::optional<double> rf_shift_hz,search_margin;
    std::optional<std::string> oscillator,rf_oscillator,reference,spread_preset,selective_perturbation;
    std::optional<std::string> clock_sync,fhss,dsss_version,dsss_mode,search_arithmetic,receiver_strategy,doppler,wrong_key_admission;
    std::optional<double> receiver_cpu_budget;
    // Optional patches retain disabled/random preferences without enabling a key.
    std::optional<unsigned> symbol_burst,symbol_burst_random;
    std::optional<std::string> data_cadence;
    // Inline list contents or a file reference; parser never opens files.
    std::optional<std::string> fhss_list,fhss_list_file;
    // File references, explicit public bank and named selection only;
    // never serialize key material. Omitted keyset preserves the current bank.
    std::optional<std::string> keyfile,keyset,key_name,tx_key;
    std::optional<double> audio_timing_error_seconds;
    bool audio_timing_default=false; // Retain the Default choice alongside its effective allowance.
    // Zero epoch clears the configured calendar. Ordinal is independent of
    // message boundaries; both peers must use the same initial address.
    std::optional<std::uint64_t> calendar_epoch,calendar_ordinal;
    bool auto_calendar=false;
    std::optional<double> live_noise_dbfs_hz;
    bool auto_live_noise=false;
    bool clear_live_noise=false;
    std::optional<std::string> fec;
    std::optional<bool> full_duplex;
    std::optional<bool> gpu;
    std::optional<unsigned> dsss_factor;
    std::optional<double> minimum_carrier_spacing_hz;
    std::optional<unsigned> workspace_percent; // 0 selects automatic headroom.
    bool operator==(const Patch&) const=default;
};
// Only argument text is read: no process, shell expansion, or file is invoked.
// parse accepts an optional executable name/path and ordinary quoted tokens.
Patch parse(std::string_view command);
// Parses the shared public calendar's whole UTC epoch. Accepts `none`, an
// unsigned Unix-seconds value, or an explicit UTC date in one of the UI forms.
// Zero is the canonical unconfigured-calendar value.
std::uint64_t parse_calendar_epoch(std::string_view value);
std::uint32_t parse_audio_sample_rate(std::string_view value);
// Already split setting arguments, excluding the executable and GUI flags.
Patch parse_arguments(std::span<const std::string> arguments);
// Also validate key flags for callers constructing a Patch directly.
void validate_key_selection(const Patch& settings);
// Formats only populated fields, with a host-appropriate relative GUI command.
// The single Robust target is exported as --target-snr.
std::string format(const Patch& settings);
}
