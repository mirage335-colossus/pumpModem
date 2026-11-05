#pragma once
#include "datapump/triad_modem_v3.hpp"
#include <string>

namespace datapump::triad::v3 {
// Public, keyed-pattern-independent engineering spectrum. This is expected
// energy for zero-mean independent complex coefficients, not compact support
// or a worst-case bound over every possible encrypted pattern/channel.
struct ShapingOptions {
    double tail_energy_fraction = .001;
    // Begin at no more than64 phases; refine only on failed phase
    // convergence, up to this explicit cap. Small exact phase banks stop early.
    unsigned maximum_phase_samples = 256;
    std::size_t maximum_fft_size = 32768;
    bool allow_center_alignment = true;
};
struct SpectralCertificate {
    double measured_bandwidth_hz = 0, placement_bandwidth_hz = 0;
    double measured_tail_fraction = 0, requested_tail_fraction = 0;
    double coarse_width_hz = 0, refined_width_hz = 0;
    double phase_refinement_width_hz = 0, phase_refinement_error_hz = 0;
    double sampling_refinement_error_hz = 0;
    double frequency_bin_hz = 0, far_outside_energy_fraction = 0;
    double normalized_pulse_energy = 0;
    unsigned phase_samples = 0;
    // kernel_ffts describes the selected certificate's distinct resolution/
    // phase profiles; total_kernel_ffts also includes earlier adaptive looks
    // for this candidate, with cached profiles counted only when executed.
    std::size_t fft_size = 0, kernel_ffts = 0, total_kernel_ffts = 0;
    unsigned phase_refinement_attempts = 0;
    bool converged = false;
    std::string method, limitation;
};
struct ShapingPlan {
    Format format{};
    Geometry geometry{};
    SpectralCertificate spectrum{};
    bool fallback = false;
    std::string reason;
    unsigned candidate_count = 0;
    // All executed kernel FFTs across every candidate and failed adaptive look.
    // Geometry, allocation, FFT planning and finalization are separate costs.
    std::size_t total_kernel_ffts = 0;
};
const char* pulse_shape_name(PulseShape);
double shaping_pulse(PulseShape,double chip_offset);
// Same source envelope used by renderer and spectral kernels. Physical
// propagation follows shaping and must retain its own complete tails.
double shaping_envelope(const Format&,const Geometry&,long double calendar_seconds);
SpectralCertificate shaping_spectrum(const Format&,const ShapingOptions& = {});
// Bounded pulse/ramp/lattice search; preserves a valid input's protected-cell
// count, tile duration and full calendar. Invalid FHSS aperture may be repaired
// only by selecting a feasible lattice/ramp. Other failures remain explicit.
// Unsupported/nonconverged spectra retain the old nominal+2/ramp allowance.
ShapingPlan plan_shaping(const Format&,const ShapingOptions& = {});
}
