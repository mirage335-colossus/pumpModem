#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>

namespace datapump::fast::detail {

// Display only. Select evenly across the actual pre-slice data estimates.
class SsbDisplayPoints {
public:
    explicit SsbDisplayPoints(std::size_t data_symbols)
        : stride_(std::max<std::size_t>(1,(data_symbols+capacity-1)/capacity)) {}

    void record(std::size_t data_symbol_index,std::complex<double> point) noexcept {
        if(data_symbol_index%stride_ || count_==samples_.size())return;
        const auto value=static_cast<std::complex<float>>(point);
        if(std::isfinite(value.real()) && std::isfinite(value.imag()))samples_[count_++]=value;
    }

    void clear() noexcept { count_=0; }
    std::span<const std::complex<float>> samples() const noexcept {
        return std::span(samples_).first(count_);
    }

private:
    static constexpr std::size_t capacity=512;
    const std::size_t stride_;
    std::array<std::complex<float>,capacity> samples_{};
    std::size_t count_=0;
};

} // namespace datapump::fast::detail
