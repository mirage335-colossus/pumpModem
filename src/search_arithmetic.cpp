#include "search_arithmetic.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define DATAPUMP_SEARCH_SSE2 1
#define DATAPUMP_SEARCH_AVX2 1
#define DATAPUMP_SEARCH_TARGET_SSE2 __attribute__((target("sse2")))
#define DATAPUMP_SEARCH_TARGET_AVX2 __attribute__((target("avx2")))
#elif defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define DATAPUMP_SEARCH_SSE2 1
#define DATAPUMP_SEARCH_TARGET_SSE2
#endif
#endif

namespace datapump::modem::detail::search_arithmetic {
namespace {
using Complex = std::complex<double>;
constexpr auto infinity = std::numeric_limits<double>::infinity();
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<float>::is_iec559);

template<class T> bool gradual_underflow() {
    // Volatile operands prevent constant folding past a caller's FP control
    // state (notably x86 DAZ/FTZ). Unsupported environments refine everything.
    volatile T normal = std::numeric_limits<T>::min(), two = 2;
    volatile T tiny = std::numeric_limits<T>::denorm_min(), one = 1;
    return normal / two > 0 && tiny * one > 0;
}

// With IEEE arithmetic and gradual underflow, one adjacent representable value
// encloses each primitive, including underflow. No rounding-mode changes, FMA,
// fast-math, or dependence on a wider long-double format is needed.
double up(double x) { return std::isnan(x) ? infinity : std::nextafter(x, infinity); }
double down(double x) { return std::nextafter(x, -infinity); }
double add_up(double a, double b) {
    if (!a) return b;
    if (!b) return a;
    return up(a + b);
}
double mul_up(double a, double b) {
    if (!a || !b) return 0;
    return up(a * b);
}
double sqrt_up(double a) { return a == 0 ? 0 : up(std::sqrt(a)); }
double disk(double real, double imag) {
    return sqrt_up(add_up(mul_up(real, real), mul_up(imag, imag)));
}
double ulp(double x) {
    if (!std::isfinite(x)) return infinity;
    const auto a = std::abs(x);
    return up(a) - a;
}
double multiply_error(double a, double b, double product) {
    return a == 0 || b == 0 ? 0 : ulp(product);
}
bool finite(Complex x) { return std::isfinite(x.real()) && std::isfinite(x.imag()); }
void validate_fp(std::span<const Complex> x, std::span<const Complex> t, bool fp32 = false) {
    if (x.size() != t.size()) throw Error("search arithmetic row lengths differ");
    for (std::size_t i = 0; i < x.size(); ++i)
        if (!finite(x[i]) || !finite(t[i]))
            throw Error("search arithmetic requires finite operands");
        else if (fp32 && std::max({std::abs(x[i].real()), std::abs(x[i].imag()),
                                  std::abs(t[i].real()), std::abs(t[i].imag())}) >
                                  std::numeric_limits<float>::max())
            throw Error("search arithmetic operand exceeds FP32 range");
}
std::int64_t checked_add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        throw Error("search arithmetic integer dot overflow");
    return a + b;
}
IntegerDot dot_scalar(std::span<const Complex8> x, std::span<const Complex8> t) {
    IntegerDot result;
    for (std::size_t begin = 0; begin < x.size();) {
        const auto end = begin + std::min(integer_chunk_size, x.size() - begin);
        std::int32_t real = 0, imag = 0;
        for (auto i = begin; i < end; ++i) {
            const auto xr = static_cast<std::int32_t>(x[i].real);
            const auto xi = static_cast<std::int32_t>(x[i].imag);
            const auto tr = static_cast<std::int32_t>(t[i].real);
            const auto ti = static_cast<std::int32_t>(t[i].imag);
            real += xr * tr + xi * ti;
            imag += xi * tr - xr * ti;
        }
        accumulate(result, {real, imag});
        begin = end;
    }
    return result;
}
Complex fp64_scalar(std::span<const Complex> x, std::span<const Complex> t) {
    double real = 0, imag = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        real += x[i].real() * t[i].real() + x[i].imag() * t[i].imag();
        imag += x[i].imag() * t[i].real() - x[i].real() * t[i].imag();
    }
    return {real, imag};
}
Complex fp32_scalar(std::span<const Complex> x, std::span<const Complex> t) {
    float real = 0, imag = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto xr = static_cast<float>(x[i].real()), xi = static_cast<float>(x[i].imag());
        const auto tr = static_cast<float>(t[i].real()), ti = static_cast<float>(t[i].imag());
        real += xr * tr + xi * ti;
        imag += xi * tr - xr * ti;
    }
    return {real, imag};
}

