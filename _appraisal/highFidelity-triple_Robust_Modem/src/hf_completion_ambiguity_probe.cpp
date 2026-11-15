#include "appraisal/hf_native_chain.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "triad_probe_json.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef APPRAISAL_SOURCE_SHA
#define APPRAISAL_SOURCE_SHA "unavailable"
#endif
#ifndef DATAPUMP_TRIAD_PROBE_SOURCE_SHA
#define DATAPUMP_TRIAD_PROBE_SOURCE_SHA "unavailable"
#endif
#ifndef DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT
#define DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT "unavailable"
#endif

namespace {
using namespace appraisal;
using namespace datapump;
using namespace datapump::triad::v3;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using datapump::triad::probe::write_json;
using Clock = std::chrono::steady_clock;

constexpr double rf_hz = 10e6;
constexpr double noise_variance = .0001;
constexpr std::uint64_t receiver_noise_seed = 0x4d6f64656d303031ULL;
constexpr std::uint64_t propagation_seed = 0x4d6f64656d303032ULL;
constexpr std::size_t receiver_chunk = 4096;
constexpr std::uint64_t maximum_samples = 2'000'000;
constexpr std::uint64_t source_preroll_samples = 480;
constexpr std::uint64_t public_filter_halo_samples = 8;
constexpr double maximum_wall_seconds = 120.;

struct Assertion {
    std::string name;
    bool passed = false;
};
std::vector<Assertion> assertions;

constexpr std::string_view state_name(StreamReceiverState state) {
    switch (state) {
    case StreamReceiverState::receiving: return "receiving";
    case StreamReceiverState::input_ended: return "input_ended";
    case StreamReceiverState::cancelled: return "cancelled";
    case StreamReceiverState::resource_limit: return "resource_limit";
    case StreamReceiverState::failed: return "failed";
    }
    return "unknown";
}

void check(bool condition, std::string name) {
    assertions.push_back({std::move(name), condition});
}

Json bits_json(std::span<const std::uint8_t> bits) {
    Json::Array result;
    result.reserve(bits.size());
    for (const auto bit : bits) result.emplace_back(bit);
    return result;
}

std::uint64_t relative_sample(const PublicProfile& profile, HelperBurstCoordinate coordinate) {
    return profile.calendar().distance(profile.origin(), coordinate);
}

std::string sha256_iq(std::span<const Complex> samples) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("could not initialize I/Q input SHA-256");
    std::array<unsigned char, 16> row{};
    const auto encode = [](double value, unsigned char* out) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<unsigned char>(bits >> (8 * i));
    };
    for (const auto sample : samples) {
        encode(sample.real(), row.data());
        encode(sample.imag(), row.data() + 8);
        if (EVP_DigestUpdate(digest.get(), row.data(), row.size()) != 1)
            throw std::runtime_error("could not update I/Q input SHA-256");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned size = 0;
    if (EVP_DigestFinal_ex(digest.get(), bytes.data(), &size) != 1 || size != 32)
        throw std::runtime_error("could not finalize I/Q input SHA-256");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned i = 0; i < size; ++i) {
        result.push_back(hex[bytes[i] >> 4]);
        result.push_back(hex[bytes[i] & 15]);
    }
    return result;
}

bool bit_exact_iq(Complex a, Complex b) {
    return std::bit_cast<std::uint64_t>(a.real()) == std::bit_cast<std::uint64_t>(b.real()) &&
           std::bit_cast<std::uint64_t>(a.imag()) == std::bit_cast<std::uint64_t>(b.imag());
}

HfNativeChainConfig chain_config(const EmittedSource& source, std::uint64_t capture_stop) {
    HfNativeChainConfig config;
    auto& endpoint = config.endpoints;
    endpoint.effects.sample_rate = source.format.sample_rate;
    endpoint.effects.rf_hz = rf_hz;
    endpoint.effects.occupied_bandwidth_hz = source.geometry.occupied_bandwidth_hz;
    endpoint.effects.nominal_conversion_if_hz = 500.;
    endpoint.effects.noise_enabled = false;
    endpoint.complex_noise_variance = 0.;
    endpoint.noise_seed = receiver_noise_seed;
    endpoint.path_seed = propagation_seed;
    endpoint.maximum_chunk_samples = 4096;
    config.final_receiver_noise_variance = noise_variance;
    config.propagation_seed = propagation_seed;
    config.propagation.rf_hz = rf_hz;
    config.tx_horizon_samples = source.geometry.source_samples + 128;
    config.propagation_horizon_samples = config.tx_horizon_samples + 384;
    config.capture_horizon_samples = std::max(capture_stop,
        config.propagation_horizon_samples + 512);
    config.tx_band = {source.minimum_frequency_hz - 16., source.maximum_frequency_hz + 16.};
    config.propagation_band = config.tx_band;
    config.bridge_chunk_samples = 1024;
    config.maximum_workspace_bytes = 64u * 1024u * 1024u;
    config.propagation.fade.ramp_seconds = 0.;
    config.propagation.fade.voltage_floor = 0.;
    config.propagation.fade.first_seconds = -1.;
    config.propagation.fade.stop_seconds = -1.;
    return config;
}

std::vector<Complex> read_through(HfNativeChain& chain, std::uint64_t stop) {
    if (stop > chain.total_samples() || stop > maximum_samples)
        throw std::runtime_error("paired witness exceeded its declared capture/sample budget");
    std::vector<Complex> result(static_cast<std::size_t>(stop));
    std::vector<std::uint8_t> validity(receiver_chunk);
    std::size_t at = 0;
    while (at < result.size()) {
        const auto count = std::min(receiver_chunk, result.size() - at);
        const auto read = chain.read(std::span(result).subspan(at, count), std::span(validity).first(count));
        if (read != count || !std::all_of(validity.begin(), validity.begin() + count,
                                           [](std::uint8_t value) { return value == 1; }))
            throw std::runtime_error("paired HF channel ended or erased samples before the requested calendar");
        at += read;
    }
    return result;
}

struct NativePrefixResult {
    std::vector<CompletedMessage> messages;
    std::vector<StreamSlotReceipt> slots;
    StreamReceiverReport report;
};

NativePrefixResult receive_prefix(const PublicProfile& profile, const Crypto& key,
                                  const ReceiverConfig& config, std::span<const Complex> input,
                                  std::string_view label) {
    StreamReceiverOptions options;
    options.maximum_input_chunk_samples = receiver_chunk;
    const NativeMessageProfile message{FecMode::off, true, 16, key};
    CompleteStreamReceiver receiver(profile, config, key, options, message);
    NativePrefixResult result;
    const auto drain = [&] {
        result.report = receiver.report();
        if (result.report.state != StreamReceiverState::receiving || result.report.input_closed)
            throw std::runtime_error(std::string(label) + " receiver " +
                std::string(state_name(result.report.state)) + " before EOF at input sample " +
                std::to_string(result.report.input_stop) + ", next slot " +
                std::to_string(result.report.next_public_slot) + " (" + result.report.reason + ")");
        auto messages = receiver.drain_messages();
        result.messages.insert(result.messages.end(), std::make_move_iterator(messages.begin()),
                               std::make_move_iterator(messages.end()));
        auto slots = receiver.drain_recent_slots();
        result.slots.insert(result.slots.end(), std::make_move_iterator(slots.begin()),
                            std::make_move_iterator(slots.end()));
    };
    std::size_t at = 0;
    while (at < input.size()) {
        const auto count = std::min(receiver_chunk, input.size() - at);
        StreamPushResult pushed;
        try {
            pushed = receiver.push(at, input.subspan(at, count));
        } catch (...) {
            drain(); // Keep the terminal state/reason concealed by a generic push error.
            throw;
        }
        drain();
        if (pushed == StreamPushResult::accepted) {
            at += count;
            continue;
        }
        if (result.report.output_backpressured) continue;
        if (!result.report.active_owners)
            throw std::runtime_error(std::string(label) + " receiver cannot admit its next unchanged chunk");
        (void)receiver.wait_for_slot_completion();
        drain();
    }
    (void)receiver.resume(true);
    drain();
    return result;
}

