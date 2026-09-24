#pragma once
#include "datapump/transfer.hpp"
#include <span>
#include <string>
#include <string_view>

namespace datapump::simulation {
inline constexpr std::string_view reference_cpu = "Intel Core i9-13900H";
inline constexpr std::string_view reference_gpu = "RTX 4090 Laptop GPU";

struct Estimate {
    // Engineering estimates, not measurements or calibrated probabilities.
    // Idealized success conditional on completing receiver computation: the
    // supplied draft survives reception/correction. This is not a deadline or
    // an empirical success rate for the implementation.
    // success_probability is meaningful only when confidence_available is true.
    double success_probability = 0;
    // First raw bit in the same geometry, independent of draft length/FEC.
    // Can remain supported when complete-draft tracking is outside coverage.
    double one_bit_success_probability = 0;
    bool one_bit_confidence_available = false;
    // Monte Carlo sampling uncertainty, conditional on the channel/model.
    // These intervals do not include model error or physical-link uncertainty.
    std::size_t probability_trials = 0;
    double one_bit_probability_low = 0, one_bit_probability_high = 1;
    double success_probability_low = 0, success_probability_high = 1;
    bool probability_interval_available = false;
    bool probability_search_approximation = false;
    std::size_t probability_carrier_candidates = 0;
    std::string probability_model_limit;
    double cpu_seconds = 0;
    // Receive projection, search and tracking only; excludes synthetic channel
    // generation. Divide by simulated_seconds for a rough real-time workload,
    // not a measured CPU utilization or a guarantee about per-bit latency.
    double receiver_cpu_seconds = 0;
    double gpu_seconds = 0;
    // Included in both totals: serial whole-symbol continuation for one
    // established signal stream per matching FFT profile, through observed
    // absence. Competing/noise tracks and reacquisition are not upper-bounded.
    // Unrelated key/epoch banks add acquisition work, not established streams.
    double tracking_seconds = 0;
    double tracking_symbol_windows = 0;
    double simulated_seconds = 0;
    double modeled_symbol_snr_db = 0;
    // Nonnegative loss from random phase drift during one complete coherent
    // symbol. Pattern reversals do not reset the carrier's phase history.
    double phase_coherence_loss_db = 0;
    // Eligible geometry can additionally combine four section fits. These
    // fields describe that policy, not a live receiver's allocation: compact
    // banks retain the coherent-only path if the added state cannot fit.
    // True only when eligible geometry uses the limited coherent reference
    // instead of the combined statistical model. That reference retains the
    // detector-choice penalty even on compact fallback; it is not a lower bound.
    bool coherent_reference_only = false;
    // Fixed matched-statistic trials modeled eligible branches, correlated noise,
    // phase paths and competing bits. This does not run the adaptive receiver.
    bool drift_model_available = false;
    // Same modeled scenario using only the original coherent score, without
    // the extra detector-choice penalty. Requires drift_model_available and
    // confidence_available; it is a comparison, not a measured receiver run.
    double coherent_success_probability = 0;
    std::size_t drift_sections = 1;
    double drift_section_seconds = 0;
    // Phase loss within the longest implemented section, diagnostic only.
    // The combined model samples phase paths rather than substituting this
    // scalar loss into the original coherent probability formula.
    double section_phase_coherence_loss_db = 0;
    // Receiver-local differential geometry. Zero means ineligible. Counts
    // describe eligible geometry, not an actual live allocation; the model
    // also checks the workspace allowance before crediting this detector.
    std::uint64_t differential_windows = 0;
    double differential_window_seconds = 0;
    bool differential_model_available = false;
    double differential_added_detection_probability = 0;
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
// compute_probability=false returns search/workspace and compute estimates
// without probability trials; confidence_available then remains false.
// differential_window_seconds must match the receiver-local PatternSearch
// setting. Zero disables that detector; the default matches production use.
// probability_trials controls only Monte Carlo precision, not receiver policy.
// Details, assumed reference throughput and limitations:
// docs/simulation-estimates.md.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options, bool raw_bits,
                  const modem::ChannelConfig& channel,
                  std::span<const modem::Config> receive_profiles = {},
                  std::size_t receive_key_count = 1,
                  bool compute_probability = true,
                  double differential_window_seconds = 100,
                  std::size_t probability_trials = 4096);
}
