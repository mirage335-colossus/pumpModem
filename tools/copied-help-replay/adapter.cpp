#include "copied_help_fixture_contract.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/symbol_schedule.hpp"

#ifndef COPIED_HELP_FIXTURE_HEADER
#error COPIED_HELP_FIXTURE_HEADER must name a generated fixture header
#endif
#ifndef COPIED_HELP_FIXTURE_NAMESPACE
#error COPIED_HELP_FIXTURE_NAMESPACE must name its generated namespace
#endif
#include COPIED_HELP_FIXTURE_HEADER

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>
#include <openssl/evp.h>

namespace replay = datapump::diagnostic::copied_help_replay;
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t maximum_samples = 1048576;
constexpr std::size_t maximum_output_bytes = 8 * 1024 * 1024;
constexpr std::size_t maximum_native_events = 4096;

struct SetupError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Difference : std::runtime_error { using std::runtime_error::runtime_error; };

template<class Work> auto prepare(std::string_view operation, Work&& work) {
    try { return work(); }
    catch (const SetupError&) { throw; }
    catch (const std::exception& error) { throw SetupError(std::string(operation)+": "+error.what()); }
}

std::string quote(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) {
            char encoded[7]; std::snprintf(encoded, sizeof encoded, "\\u%04x", static_cast<unsigned>(c)); result += encoded;
        } else result += static_cast<char>(c);
    }
    result += '"'; return result;
}

std::string hex(double value) {
    std::ostringstream out; out << std::hexfloat << value; return out.str();
}
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

class Output {
    std::FILE* file_ = nullptr;
    std::size_t bytes_ = 0, rows_ = 0;
    bool flushed_ = false, closed_ = false;
public:
    explicit Output(const std::filesystem::path& path) {
        file_ = std::fopen(path.string().c_str(), "wbx");
        if (!file_) throw SetupError("output must be a writable new JSONL file");
    }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output() { close(); }
    void line(std::string_view value) {
        if (!file_) throw SetupError("output is closed");
        if (value.size() + 1 > maximum_output_bytes - bytes_) throw SetupError("diagnostic output byte cap exhausted");
        if (std::fwrite(value.data(), 1, value.size(), file_) != value.size() || std::fputc('\n', file_) == EOF)
            throw SetupError("diagnostic output write failed");
        bytes_ += value.size() + 1; ++rows_;
    }
    bool close() noexcept {
        if (!file_) return flushed_ && closed_;
        auto* file = file_; file_ = nullptr;
        flushed_ = std::fflush(file) == 0;
        closed_ = std::fclose(file) == 0;
        return flushed_ && closed_;
    }
    std::size_t rows() const { return rows_; }
    bool flush_ok() const { return flushed_; }
    bool close_ok() const { return closed_; }
};

std::string sha256(std::span<const float> pcm) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{}; unsigned count = 0;
    if (EVP_Digest(pcm.data(), pcm.size_bytes(), digest.data(), &count, EVP_sha256(), nullptr) != 1 || count != 32)
        throw SetupError("PCM SHA256 failed");
    constexpr char digits[] = "0123456789abcdef"; std::string result;
    for (unsigned i = 0; i < count; ++i) { result += digits[digest[i] >> 4]; result += digits[digest[i] & 15]; }
    return result;
}

std::string key_tag(const datapump::Crypto& key) {
    const auto tag = key.mac(datapump::Bytes{'D','P','-','R','X','-','B','A','N','K'});
    constexpr char digits[] = "0123456789abcdef"; std::string result;
    for (const auto value : tag) { result += digits[value >> 4]; result += digits[value & 15]; }
    return result;
}

std::filesystem::path resolve(const std::filesystem::path& base, std::string_view relative) {
    const auto path = std::filesystem::u8path(relative.begin(), relative.end());
    return path.is_absolute() ? path : base / path;
}

std::vector<float> read_pcm(const std::filesystem::path& path, std::string_view expected_sha) {
    if constexpr (std::endian::native != std::endian::little || sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559)
        throw SetupError("adapter requires little-endian IEC559 float32");
    const auto bytes = std::filesystem::file_size(path);
    if (!bytes || bytes % sizeof(float) || bytes / sizeof(float) > maximum_samples)
        throw SetupError("PCM size is empty, misaligned or over its retained cap");
    std::vector<float> pcm(static_cast<std::size_t>(bytes / sizeof(float)));
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(bytes));
    if (!input || input.peek() != std::char_traits<char>::eof()) throw SetupError("PCM read failed or size changed");
    if (sha256(pcm) != expected_sha) throw SetupError("retained PCM SHA256 mismatch");
    return pcm;
}

void difference(Output& output, std::string_view field, std::uint64_t feed_first,
                std::string_view expected, std::string_view actual) {
    output.line("{\"event\":\"first_difference\",\"field\":" + quote(field) +
        ",\"feed_first\":" + std::to_string(feed_first) + ",\"expected\":" + quote(expected) +
        ",\"actual\":" + quote(actual) + "}");
    throw Difference(std::string(field));
}
void equal_integer(Output& out, std::string_view field, std::uint64_t first, std::uint64_t a, std::uint64_t b) {
    if (a != b) difference(out, field, first, std::to_string(a), std::to_string(b));
}
void equal_double(Output& out, std::string_view field, std::uint64_t first, double a, double b) {
    if (!same(a, b)) difference(out, field, first, hex(a), hex(b));
}
std::uint64_t physical(std::uint64_t origin, std::uint64_t local) {
    if (local > UINT64_MAX - origin) throw SetupError("native physical coordinate overflow");
    return origin + local;
}
std::string wire(const datapump::modem::PatternBurst& burst) {
    std::string result; result.reserve(burst.bits.size());
    for (const auto bit : burst.bits) result += bit == 0 ? '0' : bit == 1 ? '1' : bit == datapump::modem::missing_pattern_bit ? '?' : '!';
    return result;
}

void native_output(Output& out, const replay::Fixture& fixture, const replay::Feed& feed,
                   const datapump::modem::PatternBurst& burst, std::uint64_t ordinal) {
    std::ostringstream row;
    row << "{\"event\":\"native_burst\",\"ordinal\":" << ordinal << ",\"receiver_id\":" << fixture.receiver_id
        << ",\"feed_first\":" << feed.first << ",\"feed_end\":" << feed.first + feed.count
        << ",\"first_sample\":" << burst.first_sample << ",\"end_sample\":" << burst.end_sample
        << ",\"stream_first_sample\":" << burst.stream_first_sample << ",\"stream_first_symbol\":" << burst.stream_first_symbol
        << ",\"chunk_first_symbol\":" << burst.first_stream_symbol << ",\"stream_phase_samples\":" << burst.stream_phase_samples
        << ",\"physical_first\":" << physical(fixture.input_origin, burst.stream_first_sample)
        << ",\"physical_chunk_first\":" << physical(fixture.input_origin, burst.first_sample)
        << ",\"physical_end\":" << physical(fixture.input_origin, burst.end_sample)
        << ",\"wire_bits\":" << quote(wire(burst)) << ",\"bit_values\":[";
    bool comma = false;
    for (const auto bit : burst.bits) { if (comma) row << ','; comma = true; row << unsigned(bit); }
    row << "],\"missing_slots\":" << burst.missing_slots << ",\"complete\":" << (burst.complete ? "true" : "false")
        << ",\"frequency_hz\":" << quote(hex(burst.frequency_hz)) << ",\"score\":" << quote(hex(burst.score))
        << ",\"support_samples\":" << quote(hex(burst.support_samples)) << '}';
    out.line(row.str());
}

void compare(Output& out, const replay::Fixture& fixture, const replay::Feed& feed,
             const replay::ExpectedBurst& expected, const datapump::modem::PatternBurst& actual) {
    equal_integer(out, "feed_first", feed.first, expected.feed_first, feed.first);
    equal_integer(out, "physical_first", feed.first, expected.physical_first, physical(fixture.input_origin, actual.stream_first_sample));
    equal_integer(out, "physical_end", feed.first, expected.physical_end, physical(fixture.input_origin, actual.end_sample));
    equal_integer(out, "stream_first_symbol", feed.first, expected.stream_first_symbol, actual.stream_first_symbol);
    equal_integer(out, "chunk_first_symbol", feed.first, expected.chunk_first_symbol, actual.first_stream_symbol);
    equal_integer(out, "stream_phase_samples", feed.first, expected.stream_phase_samples, actual.stream_phase_samples);
    equal_integer(out, "missing_slots", feed.first, expected.missing_slots, actual.missing_slots);
    equal_integer(out, "complete", feed.first, expected.complete, actual.complete);
    const auto bits = wire(actual);
    if (bits != expected.wire_bits) difference(out, "wire_bits", feed.first, expected.wire_bits, bits);
    equal_double(out, "score", feed.first, expected.score, actual.score);
    equal_double(out, "support_samples", feed.first, expected.support_samples, actual.support_samples);
    if (expected.physical_chunk_first)
        equal_integer(out, "physical_chunk_first", feed.first, *expected.physical_chunk_first, physical(fixture.input_origin, actual.first_sample));
    if (expected.frequency_hz) equal_double(out, "frequency_hz", feed.first, *expected.frequency_hz, actual.frequency_hz);
}

struct Counts {
    std::uint64_t feeds = 0, active_feeds = 0, native_events = 0, compared_events = 0, terminal_events = 0;
    std::uint64_t partition_push_calls = 0;
    std::string_view stage = "fixture_setup";
};

