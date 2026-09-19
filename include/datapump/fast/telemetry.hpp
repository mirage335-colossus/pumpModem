#pragma once

#include "datapump/fast/profile.hpp"
#include <array>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace datapump::fast {

// Display-only samples. They never feed carrier/timing loops, admission, FEC,
// authentication, file interpretation, or physical-completion decisions.
struct Diagnostics {
    std::uint64_t stream_id=0,revision=0,samples=0;
    std::uint32_t sample_rate=0;
    unsigned constellation=0;
    bool transmitting=false,acquired=false,spectrum_valid=false;
    std::size_t waveform_count=0,constellation_count=0,input_count=0;
    float waveform_rms=0,waveform_peak=0;
    std::array<float,1024> waveform{};
    // Latest 512 samples, Hann-windowed, amplitude dBFS. Bin k is k*Fs/512
    // Hz; DC uses one-sided DC scaling. Values below -120 dBFS are clamped.
    std::array<float,256> spectrum_db{};
    std::array<std::complex<float>,512> constellation_points{};
    // Matched-filter input samples before timing/gain/carrier correction.
    // These are separate from acquired payload symbols and may contain noise.
    std::array<std::complex<float>,512> input_points{};
};

std::uint64_t next_diagnostics_stream_id() noexcept;
std::shared_ptr<const Diagnostics> initial_diagnostics(const Profile&,bool transmitting,std::uint64_t stream_id) noexcept;

// One worker owns the collector; consumers only see immutable frames. Arrays
// remain fixed for the stream lifetime. Symbol recording is an O(1) modem tap;
// FFT/publication happens only on the capture consumer/playback producer.
class Telemetry {
public:
    using Clock=std::chrono::steady_clock;
    Telemetry(const Profile&,bool transmitting,std::uint64_t stream_id) noexcept;
    void record_samples(std::span<const float>) noexcept;
    void record_symbol(std::complex<float>) noexcept;
    void record_input(std::complex<float>) noexcept;
    // Returns a new frame at most once per 100 ms, otherwise null. An allocation
    // failure drops diagnostic output only and cannot abort the transfer.
    std::shared_ptr<const Diagnostics> publish(bool acquired,Clock::time_point now=Clock::now()) noexcept;
    std::size_t workspace_bytes() const noexcept;
private:
    std::uint64_t stream_id_,revision_=0,samples_=0;
    std::uint32_t sample_rate_;
    unsigned constellation_;
    bool transmitting_,published_=false;
    Clock::time_point last_publication_{};
    std::array<float,1024> waveform_{};
    std::array<std::complex<float>,512> points_{};
    std::array<std::complex<float>,512> input_{};
    std::size_t waveform_position_=0,waveform_count_=0,point_position_=0,point_count_=0;
    std::size_t input_position_=0,input_count_=0;
};

} // namespace datapump::fast
