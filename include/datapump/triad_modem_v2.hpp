#pragma once
#include "datapump/triad_modem.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace datapump::triad::v2 {
using Complex = triad::Complex;
using Arithmetic = triad::Arithmetic;
enum class Receiver { coherent, differential, amplitude_only };
struct Format {
    double inner_rate_hz = 36;
    unsigned added_symbols = 10;
    bool added_interleave = true;
    std::uint32_t sample_rate = 960;
    double minimum_bit_energy_seconds = 14.22222222222222;
    unsigned minimum_inner_coefficients = 64;
    unsigned bits_per_frame = 1;
    unsigned minimum_dwells = 20;
    bool fhss = false; // Transmitter envelope only; not physical RF retuning.
    double dwell_seconds = .4, quiet_seconds = 0, ramp_seconds = .02, margin_seconds = .005;
    unsigned burst_wait_frames = 0;
    bool encrypt = true;
    std::uint64_t epoch = 1793404800;
};
struct Geometry {
    double inner_chip_rate_hz = 0, fine_chip_rate_hz = 0;
    double carrier_spacing_hz = 0, occupied_bandwidth_hz = 0;
    double frame_seconds = 0, payload_energy_seconds_per_bit = 0;
    unsigned dwells_per_frame = 0;
    std::size_t inner_coefficients_per_bit = 0, fine_coefficients_per_bit = 0;
    std::size_t fine_cells_per_frame = 0, payload_cells_per_frame = 0;
    std::size_t frame_samples = 0;
};
struct Cell {
    double center_seconds = 0;
    std::size_t fine_index = 0, bit = 0;
    unsigned frame = 0, dwell = 0;
    std::size_t logical_inner = 0;
    unsigned added_index = 0;
};
struct Waveform {
    std::vector<Complex> samples;
    std::vector<Cell> cells;
    std::size_t source_bits = 0, source_end_sample = 0;
    double mean_power = 0, peak_power = 0;
    double payload_energy_seconds_per_bit = 0;
};
struct SearchPrior {
    double frequency_half_width_hz = .31;
    // Zero selects quarter-cycle spacing for the chosen coherent segment.
    double frequency_step_hz = 0;
    double arrival_min_seconds = -6, arrival_max_seconds = 6;
    // Zero selects four samples per fine chip. Search is outward-rounded.
    double timing_step_seconds = 0;
    std::vector<double> clock_offsets_ppm{0};
    std::vector<std::int64_t> epoch_offsets_seconds{-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6};
    // Zero is full-pattern coherence. Otherwise segments may cross dwells.
    double segment_seconds = 1.6;
    bool independent_hop_phase = false;
    unsigned threads = 2;
    Arithmetic arithmetic = Arithmetic::automatic;
    std::size_t memory_limit_bytes = 256ULL * 1024 * 1024;
    std::size_t maximum_hypotheses = 2000000;
    // Exposed empirical gates; not calibrated false-alarm probabilities.
    double minimum_score = .03, minimum_margin = .005;
};
struct Reception {
    std::vector<std::uint8_t> bits;
    std::vector<double> bit_scores, bit_margins;
    bool accepted = false, complete_observation = false;
    // Exhaustive declared grids for the retained, coarsely matched tensor;
    // this does not claim exact raw-waveform likelihood at each fine CFO.
    bool fine_likelihood_exhaustive = true;
    double score = 0, margin = 0;
    double frequency_hz = 0, timing_seconds = 0, clock_ppm = 0;
    std::int64_t epoch_offset_seconds = 0;
    std::size_t hypotheses = 0, coarse_hypotheses = 0, refined_hypotheses = 0;
    std::size_t frequency_banks = 0, fft_count = 0, projection_values = 0;
    std::size_t working_bytes = 0, retained_samples = 0;
    unsigned threads_used = 0;
    double wall_seconds = 0, cpu_seconds = 0;
    Arithmetic arithmetic_used = Arithmetic::fp64;
};
Geometry geometry(const Format&);
std::vector<Cell> layout(const Format&, std::size_t bit_count, const Crypto&);
Waveform transmit(std::span<const std::uint8_t> bits, const Format&, const Crypto&);
// bit_count is a local study configuration, not received framing. The capture
// must include the complete winning frame calendar at its candidate arrival.
Reception receive(std::span<const Complex>, std::size_t bit_count,
                  const Format&, const Crypto&, const SearchPrior&, Receiver);
const char* receiver_name(Receiver);
} // namespace datapump::triad::v2
