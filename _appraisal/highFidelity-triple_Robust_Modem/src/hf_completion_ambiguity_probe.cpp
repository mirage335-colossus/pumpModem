#include "appraisal/hf_native_chain.hpp"
#include "datapump/execution_resources.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "hf_predictive_quiet.hpp"
#include "hf_protected_reassembly.hpp"
#include "triad_probe_json.hpp"
#include "triad_stream_backend.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
namespace stream_detail = datapump::triad::v3::detail;

constexpr double rf_hz = 10e6;
constexpr double noise_variance = .0001;
constexpr std::uint64_t receiver_noise_seed = 0x4d6f64656d303031ULL;
constexpr std::uint64_t propagation_seed = 0x4d6f64656d303032ULL;
constexpr std::size_t receiver_chunk = 4096;
constexpr std::uint64_t maximum_samples = 2'000'000;
constexpr std::uint64_t source_preroll_samples = 480;
constexpr std::uint64_t public_filter_halo_samples = 8;
constexpr double maximum_wall_seconds = 120.;
// Frozen before either policy runs; neither value is fitted to the capture.
constexpr double continuing_tone_hz = 17.;
constexpr double continuing_tone_amplitude = .3;
constexpr std::uint64_t public_probe_slots = 5;

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

HfNativeChainConfig chain_config(const EmittedSource& source, std::uint64_t capture_stop,
                                 bool predictive_tone) {
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
    if (predictive_tone) {
        config.continuing_rf_interference = true;
        config.propagation_signal_stop_samples = config.propagation_horizon_samples;
        // Actual endpoint clocks and their public bound are zero in this
        // discriminator. The field continues through the final RX lookahead;
        // wanted-support metadata is not supplied to either receiver.
        config.public_endpoint_clock_bound_ppm = 0.;
        config.propagation_horizon_samples = config.capture_horizon_samples + 8;
        config.propagation.noise.tone_hz = continuing_tone_hz;
        config.propagation.noise.tone_amplitude = continuing_tone_amplitude;
    }
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

std::vector<Complex> read_through(HfNativeChain& chain, std::uint64_t stop, std::uint64_t limit = maximum_samples) {
    if (stop > chain.total_samples() || stop > limit)
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
    std::vector<ProtectedReassemblyCandidate> recovered;
    std::optional<ProtectedReassemblyReport> consumer;
    std::vector<StreamSlotReceipt> slots;
    StreamReceiverReport report;
    std::vector<HfPredictiveQuietResult> predictions;
    std::string input_sha256;
    std::size_t training_reserved_bytes = 0;
    std::size_t consumer_and_receipt_reserved_bytes = 0;
    std::uint64_t training_scanned_samples = 0;
    double training_seconds = 0., service_wall_seconds = 0., service_cpu_seconds = 0.;
    bool predictive = false, verified_protected_receipts = false, contiguous_native_event_origins = false;
};

struct PrefixServiceTime {
    NativePrefixResult& result;
    Clock::time_point wall = Clock::now();
    std::clock_t cpu = std::clock();
    ~PrefixServiceTime() {
        result.service_wall_seconds = std::chrono::duration<double>(Clock::now() - wall).count();
        result.service_cpu_seconds = static_cast<double>(std::clock() - cpu) / CLOCKS_PER_SEC;
    }
};

void run_native_prefix(NativePrefixResult& result, const PublicProfile& profile, const Crypto& key,
                       const ReceiverConfig& config, std::span<const Complex> input,
                       std::string_view label, bool predictive, std::uint64_t slot_limit,
                       std::stop_token stop, FecMode fec) {
    result.predictive = predictive;
    result.input_sha256 = sha256_iq(input);
    const PrefixServiceTime service{result};
    StreamReceiverOptions options;
    options.maximum_input_chunk_samples = receiver_chunk;
    options.record_commit_timing = true;
    const NativeMessageProfile message{fec, true, 16, key};
    const bool protected_capture = fec == FecMode::rs20;
    if (protected_capture) {
        const auto codec_limits = message_receiver_options(message);
        const auto short_limits = message_receiver_options(fec);
        // This one sealed, fully valid capture starts at the public origin
        // and contains too few slots to hit any short codec resource limit
        // or to enter the long-form marker path. No other streaming fixture
        // may assume that a recovery snapshot records the recovery history.
        if (slot_limit != 66 || slot_limit >= codec_limits.maximum_slots ||
            slot_limit >= short_limits.maximum_data_bits || slot_limit >= message_marker_bits ||
            input.size() != make_receiver_slot_plan(profile, config, slot_limit - 1).stop_sample)
            throw std::runtime_error("protected overlap requires the 66-slot all-valid bounded public capture");
        options.recent_slot_receipts = slot_limit;
        result.consumer_and_receipt_reserved_bytes = 65'536 + slot_limit * 16'384;
        const auto resources = resolve_stream_receiver_resources(options);
        if (result.consumer_and_receipt_reserved_bytes >= resources.memory_limit_bytes)
            throw std::runtime_error("protected consumer and ordered receipts exhaust the receiver memory limit");
        options.memory_limit_bytes = resources.memory_limit_bytes - result.consumer_and_receipt_reserved_bytes;
        result.messages.reserve(slot_limit);
        result.slots.reserve(slot_limit);
        result.recovered.reserve(slot_limit);
        if (predictive) result.predictions.reserve(slot_limit);
    }
    std::unique_ptr<HfPredictiveQuiet> predictor;
    std::unique_ptr<CompleteStreamReceiver> receiver;
    if (predictive) {
        predictor = std::make_unique<HfPredictiveQuiet>(profile, config, slot_limit);
        result.training_reserved_bytes = predictor->training_workspace_bytes();
        const auto resources = resolve_stream_receiver_resources(options);
        if (result.training_reserved_bytes >= resources.memory_limit_bytes)
            throw std::runtime_error("predictive training exhausts the receiver memory limit");
        options.memory_limit_bytes = resources.memory_limit_bytes - result.training_reserved_bytes;
        // Causal predictions use only preceding public gaps. Train before
        // constructing the native executor: this scan is charged to service,
        // and cannot become an extra numerical worker alongside the full W.
        for (std::size_t at = 0; at < input.size();) {
            const auto count = std::min(receiver_chunk, input.size() - at);
            predictor->observe(at, input.subspan(at, count), {}, stop);
            at += count;
            result.training_scanned_samples = at;
        }
        result.training_seconds = predictor->training_seconds();
        const stream_detail::StreamWorkspace workspace = [&](const auto& c, const auto& slot, unsigned workers) {
            return predictor->workspace(c, slot, workers, false);
        };
        const stream_detail::StreamWorkspace overlap_workspace = [&](const auto& c, const auto& slot, unsigned workers) {
            return predictor->workspace(c, slot, workers, true);
        };
        const stream_detail::StreamExecutorScorer scorer = [&](const auto& c, const auto& slot, const auto& crypto,
                auto rows, auto valid, unsigned workers, std::size_t lease, std::stop_token stop,
                receiver_detail::ReceiverExecutor* executor) {
            return predictor->score(c, slot, crypto, rows, valid, workers, lease, stop, executor);
        };
        receiver.reset(new CompleteStreamReceiver(stream_detail::StreamReceiverFixture::make_with_executor(
            profile, config, key, options, workspace, scorer, message_decoder(message.fec),
            message_receiver_options(message), stream_detail::StreamFootprint{}, overlap_workspace)));
    } else {
        receiver = std::make_unique<CompleteStreamReceiver>(profile, config, key, options, message);
    }
    auto cancel_receiver = [&] { receiver->request_cancel(); };
    std::optional<std::stop_callback<decltype(cancel_receiver)>> interrupt;
    interrupt.emplace(stop, cancel_receiver);
    const auto drain = [&] {
        result.report = receiver->report();
        auto messages = receiver->drain_messages();
        result.messages.insert(result.messages.end(), std::make_move_iterator(messages.begin()),
                               std::make_move_iterator(messages.end()));
        auto slots = receiver->drain_recent_slots();
        if (predictor)
            for (const auto& slot : slots) result.predictions.push_back(predictor->result(slot.score.index));
        result.slots.insert(result.slots.end(), std::make_move_iterator(slots.begin()),
                            std::make_move_iterator(slots.end()));
        if (result.report.state != StreamReceiverState::receiving || result.report.input_closed)
            throw std::runtime_error(std::string(label) + " receiver " +
                std::string(state_name(result.report.state)) + " before EOF at input sample " +
                std::to_string(result.report.input_stop) + ", next slot " +
                std::to_string(result.report.next_public_slot) + " (" + result.report.reason + ")");
        if (protected_capture && result.report.recovering)
            throw std::runtime_error(std::string(label) + " native recovery broke the protected fragment session");
    };
    std::size_t at = 0;
    while (at < input.size()) {
        if (stop.stop_requested()) {
            (void)receiver->cancel();
            drain();
            throw std::runtime_error("predeclared active-fade wall limit reached");
        }
        const auto count = std::min(receiver_chunk, input.size() - at);
        StreamPushResult pushed;
        try {
            pushed = receiver->push(at, input.subspan(at, count));
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
        (void)receiver->wait_for_slot_completion();
        drain();
    }
    do {
        const auto previous = result.report.next_public_slot;
        (void)receiver->resume(true);
        drain();
        if (protected_capture && result.report.next_public_slot == previous &&
            result.report.next_public_slot < slot_limit && !result.report.output_backpressured)
            throw std::runtime_error("native protected capture stopped making ordered commit progress");
    } while (protected_capture && result.report.next_public_slot < slot_limit);
    interrupt.reset();
    receiver.reset();
    predictor.reset();
}

NativePrefixResult receive_prefix(const PublicProfile& profile, const Crypto& key,
                                  const ReceiverConfig& config, std::span<const Complex> input,
                                  std::string_view label, bool predictive = false) {
    NativePrefixResult result;
    run_native_prefix(result, profile, key, config, input, label, predictive, public_probe_slots, {}, FecMode::off);
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

bool ambiguous_prefix(const NativePrefixResult& result, const PublicProfile& profile,
                      std::span<const std::uint8_t> data, std::uint64_t input_stop) {
    if (result.report.state != StreamReceiverState::receiving || result.report.input_closed ||
        result.report.input_stop != input_stop || result.report.next_public_slot != public_probe_slots ||
        result.report.uncommitted_slots != 0 || result.slots.size() != public_probe_slots ||
        !result.messages.empty()) return false;
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        const auto expected = profile.slot(i);
        const auto& slot = result.slots[i];
        if (slot.score.index != i || slot.pattern_epoch != expected.pattern_epoch ||
            slot.symbol_ordinal != expected.symbol_ordinal || !slot.score.complete || !slot.score.all_valid)
            return false;
        if (i < data.size()) {
            if (slot.score.decision != SlotDecision::data || slot.score.bit != data[i]) return false;
        } else if (slot.score.decision != SlotDecision::ambiguous || slot.score.bit.has_value()) return false;
    }
    return true;
}

bool same_data_evidence(const NativePrefixResult& ordinary, const NativePrefixResult& predictive) {
    if (ordinary.slots.size() != predictive.slots.size()) return false;
    for (std::size_t i = 0; i < ordinary.slots.size(); ++i) {
        const auto& a = ordinary.slots[i].score;
        const auto& b = predictive.slots[i].score;
        if (a.operational_bit_evidence != b.operational_bit_evidence ||
            a.operational_log_e != b.operational_log_e || a.operational_threshold != b.operational_threshold ||
            (a.decision == SlotDecision::data && (b.decision != a.decision || b.bit != a.bit))) return false;
    }
    return true;
}

bool verify_protected_receipts(NativePrefixResult& result, const PublicProfile& profile, const ReceiverConfig& config,
                               std::uint64_t slots, std::uint64_t capture_stop) {
    if (result.report.state != StreamReceiverState::receiving || result.report.input_closed ||
        result.report.recovering || result.report.input_stop != capture_stop ||
        result.report.next_public_slot != slots || result.report.uncommitted_slots || result.slots.size() != slots)
        return false;
    for (std::uint64_t i = 0; i < slots; ++i) {
        const auto& receipt = result.slots[i];
        const auto& score = receipt.score;
        const auto plan = make_receiver_slot_plan(profile, config, i);
        if (score.index != i || receipt.pattern_epoch != plan.format.epoch || receipt.symbol_ordinal != plan.format.symbol_ordinal ||
            score.first_sample != plan.first_sample || score.stop_sample != plan.stop_sample ||
            score.quiet_first_sample != plan.quiet_first_sample || score.quiet_stop_sample != plan.quiet_stop_sample ||
            score.sample_rate_hz != config.sample_rate_hz || score.consumed_samples != plan.stop_sample - plan.first_sample ||
            !score.complete || !score.all_valid)
            return false;
    }
    result.verified_protected_receipts = true;
    return true;
}

void reassemble_protected_receipts(NativePrefixResult& result) {
    if (!result.verified_protected_receipts)
        throw std::logic_error("protected consumer requires verified full native slot receipts");
    std::uint64_t previous_end = 0;
    for (const auto& event : result.messages) {
        // A native recovery ends by rearming with a new episode origin. It
        // cannot emit an event across that break. This fixture only tests
        // contiguous episode origins; even a benign idle rearm is excluded.
        if (event.first_public_slot != previous_end || event.stop_public_slot <= event.first_public_slot ||
            event.stop_public_slot > result.report.next_public_slot || !event.message.physical_complete)
            return;
        previous_end = event.stop_public_slot;
    }
    result.contiguous_native_event_origins = true;
    HfProtectedReassembly consumer(FecMode::rs20);
    auto event = result.messages.begin();
    for (const auto& slot : result.slots) {
        const auto cursor = slot.score.index + 1;
        if (event != result.messages.end() && event->stop_public_slot == cursor) {
            if (auto recovered = consumer.consume(*event)) result.recovered.push_back(std::move(*recovered));
            ++event;
        }
        consumer.advance(cursor);
    }
    result.consumer = consumer.report();
}

Json prediction_receipt(const HfPredictiveQuietResult& p) {
    const auto range = [](SampleRange r) { return object({{"first", r.first}, {"stop", r.stop}}); };
    return object({{"training_status", p.training_status}, {"status", p.status},
        {"frequency_first", range(p.frequency_first)}, {"frequency_second", range(p.frequency_second)},
        {"frequency_first_gap_slot", p.frequency_first_gap_slot},
        {"frequency_second_gap_slot", p.frequency_second_gap_slot},
        {"frequency_middle_gap_slot", p.frequency_middle_gap_slot},
        {"frequency_middle", range(p.frequency_middle)},
        {"frequency_middle_proposal", range(p.frequency_middle_proposal)},
        {"frequency_middle_confirmation", range(p.frequency_middle_confirmation)},
        {"frequency_baseline", p.frequency_baseline}, {"frequency_interval_rule", p.frequency_interval_rule},
        {"frequency_witness_count", p.frequency_witness_count},
        {"frequency_older_pair_intervals", p.frequency_older_pair_intervals},
        {"frequency_adjacent_pair_intervals", p.frequency_adjacent_pair_intervals},
        {"frequency_joint_intervals", p.frequency_joint_intervals},
        {"amplitude", range(p.amplitude)}, {"validation", range(p.validation)},
        {"frequency_hz", p.frequency_hz}, {"frequency_lower_hz", p.frequency_lower_hz},
        {"frequency_upper_hz", p.frequency_upper_hz}, {"amplitude_magnitude", p.amplitude_magnitude},
        {"frequency_confidence_exclusion_probability", p.frequency_confidence_exclusion_probability},
        {"validation_energy", p.validation_energy}, {"validation_threshold", p.validation_threshold},
        {"coherent_residual", p.coherent_residual}, {"coherent_threshold", p.coherent_threshold},
        {"line_direction_gain_squared", p.line_direction_gain_squared},
        {"legacy_energy", p.legacy_energy}, {"legacy_threshold", p.legacy_threshold},
        {"expanded_energy", p.expanded_energy}, {"expanded_threshold", p.expanded_threshold},
        {"raw_quiet_energy", p.raw_quiet_energy}, {"raw_quiet_threshold", p.raw_quiet_threshold},
        {"raw_line_log_evidence", p.raw_line_log_evidence}, {"raw_line_frequency_hz", p.raw_line_frequency_hz},
        {"extra_seconds", p.extra_seconds}, {"attempted", p.attempted}, {"accepted", p.accepted}});
}

Json prefix_receipt(const NativePrefixResult& result) {
    Json::Array slots;
    std::size_t data = 0, quiet = 0, ambiguous = 0;
    double slot_helper_seconds = 0., slot_coarse_seconds = 0., slot_fine_seconds = 0.;
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        const auto& slot = result.slots[i];
        const auto& row = slot.score;
        const auto decision = row.decision == SlotDecision::data ? "data" :
            row.decision == SlotDecision::quiet ? "quiet" : "ambiguous";
        data += row.decision == SlotDecision::data;
        quiet += row.decision == SlotDecision::quiet;
        ambiguous += row.decision == SlotDecision::ambiguous;
        slot_helper_seconds += row.helper_seconds;
        slot_coarse_seconds += row.coarse_seconds;
        slot_fine_seconds += row.fine_seconds;
        auto receipt = object({{"index", row.index}, {"pattern_epoch", slot.pattern_epoch},
            {"symbol_ordinal", slot.symbol_ordinal}, {"all_valid", row.all_valid}, {"complete", row.complete},
            {"decision", decision}, {"bit", row.bit ? Json(*row.bit) : Json{}}, {"reason", row.reason},
            {"first_sample", row.first_sample}, {"stop_sample", row.stop_sample},
            {"quiet_first_sample", row.quiet_first_sample}, {"quiet_stop_sample", row.quiet_stop_sample},
            {"operational_bit_evidence", Json::Array{row.operational_bit_evidence[0], row.operational_bit_evidence[1]}},
            {"operational_log_e", row.operational_log_e}, {"operational_threshold", row.operational_threshold},
            {"quiet_energy", row.quiet_energy}, {"quiet_threshold", row.quiet_threshold},
            {"varying_quiet_energy", row.varying_quiet_energy}, {"varying_quiet_threshold", row.varying_quiet_threshold},
            {"varying_quiet_status", row.varying_quiet_status},
            {"varying_line_log_evidence", row.varying_line_log_evidence}, {"workspace_peak_bytes", row.workspace_peak_bytes},
            {"workers", slot.workers}, {"helper_seconds", row.helper_seconds},
            {"coarse_seconds", row.coarse_seconds}, {"fine_seconds", row.fine_seconds},
            {"commit_wall_seconds", slot.commit_timing ? Json(slot.commit_timing->wall_seconds) : Json{}}});
        if (result.predictive) receipt["predictive_quiet"] = prediction_receipt(result.predictions.at(i));
        slots.emplace_back(std::move(receipt));
    }
    Json::Array messages;
    for (const auto& event : result.messages) {
        const auto& m = event.message;
        Json::Array ending_quiet;
        for (auto slot : m.trailing_quiet_slots) ending_quiet.emplace_back(slot);
        messages.emplace_back(object({{"first_public_slot", event.first_public_slot},
            {"stop_public_slot", event.stop_public_slot}, {"state", static_cast<unsigned>(m.state)},
            {"reason", m.reason}, {"physical_complete", m.physical_complete},
            {"decoded_content", m.decoded_content.has_value()},
            {"decoded_bytes", m.decoded_content ? bits_json(m.decoded_content->message.data) : Json{}},
            {"observed_bits", m.observed_bits}, {"missing_slots", m.missing_slots},
            {"received_bits", bits_json(m.received_bits)}, {"trailing_quiet_slots", std::move(ending_quiet)},
            {"message_service_seconds", m.message_service_seconds}}));
    }
    const auto& report = result.report;
    auto receipt = object({{"policy", result.predictive ? "predictive_quiet" : "ordinary_native"},
        {"input_sha256", result.input_sha256}, {"state", state_name(report.state)}, {"input_closed", report.input_closed},
        {"accepted_input_stop", report.input_stop}, {"next_public_slot", report.next_public_slot},
        {"uncommitted_slots", report.uncommitted_slots}, {"workers", report.resources.workers},
        {"hardware_threads", report.resources.hardware_threads}, {"slots", std::move(slots)},
        {"data_slots", data}, {"quiet_slots", quiet}, {"ambiguous_slots", ambiguous},
        {"messages", std::move(messages)}, {"service_wall_seconds", result.service_wall_seconds},
        {"service_cpu_seconds", result.service_cpu_seconds}, {"summed_slot_helper_seconds", slot_helper_seconds},
        {"summed_slot_coarse_seconds", slot_coarse_seconds}, {"summed_slot_fine_seconds", slot_fine_seconds},
        {"training_seconds", result.training_seconds}, {"training_scanned_samples", result.training_scanned_samples},
        {"training_reserved_bytes", result.training_reserved_bytes},
        {"training_before_receiver_executor", result.predictive},
        {"peak_executor_workers", report.peak_executor_workers}, {"peak_running_owners", report.peak_running_owners},
        {"peak_reserved_bytes", report.peak_reserved_bytes}, {"memory_limit_bytes", report.resources.memory_limit_bytes},
        {"capacity_qualification", false}});
    if (result.consumer_and_receipt_reserved_bytes) {
        receipt["protected_consumer_started"] = result.consumer.has_value();
        receipt["protected_native_receipts_verified"] = result.verified_protected_receipts;
        receipt["contiguous_native_event_origins"] = result.contiguous_native_event_origins;
        receipt["retained_consumer_and_receipt_reserved_bytes"] = result.consumer_and_receipt_reserved_bytes;
        receipt["controller_wait_seconds"] = report.controller_wait_seconds ? Json(*report.controller_wait_seconds) : Json{};
        receipt["ready_windows_may_overlap"] = true;
    }
    if (result.consumer) {
        const auto& c = *result.consumer;
        receipt["protected_consumer"] = object({{"events", c.events}, {"seeds", c.seeds}, {"appends", c.appends},
            {"decode_attempts", c.decode_attempts}, {"candidates", c.candidates}, {"malformed_events", c.malformed_events},
            {"expirations", c.expirations}, {"resets", c.resets}, {"last_attempt_positions", c.last_attempt_positions},
            {"last_attempt_erasures", c.last_attempt_erasures}, {"last_gap_positions", c.last_gap_positions},
            {"pending_positions", c.pending_positions}, {"last_action", c.last_action}, {"decode_seconds", c.decode_seconds},
            {"service_seconds", c.service_seconds}, {"consumer_and_receipt_reserved_bytes", result.consumer_and_receipt_reserved_bytes}});
        Json::Array recovered;
        for (const auto& message : result.recovered)
            recovered.push_back(object({{"first_public_slot", message.first_public_slot},
                {"stop_public_slot", message.stop_public_slot}, {"bit_positions", message.bit_positions},
                {"erased_positions", message.erased_positions}, {"fragments", message.fragments},
                {"bytes", bits_json(message.content.message.data)}, {"authenticated", message.content.authenticated}}));
        receipt["protected_recovered_candidates"] = std::move(recovered);
        receipt["consumer_runs_after_both_complete_native_replays"] = true;
    }
    return receipt;
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

enum class FadeCase { unprotected_prefix, protected_wanted, protected_wrong_key };

constexpr std::string_view fade_mode(FadeCase selected) {
    switch (selected) {
    case FadeCase::unprotected_prefix: return "predictive-active-fade";
    case FadeCase::protected_wanted: return "predictive-protected-fade";
    case FadeCase::protected_wrong_key: return "predictive-protected-fade-wrong-key";
    }
    return "unknown";
}

Json wrong_key_fixture_roots() {
    return object({{"aes_root_bytes", 64}, {"aes_waveform_seed_hex_range", "a5..c4 ascending"},
        {"aes_root_suffix_hex_range", "c5..e4 ascending"}, {"serpent_root_bytes", 64},
        {"serpent_root_hex_range", "31..70 ascending"}, {"scope", "public deterministic test fixture"}});
}

int predictive_active_fade(Clock::time_point began, FadeCase selected, bool key_preflight) {
    constexpr std::uint64_t sample_limit = 12'000'000;
    const bool protected_frame = selected != FadeCase::unprotected_prefix;
    const bool wrong_key = selected == FadeCase::protected_wrong_key;
    const auto fec = protected_frame ? FecMode::rs20 : FecMode::off;
    const double time_limit = key_preflight ? 15. : protected_frame ? 1800. : 600.;
    constexpr std::uint64_t path_seed = 70501, noise_seed = 80501;
    constexpr double receiver_variance = 1.92, rf_tone_hz = 100.;
    constexpr std::uint64_t endpoint_fade_margin_rows = 64;
    std::stop_source cancellation;
    std::mutex deadline_mutex;
    std::condition_variable_any deadline_changed;
    std::jthread timer([&](std::stop_token done) {
        std::unique_lock lock(deadline_mutex);
        deadline_changed.wait_until(lock, done, began + std::chrono::duration<double>(time_limit), [] { return false; });
        if (!done.stop_requested()) cancellation.request_stop();
    });
    const auto token = cancellation.get_token();

    PublicProfileConfig public_config;
    auto& format = public_config.format;
    format.sample_rate = 1920;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.added_interleave = true;
    format.bit_seconds = 64.;
    format.epoch = 1793404800;
    format.symbol_ordinal = 0;
    format.encrypt = true;
    public_config.data_if_hz = 240.;
    format.configured_carrier_hz = rf_hz + public_config.data_if_hz;
    public_config.capture_lead_seconds = .25;
    public_config.arrival_low_seconds = 2. / format.sample_rate;
    public_config.arrival_high_seconds = 130. / format.sample_rate;
    public_config.cfo_bound_hz = 6.;
    public_config.audio_pair_ppm = public_config.clock_bound_ppm = 4.;
    public_config.receiver_filter_halo_samples = 152;
    const PublicProfile profile(public_config);
    const auto key = Crypto::amateur();
    std::array<std::uint8_t, 64> wrong_aes_root{}, wrong_serpent_root{};
    std::iota(wrong_aes_root.begin(), wrong_aes_root.end(), static_cast<std::uint8_t>(0xa5));
    std::iota(wrong_serpent_root.begin(), wrong_serpent_root.end(), static_cast<std::uint8_t>(0x31));
    const auto transmitter_key = wrong_key ? Crypto::from_independent_roots(wrong_serpent_root, wrong_aes_root) : key;
    const Bytes prefix{'A'}, payload = protected_frame ? Bytes{'e'} : Bytes{'A', 'e'};
    const auto prefix_bits = encode_message_bits(prefix, FecMode::off);
    const auto wire_bits = encode_message_bits(payload, fec);
    constexpr std::uint64_t first_faded = 13, resumed = 15;
    const auto slot_stop = wire_bits.size() + (protected_frame ? 2 : 0);
    if (protected_frame) {
        if (wire_bits.size() != 64) throw std::runtime_error("protected fade requires the canonical 64-position RS20 e frame");
        check(true, "native_RS20_e_has_64_wire_positions_before_its_two_ending_quiet_slots");
    } else {
        if (prefix_bits.size() != first_faded || wire_bits.size() != 16 ||
            !std::ranges::equal(prefix_bits, std::span(wire_bits).first(first_faded)))
            throw std::runtime_error("active-fade payload must share the canonical A prefix and resume in slot 15");
        check(true, "native_Ae_wire_bits_begin_with_complete_A_short_code");
    }
    if (wrong_key) {
        const Crypto waveform_only{std::span(wrong_aes_root).first<32>()};
        bool same_waveform_seed = true;
        for (const auto purpose : {StreamPurpose::Data, StreamPurpose::Dsss, StreamPurpose::Scrambler, StreamPurpose::Fhss}) {
            const auto stream = transmitter_key.stream(purpose, format.epoch, 0, 32);
            same_waveform_seed &= stream == waveform_only.stream(purpose, format.epoch, 0, 32) &&
                stream != key.stream(purpose, format.epoch, 0, 32);
        }
        const auto mask = transmitter_key.confidentiality_stream(format.epoch, 0, 32);
        if (!format.encrypt || !same_waveform_seed || mask.size() != 32 || mask == key.confidentiality_stream(format.epoch, 0, 32))
            throw std::runtime_error("protected wrong-key fixture did not preserve its distinct waveform seed and encrypted payload stream");
        check(true, "wrong_key_keeps_distinct_AES_waveform_seed_and_supported_confidentiality");
    }
    if (key_preflight) {
        EmissionOptions options;
        options.bits = std::span(wire_bits).first(1);
        options.slot_count = 1;
        options.stop = token;
        Emission emission(profile, transmitter_key, options);
        std::vector<Complex> rows(receiver_chunk);
        const auto count = emission.read(rows);
        const auto report = emission.report();
        check(count == rows.size() && report.generated_samples == count && std::isfinite(report.data_energy) && report.data_energy > 0,
              "distinct_key_renders_a_bounded_encrypted_native_DATA_prefix");
        const auto wall = std::chrono::duration<double>(Clock::now() - began).count();
        check(!token.stop_requested() && wall <= time_limit, "wrong_key_preflight_fits_15_seconds");
        const auto output = object({{"probe", "hf_completion_ambiguity"}, {"mode", "predictive-protected-key-preflight"},
            {"status", all_passed() ? "passed" : "failed"}, {"wrong_key_fixture_roots", wrong_key_fixture_roots()},
            {"encrypted_profile", format.encrypt}, {"native_emitted_rows", count}, {"maximum_emitted_rows", rows.size()},
            {"native_emitted_data_energy", static_cast<double>(report.data_energy)}, {"receiver_executed", false},
            {"communications_assessed", false}, {"wall_seconds", wall}, {"assertions", assertion_json()},
            {"source_identity", object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
                {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
                {"native_source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT}})}});
        std::cout << std::setprecision(17);
        write_json(std::cout, output);
        std::cout << '\n';
        return all_passed() ? 0 : 1;
    }

    ReceiverConfig config;
    config.sample_rate_hz = format.sample_rate;
    config.data_if_hz = public_config.data_if_hz;
    config.noise_variance = receiver_variance;
    config.frequency_bound_hz = 1.;
    config.clock_bound_ppm = 4.;
    config.endpoint_clock_bound_ppm = 2.;
    config.time_varying_channel = true;
    config.time_varying_atmospheric_hz = config.time_varying_common_doppler_hz = 5.;
    config.capture_tail_bound_seconds = 152. / format.sample_rate;
    config.helper = profile.helper_plan();
    config.data_basis_gain = 1. / std::sqrt(data_power_reference(format, key, token).mean_norm);
    const auto previous = make_receiver_slot_plan(profile, config, first_faded - 1);
    const auto fade_a = make_receiver_slot_plan(profile, config, first_faded);
    const auto fade_b = make_receiver_slot_plan(profile, config, first_faded + 1);
    const auto resumed_slot = make_receiver_slot_plan(profile, config, resumed);
    // The archived oblique A run used these public slots. Protect the test
    // against accidentally moving its source origin or quiet apertures.
    if (previous.stop_sample != 2'208'571 || fade_a.first_sample != 2'246'241 || fade_a.stop_sample != 2'381'372 ||
        fade_b.first_sample != 2'419'040 || fade_b.stop_sample != 2'554'173)
        throw std::runtime_error("active-fade calendar does not match the archived oblique A public geometry");
    check(true, "public_wanted_prefix_and_two_fade_apertures_match_archived_oblique_A");
    const auto capture_stop = make_receiver_slot_plan(profile, config, slot_stop - 1).stop_sample;
    const auto mute_first = std::midpoint(previous.stop_sample, fade_a.first_sample);
    const auto mute_stop = std::midpoint(fade_b.stop_sample, resumed_slot.first_sample);
    if (capture_stop > sample_limit || mute_first < previous.stop_sample + endpoint_fade_margin_rows ||
        fade_a.first_sample < mute_first + endpoint_fade_margin_rows ||
        mute_stop < fade_b.stop_sample + endpoint_fade_margin_rows ||
        resumed_slot.first_sample < mute_stop + endpoint_fade_margin_rows)
        throw std::runtime_error("active-fade public gaps do not cover the endpoint clock and interpolation margins");
    check(true, "wanted_only_fade_covers_exactly_two_observations_with_endpoint_margin");

    const auto resources = resolve_stream_receiver_resources();
    HfNativeChainReport render_report;
    double emitted_data_energy = 0.;
    const auto render_begin = Clock::now();
    const auto input = [&] {
        EmissionOptions emission_options;
        emission_options.bits = wire_bits;
        emission_options.slot_count = wire_bits.size();
        emission_options.stop = token;
        Emission emission(profile, transmitter_key, emission_options);
        auto source = emission.source();
        const auto sample_at = source.sample_at;
        source.sample_at = [sample_at](std::uint64_t row) {
            return row < source_preroll_samples ? Complex{} : sample_at(row - source_preroll_samples);
        };
        source.geometry.source_samples += source_preroll_samples;
        auto chain = chain_config(source, capture_stop, false);
        auto& effects = chain.endpoints.effects;
        effects.seed = path_seed;
        effects.nominal_conversion_if_hz = config.data_if_hz;
        effects.tx.audio_ppm = 1.;
        effects.rx.audio_ppm = -1.;
        effects.tx.rf_ppm = .01;
        effects.rx.rf_ppm = -.01;
        chain.endpoints.noise_seed = noise_seed;
        chain.propagation_seed = path_seed;
        chain.final_receiver_noise_variance = receiver_variance;
        chain.propagation.paths.front().delay_seconds = std::midpoint(public_config.arrival_low_seconds,
                                                                     public_config.arrival_high_seconds);
        chain.propagation.fade.first_seconds = static_cast<double>(mute_first) / format.sample_rate;
        chain.propagation.fade.stop_seconds = static_cast<double>(mute_stop) / format.sample_rate;
        chain.propagation.noise.tone_amplitude = continuing_tone_amplitude;
        chain.propagation.noise.tone_hz = rf_tone_hz;
        chain.tx_band = {-384., 384.};
        chain.propagation_band = {-389., 389.};
        chain.continuing_rf_interference = true;
        chain.propagation_signal_stop_samples = chain.propagation_horizon_samples;
        chain.public_endpoint_clock_bound_ppm = config.endpoint_clock_bound_ppm;
        chain.propagation_horizon_samples = static_cast<std::uint64_t>(std::ceil(chain.capture_horizon_samples /
            (1. - chain.public_endpoint_clock_bound_ppm * 1e-6) + 8));
        const auto capture_bytes = capture_stop * sizeof(Complex);
        const auto preparation_reserve = execution::process_footprint_bytes() + source.working_bytes +
            capture_bytes + chain.maximum_workspace_bytes + receiver_chunk;
        if (preparation_reserve >= resources.memory_limit_bytes || chain.propagation_horizon_samples > sample_limit)
            throw std::runtime_error("active-fade render and retained IQ exceed the predeclared resource bound");
        HfNativeChain channel(source, chain, token);
        auto iq = read_through(channel, capture_stop, sample_limit);
        render_report = channel.report();
        emitted_data_energy = static_cast<double>(emission.report().data_energy);
        return iq;
    }();
    const auto render_seconds = std::chrono::duration<double>(Clock::now() - render_begin).count();
    const auto digest = sha256_iq(input);
    NativePrefixResult baseline, candidate;
    bool baseline_completed = false, candidate_started = false;
    try {
        run_native_prefix(baseline, profile, key, config, input, "active-fade native", false, slot_stop, token, fec);
        baseline_completed = true;
        candidate_started = true;
        run_native_prefix(candidate, profile, key, config, input, "active-fade predictor", true, slot_stop, token, fec);
    } catch (const std::exception& error) {
        if (!protected_frame) throw;
        check(false, "both_native_protected_replays_complete_within_the_declared_budget");
        const auto wall = std::chrono::duration<double>(Clock::now() - began).count();
        auto partial = failure_receipt(error.what(), wall);
        partial["mode"] = fade_mode(selected);
        partial["status"] = token.stop_requested() ? "time_limit" : "failed";
        partial["failing_policy"] = candidate_started ? "predictive_quiet" : "ordinary_native";
        partial["baseline_completed"] = baseline_completed;
        partial["candidate_started"] = candidate_started;
        partial["candidate_completed"] = false;
        partial["communications_assessed"] = false;
        partial["branch_covered"] = false;
        partial["operational_safety_pass"] = false;
        partial["protected_consumer_started"] = false;
        partial["input_sha256"] = digest;
        partial["rendered_input_rows"] = input.size();
        partial["render_wall_seconds"] = render_seconds;
        partial["public_slot_stop"] = slot_stop;
        partial["maximum_samples"] = sample_limit;
        partial["maximum_wall_seconds"] = time_limit;
        if (wrong_key) partial["wrong_key_fixture_roots"] = wrong_key_fixture_roots();
        partial["baseline"] = prefix_receipt(baseline);
        partial["candidate"] = candidate_started ? prefix_receipt(candidate) : Json{};
        std::cout << std::setprecision(17);
        write_json(std::cout, partial);
        std::cout << '\n';
        return 1;
    }
    if (protected_frame) {
        const bool verified_baseline = verify_protected_receipts(baseline, profile, config, slot_stop, capture_stop);
        const bool verified_candidate = verify_protected_receipts(candidate, profile, config, slot_stop, capture_stop);
        check(verified_baseline && verified_candidate, "both_complete_native_receipts_match_the_bounded_contiguous_public_capture");
        if (verified_baseline && verified_candidate) {
            reassemble_protected_receipts(baseline);
            reassemble_protected_receipts(candidate);
        }
    }
    const auto comparison_complete = [&](const NativePrefixResult& result) {
        return result.report.state == StreamReceiverState::receiving && !result.report.input_closed &&
            result.report.input_stop == capture_stop && result.report.next_public_slot == slot_stop &&
            result.report.uncommitted_slots == 0 && result.slots.size() == slot_stop &&
            std::ranges::all_of(result.slots, [](const auto& row) { return row.score.complete && row.score.all_valid; });
    };
    const bool complete = comparison_complete(baseline) && comparison_complete(candidate) &&
        candidate.predictions.size() == slot_stop;
    check(complete, "both_policies_score_all_public_apertures_and_resume_before_eof");
    check(baseline.input_sha256 == digest && candidate.input_sha256 == digest &&
          candidate.training_scanned_samples == capture_stop,
          "both_receivers_and_the_causal_training_pass_use_one_immutable_capture");
    check(same_data_evidence(baseline, candidate), "both_policies_preserve_native_data_evidence_on_matched_IQ");
    check(resources.hardware_threads > 0 && resources.workers == 2 * resources.hardware_threads &&
          baseline.report.resources.workers == resources.workers && candidate.report.resources.workers == resources.workers,
          "both_policies_use_full_2H_worker_budget");

    const auto predictor_changed_to_quiet = [&](std::uint64_t i) {
        if (!complete) return false;
        const auto& a = baseline.slots[i].score;
        const auto& b = candidate.slots[i].score;
        const auto& p = candidate.predictions[i];
        return a.decision == SlotDecision::ambiguous && b.decision == SlotDecision::quiet && p.attempted && p.accepted &&
            p.frequency_first.stop <= b.first_sample && p.frequency_middle.stop <= b.first_sample &&
            p.frequency_second.stop <= b.first_sample && p.amplitude.stop <= b.first_sample && p.validation.stop <= b.first_sample;
    };
    const bool activated = predictor_changed_to_quiet(first_faded) && predictor_changed_to_quiet(first_faded + 1);
    bool resumed_data = complete, intact_prefix = complete, correct_nonoutage = complete;
    if (complete) {
        for (std::uint64_t i = 0; i < slot_stop; ++i) {
            const auto expected = profile.slot(i);
            const auto& a = baseline.slots[i];
            const auto& b = candidate.slots[i];
            check(a.score.index == i && b.score.index == i && a.symbol_ordinal == expected.symbol_ordinal &&
                  b.symbol_ordinal == expected.symbol_ordinal && a.pattern_epoch == expected.pattern_epoch &&
                  b.pattern_epoch == expected.pattern_epoch, "both_policies_preserve_public_calendar_slot_" + std::to_string(i));
            if (i < first_faded || (i >= resumed && i < wire_bits.size())) {
                const bool observed = a.score.decision == SlotDecision::data && a.score.bit == wire_bits[i] &&
                    b.score.decision == SlotDecision::data && b.score.bit == wire_bits[i];
                correct_nonoutage &= observed;
                if (i < first_faded) intact_prefix &= observed;
                if (i == resumed) resumed_data = observed;
            }
        }
    }

    std::size_t new_premature = 0, new_wrong = 0;
    bool accepted_prefix = false;
    const auto same_event = [](const CompletedMessage& a, const CompletedMessage& b) {
        if (a.first_public_slot != b.first_public_slot || a.stop_public_slot != b.stop_public_slot ||
            a.message.physical_complete != b.message.physical_complete || a.message.state != b.message.state ||
            a.message.decoded_content.has_value() != b.message.decoded_content.has_value()) return false;
        return !a.message.decoded_content || a.message.decoded_content->message.data == b.message.decoded_content->message.data;
    };
    for (const auto& event : candidate.messages) {
        if (std::ranges::any_of(baseline.messages, [&](const auto& ordinary) { return same_event(ordinary, event); })) continue;
        if (event.message.physical_complete && event.stop_public_slot < wire_bits.size() + 2) ++new_premature;
        if (event.message.decoded_content && event.message.decoded_content->message.data != payload) {
            ++new_wrong;
            accepted_prefix |= event.stop_public_slot == resumed && event.message.decoded_content->message.data == prefix;
        }
    }
    bool covered = complete && intact_prefix && activated && resumed_data;
    Json protected_outcome;
    if (protected_frame) {
        const auto expected_reject = [](const CompletedMessage& event, std::uint64_t first, std::uint64_t stop) {
            return event.first_public_slot == first && event.stop_public_slot == stop && event.message.physical_complete &&
                event.message.state == MessageState::invalid_message && event.message.reason == "codec_rejected_message" &&
                !event.message.decoded_content;
        };
        const bool native_rejects = candidate.messages.size() == 2 &&
            expected_reject(candidate.messages.front(), 0, resumed) && expected_reject(candidate.messages.back(), resumed, slot_stop);
        const bool combined = candidate.consumer && candidate.consumer->decode_attempts == 1 &&
            candidate.consumer->last_attempt_positions == wire_bits.size() && candidate.consumer->last_attempt_erasures == 2 &&
            candidate.consumer->last_gap_positions == 2;
        const bool ended_quiet = complete && candidate.slots[slot_stop - 2].score.decision == SlotDecision::quiet &&
            candidate.slots[slot_stop - 1].score.decision == SlotDecision::quiet;
        const bool ending_activation = predictor_changed_to_quiet(slot_stop - 2) && predictor_changed_to_quiet(slot_stop - 1);
        const bool exact_recovery = candidate.recovered.size() == 1 && candidate.recovered[0].first_public_slot == 0 &&
            candidate.recovered[0].stop_public_slot == slot_stop && candidate.recovered[0].bit_positions == wire_bits.size() &&
            candidate.recovered[0].erased_positions == 2 && candidate.recovered[0].fragments == 2 &&
            candidate.recovered[0].content.message.data == payload;
        const auto accepted_bad = [&](const NativePrefixResult& result) {
            return std::ranges::any_of(result.messages, [&](const auto& event) {
                return event.message.decoded_content && (wrong_key || event.message.decoded_content->message.data != payload ||
                    event.first_public_slot != 0 || event.stop_public_slot != slot_stop);
            }) || std::ranges::any_of(result.recovered, [&](const auto& event) {
                return wrong_key || event.content.message.data != payload ||
                    event.first_public_slot != 0 || event.stop_public_slot != slot_stop;
            });
        };
        const bool no_bad_acceptance = !accepted_bad(baseline) && !accepted_bad(candidate);
        const auto false_data = [](const NativePrefixResult& result) {
            return std::ranges::count_if(result.slots, [](const auto& receipt) { return receipt.score.decision == SlotDecision::data; });
        };
        const auto baseline_false_data = wrong_key ? false_data(baseline) : 0;
        const auto candidate_false_data = wrong_key ? false_data(candidate) : 0;
        const bool consumer_acceptance_branch = complete && activated && ending_activation && native_rejects && combined;
        covered = consumer_acceptance_branch && (wrong_key || correct_nonoutage);
        check(ending_activation, "predictor_uses_earlier_public_gaps_to_change_both_ending_slots_from_ambiguous_to_quiet");
        check(native_rejects, "candidate_retains_both_native_codec_rejections_at_actual_two_quiet_completions");
        check(combined, "native_event_consumer_combines_once_at_final_quiet_with_64_positions_and_two_located_erasures");
        check(no_bad_acceptance, "no_incorrect_native_or_post_drain_protected_content_accepted");
        if (wrong_key) {
            check(!baseline_false_data && !candidate_false_data, "wrong_key_native_data_discrimination");
        } else {
            check(correct_nonoutage, "every_nonoutage_bit_including_resumed_data_is_correct_in_both_policies");
            check(exact_recovery, "candidate_recovers_exact_RS20_e_once_at_its_true_physical_end");
        }
        protected_outcome = object({{"native_codec_rejections_at_both_expected_ends", native_rejects},
            {"candidate_ending_quiet_pair_observed", ended_quiet}, {"candidate_ending_quiet_pair_predicted", ending_activation},
            {"candidate_combined_64_positions_with_two_erasures", combined},
            {"consumer_acceptance_branch_covered", consumer_acceptance_branch}, {"no_incorrect_content_accepted", no_bad_acceptance},
            {"consumer_acceptance_check", consumer_acceptance_branch && no_bad_acceptance && (wrong_key || exact_recovery)},
            {"correct_nonoutage_data_in_both_policies", !wrong_key && correct_nonoutage},
            {"exact_eventual_e_recovered_from_actual_native_events", !wrong_key && exact_recovery},
            {"wrong_key_false_data_slots_baseline", baseline_false_data}, {"wrong_key_false_data_slots_candidate", candidate_false_data},
            {"wrong_key_data_discrimination_pass", wrong_key ? Json(!baseline_false_data && !candidate_false_data) : Json{}},
            {"decoder_receives_truth_or_synthetic_events", false}, {"additional_consumer_dsp_pass", false}});
    } else {
        check(!new_premature && !new_wrong, "candidate_does_not_introduce_premature_or_wrong_content_acceptance");
    }
    const double wall = std::chrono::duration<double>(Clock::now() - began).count();
    check(wall <= time_limit && !token.stop_requested(), "active_fade_probe_fits_declared_wall_budget");
    const auto status = !all_passed() ? "failed" : covered ? "passed" : "uncovered";
    auto output = object({{"probe", "hf_completion_ambiguity"}, {"mode", fade_mode(selected)},
        {"status", status}, {"operational_safety_pass", covered && all_passed()},
        {"scope", "constructed unit-gain fixed-delay substitute for the oblique field with the original oblique-A public and endpoint geometry"},
        {"branch_covered", covered}, {"both_fade_slots_changed_from_ambiguous_to_quiet", activated},
        {"canonical_prefix_correct_in_both_policies", intact_prefix}, {"correct_resumed_data_in_both_policies", resumed_data},
        {"new_premature_completions", new_premature}, {"new_wrong_accepted_messages", new_wrong},
        {"new_accepted_A_prefix", accepted_prefix}, {"finish_input_called", false},
        {"native_receiver_uses_outage_labels_or_true_payload", false}, {"shared_rendered_capture", true},
        {"input_sha256", digest}, {"rendered_input_rows", input.size()}, {"maximum_samples", sample_limit},
        {"materialized_capture_bytes", input.size() * sizeof(Complex)},
        {"materialized_capture_charged_to_receiver_process_footprint", true}, {"maximum_wall_seconds", time_limit},
        {"wall_seconds", wall}, {"render_wall_seconds", render_seconds}, {"channel_workspace_bytes", render_report.workspace_bytes},
        {"renderer_tx_horizon_samples", render_report.tx_horizon_samples},
        {"renderer_propagation_horizon_samples", render_report.propagation_horizon_samples},
        {"renderer_capture_horizon_samples", render_report.capture_horizon_samples},
        {"bounds", object({{"sample_rate_hz", config.sample_rate_hz}, {"public_data_if_hz", config.data_if_hz},
            {"requested_bit_seconds", format.bit_seconds}, {"actual_bit_seconds", profile.geometry().bit_seconds},
            {"arrival_low_seconds", public_config.arrival_low_seconds}, {"arrival_high_seconds", public_config.arrival_high_seconds},
            {"fixed_path_delay_seconds", std::midpoint(public_config.arrival_low_seconds, public_config.arrival_high_seconds)},
            {"public_clock_bound_ppm", config.clock_bound_ppm}, {"public_endpoint_clock_bound_ppm", config.endpoint_clock_bound_ppm},
            {"public_oscillator_bound_hz", config.frequency_bound_hz}, {"public_atmospheric_bound_hz", config.time_varying_atmospheric_hz},
            {"receiver_filter_halo_samples", public_config.receiver_filter_halo_samples}})},
        {"fixture", object({{"wanted_bytes", bits_json(payload)}, {"short_prefix_bytes", protected_frame ? Json{} : bits_json(prefix)},
            {"wanted_wire_bits", bits_json(wire_bits)}, {"faded_public_slots", Json::Array{first_faded, first_faded + 1}},
            {"resumed_public_slot", resumed}, {"propagation_fade_first_sample", mute_first},
            {"propagation_fade_stop_sample", mute_stop}, {"wanted_voltage_during_fade", 0.},
            {"interference_continues_during_fade", true}, {"rf_tone_hz", rf_tone_hz},
            {"rf_tone_amplitude", continuing_tone_amplitude}, {"noise_variance", receiver_variance},
            {"path_seed", path_seed}, {"noise_seed", noise_seed}, {"actual_tx_audio_ppm", 1.}, {"actual_rx_audio_ppm", -1.},
            {"actual_tx_rf_ppm", .01}, {"actual_rx_rf_ppm", -.01}})},
        {"baseline", prefix_receipt(baseline)}, {"candidate", prefix_receipt(candidate)}, {"assertions", assertion_json()},
        {"source_identity", object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
            {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
            {"native_source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT}})}});
    if (protected_frame) {
        const auto uncoded_bits = encode_message_bits(payload, FecMode::off).size();
        output["fec"] = "rs20";
        output["operational_qualification"] = false;
        output["wrong_key"] = wrong_key;
        output["wrong_key_direction"] = wrong_key ? "fixed_distinct_transmitter_a5_through_c4_to_amateur_receiver" : "none";
        if (wrong_key) output["wrong_key_fixture_roots"] = wrong_key_fixture_roots();
        output["protected_outcome"] = std::move(protected_outcome);
        output["reassembly_scope"] = object({{"public_slots", slot_stop},
            {"retained_capture_all_valid", true}, {"receiver_feed_starts_at_sample_zero_and_has_no_gaps", true},
            {"native_short_codec_position_limit", message_receiver_options(fec).maximum_data_bits},
            {"native_long_marker_positions", message_marker_bits}, {"overlapping_ready_windows_enabled", true},
            {"strict_contiguous_native_event_origins_required_for_consumer", true},
            {"scope", "fixed valid 66-slot capture below the native recovery/resource thresholds; independent consumer abstains on an episode-origin break"}});
        output["coding_cost"] = object({{"coded_active_slots", wire_bits.size()}, {"uncoded_reference_slots", uncoded_bits},
            {"nominal_active_airtime_and_incident_energy_ratio_at_equal_power", static_cast<double>(wire_bits.size()) / uncoded_bits},
            {"nominal_coded_active_seconds", wire_bits.size() * profile.geometry().bit_seconds},
            {"nominal_uncoded_active_seconds", uncoded_bits * profile.geometry().bit_seconds},
            {"emitted_data_iq_energy_sample_sum", emitted_data_energy},
            {"propagation_wanted_iq_energy_after_fade_sample_sum", static_cast<double>(render_report.propagation.signal_energy)},
            {"uncoded_energy_measured_at_the_same_outage", false}});
    }
    std::cout << std::setprecision(17);
    write_json(std::cout, output);
    std::cout << '\n';
    return covered && all_passed() ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    const auto began = Clock::now();
    const auto selection = argc == 2 ? std::string_view(argv[1]) : std::string_view{};
    const bool predictive_tone = selection == "--predictive-tone";
    const bool key_preflight = selection == "--predictive-protected-key-preflight";
    std::optional<FadeCase> fade_case;
    if (selection == "--predictive-active-fade") fade_case = FadeCase::unprotected_prefix;
    if (selection == "--predictive-protected-fade") fade_case = FadeCase::protected_wanted;
    if (selection == "--predictive-protected-fade-wrong-key" || key_preflight) fade_case = FadeCase::protected_wrong_key;
    try {
        if (fade_case) return predictive_active_fade(began, *fade_case, key_preflight);
        if (argc != 1 && !predictive_tone)
            throw std::invalid_argument("usage: high_fidelity_completion_ambiguity_probe [--predictive-tone|--predictive-active-fade|--predictive-protected-fade|--predictive-protected-fade-wrong-key|--predictive-protected-key-preflight]");
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
        public_config.format.configured_carrier_hz = rf_hz + public_config.data_if_hz;
        if (predictive_tone) public_config.cfo_bound_hz = 3.25;
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
        if (predictive_tone) {
            receiver_config.frequency_bound_hz = .25;
            receiver_config.time_varying_channel = true;
            receiver_config.time_varying_atmospheric_hz = 3.;
            receiver_config.time_varying_common_doppler_hz = 3.;
        }
        const auto resources = resolve_stream_receiver_resources();
        std::size_t maximum_slot_workspace = 0;
        for (std::size_t i = 0; i < short_bits.size() + 2; ++i)
            maximum_slot_workspace = std::max(maximum_slot_workspace, receiver_slot_workspace(
                receiver_config, make_receiver_slot_plan(profile, receiver_config, i), resources.workers));
        check(maximum_slot_workspace > 0 && maximum_slot_workspace < resources.memory_limit_bytes,
              "all_shared_slots_have_valid_geometry_and_bounded_full_worker_workspace_quotes");
        std::size_t maximum_predictive_workspace = 0, predictive_training_reserve = 0;
        if (predictive_tone) {
            const HfPredictiveQuiet quote(profile, receiver_config, public_probe_slots);
            predictive_training_reserve = quote.training_workspace_bytes();
            for (std::uint64_t i = 0; i < public_probe_slots; ++i)
                maximum_predictive_workspace = std::max(maximum_predictive_workspace, quote.workspace(
                    receiver_config, make_receiver_slot_plan(profile, receiver_config, i), resources.workers, true));
            check(maximum_predictive_workspace >= maximum_slot_workspace &&
                  predictive_training_reserve < resources.memory_limit_bytes &&
                  maximum_predictive_workspace < resources.memory_limit_bytes - predictive_training_reserve,
                  "predictor_and_full_worker_slot_quotes_fit_shared_memory_limit");
            check(short_bits.size() + 2 == public_probe_slots,
                  "predictor_uses_fixed_public_five_slot_probe_horizon");
            check(continuing_tone_hz >= short_source.minimum_frequency_hz &&
                  continuing_tone_hz <= short_source.maximum_frequency_hz,
                  "frozen_17_hz_interferer_is_in_the_occupied_data_band");
        }
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
        auto short_chain_config = chain_config(short_source, std::max(later_stop, std::uint64_t{1}), predictive_tone);
        auto long_chain_config = chain_config(long_source, std::max(later_stop, std::uint64_t{1}), predictive_tone);
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

        const auto render_begin = Clock::now();
        HfNativeChain short_chain(short_source, short_chain_config);
        HfNativeChain long_chain(long_source, long_chain_config);
        check(later_stop <= short_chain.total_samples() && later_stop <= long_chain.total_samples(),
              "native_channel_horizons_cover_requested_calendar");
        const auto short_iq = read_through(short_chain, later_stop);
        const auto long_iq = read_through(long_chain, later_stop);
        const auto render_seconds = std::chrono::duration<double>(Clock::now() - render_begin).count();
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

        const auto ordinary_short_rx = receive_prefix(profile, key, receiver_config, short_prefix, "ordinary short");
        const auto ordinary_long_rx = receive_prefix(profile, key, receiver_config, long_prefix, "ordinary long");
        std::optional<NativePrefixResult> predictive_short_rx, predictive_long_rx;
        if (predictive_tone) {
            check(ambiguous_prefix(ordinary_short_rx, profile, short_bits, prefix_stop),
                  "ordinary_short_tone_receiver_commits_three_data_and_two_ambiguous_slots_without_completion");
            check(ambiguous_prefix(ordinary_long_rx, profile, short_bits, prefix_stop),
                  "ordinary_long_tone_receiver_commits_three_data_and_two_ambiguous_slots_without_completion");
            predictive_short_rx = receive_prefix(profile, key, receiver_config, short_prefix, "predictive short", true);
            predictive_long_rx = receive_prefix(profile, key, receiver_config, long_prefix, "predictive long", true);
            check(same_data_evidence(ordinary_short_rx, *predictive_short_rx) &&
                  same_data_evidence(ordinary_long_rx, *predictive_long_rx),
                  "predictor_preserves_matched_native_data_evidence_and_data_decisions");
            check(ordinary_short_rx.input_sha256 == predictive_short_rx->input_sha256 &&
                  ordinary_long_rx.input_sha256 == predictive_long_rx->input_sha256 &&
                  ordinary_short_rx.input_sha256 == short_hash && ordinary_long_rx.input_sha256 == long_hash,
                  "both_policies_receive_the_exact_same_two_immutable_prefixes");
            check(predictive_short_rx->training_scanned_samples == prefix_stop &&
                  predictive_long_rx->training_scanned_samples == prefix_stop,
                  "sequential_causal_training_prepass_is_complete_and_charged");
            const auto causal_quiet = [](const NativePrefixResult& result) {
                if (result.slots.size() != public_probe_slots || result.predictions.size() != public_probe_slots)
                    return false;
                for (std::size_t i = 3; i < public_probe_slots; ++i) {
                    const auto& p = result.predictions[i];
                    const auto before = result.slots[i].score.first_sample;
                    if (!p.attempted || !p.accepted || p.frequency_first.stop > before ||
                        p.frequency_second.stop > before || p.amplitude.stop > before || p.validation.stop > before)
                        return false;
                }
                return true;
            };
            check(causal_quiet(*predictive_short_rx) && causal_quiet(*predictive_long_rx),
                  "both_predictive_quiet_decisions_use_only_earlier_public_gap_samples");
        }
        const auto& short_rx = predictive_tone ? *predictive_short_rx : ordinary_short_rx;
        const auto& long_rx = predictive_tone ? *predictive_long_rx : ordinary_long_rx;
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
              long_rx.report.resources.hardware_threads == resources.hardware_threads &&
              ordinary_short_rx.report.resources.workers == resources.workers &&
              ordinary_long_rx.report.resources.workers == resources.workers &&
              ordinary_short_rx.report.resources.hardware_threads == resources.hardware_threads &&
              ordinary_long_rx.report.resources.hardware_threads == resources.hardware_threads,
              "canonical_receiver_uses_default_full_2H_worker_budget");
        const double wall = std::chrono::duration<double>(Clock::now() - began).count();
        check(wall <= maximum_wall_seconds, "bounded_probe_wall_time_at_or_below_120_seconds");
        const auto passed = all_passed();
        std::cout << std::setprecision(17);
        auto output = object({
            {"probe", "hf_completion_ambiguity"},
            {"status", passed ? "passed" : "failed"},
            {"claim_scope", predictive_tone ?
                "constructed_continuing_tone_predictor_outage_integrity_failure_witness_not_operational_qualification" :
                "constructed_zero_floor_outage_identifiability_witness_not_natural_channel_probability"},
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
        });
        if (predictive_tone) {
            const auto policy = [&](const NativePrefixResult& a, const NativePrefixResult& b) {
                const auto accepted_incorrect = [&](const NativePrefixResult& result, const Bytes& wanted) {
                    return std::count_if(result.messages.begin(), result.messages.end(), [&](const auto& event) {
                        return event.message.decoded_content && event.message.decoded_content->message.data != wanted;
                    });
                };
                return object({{"short_receiver", prefix_receipt(a)}, {"long_receiver", prefix_receipt(b)},
                    {"short_accepted_incorrect_messages", static_cast<std::uint64_t>(accepted_incorrect(a, short_text))},
                    {"long_accepted_incorrect_messages", static_cast<std::uint64_t>(accepted_incorrect(b, long_text))},
                    {"short_space_completed_before_eof", complete_prefix(a, profile, short_bits, prefix_stop) &&
                        decoded_as_space(a.messages)},
                    {"long_prefix_accepted_as_space_before_eof", complete_prefix(b, profile, short_bits, prefix_stop) &&
                        decoded_as_space(b.messages)},
                    {"pair_service_wall_seconds", a.service_wall_seconds + b.service_wall_seconds},
                    {"pair_service_cpu_seconds", a.service_cpu_seconds + b.service_cpu_seconds}});
            };
            output["mode"] = "predictive-tone";
            output["operational_safety_pass"] = false;
            output["expected_communications_failure_reproduced"] = same_iq && short_good && long_premature &&
                resumed_difference_energy > 1e-12 &&
                ambiguous_prefix(ordinary_short_rx, profile, short_bits, prefix_stop) &&
                ambiguous_prefix(ordinary_long_rx, profile, short_bits, prefix_stop);
            output["selected_policy_in_top_level_receipts"] = "predictive_quiet";
            output["policies"] = object({{"ordinary_native", policy(ordinary_short_rx, ordinary_long_rx)},
                {"predictive_quiet", policy(short_rx, long_rx)}});
            output["predictive_tone_scope"] = object({
                {"fixed_tone_hz", continuing_tone_hz}, {"fixed_tone_amplitude", continuing_tone_amplitude},
                {"actual_clock_errors_ppm", 0.}, {"actual_oscillator_offsets_hz", 0.},
                {"public_cfo_bound_hz", public_config.cfo_bound_hz},
                {"receiver_frequency_bound_hz", receiver_config.frequency_bound_hz},
                {"receiver_atmospheric_bound_hz", receiver_config.time_varying_atmospheric_hz},
                {"receiver_common_doppler_bound_hz", receiver_config.time_varying_common_doppler_hz},
                {"public_slot_first", 0}, {"public_slot_stop", public_probe_slots},
                {"helper_enabled", false}, {"receiver_uses_true_payload_or_end", false},
                {"tone_added_after_wanted_outage", true}, {"continuing_rf_interference", true},
                {"short_wanted_support_stop_samples", short_chain_config.propagation_signal_stop_samples},
                {"long_wanted_support_stop_samples", long_chain_config.propagation_signal_stop_samples},
                {"short_propagation_horizon_samples", short_chain_config.propagation_horizon_samples},
                {"long_propagation_horizon_samples", long_chain_config.propagation_horizon_samples},
                {"short_capture_horizon_samples", short_chain_config.capture_horizon_samples},
                {"long_capture_horizon_samples", long_chain_config.capture_horizon_samples},
                {"rendered_samples_per_chain", later_stop}, {"maximum_rendered_samples_per_chain", maximum_samples},
                {"rendered_once_per_source", true}, {"shared_render_wall_seconds", render_seconds},
                {"maximum_predictive_slot_workspace_bytes", maximum_predictive_workspace},
                {"predictive_training_reserve_bytes", predictive_training_reserve},
                {"service_includes_training_and_executor_teardown", true},
                {"natural_fade_probability_or_reliability_claim", false}});
        }
        write_json(std::cout, output);
        std::cout << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        assertions.push_back({"probe_completed_without_exception", false});
        const double wall = std::chrono::duration<double>(Clock::now() - began).count();
        auto failure = failure_receipt(error.what(), wall);
        if (predictive_tone) {
            failure["mode"] = "predictive-tone";
            failure["operational_safety_pass"] = false;
            failure["expected_communications_failure_reproduced"] = false;
            failure["claim_scope"] = "predictive_outage_witness_incomplete_after_exception";
        }
        if (fade_case) {
            failure["mode"] = key_preflight ? "predictive-protected-key-preflight" : fade_mode(*fade_case);
            failure["operational_safety_pass"] = false;
            failure["branch_covered"] = false;
            failure["communications_assessed"] = false;
            failure["claim_scope"] = key_preflight ? "wrong_key_emission_preflight_incomplete" :
                "active_fade_acceptance_incomplete_after_exception";
            if (*fade_case == FadeCase::protected_wrong_key) failure["wrong_key_fixture_roots"] = wrong_key_fixture_roots();
        }
        write_json(std::cout, failure);
        std::cout << '\n';
        return 1;
    }
}
