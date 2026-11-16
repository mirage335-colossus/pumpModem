#include "appraisal/hf_native_chain.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "hf_normalized_null.hpp"
#include "hf_protected_reassembly.hpp"
#include "triad_probe_json.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
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
constexpr std::size_t chunk_rows = 4096, consumer_reserve = 65536;
constexpr std::size_t normalized_capture_reserve = hf_normalized_null_maximum_rows * sizeof(Complex);
constexpr double noise_variance = .0001, maximum_wall_seconds = 120.;
constexpr std::uint64_t ordinary_sample_limit = 10'000'000, separate_sample_limit = 16'000'000;
constexpr std::uint64_t wrong_key_join_sample_limit = 18'000'000;
constexpr std::uint64_t noise_seed = 0x5253323070726f62ULL, path_seed = 0x525332306368616eULL;

struct Deadline {
    std::stop_source source;
    const Clock::time_point stop = Clock::now() + std::chrono::seconds(120);
    std::jthread timer{[this](std::stop_token cancelled) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        (void)wake.wait_until(lock, cancelled, stop, [] { return false; });
        if (!cancelled.stop_requested()) source.request_stop();
    }};
    void check() const {
        if (source.stop_requested()) throw std::runtime_error("declared 120-second case deadline exhausted");
    }
};

struct Digest {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    Digest() {
        if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("could not initialize input SHA-256");
    }
    void add(std::span<const Complex> rows) {
        std::array<unsigned char, 16> encoded{};
        for (const auto value : rows) {
            const std::array parts{value.real(), value.imag()};
            for (unsigned part = 0; part < 2; ++part) {
                const auto bits = std::bit_cast<std::uint64_t>(parts[part]);
                for (unsigned byte = 0; byte < 8; ++byte)
                    encoded[8 * part + byte] = static_cast<unsigned char>(bits >> (8 * byte));
            }
            if (EVP_DigestUpdate(context.get(), encoded.data(), encoded.size()) != 1)
                throw std::runtime_error("could not update input SHA-256");
        }
    }
    std::string finish() {
        std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
        unsigned count = 0;
        if (EVP_DigestFinal_ex(context.get(), bytes.data(), &count) != 1 || count != 32)
            throw std::runtime_error("could not finalize input SHA-256");
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (unsigned i = 0; i < count; ++i) {
            result += hex[bytes[i] >> 4]; result += hex[bytes[i] & 15];
        }
        return result;
    }
};

Json bytes_json(std::span<const std::uint8_t> bytes) {
    Json::Array result;
    for (auto value : bytes) result.emplace_back(value);
    return result;
}

struct Case {
    std::string_view name;
    std::vector<Bytes> payloads;
    std::optional<std::uint64_t> fade_first;
    bool noise = false, wrong_key = false;
    std::uint64_t seed = noise_seed;
    bool wrong_key_join = false;
};

Case select_case(std::string_view name) {
    if (name == "clean") return {name, {{'B'}}, {}, false, false};
    if (name == "fade-byte") return {name, {{'B'}}, 10, false, false};
    if (name == "fade-boundary") return {name, {{'e'}}, 15, false, false, noise_seed + 1};
    if (name == "noise") return {name, {{'B'}}, {}, true, false};
    if (name == "wrong-key") return {name, {{'B'}}, {}, false, true};
    if (name == "separate") return {name, {{' '}, {'e'}}, {}, false, false};
    if (name == "wrong-key-join") return {name, {{'B'}, {'e'}}, {}, false, true, noise_seed, true};
    throw std::invalid_argument("--case must be clean, fade-byte, fade-boundary, noise, wrong-key, separate, or wrong-key-join");
}

Json identity() {
    return object({{"appraisal_source_sha256", APPRAISAL_SOURCE_SHA},
        {"native_source_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
        {"native_source_commit_at_configure", DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT}});
}

Json consumer_report(const ProtectedReassemblyReport& report) {
    return object({{"events", report.events}, {"seeds", report.seeds}, {"appends", report.appends},
        {"decode_attempts", report.decode_attempts}, {"candidates", report.candidates},
        {"malformed_events", report.malformed_events}, {"expirations", report.expirations}, {"resets", report.resets},
        {"pending_positions", report.pending_positions}, {"pending_erased_positions", report.pending_erased_positions},
        {"pending_fragments", report.pending_fragments}, {"last_attempt_positions", report.last_attempt_positions},
        {"last_attempt_erasures", report.last_attempt_erasures}, {"last_gap_positions", report.last_gap_positions},
        {"last_action", report.last_action}, {"decode_seconds", report.decode_seconds},
        {"service_seconds", report.service_seconds}});
}
} // namespace