void run(Output& out, const replay::Fixture& fixture, const std::filesystem::path& base, Counts& counts) {
    if (fixture.feeds.empty() || fixture.expected.empty() || !fixture.make_config || !fixture.make_search)
        throw SetupError("fixture is missing feeds, golden bursts or config builders");
    if (fixture.feeds.size() > 8192 || fixture.expected.size() > maximum_native_events)
        throw SetupError("fixture inventory exceeds retained bounds");
    if (!fixture.expected.back().complete || fixture.feeds.back().first != fixture.expected.back().feed_first)
        throw SetupError("fixture must end on its retained last physically complete candidate feed");
    const auto pcm = prepare("retained PCM",[&]{return read_pcm(resolve(base, fixture.pcm_path), fixture.pcm_sha256);});
    const auto keys = prepare("retained keyring",[&]{return datapump::load_keyring(resolve(base, fixture.keyfile_path));});
    std::optional<datapump::Crypto> selected_key;
    for (const auto& item : keys) if (key_tag(item.key) == fixture.key_tag_hex) {
        if (selected_key) throw SetupError("key HMAC tag is ambiguous in retained keyring");
        selected_key = item.key;
    }
    if (!selected_key) throw SetupError("retained keyring has no matching HMAC tag");
    const auto config = prepare("captured Config",[&]{return fixture.make_config(*selected_key);});
    auto search = prepare("captured PatternSearch",[&]{return fixture.make_search();});
    if (!config.data_key || key_tag(*config.data_key) != fixture.key_tag_hex || config.stream_epoch != fixture.epoch)
        throw SetupError("generated seeded Config has wrong key or epoch");
    if (config.symbol_burst.enabled() || config.fake_hopping || config.scheduled_hopping || search.scheduled_capture || search.compact_clock_search)
        throw SetupError("fixture contains unsupported calendar or correlator context");
    if (search.input_sample_origin != fixture.input_origin || !fixture.constructor_grant || fixture.constructor_grant > std::numeric_limits<std::size_t>::max())
        throw SetupError("fixture origin or constructor grant is invalid");
    std::ostringstream begin;
    begin << "{\"event\":\"case_begin\",\"label\":" << quote(fixture.label) << ",\"receiver_id\":" << fixture.receiver_id
        << ",\"epoch\":" << fixture.epoch << ",\"input_origin\":" << fixture.input_origin << ",\"constructor_grant\":" << fixture.constructor_grant
        << ",\"pcm_sha256\":" << quote(fixture.pcm_sha256) << ",\"feeds\":" << fixture.feeds.size()
        << ",\"expected_events\":" << fixture.expected.size()
        << ",\"native_storage_context_replayed\":false,\"dynamic_workspace_replayed\":false,\"scope\":\"captured receiver prefix through terminal feed; no EOF flush or postterminal claim\"}";
    out.line(begin.str());

    std::unique_ptr<datapump::modem::StreamingReceiver> receiver;
    std::complex<double> mixer{1,0};
    const auto rotation = std::polar(1.,-2*std::numbers::pi*config.carrier_hz/config.sample_rate);
    std::array<std::complex<double>,2048> projected{};
    std::uint64_t end = 0, pcm_end = fixture.feeds.front().pcm_offset;
    std::size_t expected_index = 0;
    bool active_seen = false, active_ended = false;
    for (const auto& feed : fixture.feeds) {
        counts.stage = "projection";
        if (feed.first != end || feed.pcm_offset != pcm_end || !feed.count || feed.count > projected.size() || feed.first > UINT64_MAX - feed.count ||
            feed.pcm_offset > pcm.size() || feed.count > pcm.size() - feed.pcm_offset)
            throw SetupError("noncontiguous or out-of-range retained feed");
        equal_double(out, "mixer_real_before", feed.first, feed.mixer_real, mixer.real());
        equal_double(out, "mixer_imag_before", feed.first, feed.mixer_imag, mixer.imag());
        const auto raw = std::span(pcm).subspan(static_cast<std::size_t>(feed.pcm_offset), static_cast<std::size_t>(feed.count));
        // These are the original source37 feed_samples expressions/order.
        for (std::size_t i=0;i<raw.size();++i) {
            projected[i]=static_cast<double>(raw[i])*mixer;mixer*=rotation;
        }
        mixer/=std::abs(mixer);
        equal_double(out, "mixer_real_after", feed.first, feed.after_real, mixer.real());
        equal_double(out, "mixer_imag_after", feed.first, feed.after_imag, mixer.imag());
        ++counts.feeds; end = feed.first + feed.count; pcm_end = feed.pcm_offset + feed.count;
        if (!feed.receiver_active) {
            if (active_seen) { active_ended = true; receiver.reset(); }
            if (expected_index < fixture.expected.size() && fixture.expected[expected_index].feed_first <= feed.first)
                difference(out, "missing_native_burst_in_inactive_feed", feed.first, "retained expected burst", "receiver inactive");
            continue;
        }
        if (active_ended) throw SetupError("receiver fixture attempts reactivation");
        if (!receiver) {
            counts.stage = "receiver_construction";
            if (feed.first != fixture.input_origin) throw SetupError("first active feed is not the original receiver admission origin");
            receiver = std::make_unique<datapump::modem::StreamingReceiver>(config, static_cast<std::size_t>(fixture.constructor_grant), search);
            if (receiver->clock_windowed()) throw SetupError("captured FFT fixture constructed a correlator fallback");
            out.line("{\"event\":\"receiver_constructed\",\"feed_first\":"+std::to_string(feed.first)+
                ",\"working_bytes\":"+std::to_string(receiver->working_bytes())+
                ",\"reserved_workspace_bytes\":"+std::to_string(receiver->reserved_workspace_bytes())+
                ",\"local_clock_fallback\":"+(receiver->local_clock_fallback()?"true":"false")+"}");
            active_seen = true;
        }
        ++counts.active_feeds;
        counts.stage = "receiver_push";
        if (receiver->try_skip_unobserved(raw.size())) throw SetupError("ordinary FFT fixture unexpectedly skipped input");
        receiver->push(raw,std::span(projected).first(raw.size()));
        counts.stage = "receiver_process_pending";
        receiver->process_pending();
        counts.stage = "receiver_public_drain";
        for (const auto& burst : receiver->take_pattern_bursts()) {
            if (++counts.native_events > maximum_native_events) throw SetupError("native output event cap exhausted");
            native_output(out,fixture,feed,burst,counts.native_events);
            // Live's original publication predicate ignores only these no-ops.
            if (burst.bits.empty() && !burst.missing_slots && !burst.complete) continue;
            if (expected_index == fixture.expected.size()) difference(out,"unexpected_native_burst",feed.first,"no further burst","extra native output");
            compare(out,fixture,feed,fixture.expected[expected_index],burst);
            ++expected_index; ++counts.compared_events;
            counts.terminal_events += burst.complete;
        }
        if (expected_index < fixture.expected.size() && fixture.expected[expected_index].feed_first <= feed.first)
            difference(out,"missing_native_burst",feed.first,"retained expected burst","no corresponding native output");
    }
    if (!active_seen || !counts.terminal_events || expected_index != fixture.expected.size())
        throw Difference("retained physically complete output prefix not exhausted exactly");
    counts.stage = "matched_captured_prefix";
    // Do not call finish/flush: captured, fully scored absence supplies completion.
}

