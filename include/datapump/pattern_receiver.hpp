#pragma once
#include "datapump/modem.hpp"
#include <memory>

namespace datapump::modem {
// Scores use natural-log evidence against an ideal circular-Gaussian reference.
// Real-PCM covariance and adaptive searches require empirical calibration;
// neither a lifetime false-alarm guarantee nor authentication is implied.
struct PatternEvidence {
    std::uint64_t first_sample = 0, end_sample = 0, stream_symbol = 0;
    double frequency_hz = 0, score = 0, alternative_score = 0;
    unsigned bit = 0;
};
struct PatternBurst {
    Bytes bits;
    std::uint64_t first_sample = 0, end_sample = 0;
    std::uint64_t first_stream_symbol = 0;
    double frequency_hz = 0, score = 0;
    bool complete = false;
};
struct PatternSearch {
    // A finite, explicit frequency bank. Empty selects five offsets separated
    // by 1/(4 symbol duration). No carrier/constellation lock precedes scoring.
    std::vector<double> frequency_offsets_hz;
    double false_alarm_probability = 1e-8;
    double retain_score = 5;
    std::size_t candidate_limit = 2048, track_limit = 16, bit_limit = 1024 * 1024;
    // Independently try these first stream positions (0 through count-1).
    // This bounded local keystream search adds no transmitted metadata.
    std::size_t initial_stream_symbols = 4;
    // Optional system-clock prediction of symbol zero relative to the first
    // supplied sample. The long-symbol correlator searches this entire window
    // at half-chip resolution; it rejects unaffordable coverage explicitly.
    std::optional<double> start_offset_seconds;
    double start_uncertainty_seconds = 0;
    std::vector<double> clock_errors_ppm{0};
};
class PatternReceiver {
public:
    PatternReceiver(Config, std::size_t workspace_bytes = 8 * 1024 * 1024,
                    PatternSearch search = {});
    ~PatternReceiver();
    PatternReceiver(PatternReceiver&&) noexcept;
    PatternReceiver& operator=(PatternReceiver&&) noexcept;
    void push(std::span<const float>, std::stop_token = {});
    // Shared per-sample carrier projection for a bank with one carrier/clock.
    // projected[i] is raw[i] * exp(-j*carrier_phase[i]); an arbitrary fixed
    // projection phase is fitted by the pattern score. Both spans must match.
    void push(std::span<const float>,std::span<const std::complex<double>> projected,std::stop_token = {});
    void finish(std::stop_token = {});
    std::vector<PatternBurst> take_bursts();
    PatternBurst provisional() const;
    std::vector<PatternEvidence> candidates() const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // True when acquisition covers an explicit finite system-clock window.
    bool clock_windowed() const;
    std::size_t working_bytes() const;
    // Reject a reduction that cannot retain current state. Future candidate
    // growth is capped within the new ceiling; excess payload throws Error.
    void set_workspace_bytes(std::size_t);
    Diagnostics diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
