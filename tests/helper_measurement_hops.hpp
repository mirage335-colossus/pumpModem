#pragma once

// Deterministic RF-command fixtures for standalone Helper simulations. These
// describe schedule geometry only; they do not certify a radio, its emissions,
// power, antenna, or national authorization. No oscillator truth is involved.
#include "datapump/fhss_schedule.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace helper_measurement_hops {
// The optional public test-key namespace is injective over 64-bit seeds and
// disjoint from every legacy key: its first two bytes match, while consecutive
// bytes of any legacy key differ. The modem still derives its cryptographic
// FHSS keystream from this deterministic, non-secret fixture key.
inline datapump::Bytes fixture_key(std::uint64_t seed, bool expanded = false) {
    datapump::Bytes bytes(32);
    if (expanded) {
        constexpr std::array<std::uint8_t, 24> domain{
            'H','H','-','H','E','L','P','E','R','-','F','H','S','S','-','T','E','S','T','-','V','3','!','!'};
        std::copy(domain.begin(), domain.end(), bytes.begin());
        for (unsigned i = 0; i < 8; ++i)
            bytes[domain.size()+i] = static_cast<std::uint8_t>(seed >> (8U*i));
        return bytes;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(i + seed * 37U);
    return bytes;
}
struct Profile {
    std::vector<double> centers_hz;
    std::vector<unsigned> order;
    double dwell_seconds = .4;

    double frequency(std::size_t hop) const {
        if (order.empty()) throw std::invalid_argument("empty Helper hop order");
        return centers_hz.at(order.at(hop));
    }
};

// Use the modem's keyed, unbiased permutation generator. This is a FINITE
// simulation schedule, so exhausting it is an error rather than recycling it.
// In FCC mode each draw is unbiased among all channels not yet used in the
// current 100-hop block and not used in the previous 74 dwells. Every block
// therefore uses each channel once, and each recurrence is at least 75 dwells
// apart. At .4 s per dwell, any 30 s window contains at most .4 s per channel.
// Per-block random draws use the repository's FHSS cryptographic keystream and
// rejection sampling; a public deterministic test key is used only in fixtures.
inline Profile make_profile(unsigned channels, double first_hz, double spacing_hz,
                            bool fcc_window = false, std::uint64_t seed = 0,
                            unsigned blocks = 64, bool expanded_key = false) {
    if (channels < 2 || channels > datapump::modem::FhssPlan::capacity)
        throw std::invalid_argument("invalid Helper hop channel count");
    if (!blocks || (fcc_window && channels != 100))
        throw std::invalid_argument("invalid Helper hop schedule geometry");
    auto plan = std::make_shared<datapump::modem::FhssPlan>();
    plan->channels = channels;
    plan->dwell_seconds = .4;
    Profile result;
    result.centers_hz.reserve(channels);
    result.order.reserve(static_cast<std::size_t>(channels) * blocks);
    for (unsigned i = 0; i < channels; ++i) {
        plan->shifts[i] = first_hz + spacing_hz * i;
        result.centers_hz.push_back(plan->shifts[i]);
    }
    const datapump::Crypto key(fixture_key(seed, expanded_key));
    if (!fcc_window) {
        datapump::modem::FhssSchedule schedule(plan, key, 1720000000);
        for (std::size_t i = 0; i < static_cast<std::size_t>(channels) * blocks; ++i)
            result.order.push_back(schedule.channel(i));
        return result;
    }
    std::vector<std::size_t> last(channels, 0);
    std::vector<bool> seen(channels, false);
    for (unsigned block = 0; block < blocks; ++block) {
        std::fill(seen.begin(), seen.end(), false);
        auto bytes = key.stream(datapump::StreamPurpose::Fhss, 1720000000,
            static_cast<std::uint64_t>(block) * datapump::modem::FhssSchedule::cycle_bytes,
            datapump::modem::FhssSchedule::cycle_bytes, datapump::StreamDomain::FhssCycleV2);
        std::size_t used = 0;
        for (unsigned slot = 0; slot < channels; ++slot) {
            std::vector<unsigned> eligible;
            for (unsigned channel = 0; channel < channels; ++channel)
                if (!seen[channel] && (last[channel] == 0 || result.order.size() + 1 - last[channel] >= 75))
                    eligible.push_back(channel);
            if (eligible.empty()) throw std::runtime_error("no eligible FCC fixture hop");
            const std::uint64_t bound = eligible.size();
            const std::uint64_t limit = (std::uint64_t{1} << 32) / bound * bound;
            std::uint32_t word;
            do {
                if (used + 4 > bytes.size()) throw std::runtime_error("FCC fixture random draw budget exhausted");
                word = 0;
                for (unsigned j = 0; j < 4; ++j) word = (word << 8) | bytes[used++];
            } while (word >= limit);
            const auto channel = eligible[word % bound];
            result.order.push_back(channel);
            seen[channel] = true;
            last[channel] = result.order.size(); // one-based, zero means unseen
        }
        std::fill(bytes.begin(), bytes.end(), 0);
    }
    return result;
}

// 50 channels at 50 kHz: 28.775--31.225 MHz, centered on 30 MHz.
// This is a simulation geometry and does not assert HF band authorization.
inline Profile hf50(std::uint64_t seed = 0, bool expanded_key = false) { return make_profile(50, 28.775e6, 50e3, false, seed, 64, expanded_key); }

// FCC 5725--5850 MHz scheduling example: 100 channels, 1.2 MHz spacing;
// 5728--5846.8 MHz centers leave room for a <=1 MHz occupied channel width.
inline Profile fcc100(std::uint64_t seed = 0, bool expanded_key = false) { return make_profile(100, 5728e6, 1.2e6, true, seed, 64, expanded_key); }

// EU 5725--5875 MHz SRD scheduling example (EN 300 440): 20 channels,
// 7.2 MHz spacing, 5731.6--5868.4 MHz centers and 136.8 MHz center span.
inline Profile eu20(std::uint64_t seed = 0, bool expanded_key = false) { return make_profile(20, 5731.6e6, 7.2e6, false, seed, 64, expanded_key); }
} // namespace helper_measurement_hops
