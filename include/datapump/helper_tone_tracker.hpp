#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <span>

namespace datapump::helper {
struct ToneTrackerConfig {
    std::uint32_t sample_rate=0;
    double expected_hz=0,search_half_width_hz=0;
    double phase_noise_radians_per_sqrt_second=0;
    // Operational following gate, not a drift rate inferred from ppm accuracy.
    // Unknown temperature evolution is not a prediction or a future lease.
    double maximum_tracking_slew_hz_per_second=.5;
    // Pure pilot: independent short phase apertures, frequency-only pooling.
    // Auxiliary decoding and phase continuity across retunes are not required.
    bool segmented_pilot=false;
    // An unretuned pure pilot retains phase across publication windows and
    // amplitude ramps. Explicit retunes always invalidate this coherence.
    // False retains the independent200ms frequency-pooling baseline.
    bool continuous_pilot=true;
};
struct ToneFrequencyQuery {
    bool valid=false;
    double frequency_hz=0,uncertainty_hz=0;
};
// One current straight full-dwell frequency observation. Validity certifies
// measured support and estimator geometry only; it does not qualify ordinary
// reception, phase correction, a clock relation or a future frequency lease.
struct ToneFrequencyObservation {
    bool valid=false;
    std::uint64_t first_sample=0,end_sample=0,state_endpoint_sample=0;
    std::uint64_t mean_sample=0;
    double mean_sample_fraction=0,frequency_hz=0;
    // Independent thermal/declared phase-process variance excludes the
    // separately reported observed residual excess (which can include drift,
    // model mismatch or self-interference). Neither is the local slew bound.
    double independent_variance_hz2=0,residual_variance_hz2=0;
    // Endpoint b is the last observed ADC center, end_sample-1. The frequency
    // estimate is dated b-mu. The actual linearized phase-fit kernel has
    // w=-cumulative phase impulses, k''=w and k(a)=k'(a)=0.
    double mass=0,mu_seconds=0,integral_k_seconds2=0;
    double integral_b_minus_s_k_seconds3=0,integral_k_squared_seconds3=0;
    double k_at_end_seconds=0,k_prime_at_end=0;
};
struct ToneTrackerSnapshot {
    std::uint64_t sample=0,captured_end_sample=0,first_sample=0;
    std::uint32_t sample_rate=0;
    ToneFrequencyObservation frequency_observation;
    double frequency_hz=0,uncertainty_hz=0,carrier_to_noise_db_hz=0;
    double frequency_rate_hz_per_second=0;
    // Covariance of the locally linear frequency model evaluated at sample.
    // Units are Hz^2, Hz^2/s and Hz^2/s^2, respectively. Includes measured
    // residual inflation; it carries no phase authority or future lease.
    double frequency_variance_hz2=0,frequency_rate_covariance_hz2_per_second=0;
    double frequency_rate_variance_hz2_per_second2=0;
    double frequency_model_expected_hz=0,frequency_model_residual_scale=1;
    bool frequency_covariance_valid=false;
    bool present=false,qualified=false;
    // Joint phase/frequency fits to disjoint200ms contexts refine the FLL
    // using at most36s of bounded numeric anchors. Full support is
    // first_sample..sample. This conditional locally linear evolution model
    // includes measured curvature/noise allowances and a current FLL veto.
    // Abrupt frequency or drift changes can temporarily exceed its engineering
    // uncertainty before withdrawal; it grants no future thermal/Doppler bound.
    bool frequency_precision_refined=false;
    bool frequency_continuous_phase=false;
    std::uint64_t continuous_phase_resets=0;
    std::uint64_t continuous_model_resets=0,continuous_last_model_reset_sample=0;
    // Explicit actual-retune mode only: one independently acquired straight
    // aperture, with no pooled RF/audio model. Its constant engineering
    // interval includes the configured operational slew allowance throughout
    // that observed aperture; it grants neither drift measurement nor clock
    // authority. frequency_at() remains confined to first_sample..sample.
    bool frequency_local_aperture=false;
    // Pilot-mode provenance: support may span many independent phase apertures.
    double coherent_aperture_seconds=0,frequency_support_seconds=0,observation_age_seconds=0;
    // Latest disjoint frequency context. A fallback's selected gap is a model,
    // not a known retune date; these fields never grant phase authority.
    bool frequency_used_fallback=false;
    unsigned frequency_gap_index=0;
    double frequency_aperture_cube_seconds3=0;
    std::uint64_t frequency_context_first_sample=0,frequency_context_end_sample=0;
    // Absolute cosine carrier phase modulo one cycle at phase_sample (raw ADC
    // coordinate), conditional on the current20ms projection. Never pooled.
    double phase_cycles=0,phase_uncertainty_cycles=0,phase_sample_fraction=0;
    std::uint64_t phase_sample=0;
    // Projection values above are diagnostics; they never qualify pilot phase correction.
    bool phase_qualified=false,projection_phase_valid=false;
    // Accepted independent context only. Support is[first,end); the fractional
    // reference date is observed, and frequency is this local fit rather than
    // the separately pooled oscillator estimate. Never grants phase outside
    // this actual support or continuity through an unknown hop.
    double phase_fit_cycles=0,phase_fit_uncertainty_cycles=0;
    double phase_fit_frequency_hz=0,phase_fit_frequency_uncertainty_hz=0;
    // Whole observed support, including joint phase/slope covariance and the
    // local process/residual allowances. Absent on legacy snapshots; never
    // supplies authority independently of phase_fit_qualified and its dates.
    std::optional<double> phase_fit_support_uncertainty_cycles;
    double phase_fit_sample_fraction=0;
    std::uint64_t phase_fit_sample=0,phase_fit_first_sample=0,phase_fit_end_sample=0;
    bool phase_fit_qualified=false;
    // Independent source-clock evidence from actual periodic reversals.
    // Positive ppm means the generator's400ms intervals arrive in fewer ADC
    // samples. A quasistationary local aperture-mean engineering estimate with
    // actual dated support; no future thermal/evolution lease, UTC epoch,
    // payload-clock relation or phase authority follows. Consumers must apply
    // their original clock domain and separately validate conditional use.
    double clock_error_ppm=0,clock_uncertainty_ppm=0;
    std::uint64_t clock_sample=0,clock_first_sample=0,clock_last_sample=0;
    bool clock_qualified=false;
    // Interpolate only inside the retained model's observed support bounds.
    // Output Hz belongs to the CURRENT explicitly mapped reference geometry:
    // common residual=(Hz-frequency_model_expected_hz)/frequency_model_residual_scale.
    // Historical RF geometry requires the caller's own dated remapping. Gaps
    // inside these bounds remain conditional on the fitted linear model; this
    // is frequency interpolation, never a claim of observed past phase.
    ToneFrequencyQuery frequency_at(std::uint64_t query_sample,double fraction=0)const noexcept {
        ToneFrequencyQuery out;
        if(!qualified || !frequency_covariance_valid || !sample_rate ||
           !std::isfinite(fraction) || fraction<0 || fraction>=1 ||
           query_sample<first_sample || query_sample>sample ||
           (query_sample==sample && fraction>0))return out;
        const auto dt=static_cast<double>((static_cast<long double>(query_sample)-sample+fraction)/sample_rate);
        const auto variance=std::fma(dt,std::fma(dt,frequency_rate_variance_hz2_per_second2,
            2*frequency_rate_covariance_hz2_per_second),frequency_variance_hz2);
        out.frequency_hz=std::fma(frequency_rate_hz_per_second,dt,frequency_hz);
        if(!(variance>=0) || !std::isfinite(variance) || !std::isfinite(out.frequency_hz))return out;
        out.uncertainty_hz=3*std::sqrt(variance);out.valid=true;return out;
    }
};
// workspace_required/bytes include this object and its fixed owned implementation.
// Synchronous strongest-tone acquisition followed by short phase-difference
// frequency measurements. The interval is an engineering noise/variance gate,
// not a calibrated 99% confidence claim. No epoch, UTC or message authority
// is created; source-clock evidence is independently conditional. Real capture
// retains an unresolved conjugate-image/physical-sideband branch ambiguity.
// Qualified following needs the tone at least200Hz from DC/Nyquist so its
// independent nearby noise projections remain physically representable.
class ToneTracker {
public:
    static std::size_t workspace_required(const ToneTrackerConfig&);
    explicit ToneTracker(ToneTrackerConfig,std::size_t workspace_grant,
                         std::uint64_t first_sample=0);
    ~ToneTracker();
    ToneTracker(ToneTracker&&)noexcept;
    ToneTracker& operator=(ToneTracker&&)noexcept;
    ToneTracker(const ToneTracker&)=delete;
    ToneTracker& operator=(const ToneTracker&)=delete;
    std::size_t workspace_bytes()const noexcept;
    void reset(std::uint64_t first_sample=0)noexcept;
    void push(std::span<const float>,std::uint64_t first_sample);
    // Call at an actual retune boundary, before its first new sample. The phase
    // is always invalidated. Frequency records survive only with an explicitly
    // justified model: (observed_hz-expected_hz)/residual_scale is the common
    // quantity. A single tone cannot establish RF versus audio-clock scaling.
    // A nonzero settled_end_sample is the caller-justified EXCLUSIVE end of
    // the usable straight-pilot aperture. No Helper-calendar/ramp alignment is
    // inferred or subtracted. Its Hann kernel assumes constant amplitude
    // within each row. It requests one bounded full-dwell fit with one causal
    // reprojection of its discovery PCM. Phase remains
    // independent after every retune. Zero preserves ordinary short contexts.
    void begin_pilot_segment(std::uint64_t first_sample,double expected_hz,
                             double search_half_width_hz,bool preserve_frequency=false,
                             double residual_scale=1,std::uint64_t settled_first_sample=0,
                             bool qualify_local_aperture=false,
                             std::uint64_t settled_end_sample=0);
    ToneTrackerSnapshot snapshot()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
