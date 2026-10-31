#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace datapump::helper {

// Bounded raw-baseband delay queue. It neither estimates nor applies a radio
// correction. A caller buffers its Helper-only correction plans separately and
// applies them in sample-index order, before those samples enter the existing
// conditioner. This object never reads an OS clock or uses a probe as a clock.
// It is not internally synchronized; producer/consumer calls on one instance
// must be serialized by the caller.
class HelperDelayedBuffer {
public:
    struct Config {
        std::size_t maximum_samples=0, maximum_blocks=0;
        std::uint64_t delay_nanoseconds=2'000'000'000ULL;
        // Maximum discrepancy between adjacent capture-system anchors and the
        // preceding block's extrapolated sample clock. Adjacent blocks may
        // report larger steps, but such a step is flagged as discontinuous.
        long double clock_jump_tolerance_nanoseconds=1'000'000;
    };
    struct TransmitterTime {
        bool valid=false;
        // One persistent receiver-defined reference. This is not an absolute
        // transmitter-time acquisition or an authorization to choose a branch.
        std::uint64_t reference_sample=0;
        long double seconds_at_first_sample=0;
        long double seconds_per_sample=0;
        // Caller-supplied bound for the entire block, including clock slope
        // uncertainty. It is not recomputed by the delay queue.
        long double uncertainty_seconds=0;
        // Exact identity fractional component of the receiver-defined input
        // reference. Values are in [0,1). Construct this once from the tracker
        // and reuse it unchanged, including after gaps and dropouts.
        long double reference_fractional_sample=0;
    };
    struct CaptureBlock {
        std::uint64_t first_sample=0;
        std::size_t sample_count=0;
        bool samples_present=true;
        // The input block begins at these independent clock coordinates.
        // System time may jump. Release decisions use only monotonic time.
        std::int64_t capture_system_nanoseconds=0;
        std::uint64_t capture_monotonic_nanoseconds=0;
        long double sample_period_nanoseconds=0;
        // Absolute per-sample clock-period uncertainty; zero means the caller
        // asserts an exact supplied period. Accumulated error is propagated.
        long double sample_period_uncertainty_nanoseconds=0;
        long double capture_uncertainty_nanoseconds=0;
        // Bound covering the first and also any explicitly supplied last
        // monotonic capture timestamp. Added to the deadline to avoid an early
        // release and used for monotonic clock-discontinuity detection.
        std::uint64_t capture_monotonic_uncertainty_nanoseconds=0;
        // Optional actual last-sample monotonic timestamp. Without it the
        // deadline uses the upper edge of the period-uncertainty interval.
        bool capture_end_monotonic_valid=false;
        std::uint64_t capture_end_monotonic_nanoseconds=0;
        // The producer must split blocks at known clock changes and supply a
        // distinct epoch whenever its system/sample-clock association changes.
        std::uint64_t capture_clock_epoch=0;
        // A valid schedule slot applies to the entire block. The producer
        // must split at hop boundaries; this queue has no hop calendar.
        bool schedule_valid=false;
        std::uint64_t schedule_epoch=0, schedule_slot=0;
        TransmitterTime transmitter_time{};
    };
    struct ReleasedBlock {
        CaptureBlock capture{}; // never overwritten with the release time
        std::uint64_t release_monotonic_nanoseconds=0;
        std::uint64_t eligible_monotonic_nanoseconds=0;
        // Entire block must be captured, then age for configured delay. The
        // first sample consequently waits up to one block longer than the last.
        std::uint64_t missing_samples_before=0;
        bool capture_time_discontinuity=false;
        bool transmitter_reference_mismatch=false;
        bool invalid_helper_frontier=false;
        // Caller supplies the last monotonic time for which Helper samples
        // were actually observed, not merely the current wall/processing time.
        // A tail released after the stream stops has incomplete lookahead.
        // This is only the elapsed distance to the observed frontier, not
        // proof that observations cover the intervening period.
        std::uint64_t helper_frontier_advance_nanoseconds=0;
        bool helper_coverage_contiguous=false;
        bool configured_helper_lookahead_available=false;
    };
    // reference_mismatch also ENQUEUES the samples, but marks the conflicting
    // transmitter estimate invalid in the queued metadata; it never resets the
    // first retained reference. Other non-accepted statuses do not enqueue.
    enum class PushResult { accepted, invalid, out_of_order, full, reference_mismatch };
    enum class PopResult { released, empty, not_ready, output_too_small, nonmonotonic_release };

    explicit HelperDelayedBuffer(Config);
    // Input indices are ordered and may skip; missing indices are reported on
    // the next block. Explicit missing blocks carry no sample bytes but still
    // preserve their index/time range. Gaps are never silently filled.
    PushResult push(const CaptureBlock&,std::span<const std::complex<float>> samples);
    // `helper_frontier_monotonic_nanoseconds` is the caller's actual observed
    // Helper frontier (not a prediction). If its timestamp is uncertain it
    // must be a conservative lower bound; overestimating it falsely qualifies
    // lookahead. It affects metadata only; eligibility
    // is still governed by actual elapsed monotonic time. The caller separately
    // asserts contiguous Helper evidence; a frontier alone does not prove it.
    // `output` must fit the complete present block. Missing blocks require no
    // output space. The conservative default does not assert continuous data.
    PopResult pop(std::uint64_t now_monotonic_nanoseconds,
                  std::uint64_t helper_frontier_monotonic_nanoseconds,
                  std::span<std::complex<float>> output,ReleasedBlock&,
                  bool helper_coverage_contiguous=false);
    std::size_t queued_blocks() const noexcept {return block_count_;}
    std::size_t queued_samples() const noexcept {return sample_count_;}
    std::size_t allocated_bytes() const noexcept;
    // The reported affine mapping is conditional on the supplied capture clock
    // and uncertainty, including accumulated period error and floating-point
    // arithmetic rounding. It is invalid for missing data, a fractional coordinate
    // outside this block, or when its anchor epoch is not trustworthy. Never use
    // it to infer a continuous mapping across two separately stamped blocks.
    static bool capture_system_time_at(const CaptureBlock&,long double input_sample,
                                       long double& system_nanoseconds,
                                       long double& uncertainty_nanoseconds) noexcept;
private:
    struct Entry {
        CaptureBlock capture{};
        std::uint64_t end_monotonic_nanoseconds=0, eligible_nanoseconds=0;
        std::uint64_t missing_before=0;
        bool clock_discontinuity=false,reference_mismatch=false;
    };
    Config config_;
    std::vector<std::complex<float>> samples_;
    std::vector<Entry> blocks_;
    std::size_t sample_head_=0,sample_count_=0,block_head_=0,block_count_=0;
    bool have_previous_=false,have_reference_=false,have_release_clock_=false;
    CaptureBlock previous_{};
    std::uint64_t previous_end_=0,retained_reference_=0,last_release_clock_=0;
    long double retained_reference_fractional_sample_=0;
};
}
