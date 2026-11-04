#pragma once
#include "datapump/fast/profile.hpp"
#include <string>
#include <vector>

namespace datapump::fast {
// Expected SNR is an assumption over the ORIGINAL channel bandwidth. These
// presets neither measure the link nor negotiate settings with the other end.
struct SnrPreset {
    Profile profile;
    double expected_snr_db;
    double reference_bandwidth_hz;
    std::string note;
};
double default_expected_snr(Channel channel);
std::vector<double> expected_snr_options(Channel channel);
SnrPreset resolve_snr_preset(Channel channel,double expected_snr_db);

struct SymbolRateOption {std::string id,label;};
std::vector<SymbolRateOption> symbol_rate_options(const Profile& profile);
std::string symbol_rate_option_id(const Profile& profile);
// Explicit rates retain the current waveform, QAM, LDPC, depth and passband
// centre. Auto restores only the channel's default timing for that waveform.
Profile apply_symbol_rate_option(Profile profile,std::string_view id);
}
