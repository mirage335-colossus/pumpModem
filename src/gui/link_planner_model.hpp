#pragma once
#include "datapump/transfer.hpp"
#include <optional>
#include <string>
#include <vector>

namespace datapump::gui::planner {
struct Inputs {
    transfer::Options options;
    tuning::PatternMode mode=tuning::PatternMode::auto_pattern;
    modem::ChannelConfig channel;
    double target_db_hz=-8;
    double tx_dbm=3;
    double path_loss_db=170;
    double noise_density_dbm_hz=-164;
    std::size_t wire_bits=1;
    bool empty_draft=false; // One-bit preview; the composer remains empty.
    // Presentation only; the actual byte allowance above drives every check.
    unsigned dsp_workspace_percent=0;
};
struct Point {
    double target_db_hz=0;
    double bit_seconds=0;
    double observer_ratio=0;
    bool observer_available=false;
};
struct Model {
    Inputs inputs;
    std::vector<Point> points;
    bool available=false;
    bool automatic_mode=true;
    bool observer_available=false;
    bool observer_hypothetical=true;
    bool shaped_band=false;
    bool clock_search_supported=false;
    bool receiver_workspace_supported=false;
    double bit_seconds=0;
    double send_seconds=0;
    // Earliest modeled finish: whole burst plus fully scored absent symbols.
    double finish_seconds=0;
    double observer_ratio=0;
    double received_dbm=0;
    double actual_cn0_db_hz=0;
    double margin_db=0;
    double occupied_bandwidth_hz=0;
    double low_audio_hz=0;
    double high_audio_hz=0;
    std::string receiver_status;
    std::string clock_limit_reason;
    std::string error;
    std::optional<double> fast_target; // One bit per second, if reachable.
    std::optional<double> day_target; // One day per bit, if reachable.
    // A checked usable edge from the long-duration side: clock search AND RAM.
    // Sample rounding and short-profile policy can create other coverage gaps.
    std::optional<double> clock_target;
    // Checked useful steps, normally about 1 dB, skipping clock/RAM gaps.
    // A remaining end point may be nearer than 1 dB. Never a promise that all
    // intervening sample-quantized profiles fit or that every island is known.
    std::optional<double> stronger_fit_target;
    std::optional<double> weaker_fit_target;
};
// Bounded analytical planning only; no sampled audio or transmission occurs.
Model build(const Inputs& inputs);
std::string duration(double seconds);
}
