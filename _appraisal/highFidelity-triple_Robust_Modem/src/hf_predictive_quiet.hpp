#pragma once

#include "datapump/triad_stream.hpp"
#include "triad_receiver_executor.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace appraisal {

namespace predictive_quiet_detail {

using datapump::triad::v3::Complex;
using datapump::triad::v3::SampleRange;

// A tone amplitude is referred to the centre of the samples that estimated it.
struct ToneMean {
    SampleRange range;
    double frequency_hz = 0;
    Complex amplitude{};
};

struct FrequencyInterval {
    double lower = 0, upper = 0;
};

struct FrequencyWitness {
    ToneMean mean;
    double phase_radius_turns = 0;
    std::array<FrequencyInterval, 2> intervals{};
    std::size_t interval_count = 0;
};

inline constexpr std::size_t frequency_nodes = 9;
inline constexpr double frequency_exclusion_probability = 1e-6;

struct Prediction {
    std::array<ToneMean, frequency_nodes> nodes{};
    double training_mean_magnitude = 0;
};

// Appraisal component probes also exercise the exact same finite geometry.
std::optional<std::vector<FrequencyInterval>> phase_intervals(FrequencyInterval bounds, double phase_turns,
                                                              double radius_turns, long double separation_seconds);
std::optional<double> first_lobe_radius(double sample_rate_hz, std::size_t rows,
                                        double minimum_coherent_fraction);
double line_direction_gain_squared(std::size_t training_rows, std::size_t observation_rows);
SampleRange public_training_gap(const datapump::triad::v3::PublicSlot& preceding,
                                const datapump::triad::v3::PublicSlot& current, double sample_rate_hz);
std::optional<std::vector<FrequencyInterval>> frequency_pair_intervals(const FrequencyWitness& older,
    const FrequencyWitness& recent, double sample_rate_hz);
// The pair solver returns sorted, merged intervals. Intersect those closed
// unions before requiring a unique frequency interval.
std::vector<FrequencyInterval> intersect_frequency_pairs(std::span<const FrequencyInterval>,
                                                         std::span<const FrequencyInterval>);
std::optional<FrequencyInterval> unique_frequency_interval(std::span<const FrequencyInterval>);

} // namespace predictive_quiet_detail

struct HfPredictiveQuietResult {
    using SampleRange = datapump::triad::v3::SampleRange;

    SampleRange frequency_first, frequency_middle, frequency_second, amplitude, validation;
    SampleRange frequency_first_proposal, frequency_first_confirmation;
    SampleRange frequency_middle_proposal, frequency_middle_confirmation;
    SampleRange frequency_second_proposal, frequency_second_confirmation;
    std::size_t frequency_first_gap_slot = 0, frequency_middle_gap_slot = 0, frequency_second_gap_slot = 0;
    std::size_t frequency_witness_count = 0;
    std::size_t frequency_older_pair_intervals = 0, frequency_adjacent_pair_intervals = 0, frequency_joint_intervals = 0;
    std::string_view frequency_baseline = "unavailable";
    std::string_view frequency_interval_rule = "fixed_three_witness_intersection_v1";
    std::string_view training_status = "insufficient_prior_public_gaps";
    std::string_view status = "canonical_decision_retained";
    double frequency_hz = 0, amplitude_magnitude = 0;
    double frequency_lower_hz = 0, frequency_upper_hz = 0;
    double frequency_confidence_exclusion_probability = 0;
    std::size_t frequency_coherent_nodes = 0, frequency_band_nodes = 0;
    double coherent_interpolation_norm = 0, band_interpolation_norm = 0;
    double validation_line_residual = 0, validation_energy = 0, validation_threshold = 0;
    double line_direction_gain_squared = 0, coherent_residual = 0, coherent_threshold = 0;
    double raw_quiet_energy = 0, raw_quiet_threshold = 0;
    double raw_line_frequency_hz = 0, raw_line_log_evidence = 0;
    double legacy_energy = 0, legacy_threshold = 0, expanded_energy = 0, expanded_threshold = 0;
    std::size_t legacy_modes = 0, expanded_modes = 0;
    double extra_seconds = 0;
    bool attempted = false, accepted = false;
};

// Appraisal-only slot policy. A serial producer supplies unique chronological
// input chunks before entering them into the normal native receiver. Each
// prediction uses only earlier public guard intervals. Its entry is ready by
// the start of the corresponding observation and is immutable during scoring.
// A scorer writes only its own outcome entry, which the caller reads after
// ordered commit of that slot.
class HfPredictiveQuiet {
public:
    using Complex = datapump::triad::v3::Complex;
    using Crypto = datapump::Crypto;
    using PublicProfile = datapump::triad::v3::PublicProfile;
    using ReceiverConfig = datapump::triad::v3::ReceiverConfig;
    using ReceiverExecutor = datapump::triad::v3::receiver_detail::ReceiverExecutor;
    using ReceiverSlotPlan = datapump::triad::v3::ReceiverSlotPlan;
    using ReceiverSlotScore = datapump::triad::v3::ReceiverSlotScore;
    using SampleRange = datapump::triad::v3::SampleRange;

    HfPredictiveQuiet(const PublicProfile&, const ReceiverConfig&, std::uint64_t slot_limit);

    std::size_t training_workspace_bytes() const noexcept { return training_bytes_; }
    double training_seconds() const noexcept { return training_seconds_; }
    void observe(std::uint64_t first, std::span<const Complex>, std::span<const std::uint8_t> valid,
                 std::stop_token);
    std::size_t workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers, bool overlap) const;
    ReceiverSlotScore score(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                            std::span<const Complex>, std::span<const std::uint8_t>, unsigned workers,
                            std::size_t lease, std::stop_token, ReceiverExecutor*);
    // Evaluate the quiet check after a complete ordinary native score. This
    // entry point also lets a component test isolate the predictive statistic.
    void consider(const ReceiverConfig&, const ReceiverSlotPlan&, std::span<const Complex>,
                  ReceiverSlotScore&, std::stop_token);
    const HfPredictiveQuietResult& result(std::uint64_t index) const;

private:
    struct Entry {
        SampleRange gap;
        std::optional<predictive_quiet_detail::FrequencyWitness> frequency_fit;
        std::optional<predictive_quiet_detail::Prediction> prediction;
        HfPredictiveQuietResult result;
    };

    ReceiverConfig training_config_;
    ReceiverSlotPlan training_slot_;
    std::vector<Entry> entries_;
    std::vector<Complex> gap_samples_;
    std::size_t next_gap_ = 1, training_bytes_ = 0;
    std::uint64_t input_stop_ = 0;
    bool gap_valid_ = true;
    double training_seconds_ = 0;

    void train(std::size_t index, std::stop_token);
};

} // namespace appraisal
