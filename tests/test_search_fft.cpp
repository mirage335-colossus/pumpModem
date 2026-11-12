#include "../src/search_fft.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
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
// The original stage-major radix-two order, using the plan's own twiddles.
template<class T,class Plan>
std::vector<std::complex<T>> stage_major(std::vector<std::complex<T>> a,bool inverse,const Plan& plan) {
    const auto n=a.size();
    for(std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t length=2;length<=n;length*=2) {
        const auto half=length/2;
        const auto* table=plan.twiddles.data()+half-1;
        for(std::size_t begin=0;begin<n;begin+=length)for(std::size_t j=0;j<half;++j) {
            const auto w=inverse?std::conj(table[j]):table[j];
            const auto u=a[begin+j],v=a[begin+j+half]*w;
            a[begin+j]=u+v;a[begin+j+half]=u-v;
        }
    }
    if(inverse)for(auto& value:a)value/=static_cast<T>(n);
    return a;
}
// Cache blocking and an output prefix reorder or omit butterflies. Every
// value a caller may read must keep the stage-major transform's exact bits.
template<class T,class Plan>void blocked_order_is_exact(std::initializer_list<std::size_t> sizes) {
    std::uint32_t state=0x9e3779b9U;
    const auto next=[&] {
        state=state*1664525U+1013904223U;
        return static_cast<T>(static_cast<std::int32_t>(state))/static_cast<T>(1U<<30);
    };
    for(const auto size:sizes) {
        const Plan plan(size);
        std::vector<std::complex<T>> input(size);
        for(auto& value:input)value={next(),next()};
        // Signed zeros must survive the vectorized first stages unchanged.
        for(std::size_t i=0;i<size;i+=7)input[i]={i%2?T(0):-T(0),i%3?-T(0):T(0)};
        for(const bool inverse:{false,true}) {
            const auto expected=stage_major(input,inverse,plan);
            auto native=input,scalar=input;
            fft::fft(std::span(native),inverse,plan);
            fft::fft(std::span(scalar),inverse,plan,{},fft::Kernel::scalar);
            const auto same=[&](const std::vector<std::complex<T>>& value,std::size_t count) {
                return !std::memcmp(value.data(),expected.data(),count*sizeof(std::complex<T>));
            };
            check(same(native,size) && same(scalar,size),"cache-blocked FFT order changed stage-major arithmetic");
            for(const std::size_t prefix:{std::size_t{1},std::size_t{3},std::size_t{5},size/8+1,size/2,size/2+1,size}) {
                if(!prefix || prefix>size)continue;
                auto pruned=input;
                fft::fft_prefix(std::span(pruned),inverse,plan,prefix);
                auto scalar_pruned=input;
                fft::fft_prefix(std::span(scalar_pruned),inverse,plan,prefix,{},fft::Kernel::scalar);
                check(same(pruned,prefix) && same(scalar_pruned,prefix),
                      "FFT output prefix differs from the complete transform");
            }
            // A permuted operand skips the transform's own permutation.
            auto permuted=input;
            fft::permute(std::span(permuted));
            auto twice=permuted;
            fft::permute(std::span(twice));
            check(!std::memcmp(twice.data(),input.data(),size*sizeof(std::complex<T>)),
                  "FFT element permutation is not an involution");
            fft::fft_prefix(std::span(permuted),inverse,plan,size,{},fft::Kernel::automatic,true);
            check(same(permuted,size),"permuted FFT input changed the complete transform");
            auto invalid=input;
            rejects([&] { fft::fft_prefix(std::span(invalid),inverse,plan,0); });
            rejects([&] { fft::fft_prefix(std::span(invalid),inverse,plan,size+1); });
        }
    }
}
}
int main() { try {
    // Below, at and above both block sizes for each element width.
    blocked_order_is_exact<float,fft::FloatPlan>({1,2,4,8,16,1024,2048,8192,16384,65536});
    blocked_order_is_exact<double,fft::DoublePlan>({1,2,4,8,512,1024,4096,8192,32768});
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

    // Cover AVX2 lanes and a non-vector tail using differently scaled inputs.
    std::array<std::complex<float>, 19> left{}, right{}, products{};
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto exponent = static_cast<int>(i) - 9;
        left[i] = {std::ldexp(.7F, exponent), std::ldexp(-.9F, -exponent)};
        right[i] = {std::ldexp(-.2F, -exponent), std::ldexp(.6F, exponent)};
    }
    fft::multiply_spectra(products, left, right);
    for (std::size_t i = 0; i < products.size(); ++i)
        check(products[i] == left[i] * right[i], "FP32 spectrum product changed scalar arithmetic");
    rejects([&] { fft::multiply_spectra(products, left, right, float_stop.get_token()); });

    // Use integer extrema, half-integer ties, distinct scales and a tail.
    std::array<std::complex<float>, 19> q15_left{}, q15_right{};
    q15_left[0] = {32767.F, -32767.F};
    q15_right[0] = {-32767.F, 32767.F};
    q15_left[1] = {1.5F, -2.5F};
    q15_left[18] = {-1.5F, 2.5F};
    for (std::size_t i = 1; i < q15_right.size(); ++i)
        q15_right[i] = {static_cast<float>(i) + .5F, -static_cast<float>(i) - 1.5F};
    for (auto& value : q15_right) value *= .125F;
    const auto qleft = fft::pack_q15(q15_left), qright = fft::pack_q15(q15_right);
    const auto scalar_left = fft::pack_q15(q15_left, {}, fft::Kernel::scalar);
    const auto scalar_right = fft::pack_q15(q15_right, {}, fft::Kernel::scalar);
    check(qleft.exponent() == 0 && qright.exponent() == -3 &&
          qleft.components()[0] == 32767 && qleft.components()[1] == -32767 &&
          qleft.components()[2] == 2 && qleft.components()[3] == -2 &&
          qleft.components()[36] == -2 && qleft.components()[37] == 2 &&
          std::ranges::equal(qleft.components(), scalar_left.components()) &&
          std::ranges::equal(qright.components(), scalar_right.components()),
          "Q15 quantization violated scaling, rounding or ISA parity");
    fft::multiply_spectra(products, qleft, qright);
    for (std::size_t i = 0; i < products.size(); ++i) {
        const std::int64_t ar = qleft.components()[2 * i], ai = qleft.components()[2 * i + 1];
        const std::int64_t br = qright.components()[2 * i], bi = qright.components()[2 * i + 1];
        check(products[i] == std::complex<float>(static_cast<float>(ar * br - ai * bi),
                                                  static_cast<float>(ar * bi + ai * br)),
              "Q15 complex product changed the bounded widened integer result");
    }
    std::array<std::complex<float>, 2> q15_zero{};
    const auto packed_zero = fft::pack_q15(q15_zero);
    check(packed_zero.exponent() == 0 &&
          std::ranges::all_of(packed_zero.components(), [](auto x) { return x == 0; }),
          "Q15 zero spectrum introduced signal or scaling");
    rejects([&] { (void)fft::pack_q15(q15_left, float_stop.get_token()); });
    rejects([&] { fft::multiply_spectra(products, qleft, qright, float_stop.get_token()); });
    q15_left[7] = {std::numeric_limits<float>::quiet_NaN(), 0};
    rejects([&] { (void)fft::pack_q15(q15_left); });

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

    // Constant spectra give a known coherent inverse-FFT peak. Exercise a
    // 131072-row Q15 transform with packed products near the signed limit.
    constexpr auto packed_size = 1U << 17;
    const std::vector<std::complex<float>> constant(packed_size, {32767.F, -32767.F});
    const auto packed_constant = fft::pack_q15(constant);
    std::vector<std::complex<float>> inverse(packed_size);
    fft::multiply_spectra(inverse, packed_constant, packed_constant);
    fft::fft(inverse, true, fft::FloatPlan(packed_size));
    const auto expected_peak = std::complex<float>(0, static_cast<float>(-2LL * 32767 * 32767));
    check(inverse.front() == expected_peak &&
          std::ranges::all_of(std::span(inverse).subspan(1), [](auto x) { return x == std::complex<float>{}; }),
          "Long coherent Q15 inverse transform lost its finite peak or zero bins");
    std::cout << "Bounded FFT correctness and cancellation checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }
