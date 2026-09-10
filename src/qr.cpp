#include "datapump/qr.hpp"

#include "qrcodegen.hpp"

#include <sstream>
#include <utility>

namespace datapump {
namespace {
void validate_text(std::string_view text) {
    if (text.size() > 2000) throw Error("QR text exceeds 500 Unicode characters");
    std::size_t characters = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[offset++]);
        std::uint32_t codepoint = first;
        std::uint32_t minimum = 0;
        unsigned int following = 0;
        if (first < 0x80) {
            // A single ASCII byte, including an embedded zero, is valid UTF-8.
        } else if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU; following = 1; minimum = 0x80;
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU; following = 2; minimum = 0x800;
        } else if ((first & 0xf8U) == 0xf0U) {
            codepoint = first & 0x07U; following = 3; minimum = 0x10000;
        } else {
            throw Error("QR text is not valid UTF-8");
        }
        for (unsigned int i = 0; i < following; ++i) {
            if (offset == text.size()) throw Error("QR text contains truncated UTF-8");
            const auto byte = static_cast<std::uint8_t>(text[offset++]);
            if ((byte & 0xc0U) != 0x80U) throw Error("QR text is not valid UTF-8");
            codepoint = (codepoint << 6) | (byte & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            throw Error("QR text is not valid UTF-8");
        }
        if (++characters > 500) throw Error("QR text exceeds 500 Unicode characters");
    }
}
void validate_scale(int scale) {
    if (scale < 1 || scale > 32) throw Error("QR scale must be between 1 and 32 pixels per module");
}
} // namespace

QrCode::QrCode(int size, Bytes modules) : size_(size), modules_(std::move(modules)) {}

bool QrCode::dark(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= size_ || y >= size_) return false;
    return modules_[static_cast<std::size_t>(y * size_ + x)] != 0;
}

QrCode encode_qr(std::string_view text) {
    validate_text(text);
    try {
        const Bytes data(text.begin(), text.end());
        const std::vector<qrcodegen::QrSegment> segments{
            qrcodegen::QrSegment::makeEci(26), qrcodegen::QrSegment::makeBytes(data)};
        const auto symbol = qrcodegen::QrCode::encodeSegments(
            segments, qrcodegen::QrCode::Ecc::LOW, 1, 40, -1, false);
        const int size = symbol.getSize();
        Bytes modules(static_cast<std::size_t>(size * size));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                modules[static_cast<std::size_t>(y * size + x)] = symbol.getModule(x, y) ? 1 : 0;
            }
        }
        return QrCode(size, std::move(modules));
    } catch (const std::exception& error) {
        throw Error(std::string("QR encoding failed: ") + error.what());
    }
}

std::string QrCode::svg(int scale) const {
    validate_scale(scale);
    constexpr int quiet_zone = 4;
    const int extent = size_ + quiet_zone * 2;
    std::ostringstream output;
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << extent * scale
           << "\" height=\"" << extent * scale << "\" viewBox=\"0 0 " << extent << ' ' << extent
           << "\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/>"
           << "<path fill=\"black\" d=\"";
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) {
            if (dark(x, y)) output << 'M' << x + quiet_zone << ',' << y + quiet_zone << "h1v1h-1z";
        }
    }
    output << "\"/></svg>\n";
    return output.str();
}

std::string QrCode::pbm(int scale) const {
    validate_scale(scale);
    constexpr int quiet_zone = 4;
    const int pixels = (size_ + quiet_zone * 2) * scale;
    std::string output = "P1\n" + std::to_string(pixels) + ' ' + std::to_string(pixels) + '\n';
    output.reserve(output.size() + static_cast<std::size_t>(pixels) * static_cast<std::size_t>(pixels) * 2);
    for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
            output += dark(x / scale - quiet_zone, y / scale - quiet_zone) ? '1' : '0';
            output += x == pixels - 1 ? '\n' : ' ';
        }
    }
    return output;
}

std::string qr_svg(std::string_view text, int scale) { return encode_qr(text).svg(scale); }
std::string qr_pbm(std::string_view text, int scale) { return encode_qr(text).pbm(scale); }

} // namespace datapump
