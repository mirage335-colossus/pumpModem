#pragma once
#include "datapump/live.hpp"
#include <algorithm>

namespace datapump::live::detail {
// Both local transmit geometries share one helper frequency. Selecting a long
// message or editing the callsign cannot move the carrier into the other band.
inline double helper_occupied_bandwidth(const Settings& settings) {
    const auto occupied=[](const modem::Config& c) {
        return modem::waveform_bandwidth_hz(c)+modem::waveform_gate_bandwidth_allowance_hz(c);
    };
    auto bandwidth=occupied(settings.transfer.modem);
    if(settings.long_message_modem)bandwidth=std::max(bandwidth,occupied(*settings.long_message_modem));
    return bandwidth;
}
}
