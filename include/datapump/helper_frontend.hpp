#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace datapump::helper {

struct CaptureBand { double lower_hz=0,upper_hz=0; };

// A coarse FFT peak is only a seed for the independent narrow tone receiver.
// Neither this type nor repeated peaks qualify presence or synchronization.
// half_width is an engineering FFT-bin/guard envelope, not a confidence bound.
// Real PCM has conjugate images: positive frequency cannot establish the sign
// of an RF offset. The caller must retain every admissible signed branch.
struct ToneCandidate {
    double frequency_hz=0,frequency_half_width_hz=0;
    // tone_power estimates sinusoid power (with FFT scalloping), whereas
    // local_noise_power is equivalent rectangular-bin noise power. The raw
    // Hann periodogram-bin peak_to_noise_db differs from their power ratio by
    // the Hann equivalent noise bandwidth (~1.5 bins); none is calibrated C/N0.
    double tone_power=0,local_noise_power=0,peak_to_noise_db=0;
    std::uint64_t first_sample=0,end_sample=0; // end is exclusive
    // Disjoint windows with a stable strongest peak; not a coherent phase fit.
    std::uint32_t coherent_support=0;
};

class CoarseToneSearch {
public:
    struct Config {
        std::uint32_t sample_rate=0;
        double expected_hz=0,search_half_width_hz=0;
        std::size_t fft_size=8192;
        std::uint32_t persistence_windows=3;
    };
    // Component-owned object/FFT/arrays; allocator and runtime stack overhead
    // are external, as for the existing helper receiver workspace contract.
    static std::size_t workspace_required(const Config&);
    // Pure planning query with the same configuration validation as workspace.
    // This FFT-bin/guard envelope is neither confidence nor synchronization.
    static double seed_half_width_bound(const Config&);
    explicit CoarseToneSearch(Config,std::size_t workspace_grant,
                              std::uint64_t first_sample=0);
    ~CoarseToneSearch();
    CoarseToneSearch(CoarseToneSearch&&) noexcept;
    CoarseToneSearch& operator=(CoarseToneSearch&&) noexcept;
    CoarseToneSearch(const CoarseToneSearch&)=delete;
    CoarseToneSearch& operator=(const CoarseToneSearch&)=delete;
    std::size_t workspace_bytes()const noexcept;
    CaptureBand capture_band()const noexcept;
    std::uint64_t transformed_windows()const noexcept;
    void push(std::span<const float>,std::uint64_t first_sample);
    std::optional<ToneCandidate> take_candidate();
    // Explicit capture gap/generation/sample-clock change only. No implicit
    // discontinuity handling, hidden resampling, or reset on missing tone.
    void reset(std::uint64_t first_sample=0)noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A fixed-rate, fixed-delay real-PCM frequency translator. It constructs an
// analytic signal with an antisymmetric Hilbert FIR before mixing; a cosine
// mixer alone would produce a second payload image. The caller supplies q and
// the complete input-band union. Optional continuous-reference updates change
// only the NCO, retaining every sample and the fixed FIR delay.
class AnalyticFrequencyTranslator {
public:
    struct Config {
        std::uint32_t sample_rate=0;
        double correction_hz=0;
        CaptureBand input_band{};
        std::uint64_t first_sample=0;
        bool conjugate=false;
        // Prepare analytic conversion even when the initial correction is zero.
        bool continuous_reference=false;
    };
    struct PilotObservation {
        double expected_hz=0,observed_hz=0,phase_cycles=0;
        std::uint64_t phase_sample=0;
        double phase_sample_fraction=0;
        // Half-open support of one coherent, settled phase/frequency model.
        // The caller owns its qualification, including retune boundaries.
        std::uint64_t first_sample=0,end_sample=0,captured_end_sample=0;
        // Optional dated oscillator-model frequency for the continued NCO
        // slope. Absolute phase transport still uses observed_hz from this
        // single coherent local fit. Empty preserves the local-fit slope.
        std::optional<double> tracking_frequency_hz={};
    };
    static constexpr std::size_t tap_count=1025;
    static constexpr std::size_t group_delay=(tap_count-1)/2;
    // Component-owned fixed FIR/history/object storage, excluding the external
    // allocator/runtime stack. No per-push or response-certificate allocation.
    static std::size_t workspace_required()noexcept;
    explicit AnalyticFrequencyTranslator(Config,std::size_t workspace_grant);
    ~AnalyticFrequencyTranslator();
    AnalyticFrequencyTranslator(AnalyticFrequencyTranslator&&)noexcept;
    AnalyticFrequencyTranslator& operator=(AnalyticFrequencyTranslator&&)noexcept;
    AnalyticFrequencyTranslator(const AnalyticFrequencyTranslator&)=delete;
    AnalyticFrequencyTranslator& operator=(const AnalyticFrequencyTranslator&)=delete;
    std::size_t workspace_bytes()const noexcept;
    std::size_t delay()const noexcept{return group_delay;}
    CaptureBand input_band()const noexcept;
    CaptureBand output_band()const noexcept;
    // Continuous stored-FIR linear-response bounds. They include roundoff in
    // qualification, but not float PCM rounding, DAC/RF errors or input energy
    // outside the declared band. Constructor declines above .001.
    double maximum_amplitude_error()const noexcept;
    double image_leakage_bound()const noexcept;
    // For output coordinate n, the corresponding input is n-delay(). The NCO
    // adds this phase AFTER optional analytic conjugation. A physical reference
    // at k therefore maps to output k+delay(), sign*phase+phase_at(k+delay()).
    double phase_at(std::uint64_t output_sample)const noexcept;
    std::uint64_t next_sample()const noexcept;
    // Change only the NCO slope at the next unprocessed sample. Its phase and
    // FIR history remain continuous. After a change phase_at describes the
    // current affine segment, not a historical phase observation.
    // An explicit frequency-only update also cancels a pending pilot update.
    bool track_frequency(double correction_hz)noexcept;
    // Transfer an absolute raw-cosine pilot phase to the common NCO. The
    // correction starts at max(next_sample(), first_sample+2*delay()), only
    // when its complete Hilbert input window lies in the supplied support. A
    // future start is held in fixed numeric state and applied inside push;
    // phase_at continues to describe the current segment until then. A second
    // pilot update is declined while one is pending. The caller must supply
    // the actual captured frontier, and must establish that this single-hop
    // model supports the target coordinate; the API grants no phase authority
    // across a retune or a future coherence guarantee.
    bool track_pilot(const PilotObservation&)noexcept;
    // Exactly input.size() outputs, including the known FIR startup transient.
    // No EOF flush, time warp, gain normalization, or reset on helper loss.
    // Input and output may alias exactly. Gaps and nonfinite PCM are rejected.
    void push(std::span<const float> input,std::span<float> output,
              std::uint64_t first_sample);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
