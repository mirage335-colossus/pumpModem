#pragma once
#include "datapump/simulation_estimate.hpp"
#include "datapump/pattern_search.hpp"
#include "receiver_probability.hpp"
#include <span>
namespace datapump::simulation::detail {
// Estimator-only bounded local Gaussian field. Complete receiver calibration,
// not this API, determines whether its result may be called an RX estimate.
struct EmeProbabilitySettings {
    std::size_t trials=4096;
    unsigned timing_radius=8,carrier_radius=4,source_subdivisions=75;
    double frequency_offset_hz=0,clock_error_ppm=0;
    bool operator==(const EmeProbabilitySettings&)const=default;
};
struct EmeCellScores {
    std::array<double,2> combined{},coherent{};
};
struct EmeScoreGrid {
    std::size_t length=0,starts=0;
    std::vector<modem::PatternFrequencyRateHypothesis> hypotheses;
    std::vector<EmeCellScores> cells; // original job, then ascending start
};
EmeScoreGrid eme_score_grid(const modem::Config&,std::span<const std::complex<double>>,
    unsigned timing_radius,unsigned carrier_radius,std::stop_token = {});
struct EmeSourceProjection {
    std::vector<std::complex<double>> observations;
    double mean_energy=0;
};
EmeSourceProjection eme_source_projection(const modem::Config&,unsigned bit,double fractional_bin,
    double phase,unsigned timing_radius,unsigned source_subdivisions,
    double frequency_offset_hz=0,double clock_error_ppm=0,std::stop_token = {});
struct EmeProbabilityResult {
    ReceiverProbability probability;
    double full_bank_threshold=0;
    std::size_t timing_boundary_admissions=0,carrier_boundary_admissions=0;
    std::size_t local_jobs=0,local_starts=0;
    std::vector<std::uint8_t> trial_categories; // 0 miss, 1 correct, 2 wrong
    std::vector<std::int16_t> winning_start,winning_carrier;
};
EmeProbabilityResult eme_receiver_probability(const transfer::Options&,ReceiverWorkMode,
    double symbol_snr_db,double diffusion_degrees,EmeProbabilitySettings = {},std::stop_token = {});
}
