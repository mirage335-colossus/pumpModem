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
// Public twiddle plans for the canonical native waveform shaping.
enum class Kernel { automatic,scalar };
const char* kernel_name(Kernel kernel=Kernel::automatic);
struct DoublePlan {
    explicit DoublePlan(std::size_t maximum,std::stop_token stop={});
    std::size_t maximum=0;
    std::vector<std::complex<double>> twiddles;
    std::size_t working_bytes()const{return twiddles.capacity()*sizeof(std::complex<double>);}
};
void fft(std::span<std::complex<double>>,bool inverse,const DoublePlan&,std::stop_token stop={},Kernel=Kernel::automatic);
}