#ifdef DATAPUMP_SEARCH_SSE2
DATAPUMP_SEARCH_TARGET_SSE2
IntegerDot dot_sse2(std::span<const Complex8> x, std::span<const Complex8> t) {
    IntegerDot result;
    const auto zero = _mm_setzero_si128();
    const auto signs = _mm_set_epi16(1, -1, 1, -1, 1, -1, 1, -1);
    for (std::size_t begin = 0; begin < x.size();) {
        const auto end = begin + std::min(integer_chunk_size, x.size() - begin);
        auto re = zero, im = zero;
        auto i = begin;
        for (; end - i >= 8; i += 8) {
            const auto xb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(x.data() + i));
            const auto tb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(t.data() + i));
            const auto xs = _mm_cmpgt_epi8(zero, xb), ts = _mm_cmpgt_epi8(zero, tb);
            const __m128i xx[2]{_mm_unpacklo_epi8(xb, xs), _mm_unpackhi_epi8(xb, xs)};
            const __m128i tt[2]{_mm_unpacklo_epi8(tb, ts), _mm_unpackhi_epi8(tb, ts)};
            for (unsigned half = 0; half < 2; ++half) {
                auto crossed = _mm_shufflelo_epi16(tt[half], _MM_SHUFFLE(2, 3, 0, 1));
                crossed = _mm_shufflehi_epi16(crossed, _MM_SHUFFLE(2, 3, 0, 1));
                crossed = _mm_mullo_epi16(crossed, signs);
                re = _mm_add_epi32(re, _mm_madd_epi16(xx[half], tt[half]));
                im = _mm_add_epi32(im, _mm_madd_epi16(xx[half], crossed));
            }
        }
        std::array<std::int32_t, 4> rr{}, ii{};
        _mm_storeu_si128(reinterpret_cast<__m128i*>(rr.data()), re);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ii.data()), im);
        IntegerDot chunk;
        for (unsigned lane = 0; lane < 4; ++lane) {
            chunk.real += rr[lane]; chunk.imag += ii[lane];
        }
        for (; i < end; ++i) {
            const auto xr = static_cast<std::int32_t>(x[i].real), xi = static_cast<std::int32_t>(x[i].imag);
            const auto tr = static_cast<std::int32_t>(t[i].real), ti = static_cast<std::int32_t>(t[i].imag);
            chunk.real += xr * tr + xi * ti;
            chunk.imag += xi * tr - xr * ti;
        }
        accumulate(result, chunk); begin = end;
    }
    return result;
}
DATAPUMP_SEARCH_TARGET_SSE2
Complex fp64_sse2(std::span<const Complex> x, std::span<const Complex> t) {
    auto re = _mm_setzero_pd(), im = re;
    const auto signs = _mm_set_pd(1., -1.);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto xx = _mm_loadu_pd(reinterpret_cast<const double*>(x.data() + i));
        const auto tt = _mm_loadu_pd(reinterpret_cast<const double*>(t.data() + i));
        re = _mm_add_pd(re, _mm_mul_pd(xx, tt));
        im = _mm_add_pd(im, _mm_mul_pd(xx, _mm_mul_pd(_mm_shuffle_pd(tt, tt, 1), signs)));
    }
    std::array<double, 2> rr{}, ii{};
    _mm_storeu_pd(rr.data(), re); _mm_storeu_pd(ii.data(), im);
    return {rr[0] + rr[1], ii[0] + ii[1]};
}
DATAPUMP_SEARCH_TARGET_SSE2
Complex fp32_sse2(std::span<const Complex> x, std::span<const Complex> t) {
    auto re = _mm_setzero_ps(), im = re;
    const auto signs = _mm_set_ps(1.f, -1.f, 1.f, -1.f);
    std::size_t i = 0;
    for (; x.size() - i >= 2; i += 2) {
        const auto xx = _mm_movelh_ps(
            _mm_cvtpd_ps(_mm_loadu_pd(reinterpret_cast<const double*>(x.data() + i))),
            _mm_cvtpd_ps(_mm_loadu_pd(reinterpret_cast<const double*>(x.data() + i + 1))));
        const auto tt = _mm_movelh_ps(
            _mm_cvtpd_ps(_mm_loadu_pd(reinterpret_cast<const double*>(t.data() + i))),
            _mm_cvtpd_ps(_mm_loadu_pd(reinterpret_cast<const double*>(t.data() + i + 1))));
        re = _mm_add_ps(re, _mm_mul_ps(xx, tt));
        im = _mm_add_ps(im, _mm_mul_ps(xx,
            _mm_mul_ps(_mm_shuffle_ps(tt, tt, _MM_SHUFFLE(2, 3, 0, 1)), signs)));
    }
    std::array<float, 4> rr{}, ii{};
    _mm_storeu_ps(rr.data(), re); _mm_storeu_ps(ii.data(), im);
    float real = (rr[0] + rr[1]) + (rr[2] + rr[3]);
    float imag = (ii[0] + ii[1]) + (ii[2] + ii[3]);
    if (i < x.size()) {
        const auto tail = fp32_scalar(x.subspan(i), t.subspan(i));
        real += static_cast<float>(tail.real()); imag += static_cast<float>(tail.imag());
    }
    return {real, imag};
}
#endif

