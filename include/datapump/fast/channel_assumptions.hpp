#pragma once

namespace datapump::fast {
struct Profile;

// Locally declared worst-case link conditions. No observed or realized
// channel values enter this declaration or select transmitter geometry.
struct ChannelAssumptions {
    double baseband_ppm = 0;
    double baseband_phase_rad_sqrt_s = 0;
    double shift_ppm = 0;
    double shift_phase_rad_sqrt_s = 0;
    bool shared_reference = false;
    double shift_hz = 0; // RF oscillator coordinate, never an audio carrier.
    double delay_ms = 0;
    double doppler_hz = 0;
    double selective_slow_depth = 0;
    double selective_fast_depth = 0;
    double margin = 1;

    bool operator==(const ChannelAssumptions&) const = default;
};

void validate(const ChannelAssumptions&);
struct ChannelBounds {
    double clock_ppm;
    double frequency_hz;
    double delay_ms;
};

struct ReceiverAssumptionBounds {
    ChannelBounds requested;
    ChannelBounds admitted;
    bool sparse_search;
};

// Validates the requested limits against the selected native backend and
// returns its local search envelope, including hardware/model allowance.
ReceiverAssumptionBounds receiver_assumption_bounds(const Profile&, const ChannelAssumptions&);
void validate_receiver_assumptions(const Profile&, const ChannelAssumptions&);
}
