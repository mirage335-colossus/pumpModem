#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace matrix_rx {

constexpr int sample_rate = 3200;
constexpr int symbol_samples = 256;
constexpr int fft_length = 1024;
constexpr int timing_step = 4;
constexpr int tone_half_separation = 6;
constexpr double noise_false_alarm = 0.001;
constexpr float history_gamma = 0.35f;

struct Shape {
    int bits;
    int words;
    int starts;
    int frequencies;
    int rows() const { return starts * frequencies; }
    int data_samples() const { return 2 * bits * symbol_samples; }
    int opportunity_samples() const { return data_samples() + (starts - 1) * timing_step; }
};

constexpr float likelihood_snr(const Shape& shape) {
    return shape.bits >= 2048 ? 0.20f : 0.80f;
}

struct Sample { float re, im; };
struct Scene {
    std::vector<Sample> input;
    int word;
    int start;
    int frequency;
};

enum class Channel { flat, moderate, disturbed, holdout, noise, interferer, wrong_word };

// The named fixed codebook contains exactly shape.words messages, with an
// independently generated sign for each word and feature. +1 selects high tone.
int code_sign(int word, int bit);
int center_bin(int bit);
int tone_bin(const Shape& shape, int bit, int frequency, int sign);
Scene render(const Shape& shape, Channel channel, double cn0_dbhz, std::uint64_t seed);
const char* name(Channel channel);

struct Evidence {
    std::vector<float> contrast; // column-major rows by bits, q_high - q_low
    std::vector<float> common;   // per-row sum(q_high + q_low)
    std::vector<float> power;    // per-row sum of all four |z|^2 per bit
};

// Independent computation at one row, used to check the GPU FFT and feature map.
Evidence reference_row(const Shape& shape, const std::vector<Sample>& input,
                       int row, bool history);
void validate_shape(const Shape& shape);
void check_fixture();

} // namespace matrix_rx