#ifdef DATAPUMP_SEARCH_AVX2
DATAPUMP_SEARCH_TARGET_AVX2
IntegerDot dot_avx2(std::span<const Complex8> x, std::span<const Complex8> t) {
    IntegerDot result;
    const auto signs = _mm256_set_epi16(1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1);
    for (std::size_t begin = 0; begin < x.size();) {
        const auto end = begin + std::min(integer_chunk_size, x.size() - begin);
        auto re = _mm256_setzero_si256(), im = re;
        auto i = begin;
        for (; end - i >= 8; i += 8) {
            const auto xx = _mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x.data() + i)));
            const auto tt = _mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t.data() + i)));
            auto crossed = _mm256_shufflelo_epi16(tt, _MM_SHUFFLE(2, 3, 0, 1));
            crossed = _mm256_shufflehi_epi16(crossed, _MM_SHUFFLE(2, 3, 0, 1));
            crossed = _mm256_mullo_epi16(crossed, signs);
            re = _mm256_add_epi32(re, _mm256_madd_epi16(xx, tt));
            im = _mm256_add_epi32(im, _mm256_madd_epi16(xx, crossed));
        }
        std::array<std::int32_t, 8> rr{}, ii{};
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(rr.data()), re);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(ii.data()), im);
        IntegerDot chunk;
        for (unsigned lane = 0; lane < 8; ++lane) {
            chunk.real += rr[lane]; chunk.imag += ii[lane];
        }
        for (; i < end; ++i) {
            const auto xr = static_cast<std::int32_t>(x[i].real), xi = static_cast<std::int32_t>(x[i].imag);
            const auto tr = static_cast<std::int32_t>(t[i].real), ti = static_cast<std::int32_t>(t[i].imag);
            chunk.real += xr * tr + xi * ti;
            chunk.imag += xi * tr - xr * ti;
        }
        accumulate(result, chunk); begin = end;
    }
    return result;
}
DATAPUMP_SEARCH_TARGET_AVX2
std::array<IntegerDot,3> compensated_avx2(std::span<const Complex8> x0,
        std::span<const Complex8> x1,std::span<const Complex8> t0,std::span<const Complex8> t1) {
    std::array<IntegerDot,3> result{};
    const auto signs=_mm256_set_epi16(1,-1,1,-1,1,-1,1,-1,1,-1,1,-1,1,-1,1,-1);
    for(std::size_t begin=0;begin<x0.size();) {
        const auto end=begin+std::min(integer_chunk_size,x0.size()-begin);
        auto ar=_mm256_setzero_si256(),ai=ar,br=ar,bi=ar,cr=ar,ci=ar;
        auto i=begin;
        for(;end-i>=8;i+=8) {
            const auto x=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x0.data()+i)));
            const auto y=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x1.data()+i)));
            auto t=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t0.data()+i)));
            ar=_mm256_add_epi32(ar,_mm256_madd_epi16(x,t));
            br=_mm256_add_epi32(br,_mm256_madd_epi16(y,t));
            t=_mm256_shufflelo_epi16(t,_MM_SHUFFLE(2,3,0,1));
            t=_mm256_shufflehi_epi16(t,_MM_SHUFFLE(2,3,0,1));
            t=_mm256_mullo_epi16(t,signs);
            ai=_mm256_add_epi32(ai,_mm256_madd_epi16(x,t));
            bi=_mm256_add_epi32(bi,_mm256_madd_epi16(y,t));
            t=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t1.data()+i)));
            cr=_mm256_add_epi32(cr,_mm256_madd_epi16(x,t));
            t=_mm256_shufflelo_epi16(t,_MM_SHUFFLE(2,3,0,1));
            t=_mm256_shufflehi_epi16(t,_MM_SHUFFLE(2,3,0,1));
            t=_mm256_mullo_epi16(t,signs);
            ci=_mm256_add_epi32(ci,_mm256_madd_epi16(x,t));
        }
        std::array<std::array<std::int32_t,8>,6> lanes{};
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[0].data()),ar);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[1].data()),ai);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[2].data()),br);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[3].data()),bi);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[4].data()),cr);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes[5].data()),ci);
        std::array<IntegerDot,3> chunk{};
        for(unsigned j=0;j<3;++j)for(unsigned lane=0;lane<8;++lane) {
            chunk[j].real+=lanes[2*j][lane];chunk[j].imag+=lanes[2*j+1][lane];
        }
        for(;i<end;++i) {
            const std::array<Complex8,3> x{x0[i],x1[i],x0[i]},t{t0[i],t0[i],t1[i]};
            for(unsigned j=0;j<3;++j) {
                chunk[j].real+=static_cast<std::int32_t>(x[j].real)*t[j].real+static_cast<std::int32_t>(x[j].imag)*t[j].imag;
                chunk[j].imag+=static_cast<std::int32_t>(x[j].imag)*t[j].real-static_cast<std::int32_t>(x[j].real)*t[j].imag;
            }
        }
        for(unsigned j=0;j<3;++j)accumulate(result[j],chunk[j]);
        begin=end;
    }
    return result;
}
DATAPUMP_SEARCH_TARGET_AVX2
Complex fp64_avx2(std::span<const Complex> x, std::span<const Complex> t) {
    auto re = _mm256_setzero_pd(), im = re;
    const auto signs = _mm256_set_pd(1., -1., 1., -1.);
    std::size_t i = 0;
    for (; x.size() - i >= 2; i += 2) {
        const auto xx = _mm256_loadu_pd(reinterpret_cast<const double*>(x.data() + i));
        const auto tt = _mm256_loadu_pd(reinterpret_cast<const double*>(t.data() + i));
        re = _mm256_add_pd(re, _mm256_mul_pd(xx, tt));
        im = _mm256_add_pd(im, _mm256_mul_pd(xx, _mm256_mul_pd(_mm256_permute_pd(tt, 5), signs)));
    }
    std::array<double, 4> rr{}, ii{};
    _mm256_storeu_pd(rr.data(), re); _mm256_storeu_pd(ii.data(), im);
    auto real = (rr[0] + rr[1]) + (rr[2] + rr[3]);
    auto imag = (ii[0] + ii[1]) + (ii[2] + ii[3]);
    if (i < x.size()) {
        const auto tail = fp64_scalar(x.subspan(i), t.subspan(i));
        real += tail.real(); imag += tail.imag();
    }
    return {real, imag};
}
DATAPUMP_SEARCH_TARGET_AVX2
Complex fp32_avx2(std::span<const Complex> x, std::span<const Complex> t) {
    auto re = _mm256_setzero_ps(), im = re;
    const auto signs = _mm256_set_ps(1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f);
    std::size_t i = 0;
    for (; x.size() - i >= 4; i += 4) {
        const auto xlo = _mm256_cvtpd_ps(_mm256_loadu_pd(reinterpret_cast<const double*>(x.data() + i)));
        const auto xhi = _mm256_cvtpd_ps(_mm256_loadu_pd(reinterpret_cast<const double*>(x.data() + i + 2)));
        const auto tlo = _mm256_cvtpd_ps(_mm256_loadu_pd(reinterpret_cast<const double*>(t.data() + i)));
        const auto thi = _mm256_cvtpd_ps(_mm256_loadu_pd(reinterpret_cast<const double*>(t.data() + i + 2)));
        const auto xx = _mm256_insertf128_ps(_mm256_castps128_ps256(xlo), xhi, 1);
        const auto tt = _mm256_insertf128_ps(_mm256_castps128_ps256(tlo), thi, 1);
        re = _mm256_add_ps(re, _mm256_mul_ps(xx, tt));
        im = _mm256_add_ps(im, _mm256_mul_ps(xx,
            _mm256_mul_ps(_mm256_permute_ps(tt, _MM_SHUFFLE(2, 3, 0, 1)), signs)));
    }
    std::array<float, 8> rr{}, ii{};
    _mm256_storeu_ps(rr.data(), re); _mm256_storeu_ps(ii.data(), im);
    float real = 0, imag = 0;
    for (unsigned lane = 0; lane < 8; ++lane) { real += rr[lane]; imag += ii[lane]; }
    if (i < x.size()) {
        const auto tail = fp32_scalar(x.subspan(i), t.subspan(i));
        real += static_cast<float>(tail.real()); imag += static_cast<float>(tail.imag());
    }
    return {real, imag};
}
#endif

