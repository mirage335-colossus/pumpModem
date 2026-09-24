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
// Spectrum-only display estimate. Equal-width PSD averages compare occupied
// receive bins against surrounding unoccupied bins; subtract the estimated
// in-band noise before taking S/N. No value feeds receiver decisions.
inline std::optional<double> waterfall_snr(const fast::Diagnostics& d,const fast::Profile& p) {
    if(d.transmitting||!d.spectrum_valid||!d.sample_rate)return std::nullopt;
    const double bin_hz=d.sample_rate/512.;
    const double low=fast::occupied_lower_hz(p),high=fast::occupied_upper_hz(p);
    const double guard=2*bin_hz,adjacent=std::max((high-low)*.25,4*bin_hz);
    double signal_noise=0,noise=0;unsigned occupied_count=0,noise_count=0;
    for(std::size_t k=1;k<d.spectrum_db.size();++k) {
        const double hz=k*bin_hz,db=d.spectrum_db[k];
        if(!std::isfinite(db))continue;
        const auto power=std::pow(10.,db/10.);
        if(hz>=low&&hz<=high) {signal_noise+=power;++occupied_count;}
        else if((hz>=low-guard-adjacent&&hz<=low-guard)||(hz>=high+guard&&hz<=high+guard+adjacent)) {
            noise+=power;++noise_count;
        }
    }
    if(!occupied_count||noise_count<2)return std::nullopt;
    noise/=noise_count;signal_noise/=occupied_count;
    if(noise<=1.01e-12&&signal_noise<=1.01e-12)return std::nullopt;
    return 10*std::log10(std::max(1e-4,(signal_noise-noise)/std::max(1e-12,noise)));
}
// Reception labels describe the captured result, never a later settings edit.
inline std::string transfer_stage(const fast::Snapshot& snapshot) {
    // Content completion is already gated on observed physical absence. The
    // session worker can still be unwinding after it publishes that result.
    if(snapshot.complete)return snapshot.authenticated?"RECEIVED · authenticated":"RECEIVED · checksum verified, unauthenticated";
    if(snapshot.active) {
        if(snapshot.transmitting)return "TRANSMITTING";
        if(snapshot.decoding_stopped)return "RECEIVING / DECODING STOPPED";
        if(snapshot.failed_cycles)return "RECEIVING / MISSING DATA";
        return "RECEIVING / PENDING";
    }
    if(snapshot.cancelled)return "CANCELLED · no completion implied";
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
    return protection+damage+"\n"+(snapshot.encrypted?"":"Unauthenticated · ")+(snapshot.complete?"received bytes available":snapshot.physical_complete?
        "physical end; validation pending/failed":snapshot.decoding_stopped?"decoding stopped; awaiting physical end":
        snapshot.failed_cycles?"decoding continues; awaiting physical end":"awaiting physical end");
}
}
