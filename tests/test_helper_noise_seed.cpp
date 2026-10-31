#include "helper_live_probe_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace hp = helper_live_probe;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        hp::Config legacy;
        legacy.expanded_schedule_key = true;
        legacy.seed = 1720000001;
        hp::Config first = legacy;
        first.noise_seed = 1721000001;
        hp::Config second = legacy;
        second.noise_seed = 1721000002;
        hp::Source a(legacy), b(first), replay(first), c(second);
        std::size_t different_noise = 0, different_seeds = 0;
        for (unsigned dwell = 0; dwell < 3; ++dwell) {
            const auto x = a.generate(dwell), y = b.generate(dwell);
            const auto z = replay.generate(dwell), w = c.generate(dwell);
            require(x.shift_hz == y.shift_hz && y.shift_hz == z.shift_hz &&
                        z.shift_hz == w.shift_hz, "schedule changed with noise seed");
            for (std::size_t i = 0; i < x.samples.size(); ++i) {
                require(x.common_phasor[i] == y.common_phasor[i] &&
                            y.common_phasor[i] == z.common_phasor[i] &&
                            z.common_phasor[i] == w.common_phasor[i],
                        "hop or oscillator phase changed with noise seed");
                require(y.clean_probe[i] == z.clean_probe[i] &&
                            y.clean_probe[i] == w.clean_probe[i],
                        "probe changed with noise seed");
                require(y.samples[i] == z.samples[i], "noise seed replay changed");
                different_noise += x.samples[i] != y.samples[i];
                different_seeds += y.samples[i] != w.samples[i];
            }
        }
        require(different_noise > 100 && different_seeds > 100,
                "distinct thermal seeds did not change noise samples");
        // The real probe starts at 38 seconds. Advance both sources on their
        // required sequential calendars to exercise a nonzero clean twin,
        // rather than treating matching zeros during the lead as evidence.
        for (unsigned dwell = 3; dwell < 95; ++dwell) {
            a.generate(dwell);
            b.generate(dwell);
        }
        const auto heldout_legacy = a.generate(95);
        const auto heldout_seeded = b.generate(95);
        std::size_t nonzero_probe = 0;
        for (std::size_t i = 0; i < heldout_legacy.samples.size(); ++i) {
            require(heldout_legacy.common_phasor[i] == heldout_seeded.common_phasor[i],
                    "held-out oscillator phase changed with noise seed");
            require(heldout_legacy.clean_probe[i] == heldout_seeded.clean_probe[i],
                    "held-out probe changed with noise seed");
            nonzero_probe += std::norm(heldout_legacy.clean_probe[i]) > 0;
        }
        require(nonzero_probe > 100, "held-out probe control was not populated");
        std::cout << "helper noise seed replay and phase isolation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "helper noise seed: " << e.what() << '\n';
        return 1;
    }
}