double energy_product(double x, double t) {
    if (!(x >= 0) || !(t >= 0)) return infinity;
    return mul_up(sqrt_up(x), sqrt_up(t));
}
double count_upper(std::size_t count) { return count ? up(static_cast<double>(count)) : 0; }
// Enclose a nonnegative squared sum with <=4*count+16 rounded primitives.
// Every exact term's rounding-factor product is >=1-m*eps (Bernoulli), while
// propagated additive underflow is <=m*eta/(1-m*eps). This uses only O(1)
// outward operations per tile; overflow makes the certificate inconclusive.
double nonnegative_sum_upper(double sum, std::size_t count) {
    if (!(sum >= 0) || !std::isfinite(sum)) return infinity;
    const auto operations = add_up(mul_up(4, count_upper(count)), 16);
    const auto denominator = down(1 - mul_up(operations, std::numeric_limits<double>::epsilon()));
    if (!(denominator > 0)) return infinity;
    const auto underflow = up(mul_up(operations, std::numeric_limits<double>::denorm_min()) / denominator);
    return up(add_up(sum, underflow) / denominator);
}
double gamma_upper(double operations, double epsilon) {
    const auto product = mul_up(operations, epsilon);
    const auto denominator = down(1 - product);
    return denominator > 0 ? up(product / denominator) : infinity;
}
bool valid_block(const QuantizedBlock& q) {
    return q.size <= quantized_tile_size && std::isfinite(q.scale) && q.scale >= 0 &&
        q.original_energy_upper >= 0 && q.reconstructed_energy_upper >= 0 && q.residual_energy_upper >= 0;
}
} // namespace