// This comparison is independent of drainable chunk grouping. Symbol addresses
// and order remain authoritative; samples hidden inside an original chunk are
// not fabricated by interpolating its endpoints.
struct Canonical {
    const replay::Fixture& fixture;
    Output& out;
    std::string expected_bits;
    std::uint64_t cursor = 0, first_symbol = 0, compared_slots = 0;
    std::uint64_t start_anchors = 0, end_anchors = 0;
    std::uint64_t unrecorded_start_coordinates = 0, unrecorded_end_witnesses = 0;
    bool terminal_seen = false;
    const replay::ExpectedBurst& terminal;

    Canonical(Output& output, const replay::Fixture& value)
        : fixture(value), out(output), terminal(value.expected.back()) {
        first_symbol = cursor = value.expected.front().chunk_first_symbol;
        const auto& first = value.expected.front();
        if (first.chunk_first_symbol != first.stream_first_symbol || !terminal.frequency_hz)
            throw SetupError("canonical fixture lacks its stable initial address or terminal frequency");
        std::uint64_t next = first_symbol, prior_feed = 0;
        for (std::size_t index=0; index<value.expected.size(); ++index) {
            const auto& burst = value.expected[index];
            if (burst.physical_first != first.physical_first || burst.stream_first_symbol != first.stream_first_symbol ||
                burst.stream_phase_samples != first.stream_phase_samples || burst.chunk_first_symbol != next || (index && burst.feed_first<prior_feed) ||
                (burst.complete != (index+1==value.expected.size())) ||
                (!burst.wire_bits.empty() && burst.missing_slots) || !burst.frequency_hz ||
                !same(*burst.frequency_hz,*terminal.frequency_hz))
                throw SetupError("canonical fixture is not one contiguous, frequency-stable captured stream");
            if (burst.missing_slots > maximum_samples-expected_bits.size() || burst.wire_bits.size() > maximum_samples-expected_bits.size()-burst.missing_slots)
                throw SetupError("canonical symbol inventory exceeds retained sample cap");
            for (const auto bit : burst.wire_bits) {
                if (bit!='0' && bit!='1' && bit!='?') throw SetupError("invalid captured wire symbol");
                expected_bits += bit;
            }
            expected_bits.append(static_cast<std::size_t>(burst.missing_slots),'?');
            const auto count = burst.wire_bits.size()+burst.missing_slots;
            if (count > UINT64_MAX-next) throw SetupError("canonical captured symbol coordinate overflow");
            next += count; prior_feed=burst.feed_first;
        }
    }

