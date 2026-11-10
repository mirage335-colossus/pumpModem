#include "datapump/triad_receiver.hpp"

#include "triad_receiver_coarse.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_receiver_helper.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace datapump::triad::v3 {
namespace {
using namespace receiver_detail;
using Clock = std::chrono::steady_clock;
constexpr std::size_t receipts_bytes = 64 * 1024;
constexpr std::size_t maximum_fine_coordinates = 64;
constexpr std::size_t maximum_helper_intervals = 64;

void require(bool valid, const char* reason) {
    if (!valid) throw Error(reason);
}

std::size_t validate(const ReceiverConfig& c, const ReceiverSlotPlan& s, unsigned workers) {
    require(workers > 0, "Robust slot requires an explicit positive worker share");
    require(std::isfinite(c.sample_rate_hz) && c.sample_rate_hz >= 1 && c.sample_rate_hz <= 120e6 &&
        std::floor(c.sample_rate_hz) == c.sample_rate_hz, "invalid Robust receiver sample rate");
    require(std::isfinite(c.noise_variance) && c.noise_variance > 0 &&
        std::isfinite(c.data_basis_gain) && c.data_basis_gain > 0, "invalid Robust receiver gain/noise model");
    const std::array nonnegative{
        c.data_if_hz, c.frequency_bound_hz, c.clock_bound_ppm, c.endpoint_clock_bound_ppm,
        c.spread_delay_seconds, c.random_doppler_width_hz, c.delay_motion_doppler_bound_hz,
        c.delay_excursion_seconds, c.capture_tail_bound_seconds, c.relative_phase_diffusion_rad_sqrt_s,
        c.selective_modulation_rate_hz, c.selective_slow_depth, c.selective_fast_depth,
        c.source_spectral_skirt_allowance_hz};
    require(std::all_of(nonnegative.begin(), nonnegative.end(), [](double x) { return x >= 0 && std::isfinite(x); }) &&
        std::isfinite(c.spread_doppler_hz) && c.data_if_hz <= 120e6 && c.clock_bound_ppm < 1e6 &&
        c.endpoint_clock_bound_ppm < 1e6, "invalid Robust public channel bounds");
    if (c.helper_enabled) {
        const std::array frequencies{c.helper.low_frequency_hz, c.helper.frequency_hz,
                                    c.helper.auxiliary_frequency_hz};
        require(std::all_of(frequencies.begin(), frequencies.end(),
            [](double x) { return x > 0 && std::isfinite(x); }), "invalid Robust Helper placement");
    }
    require(s.first_sample < s.stop_sample && s.stop_sample - s.first_sample <= SIZE_MAX &&
        s.first_sample <= s.quiet_first_sample && s.quiet_first_sample <= s.quiet_stop_sample &&
        s.quiet_stop_sample <= s.stop_sample, "invalid Robust public receiver window");
    require(s.format.epoch >= 6 && s.format.epoch <= UINT32_MAX - 6, "invalid Robust waveform epoch");
    require(std::isfinite(s.arrival_lower_seconds) && std::isfinite(s.arrival_upper_seconds) &&
        s.arrival_lower_seconds <= s.arrival_upper_seconds && std::isfinite(s.window_capture_lead_seconds) &&
        std::isfinite(s.source_start_seconds) && s.source_start_seconds >= 0,
        "invalid Robust public arrival calendar");
    if (c.helper_enabled) {
        require(std::isfinite(s.helper_begin_seconds) && std::isfinite(s.helper_end_seconds) &&
            0 <= s.helper_begin_seconds && s.helper_begin_seconds <= s.source_start_seconds &&
            s.source_start_seconds < s.helper_end_seconds, "invalid Robust public Helper calendar");
    }
    (void)geometry(s.format);
    return static_cast<std::size_t>(s.stop_sample - s.first_sample);
}

struct StageWorkspace {
    std::size_t input, helper, coarse, fine, total;
};

StageWorkspace plan_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    const auto rows = validate(config, slot, workers);
    const auto input = checked_multiply(rows, sizeof(Complex) + sizeof(std::uint8_t));
    const auto helper = helper_workspace(config, slot);
    const auto coarse = coarse_workspace(config, slot, workers);
    const auto fine = fine_workspace(config, slot, workers);
    const auto total = checked_add(checked_add(input, receipts_bytes), std::max({helper, coarse, fine}));
    return {input, helper, coarse, fine, total};
}