bool backend_available(Backend backend) noexcept {
    if (backend == Backend::automatic || backend == Backend::scalar) return true;
#if defined(DATAPUMP_SEARCH_SSE2) && (defined(__GNUC__) || defined(__clang__))
    static const bool sse2 = [] { __builtin_cpu_init(); return bool(__builtin_cpu_supports("sse2")); }();
    if (backend == Backend::sse2) return sse2;
#elif defined(DATAPUMP_SEARCH_SSE2)
    if (backend == Backend::sse2) return true;
#endif
#ifdef DATAPUMP_SEARCH_AVX2
    static const bool avx2 = [] { __builtin_cpu_init(); return bool(__builtin_cpu_supports("avx2")); }();
    if (backend == Backend::avx2) return avx2;
#endif
    return false;
}
Backend selected_backend(Backend backend) {
    if (backend == Backend::automatic) {
        if (backend_available(Backend::avx2)) return Backend::avx2;
        if (backend_available(Backend::sse2)) return Backend::sse2;
        return Backend::scalar;
    }
    if (!backend_available(backend)) throw Error("requested search arithmetic ISA is unavailable");
    return backend;
}
const char* backend_name(Backend backend) {
    switch (selected_backend(backend)) {
    case Backend::scalar: return "scalar";
    case Backend::sse2: return "sse2";
    case Backend::avx2: return "avx2";
    case Backend::automatic: break;
    }
    throw Error("invalid search arithmetic ISA");
}

QuantizedBlock pack(std::span<const Complex> input, std::span<Complex8> output) {
    if (input.size() > quantized_tile_size || output.size() < input.size())
        throw Error("search arithmetic packing tile exceeds supplied bounds");
    if (!input.empty()) {
        const auto a = reinterpret_cast<std::uintptr_t>(input.data());
        const auto b = reinterpret_cast<std::uintptr_t>(output.data());
        const auto na = input.size_bytes(), nb = input.size() * sizeof(Complex8);
        if ((a <= b && b - a < na) || (b < a && a - b < nb))
            throw Error("search arithmetic packing output overlaps input");
    }
    double maximum = 0;
    for (auto value : input) {
        if (!finite(value)) throw Error("search arithmetic requires finite operands");
        maximum = std::max({maximum, std::abs(value.real()), std::abs(value.imag())});
    }
    QuantizedBlock result;
    result.size = input.size();
    result.scale = maximum == 0 ? 0 : std::max(maximum / 127., std::numeric_limits<double>::denorm_min());
    const auto inverse = result.scale > 0 ? 1. / result.scale : 0.;
    const bool multiply_scale = std::isnormal(inverse);
    double original_sum = 0, residual_sum = 0;
    std::int32_t integer_energy = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::array<std::int8_t, 2> packed{};
        const std::array<double, 2> original{input[i].real(), input[i].imag()};
        for (unsigned component = 0; component < 2; ++component) {
            const auto value = original[component];
            auto ratio = multiply_scale ? std::min(std::abs(value) * inverse, 127.) :
                result.scale == 0 ? 0 : std::min(std::abs(value / result.scale), 127.);
            auto integer = static_cast<int>(ratio);
            // Reciprocal multiplication avoids a division for ordinary values.
            // Its two rounded operations can differ from division only near a
            // rounding boundary. Resolve those cases with the original division
            // so packing retains exactly the documented nearest/ties-away rule.
            if (multiply_scale && std::abs((ratio - integer) - .5) <=
                    8 * 127 * std::numeric_limits<double>::epsilon()) {
                ratio = std::min(std::abs(value / result.scale), 127.);
                integer = static_cast<int>(ratio);
            }
            // ratio-integer is exact by Sterbenz (or integer is zero), so this
            // preserves std::round's ties-away behavior without a libm call.
            integer += ratio - integer >= .5;
            if (value < 0) integer = -integer;
            packed[component] = static_cast<std::int8_t>(integer);
            integer_energy += integer * integer;
            const auto reconstructed = integer * result.scale;
            const auto residual = value - reconstructed;
            original_sum += value * value;
            residual_sum += residual * residual;
        }
        output[i] = {packed[0], packed[1]};
    }
    if (!gradual_underflow<double>()) {
        result.original_energy_upper = result.reconstructed_energy_upper =
            result.residual_energy_upper = infinity;
    } else if (maximum != 0) {
        result.original_energy_upper = nonnegative_sum_upper(original_sum, input.size());
        // sum(q^2) <=2048*127^2 =33032192 is an exact int32 sum. Bound the
        // exact scale*q norm without enclosing every reconstructed component.
        const auto reconstruction_norm = mul_up(result.scale, sqrt_up(static_cast<double>(integer_energy)));
        result.reconstructed_energy_upper = mul_up(reconstruction_norm, reconstruction_norm);
        const auto d_norm = sqrt_up(nonnegative_sum_upper(residual_sum, input.size()));
        const auto tiny_norm = mul_up(sqrt_up(mul_up(2, count_upper(input.size()))),
                                      std::numeric_limits<double>::denorm_min());
        const auto epsilon = std::numeric_limits<double>::epsilon();
        // r=fl(scale*q), d=fl(x-r). Then ||x-r|| <= (||d||+sqrt(M)*eta)/(1-eps)
        // and ||r-scale*q|| <=eps*||scale*q||+sqrt(M)*eta, M=2*input.size().
        const auto residual_norm = add_up(up(add_up(d_norm, tiny_norm) / down(1 - epsilon)),
            add_up(mul_up(epsilon, reconstruction_norm), tiny_norm));
        result.residual_energy_upper = mul_up(residual_norm, residual_norm);
    }
    return result;
}