    void end_feed(const replay::Feed& feed) {
        std::uint64_t expected_slots=0; bool expected_complete=false;
        for (const auto& burst : fixture.expected) {
            if (burst.feed_first>feed.first) break;
            expected_slots+=burst.wire_bits.size()+burst.missing_slots;
            expected_complete|=burst.complete;
        }
        equal_integer(out,"canonical_prefix_slots_at_original_drain",feed.first,expected_slots,compared_slots);
        equal_integer(out,"canonical_completion_at_original_drain",feed.first,expected_complete,terminal_seen);
    }

    void accept(const replay::Feed& feed, const datapump::modem::PatternBurst& actual) {
        if (terminal_seen) difference(out,"canonical_output_after_completion",feed.first,"none","additional output");
        const auto& first = fixture.expected.front();
        equal_integer(out,"canonical_physical_first",feed.first,first.physical_first,physical(fixture.input_origin,actual.stream_first_sample));
        equal_integer(out,"canonical_stream_first_symbol",feed.first,first.stream_first_symbol,actual.stream_first_symbol);
        equal_integer(out,"canonical_stream_phase_samples",feed.first,first.stream_phase_samples,actual.stream_phase_samples);
        equal_integer(out,"canonical_chunk_first_symbol",feed.first,cursor,actual.first_stream_symbol);
        equal_double(out,"canonical_frequency_hz",feed.first,*terminal.frequency_hz,actual.frequency_hz);
        if (!actual.bits.empty() && actual.missing_slots) throw SetupError("native chunk combines wire symbols and compact missing slots");
        if (actual.bits.size()>maximum_samples || actual.missing_slots>maximum_samples-actual.bits.size())
            throw SetupError("native canonical symbol inventory exceeds retained cap");
        const auto count=static_cast<std::uint64_t>(actual.bits.size()+actual.missing_slots);
        if (cursor<first_symbol || count>expected_bits.size() || cursor-first_symbol>expected_bits.size()-count)
            difference(out,"canonical_extra_symbols",feed.first,std::to_string(expected_bits.size()),std::to_string(cursor-first_symbol+count));
        auto actual_bits=wire(actual);
        if (actual.missing_slots) actual_bits.append(actual.missing_slots,'?');
        for (std::size_t index=0;index<actual_bits.size();++index) {
            const auto address=cursor+index;
            if (actual_bits[index]!=expected_bits[static_cast<std::size_t>(address-first_symbol)])
                difference(out,"canonical_wire_at_symbol_"+std::to_string(address),feed.first,
                    std::string(1,expected_bits[static_cast<std::size_t>(address-first_symbol)]),std::string(1,actual_bits[index]));
        }
        bool start_known=false, end_known=false;
        for (const auto& expected : fixture.expected) {
            if (expected.chunk_first_symbol==cursor && expected.physical_chunk_first) {
                equal_integer(out,"canonical_physical_chunk_first",feed.first,*expected.physical_chunk_first,physical(fixture.input_origin,actual.first_sample));
                start_known=true;
                break;
            }
        }
        const auto next=cursor+count;
        for (const auto& expected : fixture.expected) {
            const auto expected_end=expected.chunk_first_symbol+expected.wire_bits.size()+expected.missing_slots;
            // An empty terminal is compared separately; pending witnesses at the
            // same symbol count are meaningful before physical completion too.
            if (!expected.complete && expected_end==next) {
                equal_integer(out,"canonical_physical_chunk_end",feed.first,expected.physical_end,physical(fixture.input_origin,actual.end_sample));
                equal_double(out,"canonical_score_at_symbol_"+std::to_string(next),feed.first,expected.score,actual.score);
                equal_double(out,"canonical_support_at_symbol_"+std::to_string(next),feed.first,expected.support_samples,actual.support_samples);
                end_known=true;
                break;
            }
        }
        start_anchors+=start_known; end_anchors+=end_known;
        unrecorded_start_coordinates+=!start_known; unrecorded_end_witnesses+=!end_known && !actual.complete;
        cursor=next; compared_slots+=count;
        if (actual.complete) {
            equal_integer(out,"canonical_terminal_feed_first",feed.first,terminal.feed_first,feed.first);
            equal_integer(out,"canonical_terminal_next_symbol",feed.first,first_symbol+expected_bits.size(),cursor);
            equal_integer(out,"canonical_terminal_physical_end",feed.first,terminal.physical_end,physical(fixture.input_origin,actual.end_sample));
            if (terminal.physical_chunk_first)
                equal_integer(out,"canonical_terminal_physical_chunk_first",feed.first,*terminal.physical_chunk_first,physical(fixture.input_origin,actual.first_sample));
            equal_double(out,"canonical_terminal_score",feed.first,terminal.score,actual.score);
            equal_double(out,"canonical_terminal_support_samples",feed.first,terminal.support_samples,actual.support_samples);
            terminal_seen=true;
        }
    }

