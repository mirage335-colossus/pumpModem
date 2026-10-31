#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace hm = helper_measurement_hops;
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void check_permutation_and_jumps(const hm::Profile& p, unsigned count,
                                  double first, double spacing, const char* name) {
    require(p.centers_hz.size() == count && p.order.size() >= count * 4,
            "wrong channel count");
    require(p.dwell_seconds == .4, "wrong dwell");
    for (unsigned i = 0; i < count; ++i) {
        require(std::abs(p.centers_hz[i] - (first + i * spacing)) < 1e-5,
                "wrong channel center");
    }
    for (std::size_t block = 0; block < p.order.size() / count; ++block) {
        std::vector<bool> seen(count);
        for (unsigned slot = 0; slot < count; ++slot) {
            const auto channel = p.order[block * count + slot];
            require(channel < count && !seen[channel], "block is not a permutation");
            seen[channel] = true;
        }
        if (block > 0) {
            bool changed = false;
            for (unsigned slot = 0; slot < count; ++slot)
                changed = changed || p.order[block * count + slot] != p.order[(block - 1) * count + slot];
            require(changed, "successive random channel orders are identical");
        }
    }
    double largest = 0;
    unsigned wide = 0;
    const double span = p.centers_hz.back() - p.centers_hz.front();
    // The 90-dwell lead-in must exercise actual wide transitions, not just a
    // channel list containing large values that are never commanded.
    for (std::size_t i = 1; i < 90; ++i) {
        const auto jump = std::abs(p.frequency(i) - p.frequency(i - 1));
        require(jump > 0, "consecutive command did not hop");
        largest = std::max(largest, jump);
        if (jump >= span / 2) ++wide;
    }
    require(largest >= span * .75 && wide > 0, "fixture lacks wide lead-in hops");
    std::cout << name << " channels=" << count << " first_hz=" << first
              << " last_hz=" << p.centers_hz.back() << " spacing_hz=" << spacing
              << " maximum_first90_jump_hz=" << largest << " wide_jumps=" << wide << '\n';
}

// Check the complete finite calendar's recurrence gap, then compute exact
// overlap in windows beginning on and between dwell boundaries over several
// blocks. The gap establishes the same airtime bound for every later window.
void check_fcc_sliding_occupancy(const hm::Profile& p) {
    const double period = p.dwell_seconds;
    double max_occupancy = 0;
    const std::size_t count = p.centers_hz.size();
    std::vector<std::size_t> last(count, p.order.size());
    for (std::size_t i = 0; i < p.order.size(); ++i) {
        const auto channel = p.order[i];
        if (last[channel] != p.order.size())
            require(i - last[channel] >= 75, "FCC channel reused within last 74 dwells");
        last[channel] = i;
    }
    for (std::size_t start = 0; start < 3 * count; ++start) {
        for (double offset : {0., .031, .2, .399}) {
            const double begin = start * period + offset, end = begin + 30.;
            std::vector<double> occupied(count);
            const auto first = static_cast<std::size_t>(std::floor(begin / period));
            const auto last = static_cast<std::size_t>(std::ceil(end / period));
            for (std::size_t dwell = first; dwell < last; ++dwell) {
                const double overlap = std::max(0., std::min(end, (dwell + 1) * period) -
                                                     std::max(begin, dwell * period));
                occupied[p.order.at(dwell)] += overlap;
            }
            for (double duration : occupied) {
                max_occupancy = std::max(max_occupancy, duration);
                require(duration <= .4 + 1e-10, "FCC fixture exceeds sliding occupancy");
            }
        }
    }
    require(p.centers_hz.front() - .5e6 >= 5725e6 &&
            p.centers_hz.back() + .5e6 <= 5850e6, "FCC width/edge geometry");
    require(p.centers_hz[1] - p.centers_hz[0] >= 1e6,
            "FCC spacing insufficient for assumed 1 MHz width");
    std::cout << "FCC 30s sliding maximum channel airtime=" << max_occupancy << " s\n";
}

