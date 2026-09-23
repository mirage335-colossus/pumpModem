#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace datapump::simulation::detail {

// The estimator needs binary64 common random numbers, not a generic engine
// range calculation. libstdc++'s generic generate_canonical computes long-
// double logarithms of the engine range; Clang can leave them in each draw,
// making AArch64's software binary128 path dominate otherwise bounded work.
// This is the same mt19937_64 / 2^64 mapping, including rounding below one.
inline double probability_uniform_word(std::uint64_t word) {
    // Exact powers of two keep the multiply in binary64 and avoid per-draw
    // ldexp/nextafter library calls on compilers that do not fold them.
    return std::min(static_cast<double>(word)*0x1p-64,0x1.fffffffffffffp-1);
}
inline double probability_uniform(std::mt19937_64& generator) {
    return probability_uniform_word(generator());
}

// Freeze the polar order used by the calibrated GNU estimator. Both receiver
// models retain the same seeded stream and cached companion draw across calls.
// This is statistical model input; waveform/channel noise and secrets do not
// use this helper.
class ProbabilityNormal {
    double saved_=0;
    bool has_saved_=false;
public:
    double operator()(std::mt19937_64& generator) {
        if(has_saved_) {has_saved_=false;return saved_;}
        double x,y,radius;
        do {
            x=2*probability_uniform(generator)-1;
            y=2*probability_uniform(generator)-1;
            radius=x*x+y*y;
        } while(radius>1||radius==0);
        const auto scale=std::sqrt(-2*std::log(radius)/radius);
        saved_=x*scale;has_saved_=true;
        return y*scale;
    }
};

} // namespace datapump::simulation::detail
