#pragma once

#include "datapump/packet.hpp"
#include <deque>
#include <optional>
#include <span>
#include <string>

namespace datapump::gui {
// The UI inserts only successfully decoded packets. Diagnostic sample buffers
// are displayed separately and are never retained for every received message.
class Inbox {
public:
    explicit Inbox(std::size_t capacity = default_memory_limit);
    void put(DecodedPacket packet);
    void clear() noexcept;
    const std::deque<DecodedPacket>& items() const noexcept { return items_; }
    std::size_t size_bytes() const noexcept { return used_; }
private:
    std::size_t capacity_;
    std::size_t used_ = 0;
    std::deque<DecodedPacket> items_;
};

struct SignalLine {
    std::uint64_t id = 0;
    double frequency_hz = 0;
    std::string text;
    bool validated = false;
    std::string packet_id;
};
// Pending decoder observations can be replaced as more symbols/parity arrive.
// Only a final verified observation can provide a clipboard lookup identity.
class Signals {
public:
    void update(SignalLine line);
    void clear() noexcept { lines_.clear(); }
    const std::deque<SignalLine>& lines() const noexcept { return lines_; }
    std::optional<std::string> copy_id(std::size_t index) const;
private:
    std::deque<SignalLine> lines_;
};

// Clipboard text is UTF-8, with no embedded zero byte. Binary messages can
// always be saved explicitly, but are never silently coerced to text.
bool valid_clipboard_text(std::span<const std::uint8_t> bytes) noexcept;
std::string id_label(const Message& message);
std::string display_label(std::string_view text);
}
