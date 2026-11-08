#include "../src/search_fft.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <vector>

using namespace datapump;
namespace fft = datapump::modem::detail::search_fft;
namespace {
void check(bool condition,const char* reason) { if(!condition)throw Error(reason); }
template<class Action>void rejects(Action action) {
    try { action(); } catch(const Error&) { return; }
    throw Error("invalid FFT operation accepted");
}
// Independent direct transform, deliberately bounded to tiny correctness cases.
std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& input,bool inverse) {
    std::vector<std::complex<double>> result(input.size());
    for(std::size_t k=0;k<input.size();++k)for(std::size_t n=0;n<input.size();++n)
        result[k]+=input[n]*std::polar(1.,(inverse?2.:-2.)*std::numbers::pi*
            static_cast<double>(k*n)/static_cast<double>(input.size()));
    if(inverse)for(auto& value:result)value/=static_cast<double>(input.size());
    return result;
}
}
int main() { try {
    fft::DoublePlan doubles(64);
    for(const std::size_t size:{1U,2U,8U,32U,64U})for(const bool inverse:{false,true}) {
        std::vector<std::complex<double>> input(size);
        for(std::size_t i=0;i<size;++i)
            input[i]={std::sin(static_cast<double>(i)*.3),std::cos(static_cast<double>(i)*.7)};
        const auto expected=dft(input,inverse);
        auto native=input,scalar=input;
        fft::fft(native,inverse,doubles);fft::fft(scalar,inverse,doubles,{},fft::Kernel::scalar);
        for(std::size_t i=0;i<size;++i) {
            check(native[i]==scalar[i],"FFT ISA dispatch changed arithmetic");
            check(std::abs(native[i]-expected[i])<1e-10,"FP64 FFT disagrees with independent DFT");
        }
    }
    rejects([] { fft::DoublePlan invalid(3); });
    std::vector<std::complex<double>> invalid(3);
    rejects([&] { fft::fft(invalid,false,doubles); });
    std::vector<std::complex<double>> oversized(128);
    rejects([&] { fft::fft(oversized,false,doubles); });
    std::stop_source stop;stop.request_stop();
    std::vector<std::complex<double>> cancelled(8);
    rejects([&] { fft::fft(cancelled,false,doubles,stop.get_token()); });
    std::cout << "Bounded FFT correctness and cancellation checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }
