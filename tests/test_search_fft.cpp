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
std::vector<std::complex<double>> dft(const std::vector<std::complex<float>>& input,bool inverse) {
    std::vector<std::complex<double>> widened;
    widened.reserve(input.size());
    for(const auto value:input)widened.emplace_back(value.real(),value.imag());
    return dft(widened,inverse);
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

    fft::FloatPlan singles(64);
    check(singles.working_bytes()==singles.twiddles.capacity()*sizeof(std::complex<float>),
          "FP32 FFT plan does not report its twiddle workspace");
    check(singles.working_bytes()*sizeof(std::complex<double>)==
              doubles.working_bytes()*sizeof(std::complex<float>),
          "Equal-geometry FP32 FFT twiddles do not use half the plan storage");
    for(const std::size_t size:{1U,2U,8U,32U,64U})for(const bool inverse:{false,true}) {
        std::vector<std::complex<float>> input(size);
        for(std::size_t i=0;i<size;++i)
            input[i]={static_cast<float>(std::sin(static_cast<double>(i)*.3)),
                      static_cast<float>(std::cos(static_cast<double>(i)*.7))};
        const auto expected=dft(input,inverse);
        auto native=input,scalar=input;
        fft::fft(native,inverse,singles);fft::fft(scalar,inverse,singles,{},fft::Kernel::scalar);
        for(std::size_t i=0;i<size;++i) {
            check(native[i]==scalar[i],"FP32 FFT ISA dispatch changed arithmetic");
            check(std::abs(std::complex<double>(native[i].real(),native[i].imag())-expected[i])<2e-5,
                  "FP32 FFT disagrees with independent double DFT");
        }
    }
    rejects([] { fft::FloatPlan invalid(3); });
    std::vector<std::complex<float>> float_invalid(3);
    rejects([&] { fft::fft(float_invalid,false,singles); });
    std::vector<std::complex<float>> float_oversized(128);
    rejects([&] { fft::fft(float_oversized,false,singles); });
    auto incomplete_plan=singles;
    incomplete_plan.twiddles.pop_back();
    std::vector<std::complex<float>> incomplete_input(8);
    rejects([&] { fft::fft(incomplete_input,false,incomplete_plan); });
    std::stop_source float_stop;float_stop.request_stop();
    std::vector<std::complex<float>> float_cancelled(8);
    rejects([&] { fft::fft(float_cancelled,false,singles,float_stop.get_token()); });

    // A longer round-trip catches twiddle recurrence drift without relying on
    // the implementation's butterfly layout for its expected result.
    constexpr std::size_t long_size=1U<<16;
    fft::FloatPlan long_plan(long_size);
    std::vector<std::complex<float>> long_input(long_size);
    for(std::size_t i=0;i<long_size;++i) {
        const auto phase=2*std::numbers::pi*static_cast<double>(i)/static_cast<double>(long_size);
        long_input[i]={static_cast<float>(.7*std::cos(17*phase)+.2*std::sin(53*phase)),
                       static_cast<float>(.6*std::sin(17*phase)-.1*std::cos(53*phase))};
    }
    auto roundtrip=long_input;
    fft::fft(roundtrip,false,long_plan);fft::fft(roundtrip,true,long_plan);
    for(std::size_t i=0;i<long_size;++i)
        check(std::abs(roundtrip[i]-long_input[i])<2e-5F,"Long FP32 FFT round-trip lost precision");
    std::cout << "Bounded FFT correctness and cancellation checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }
