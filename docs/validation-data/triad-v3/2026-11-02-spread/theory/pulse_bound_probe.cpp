// Probe the actual shared lookup table; this is not a replacement pulse model.
#include "datapump/pattern_pulse.hpp"
#include "triad_gaussian_fading.hpp"
#include <iomanip>
#include <iostream>
int main() {
    using namespace datapump::modem;
    constexpr unsigned resolution = 256; // Python verifies the header contract.
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
        datapump::triad::v3::detail::GaussianRayFading fading(.001, dt, 123);
        std::cout << "{\"width_2sigma_hz\":0.001,\"sample_interval_seconds\":" << dt
                  << ",\"coefficients\":[";
        bool coefficient_separator = false;
        for (double h : fading.coefficients()) {
            if (coefficient_separator) std::cout << ',';
            coefficient_separator = true;
            std::cout << h;
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
