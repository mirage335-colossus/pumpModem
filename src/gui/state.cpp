#include "state.hpp"
#include <algorithm>

namespace datapump::gui {
Inbox::Inbox(std::size_t capacity) : capacity_(capacity) {
    if (!capacity) throw Error("Receive cache capacity must be positive");
}
void Inbox::put(DecodedPacket packet) {
    if (packet.message.data.size() > capacity_) throw Error("Received message exceeds the cache capacity");
    auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.message.id == packet.message.id;
    });
    if (existing != items_.end()) {
        used_ -= existing->message.data.size();
        items_.erase(existing);
    }
    while (!items_.empty() &&
           (packet.message.data.size() > capacity_ - used_ || items_.size() >= 4096)) {
        used_ -= items_.front().message.data.size();
        items_.pop_front();
    }
    used_ += packet.message.data.size();
    items_.push_back(std::move(packet));
}
void Inbox::clear() noexcept { items_.clear(); used_ = 0; }

void Signals::update(SignalLine line) {
    if (line.text.size()>4096) line.text.resize(4096);
    const auto found=std::find_if(lines_.begin(),lines_.end(),[&](const auto& item) { return item.id==line.id; });
    if (found!=lines_.end()) {
        if (found->validated && !line.validated) return;
        *found=std::move(line);
    } else {
        if (lines_.size()>=64) lines_.pop_front();
        lines_.push_back(std::move(line));
    }
}
std::optional<std::string> Signals::copy_id(std::size_t index) const {
    if (index>=lines_.size() || !lines_[index].validated || lines_[index].packet_id.empty()) return std::nullopt;
    return lines_[index].packet_id;
}

bool valid_clipboard_text(std::span<const std::uint8_t> bytes) noexcept {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = bytes[i++];
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned continuation;
        std::uint32_t value;
        std::uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) { continuation=1; value=first&0x1f; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation=2; value=first&0x0f; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation=3; value=first&7; minimum=0x10000; }
        else return false;
        if (continuation > bytes.size() - i) return false;
        for (unsigned n=0; n<continuation; ++n) {
            const auto next=bytes[i++];
            if ((next&0xc0) != 0x80) return false;
            value=(value<<6)|(next&0x3f);
        }
        if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    }
    return true;
}
std::string id_label(const Message& message) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;
    for (auto value:message.id) { result+=digits[value>>4]; result+=digits[value&15]; }
    return result;
}
std::string display_label(std::string_view text) {
    std::string result;
    for (const char value:text) {
        const auto byte=static_cast<unsigned char>(value);
        result += byte<32 || byte==127 ? ' ' : value;
    }
    return result;
}
}
