#pragma once

// PRIVATE ESTIMATOR PROTOTYPE. No receiver, framing or admission state changes.
#include "datapump/helper_filter.hpp"
#include "pattern_fft_batch.hpp"
#include <optional>
#include <stop_token>
#include <string>

namespace datapump::simulation::detail::conditioned_private {

// This first prototype models logical-input AWGN. It does not model the
// physical preselector/decimator, radio, or an adaptively estimated phase view.
// Both source arrays must be actual production-rendered isolated first-symbol
// PCM, including the entire FIR halo, at the declared fixed phase/CFO/LF.
// Unknown adjacent symbols and outer-code presence/physical END are excluded.
struct FrozenConditioning {
    std::optional<double> filter_hz;
    double frequency_correction_hz=0;
    // EXCLUSIVE observed raw frontier. For helper::Reference's inclusive
    // last-observed sample, the adapter must use an overflow-checked +1.
    // Equality with first_raw_sample is disjoint, including the FIR halo.
    std::uint64_t first_raw_sample=0,prior_evidence_end_sample=0;
    bool independent_prior=false;
};
struct FirstBitRequest {
    modem::Config config;
    modem::detail::FftSearchGeometry geometry;
    // Original initial-symbol identities/order for this conditional epoch are
    // retained. A mask may select RF cells, but every original (symbol,phase,LF)
    // group must remain represented. No new cells. Other epoch acquisition is
    // excluded; its ORIGINAL threshold charge is retained.
    std::vector<modem::detail::FftSearchJob> original_jobs;
    std::vector<std::uint8_t> selected_original_cells;
    // Supplied by the ORIGINAL ordinary admission policy, before RF selection.
    // This component never derives a smaller trial penalty from selected work.
    double original_acquisition_threshold=0;
    std::uint64_t original_threshold_trials=0;
    std::size_t starts=1;
    // Export the actual native Impl policy. False requires the selected RF
    // span to fit one ordinary close-frequency cluster (<=Fs/symbol).
    bool competing_frequencies=false;
    FrozenConditioning conditioning;
    std::array<std::vector<float>,2> source_pcm;
    // Optional actual helper waveform, in the same raw coordinates and units.
    // Empty explicitly leaves helper leakage/interference unmodeled.
    std::vector<float> helper_pcm;
    double signal_scale=1,raw_noise_variance=1;
    std::size_t trials=256;
    std::uint64_t seed=0xa62ec4b3da751809ULL;
};
struct FirstBitReference {
    bool reference_probability_available=false;
    // These never become true in this isolated FIRST-BIT component.
    bool confidence_available=false,draft_probability_available=false;
    double correct=0,wrong=0,correct_low=0,correct_high=1;
    double threshold=0;
    std::uint64_t original_threshold_trials=0,first_raw_sample=0,observed_raw_end_sample=0;
    std::size_t trials=0,original_jobs=0,selected_jobs=0,retained_clock_rates=0;
    std::size_t raw_samples=0,projected_samples=0,filter_delay=0,owned_bytes=0;
    std::uint64_t filter_multiply_add_bound=0,template_sample_bound=0;
    // Original under-cap requests retain their generated direct execution.
    // The optional raw cache counts one-time generation and repeated score
    // work separately; it never reduces any statistical trial charge.
    bool prepared_template_cache=false;
    std::size_t cached_observation_bytes=0,prepared_tile_jobs=0;
    std::uint64_t uncached_template_sample_bound=0,direct_complex_product_bound=0;
    std::string limit;
};
struct FiniteObservation {
    std::vector<std::complex<double>> values;
    std::vector<double> energy_prefix;
    std::uint64_t first_raw_sample=0,observed_raw_end_sample=0,first_output_sample=0;
};

inline constexpr std::size_t maximum_raw_samples=32768;
inline constexpr std::size_t maximum_projected_samples=8192;
inline constexpr std::size_t maximum_selected_jobs=257;
inline constexpr std::size_t maximum_original_jobs=32768;
inline constexpr std::size_t maximum_owned_bytes=8*1024*1024;
inline constexpr std::uint64_t maximum_filter_multiply_adds=1ULL<<30;
inline constexpr std::uint64_t maximum_template_samples=1ULL<<26;
inline constexpr std::uint64_t maximum_cached_score_products=1ULL<<30;

// Exact production PayloadFilter::push, including binary32 output rounding.
// chunk is a diagnostic partition only; it does not change the finite window.
// The supplied PCM must cover exactly all required observed output and halo.
FiniteObservation condition_observations(const FirstBitRequest&,std::span<const float> raw,
                                        std::size_t chunk=4096,std::stop_token={});

// Existing synchronous production score operator, with the SAME observations
// supplying each selected cell's dot and complete received-energy denominator.
// Returned cells are in selected ORIGINAL order, job-major then start-major.
std::vector<modem::detail::FftSearchScore> score_observations(
    const FirstBitRequest&,const FiniteObservation&,std::stop_token={});

// Focused parity entry: the SAME production FP64 direct contraction, using
// the explicit raw prepared domain and an allocation-free primary row builder.
std::vector<modem::detail::FftSearchScore> score_observations_prepared(
    const FirstBitRequest&,const FiniteObservation&,std::stop_token={});

// Paired common-noise draws for both normal bit alternatives. No source bit,
// arrival, helper metadata or known time is given to runtime reception here.
FirstBitReference conditioned_first_bit_reference(const FirstBitRequest&,std::stop_token={});

} // namespace datapump::simulation::detail::conditioned_private
