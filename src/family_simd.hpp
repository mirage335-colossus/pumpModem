#pragma once
// Artifact prototype only; root owns production integration and qualification.
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::modem::detail::family_simd {
enum class Backend { automatic, scalar, sse2, avx2 };
bool available(Backend) noexcept;
Backend selected(Backend = Backend::automatic);
const char* name(Backend = Backend::automatic);

// Each sample row has either candidates scalars (row_stride>=candidates) or
// one shared scalar (row_stride==0). Plane rows remain in physical sample order.
struct RawFamily {
    std::size_t candidates=0, samples=0;
    std::size_t projection_row_stride=0, template_row_stride=0;
    std::array<std::span<const double>,6> projection; // xc,xs,cc,ss,cs,energy
    std::array<std::array<std::span<const double>,2>,2> coefficient; // bit,I/Q
};
struct FitPlanes {
    std::array<std::span<double>,6> value; // xc,xs,cc,ss,cs,energy
    std::span<std::uint64_t> count;
};
// No allocation, row sorting, temporal reduction, admission or detector logic.
// Repeats the original scheduled Fit contribution (+0 then target+=contribution)
// in ascending sample order for each bit and candidate. SIMD lanes are candidates.
// Outputs and inputs must not overlap and output planes must not overlap one
// another. Caller keeps all views leased for this synchronous call. Use bounded
// tiles ending before every detector/window/symbol/progress/cancellation fence.
// Shape/count/ISA errors are checked before any output mutation. Nonfinite values
// propagate; original host numerical checks/fallback still govern publication.
void add_raw_f64(const RawFamily&, const std::array<FitPlanes,2>&,
                 Backend = Backend::automatic);

// Two complex products followed by addition, with original grouped association:
// real=(ar*z0r-ai*z0i)+(br*z1r-bi*z1i), imag likewise. Native T throughout.
// FP32 use requires separate operand/range/certificate authorization; ISA or
// policy alone never authorizes narrowing Gram, energy or final evidence.
void affine_f64(std::span<const double> ar,std::span<const double> ai,
    std::span<const double> br,std::span<const double> bi,
    std::complex<double> z0,std::complex<double> z1,
    std::span<double> real,std::span<double> imag,Backend = Backend::automatic);
void affine_f32(std::span<const float> ar,std::span<const float> ai,
    std::span<const float> br,std::span<const float> bi,
    std::complex<float> z0,std::complex<float> z1,
    std::span<float> real,std::span<float> imag,Backend = Backend::automatic);
} // namespace datapump::modem::detail::family_simd