double round_even(double value) {
    const auto floor = std::floor(value), fraction = value - floor;
    return floor + (fraction > .5 || (fraction == .5 && std::abs(std::fmod(floor, 2)) == 1.));
}

FineCoordinate snap(FineCoordinate coordinate, const AdmissionGrid& grid) {
    const auto epoch = coordinate.epoch_offset_seconds;
    require(std::find(receiver_epoch_offsets.begin(), receiver_epoch_offsets.end(), epoch) !=
        receiver_epoch_offsets.end() && std::isfinite(coordinate.arrival_seconds) &&
        std::isfinite(coordinate.frequency_hz) && std::isfinite(coordinate.clock_ppm),
        "Robust coarse proposal is outside its declared public grid");
    if (grid.arrival_intervals) {
        const auto step = (grid.upper - grid.lower) / grid.arrival_intervals;
        const auto index = std::clamp(round_even((coordinate.arrival_seconds - grid.lower) / step),
                                      0., grid.arrival_intervals);
        coordinate.arrival_seconds = grid.lower + index * step;
    } else {
        coordinate.arrival_seconds = grid.lower;
    }
    const auto frequency_index = std::clamp(round_even(coordinate.frequency_hz / grid.frequency_step),
                                            -grid.frequency_radius, grid.frequency_radius);
    coordinate.frequency_hz = frequency_index * grid.frequency_step;
    if (grid.clock_bound) {
        const auto index = std::clamp(round_even((coordinate.clock_ppm + grid.clock_bound) * 8 /
                                                (2 * grid.clock_bound)), 0., 8.);
        coordinate.clock_ppm = -grid.clock_bound + index * (2 * grid.clock_bound / 8);
    } else {
        coordinate.clock_ppm = 0;
    }
    return coordinate;
}

void classify(ReceiverSlotScore& score, const AdmissionGrid& grid) {
    score.operational_threshold = receiver_operational_threshold();
    const double log_mass = std::log(static_cast<double>(score.fine_candidates)) - grid.log_count();
    score.selected_prior_mass = std::exp(log_mass);
    const auto best = std::max(score.bit_evidence[0], score.bit_evidence[1]);
    const auto gap = std::abs(score.bit_evidence[0] - score.bit_evidence[1]);
    score.operational_log_e = best + std::log1p(std::exp(-gap)) - std::numbers::ln2 + log_mass;
    require(std::isfinite(score.operational_log_e), "Robust DATA evidence is nonfinite");
    score.operational_passes_conditional_bound = score.complete && score.zero_h0_verified &&
        score.operational_log_e >= score.operational_threshold;
    if (score.operational_passes_conditional_bound && score.all_valid && score.bit_evidence[0] != score.bit_evidence[1]) {
        score.decision = SlotDecision::data;
        score.bit = static_cast<std::uint8_t>(score.bit_evidence[1] > score.bit_evidence[0]);
        score.reason = "fixed_affine_data_evidence";
    } else if (score.complete && score.zero_h0_verified && score.noise_statistics_valid &&
               score.quiet_energy <= score.quiet_threshold) {
        score.decision = SlotDecision::quiet;
        score.reason = "scored_public_quiet_aperture";
    } else {
        score.reason = score.complete ? "unresolved_bit_or_null" : "incomplete_fine_observation";
    }
}

double elapsed(Clock::time_point began) {
    return std::chrono::duration<double>(Clock::now() - began).count();
}
} // namespace

std::size_t receiver_slot_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    return plan_workspace(config, slot, workers).total;
}

