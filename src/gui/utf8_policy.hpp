#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::gui {
// Text crossing a native editor or clipboard boundary is UTF-8 with no embedded
// zero byte. This validation has no application model or toolkit dependencies.
inline bool valid_clipboard_text(std::span<const std::uint8_t> bytes) noexcept {
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
}
