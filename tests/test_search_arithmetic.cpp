#include "../src/search_arithmetic.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

using namespace datapump;
namespace arithmetic = datapump::modem::detail::search_arithmetic;
using Complex = std::complex<double>;
using Wide = std::complex<long double>;
namespace {
void check(bool condition, const char* message) { if (!condition) throw Error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const Error&) { rejected = true; }
    check(rejected, message);
}
std::vector<arithmetic::Backend> backends() {
    std::vector<arithmetic::Backend> result{arithmetic::Backend::scalar};
    for (auto backend : {arithmetic::Backend::sse2, arithmetic::Backend::avx2})
        if (arithmetic::backend_available(backend)) result.push_back(backend);
    return result;
}
Wide oracle(std::span<const Complex> x, std::span<const Complex> t) {
    Wide result;
    for (std::size_t i = 0; i < x.size(); ++i)
        result += Wide{x[i].real(), x[i].imag()} * std::conj(Wide{t[i].real(), t[i].imag()});
    return result;
}
long double energy(std::span<const Complex> x) {
    long double result = 0;
    for (auto value : x) result += static_cast<long double>(value.real()) * value.real() +
                                   static_cast<long double>(value.imag()) * value.imag();
    return result;
}
double energy_upper(std::span<const Complex> values) {
    std::array<arithmetic::Complex8, arithmetic::quantized_tile_size> scratch{};
    double result = 0;
    for (std::size_t offset = 0; offset < values.size(); offset += scratch.size()) {
        const auto count = std::min(scratch.size(), values.size() - offset);
        const auto block = arithmetic::pack(values.subspan(offset, count), scratch);
        result = std::nextafter(result + block.original_energy_upper, std::numeric_limits<double>::infinity());
    }
    return result;
}
void packing_orientation_and_validation() {
    const std::array<Complex, 8> values{{{127, -127}, {1, -2}, {-.5, .5},
        {63.5, -63.5}, {0, 0}, {-126, 126}, {3, -4}, {-.25, .25}}};
    std::array<arithmetic::Complex8, 10> packed;
    std::fill(packed.begin(), packed.end(), arithmetic::Complex8{19, -23});
    const auto block = arithmetic::pack(values, packed);
    check(block.size == values.size() && block.scale == 1, "quantizer changed common I/Q scale");
    const std::array<arithmetic::Complex8, 8> expected{{{127, -127}, {1, -2}, {-1, 1},
        {64, -64}, {0, 0}, {-126, 126}, {3, -4}, {0, 0}}};
    for (std::size_t i = 0; i < values.size(); ++i)
        check(packed[i].real == expected[i].real && packed[i].imag == expected[i].imag,
              "quantizer lost I/Q, symmetric range or specified tie behavior");
    check(packed[8].real == 19 && packed[9].imag == -23, "packing overwrote output padding");
    const auto original = packed;
    auto invalid = values;
    invalid.back() = {std::numeric_limits<double>::infinity(), 0};
    rejects([&] { arithmetic::pack(invalid, packed); }, "packing accepted nonfinite input");
    check(std::equal(packed.begin(), packed.end(), original.begin(), [](auto a, auto b) {
        return a.real == b.real && a.imag == b.imag;
    }), "invalid packing partially mutated output");
    invalid.back() = {0, std::numeric_limits<double>::quiet_NaN()};
    rejects([&] { arithmetic::pack(invalid, packed); }, "packing accepted NaN input");
    rejects([&] { arithmetic::pack(values, std::span(packed).first(7)); }, "packing ignored output bound");
    auto mutable_values = values;
    auto aliased = std::span(reinterpret_cast<arithmetic::Complex8*>(mutable_values.data()), values.size());
    rejects([&] { arithmetic::pack(mutable_values, aliased); }, "packing accepted aliased output");
    std::vector<Complex> oversized(arithmetic::quantized_tile_size + 1);
    std::vector<arithmetic::Complex8> oversized_output(oversized.size());
    rejects([&] { arithmetic::pack(oversized, oversized_output); }, "packing exceeded bounded tile size");
    std::array<Complex, 8> zero{};
    const auto z = arithmetic::pack(zero, packed);
    check(z.scale == 0 && z.original_energy_upper == 0 && z.reconstructed_energy_upper == 0 &&
          z.residual_energy_upper == 0, "zero tile created spurious energy or residual");
    const auto empty = arithmetic::pack({}, {});
    check(empty.size == 0 && empty.scale == 0, "empty packing changed geometry");
}
void packing_boundary_parity_and_small_residuals() {
    // Force scale=1, exercise the immediately adjacent representable values
    // around every half-integer, and compare every packed byte to std::round.
    std::vector<Complex> values{{127., -127.}};
    for (int i = 0; i < 127; ++i) {
        const auto middle = i + .5;
        for (auto v : {std::nextafter(middle, 0.), middle,
                       std::nextafter(middle, std::numeric_limits<double>::infinity())})
            values.push_back({v, -v});
    }
    std::vector<arithmetic::Complex8> packed(values.size());
    auto q = arithmetic::pack(values, packed);
    check(q.scale == 1, "half-integer parity fixture lost exact scale");
    for (std::size_t i = 0; i < values.size(); ++i)
        check(packed[i].real == static_cast<int>(std::round(values[i].real())) &&
              packed[i].imag == static_cast<int>(std::round(values[i].imag())),
              "cheap quantizer changed std::round at a half-integer boundary");
    // Rounded reconstructions can equal input exactly while the exact s*q
    // reconstruction differs: residual certification must include that error.
    values.resize(arithmetic::quantized_tile_size);
    packed.resize(values.size());
    const auto scale = .17 / 127.;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto integer = static_cast<int>(i % 127) + 1;
        values[i] = {integer * scale, -integer * scale};
    }
    values[0] = {.17, -.17};
    q = arithmetic::pack(values, packed);
    long double residual = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const Wide v{values[i].real(), values[i].imag()};
        const Wide r{static_cast<long double>(packed[i].real) * q.scale,
                     static_cast<long double>(packed[i].imag) * q.scale};
        residual += std::norm(v - r);
    }
    check(residual <= q.residual_energy_upper && q.residual_energy_upper > 0 &&
          std::isfinite(q.residual_energy_upper), "block residual enclosure lost rounded reconstruction error");
    // Squared products and sums are subnormal here: an ordinary relative-only
    // energy inflation could incorrectly certify zero.
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = {std::ldexp(1. + static_cast<double>(i % 9) / 16., -530),
                    -std::ldexp(1. + static_cast<double>(i % 7) / 16., -530)};
    q = arithmetic::pack(values, packed);
    residual = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const Wide v{values[i].real(), values[i].imag()};
        const Wide r{static_cast<long double>(packed[i].real) * q.scale,
                     static_cast<long double>(packed[i].imag) * q.scale};
        residual += std::norm(v - r);
    }
    check(energy(values) <= q.original_energy_upper && residual <= q.residual_energy_upper &&
          std::isfinite(q.original_energy_upper) && std::isfinite(q.residual_energy_upper),
          "block energy enclosure missed gradual underflow");
    check(std::isinf(arithmetic::fp32_roundoff_error(0, 0, std::numeric_limits<std::size_t>::max())),
          "unbounded FP32 operation count supplied a finite radius");
    if constexpr (std::numeric_limits<std::size_t>::digits >= 53)
        check(std::isinf(arithmetic::reference_roundoff_error(0, 0, std::numeric_limits<std::size_t>::max())),
              "unbounded FP64 operation count supplied a finite radius");
}
void compensated_tile_certificates() {
    std::mt19937_64 random(614131);std::normal_distribution<double> normal;
    std::array<Complex,arithmetic::quantized_tile_size> x,t;
    std::array<arithmetic::Complex8,arithmetic::quantized_tile_size> x0,x1,t0,t1;
    for(unsigned kind=0;kind<9;++kind) {
        for(std::size_t i=0;i<x.size();++i) {
            x[i]={normal(random),normal(random)};t[i]={normal(random),normal(random)};
            if(kind==1)t[i]=x[i]; // coherent low*low omission must be bounded
            if(kind==2){x[i]*=1e-150;t[i]*=1e150;}
            if(kind==3){x[i]*=1e150;t[i]*=1e-150;}
            if(kind==4){x[i]*=1e-159;t[i]*=1e-159;}
            if(kind==5){x[i]={.5+static_cast<double>(i%127),-.5};t[i]=x[i];}
            if(kind==6){x[i]={0,0};t[i]={0,0};}
            if(kind==7){x[i]={std::sin(.031*i),std::cos(.1*i)};t[i]=std::conj(x[i]);}
            if(kind==8){x[i]*=std::ldexp(1.,static_cast<int>(i%20)-10);t[i]*=1e-8;}
        }
        auto qx=arithmetic::pack_compensated(x,x0,x1),qt=arithmetic::pack_compensated(t,t0,t1);
        for(const auto n:{std::size_t{1},std::size_t{17},std::size_t{1024}}) {
            const auto start=x.size()-n;
            auto a=qx,b=qt;a.first.size=a.residual.size=b.first.size=b.residual.size=n;
            const auto xx0=std::span(x0).subspan(start,n),xx1=std::span(x1).subspan(start,n);
            const auto tt0=std::span(t0).first(n),tt1=std::span(t1).first(n);
            const auto bound=arithmetic::combine_compensated(arithmetic::dot_i8(xx0,tt0),
                arithmetic::dot_i8(xx1,tt0),arithmetic::dot_i8(xx0,tt1),a,b);
            const auto exact=oracle(std::span(x).subspan(start,n),std::span(t).first(n));
            const Wide approximate{bound.value.real(),bound.value.imag()};
            check(std::abs(exact-approximate)<=bound.error,"compensated INT8 disk lost original complex dot or sliced residual energy");
            arithmetic::CompensatedReduction reduction;
            for(unsigned repeat=0;repeat<129;++repeat)
                arithmetic::accumulate_compensated(reduction,arithmetic::dot_i8(xx0,tt0),
                    arithmetic::dot_i8(xx1,tt0),arithmetic::dot_i8(xx0,tt1),a,b);
            const auto whole=arithmetic::finish_compensated(reduction);
            check(std::abs(129.L*exact-Wide{whole.value.real(),whole.value.imag()})<=whole.error,
                "whole compensated reduction lost cumulative roundoff/underflow certificate");
        }
    }
    rejects([&]{arithmetic::pack_compensated(x,x0,x0);},"compensated planes accepted overlapping storage");
    const auto original=x;const auto previous=x0;
    auto alias=std::span(reinterpret_cast<arithmetic::Complex8*>(x.data()),x.size());
    rejects([&]{arithmetic::pack_compensated(x,x0,alias);},"compensated residual plane overwrote immutable input");
    check(x==original&&std::equal(x0.begin(),x0.end(),previous.begin(),[](auto a,auto b){return a.real==b.real&&a.imag==b.imag;}),
        "compensated alias rejection mutated input/first plane");
}
void integer_exact_and_chunk_overflow() {
    // Includes -128, intentionally outside the symmetric packer's range, and
    // enough repeated maximal products to exceed int32 over the whole row.
    const std::size_t size = arithmetic::integer_chunk_size * 5 + 17;
    std::vector<arithmetic::Complex8> x(size), t(size);
    for (std::size_t i = 0; i < size; ++i) {
        x[i] = {static_cast<std::int8_t>(i % 3 == 0 ? -128 : 127),
                static_cast<std::int8_t>(i % 5 == 0 ? 127 : -128)};
        t[i] = {static_cast<std::int8_t>(i % 7 == 0 ? 127 : -128),
                static_cast<std::int8_t>(i % 11 == 0 ? -128 : 127)};
    }
    for (auto backend : backends()) {
        for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{8},
                std::size_t{9}, arithmetic::integer_chunk_size - 1, arithmetic::integer_chunk_size,
                arithmetic::integer_chunk_size + 1, size}) {
            arithmetic::IntegerDot expected;
            for (std::size_t i = 0; i < count; ++i) {
                const auto xr = static_cast<std::int64_t>(x[i].real), xi = static_cast<std::int64_t>(x[i].imag);
                const auto tr = static_cast<std::int64_t>(t[i].real), ti = static_cast<std::int64_t>(t[i].imag);
                expected.real += xr * tr + xi * ti; expected.imag += xi * tr - xr * ti;
            }
            const auto actual = arithmetic::dot_i8(std::span(x).first(count), std::span(t).first(count), backend);
            check(actual.real == expected.real && actual.imag == expected.imag,
                  "integer SIMD dot changed sign, tail, lane reduction or chunk result");
        }
        const auto offset = arithmetic::dot_i8(std::span(x).subspan(1, 1031), std::span(t).subspan(3, 1031), backend);
        const auto scalar = arithmetic::dot_i8(std::span(x).subspan(1, 1031), std::span(t).subspan(3, 1031), arithmetic::Backend::scalar);
        check(offset.real == scalar.real && offset.imag == scalar.imag, "integer unaligned rows changed a dot");
    }
    std::fill(x.begin(), x.end(), arithmetic::Complex8{-128, -128});
    const auto large = arithmetic::dot_i8(x, x);
    check(large.real == static_cast<std::int64_t>(size) * 32768 && large.imag == 0 &&
          large.real > std::numeric_limits<std::int32_t>::max(), "integer chunk widening lost large total");
    arithmetic::IntegerDot maximum{std::numeric_limits<std::int64_t>::max(), 17};
    rejects([&] { arithmetic::accumulate(maximum, {1, 1}); }, "integer accumulator wrapped positive int64");
    check(maximum.real == std::numeric_limits<std::int64_t>::max() && maximum.imag == 17,
          "integer overflow partially mutated accumulator");
    arithmetic::IntegerDot minimum{17, std::numeric_limits<std::int64_t>::min()};
    rejects([&] { arithmetic::accumulate(minimum, {1, -1}); }, "integer accumulator wrapped negative int64");
    check(minimum.real == 17 && minimum.imag == std::numeric_limits<std::int64_t>::min(),
          "second-component overflow partially mutated accumulator");
    rejects([&] { arithmetic::dot_i8(x, std::span(t).first(1)); }, "integer dot accepted unequal lengths");
}
void residual_and_dot_certificates() {
    std::mt19937_64 rng(0xb9ae88de7765ULL);
    std::uniform_real_distribution<double> sample(-1, 1);
    for (const auto size : {std::size_t{1}, std::size_t{3}, std::size_t{15}, std::size_t{1023}, std::size_t{1024}}) {
        for (const auto exponent : {-530, -35, 0, 35, 490}) {
            std::vector<Complex> x(size), t(size);
            for (std::size_t i = 0; i < size; ++i) {
                x[i] = {std::ldexp(sample(rng), exponent), std::ldexp(sample(rng), exponent)};
                t[i] = {std::ldexp(sample(rng), -exponent), std::ldexp(sample(rng), -exponent)};
            }
            std::vector<arithmetic::Complex8> qx(size), qt(size);
            const auto bx = arithmetic::pack(x, qx), bt = arithmetic::pack(t, qt);
            for (const auto data : {std::pair{std::span<const Complex>(x), std::pair{std::span<const arithmetic::Complex8>(qx), bx}},
                                    std::pair{std::span<const Complex>(t), std::pair{std::span<const arithmetic::Complex8>(qt), bt}}}) {
                const auto& block = data.second.second;
                long double original = 0, reconstructed = 0, residual = 0;
                for (std::size_t i = 0; i < size; ++i) {
                    const Wide v{data.first[i].real(), data.first[i].imag()};
                    const Wide q{static_cast<long double>(data.second.first[i].real) * block.scale,
                                 static_cast<long double>(data.second.first[i].imag) * block.scale};
                    original += std::norm(v); reconstructed += std::norm(q); residual += std::norm(v - q);
                }
                check(original <= block.original_energy_upper, "original energy upper bound rounded inward");
                check(reconstructed <= block.reconstructed_energy_upper, "reconstructed energy upper bound rounded inward");
                check(residual <= block.residual_energy_upper, "residual energy upper bound rounded inward");
            }
            const auto exact = oracle(x, t);
            for (auto backend : backends()) {
                const auto bounded = arithmetic::combine(arithmetic::dot_i8(qx, qt, backend), bx, bt);
                const Wide approx{bounded.value.real(), bounded.value.imag()};
                check(std::abs(exact - approx) <= bounded.error, "INT8 error disk missed original complex dot");
                check(std::abs(exact) <= arithmetic::magnitude_upper(bounded), "INT8 magnitude certificate rounded inward");
                auto partial = bounded;
                arithmetic::accumulate(partial, bounded);
                check(std::abs(2.L * exact - Wide{partial.value.real(), partial.value.imag()}) <= partial.error,
                      "bounded-dot accumulation missed complex sum rounding");
            }
        }
    }
    // Smallest subnormals and almost-maximal doubles may give conservative
    // infinite energy bounds; they must remain safe rather than fabricate zero.
    for (const auto a : {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
                        std::numeric_limits<double>::max()}) {
        const std::array<Complex, 1> x{{{a, -a}}};
        std::array<arithmetic::Complex8, 1> packed{};
        const auto b = arithmetic::pack(x, packed);
        check(b.scale > 0 && b.original_energy_upper > 0, "extreme finite packing silently lost its energy");
    }
    arithmetic::QuantizedBlock bad;
    bad.size = 1;
    rejects([&] { arithmetic::combine({32769, 0}, bad, bad); }, "certificate accepted impossible integer dot");
    bad.scale = -1;
    rejects([&] { arithmetic::combine({}, bad, bad); }, "certificate accepted negative scale");
    arithmetic::BoundedDot nonfinite{{std::numeric_limits<double>::infinity(), 0}, 0};
    check(std::isinf(arithmetic::magnitude_upper(nonfinite)), "nonfinite dot supplied a finite screening certificate");
}
void multi_tile_and_float_widths() {
    std::mt19937_64 rng(0x172713acULL);
    std::normal_distribution<double> gaussian;
    constexpr std::size_t size = arithmetic::quantized_tile_size * 3 + 19;
    std::vector<Complex> x(size), t(size);
    for (std::size_t i = 0; i < size; ++i) {
        const auto scale = i < 1024 ? .003 : i < 2048 ? 13. : .17;
        x[i] = {gaussian(rng) * scale, gaussian(rng) * scale};
        t[i] = {gaussian(rng) / scale, gaussian(rng) / scale};
    }
    const auto exact = oracle(x, t);
    const auto ex = energy_upper(x), et = energy_upper(t);
    check(energy(x) <= ex && energy(t) <= et, "multi-tile original energy accumulation rounded inward");
    for (auto backend : backends()) {
        arithmetic::BoundedDot total;
        std::array<arithmetic::Complex8, arithmetic::quantized_tile_size> qx{}, qt{};
        for (std::size_t begin = 0; begin < size; begin += qx.size()) {
            const auto count = std::min(qx.size(), size - begin);
            const auto bx = arithmetic::pack(std::span(x).subspan(begin, count), qx);
            const auto bt = arithmetic::pack(std::span(t).subspan(begin, count), qt);
            arithmetic::accumulate(total, arithmetic::combine(arithmetic::dot_i8(std::span(qx).first(count),
                                  std::span(qt).first(count), backend), bx, bt));
        }
        check(std::abs(exact - Wide{total.value.real(), total.value.imag()}) <= total.error,
              "changing tile scales invalidated full-observation certificate");
        const auto reference = arithmetic::dot_fp64(x, t, arithmetic::Backend::scalar);
        const auto reference_error = arithmetic::reference_roundoff_error(ex, et, size);
        check(std::abs(Wide{reference.real(), reference.imag()} - Wide{total.value.real(), total.value.imag()}) <=
              total.error + reference_error, "complete reference-roundoff margin missed ascending FP64 dot");
        for (auto count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7}, size}) {
            const auto xx = std::span(x).first(count), tt = std::span(t).first(count);
            const auto expected = oracle(xx, tt);
            const auto xe = energy_upper(xx), te = energy_upper(tt);
            const auto d32 = arithmetic::dot_fp32(xx, tt, backend);
            const auto d64 = arithmetic::dot_fp64(xx, tt, backend);
            check(std::abs(expected - Wide{d32.real(), d32.imag()}) <= arithmetic::fp32_roundoff_error(xe, te, count),
                  "FP32 width/reduction exceeded its original-double error bound");
            check(std::abs(expected - Wide{d64.real(), d64.imag()}) <= arithmetic::reference_roundoff_error(xe, te, count),
                  "FP64 SIMD reduction exceeded its error bound");
        }
    }
    const std::array<Complex, 2> a{{{16777217., 1}, {1, -1}}}, b{{{1, 1}, {2, 3}}};
    const auto d32 = arithmetic::dot_fp32(a, b, arithmetic::Backend::scalar);
    const auto d64 = arithmetic::dot_fp64(a, b, arithmetic::Backend::scalar);
    check(d32 != d64, "FP32 request silently used FP64 operands or accumulation");
    const std::array<Complex, 1> enormous{{{std::numeric_limits<double>::max(), 0}}};
    rejects([&] { arithmetic::dot_fp32(enormous, enormous); }, "FP32 accepted out-of-range conversion");
    rejects([&] { arithmetic::dot_fp64(a, std::span(b).first(1)); }, "FP64 accepted unequal row lengths");
    auto nan = a;
    nan[1] = {0, std::numeric_limits<double>::quiet_NaN()};
    rejects([&] { arithmetic::dot_fp32(nan, b); }, "FP32 accepted nonfinite input");
    rejects([&] { arithmetic::dot_fp64(nan, b); }, "FP64 accepted nonfinite input");
    check(std::isinf(arithmetic::reference_roundoff_error(-1, 1, 1)) &&
          std::isinf(arithmetic::fp32_roundoff_error(1, -1, 1)), "negative energy supplied a finite certificate");
}
void fused_integer_equivalence() {
    std::array<std::vector<arithmetic::Complex8>,4> rows;
    std::mt19937 random(93171);
    for(auto& row:rows){row.resize(32775);for(auto& z:row)z={static_cast<std::int8_t>(random()%256-128),static_cast<std::int8_t>(random()%256-128)};}
    for(const auto n:{0U,1U,7U,8U,15U,31U,64U,256U,1024U,16383U,16384U,16385U,32769U})
        for(const auto offset:{0U,1U,3U})for(const auto backend:backends()) {
            std::array<std::span<const arithmetic::Complex8>,4> v;
            for(unsigned j=0;j<4;++j)v[j]=std::span(rows[j]).subspan(offset,n);
            const auto fused=arithmetic::dot_compensated_i8(v[0],v[1],v[2],v[3],backend);
            const std::array<arithmetic::IntegerDot,3> expected{arithmetic::dot_i8(v[0],v[2],arithmetic::Backend::scalar),
                arithmetic::dot_i8(v[1],v[2],arithmetic::Backend::scalar),arithmetic::dot_i8(v[0],v[3],arithmetic::Backend::scalar)};
            for(unsigned j=0;j<3;++j)check(fused[j].real==expected[j].real&&fused[j].imag==expected[j].imag,
                "fused compensated kernel changed exact integer products");
        }
    for(auto& row:rows)row.assign(arithmetic::integer_chunk_size*5+17,arithmetic::Complex8{-128,-128});
    const auto worst=arithmetic::dot_compensated_i8(rows[0],rows[1],rows[2],rows[3]);
    for(auto z:worst)check(z.real==32768LL*static_cast<std::int64_t>(rows[0].size())&&
        z.real>std::numeric_limits<std::int32_t>::max()&&z.imag==0,"fused signed widening/chunk bound failed");
    rejects([&]{arithmetic::dot_compensated_i8(rows[0],{},rows[2],rows[3]);},"fused kernel ignored row lengths");
}
void dispatch_and_disabled_underflow() {
    check(arithmetic::backend_available(arithmetic::Backend::scalar), "portable scalar kernel missing");
    const auto selected = arithmetic::selected_backend();
    check(selected != arithmetic::Backend::automatic && arithmetic::backend_available(selected),
          "automatic dispatch selected unsupported instructions");
    check(std::string(arithmetic::backend_name()) == arithmetic::backend_name(selected),
          "actual dispatch diagnostic disagreed with selected backend");
    for (auto backend : {arithmetic::Backend::sse2, arithmetic::Backend::avx2})
        if (!arithmetic::backend_available(backend))
            rejects([&] { arithmetic::dot_i8({}, {}, backend); }, "explicit unavailable ISA silently fell back");
#if defined(__SSE2__) || defined(_M_X64)
    const auto original = _mm_getcsr();
    struct Restore { unsigned state; ~Restore() { _mm_setcsr(state); } } restore{original};
    _mm_setcsr(original | 0x8000U);
    const std::array<Complex, 1> values{{{1., 2.}}};
    std::array<arithmetic::Complex8, 1> output{};
    const auto q = arithmetic::pack(values, output);
    check(std::isinf(q.original_energy_upper) && std::isinf(q.residual_energy_upper) &&
          std::isinf(arithmetic::reference_roundoff_error(5, 5, 1)) &&
          std::isinf(arithmetic::fp32_roundoff_error(5, 5, 1)),
          "disabled gradual underflow fabricated a rigorous certificate");
#endif
}
} // namespace
int main() {
    try {
        packing_orientation_and_validation();
        packing_boundary_parity_and_small_residuals();
        compensated_tile_certificates();
        integer_exact_and_chunk_overflow();
        fused_integer_equivalence();
        residual_and_dot_certificates();
        multi_tile_and_float_widths();
        dispatch_and_disabled_underflow();
        std::cout << "search arithmetic passed; dispatch=" << arithmetic::backend_name() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
