#pragma once
#include "datapump/crypto.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Experimental waveform and receiver only. This namespace does not implement
// ordinary Robust framing, streaming progress, completion or GUI integration.
namespace datapump::triad {
using Complex = std::complex<double>;
enum class Receiver { differential, segmented_complex, amplitude_only };
enum class Arithmetic { automatic, fp32, fp64 };
struct Format {
    double aggregate_rate_hz = 1200;
    std::uint32_t sample_rate = 6400;
    unsigned dwells_per_frame = 20;
    unsigned bits_per_frame = 4;
    unsigned added_symbols = 1;
    bool added_interleave = false;
    bool fhss = false;
    unsigned burst_wait_frames = 0;
    double dwell_seconds = .4;
    double ramp_seconds = .02;
    double margin_seconds = .02;
    bool encrypt = true;
    std::uint64_t epoch = 1793404800;
};
struct Geometry {
    double chip_rate_hz = 0, carrier_spacing_hz = 0, occupied_bandwidth_hz = 0;
    double frame_seconds = 0;
    unsigned cells_per_dwell = 0;
    std::size_t frame_samples = 0;
};
struct Cell {
    double center_seconds = 0;
    std::size_t bit = 0;
    unsigned frame = 0, dwell = 0, slot = 0, logical_cell = 0, added_index = 0;
};
struct Waveform {
    std::vector<Complex> samples;
    std::vector<Cell> cells;
    std::size_t source_bits = 0;
    std::size_t source_end_sample = 0;
    double mean_power = 0, peak_power = 0;
};
struct SearchPrior {
    double frequency_half_width_hz = 100;
    // Zero selects chip_rate/8. The complete declared interval is searched.
    double frequency_step_hz = 0;
    std::vector<double> timing_offsets_seconds{0};
    std::vector<double> clock_offsets_ppm{0};
    std::vector<std::int64_t> epoch_offsets_seconds{0};
    unsigned segment_cells = 8;
    unsigned threads = 2;
    std::size_t memory_limit_bytes = 256ULL * 1024 * 1024;
    std::size_t maximum_hypotheses = 100000;
    Arithmetic arithmetic = Arithmetic::automatic;
    // Empirical score/margin gates, not a calibrated false-alarm probability.
    double minimum_score = .03;
    double minimum_margin = .005;
};
struct Reception {
    std::vector<std::uint8_t> bits;
    std::vector<double> bit_scores, bit_margins;
    bool accepted = false;
    bool complete_observation = false;
    double score = 0, margin = 0;
    double frequency_hz = 0, timing_seconds = 0, clock_ppm = 0;
    std::int64_t epoch_offset_seconds = 0;
    std::size_t hypotheses = 0, frequency_banks = 0, fft_count = 0;
    std::size_t projection_values = 0, working_bytes = 0;
    unsigned threads_used = 0;
    double wall_seconds = 0, cpu_seconds = 0;
    Arithmetic arithmetic_used = Arithmetic::fp64;
};
Geometry geometry(const Format&);
// Exact arbitrary bit count; padding cells in the final locally configured
// frame use a separate keyed pattern and are not returned as source bits.
std::vector<Cell> layout(const Format&, std::size_t bit_count, const Crypto&);
Waveform transmit(std::span<const std::uint8_t> bits, const Format&, const Crypto&);
// bit_count is a LOCAL study configuration, never inferred from received data.
// No oscillator/channel truth or transmitted bits enter the receiver.
Reception receive(std::span<const Complex> samples, std::size_t bit_count,
                  const Format&, const Crypto&, const SearchPrior&, Receiver);
Crypto experiment_key(const Crypto&, const char* purpose);
const char* receiver_name(Receiver);
const char* arithmetic_name(Arithmetic);
} // namespace datapump::triad