CompensatedBlock pack_compensated(std::span<const Complex> input,std::span<Complex8> first,
        std::span<Complex8> residual) {
    if(input.size()>quantized_tile_size || residual.size()<input.size())throw Error("compensated search tile exceeds storage");
    const auto begin=reinterpret_cast<std::uintptr_t>(first.data()),end=reinterpret_cast<std::uintptr_t>(residual.data());
    const auto bytes=input.size()*sizeof(Complex8);
    if((begin<=end&&end-begin<bytes)||(end<begin&&begin-end<bytes))throw Error("compensated search planes overlap");
    const auto source=reinterpret_cast<std::uintptr_t>(input.data());
    if((source<=end&&end-source<input.size_bytes())||(end<source&&source-end<bytes))
        throw Error("compensated residual output overlaps input");
    CompensatedBlock result;result.first=pack(input,first);
    std::array<Complex,quantized_tile_size> remainder{};bool valid=true;
    for(std::size_t i=0;i<input.size();++i) {
        remainder[i]=input[i]-Complex{result.first.scale*first[i].real,result.first.scale*first[i].imag};
        valid=valid&&finite(remainder[i]);
    }
    if(!valid) {
        std::fill_n(residual.begin(),input.size(),Complex8{});result.residual.size=input.size();
        result.tail_norm_upper=infinity;return result;
    }
    result.residual=pack(std::span(remainder).first(input.size()),residual);
    const auto epsilon=std::numeric_limits<double>::epsilon();
    const auto tiny=mul_up(sqrt_up(mul_up(2,count_upper(input.size()))),std::numeric_limits<double>::denorm_min());
    const auto formation=add_up(mul_up(epsilon,sqrt_up(result.first.reconstructed_energy_upper)),
        add_up(up(add_up(mul_up(epsilon,sqrt_up(result.residual.original_energy_upper)),tiny)/down(1-epsilon)),tiny));
    result.tail_norm_upper=add_up(sqrt_up(result.residual.residual_energy_upper),formation);
    return result;
}

void accumulate(IntegerDot& result, IntegerDot addend) {
    const IntegerDot next{checked_add(result.real, addend.real), checked_add(result.imag, addend.imag)};
    result = next;
}
IntegerDot dot_i8(std::span<const Complex8> x, std::span<const Complex8> t, Backend backend) {
    if (x.size() != t.size()) throw Error("search arithmetic row lengths differ");
    // The conservative per-component whole-row bound also prevents overflow
    // even for a caller that explicitly supplies asymmetric -128 operands.
    if (x.size() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 32768)
        throw Error("search arithmetic integer row exceeds int64 capacity");
    switch (selected_backend(backend)) {
#ifdef DATAPUMP_SEARCH_AVX2
    case Backend::avx2: return dot_avx2(x, t);
#endif
#ifdef DATAPUMP_SEARCH_SSE2
    case Backend::sse2: return dot_sse2(x, t);
#endif
    default: return dot_scalar(x, t);
    }
}
std::array<IntegerDot,3> dot_compensated_i8(std::span<const Complex8> x0,
        std::span<const Complex8> x1,std::span<const Complex8> t0,
        std::span<const Complex8> t1,Backend backend) {
    if(x0.size()!=x1.size()||x0.size()!=t0.size()||x0.size()!=t1.size())
        throw Error("compensated integer row lengths differ");
    if(x0.size()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())/32768)
        throw Error("compensated integer row exceeds int64 capacity");
    backend=selected_backend(backend);
#ifdef DATAPUMP_SEARCH_AVX2
    if(backend==Backend::avx2)return compensated_avx2(x0,x1,t0,t1);
