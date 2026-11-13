#pragma once

#include "datapump/fast/modem.hpp"
#include <array>
#include <functional>
#include <memory>

namespace datapump::fast::detail {

inline constexpr unsigned dense_acoustic_frontend_radius=48;
using DenseAcousticBandpass=std::array<double,2*dense_acoustic_frontend_radius+1>;
DenseAcousticBandpass dense_acoustic_bandpass(double center_hz);

std::uint64_t dense_acoustic_end_silence_samples(const Profile&);

struct DenseTiming {
    double epoch;
    double samples_per_symbol;
    double carrier_hz_offset;
};

// Verify a new same-ordinal raw timing observation against the most recent
// independently verified public marker. Receipt latency is a separate limit.
bool dense_marker_timing_matches(const Profile&,std::uint64_t ordinal,
    DenseTiming reference,DenseTiming observed,double allowance_samples);
std::uint64_t dense_public_marker_tick(const Profile&,std::uint64_t ordinal,DenseTiming);

inline constexpr std::size_t dense_pilot_groups_per_unit=8;
inline constexpr std::size_t dense_bits_per_unit=2*4*dense_pilot_groups_per_unit;
inline constexpr std::size_t dense_units_per_interval=physical_interval_bits/dense_bits_per_unit;
inline constexpr std::size_t dense_points_per_interval=physical_interval_bits/2;
inline constexpr unsigned dense_quiet_veto_lookahead=80;

enum class DensePresence { unscored, absent, present };
struct DensePilotReceipt {
    double quality=0.;
    DensePresence presence=DensePresence::unscored;
    std::optional<ReceivedSignalRange> support;
    std::optional<double> evm;
};

// Wire ordinals are fixed by the independently observed public bootstrap.
// A receipt is synchronous and its soft-bit view is valid only during the call.
struct DenseIntervalReceipt {
    std::uint64_t ordinal;
    ReceivedSignalRange support;
    std::array<DensePilotReceipt,dense_units_per_interval> units;
    std::span<const float> llrs;
    std::span<const std::optional<std::complex<float>>> points;
};

using DenseIntervalObserver=std::function<void(const DenseIntervalReceipt&)>;
using DensePresenceObserver=std::function<void(ReceivedSignalRange)>;
using DenseTimingObserver=std::function<void(std::uint64_t,DenseTiming)>;
using DenseQuietObserver=std::function<void(ReceivedSignalRange,bool absent)>;

struct DenseReceiverObservers {
    DenseIntervalObserver interval;
    DensePresenceObserver presence;
    DenseTimingObserver timing;
    DenseQuietObserver quiet;
};

// Compact single carrier with four data symbols between public pilot words.
// This receiver uses observed local channel variation, never simulation state.
class DenseAcousticReceiver {
public:
    DenseAcousticReceiver(const Profile&, ValidatedIntervalSink, SymbolObserver,
        SymbolObserver, std::stop_token, std::optional<ChannelBounds>, DenseReceiverObservers={},
        std::optional<ChannelAssumptions> = {});
    ~DenseAcousticReceiver();
    std::size_t consume(std::span<const float>);
    void finish() noexcept;
    const ModemProgress& progress() const noexcept;
    std::optional<std::uint64_t> completion_tick() const noexcept;
    std::size_t workspace_bytes() const noexcept;
    bool adopt_timing(DenseTiming,std::uint64_t next_ordinal,bool cadence_verified=false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
