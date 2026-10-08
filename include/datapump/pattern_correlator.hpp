#pragma once
#include "datapump/pattern_receiver.hpp"

namespace datapump::modem {
namespace detail {class CorrelationProjectionCache;}
// Numerical diagnostics for paired validation. These are deliberately separate
// from saved modem/search settings; ordinary callers select the automatic path.
struct PatternCorrelatorOptions {
    bool raw_reference=false;
    bool measure_work=false;
};
enum class PatternCorrelationBackend { raw, pulse, pulse_moments, pulse_segments };
struct PatternCorrelatorWork {
    PatternCorrelationBackend backend=PatternCorrelationBackend::raw;
    std::size_t hypotheses=0,lattices=0,phase_groups=0,peak_workspace_bytes=0;
    unsigned drift_sections=1;
    std::uint64_t differential_window_samples=0,samples=0,cells=0,segments=0;
    // Kernel preparation is included in frontend time and also shown separately.
    // Opt-in timers affect execution cost; use uninstrumented runs for speedup.
    double frontend_seconds=0,search_seconds=0,kernel_seconds=0;
    double frontend_cpu_seconds=0,search_cpu_seconds=0,kernel_cpu_seconds=0;
};
// Finite clock/frequency/start-time hypotheses with streaming sufficient
// statistics. Storage depends on requested coverage and retained bits, never
// the duration of a symbol. A system-clock start window is mandatory.
class PatternCorrelator {
public:
    PatternCorrelator(Config, PatternSearch, std::size_t workspace_bytes,
                      PatternCorrelatorOptions = {});
    ~PatternCorrelator();
    PatternCorrelator(PatternCorrelator&&) noexcept;
    PatternCorrelator& operator=(PatternCorrelator&&) noexcept;
    void push(std::span<const float>, std::stop_token = {}, detail::CorrelationProjectionCache* = nullptr);
    void finish(std::stop_token = {});
    // Publish accepted decisions at each drain; chunk capacity never
    // requires a longer message before a pending symbol becomes visible.
    std::vector<PatternBurst> take_bursts();
    PatternBurst provisional() const;
    std::vector<PatternEvidence> candidates() const;
    std::vector<PatternEvidence> candidates(std::size_t limit) const;
    std::vector<std::complex<double>> take_chip_constellation();
    bool acquiring() const;
    bool synchronized() const;
    // Every original clock hypothesis has scored a fully observed symbol.
    // A leading partial symbol and capture EOF do not satisfy this coverage.
    bool initial_search_complete() const;
    // Actual detector availability after reserving the complete search bank.
    bool drift_tolerant() const;
    std::size_t working_bytes() const;
    // Includes promised optional detector state not yet materialized by a push.
    std::size_t reserved_workspace_bytes() const;
    void set_workspace_bytes(std::size_t);
    Diagnostics diagnostics() const;
    PatternCorrelatorWork work() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
