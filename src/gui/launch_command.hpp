#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace datapump::gui::launch_command {
// Omitted values preserve the destination's current settings. Parsing creates
// a complete, validated patch before the application changes any state.
struct Patch {
    std::optional<double> tx_dbm,path_loss_db,noise_dbm_hz,target_db_hz;
    std::optional<double> short_target_db_hz,long_target_db_hz,rate_hz,carrier_hz;
    std::optional<std::string> oscillator,pattern;
    std::optional<unsigned> workspace_percent;
    bool operator==(const Patch&) const=default;
};
// Only argument text is read: no process, shell expansion, or file is invoked.
// parse accepts an optional executable name/path and ordinary quoted tokens.
Patch parse(std::string_view command);
// Already split setting arguments, excluding the executable and GUI flags.
Patch parse_arguments(std::span<const std::string> arguments);
// Formats only populated fields, with a host-appropriate relative GUI command.
// Common targets precede individual overrides so round trips retain intent.
std::string format(const Patch& settings);
}