void check_eu_return(const hm::Profile& p) {
    require(p.centers_hz.front() >= 5725e6 && p.centers_hz.back() <= 5875e6,
            "EU channel centers outside assumed band");
    require(p.centers_hz.back() - p.centers_hz.front() > .9 * 150e6,
            "EU center span not greater than 90% of band");
    require(p.dwell_seconds <= 1., "EU dwell exceeds illustrative limit");
    const auto count = p.centers_hz.size();
    const double maximum_return = 4 * p.dwell_seconds * count;
    std::size_t longest_return = 0;
    for (unsigned channel = 0; channel < count; ++channel) {
        std::size_t previous = p.order.size();
        for (std::size_t hop = 0; hop < p.order.size(); ++hop) {
            if (p.order[hop] != channel) continue;
            if (previous != p.order.size()) {
                require((hop - previous) * p.dwell_seconds <= maximum_return,
                        "EU fixture return exceeds illustrative limit");
                longest_return = std::max(longest_return, hop - previous);
            }
            previous = hop;
        }
    }
    std::cout << "EU maximum channel return=" << longest_return * p.dwell_seconds
              << " s, illustrative limit=" << maximum_return << " s\n";
}

// Retain a direct characterization of the production calendar. It uses an
// independently keyed permutation for each cycle, unlike the constrained FCC
// fixture, which randomizes each hop among channels eligible under its window.
// An observed discrepancy is printed, never interpreted as radio certification.
void report_production_cycle_seam(const hm::Profile& p) {
    auto plan = std::make_shared<datapump::modem::FhssPlan>();
    plan->channels = static_cast<unsigned>(p.centers_hz.size());
    plan->dwell_seconds = p.dwell_seconds;
    for (unsigned i = 0; i < plan->channels; ++i) plan->shifts[i] = p.centers_hz[i];
    datapump::Bytes bytes(32);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i);
    datapump::Crypto key(bytes);
    datapump::modem::FhssSchedule schedule(plan, key, 1720000000);
    unsigned violating = 0;
    for (unsigned begin = 0; begin < plan->channels * 3; ++begin) {
        std::vector<bool> seen(plan->channels);
        bool repeat = false;
        for (unsigned j = 0; j < 75; ++j) {
            const auto channel = schedule.channel(begin + j);
            repeat = repeat || seen[channel];
            seen[channel] = true;
        }
        violating += repeat;
    }
    std::cout << "Production independent-permutation 75-slot windows with repeated channel="
              << violating << "/" << plan->channels * 3 << '\n';
}
}

int main() {
    try {
        const auto hf = hm::hf50(), fcc = hm::fcc100(), eu = hm::eu20();
        check_permutation_and_jumps(hf, 50, 28.775e6, 50e3, "HF");
        check_permutation_and_jumps(fcc, 100, 5728e6, 1.2e6, "FCC-profile");
        check_permutation_and_jumps(eu, 20, 5731.6e6, 7.2e6, "EU-profile");
        check_fcc_sliding_occupancy(fcc);
        check_eu_return(eu);
        report_production_cycle_seam(fcc);
        const auto other_fcc = hm::fcc100(1), other_hf = hm::hf50(1), other_eu = hm::eu20(1);
        require(other_fcc.order != fcc.order && other_hf.order != hf.order && other_eu.order != eu.order,
                "key changes did not alter randomized orders");
        check_permutation_and_jumps(other_fcc, 100, 5728e6, 1.2e6, "FCC-second-key");
        check_fcc_sliding_occupancy(other_fcc);
        for (unsigned seed = 2; seed < 10; ++seed)
            check_fcc_sliding_occupancy(hm::fcc100(seed));
        std::cout << "Schedule geometry only; no transmitter/regulatory approval.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Helper hop schedule test: " << e.what() << '\n';
        return 1;
    }
}
