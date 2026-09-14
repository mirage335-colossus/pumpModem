#pragma once

#include "datapump/packet.hpp"
#include "utf8_policy.hpp"
#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace datapump::gui {
// Each returned byte is one exact bit, in entry order. Whitespace separates
// groups but never pads, truncates or removes leading zero bits.
Bytes parse_binary_bits(std::string_view text);

// The UI inserts only successfully decoded packets. Diagnostic sample buffers
// are displayed separately and are never retained for every received message.
class Inbox {
public:
    explicit Inbox(std::size_t capacity = default_memory_limit);
    void put(DecodedPacket packet);
    void clear() noexcept;
    const std::deque<DecodedPacket>& items() const noexcept { return items_; }
    std::vector<const DecodedPacket*> file_items() const;
    std::size_t size_bytes() const noexcept { return used_; }
private:
    std::size_t capacity_;
    std::size_t used_ = 0;
    std::deque<DecodedPacket> items_;
};

// The nonce separation delay applies only to encrypted output sent to hardware.
// Simulation and plain output still share the single active-transmission slot.
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
    bool active_encrypted_output_ = false;
    Clock::time_point next_encrypted_{};
};

struct PlotUpdate {
    bool update_plots = false;
    bool append_waterfall = false;
    bool clear_waterfall = false;
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
    std::string packet_id;
    bool text_message = true;
    std::optional<double> preamble_received_percent = std::nullopt;
    std::optional<PacketBitAccuracy> pre_fec_accuracy = std::nullopt;
    bool binary = false;
    bool complete = false;
    std::size_t received_bits = 0;
    std::size_t expected_bits = 0;
    std::optional<double> pattern_score;
};
std::string signal_status_label(const SignalLine& line);
std::string signal_preamble_label(const SignalLine& line);
std::string signal_data_label(const SignalLine& line);
// Pending decoder observations can be replaced as more symbols/parity arrive.
// Verified packets have clipboard lookup identities. Complete raw binary
// observations have a separate bit-string path with no packet/authentication ID.
class Signals {
public:
    void update(SignalLine line);
    void clear() noexcept { lines_.clear(); }
    const std::deque<SignalLine>& lines() const noexcept { return lines_; }
    std::optional<std::string> copy_id(std::size_t index) const;
    std::optional<std::string> copy_bits(std::size_t index) const;
    std::optional<std::string> copy_text(std::size_t index) const;
private:
    std::deque<SignalLine> lines_;
};

std::string id_label(const Message& message);
std::string display_label(std::string_view text);
}