    void finish() {
        if (!terminal_seen || compared_slots!=expected_bits.size())
            difference(out,"canonical_missing_terminal_or_symbols",fixture.feeds.back().first,
                std::to_string(expected_bits.size())+" slots and physical completion",std::to_string(compared_slots)+" slots; terminal="+(terminal_seen?"true":"false"));
        std::ostringstream row;
        row << "{\"event\":\"canonical_result\",\"matched\":true,\"symbol_slots\":" << compared_slots
            << ",\"wire_bits\":" << quote(expected_bits) << ",\"matched_chunk_start_anchors\":" << start_anchors
            << ",\"matched_chunk_end_score_support_anchors\":" << end_anchors
            << ",\"unrecorded_internal_chunk_start_coordinates\":" << unrecorded_start_coordinates
            << ",\"unrecorded_internal_chunk_end_score_support\":" << unrecorded_end_witnesses
            << ",\"terminal_fields_exact\":true,\"all_chunk_physical_geometry_claimed\":false}";
        out.line(row.str());
    }
};

void run_partition(Output& out, const replay::Fixture& fixture, const std::filesystem::path& base,
                   Counts& counts, std::string_view partition) {
    if (fixture.feeds.empty() || fixture.expected.empty() || !fixture.make_config || !fixture.make_search)
        throw SetupError("fixture is missing feeds, golden bursts or config builders");
    if (fixture.feeds.size()>8192 || fixture.expected.size()>maximum_native_events ||
        !fixture.expected.back().complete || fixture.feeds.back().first!=fixture.expected.back().feed_first)
        throw SetupError("fixture bounds or retained terminal feed are invalid");
    const auto pcm=prepare("retained PCM",[&]{return read_pcm(resolve(base,fixture.pcm_path),fixture.pcm_sha256);});
    const auto keys=prepare("retained keyring",[&]{return datapump::load_keyring(resolve(base,fixture.keyfile_path));});
    std::optional<datapump::Crypto> selected_key;
    for (const auto& item : keys) if (key_tag(item.key)==fixture.key_tag_hex) {
        if (selected_key) throw SetupError("key HMAC tag is ambiguous in retained keyring");
        selected_key=item.key;
    }
    if (!selected_key) throw SetupError("retained keyring has no matching HMAC tag");
    const auto config=prepare("captured Config",[&]{return fixture.make_config(*selected_key);});
    const auto search=prepare("captured PatternSearch",[&]{return fixture.make_search();});
    if (!config.data_key || key_tag(*config.data_key)!=fixture.key_tag_hex || config.stream_epoch!=fixture.epoch ||
        config.symbol_burst.enabled() || config.fake_hopping || config.scheduled_hopping || search.scheduled_capture || search.compact_clock_search ||
        search.input_sample_origin!=fixture.input_origin || !fixture.constructor_grant || fixture.constructor_grant>std::numeric_limits<std::size_t>::max())
        throw SetupError("generated Config/Search/key/epoch/grant context is invalid or unsupported");
    Canonical canonical(out,fixture);
    out.line("{\"event\":\"case_begin\",\"label\":"+quote(fixture.label)+",\"partition\":"+quote(partition)+
        ",\"comparison\":\"addressed canonical stream and retained physical witnesses\",\"receiver_id\":"+std::to_string(fixture.receiver_id)+
        ",\"pcm_sha256\":"+quote(fixture.pcm_sha256)+",\"input_origin\":"+std::to_string(fixture.input_origin)+
        ",\"constructor_grant\":"+std::to_string(fixture.constructor_grant)+
        ",\"native_storage_context_replayed\":false,\"dynamic_workspace_replayed\":false,\"original_projection_schedule\":true,\"exact_original_chunks_required\":"+(partition=="unsplit"?"true":"false")+"}");

    // The entire projected prefix is computed ONCE at the original feed
    // normalization boundaries, before any partitioned receiver push occurs.
    counts.stage="original_projection_precompute";
    std::vector<std::complex<double>> projected;
    std::complex<double> mixer{1,0};
    const auto rotation=std::polar(1.,-2*std::numbers::pi*config.carrier_hz/config.sample_rate);
    std::uint64_t end=0,pcm_end=fixture.feeds.front().pcm_offset;
    for (const auto& feed : fixture.feeds) {
        if (feed.first!=end || feed.pcm_offset!=pcm_end || !feed.count || feed.count>2048 || feed.first>UINT64_MAX-feed.count ||
            feed.pcm_offset>pcm.size() || feed.count>pcm.size()-feed.pcm_offset || feed.count>maximum_samples-projected.size())
            throw SetupError("noncontiguous or out-of-range retained feed");
        equal_double(out,"mixer_real_before",feed.first,feed.mixer_real,mixer.real());
        equal_double(out,"mixer_imag_before",feed.first,feed.mixer_imag,mixer.imag());
        const auto raw=std::span(pcm).subspan(static_cast<std::size_t>(feed.pcm_offset),static_cast<std::size_t>(feed.count));
        const auto offset=projected.size(); projected.resize(offset+raw.size());
        for (std::size_t i=0;i<raw.size();++i) {
            projected[offset+i]=static_cast<double>(raw[i])*mixer;mixer*=rotation;
        }
        mixer/=std::abs(mixer);
        equal_double(out,"mixer_real_after",feed.first,feed.after_real,mixer.real());
        equal_double(out,"mixer_imag_after",feed.first,feed.after_imag,mixer.imag());
        ++counts.feeds; end=feed.first+feed.count;pcm_end=feed.pcm_offset+feed.count;
    }
    const auto projected_bytes=std::as_bytes(std::span(projected));
    std::array<unsigned char,EVP_MAX_MD_SIZE> projected_digest{};unsigned projected_digest_size=0;
    if (EVP_Digest(projected_bytes.data(),projected_bytes.size(),projected_digest.data(),&projected_digest_size,EVP_sha256(),nullptr)!=1 || projected_digest_size!=32)
        throw SetupError("fixed projected waveform SHA256 failed");
    constexpr char digits[]="0123456789abcdef";std::string projected_hash;
    for (unsigned i=0;i<projected_digest_size;++i) {projected_hash+=digits[projected_digest[i]>>4];projected_hash+=digits[projected_digest[i]&15];}
    out.line("{\"event\":\"projection_ready\",\"samples\":"+std::to_string(projected.size())+
        ",\"original_feeds\":"+std::to_string(counts.feeds)+",\"all_mixer_witnesses_exact\":true,\"projected_native_complex_sha256\":"+quote(projected_hash)+"}");
    std::unique_ptr<datapump::modem::StreamingReceiver> receiver;
    bool active_seen=false,active_ended=false;
    std::size_t exact_expected_index=0;
    constexpr std::array<std::size_t,5> ragged{17,1,233,3,1024};
    for (const auto& feed : fixture.feeds) {
        if (!feed.receiver_active) {
            if (active_seen) {active_ended=true;receiver.reset();}
            continue;
        }
        if (active_ended) throw SetupError("receiver fixture attempts reactivation");
        if (!receiver) {
            counts.stage="receiver_construction";
            if (feed.first!=fixture.input_origin) throw SetupError("first active feed is not the original receiver admission origin");
            receiver=std::make_unique<datapump::modem::StreamingReceiver>(config,static_cast<std::size_t>(fixture.constructor_grant),search);
            if (receiver->clock_windowed()) throw SetupError("captured FFT fixture constructed a correlator fallback");
            out.line("{\"event\":\"receiver_constructed\",\"feed_first\":"+std::to_string(feed.first)+
                ",\"working_bytes\":"+std::to_string(receiver->working_bytes())+
                ",\"reserved_workspace_bytes\":"+std::to_string(receiver->reserved_workspace_bytes())+
                ",\"local_clock_fallback\":"+(receiver->local_clock_fallback()?"true":"false")+"}");
            active_seen=true;
        }
        ++counts.active_feeds;
        const auto raw=std::span(pcm).subspan(static_cast<std::size_t>(feed.pcm_offset),static_cast<std::size_t>(feed.count));
        const auto mixed=std::span(projected).subspan(static_cast<std::size_t>(feed.first),static_cast<std::size_t>(feed.count));
        counts.stage="partitioned_receiver_push";
        // Keep Live's unobserved query at its original feed boundary. Only push
        // itself is subdivided; no new pending processing or drain is inserted.
        if (receiver->try_skip_unobserved(raw.size())) throw SetupError("ordinary FFT fixture unexpectedly skipped input");
        for (std::size_t offset=0,part=0;offset<raw.size();++part) {
            const auto count=std::min(raw.size()-offset,partition=="unsplit"?raw.size():(partition=="bins512"?std::size_t{512}:ragged[part%ragged.size()]));
            out.line("{\"event\":\"receiver_push_partition\",\"original_feed_first\":"+std::to_string(feed.first)+
                ",\"part\":"+std::to_string(part)+",\"first\":"+std::to_string(feed.first+offset)+",\"count\":"+std::to_string(count)+"}");
            receiver->push(raw.subspan(offset,count),mixed.subspan(offset,count));
            ++counts.partition_push_calls;offset+=count;
        }
        counts.stage="receiver_process_pending";receiver->process_pending();
        counts.stage="receiver_public_drain";
        const auto bursts=receiver->take_pattern_bursts();
        // Retain the entire public drain batch before comparing its first
        // difference. No subsequent push occurs after a comparison failure.
        for (const auto& burst : bursts) {
            if (++counts.native_events>maximum_native_events) throw SetupError("native output event cap exhausted");
            native_output(out,fixture,feed,burst,counts.native_events);
        }
        for (const auto& burst : bursts) {
            if (burst.bits.empty() && !burst.missing_slots && !burst.complete) continue;
            if (partition=="unsplit") {
                if (exact_expected_index==fixture.expected.size())
                    difference(out,"unsplit_unexpected_native_burst",feed.first,"no further burst","extra native output");
                compare(out,fixture,feed,fixture.expected[exact_expected_index],burst);
                ++exact_expected_index;
            }
            canonical.accept(feed,burst);++counts.compared_events;counts.terminal_events+=burst.complete;
        }
        if (partition=="unsplit" && exact_expected_index<fixture.expected.size() && fixture.expected[exact_expected_index].feed_first<=feed.first)
            difference(out,"unsplit_missing_native_burst",feed.first,"retained expected burst","no corresponding native output");
        canonical.end_feed(feed);
    }
    counts.stage="canonical_final_comparison";
    if (!active_seen) throw SetupError("fixture never admitted its receiver");
    canonical.finish();
    if (partition=="unsplit" && exact_expected_index!=fixture.expected.size())
        throw Difference("unsplit control did not exhaust every exact retained chunk");
    out.line("{\"event\":\"partition_result\",\"partition\":"+quote(partition)+",\"push_calls\":"+std::to_string(counts.partition_push_calls)+
        ",\"original_active_feeds\":"+std::to_string(counts.active_feeds)+",\"process_and_drain_only_after_original_feeds\":true,\"eof_completion_used\":false}");
    counts.stage="matched_canonical_captured_prefix";
}

} // namespace

