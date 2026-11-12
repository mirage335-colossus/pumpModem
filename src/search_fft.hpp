#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <complex>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>
namespace datapump::modem::detail::search_fft {
// Reusable radix-2 twiddle plans for canonical native transforms.
enum class Kernel { automatic,scalar };
const char* kernel_name(Kernel kernel=Kernel::automatic);
struct DoublePlan {
    explicit DoublePlan(std::size_t maximum,std::stop_token stop={});
    std::size_t maximum=0;
    std::vector<std::complex<double>> twiddles;
    std::size_t working_bytes()const{return twiddles.capacity()*sizeof(std::complex<double>);}
};
// Separate post-channelization arithmetic. Twiddles are generated in double
// precision and rounded once for each plan; sensitive scoring and coordinates
// remain outside this FFT interface.
struct FloatPlan {
    explicit FloatPlan(std::size_t maximum,std::stop_token stop={});
    std::size_t maximum=0;
    std::vector<std::complex<float>> twiddles;
    std::size_t working_bytes()const{return twiddles.capacity()*sizeof(std::complex<float>);}
};
void fft(std::span<std::complex<double>>,bool inverse,const DoublePlan&,std::stop_token stop={},Kernel=Kernel::automatic);
void fft(std::span<std::complex<float>>,bool inverse,const FloatPlan&,std::stop_token stop={},Kernel=Kernel::automatic);
// The same transform for a caller that reads only its first `prefix` outputs.
// Those equal the complete transform's bit for bit. The rest are unspecified.
// A `permuted` input already has the transform's bit-reversed element order.
void fft_prefix(std::span<std::complex<double>>,bool inverse,const DoublePlan&,std::size_t prefix,
                std::stop_token stop={},Kernel=Kernel::automatic,bool permuted=false);
void fft_prefix(std::span<std::complex<float>>,bool inverse,const FloatPlan&,std::size_t prefix,
                std::stop_token stop={},Kernel=Kernel::automatic,bool permuted=false);
// The transform's bit-reversed element order, which is its own inverse.
// Elementwise products of equally permuted operands are permuted products,
// so one permutation per operand replaces one per product transform.
void permute(std::span<std::complex<double>>,std::stop_token stop={});
void permute(std::span<std::complex<float>>,std::stop_token stop={});
// Elementwise product of finite, scaled FP32 spectra into inverse FFT input.
void multiply_spectra(std::span<std::complex<float>> output, std::span<const std::complex<float>> left,
                      std::span<const std::complex<float>> right, std::stop_token stop = {},
                      Kernel kernel = Kernel::automatic);

// Interleaved symmetric INT16 I/Q. Multiply by 2^exponent() to recover
// each stored component's scale. Only pack_q15 constructs the integer data.
class Q15Spectrum {
public:
    int exponent() const noexcept { return exponent_; }
    std::span<const std::int16_t> components() const noexcept { return components_; }
    std::size_t working_bytes() const noexcept { return components_.capacity() * sizeof(std::int16_t); }
private:
    std::vector<std::int16_t> components_;
    int exponent_ = 0;
    friend Q15Spectrum pack_q15(std::span<const std::complex<float>>, std::stop_token, Kernel);
};

Q15Spectrum pack_q15(std::span<const std::complex<float>>, std::stop_token stop = {},
                     Kernel kernel = Kernel::automatic);
// Returns unscaled integer pair sums rounded to FP32 for the inverse FFT.
// Restore both operands' exponents when scoring its output.
void multiply_spectra(std::span<std::complex<float>> output, const Q15Spectrum& left,
                      const Q15Spectrum& right, std::stop_token stop = {}, Kernel kernel = Kernel::automatic);
}
