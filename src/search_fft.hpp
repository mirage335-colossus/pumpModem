#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <complex>
#include <cmath>
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
}
