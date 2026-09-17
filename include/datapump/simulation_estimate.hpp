#pragma once
#include "datapump/transfer.hpp"
#include <span>
#include <string_view>

namespace datapump::simulation {
inline constexpr std::string_view reference_cpu = "Intel Core i9-13900H";
inline constexpr std::string_view reference_gpu = "RTX 4090 Laptop GPU";

struct Estimate {
    // Engineering estimates, not measurements or calibrated probabilities.
    // Success means the entire supplied draft survives reception/correction.
    double success_probability = 0;
    double cpu_seconds = 0;
    double gpu_seconds = 0;
    double simulated_seconds = 0;
    double modeled_symbol_snr_db = 0;
    std::size_t receiver_profiles = 0;
    bool profile_matches = false;
    bool confidence_available = false;
    // The production receiver currently has no GPU execution backend.
    bool gpu_hypothetical = true;
};

// Uses an already encoded draft estimate: it never changes framing, encodes
// another message, samples PCM, measures elapsed time or inspects host hardware.
// options.modem is the actual selected transmit geometry. An empty profile
// span means that same profile; otherwise supply the independently configured
// receive bank. receive_key_count scales bank cost, not success probability.
// raw_bits covers both exact binary drafts and the fixed short dictionary.
// Details, assumed reference throughput and limitations:
// docs/simulation-estimates.md.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options, bool raw_bits,
                  const modem::ChannelConfig& channel,
                  std::span<const modem::Config> receive_profiles = {},
                  std::size_t receive_key_count = 1);
}