int main(int argc, char** argv) {
    const auto began = Clock::now();
    const auto cpu_began = std::clock();
    Json::Array assertions, slot_rows, native_rows, recovered_rows;
    std::string name;
    std::uint64_t rendered = 0, accepted = 0;
    StreamReceiverReport report;
    std::vector<CompletedMessage> completed;
    std::vector<ProtectedReassemblyCandidate> recovered;
    std::vector<StreamSlotReceipt> slots;
    bool observed_recovery = false;
    bool normalized_first_slot = false;
    const auto check = [&](bool passed, std::string_view label) {
        assertions.emplace_back(object({{"name", label}, {"passed", passed}}));
        return passed;
    };
    try {
        if ((argc != 3 && argc != 4) || std::string_view(argv[1]) != "--case" ||
            (argc == 4 && std::string_view(argv[3]) != "--normalized-first-slot"))
            throw std::invalid_argument("usage: high_fidelity_protected_reassembly_probe --case CASE [--normalized-first-slot]");
        name = argv[2];
        const auto selected = select_case(name);
        normalized_first_slot = argc == 4;
        if (normalized_first_slot && name != "clean" && name != "wrong-key")
            throw std::invalid_argument("--normalized-first-slot is confined to --case clean or wrong-key");
        Deadline deadline;
        const auto cancellation = deadline.source.get_token();
        PublicProfileConfig pc;
        pc.format.sample_rate = 1920;
        pc.format.inner_rate_hz = 120;
        pc.format.added_symbols = 1;
        pc.format.added_interleave = false;
        pc.format.bit_seconds = 8.;
        pc.format.epoch = 1793850000;
        pc.format.symbol_ordinal = 741;
        pc.format.encrypt = false;
        pc.helper_enabled = false;
        pc.data_if_hz = 500.;
        pc.format.configured_carrier_hz = 10e6 + pc.data_if_hz;
        pc.lead_samples = 480;
        pc.receiver_filter_halo_samples = 8;
        const PublicProfile profile(pc);
        const auto receiver_key = Crypto::amateur();
        const std::array<std::uint8_t, 32> wrong_root = [] {
            std::array<std::uint8_t, 32> value{};
            for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::uint8_t>(0xa5 + i);
            return value;
        }();
        const auto transmitter_key = selected.wrong_key ? Crypto(wrong_root) : receiver_key;
        std::vector<Bytes> wire;
        std::vector<std::uint64_t> starts;
        std::uint64_t slot_stop = 0;
        for (const auto& payload : selected.payloads) {
            starts.push_back(slot_stop);
            wire.push_back(encode_message_bits(payload, FecMode::rs20));
            slot_stop += wire.back().size() + 2;
            // The new negative control has eight actual quiet slots between
            // two independently emitted codewords. The final two quiet slots
            // remain unchanged. Neither extent is passed to the consumer.
            if (selected.wrong_key_join && wire.size() < selected.payloads.size()) slot_stop += 6;
        }
        if (selected.wrong_key_join && (wire.size() != 2 || wire[0].size() != 72 || wire[1].size() != 64 ||
            starts[0] != 0 || starts[1] != 80 || slot_stop != 146))
            throw std::runtime_error("frozen wrong-key join geometry changed; expected B72 + quiet8 + e64 + quiet2");
        ReceiverConfig config;
        config.sample_rate_hz = pc.format.sample_rate;
        config.data_if_hz = pc.data_if_hz;
        config.noise_variance = noise_variance;
        config.data_basis_gain = 1. / std::sqrt(data_power_reference(pc.format, receiver_key, cancellation).mean_norm);
        const auto receive_slot_stop = normalized_first_slot ? std::uint64_t{1} : slot_stop;
        const auto first_observation = make_receiver_slot_plan(profile, config, 0);
        const auto original_input_stop = make_receiver_slot_plan(profile, config, slot_stop - 1).stop_sample;
        const auto input_stop = normalized_first_slot ? first_observation.stop_sample : original_input_stop;
        const auto captured_rows = normalized_first_slot ? first_observation.stop_sample - first_observation.first_sample : 0;
        if (normalized_first_slot && (!captured_rows || captured_rows > hf_normalized_null_maximum_rows))
            throw std::runtime_error("unchanged first observation exceeds normalized shadow row cap");
        const auto outside_reserve = consumer_reserve + (normalized_first_slot ? normalized_capture_reserve : 0);
        const auto sample_limit = selected.wrong_key_join ? wrong_key_join_sample_limit :
            selected.payloads.size() == 2 ? separate_sample_limit : ordinary_sample_limit;
        if (input_stop > sample_limit) throw std::runtime_error("public RS20 capture exceeds declared per-case sample limit");
        const auto resources = resolve_stream_receiver_resources();
        if (resources.memory_limit_bytes <= outside_reserve)
            throw std::runtime_error("bounded consumer/capture reserve exhausts receiver memory budget");
        std::size_t largest_slot = 0;
        for (std::uint64_t i = 0; i < receive_slot_stop; ++i)
            largest_slot = std::max(largest_slot, receiver_slot_workspace(config,
                make_receiver_slot_plan(profile, config, i), resources.workers));
        if (largest_slot >= resources.memory_limit_bytes - outside_reserve)
            throw std::runtime_error("full-worker slot quote exceeds memory remaining after bounded consumer reserve");

        std::vector<Emission> emissions;
        std::vector<EmittedSource> episode_sources;
        std::vector<std::uint64_t> origins;
        emissions.reserve(wire.size()); episode_sources.reserve(wire.size()); origins.reserve(wire.size());
        for (std::size_t i = 0; i < wire.size(); ++i) {
            EmissionOptions options;
            options.mode = selected.noise ? EmissionMode::Quiet : EmissionMode::Message;
            options.first_slot = starts[i];
            options.slot_count = wire[i].size();
            if (!selected.noise) options.bits = wire[i];
            options.source_first_sample = i == 0 ? 0 : profile.calendar().distance(profile.origin(),
                profile.transmit_slot(starts[i]).helper_begin);
            options.stop = cancellation;
            origins.push_back(options.source_first_sample);
            emissions.emplace_back(profile, transmitter_key, options);
            episode_sources.push_back(emissions.back().source());
        }
        auto source = episode_sources.front();
        for (std::size_t i = 1; i < episode_sources.size(); ++i) {
            source.geometry.source_samples = std::max(source.geometry.source_samples,
                origins[i] + episode_sources[i].geometry.source_samples);
            source.working_bytes += episode_sources[i].working_bytes;
        }
        // Native emissions keep distinct global ordinals. Summing disjoint
        // source supports inserts actual physical quiet between messages.
        source.sample_at = [&](std::uint64_t at) {
            Complex value{};
            for (std::size_t i = 0; i < episode_sources.size(); ++i)
                if (at >= origins[i] && at - origins[i] < episode_sources[i].geometry.source_samples)
                    value += episode_sources[i].sample_at(at - origins[i]);
            return value;
        };
        HfNativeChainConfig channel_config;
        auto& endpoint = channel_config.endpoints;
        endpoint.effects.sample_rate = pc.format.sample_rate;
        endpoint.effects.rf_hz = 10e6;
        endpoint.effects.occupied_bandwidth_hz = profile.geometry().occupied_bandwidth_hz;
        endpoint.effects.nominal_conversion_if_hz = pc.data_if_hz;
        endpoint.effects.noise_enabled = false;
        endpoint.complex_noise_variance = 0.;
        endpoint.noise_seed = selected.seed;
        endpoint.path_seed = path_seed;
        endpoint.maximum_chunk_samples = chunk_rows;
        channel_config.final_receiver_noise_variance = noise_variance;
        channel_config.propagation_seed = path_seed;
        channel_config.propagation.rf_hz = 10e6;
        channel_config.tx_horizon_samples = source.geometry.source_samples + 128;
        channel_config.propagation_horizon_samples = channel_config.tx_horizon_samples + 384;
        channel_config.capture_horizon_samples = std::max(original_input_stop, channel_config.propagation_horizon_samples + 512);
        channel_config.tx_band = {source.minimum_frequency_hz - 16., source.maximum_frequency_hz + 16.};
        channel_config.propagation_band = channel_config.tx_band;
        channel_config.bridge_chunk_samples = 1024;
        channel_config.maximum_workspace_bytes = 64u * 1024u * 1024u;
        std::uint64_t fade_first_sample = 0, fade_stop_sample = 0;
        if (selected.fade_first) {
            const auto first = *selected.fade_first;
            if (first == 0 || first + 2 >= wire.front().size()) throw std::runtime_error("outage is not interior");
            const auto previous = profile.slot(first - 1), start = profile.slot(first);
            const auto last = profile.slot(first + 1), after = profile.slot(first + 2);
            if (previous.data_support.stop >= start.observation.first || last.observation.stop >= after.data_support.first)
                throw std::runtime_error("declared interior fade does not have separated public gap boundaries");
            fade_first_sample = previous.data_support.stop + (start.observation.first - previous.data_support.stop) / 2;
            fade_stop_sample = last.observation.stop + (after.data_support.first - last.observation.stop) / 2;
            auto& fade = channel_config.propagation.fade;
            fade.first_seconds = static_cast<double>(fade_first_sample) / pc.format.sample_rate;
            fade.stop_seconds = static_cast<double>(fade_stop_sample) / pc.format.sample_rate;
            fade.ramp_seconds = 0.; fade.voltage_floor = 0.;
        }
        if (channel_config.capture_horizon_samples > sample_limit)
            throw std::runtime_error("complete native channel tail exceeds declared per-case sample limit");
        HfNativeChain channel(source, channel_config, cancellation);
        StreamReceiverOptions options;
        options.maximum_input_chunk_samples = chunk_rows;
        options.record_commit_timing = true;
        options.memory_limit_bytes = resources.memory_limit_bytes - outside_reserve;
        const NativeMessageProfile message{FecMode::rs20, true, 16, receiver_key};
        HfProtectedReassembly consumer(FecMode::rs20);
        const auto receiver_began = Clock::now();
        auto receiver = std::make_unique<CompleteStreamReceiver>(profile, config, receiver_key, options, message);
        std::optional<std::stop_callback<std::function<void()>>> cancel_receiver;
        cancel_receiver.emplace(cancellation, [&] { receiver->request_cancel(); });
        const auto drain = [&] {
            report = receiver->report();
            // Input is drained at every public observation boundary below.
            // At most one new commit can occur between these snapshots, so
            // recovery cannot enter and exit behind a later good report.
            if (report.recovering) {
                observed_recovery = true;
                consumer.reset(report.next_public_slot, ProtectedReassemblyBoundary::native_recovery);
                throw std::runtime_error("native recovery observed; consumer qualification aborted and state cleared");
            }
            for (auto& event : receiver->drain_messages()) {
                if (auto candidate = consumer.consume(event)) recovered.push_back(std::move(*candidate));
                completed.push_back(std::move(event));
            }
            consumer.advance(report.next_public_slot);
            auto recent = receiver->drain_recent_slots();
            slots.insert(slots.end(), std::make_move_iterator(recent.begin()), std::make_move_iterator(recent.end()));
            deadline.check();
            if (report.state != StreamReceiverState::receiving || report.input_closed)
                throw std::runtime_error("native receiver stopped before physical completion: " + report.reason);
        };
        std::vector<Complex> iq(chunk_rows);
        std::vector<std::uint8_t> validity(chunk_rows);
        std::vector<Complex> normalized_capture(static_cast<std::size_t>(captured_rows));
        std::optional<Digest> normalized_stream_digest;
        if (normalized_first_slot) normalized_stream_digest.emplace();
        std::uint64_t normalized_captured_rows = 0;
        Digest digest;
        double render_seconds = 0., digest_seconds = 0.;
        std::uint64_t boundary_slot = 0;
        auto boundary_stop = make_receiver_slot_plan(profile, config, boundary_slot).stop_sample;
        while (rendered < input_stop) {
            deadline.check();
            if (boundary_stop <= rendered) throw std::runtime_error("non-increasing public observation boundary");
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(chunk_rows, boundary_stop - rendered));
            const auto rows = std::span(iq).first(count);
            const auto mask = std::span(validity).first(count);
            const auto render_begin = Clock::now();
            if (channel.read(rows, mask) != count || !std::ranges::all_of(mask, [](auto valid) { return valid == 1; }))
                throw std::runtime_error("native chain ended or invalidated rows before public final quiet aperture");
            render_seconds += std::chrono::duration<double>(Clock::now() - render_begin).count();
            const auto digest_begin = Clock::now();
            digest.add(rows);
            if (normalized_first_slot) {
                const auto first = std::max(rendered, first_observation.first_sample);
                const auto stop = std::min(rendered + count, first_observation.stop_sample);
                if (stop > first) {
                    const auto copied = rows.subspan(static_cast<std::size_t>(first - rendered),
                        static_cast<std::size_t>(stop - first));
                    std::ranges::copy(copied, normalized_capture.begin() +
                        static_cast<std::size_t>(first - first_observation.first_sample));
                    normalized_stream_digest->add(copied);
                    normalized_captured_rows += copied.size();
                }
            }
            digest_seconds += std::chrono::duration<double>(Clock::now() - digest_begin).count();
            const auto first = rendered;
            rendered += count;
            for (;;) {
                const auto pushed = receiver->push(first, rows, mask);
                drain();
                if (pushed == StreamPushResult::accepted) { accepted += count; break; }
                if (report.output_backpressured) continue;
                if (!report.active_owners) throw std::runtime_error("native receiver cannot admit unchanged chunk");
                (void)receiver->wait_for_slot_completion(); drain();
            }
            if (rendered == boundary_stop) {
                (void)receiver->resume(true); drain();
                if (report.next_public_slot != boundary_slot + 1 || report.uncommitted_slots)
                    throw std::runtime_error("native controller did not commit the single completed public boundary");
                if (++boundary_slot < receive_slot_stop)
                    boundary_stop = make_receiver_slot_plan(profile, config, boundary_slot).stop_sample;
            }
        }
        (void)receiver->resume(true); drain();
        cancel_receiver.reset();
        receiver.reset();
        const double receiver_wall = std::chrono::duration<double>(Clock::now() - receiver_began).count();
        const auto iq_hash = digest.finish();
        Json normalized_shadow;
        bool normalized_shadow_executed = false, normalized_capture_matches = false, normalized_native_preserved = false;
        std::string normalized_observation_hash;
        double normalized_shadow_seconds = 0.;
        if (normalized_first_slot) {
            deadline.check();
            if (normalized_captured_rows != captured_rows || slots.size() != 1 ||
                !slots.front().score.complete || !slots.front().score.all_valid)
                throw std::runtime_error("normalized shadow requires the complete unchanged first native observation");
            Digest captured_digest;
            captured_digest.add(normalized_capture);
            normalized_observation_hash = normalized_stream_digest->finish();
            normalized_capture_matches = captured_digest.finish() == normalized_observation_hash;
            if (!normalized_capture_matches) throw std::runtime_error("normalized shadow capture differs from native input rows");
            const auto before = slots.front().score;
            const auto shadow_began = Clock::now();
            // Ordinary receiver stacks are already destroyed. This shadow
            // uses the unchanged static receiver config, never a time-varying
            // substitute, and does not receive a true bit or source extent.
            normalized_shadow = diagnose_hf_normalized_null(config, first_observation, receiver_key,
                normalized_capture, resources.workers, resources.memory_limit_bytes - consumer_reserve, cancellation);
            normalized_shadow_seconds = std::chrono::duration<double>(Clock::now() - shadow_began).count();
            normalized_shadow_executed = true;
            const auto& after = slots.front().score;
            normalized_native_preserved = before.decision == after.decision && before.bit == after.bit &&
                before.operational_log_e == after.operational_log_e &&
                before.operational_bit_evidence == after.operational_bit_evidence &&
                before.quiet_energy == after.quiet_energy;
            normalized_shadow["observation_sha256"] = normalized_observation_hash;
            normalized_shadow["observation_hashed_samples"] = normalized_captured_rows;
            normalized_shadow["shadow_executed_after_receiver_closed"] = true;
            normalized_shadow["original_native_decision_preserved"] = normalized_native_preserved;
            deadline.check();
        }
        const auto expected_at = [&](std::uint64_t index) -> std::optional<std::pair<std::size_t, std::size_t>> {
            if (selected.noise) return {};
            for (std::size_t i = 0; i < starts.size(); ++i)
                if (index >= starts[i] && index - starts[i] < wire[i].size()) return {{i, index - starts[i]}};
            return {};
        };
        std::size_t wrong_data = 0, false_data = 0, lost_active = 0, quiet_active = 0;
        for (const auto& receipt : slots) {
            const auto& row = receipt.score;
            const auto wanted = expected_at(row.index);
            const bool target = wanted.has_value() && !selected.wrong_key;
            if (row.decision == SlotDecision::data) {
                false_data += !target;
                if (target && (!row.bit || *row.bit != wire[wanted->first][wanted->second])) ++wrong_data;
            } else if (target) { ++lost_active; quiet_active += row.decision == SlotDecision::quiet; }
            slot_rows.emplace_back(object({{"index", row.index}, {"pattern_epoch", receipt.pattern_epoch},
                {"symbol_ordinal", receipt.symbol_ordinal}, {"complete", row.complete}, {"all_valid", row.all_valid},
                {"decision", row.decision == SlotDecision::data ? "data" : row.decision == SlotDecision::quiet ? "quiet" : "ambiguous"},
                {"bit", row.bit ? Json(*row.bit) : Json{}}, {"reason", row.reason},
                {"operational_log_e", row.operational_log_e}, {"operational_threshold", row.operational_threshold},
                {"quiet_energy", row.quiet_energy}, {"quiet_threshold", row.quiet_threshold},
                {"helper_seconds", row.helper_seconds}, {"coarse_seconds", row.coarse_seconds}, {"fine_seconds", row.fine_seconds},
                {"workspace_peak_bytes", row.workspace_peak_bytes}, {"workers", receipt.workers}}));
        }
        std::size_t canonical_correct = 0, canonical_wrong = 0, canonical_premature = 0;
        for (const auto& event : completed) {
            const auto& m = event.message;
            bool correct = false, premature = false;
            for (std::size_t i = 0; i < wire.size(); ++i) {
                correct |= !selected.noise && !selected.wrong_key && event.first_public_slot == starts[i] &&
                    event.stop_public_slot == starts[i] + wire[i].size() + 2 && m.decoded_content &&
                    m.decoded_content->message.data == selected.payloads[i];
                premature |= !selected.noise && event.first_public_slot >= starts[i] &&
                    event.first_public_slot < starts[i] + wire[i].size() &&
                    event.stop_public_slot < starts[i] + wire[i].size() + 2;
            }
            canonical_correct += correct;
            canonical_wrong += m.decoded_content.has_value() && !correct;
            canonical_premature += premature;
            Json::Array quiet;
            for (auto slot : m.trailing_quiet_slots) quiet.emplace_back(slot);
            native_rows.emplace_back(object({{"first_public_slot", event.first_public_slot},
                {"stop_public_slot", event.stop_public_slot}, {"state", static_cast<unsigned>(m.state)}, {"reason", m.reason},
                {"physical_complete", m.physical_complete}, {"decoded_content", m.decoded_content.has_value()},
                {"decoded_bytes", m.decoded_content ? bytes_json(m.decoded_content->message.data) : Json{}},
                {"observed_bits", m.observed_bits}, {"missing_slots", m.missing_slots},
                {"trailing_quiet_slots", std::move(quiet)}, {"premature_against_source", premature},
                {"message_service_seconds", m.message_service_seconds}}));
        }
        std::size_t consumer_correct = 0, consumer_wrong = 0;
        for (const auto& candidate : recovered) {
            const bool correct = !selected.noise && !selected.wrong_key && selected.payloads.size() == 1 &&
                candidate.first_public_slot == 0 && candidate.stop_public_slot == slot_stop &&
                candidate.bit_positions == wire.front().size() && candidate.content.message.data == selected.payloads.front();
            consumer_correct += correct; consumer_wrong += !correct;
            recovered_rows.emplace_back(object({{"first_public_slot", candidate.first_public_slot},
                {"stop_public_slot", candidate.stop_public_slot}, {"bit_positions", candidate.bit_positions},
                {"erased_positions", candidate.erased_positions}, {"fragments", candidate.fragments},
                {"decoded_bytes", bytes_json(candidate.content.message.data)}, {"correct_against_source", correct}}));
        }
        bool passed = true;
        bool consumer_acceptance_pass = false;
        passed &= check(report.state == StreamReceiverState::receiving && !report.input_closed &&
            report.next_public_slot == receive_slot_stop && !report.uncommitted_slots && accepted == input_stop,
            normalized_first_slot ? "first_public_observation_commits_with_input_still_open" :
                "all_public_slots_commit_with_input_open_and_no_eof");
        passed &= check(slots.size() == receive_slot_stop && std::ranges::all_of(slots, [](const auto& row) {
            return row.score.complete && row.score.all_valid;
        }), "all_native_observations_complete_and_valid");
        passed &= check(resources.hardware_threads > 0 && report.resources.workers == 2 * resources.hardware_threads,
            "unchanged_full_2H_receiver_worker_budget");
        passed &= check(!observed_recovery && boundary_slot == receive_slot_stop,
            "every_public_commit_boundary_observed_without_native_recovery");
        if (normalized_first_slot) {
            passed &= check(slots.size() == 1 && slots.front().score.decision == SlotDecision::data &&
                (selected.wrong_key ? false_data == 1 : slots.front().score.bit == wire.front().front()),
                selected.wrong_key ? "original_wrong_key_first_slot_false_data_reproduced" :
                    "original_wanted_first_slot_correct_data_reproduced");
            passed &= check(normalized_shadow_executed && normalized_capture_matches && normalized_native_preserved,
                "normalized_shadow_exercises_exact_native_aperture_without_changing_decisions");
        } else {
            passed &= check(!wrong_data && !false_data && !canonical_wrong && !consumer_wrong,
                "no_wrong_bits_false_data_or_incorrect_content");
        }
        if (normalized_first_slot) {
            // A single public DATA observation cannot assess physical message
            // completion, framing integrity, or a false-message probability.
        } else if (selected.fade_first) {
            passed &= check(lost_active == 2 && quiet_active == 2 && canonical_premature == 1 &&
                canonical_correct == 0 && completed.size() == 2, "exact_two_slot_outage_preserves_original_native_fragment_failure");
            passed &= check(consumer_correct == 1 && recovered.size() == 1 && recovered.front().erased_positions == 2 &&
                recovered.front().fragments == 2, "post_drain_consumer_recovers_one_exact_protected_message");
        } else if (selected.noise || selected.wrong_key) {
            passed &= check(canonical_correct == 0 && recovered.empty() &&
                std::ranges::none_of(completed, [](const auto& event) { return event.message.decoded_content.has_value(); }),
                "negative_control_accepts_no_native_or_consumer_content");
            if (selected.wrong_key_join) {
                const bool native_rejects = completed.size() == 2 &&
                    std::ranges::all_of(completed, [](const auto& event) {
                        return event.message.physical_complete && event.message.state == MessageState::invalid_message &&
                            event.message.reason == "codec_rejected_message" && !event.message.decoded_content;
                    }) && completed[0].first_public_slot == 0 && completed[0].stop_public_slot == 74 &&
                    completed[1].first_public_slot == 80 && completed[1].stop_public_slot == 146;
                const auto& consumer_state = consumer.report();
                const bool combined_attempt = consumer_state.decode_attempts >= 1 && consumer_state.appends >= 1 &&
                    consumer_state.last_attempt_positions == 144 && consumer_state.last_attempt_erasures == 8 &&
                    consumer_state.last_gap_positions == 8;
                const bool no_acceptance = recovered.empty() && consumer_state.candidates == 0 &&
                    std::ranges::none_of(completed, [](const auto& event) { return event.message.decoded_content.has_value(); });
                passed &= check(native_rejects, "wrong_key_join_has_two_actual_native_codec_rejections_at_frozen_boundaries");
                passed &= check(combined_attempt, "wrong_key_join_executes_combined_decode_over_144_positions_with_eight_gap_erasures");
                passed &= check(no_acceptance, "wrong_key_join_accepts_no_native_or_consumer_content");
                // This narrower result never overrides the independent false
                // DATA assertion or the overall communications status.
                consumer_acceptance_pass = native_rejects && combined_attempt && no_acceptance;
            }
        } else {
            passed &= check(canonical_correct == selected.payloads.size() && !canonical_premature && !lost_active && recovered.empty(),
                "clean_native_messages_complete_and_are_not_reassembled");
            passed &= check(consumer.report().pending_positions == 0, "canonical_success_clears_consumer_state");
        }
        const double wall = std::chrono::duration<double>(Clock::now() - began).count();
        passed &= check(wall <= maximum_wall_seconds && rendered <= sample_limit, "declared_wall_and_sample_budgets_hold");
        Json::Array episodes;
        for (std::size_t i = 0; i < starts.size(); ++i)
            episodes.emplace_back(object({{"first_public_slot", starts[i]}, {"wire_bits", wire[i].size()},
                {"payload_bytes", bytes_json(selected.payloads[i])}}));
        const auto channel_report = channel.report();
        std::cout << std::setprecision(17);
        auto output = object({{"probe", "hf_protected_reassembly"}, {"case", selected.name},
            {"status", passed ? "passed" : "failed"}, {"source_identity", identity()},
            {"claim_scope", normalized_first_slot ?
                "matched_first_native_slot_normalized_null_component_diagnostic_not_message_or_sensitivity_qualification" :
                "constructed_native_short_rs20_controller_and_consumer_experiment_not_hf_sensitivity_qualification"},
            {"operational_qualification", false}, {"fec", "rs20"}, {"helper_enabled", false},
            {"consumer_acceptance_check", selected.wrong_key_join ?
                (consumer_acceptance_pass ? "passed" : "failed") : "not_requested"},
            {"wrong_key_direction", selected.wrong_key ? "transmitter_fixed_distinct_a5_through_c4_receiver_amateur" : "none"},
            {"actual_wrong_key_join_not_synthetic_receipts", selected.wrong_key_join},
            {"inter_episode_quiet_slots", selected.payloads.size() > 1 ? (selected.wrong_key_join ? 8 : 2) : 0},
            {"public_slot_stop", receive_slot_stop}, {"episodes_evaluator_only", std::move(episodes)},
            {"requested_bit_seconds", pc.format.bit_seconds}, {"actual_bit_seconds", profile.geometry().bit_seconds},
            {"public_minimum_recurrence_seconds", 60}, {"sample_rate_hz", pc.format.sample_rate}, {"data_if_hz", pc.data_if_hz},
            {"source_minimum_frequency_hz", source.minimum_frequency_hz}, {"source_maximum_frequency_hz", source.maximum_frequency_hz},
            {"noise_variance", noise_variance}, {"noise_seed", selected.seed}, {"path_seed", path_seed},
            {"outage_first_public_slot", selected.fade_first ? Json(*selected.fade_first) : Json{}},
            {"outage_slot_count", selected.fade_first ? 2 : 0}, {"outage_first_sample", fade_first_sample},
            {"outage_stop_sample", fade_stop_sample}, {"input_iq_sha256", iq_hash},
            {"iq_hash_encoding", "LE IEEE-754 binary64 real then imaginary; all validity bytes verified one"},
            {"rendered_samples", rendered}, {"accepted_samples", accepted}, {"maximum_samples", sample_limit},
            {"sample_budget_revision", selected.wrong_key_join ?
                "18M cap approved before execution for 146 canonical public slots at1920Hz (about16.8M rows); unchanged120s deadline; no full capture" :
                "2M cannot contain canonical 60-second slots and RS20; 1920Hz retained because native source declares +/-287.5Hz; 10M single/16M separate caps approved before execution"},
            {"capture_materialized", false}, {"finish_input_called", false}, {"input_open_at_completion", !report.input_closed},
            {"consumer_receives_source_truth", false}, {"consumer_second_dsp_pass", false},
            {"drained_at_each_public_observation_boundary", true}, {"observed_native_recovery", observed_recovery},
            {"canonical_correct_messages", canonical_correct}, {"canonical_incorrect_messages", canonical_wrong},
            {"canonical_premature_completions", canonical_premature}, {"consumer_correct_messages", consumer_correct},
            {"consumer_incorrect_messages", consumer_wrong}, {"wrong_data_bits", wrong_data}, {"false_data_slots", false_data},
            {"lost_active_slots", lost_active}, {"quiet_active_slots", quiet_active},
            {"slots", std::move(slot_rows)}, {"native_messages", std::move(native_rows)},
            {"consumer_candidates", std::move(recovered_rows)}, {"consumer", consumer_report(consumer.report())},
            {"cost", object({{"wall_seconds", wall}, {"process_cpu_seconds", static_cast<double>(std::clock() - cpu_began) / CLOCKS_PER_SEC},
                {"receiver_render_consumer_wall_seconds", receiver_wall}, {"render_call_seconds", render_seconds},
                {"iq_digest_seconds", digest_seconds}, {"workers", report.resources.workers},
                {"hardware_threads", report.resources.hardware_threads}, {"peak_executor_workers", report.peak_executor_workers},
                {"native_peak_reserved_bytes", report.peak_reserved_bytes}, {"receiver_memory_limit_bytes", report.resources.memory_limit_bytes},
                {"consumer_reserved_bytes", consumer_reserve}, {"maximum_slot_workspace_bytes", largest_slot},
                {"native_channel_workspace_bytes", channel_report.workspace_bytes},
                {"streaming_chunk_rows", chunk_rows}, {"capacity_qualification", false}})},
            {"assertions", std::move(assertions)}});
        if (normalized_first_slot) {
            output["mode"] = "normalized-first-slot";
            output["component_only"] = true;
            output["physical_completion_assessed"] = false;
            output["input_open_at_completion"] = Json{};
            output["capture_materialized"] = false;
            output["first_slot_capture_materialized"] = true;
            output["source_expected_completion_public_slot_stop"] = slot_stop;
            output["original_full_input_stop_samples"] = original_input_stop;
            output["source_emission_samples"] = source.geometry.source_samples;
            output["native_full_channel_capture_horizon_samples"] = channel_config.capture_horizon_samples;
            output["normalized_capture"] = object({{"first_sample", first_observation.first_sample},
                {"stop_sample", first_observation.stop_sample}, {"rows", normalized_captured_rows},
                {"maximum_rows", hf_normalized_null_maximum_rows}, {"fixed_reserved_bytes", normalized_capture_reserve},
                {"observation_sha256", normalized_observation_hash}, {"all_valid", true},
                {"hash_matches_native_input_rows", normalized_capture_matches}});
            output["normalized_native_config"] = object({{"time_varying_channel", config.time_varying_channel},
                {"frequency_bound_hz", config.frequency_bound_hz}, {"clock_bound_ppm", config.clock_bound_ppm},
                {"endpoint_clock_bound_ppm", config.endpoint_clock_bound_ppm},
                {"spread_delay_seconds", config.spread_delay_seconds}, {"spread_doppler_hz", config.spread_doppler_hz},
                {"time_varying_atmospheric_hz", config.time_varying_atmospheric_hz},
                {"time_varying_common_doppler_hz", config.time_varying_common_doppler_hz},
                {"data_basis_gain", config.data_basis_gain}, {"changed_for_shadow", false}});
            output["normalized_shadow_seconds"] = normalized_shadow_seconds;
            output["hf_normalized_null"] = std::move(normalized_shadow);
            output["shadow_threshold_or_rejection_asserted"] = false;
            output["canonical_false_data_failure_reproduced"] = selected.wrong_key && false_data == 1;
        }
        write_json(std::cout, output);
        std::cout << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        Json::Array partial_slots, partial_messages;
        for (const auto& receipt : slots) partial_slots.emplace_back(object({{"index", receipt.score.index},
            {"decision", receipt.score.decision == SlotDecision::data ? "data" :
                receipt.score.decision == SlotDecision::quiet ? "quiet" : "ambiguous"},
            {"bit", receipt.score.bit ? Json(*receipt.score.bit) : Json{}}, {"reason", receipt.score.reason},
            {"operational_log_e", receipt.score.operational_log_e},
            {"operational_threshold", receipt.score.operational_threshold}}));
        for (const auto& event : completed) partial_messages.emplace_back(object({
            {"first_public_slot", event.first_public_slot}, {"stop_public_slot", event.stop_public_slot},
            {"physical_complete", event.message.physical_complete}, {"reason", event.message.reason},
            {"decoded_bytes", event.message.decoded_content ? bytes_json(event.message.decoded_content->message.data) : Json{}}}));
        write_json(std::cout, object({{"probe", "hf_protected_reassembly"}, {"case", name}, {"status", "failed"},
            {"error", error.what()}, {"source_identity", identity()}, {"rendered_samples", rendered}, {"accepted_samples", accepted},
            {"wall_seconds", std::chrono::duration<double>(Clock::now() - began).count()},
            {"next_public_slot", report.next_public_slot}, {"observed_native_recovery", observed_recovery},
            {"partial_native_slots", std::move(partial_slots)}, {"partial_native_messages", std::move(partial_messages)},
            {"partial_consumer_candidates", recovered.size()},
            {"consumer_acceptance_check", name == "wrong-key-join" ? "failed" : "not_requested"},
            {"mode", normalized_first_slot ? "normalized-first-slot" : "complete-receiver"},
            {"component_only", normalized_first_slot}, {"physical_completion_assessed", false},
            {"operational_qualification", false}, {"assertions", std::move(assertions)}}));
        std::cout << '\n';
        return 1;
    }
}
