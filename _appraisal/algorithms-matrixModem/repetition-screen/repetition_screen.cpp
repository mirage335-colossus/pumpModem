#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::size_t replicates = 250000;
constexpr double target_false_alarm = 0.01;

struct Complex {
    double real;
    double imag;
};

struct Scores {
    double energy;
    double phase_aided_cross;
    double phase_invariant_product;
    double covariance_llr;
};

class GaussianSource {
public:
    explicit GaussianSource(std::uint64_t seed) : engine_(seed), normal_(0.0, 1.0) {}

    Complex sample() {
        constexpr double scale = 0.7071067811865475244;
        return {normal_(engine_) * scale, normal_(engine_) * scale};
    }

private:
    std::mt19937_64 engine_;
    std::normal_distribution<double> normal_;
};

Scores score(Complex first, Complex second, double rho, double gamma) {
    const double first_power = first.real * first.real + first.imag * first.imag;
    const double second_power = second.real * second.real + second.imag * second.imag;
    const double cross_real = first.real * second.real + first.imag * second.imag;
    const double cross_imag = first.imag * second.real - first.real * second.imag;

    const double plus_power = 0.5 * (first_power + second_power + 2.0 * cross_real);
    const double minus_power = 0.5 * (first_power + second_power - 2.0 * cross_real);
    const double plus_eigenvalue = rho * (1.0 + gamma);
    const double minus_eigenvalue = rho * (1.0 - gamma);
    const double llr = plus_eigenvalue / (1.0 + plus_eigenvalue) * plus_power
                     + minus_eigenvalue / (1.0 + minus_eigenvalue) * minus_power
                     - std::log1p(plus_eigenvalue) - std::log1p(minus_eigenvalue);

    return {first_power + second_power,
            cross_real,
            std::hypot(cross_real, cross_imag),
            llr};
}

Scores noise_only(GaussianSource& random, double rho, double gamma) {
    return score(random.sample(), random.sample(), rho, gamma);
}

Scores signal_present(GaussianSource& random, double rho, double gamma) {
    const Complex first_fade = random.sample();
    const Complex independent_fade = random.sample();
    const double residual = std::sqrt(std::max(0.0, 1.0 - gamma * gamma));
    const Complex second_fade{gamma * first_fade.real + residual * independent_fade.real,
                              gamma * first_fade.imag + residual * independent_fade.imag};
    const double amplitude = std::sqrt(rho);
    const Complex first_noise = random.sample();
    const Complex second_noise = random.sample();
    return score({first_noise.real + amplitude * first_fade.real,
                  first_noise.imag + amplitude * first_fade.imag},
                 {second_noise.real + amplitude * second_fade.real,
                  second_noise.imag + amplitude * second_fade.imag}, rho, gamma);
}

std::array<double, 4> as_array(const Scores& scores) {
    return {scores.energy, scores.phase_aided_cross,
            scores.phase_invariant_product, scores.covariance_llr};
}

struct Interval {
    double low;
    double high;
};

Interval wilson(std::size_t successes, std::size_t trials) {
    constexpr double z = 1.959963984540054;
    const double n = static_cast<double>(trials);
    const double p = static_cast<double>(successes) / n;
    const double denominator = 1.0 + z * z / n;
    const double center = (p + z * z / (2.0 * n)) / denominator;
    const double half_width = z * std::sqrt(p * (1.0 - p) / n + z * z / (4.0 * n * n)) / denominator;
    return {std::max(0.0, center - half_width), std::min(1.0, center + half_width)};
}

