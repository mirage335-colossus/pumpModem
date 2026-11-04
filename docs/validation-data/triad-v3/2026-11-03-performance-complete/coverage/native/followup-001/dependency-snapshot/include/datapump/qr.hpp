#pragma once

#include "datapump/types.hpp"

#include <string_view>

namespace datapump {

// A QR Model 2 symbol with Level L error correction and UTF-8 ECI 26.
// The module matrix excludes its four-module white quiet zone.
class QrCode {
public:
    int size() const noexcept { return size_; }
    bool dark(int x, int y) const noexcept;
    std::string svg(int scale = 4) const;
    std::string pbm(int scale = 4) const;

private:
    QrCode(int size, Bytes modules);
    int size_;
    Bytes modules_;
    friend QrCode encode_qr(std::string_view text);
};

// At most 500 Unicode scalar values, represented as valid UTF-8. Embedded
// zero bytes are preserved. Malformed UTF-8 and longer messages throw Error.
QrCode encode_qr(std::string_view text);
std::string qr_svg(std::string_view text, int scale = 4);
std::string qr_pbm(std::string_view text, int scale = 4);

} // namespace datapump