ReceiverSlotScore score_receiver_slot(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                     const Crypto& key, std::span<const Complex> iq,
                                     std::span<const std::uint8_t> valid, unsigned workers,
                                     std::size_t memory_limit_bytes, std::stop_token stop) {
    check_cancelled(stop);
    const auto workspace = plan_workspace(config, slot, workers);
    require(iq.size() == slot.stop_sample - slot.first_sample &&
        (valid.empty() || valid.size() == iq.size()), "Robust input differs from the full public observation");
    if (workspace.total > memory_limit_bytes)
        throw ReceiverWorkspaceError("Robust receiver slot exceeds its admitted memory lease");

    ReceiverSlotScore score;
    score.index = slot.index;
    score.first_sample = slot.first_sample;
    score.stop_sample = slot.stop_sample;
    score.quiet_first_sample = slot.quiet_first_sample;
    score.quiet_stop_sample = slot.quiet_stop_sample;
    score.sample_rate_hz = config.sample_rate_hz;
    score.workspace_peak_bytes = workspace.total;
    score.all_valid = true;
    for (std::size_t i = 0; i < iq.size(); ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        const auto mask = valid.empty() ? 1 : valid[i];
        require(mask <= 1 && std::isfinite(iq[i].real()) && std::isfinite(iq[i].imag()),
                "Robust input must be finite with a binary validity mask");
        score.all_valid &= mask != 0;
    }

    auto began = Clock::now();
    auto helper = score_helper(config, slot, iq, valid, workspace.helper, stop);
    score.helper_seconds = elapsed(began);
    score.helper_status = std::move(helper.reason);
    require(helper.intervals.size() <= maximum_helper_intervals &&
        helper.narrowed == !helper.intervals.empty(), "invalid Robust Helper frequency assistance");
    for (const auto& interval : helper.intervals)
        require(std::isfinite(interval.lower_hz) && std::isfinite(interval.upper_hz) &&
            -config.frequency_bound_hz <= interval.lower_hz && interval.lower_hz <= interval.upper_hz &&
            interval.upper_hz <= config.frequency_bound_hz, "Robust Helper interval exceeds the public frequency prior");
    // The canonical full coarse screen requires a wholly valid observation.
    // Known erasures cannot become observed silence or complete a message.
    if (!score.all_valid) {
        score.reason = "coarse_erasures_unsupported";
        return score;
    }

    began = Clock::now();
    const std::array full_prior{FrequencyInterval{-config.frequency_bound_hz, config.frequency_bound_hz}};
    const auto intervals = helper.narrowed ? std::span<const FrequencyInterval>(helper.intervals) :
        std::span<const FrequencyInterval>(full_prior);
    auto coarse = score_coarse(config, slot, key, iq, intervals, workers, workspace.coarse, stop);
    score.coarse_seconds = elapsed(began);
    score.coarse_workspaces = coarse.workspaces;
    score.coarse_frequency_points = coarse.frequency_points;
    score.coarse_arrival_points = coarse.arrival_points;
    if (coarse.coordinates.empty()) {
        score.reason = std::move(coarse.reason);
        return score;
    }
    require(coarse.coordinates.size() <= maximum_fine_coordinates, "Robust coarse refinements exceed the fine bank cap");
    require(std::isfinite(coarse.maximum_frequency_step_hz) && coarse.maximum_frequency_step_hz > 0,
            "invalid Robust coarse frequency resolution");
    const AdmissionGrid grid(slot.arrival_lower_seconds, slot.arrival_upper_seconds,
        config.frequency_bound_hz, config.clock_bound_ppm, std::min(.01, coarse.maximum_frequency_step_hz));
    std::vector<FineCoordinate> coordinates;
    coordinates.reserve(coarse.coordinates.size());
    for (const auto& proposed : coarse.coordinates) {
        const auto coordinate = snap(proposed, grid);
        if (std::find(coordinates.begin(), coordinates.end(), coordinate) == coordinates.end())
            coordinates.push_back(coordinate);
    }
    score.fine_candidates = coordinates.size();

    began = Clock::now();
    score_fine(config, slot, key, iq, coordinates, coarse.projected_sample_rate_hz,
               coarse.clock_gap_ppm, workers, score, stop);
    classify(score, grid);
    score.fine_seconds = elapsed(began);
    check_cancelled(stop);
    return score;
}

} // namespace datapump::triad::v3
