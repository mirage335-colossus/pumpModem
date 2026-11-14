#pragma once

#include "datapump/robust_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

namespace appraisal {

// A bounded selection of the actual native GUI controls. These settings do not
// select a second signal-processing implementation. The returned canonical
// profile owns both public receiver declarations and a separately seeded
// synthetic physical realization.
struct HfGuiProfileOptions {
    std::string spread = "hf-worst", selective = "each-65", arrival = "tight";
    double margin = 3;
    double audio_ppm = 100, rf_ppm = 1;
    double audio_phase_degrees_sqrt_s = .5, rf_phase_degrees_sqrt_s = .05;
    double target_cn0_db_hz = 32, actual_cn0_db_hz = 32;
    bool clock_edge = false, helper = false;
    std::uint64_t seed = 5489;
    std::optional<std::uint64_t> noise_seed;
    // A fixture override is not the GUI's provisional target-C/N0 duration.
    std::optional<double> fixture_bit_seconds;
    std::optional<std::size_t> memory_bytes;
};

inline constexpr datapump::live::Calendar hf_gui_calendar{1793404800, 0};
inline constexpr double hf_gui_rf_shift_hz = 3.4e6;
inline constexpr double hf_gui_inner_rate_hz = 120;
inline constexpr unsigned hf_gui_added_symbols = 1;
inline constexpr double hf_gui_helper_additional_power_w = 1;

struct HfGuiProfileUnavailable : datapump::Error { using Error::Error; };

// Supported spread IDs: none, hf-best, hf-moderate, hf-worst, aurora-best,
// aurora-worst. Selective controls are the canonical off/slow-N/fast-N/each-N
// values (N=1,5,10,25,65). Arrival is either tight (explicit GPS .001/.001 s
// with zero audio allowance), or default (the GUI's broad no-sync policy and
// .07 s audio allowance). An admitted profile is not a reception guarantee.
//
// clock_edge is an explicit experimental realization: TX gets +accuracy/2 and
// RX -accuracy/2, independently for RF and audio. It does not multiply actual
// oscillator errors by Margin, narrow the public search, change the seeded
// timing offset, or reset phase-diffusion streams. The regular seeded draw is
// retained when false. This override is deliberately not called a GUI draw.
datapump::live::detail::Profile prepare_hf_gui_profile(
    const HfGuiProfileOptions&, std::stop_token = {});

} // namespace appraisal
