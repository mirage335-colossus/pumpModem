// Deterministic finite-cell projections and covariance distances; no receiver state.
#include <array>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using Complex = std::complex<double>;
constexpr int samples = 512;
constexpr double rate = 3200.0;
constexpr double tau = 6.2831853071795864769;

Complex tone(double bin, int sample)
{
    return std::polar(1.0, tau * bin * sample / samples);
}

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void close_to(double actual, double expected)
{
    check(std::abs(actual - expected) < 1e-10, "analytic reference mismatch");
}

double db(double power)
{
    return 10.0 * std::log10(power);
}

Complex mean_tone(double bin, int count)
{
    Complex sum{};
    for (int n = 0; n < count; ++n) sum += tone(bin, n);
    return sum / static_cast<double>(count);
}

void projections()
{
    close_to(std::abs(mean_tone(1, samples)), 0);
    close_to(std::abs(mean_tone(0, samples)), 1);
    std::cout << "Tone offset (Hz)   correct bin (dB)  next bin (dB)\n";
    for (const double hz : {1.0, 3.125}) {
        const double bin = hz * samples / rate;
        const double correct = std::norm(mean_tone(bin, samples));
        const double neighbour = std::norm(mean_tone(bin - 1.0, samples));
        const double ratio = std::sin(tau * bin / 2) /
                             (samples * std::sin(tau * bin / (2 * samples)));
        close_to(correct, ratio * ratio);
        if (bin == 0.5) close_to(correct, neighbour);
        std::cout << hz << "  " << db(correct) << "  " << db(neighbour) << '\n';
    }
    constexpr int offset = samples / 16;
    std::cout << "Neighbour amplitude   displaced correct bin (dB)\n";
    for (const int adjacent : {1, 0, -1}) {
        Complex sum{};
        for (int n = -offset; n < samples - offset; ++n) {
            const Complex tx = static_cast<double>(n < 0 ? adjacent : 1) * tone(7, n);
            sum += tx * std::conj(tone(7, n));
        }
        const double power = std::norm(sum / static_cast<double>(samples));
        close_to(power, std::pow((samples - offset + adjacent * offset) /
                                static_cast<double>(samples), 2));
        std::cout << adjacent << "  " << db(power) << '\n';
    }
    std::cout << "|noise correlation|, one lost quarter (adjacent bins)  "
              << std::abs(mean_tone(1, samples * 3 / 4)) << '\n';
    std::cout << "|noise correlation|, 32-sample displacement (same bin)  "
              << (samples - offset) / static_cast<double>(samples) << '\n';
}

// Gains normalized to unit sum of tap powers. Relative phase is chosen at bin 0.5.
void ray_case(const std::string& label, std::array<int, 2> bins, double signed_echo)
{
    constexpr int delay = 4;
    const Complex echo = signed_echo * tone(0.5, delay);
    const double scale = std::sqrt(1.0 + std::norm(echo));
    double received = 0.0, projected = 0.0;
    int cases = 0;
    for (int previous : bins) {
        for (int current : bins) {
            for (int quarter = 0; quarter < 4; ++quarter) {
                const Complex before = std::polar(1.0, tau * quarter / 4);
                Complex sum{};
                for (int n = 0; n < samples; ++n) {
                    const Complex delayed = n < delay ? before * tone(previous, n - delay)
                                                      : tone(current, n - delay);
                    const Complex tx = tone(current, n);
                    const Complex rx = (tx + echo * delayed) / scale;
                    received += std::norm(rx) / samples;
                    sum += rx * std::conj(tx);
                }
                projected += std::norm(sum / static_cast<double>(samples));
                ++cases;
            }
        }
    }
    received /= cases;
    projected /= cases;
    // Overlap has stationary single-tone response; independent boundary phase
    // averages the cross term to zero in the remaining delay samples.
    double interior = 0.0;
    for (int bin : bins)
        interior += std::norm((1.0 + echo * tone(-bin, delay)) / scale) / bins.size();
    close_to(received, (delay + (samples - delay) * interior) / samples);
    check(projected <= received + 1e-12, "projection energy exceeds window energy");
    if (signed_echo == 0) close_to(projected, 1.0);
    std::cout << label << "  " << db(received) << "  " << db(projected) << '\n';
}

