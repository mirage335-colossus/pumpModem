#pragma once
#include "datapump/modem.hpp"
#include <algorithm>
#include <complex>
#include <cmath>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>
namespace datapump::modem::detail::search_fft {
// Signal-array precision and execution ISA are separate choices. Timing,
// addressing, covariance and final scoring are deliberately outside this kernel.
// Plans hold public twiddles only; they are reusable for smaller power-of-two FFTs.
// Conservative engineering envelope for unscaled FP32 FFT/convolution.
// The upper bound leaves at least 32x headroom using Cauchy/Parseval for
// products and inverse partial sums. Tiny nonzero inputs use the FP64 path.
inline double float_component_limit(std::size_t reduction) {
    return std::sqrt(static_cast<double>(std::numeric_limits<float>::max()))/
        (8*static_cast<double>(std::max<std::size_t>(1,reduction)));
}
inline bool float_input_safe(std::span<const std::complex<double>> input,std::size_t reduction) {
    double peak=0;
    for(const auto value:input) {
        if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))return false;
        peak=std::max({peak,std::abs(value.real()),std::abs(value.imag())});
    }
    const auto lower=8*std::sqrt(static_cast<double>(std::numeric_limits<float>::min()))*
        static_cast<double>(std::max<std::size_t>(1,reduction));
    return peak==0 || (peak>=lower && peak<=float_component_limit(reduction));
}
template<class T> bool finite(std::span<T> input) {
    for(const auto value:input)if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))return false;
    return true;
}
enum class Kernel { automatic,scalar };
const char* kernel_name(Kernel kernel=Kernel::automatic);
struct FloatPlan {
    explicit FloatPlan(std::size_t maximum,std::stop_token stop={});
    std::size_t maximum=0;
    std::vector<std::complex<float>> twiddles;
    std::size_t working_bytes()const{return twiddles.capacity()*sizeof(std::complex<float>);}
};
struct DoublePlan {
    explicit DoublePlan(std::size_t maximum,std::stop_token stop={});
    std::size_t maximum=0;
    std::vector<std::complex<double>> twiddles;
    std::size_t working_bytes()const{return twiddles.capacity()*sizeof(std::complex<double>);}
};
void fft(std::span<std::complex<float>>,bool inverse,const FloatPlan&,std::stop_token stop={},Kernel=Kernel::automatic);
void fft(std::span<std::complex<double>>,bool inverse,const DoublePlan&,std::stop_token stop={},Kernel=Kernel::automatic);
}
