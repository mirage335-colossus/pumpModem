#pragma once

#include "datapump/triad_helper_burst.hpp"
#include "datapump/triad_modem_v3.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace datapump::triad::v3 {

inline constexpr unsigned helper_lead_seconds = 38;

struct SampleRange {
    std::uint64_t first = 0, stop = 0;
    std::uint64_t size() const { return stop - first; }
};

struct PublicSlot {
    std::uint64_t index = 0, pattern_epoch = 0, symbol_ordinal = 0;
    HelperBurstSlot tx;
    // Global receiver sample coordinates. DATA support can extend outside the
    // observation. A quiet aperture is always within it, and can be empty.
    SampleRange observation, data_support, quiet_aperture;
    std::uint64_t previous_signal_stop = 0, next_signal_first = 0;
};

struct PublicProfileConfig {
    // Use the selected format from plan_shaping(). Geometry and calendar are
    // recomputed from this format, never accepted from received metadata.
    Format format;
    double data_if_hz = 1725, gate_bandwidth_hz = 0;
    helper::Options helper;
    bool helper_enabled = false;
    std::uint32_t wait_bits = 0;
    unsigned random_extra_bits = 0;
    std::optional<std::uint64_t> lead_samples;
    std::uint64_t receiver_tail_samples = 0;
    // Half support of a centered receiver input filter, measured in logical
    // samples. Extend capture and adjacent signal bounds on both sides.
    std::uint64_t receiver_filter_halo_samples = 0;
    double capture_lead_seconds = 0, capture_trail_seconds = 0;
    double arrival_low_seconds = 0, arrival_high_seconds = 0;
    // Endpoint uncertainty is for public window construction. Search clock
    // and CFO bounds can include additional configured margins.
    double audio_pair_ppm = 0, clock_bound_ppm = 0, cfo_bound_hz = 0;
    double spread_delay_ms = 0, spread_doppler_hz = 0;
    std::optional<double> signal_delay_ms; // Defaults to the declared spread bound.
    double delay_excursion_seconds = 0;
    bool selective_signal_tail = false;
};

// Public whole-bit recurrence. An epoch is the configured anchor; neither an
// episode boundary nor the current capture start resets the keyed ordinal.
class PublicProfile {
public:
    // Omit the calendar key for the deterministic public Amateur schedule.
    explicit PublicProfile(PublicProfileConfig, std::optional<Crypto> calendar_key = {});
    const PublicProfileConfig& config() const { return config_; }
    const Geometry& geometry() const { return geometry_; }
    const HelperBurstPlan& calendar() const { return calendar_; }
    HelperBurstCoordinate origin() const { return {config_.format.epoch, 0}; }
    double effective_sample_rate() const { return config_.format.sample_rate; }
    double data_if_hz() const { return config_.data_if_hz; }
    const helper::PlacementBand& placement() const { return placement_; }
    const helper::Plan& helper_plan() const { return helper_plan_; }
    HelperBurstSlot transmit_slot(std::uint64_t index) const;
    std::uint64_t first_slot_not_before(HelperBurstCoordinate earliest_helper) const;
    std::uint64_t last_slot() const;
    PublicSlot slot(std::uint64_t index) const;
    Format basis_format(const PublicSlot&) const;
    std::uint64_t capture_stop(long double source_end_seconds) const;
private:
    std::uint64_t capture_first(long double source_begin_seconds) const;
    std::uint64_t signal_first(long double source_begin_seconds) const;
    std::uint64_t signal_stop(long double source_end_seconds) const;
    PublicProfileConfig config_;
    Geometry geometry_;
    HelperBurstPlan calendar_;
    helper::PlacementBand placement_;
    helper::Plan helper_plan_;
};

struct DataPowerReference {
    double active_seconds = 0, mean_norm = 0;
    unsigned nodes = 0, sampled_tiles = 0;
    double tile_relative_standard_error = 0;
};

// Deterministic fixed-bit reference on the anchor format, shared by the
// transmitter and receiver. It does not use the transmitted bit or channel.
DataPowerReference data_power_reference(const Format&, const Crypto&, std::stop_token = {});

enum class EmissionMode { Message, Noise, HelperOnly, Quiet };

struct EmissionOptions {
    EmissionMode mode = EmissionMode::Message;
    std::uint64_t first_slot = 0;
    std::span<const std::uint8_t> bits;
    // For modes without a draft. Message uses bits.size(). No slot-sized
    // allocation is made for noise, Helper-only, or quiet runs.
    std::uint64_t slot_count = 1;
    // A local playback may begin at Helper's first sample. Simulator fixtures
    // use zero to preserve the configured source observation origin.
    std::uint64_t source_first_sample = 0;
    std::stop_token stop;
};

struct EmissionReport {
    DataPowerReference data_reference;
    helper::Plan helper_plan;
    double reference_data_mean_norm = 0, data_calibration_gain = 0;
    double data_gain = 0, composite_gain = 0, reference_helper_ratio = 0;
    // Sample coordinates relative to the emitted source's first sample.
    std::uint64_t first_data_sample = 0, generated_samples = 0;
    long double data_energy = 0, helper_energy = 0;
};

// Bounded composite I/Q at baseband, relative to the configured DATA IF.
// Exactly one reader owns this stream. The object must outlive the consumer
// of source(), and read() and source() share the same forward cursor. Only the
// source owner may read report() while the stream is active.
class Emission {
public:
    Emission(const PublicProfile&, const Crypto& selected_key, const EmissionOptions&);
    // The isolated-pulse evaluator precedes the whole-bit recurrence. It uses
    // the same source assembler, power calibration, and Helper generator.
    static Emission isolated_fixture(const PublicProfile&, const Crypto& selected_key,
        std::uint8_t bit, bool data_enabled);
    ~Emission();
    Emission(Emission&&) noexcept;
    Emission& operator=(Emission&&) noexcept;
    Emission(const Emission&) = delete;
    Emission& operator=(const Emission&) = delete;
    EmittedSource source() const;
    std::size_t read(std::span<Complex>);
    const EmissionReport& report() const;
private:
    struct Impl;
    explicit Emission(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::triad::v3
