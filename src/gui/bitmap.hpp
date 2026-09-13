#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace datapump::gui {
enum class PixelFormat { gray8, mono1, rgb24 };
struct PixelBlock {
    unsigned width = 0, height = 0;
    std::size_t stride_bytes = 0;
    PixelFormat format = PixelFormat::gray8;
    const unsigned char* pixels = nullptr;
};
struct PixelRect { unsigned x = 0, y = 0, width = 0, height = 0; };
struct BitmapRequest {
    unsigned width = 0, height = 0;
    // Zero-sized damage paints nothing. Use full_bitmap_request for full paint.
    PixelRect damage;
    // Physical sample width / height; pixel displays normally supply 1.
    double sample_aspect_ratio = 1;
    bool monochrome = false, supports_rgb24 = false;
};
inline BitmapRequest full_bitmap_request(unsigned width, unsigned height,
                                         bool monochrome = false, bool supports_rgb24 = false) {
    return {width, height, {0, 0, width, height}, 1, monochrome, supports_rgb24};
}
// Pixel storage is borrowed only for the duration of this synchronous call.
using BitmapSink = std::function<void(unsigned x, unsigned y, PixelBlock)>;
// Opaque, immutable producer handle. Native adapters request pixels and copy
// the borrowed blocks; creating or interpreting source data belongs above this
// boundary. Copies retain the producer's snapshot for later damage repaints.
class BitmapSource {
public:
    using Paint = std::function<void(const BitmapRequest&,const BitmapSink&,bool)>;
    BitmapSource() = default;
    explicit BitmapSource(Paint paint) : paint_(std::move(paint)) {}
    void paint(const BitmapRequest& request,const BitmapSink& sink,bool color_enabled=true) const {
        if(paint_)paint_(request,sink,color_enabled);
    }
private:
    Paint paint_;
};
inline std::size_t pixel_row_bytes(unsigned width, PixelFormat format) {
    switch (format) {
    case PixelFormat::gray8: return width;
    case PixelFormat::mono1: return (static_cast<std::size_t>(width) + 7) / 8;
    case PixelFormat::rgb24:
        if (width > std::numeric_limits<std::size_t>::max() / 3) throw std::length_error("bitmap row too large");
        return static_cast<std::size_t>(width) * 3;
    }
    throw std::invalid_argument("unknown bitmap pixel format");
}
inline void validate_pixel_block(PixelBlock block) {
    const auto row = pixel_row_bytes(block.width, block.format);
    if (block.stride_bytes < row) throw std::invalid_argument("bitmap stride is shorter than a row");
    if (block.width && block.height && !block.pixels) throw std::invalid_argument("bitmap pixels are null");
    if (block.height && block.stride_bytes > std::numeric_limits<std::size_t>::max() / block.height)
        throw std::length_error("bitmap storage too large");
}

// Backend-owned transfer storage. blit copies borrowed pixels immediately,
// honors padded strides/MSB-first Mono1 and replaces rectangles at 1:1 scale.
// Gray8 -> Mono1 thresholds at 128. Producers avoid RGB when unavailable;
// this converter also permits a color backend to receive Gray8/Mono1.
class BitmapImage {
public:
    BitmapImage() = default;
    BitmapImage(unsigned width, unsigned height, PixelFormat format = PixelFormat::rgb24)
        : width_(width), height_(height), format_(format), stride_(pixel_row_bytes(width, format)) {
        if (height && stride_ > std::numeric_limits<std::size_t>::max() / height)
            throw std::length_error("bitmap storage too large");
        pixels_.resize(stride_ * height);
    }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    PixelFormat format() const { return format_; }
    const std::vector<unsigned char>& pixels() const { return pixels_; }
    PixelBlock block() const { return {width_, height_, stride_, format_, pixels_.data()}; }
    void blit(unsigned x, unsigned y, PixelBlock source) {
        validate_pixel_block(source);
        if (x > width_ || y > height_ || source.width > width_ - x || source.height > height_ - y)
            throw std::out_of_range("bitmap blit is outside its sample grid");
        if (!source.width || !source.height) return;
        for (unsigned row = 0; row < source.height; ++row) {
            const auto* input = source.pixels + source.stride_bytes * row;
            auto* output = pixels_.data() + stride_ * (y + row);
            for (unsigned col = 0; col < source.width; ++col) {
                unsigned char r, g, b;
                if (source.format == PixelFormat::rgb24) {
                    r = input[3 * static_cast<std::size_t>(col)];
                    g = input[3 * static_cast<std::size_t>(col) + 1];
                    b = input[3 * static_cast<std::size_t>(col) + 2];
                } else {
                    r = source.format == PixelFormat::gray8 ? input[col] :
                        ((input[col / 8] & (0x80U >> (col % 8))) ? 255 : 0);
                    g = b = r;
                }
                const auto target = static_cast<std::size_t>(x) + col;
                if (format_ == PixelFormat::rgb24) {
                    output[3 * target] = r; output[3 * target + 1] = g; output[3 * target + 2] = b;
                } else {
                    const auto gray = static_cast<unsigned char>((77U * r + 150U * g + 29U * b + 128) / 256);
                    if (format_ == PixelFormat::gray8) output[target] = gray;
                    else {
                        const auto mask = static_cast<unsigned char>(0x80U >> (target % 8));
                        if (gray >= 128) output[target / 8] |= mask;
                        else output[target / 8] &= static_cast<unsigned char>(~mask);
                    }
                }
            }
        }
    }
private:
    unsigned width_ = 0, height_ = 0;
    PixelFormat format_ = PixelFormat::rgb24;
    std::size_t stride_ = 0;
    std::vector<unsigned char> pixels_;
};
}
