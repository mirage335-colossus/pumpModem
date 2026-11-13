#pragma once
#include "datapump/fast/session.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

namespace datapump::gui::fast_ui {
// Steady modem payload rate: local fixed source geometry divided by physical
// coding-cycle time. Includes FEC, integrity, recurring markers, pilots and OFDM
// guards, but excludes source compression, startup and the final absence wait.
inline double expected_modem_bitrate(const fast::Profile& p,bool encrypted) {
    const auto intervals=fast::cycle_intervals(p);
    const auto period=p.capacity_mode?std::lcm(intervals,static_cast<std::size_t>(p.marker_spacing_intervals)):intervals;
    const auto cycles=period/intervals;
    const double bytes=p.capacity_mode?fast::capacity_source_bytes_per_cycle(p,encrypted):
        static_cast<double>(p.interleave_depth)*fast::source_bytes_per_group(p,encrypted)*8./9.;
    const auto samples=fast::transmission_samples(p,period*2)-fast::transmission_samples(p,period);
    return samples?bytes*8.*cycles*p.sample_rate/samples:0.;
}
inline bool fresh_input(const fast::Diagnostics& d,fast::Telemetry::Clock::time_point now) {
    return !d.transmitting && d.waveform_count && d.captured_at!=fast::Telemetry::Clock::time_point{} &&
        now>=d.captured_at && now-d.captured_at<=std::chrono::seconds(2);
}
inline std::optional<fast::LoopbackSnrEstimate> loopback_signal_snr(const fast::Snapshot& snapshot,
    fast::Telemetry::Clock::time_point now=fast::Telemetry::Clock::now()) {
    const auto& d=snapshot.diagnostics;
    if(!snapshot.loopback_test || snapshot.cancelled || !d || d->transmitting || !d->acquired ||
       !d->sample_rate || !d->last_loopback_snr || d->last_loopback_snr->observation_sample>d->samples ||
       (snapshot.active && !fresh_input(*d,now)))return std::nullopt;
    return d->last_loopback_snr;
}
// Reception labels describe the captured result, never a later settings edit.
inline std::string transfer_stage(const fast::Snapshot& snapshot) {
    const auto completed_label=[&] {
        if(snapshot.loopback_test)return std::string("LOOPBACK TEST")+
            (snapshot.authenticated?" · authenticated":" · checksum verified, unauthenticated");
        return std::string(snapshot.simulated?"SIMULATED":"RECEIVED")+
            (snapshot.authenticated?" · authenticated":" · checksum verified, unauthenticated");
    };
    // Content completion is already gated on observed physical absence. The
    // session worker can still be unwinding after it publishes that result.
    if(snapshot.complete)return completed_label();
    if(snapshot.active) {
        if(snapshot.loopback_test) {
            if(snapshot.decoding_stopped)return "LOOPBACK TEST / DECODING STOPPED";
            if(snapshot.failed_cycles)return "LOOPBACK TEST / MISSING DATA";
            return "LOOPBACK TEST / CAPTURING AND CHECKING";
        }
        if(snapshot.transmitting&&!snapshot.simulated)return "TRANSMITTING";
        const std::string operation=snapshot.simulated?"SIMULATING":"RECEIVING";
        if(snapshot.decoding_stopped)return operation+" / DECODING STOPPED";
        if(snapshot.failed_cycles)return operation+" / MISSING DATA";
        return operation+" / PENDING";
    }
    if(snapshot.cancelled)return snapshot.loopback_test?"LOOPBACK TEST · CANCELLED · no completion implied":"CANCELLED · no completion implied";
    if(snapshot.loopback_test&&(snapshot.failed_cycles||snapshot.decoding_stopped||!snapshot.error.empty()))return "LOOPBACK TEST · INCOMPLETE";
    if(snapshot.failed_cycles||snapshot.decoding_stopped||!snapshot.error.empty())return "INCOMPLETE";
    return snapshot.source_bytes?"TRANSMISSION FINISHED":"READY";
}
inline std::string integrity_label(const fast::Snapshot& snapshot) {
    if(!snapshot.revision&&!snapshot.active&&!snapshot.complete&&!snapshot.physical_complete)
        return "Receive integrity · no received stream yet";
    const auto protection=snapshot.encrypted?
        "Encryption · "+std::to_string(snapshot.authenticated_groups)+" authenticated groups":
        "Public data · "+std::to_string(snapshot.checksum_groups)+" checksum-verified groups";
    const auto damage=snapshot.failed_cycles?
        " · "+std::to_string(snapshot.failed_cycles)+(snapshot.failed_cycles==1?" damaged cycle":" damaged cycles"):std::string{};
    return (snapshot.loopback_test?"LOOPBACK TEST · ":"")+protection+damage+"\n"+(snapshot.encrypted?"":"Unauthenticated · ")+(snapshot.complete?
        (snapshot.loopback_test?"test validated":"received bytes available"):snapshot.physical_complete?
        "physical end; validation pending/failed":snapshot.decoding_stopped?"decoding stopped; awaiting physical end":
        snapshot.failed_cycles?"decoding continues; awaiting physical end":"awaiting physical end");
}
}
