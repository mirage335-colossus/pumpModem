#pragma once
#include "datapump/fast/channel_assumptions.hpp"
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
struct SnrReferenceBand {
    double lower_hz,upper_hz;
    double bandwidth_hz() const noexcept {return upper_hz-lower_hz;}
};
SnrReferenceBand expected_snr_reference_band(Channel channel);
double default_expected_snr(Channel channel);
std::vector<double> expected_snr_options(Channel channel);
SnrPreset resolve_snr_preset(Channel channel,double expected_snr_db);
SnrPreset resolve_snr_preset(Channel channel,double expected_snr_db,const ChannelAssumptions&);
// An explicit wire edit outside the fixed short SSB geometry selects the
// ordinary capacity frame. Matching edits retain the current frame size.
Profile normalize_ssb_frame_size(Profile profile) noexcept;

struct SymbolRateOption {std::string id,label;};
std::vector<SymbolRateOption> symbol_rate_options(const Profile& profile);
std::string symbol_rate_option_id(const Profile& profile);
// Explicit rates retain QAM, coding rate, depth and passband centre. The
// fixed short SSB frame requires its standard timing. Auto restores the
// channel's default timing for the current waveform.
Profile apply_symbol_rate_option(Profile profile,std::string_view id);
}
