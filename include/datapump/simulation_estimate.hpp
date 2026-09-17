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
    // success_probability is meaningful only when confidence_available is true.
    double success_probability = 0;
    double cpu_seconds = 0;
    double gpu_seconds = 0;
    double simulated_seconds = 0;
    double modeled_symbol_snr_db = 0;
    double carrier_offset_hz = 0;
    // Requested search span. If receiver_workspace_supported is false, live
    // reception may use a narrower local fallback; no probability models it.
    double carrier_search_half_width_hz = 0;
    std::size_t receiver_profiles = 0;
    bool profile_matches = false;
    // A matching profile must have enough modeled workspace for the expanded
    // FFT search. This is an approximate per-bank allowance, not an allocation
    // guarantee; reference compute times still describe the requested work.
    bool receiver_workspace_supported = false;
    bool confidence_available = false;
    // The probability approximation requires the simulated carrier to lie
    // within the default receiver's finite frequency-search span. A signal
    // outside it is not proven impossible to receive; its probability is
    // unsupported by this model regardless of the signal strength.
    bool carrier_in_search = false;
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