bool complete_prefix(const NativePrefixResult& result, const PublicProfile& profile,
                     std::span<const std::uint8_t> data, std::uint64_t input_stop) {
    const auto count = data.size() + 2;
    if (result.report.state != StreamReceiverState::receiving || result.report.input_closed ||
        result.report.input_stop != input_stop || result.report.next_public_slot != count ||
        result.report.uncommitted_slots != 0 || result.slots.size() != count) return false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto expected = profile.slot(i);
        const auto& slot = result.slots[i];
        if (slot.score.index != i || slot.pattern_epoch != expected.pattern_epoch ||
            slot.symbol_ordinal != expected.symbol_ordinal || !slot.score.complete || !slot.score.all_valid)
            return false;
        if (i < data.size()) {
            if (slot.score.decision != SlotDecision::data || slot.score.bit != data[i]) return false;
        } else if (slot.score.decision != SlotDecision::quiet || slot.score.bit.has_value()) return false;
    }
    return true;
}

Json prefix_receipt(const NativePrefixResult& result) {
    Json::Array slots;
    for (const auto& slot : result.slots) {
        const auto& row = slot.score;
        const auto decision = row.decision == SlotDecision::data ? "data" :
            row.decision == SlotDecision::quiet ? "quiet" : "ambiguous";
        slots.emplace_back(object({{"index", row.index}, {"pattern_epoch", slot.pattern_epoch},
            {"symbol_ordinal", slot.symbol_ordinal}, {"all_valid", row.all_valid}, {"complete", row.complete},
            {"decision", decision}, {"bit", row.bit ? Json(*row.bit) : Json{}}}));
    }
    const auto& report = result.report;
    return object({{"state", state_name(report.state)}, {"input_closed", report.input_closed},
        {"accepted_input_stop", report.input_stop}, {"next_public_slot", report.next_public_slot},
        {"uncommitted_slots", report.uncommitted_slots}, {"workers", report.resources.workers},
        {"hardware_threads", report.resources.hardware_threads}, {"slots", std::move(slots)},
        {"peak_reserved_bytes", report.peak_reserved_bytes}, {"memory_limit_bytes", report.resources.memory_limit_bytes}});
}

Json assertion_json() {
    Json::Array result;
    for (const auto& item : assertions)
        result.emplace_back(object({{"name", item.name}, {"passed", item.passed}}));
    return result;
}

bool all_passed() {
    return std::all_of(assertions.begin(), assertions.end(), [](const Assertion& item) { return item.passed; });
}