#endif
    return {dot_i8(x0,t0,backend),dot_i8(x1,t0,backend),dot_i8(x0,t1,backend)};
}
BoundedDot combine(IntegerDot dot, const QuantizedBlock& x, const QuantizedBlock& t) {
    if (!valid_block(x) || !valid_block(t) || x.size != t.size)
        throw Error("invalid search arithmetic quantization metadata");
    const auto maximum = static_cast<std::int64_t>(x.size) * 32768;
    if (dot.real > maximum || dot.real < -maximum || dot.imag > maximum || dot.imag < -maximum)
        throw Error("search arithmetic integer dot exceeds packing tile bound");
    const auto scale = x.scale * t.scale;
    BoundedDot result{{static_cast<double>(dot.real) * scale, static_cast<double>(dot.imag) * scale}, 0};
    if (!finite(result.value) || !std::isfinite(scale)) return {result.value, infinity};
    const auto a = add_up(energy_product(x.residual_energy_upper, t.original_energy_upper),
                          energy_product(x.reconstructed_energy_upper, t.residual_energy_upper));
    const auto b = add_up(energy_product(t.residual_energy_upper, x.original_energy_upper),
                          energy_product(t.reconstructed_energy_upper, x.residual_energy_upper));
    const auto scale_error = multiply_error(x.scale, t.scale, scale);
    const auto real_error = add_up(mul_up(std::abs(static_cast<double>(dot.real)), scale_error),
        multiply_error(static_cast<double>(dot.real), scale, result.value.real()));
    const auto imag_error = add_up(mul_up(std::abs(static_cast<double>(dot.imag)), scale_error),
        multiply_error(static_cast<double>(dot.imag), scale, result.value.imag()));
    result.error = add_up(std::min(a, b), disk(real_error, imag_error));
    return result;
}
BoundedDot combine_compensated(IntegerDot aa,IntegerDot ba,IntegerDot ab,
        const CompensatedBlock& x,const CompensatedBlock& t) {
    if(!(x.tail_norm_upper>=0)||!(t.tail_norm_upper>=0))throw Error("invalid compensated residual norm");
    // Rounding-only reconstruction of three exact integer dot products. The
    // omitted residual*residual term and final tails are bounded just once.
    const auto scaled=[](IntegerDot dot,QuantizedBlock a,QuantizedBlock b) {
        a.original_energy_upper=a.reconstructed_energy_upper;
        b.original_energy_upper=b.reconstructed_energy_upper;
        a.residual_energy_upper=b.residual_energy_upper=0;
        return combine(dot,a,b);
    };
    auto result=scaled(aa,x.first,t.first);
    accumulate(result,scaled(ba,x.residual,t.first));
    accumulate(result,scaled(ab,x.first,t.residual));
    const auto nx=sqrt_up(x.first.original_energy_upper),nt=sqrt_up(t.first.original_energy_upper);
    const auto ax=sqrt_up(x.first.reconstructed_energy_upper),at=sqrt_up(t.first.reconstructed_energy_upper);
    const auto bx=sqrt_up(x.residual.reconstructed_energy_upper),bt=sqrt_up(t.residual.reconstructed_energy_upper);
    const auto qx=std::min(add_up(ax,bx),add_up(nx,x.tail_norm_upper));
    const auto qt=std::min(add_up(at,bt),add_up(nt,t.tail_norm_upper));
    const auto left=add_up(mul_up(x.tail_norm_upper,nt),mul_up(qx,t.tail_norm_upper));
    const auto right=add_up(mul_up(t.tail_norm_upper,nx),mul_up(qt,x.tail_norm_upper));
    result.error=add_up(result.error,add_up(mul_up(bx,bt),std::min(left,right)));
    return result;
}
void accumulate_compensated(CompensatedReduction& total,IntegerDot aa,IntegerDot ba,IntegerDot ab,
        const CompensatedBlock& x,const CompensatedBlock& t) {
    if(!valid_block(x.first)||!valid_block(x.residual)||!valid_block(t.first)||!valid_block(t.residual)||
       x.first.size!=x.residual.size||x.first.size!=t.first.size||x.first.size!=t.residual.size||
       !(x.tail_norm_upper>=0)||!(t.tail_norm_upper>=0))throw Error("invalid compensated reduction metadata");
    const auto maximum=static_cast<std::int64_t>(x.first.size)*32768;
    for(auto dot:{aa,ba,ab})if(dot.real>maximum||dot.real< -maximum||dot.imag>maximum||dot.imag< -maximum)
        throw Error("compensated integer dot exceeds tile bound");
    if(total.pieces==std::numeric_limits<std::size_t>::max())throw Error("compensated reduction counter overflow");
    const auto term=[](IntegerDot z,double a,double b){const auto scale=a*b;return Complex{z.real*scale,z.imag*scale};};
    total.value+=term(aa,x.first.scale,t.first.scale);
    total.value+=term(ba,x.residual.scale,t.first.scale);
    total.value+=term(ab,x.first.scale,t.residual.scale);
    const std::array<double,8> energies{x.first.original_energy_upper,t.first.original_energy_upper,
        x.first.reconstructed_energy_upper,x.residual.reconstructed_energy_upper,
        t.first.reconstructed_energy_upper,t.residual.reconstructed_energy_upper,
        x.tail_norm_upper*x.tail_norm_upper,t.tail_norm_upper*t.tail_norm_upper};
    for(std::size_t i=0;i<energies.size();++i)total.energy[i]+=energies[i];
    ++total.pieces;
}
BoundedDot finish_compensated(const CompensatedReduction& total) {
    if(!total.pieces)return {};
    if(!finite(total.value)||!gradual_underflow<double>())return {total.value,infinity};
    std::array<double,8> norm;
    for(std::size_t i=0;i<norm.size();++i)norm[i]=sqrt_up(nonnegative_sum_upper(total.energy[i],total.pieces));
    const auto [nx,nt,ax,bx,at,bt,ex,et]=norm;
    const auto qx=std::min(add_up(ax,bx),add_up(nx,ex)),qt=std::min(add_up(at,bt),add_up(nt,et));
    const auto residual=add_up(mul_up(bx,bt),std::min(add_up(mul_up(ex,nt),mul_up(qx,et)),
        add_up(mul_up(et,nx),mul_up(qt,ex))));
    // Each exact integer converts exactly (<=2^25 per component). Scales,
    // products and accumulation have at most16*m+16 rounded primitives along
    // any path. Account separately for subnormal scale products amplified by
    // the integer before widening; overflow or disabled gradual underflow is
    // always inconclusive. No candidate can be rejected on such a result.
    const auto operations=add_up(mul_up(16,count_upper(total.pieces)),16);
    const auto gamma=gamma_upper(operations,std::numeric_limits<double>::epsilon());
    if(!std::isfinite(gamma))return {total.value,infinity};
    const auto magnitude=add_up(add_up(mul_up(ax,at),mul_up(bx,at)),mul_up(ax,bt));
    const auto tiny=mul_up(mul_up(operations,32768.*quantized_tile_size+4),std::numeric_limits<double>::denorm_min());
    const auto roundoff=mul_up(sqrt_up(2),add_up(mul_up(gamma,magnitude),mul_up(tiny,add_up(1,gamma))));
    return {total.value,add_up(residual,roundoff)};
}
void accumulate(BoundedDot& result, const BoundedDot& addend) {
    const auto next = result.value + addend.value;
    if (!finite(next) || !(result.error >= 0) || !(addend.error >= 0)) {
        result = {next, infinity}; return;
    }
    const auto real_error = result.value.real() == 0 || addend.value.real() == 0 ? 0 : ulp(next.real());
    const auto imag_error = result.value.imag() == 0 || addend.value.imag() == 0 ? 0 : ulp(next.imag());
    result.error = add_up(add_up(result.error, addend.error), disk(real_error, imag_error));
    result.value = next;
}
double magnitude_upper(const BoundedDot& value) {
    if (!finite(value.value) || !(value.error >= 0)) return infinity;
    return add_up(disk(std::abs(value.value.real()), std::abs(value.value.imag())), value.error);
}
Complex dot_fp32(std::span<const Complex> x, std::span<const Complex> t, Backend backend) {
    validate_fp(x, t, true);
    switch (selected_backend(backend)) {
#ifdef DATAPUMP_SEARCH_AVX2
    case Backend::avx2: return fp32_avx2(x, t);
#endif
#ifdef DATAPUMP_SEARCH_SSE2
    case Backend::sse2: return fp32_sse2(x, t);
#endif
    default: return fp32_scalar(x, t);
    }
}
Complex dot_fp64(std::span<const Complex> x, std::span<const Complex> t, Backend backend) {
    validate_fp(x, t);
    switch (selected_backend(backend)) {
#ifdef DATAPUMP_SEARCH_AVX2
    case Backend::avx2: return fp64_avx2(x, t);
#endif
#ifdef DATAPUMP_SEARCH_SSE2
    case Backend::sse2: return fp64_sse2(x, t);
#endif
    default: return fp64_scalar(x, t);
    }
}
double reference_roundoff_error(double x, double t, std::size_t count) {
    if (!count) return 0;
    if (!gradual_underflow<double>()) return infinity;
    const auto n = count_upper(count);
    const auto operations = add_up(mul_up(4, n), 16);
    const auto gamma = gamma_upper(operations, std::numeric_limits<double>::epsilon());
    if (!std::isfinite(gamma)) return infinity;
    const auto relative = mul_up(gamma, energy_product(x, t));
    const auto absolute = mul_up(mul_up(add_up(mul_up(8, n), 16),
        std::numeric_limits<double>::denorm_min()), add_up(1, gamma));
    return mul_up(sqrt_up(2), add_up(relative, absolute));
}
double fp32_roundoff_error(double x, double t, std::size_t count) {
    if (!count) return 0;
    if (!gradual_underflow<double>() || !gradual_underflow<float>()) return infinity;
    if (!(x >= 0) || !(t >= 0)) return infinity;
    const auto n = count_upper(count);
    const auto epsilon = static_cast<double>(std::numeric_limits<float>::epsilon());
    const auto tiny = static_cast<double>(std::numeric_limits<float>::denorm_min());
    const auto conversion_tiny = mul_up(sqrt_up(mul_up(2, n)), tiny);
    const auto nx = sqrt_up(x), nt = sqrt_up(t);
    const auto ex = add_up(mul_up(epsilon, nx), conversion_tiny);
    const auto et = add_up(mul_up(epsilon, nt), conversion_tiny);
    const auto qx = add_up(nx, ex), qt = add_up(nt, et);
    const auto conversion = add_up(mul_up(ex, nt), mul_up(qx, et));
    const auto operations = add_up(mul_up(4, n), 16);
    const auto gamma = gamma_upper(operations, epsilon);
    if (!std::isfinite(gamma)) return infinity;
    const auto relative = mul_up(gamma, mul_up(qx, qt));
    const auto absolute = mul_up(mul_up(add_up(mul_up(8, n), 16), tiny), add_up(1, gamma));
    return add_up(conversion, mul_up(sqrt_up(2), add_up(relative, absolute)));
}
} // namespace datapump::modem::detail::search_arithmetic
