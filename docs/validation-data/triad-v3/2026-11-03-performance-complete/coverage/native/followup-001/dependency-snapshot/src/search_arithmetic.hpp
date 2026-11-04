#pragma once

#include <complex>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::modem::detail::search_arithmetic {

// All storage belongs to the caller. Packing bounds both setup and certificate
// work; a shifted-input matrix is neither required nor constructed here.
inline constexpr std::size_t quantized_tile_size = 1024;
inline constexpr std::size_t integer_chunk_size = 16384;
enum class Backend { automatic, scalar, sse2, avx2 };
bool backend_available(Backend) noexcept;
Backend selected_backend(Backend = Backend::automatic);
const char* backend_name(Backend = Backend::automatic);

struct Complex8 { std::int8_t real = 0, imag = 0; };
static_assert(sizeof(Complex8) == 2);
struct IntegerDot { std::int64_t real = 0, imag = 0; };
struct QuantizedBlock {
    double scale = 0;
    double original_energy_upper = 0;
    double reconstructed_energy_upper = 0;
    double residual_energy_upper = 0;
    std::size_t size = 0;
};
struct CompensatedBlock {
    QuantizedBlock first, residual;
    double tail_norm_upper = 0;
};
struct BoundedDot {
    std::complex<double> value{};
    // Radius of a disk containing the exact mathematical original-double dot.
    // Infinity is an inconclusive certificate and must cause exact refinement.
    double error = 0;
};

// Common I/Q scale, symmetric [-127,127], nearest integer (ties away from zero).
// Rejects nonfinite inputs, overlapping output, insufficient storage and >1024
// values before mutating output. The energies bound the original values, the
// exact scale*integer reconstruction and their exact difference, respectively.
QuantizedBlock pack(std::span<const std::complex<double>>,
                    std::span<Complex8> output);

// Fixed two-plane INT8 representation. Both planes use the same integer ISA;
// this is compensated INT8 arithmetic, never adaptive operand precision.
CompensatedBlock pack_compensated(std::span<const std::complex<double>>,
    std::span<Complex8> first,std::span<Complex8> residual);
BoundedDot combine_compensated(IntegerDot first_first,IntegerDot residual_first,
    IntegerDot first_residual,const CompensatedBlock& x,const CompensatedBlock& t);

// Whole-reduction certificate: ordinary positive energy sums are enclosed once
// at finish, so each short shifted tile does no transcendental bound work.
struct CompensatedReduction {
    std::complex<double> value{};
    std::array<double,8> energy{}; // X,T,A,B,C,D,E,F squared norms
    std::size_t pieces = 0;
};
void accumulate_compensated(CompensatedReduction&,IntegerDot first_first,
    IntegerDot residual_first,IntegerDot first_residual,const CompensatedBlock& x,const CompensatedBlock& t);
BoundedDot finish_compensated(const CompensatedReduction&);

// Exact sum of x*conj(t), including -128 if explicitly supplied by a caller.
// int32 chunks have <=16384 complex values: each component is bounded by32768
// per value, so each complete chunk fits int32. Chunks widen to checked int64.
IntegerDot dot_i8(std::span<const Complex8> x, std::span<const Complex8> t,
                  Backend = Backend::automatic);
// Three exact products for fixed compensated operands: x0*t0, x1*t0,
// x0*t1, all conjugating t. The AVX2 implementation shares loads/widening;
// other CPUs execute the identical three integer products.
std::array<IntegerDot,3> dot_compensated_i8(std::span<const Complex8> x0,
    std::span<const Complex8> x1,std::span<const Complex8> t0,
    std::span<const Complex8> t1,Backend = Backend::automatic);
void accumulate(IntegerDot&, IntegerDot); // checked, no partial mutation

// Reconstruct the integer dot and bound quantization plus reconstruction
// rounding. Accumulation accounts for rounding of the approximate block sums.
BoundedDot combine(IntegerDot, const QuantizedBlock& x, const QuantizedBlock& t);
void accumulate(BoundedDot&, const BoundedDot&);
double magnitude_upper(const BoundedDot&);

// SIMD dots use the original double operands; FP32 casts them only at each
// load, accumulates in float, and widens the result. FP64 accumulates in double.
// Reduction order can differ from the receiver's ascending reference loop.
// Unsupported explicitly requested ISA, unequal lengths or nonfinite operands
// throw, as do operands outside FP32's finite range when FP32 is requested.
// Finite products/sums can overflow; a nonfinite result never supplies a usable
// screening certificate. Certificates are inconclusive with disabled gradual
// underflow (for example a caller that enables x86 DAZ/FTZ).
std::complex<double> dot_fp32(std::span<const std::complex<double>> x,
                             std::span<const std::complex<double>> t,
                             Backend = Backend::automatic);
std::complex<double> dot_fp64(std::span<const std::complex<double>> x,
                             std::span<const std::complex<double>> t,
                             Backend = Backend::automatic);

// Conservative absolute-error disks relative to mathematical original-double
// products. Supply outward upper original energies (pack() provides them).
// FP32 includes conversion error and gradual-underflow allowance; values outside
// FP32's finite range invalidate that width even if this radius is finite.
// Add the FP64 reference radius once for the COMPLETE observation, rather than
// once per tile, when screening against the original ascending FP64 dot.
double fp32_roundoff_error(double x_energy_upper, double t_energy_upper,
                           std::size_t count);
double reference_roundoff_error(double x_energy_upper, double t_energy_upper,
                                std::size_t count);

} // namespace datapump::modem::detail::search_arithmetic