double upper_quantile(std::vector<double>& values, double upper_tail_probability) {
    const std::size_t index = static_cast<std::size_t>(
        std::floor((1.0 - upper_tail_probability) * static_cast<double>(values.size())));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

void check_noise_moments() {
    GaussianSource random(0x4d61747269784d6fULL);
    constexpr std::size_t count = 200000;
    std::array<double, 3> sum{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto values = as_array(noise_only(random, 0.16, 0.5));
        sum[0] += values[0];
        sum[1] += values[1];
        sum[2] += values[2];
    }
    const double mean_energy = sum[0] / static_cast<double>(count);
    const double mean_cross = sum[1] / static_cast<double>(count);
    const double mean_product = sum[2] / static_cast<double>(count);
    if (std::abs(mean_energy - 2.0) > 0.025 || std::abs(mean_cross) > 0.01
        || std::abs(mean_product - 0.7853981633974483) > 0.012) {
        throw std::runtime_error("noise moments failed analytic CN(0,1) checks");
    }
    std::cerr << "noise moment checks passed: E[energy]=" << mean_energy
              << " E[real cross]=" << mean_cross
              << " E[absolute cross]=" << mean_product << '\n';
}

std::size_t count_above(const std::vector<Scores>& values, std::size_t statistic, double threshold) {
    std::size_t count = 0;
    for (const auto& item : values) {
        if (as_array(item)[statistic] > threshold) ++count;
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string output_path = argc > 1 ? argv[1] : "repetition-screen.csv";
        check_noise_moments();

        constexpr std::array<double, 4> rho_values{0.016, 0.16, 1.0, 4.0};
        constexpr std::array<double, 3> gamma_values{0.0, 0.5, 1.0};
        constexpr std::array<const char*, 4> statistic_names{
            "two_look_energy", "phase_aided_real_cross",
            "phase_invariant_abs_cross", "matched_known_covariance_llr"};
        std::ofstream output(output_path);
        if (!output) throw std::runtime_error("could not open output CSV");
        output << "rho_per_look,gamma,statistic,threshold,heldout_false_alarm,false_alarm_95_low,"
                  "false_alarm_95_high,detection_probability,detection_95_low,detection_95_high,"
                  "train_null_trials,heldout_null_trials,signal_trials\n";
        output << std::setprecision(9);

        for (std::size_t rho_index = 0; rho_index < rho_values.size(); ++rho_index) {
            const double rho = rho_values[rho_index];
            for (std::size_t gamma_index = 0; gamma_index < gamma_values.size(); ++gamma_index) {
                const double gamma = gamma_values[gamma_index];
                const std::uint64_t base_seed = 0x4d61747269784d6fULL
                    + static_cast<std::uint64_t>(rho_index * 101 + gamma_index * 1009);
                GaussianSource train_random(base_seed + 11);
                GaussianSource holdout_random(base_seed + 23);
                GaussianSource signal_random(base_seed + 37);
                std::array<std::vector<double>, 4> null_scores;
                for (auto& scores : null_scores) scores.reserve(replicates);
                std::vector<Scores> holdout;
                std::vector<Scores> detected;
                holdout.reserve(replicates);
                detected.reserve(replicates);

                for (std::size_t index = 0; index < replicates; ++index) {
                    const auto training = as_array(noise_only(train_random, rho, gamma));
                    for (std::size_t statistic = 0; statistic < training.size(); ++statistic)
                        null_scores[statistic].push_back(training[statistic]);
                    holdout.push_back(noise_only(holdout_random, rho, gamma));
                    detected.push_back(signal_present(signal_random, rho, gamma));
                }

                std::array<double, 4> thresholds{};
                for (std::size_t statistic = 0; statistic < thresholds.size(); ++statistic)
                    thresholds[statistic] = upper_quantile(null_scores[statistic], target_false_alarm);

                for (std::size_t statistic = 0; statistic < thresholds.size(); ++statistic) {
                    const auto false_alarms = count_above(holdout, statistic, thresholds[statistic]);
                    const auto true_detections = count_above(detected, statistic, thresholds[statistic]);
                    const auto false_interval = wilson(false_alarms, holdout.size());
                    const auto detection_interval = wilson(true_detections, detected.size());
                    const double false_rate = static_cast<double>(false_alarms) / holdout.size();
                    const double detection_rate = static_cast<double>(true_detections) / detected.size();
                    output << rho << ',' << gamma << ',' << statistic_names[statistic] << ','
                           << thresholds[statistic] << ',' << false_rate << ','
                           << false_interval.low << ',' << false_interval.high << ','
                           << detection_rate << ',' << detection_interval.low << ','
                           << detection_interval.high << ',' << replicates << ','
                           << holdout.size() << ',' << detected.size() << '\n';
                    if (std::abs(false_rate - target_false_alarm) > 0.002)
                        throw std::runtime_error("held-out false-alarm rate missed its 0.2 percentage-point check");
                }
                std::cerr << "completed rho=" << rho << " gamma=" << gamma << '\n';
            }
        }
        std::cout << "wrote " << output_path << " with independent train-null, holdout-null, and signal streams\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "repetition screen failed: " << error.what() << '\n';
        return 1;
    }
}