Json failure_receipt(std::string error, double wall_seconds) {
    return object({
        {"probe", "hf_completion_ambiguity"},
        {"status", "failed"},
        {"error", std::move(error)},
        {"assertions", assertion_json()},
        {"wall_seconds", wall_seconds},
        {"source_identity", object({
            {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
            {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
            {"native_source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT}})}
    });
}
} // namespace

int main(int argc, char**) {
    const auto began = Clock::now();
    try {
        if (argc != 1) throw std::invalid_argument("usage: high_fidelity_completion_ambiguity_probe");
        PublicProfileConfig public_config;
        public_config.format.sample_rate = 1920;
        public_config.format.inner_rate_hz = 120;
        public_config.format.added_symbols = 1;
        public_config.format.added_interleave = false;
        public_config.format.bit_seconds = 8.;
        public_config.format.epoch = 1793850000;
        public_config.format.symbol_ordinal = 741;
        public_config.format.encrypt = false;
        public_config.helper_enabled = false;
        public_config.data_if_hz = 500.;
        // Coarse template preparation inspects two rows before the source.
        // Render a common native pre-roll so slot zero really has those rows;
        // the public halo also keeps calendar rounding away from that edge.
        public_config.lead_samples = source_preroll_samples;
        public_config.receiver_filter_halo_samples = public_filter_halo_samples;
        const PublicProfile profile(public_config);
        const auto key = Crypto::amateur();
        const Bytes short_text{' '};
        const Bytes long_text{' ', 'B'};
        const auto short_bits = encode_message_bits(short_text, FecMode::off);
        const auto long_bits = encode_message_bits(long_text, FecMode::off);
        check(short_bits.size() == 3 && std::all_of(short_bits.begin(), short_bits.end(),
            [](std::uint8_t bit) { return bit == 0; }), "canonical_space_code_is_000");
        check(long_bits.size() > short_bits.size() &&
              std::equal(short_bits.begin(), short_bits.end(), long_bits.begin()),
              "long_message_shares_canonical_space_code_prefix");

        EmissionOptions short_options;
        short_options.mode = EmissionMode::Message;
        short_options.bits = short_bits;
        short_options.slot_count = short_bits.size();
        EmissionOptions long_options = short_options;
        long_options.bits = long_bits;
        long_options.slot_count = long_bits.size();
        Emission short_emission(profile, key, short_options);
        Emission long_emission(profile, key, long_options);
        const auto short_source = short_emission.source();
        const auto long_source = long_emission.source();

        ReceiverConfig receiver_config;
        receiver_config.sample_rate_hz = public_config.format.sample_rate;
        receiver_config.data_if_hz = public_config.data_if_hz;
        receiver_config.noise_variance = noise_variance;
        receiver_config.data_basis_gain = 1. / std::sqrt(data_power_reference(public_config.format, key).mean_norm);
        const auto resources = resolve_stream_receiver_resources();
        std::size_t maximum_slot_workspace = 0;
        for (std::size_t i = 0; i < short_bits.size() + 2; ++i)
            maximum_slot_workspace = std::max(maximum_slot_workspace, receiver_slot_workspace(
                receiver_config, make_receiver_slot_plan(profile, receiver_config, i), resources.workers));
        check(maximum_slot_workspace > 0 && maximum_slot_workspace < resources.memory_limit_bytes,
              "all_shared_slots_have_valid_geometry_and_bounded_full_worker_workspace_quotes");
        const auto short_complete_slot = make_receiver_slot_plan(profile, receiver_config, short_bits.size() + 1);
        const auto third_tx = profile.transmit_slot(short_bits.size() - 1);
        const auto resume_slot = short_bits.size() + 2;
        const auto later_tx = profile.slot(resume_slot);
        const auto prefix_stop = short_complete_slot.stop_sample;
        const auto outage_first = relative_sample(profile, third_tx.support_end);
        const auto later_first = later_tx.data_support.first;
        const auto later_stop = later_tx.data_support.stop;
        check(prefix_stop > outage_first && later_first > prefix_stop && later_stop > later_first,
              "derived_calendar_has_shared_tail_outage_and_later_data");
        check(resume_slot < long_bits.size(), "long_source_has_transmitted_data_after_short_quiet_calendar");
        check(prefix_stop <= maximum_samples && later_stop <= maximum_samples,
              "calendar_fits_declared_sample_budget");

        // Both physical chains are rendered through the later DATA aperture.
        // The receiver still receives only the unchanged short completion
        // prefix; the extra short-chain samples provide its matched noise
        // baseline for the long source's resumed-signal differential.
        auto short_chain_config = chain_config(short_source, std::max(later_stop, std::uint64_t{1}));
        auto long_chain_config = chain_config(long_source, std::max(later_stop, std::uint64_t{1}));
        const auto fs = static_cast<double>(public_config.format.sample_rate);
        short_chain_config.propagation.fade.first_seconds = outage_first / fs;
        short_chain_config.propagation.fade.stop_seconds = prefix_stop / fs;
        long_chain_config.propagation.fade.first_seconds = outage_first / fs;
        long_chain_config.propagation.fade.stop_seconds = prefix_stop / fs;
        const auto slot3 = make_receiver_slot_plan(profile, receiver_config, short_bits.size());
        const auto slot4 = short_complete_slot;
        check(slot3.quiet_first_sample >= outage_first && slot3.quiet_stop_sample <= prefix_stop &&
              slot4.quiet_first_sample >= outage_first && slot4.quiet_stop_sample <= prefix_stop,
              "zero_floor_outage_covers_both_scored_quiet_apertures");

        HfNativeChain short_chain(short_source, short_chain_config);
        HfNativeChain long_chain(long_source, long_chain_config);
        check(later_stop <= short_chain.total_samples() && later_stop <= long_chain.total_samples(),
              "native_channel_horizons_cover_requested_calendar");
        const auto short_iq = read_through(short_chain, later_stop);
        const auto long_iq = read_through(long_chain, later_stop);
        const auto short_prefix = std::span(short_iq).first(static_cast<std::size_t>(prefix_stop));
        const auto long_prefix = std::span(long_iq).first(static_cast<std::size_t>(prefix_stop));
        const auto short_hash = sha256_iq(short_prefix);
        const auto long_hash = sha256_iq(long_prefix);
        const auto same_iq = std::equal(short_prefix.begin(), short_prefix.end(),
                                        long_prefix.begin(), long_prefix.end(), bit_exact_iq);
        check(same_iq, "receiver_input_complex_iq_prefixes_are_bit_exactly_equal");
        check(short_hash == long_hash, "receiver_input_prefix_sha256_matches");
        long double resumed_difference_energy = 0.;
        for (std::uint64_t i = later_first; i < later_stop; ++i)
            resumed_difference_energy += std::norm(long_iq[static_cast<std::size_t>(i)] -
                                                    short_iq[static_cast<std::size_t>(i)]);
        check(std::isfinite(static_cast<double>(resumed_difference_energy)) && resumed_difference_energy > 1e-12,
              "later_native_iq_contains_resumed_long_source_signal_above_shared_noise");

        const auto short_rx = receive_prefix(profile, key, receiver_config, short_prefix, "short");
        const auto long_rx = receive_prefix(profile, key, receiver_config, long_prefix, "long");
        const auto& short_messages = short_rx.messages;
        const auto& long_messages = long_rx.messages;
        const auto short_observed = complete_prefix(short_rx, profile, short_bits, prefix_stop);
        const auto long_observed = complete_prefix(long_rx, profile, short_bits, prefix_stop);
        check(short_observed, "short_receiver_committed_three_data_and_two_quiet_slots_with_input_open");
        check(long_observed, "long_receiver_committed_three_data_and_two_quiet_slots_with_input_open");
        const auto decoded_as_space = [&](const std::vector<CompletedMessage>& messages) {
            return messages.size() == 1 && messages.front().first_public_slot == 0 &&
                messages.front().stop_public_slot == short_bits.size() + 2 &&
                messages.front().message.state == MessageState::complete &&
                messages.front().message.physical_complete &&
                messages.front().message.decoded_content &&
                messages.front().message.decoded_content->message.data == Bytes{' '};
        };
        const auto short_good = short_observed && decoded_as_space(short_messages);
        const auto long_premature = long_observed && decoded_as_space(long_messages);
        check(short_good, "legitimate_short_space_completes_before_eof");
        check(long_premature, "longer_message_prefix_is_prematurely_accepted_as_space_before_eof");

        check(resources.hardware_threads > 0 && resources.workers == 2 * resources.hardware_threads &&
              short_rx.report.resources.workers == resources.workers && long_rx.report.resources.workers == resources.workers &&
              short_rx.report.resources.hardware_threads == resources.hardware_threads &&
              long_rx.report.resources.hardware_threads == resources.hardware_threads,
              "canonical_receiver_uses_default_full_2H_worker_budget");
        const double wall = std::chrono::duration<double>(Clock::now() - began).count();
        check(wall <= maximum_wall_seconds, "bounded_probe_wall_time_at_or_below_120_seconds");
        const auto passed = all_passed();
        std::cout << std::setprecision(17);
        write_json(std::cout, object({
            {"probe", "hf_completion_ambiguity"},
            {"status", passed ? "passed" : "failed"},
            {"claim_scope", "constructed_zero_floor_outage_identifiability_witness_not_natural_channel_probability"},
            {"fec", "off"},
            {"sample_rate_hz", public_config.format.sample_rate},
            {"bit_seconds", public_config.format.bit_seconds},
            {"data_if_hz", public_config.data_if_hz},
            {"shared_public_profile", true},
            {"short_bits", bits_json(short_bits)},
            {"long_bits", bits_json(long_bits)},
            {"sample_calendar", object({
                {"short_data_bits", short_bits.size()},
                {"emitted_preroll_samples", source_preroll_samples},
                {"public_filter_halo_samples", public_filter_halo_samples},
                {"outage_first_sample_after_shared_third_support", outage_first},
                {"prefix_stop_after_two_quiet_slots", prefix_stop},
                {"later_resumed_slot", resume_slot},
                {"later_data_first_sample", later_first},
                {"later_data_stop_sample", later_stop},
                {"short_complete_slot_first_sample", short_complete_slot.first_sample},
                {"short_complete_slot_quiet_first_sample", short_complete_slot.quiet_first_sample},
                {"short_complete_slot_quiet_stop_sample", short_complete_slot.quiet_stop_sample},
                {"samples_in_prefix", prefix_stop},
                {"samples_in_later_data_window", later_stop - later_first}})},
            {"native_iq_input", object({
                {"encoding", "LE IEEE-754 binary64 real then imaginary per complex sample"},
                {"sha256_short_prefix", short_hash},
                {"sha256_long_prefix", long_hash},
                {"prefixes_exactly_equal", same_iq},
                {"later_differential_signal_energy", static_cast<double>(resumed_difference_energy)},
                {"receiver_noise_variance", noise_variance},
                {"noise_seed", receiver_noise_seed}})},
            {"completion", object({
                {"short_count", short_messages.size()},
                {"short_space_completed_before_eof", short_good},
                {"long_prefix_count", long_messages.size()},
                {"long_prefix_accepted_as_space_before_eof", long_premature},
                {"complete_message_stop_public_slot", short_messages.empty() ? 0 : short_messages.front().stop_public_slot},
                {"finish_input_called", false},
                {"both_receivers_keep_open_until_probe_destruction", !short_rx.report.input_closed &&
                    short_rx.report.state == StreamReceiverState::receiving && !long_rx.report.input_closed &&
                    long_rx.report.state == StreamReceiverState::receiving}})},
            {"short_receiver", prefix_receipt(short_rx)},
            {"long_receiver", prefix_receipt(long_rx)},
            {"maximum_slot_workspace_quote_bytes", maximum_slot_workspace},
            {"default_receiver_workers", resources.workers},
            {"default_receiver_hardware_threads", resources.hardware_threads},
            {"wall_seconds", wall},
            {"assertions", assertion_json()},
            {"source_identity", object({
                {"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
                {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
                {"native_source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT}})}
        }));
        std::cout << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        assertions.push_back({"probe_completed_without_exception", false});
        const double wall = std::chrono::duration<double>(Clock::now() - began).count();
        write_json(std::cout, failure_receipt(error.what(), wall));
        std::cout << '\n';
        return 1;
    }
}