void held_tone_cancellation()
{
    constexpr std::array positions{0, 64, 128, 192};
    constexpr std::array quarter_turns{0, 2, 1, 3};
    constexpr int tone_length = 2 * samples, first_delay = 2, last_delay = 6;
    constexpr int signal_length = positions.size() * tone_length;
    const Complex first_gain{0.5, 0.5}, last_gain{0.5, -0.5};
    std::array<Complex, signal_length + last_delay> tx{}, rx{};
    // Zero prehistory. Scatter both rays through the end of the delayed tail.
    for (int n = 0; n < signal_length; ++n) {
        const int t = n / tone_length;
        tx[n] = std::polar(1.0, tau * quarter_turns[t] / 4) *
                tone(positions[t], n % tone_length);
        rx[n + first_delay] += first_gain * tx[n];
        rx[n + last_delay] += last_gain * tx[n];
    }
    close_to(std::abs(rx.back() - last_gain * tx[signal_length - 1]), 0);

    const double error_bound = 2 * (first_delay * std::abs(first_gain) +
                                    last_delay * std::abs(last_gain)) / tone_length;
    std::cout << "Two-ray support  coherent power fraction  mean per-tone power\n";
    for (const int start : {samples, 0}) {
        Complex total{};
        double power = 0;
        for (int t = 0; t < static_cast<int>(positions.size()); ++t) {
            Complex projection{}, direct{};
            for (int n = t * tone_length + start; n < (t + 1) * tone_length; ++n) {
                const Complex reference = std::conj(tx[n]);
                projection += reference * rx[n];
                direct += reference * tx[n];
            }
            const double count = tone_length - start;
            projection /= count;
            close_to(std::abs(direct / count - 1.0), 0);
            const double error = std::abs(projection - (t < 2 ? 1.0 : -1.0));
            if (start == samples) close_to(error, 0);
            else check(error <= error_bound, "boundary error exceeds tap-delay bound");
            total += projection;
            power += std::norm(projection);
        }
        const double coherent = std::norm(total / static_cast<double>(positions.size()));
        check(coherent <= error_bound * error_bound, "four-tone sum exceeds boundary bound");
        std::cout << (start ? "second cell" : "full tone") << "  " << std::scientific
                  << coherent << "  " << power / positions.size() << std::fixed << '\n';
    }
}

// ||R0 - Rk||_F^2/(2 ||R0||_F^2), from the lag sum of the covariance matrices.
double separation(int bin_difference, double width)
{
    double diagonal = samples;
    double cross = samples;
    for (int lag = 1; lag < samples; ++lag) {
        const double frequency_time = tau * width * 0.5 * lag / rate;
        const double weight = 2.0 * (samples - lag) * std::exp(-frequency_time * frequency_time);
        diagonal += weight;
        cross += weight * std::cos(tau * bin_difference * lag / samples);
    }
    return 1.0 - cross / diagonal;
}

double average_separation(int positions, int spacing, double width)
{
    double sum = 0.0;
    for (int d = 1; d < positions; ++d)
        sum += (positions - d) * separation(d * spacing, width);
    return sum / (positions * (positions - 1) / 2.0);
}

int main()
{
    std::cout << std::fixed << std::setprecision(5);
    projections();
    std::cout << "Path  all received (dB)  correct bin (dB)\n";
    ray_case("direct only", {0, 1}, 0.0);
    ray_case("constructive", {0, 1}, 1.0);
    ray_case("destructive", {0, 1}, -1.0);
    ray_case("echo 3 dB down", {0, 1}, -std::pow(10.0, -3.0 / 20.0));
    ray_case("500 Hz separation, same channel", {-40, 40}, -1.0);
    held_tone_cancellation();
    std::cout << "Doppler width  pair(1)  pair(5)  uniform(352,1)  uniform(70,5)\n";
    for (double width : {0.0, 1.0, 10.0, 30.0}) {
        close_to(separation(0, width), 0.0);
        if (width == 0) close_to(separation(1, width), 1.0);
        std::cout << width << "  " << separation(1, width) << "  " << separation(5, width)
                  << "  " << average_separation(352, 1, width)
                  << "  " << average_separation(70, 5, width) << '\n';
    }
}
