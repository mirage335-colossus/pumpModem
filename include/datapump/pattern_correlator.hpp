#pragma once
#include "datapump/pattern_receiver.hpp"

namespace datapump::modem {
// Finite clock/frequency/start-time hypotheses with streaming sufficient
// statistics. Storage depends on requested coverage and retained bits, never
// the duration of a symbol. A system-clock start window is mandatory.
class PatternCorrelator {
public:
    PatternCorrelator(Config, PatternSearch, std::size_t workspace_bytes);
    ~PatternCorrelator();
    PatternCorrelator(PatternCorrelator&&) noexcept;
    PatternCorrelator& operator=(PatternCorrelator&&) noexcept;
    void push(std::span<const float>, std::stop_token = {});
    void finish(std::stop_token = {});
    std::vector<PatternBurst> take_bursts();
    PatternBurst provisional() const;
    std::vector<PatternEvidence> candidates() const;
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    std::size_t working_bytes() const;
    void set_workspace_bytes(std::size_t);
    Diagnostics diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
