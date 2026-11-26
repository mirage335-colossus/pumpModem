// Standalone formulas for the historical f13e3bc9 analytical model.
// Extracted pulse table and finite-FIR coefficients only; no modem or channel
// state is included. Original file hashes and interpretation are in README.md.
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

constexpr double pattern_pulse_rolloff = .25;
constexpr unsigned pattern_pulse_half_span = 8;

inline double pattern_pulse(double chip_offset) {
    constexpr std::size_t resolution = 256;
    constexpr std::size_t count = pattern_pulse_half_span * resolution;
    static const auto table = [] {
        std::array<double, count + 1> values{};
        constexpr auto a = pattern_pulse_rolloff;
        constexpr auto pi = std::numbers::pi;
        for(std::size_t i=0;i<=count;++i) {
            const auto t=static_cast<double>(i)/resolution;
            if(i==0) values[i]=1+a*(4/pi-1);
            else if(t==1/(4*a)) values[i]=a/std::sqrt(2.)*
                ((1+2/pi)*std::sin(pi/(4*a))+(1-2/pi)*std::cos(pi/(4*a)));
            else values[i]=(std::sin(pi*t*(1-a))+4*a*t*std::cos(pi*t*(1+a)))/
                (pi*t*(1-16*a*a*t*t));
        }
        double energy=values.front()*values.front()+values.back()*values.back();
        for(std::size_t i=1;i<count;++i)energy+=2*values[i]*values[i];
        const auto scale=std::sqrt(resolution/energy);
        for(auto& value:values)value*=scale;
        return values;
    }();
    const auto position=std::abs(chip_offset)*resolution;
    if(!(position<=count))return 0;
    const auto index=static_cast<std::size_t>(position);
    if(index==count)return table[count];
    return std::lerp(table[index],table[index+1],position-static_cast<double>(index));
}


std::vector<double> gaussian_coefficients(double width_hz, double dt_seconds) {
    // Same arithmetic/order as the original GaussianRayFading constructor.
    const double scale = std::numbers::pi*width_hz*dt_seconds;
    const auto half = static_cast<std::size_t>(std::ceil(5/scale));
    const auto count = 2*half+1;
    std::vector<double> coefficients(count);
    long double energy = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double x = scale*(static_cast<double>(i)-static_cast<double>(half));
        coefficients[i] = std::exp(-x*x);
        energy += static_cast<long double>(coefficients[i])*coefficients[i];
    }
    const double normalization = 1/std::sqrt(static_cast<double>(energy));
    for (auto& coefficient : coefficients) coefficient *= normalization;
    return coefficients;
}

int main() {
    constexpr unsigned resolution = 256; // Python verifies the frozen table geometry.
    std::cout << std::setprecision(17) << "{\"resolution\":" << resolution
              << ",\"half_span\":" << pattern_pulse_half_span
              << ",\"rolloff\":" << pattern_pulse_rolloff << ",\"knots\":[";
    for (unsigned i = 0; i <= pattern_pulse_half_span*resolution; ++i) {
        if (i) std::cout << ',';
        std::cout << pattern_pulse(static_cast<double>(i)/resolution);
    }
    std::cout << "],\"gaussian_fir\":[";
    bool separator = false;
    for (double dt : {4., 16., 64.}) {
        if (separator) std::cout << ',';
        separator = true;
        const auto coefficients = gaussian_coefficients(.001, dt);
        std::cout << "{\"width_2sigma_hz\":0.001,\"sample_interval_seconds\":" << dt
                  << ",\"coefficients\":[";
        bool coefficient_separator = false;
        for (double h : coefficients) {
            if (coefficient_separator) std::cout << ',';
            coefficient_separator = true;
            std::cout << h;
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
