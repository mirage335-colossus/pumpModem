#pragma once

#include "datapump/stream_codec.hpp"
#include "utf8_policy.hpp"
#include <chrono>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace datapump::live { struct Snapshot; }
namespace datapump::gui {
// Each returned byte is one exact bit, in entry order. Whitespace separates
// groups but never pads, truncates or removes leading zero bits.
Bytes parse_binary_bits(std::string_view text);

// The UI inserts only successfully decoded streams. Diagnostic sample buffers
// are displayed separately and are never retained for every received message.
class Inbox {
public:
    struct ReceptionIdentity { std::uint64_t signal_id=0, revision=0; };
    explicit Inbox(std::size_t capacity = default_memory_limit);
    void put(StreamContent stream, std::optional<ReceptionIdentity> identity = {});
    void erase(std::string_view reception_id);
    std::vector<std::string> erase_signal(std::uint64_t signal_id);
    std::optional<std::uint64_t> revision(std::uint64_t signal_id) const;
    void clear() noexcept;
    const std::deque<StreamContent>& items() const noexcept { return items_; }
    std::vector<const StreamContent*> file_items() const;
    std::size_t size_bytes() const noexcept { return used_; }
private:
    std::size_t capacity_;
    std::size_t used_ = 0;
    std::deque<StreamContent> items_;
    // Ownership lives as long as the cached source, independently of the
    // shorter visible row history, and shares the cache's item bound.
    std::map<std::array<std::uint8_t,16>,ReceptionIdentity> identities_;
};

// Every hardware transmission shares the six-second symbol-absence separation.
// Simulation supplies its separation as actual sampled silence.
class TransmissionPolicy {
public:
    using Clock = std::chrono::steady_clock;
    void started(bool simulation, bool encrypted, Clock::time_point now = Clock::now());
    void finished(Clock::time_point now = Clock::now()) noexcept;
    void abort_start() noexcept;
    std::chrono::milliseconds remaining(bool simulation, bool encrypted,
                                         Clock::time_point now = Clock::now()) const noexcept;
private:
    bool active_ = false;
    bool active_hardware_output_ = false;
    Clock::time_point next_hardware_{};
};

struct PlotUpdate {
    bool update_plots = false;
    bool append_waterfall = false;
    bool clear_waterfall = false;
    std::optional<std::uint64_t> clear_pattern_scores_through;
};
// Each chronological replay frame is displayed once. Repeated UI polls do not
// append duplicate spectrum rows, and a new replay starts a fresh waterfall.
class PlotReplayPolicy {
public:
    PlotUpdate observe(std::uint64_t sequence, std::uint64_t transmission_id,
                       bool replay, std::size_t replay_frame = 0);
    void reset() noexcept { *this = {}; }
private:
    std::optional<std::uint64_t> sequence_, transmission_;
    std::optional<std::size_t> replay_frame_;
    bool replaying_ = false;
};
std::string format_bit_rate(double bits_per_second);
// Names are comma-separated in the generation dialog. The keyfile codec
// validates printable UTF-8, uniqueness and its production format limits.
std::vector<std::string> key_entry_names(std::string_view text);
// Numbered literal labels distinguish named keys from the unencrypted sentinel.
// Labels are literal UTF-8. Native menu escaping belongs in the adapter.
std::vector<std::string> key_choice_labels(std::span<const std::string> names);
// An absolute, percent-encoded file URI for the native directory launcher.
// No command string is constructed and no keyfile content is exposed.
std::string folder_uri(const std::filesystem::path& directory);

struct SignalLine {
    std::uint64_t id = 0;
    double frequency_hz = 0;
    std::string text;
    bool validated = false;
    std::string reception_id;
    bool text_message = true;
    std::optional<double> preamble_received_percent = std::nullopt;
    std::optional<StreamBitAccuracy> pre_fec_accuracy = std::nullopt;
    bool binary = false;
    bool complete = false;
    std::size_t received_bits = 0;
    std::size_t expected_bits = 0;
    std::optional<double> pattern_score;
    // Exact retained transport bits for decoded pattern text, independently
    // of its message bytes. Empty means the original bits are unavailable.
    std::string raw_bits;
    std::size_t missing_symbols = 0;
    StreamFecStats fec_stats;
    // A stronger competing receive profile may replace even a completed
    // interpretation while retaining this reception's row identity.
    std::uint64_t revision = 0;
};
bool signal_byte_aligned(const SignalLine& line);
std::string signal_display_text(const SignalLine& line);
std::string signal_status_label(const SignalLine& line);
std::string signal_gap_label(const SignalLine& line);
std::string signal_repair_label(const SignalLine& line);
std::string signal_preamble_label(const SignalLine& line);
std::string signal_data_label(const SignalLine& line);
// Pending decoder observations can be replaced as more symbols/parity arrive.
// Verified streams have clipboard lookup identities. Complete raw binary
// observations use a text/byte view when byte-aligned and an exact bit-string
// path otherwise. Neither raw view has a stream/authentication ID.
class Signals {
public:
    bool update(SignalLine line);
    // Merged row identities stay retired within the bounded display history.
    void erase(std::uint64_t id);
    void clear() noexcept { lines_.clear(); retired_ids_.clear(); }
    const std::deque<SignalLine>& lines() const noexcept { return lines_; }
    std::optional<std::string> copy_id(std::size_t index) const;
    std::optional<std::string> copy_bits(std::size_t index) const;
    std::optional<std::string> copy_raw_bits(std::size_t index) const;
    std::optional<std::string> copy_text(std::size_t index) const;
    std::optional<Bytes> copy_bytes(std::size_t index) const;
private:
    std::deque<SignalLine> lines_;
    std::deque<std::uint64_t> retired_ids_;
};

// Apply a receiver poll atomically: retract superseded content before adding
// newly completed sources and publishing their final row interpretations.
void apply_receptions(Inbox& inbox, Signals& signals, live::Snapshot& snapshot);

std::string id_label(const Message& message);
std::string display_label(std::string_view text);
}
