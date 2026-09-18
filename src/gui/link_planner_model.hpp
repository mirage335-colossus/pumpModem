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
    std::string error;
    std::optional<double> fast_target; // One bit per second, if reachable.
    std::optional<double> day_target; // One day per bit, if reachable.
    std::optional<double> clock_target; // Lower edge of current clock search.
};
// Bounded analytical planning only; no sampled audio or transmission occurs.
Model build(const Inputs& inputs);
std::string duration(double seconds);
}
