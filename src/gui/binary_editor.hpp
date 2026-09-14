#pragma once

#include "datapump/types.hpp"
#include "utf8_policy.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace datapump::gui {
// Short binary edits select an exact raw-bit draft, including partial bytes.
// A complete-byte raw draft updates the separate text/byte view; partial bytes
// retain that view until another complete-byte or text edit. For longer text,
// binary edits replace its first 16 bytes without removing the old suffix.
// Native text fields cannot hold arbitrary bytes, so an explicit escaped mode
// preserves zero bytes, control characters and malformed UTF-8 losslessly.
class BinaryEditor {
public:
    static constexpr std::size_t prefix_limit = 16;
    static constexpr std::size_t payload_limit = 1024 * 1024;

    BinaryEditor() = default;
    explicit BinaryEditor(Bytes bytes) { commit(std::move(bytes)); }

    const Bytes& bytes() const noexcept { return bytes_; }
    const std::string& text() const noexcept { return text_; }
    bool escaped() const noexcept { return escaped_; }
    const std::optional<Bytes>& raw_bits() const noexcept { return raw_bits_; }

    std::string binary() const {
        std::string result;
        if (raw_bits_) {
            result.reserve(raw_bits_->size() + raw_bits_->size() / 8);
            for (std::size_t index = 0; index < raw_bits_->size(); ++index) {
                if (index && index % 8 == 0) result += index % 16 ? ' ' : '\n';
                result += (*raw_bits_)[index] ? '1' : '0';
            }
            return result;
        }
        const auto count = std::min(prefix_limit, bytes_.size());
        result.reserve(count * 9);
        for (std::size_t index = 0; index < count; ++index) {
            if (index) result += index % 2 ? ' ' : '\n';
            for (unsigned bit = 8; bit; --bit)
                result += (bytes_[index] >> (bit - 1)) & 1 ? '1' : '0';
        }
        return result;
    }

    void edit_text(std::string_view text) {
        if (!escaped_ && text.size() > payload_limit)
            throw Error("Message exceeds the 1 MiB byte limit");
        const auto input = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
        if (!valid_clipboard_text(input))
            throw Error("Message text must be valid UTF-8 without embedded zero bytes");
        if (!escaped_) {
            commit(Bytes(input.begin(), input.end()));
            return;
        }

        Bytes decoded;
        decoded.reserve(std::min(text.size(), payload_limit));
        for (std::size_t index = 0; index < text.size();) {
            const auto value = static_cast<std::uint8_t>(text[index++]);
            if (value != '\\') decoded.push_back(value);
            else if (index < text.size() && text[index] == '\\') {
                decoded.push_back('\\');
                ++index;
            } else if (index + 2 < text.size() && text[index] == 'x' &&
                       hex_value(text[index + 1]) >= 0 && hex_value(text[index + 2]) >= 0) {
                decoded.push_back(static_cast<std::uint8_t>(
                    hex_value(text[index + 1]) * 16 + hex_value(text[index + 2])));
                index += 3;
            } else throw Error("Escaped message accepts only \\\\ and \\xNN (two hexadecimal digits)");
            if (decoded.size() > payload_limit) throw Error("Message exceeds the 1 MiB byte limit");
        }
        commit(std::move(decoded));
    }

    void edit_binary(std::string_view text) {
        Bytes prefix;
        prefix.reserve(prefix_limit);
        Bytes exact_bits;
        exact_bits.reserve(prefix_limit * 8);
        std::uint8_t value = 0;
        std::size_t bits = 0;
        for (const char character : text) {
            if (character == ' ' || (character >= '\t' && character <= '\r')) continue;
            if (character != '0' && character != '1')
                throw Error("Binary input accepts only 0, 1 and whitespace");
            if (bits == prefix_limit * 8)
                throw Error("Binary input is limited to the first 16 bytes (128 bits)");
            exact_bits.push_back(static_cast<std::uint8_t>(character - '0'));
            value = static_cast<std::uint8_t>((value << 1) | (character - '0'));
            if (++bits % 8 == 0) {
                prefix.push_back(value);
                value = 0;
            }
        }
        if (bytes_.size() <= prefix_limit || raw_bits_) {
            if (exact_bits.empty()) { commit({}); return; }
            if (bits % 8 == 0) commit(std::move(prefix));
            raw_bits_ = std::move(exact_bits);
            return;
        }
        if (bits % 8) throw Error("Complete each binary byte with 8 bits");
        const auto suffix = std::min(prefix_limit, bytes_.size());
        prefix.insert(prefix.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(suffix), bytes_.end());
        commit(std::move(prefix));
    }

private:
    static int hex_value(char value) noexcept {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    // Return one whole displayable UTF-8 character, or zero for a byte that
    // needs escaping. Newlines and tabs are useful literal message content.
    static std::size_t displayable_length(std::span<const std::uint8_t> bytes, std::size_t index) noexcept {
        const auto first = bytes[index];
        if (first < 0x80)
            return (first >= 0x20 && first != 0x7f) || first == '\t' || first == '\n' || first == '\r' ? 1 : 0;
        const std::size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
                                  first >= 0xe0 && first <= 0xef ? 3 :
                                  first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!count || count > bytes.size() - index || !valid_clipboard_text(bytes.subspan(index, count))) return 0;
        // Unicode C1 controls are valid UTF-8 but have no editable glyph.
        if (first == 0xc2 && bytes[index + 1] < 0xa0) return 0;
        return count;
    }

    void commit(Bytes bytes) {
        if (bytes.size() > payload_limit) throw Error("Message exceeds the 1 MiB byte limit");
        bool escaped = false;
        for (std::size_t index = 0; index < bytes.size();) {
            const auto count = displayable_length(bytes, index);
            if (!count) { escaped = true; break; }
            index += count;
        }
        std::string rendered;
        if (!escaped) rendered.assign(bytes.begin(), bytes.end());
        else {
            constexpr char hex[] = "0123456789ABCDEF";
            rendered.reserve(bytes.size());
            for (std::size_t index = 0; index < bytes.size();) {
                const auto count = displayable_length(bytes, index);
                if (count) {
                    if (bytes[index] == '\\') rendered += '\\';
                    rendered.append(reinterpret_cast<const char*>(bytes.data() + index), count);
                    index += count;
                } else {
                    const auto value = bytes[index++];
                    rendered += "\\x";
                    rendered += hex[value >> 4];
                    rendered += hex[value & 15];
                }
            }
        }
        bytes_.swap(bytes);
        text_.swap(rendered);
        escaped_ = escaped;
        raw_bits_.reset();
    }

    Bytes bytes_;
    std::string text_;
    bool escaped_ = false;
    std::optional<Bytes> raw_bits_;
};
}