int main(int argc, char** argv) {
    const auto started = Clock::now();
    const auto cpu_started = std::clock();
    std::filesystem::path base, output_path;
    std::string_view partition="original";
    for (int i=1;i<argc;++i) {
        const std::string_view option(argv[i]);
        if ((option=="--fixture-base" || option=="--output") && i+1<argc) {
            auto& target = option=="--fixture-base" ? base : output_path; target = argv[++i];
        } else if (option=="--partition" && i+1<argc) {
            partition=argv[++i];
            if (partition!="original" && partition!="unsplit" && partition!="ragged" && partition!="bins512") { std::cerr << "invalid partition mode\n"; return 2; }
        } else { std::cerr << "usage: adapter --fixture-base ABS --output NEW_JSONL [--partition original|unsplit|ragged|bins512]\n"; return 2; }
    }
    if (!base.is_absolute() || !output_path.is_absolute()) { std::cerr << "fixture base and output must be absolute\n"; return 2; }
    std::unique_ptr<Output> output;
    Counts counts;
    int result = 0; std::string status = "passed", error;
    try {
        output = std::make_unique<Output>(output_path);
        if (partition=="original") run(*output,COPIED_HELP_FIXTURE_NAMESPACE::fixture(),base,counts);
        else run_partition(*output,COPIED_HELP_FIXTURE_NAMESPACE::fixture(),base,counts,partition);
    } catch (const Difference& failure) { result=1;status="mismatch";error=failure.what(); }
      catch (const SetupError& failure) { result=2;status="setup_error";error=failure.what(); }
      catch (const std::exception& failure) { result=3;status="receiver_error";error=failure.what(); }
    struct rusage resources{};
    const bool usage_ok = getrusage(RUSAGE_SELF,&resources)==0;
    const auto wall = std::chrono::duration<double>(Clock::now()-started).count();
    const auto cpu = static_cast<double>(std::clock()-cpu_started)/CLOCKS_PER_SEC;
    bool closed = false;
    if (output) {
        try {
            std::ostringstream summary;
            summary << std::setprecision(17) << "{\"event\":\"case_result\",\"status\":" << quote(status)
                << ",\"partition\":" << quote(partition) << ",\"matched\":" << (result==0?"true":"false") << ",\"error\":" << quote(error) << ",\"stage\":" << quote(counts.stage)
                << ",\"feeds\":" << counts.feeds << ",\"active_feeds\":" << counts.active_feeds << ",\"native_events\":" << counts.native_events
                << ",\"partition_push_calls\":" << counts.partition_push_calls << ",\"compared_events\":" << counts.compared_events << ",\"terminal_events\":" << counts.terminal_events
                << ",\"wall_seconds\":" << wall << ",\"process_cpu_seconds\":" << cpu << ",\"rusage_valid\":" << (usage_ok?"true":"false")
                << ",\"max_rss_kib_linux\":" << resources.ru_maxrss << ",\"eof_completion_used\":false,\"original_C_resolved\":false}";
            output->line(summary.str()); closed=output->close();
        } catch (const std::exception& failure) { error += "; output: "; error += failure.what(); result=2;status="output_incomplete"; closed=output->close(); }
    }
    if (output && !closed) { result=2;status="output_incomplete"; }
    std::cout << "{\"event\":\"replay_output_finalization\",\"status\":" << quote(status)
        << ",\"flush_ok\":" << (output&&output->flush_ok()?"true":"false")
        << ",\"close_ok\":" << (output&&output->close_ok()?"true":"false") << "}\n";
    if (!error.empty()) std::cerr << error << '\n';
    return result;
}
