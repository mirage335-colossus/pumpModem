#pragma once

#include "datapump/helper_metadata.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace datapump::helper {

// A measured reference in the caller's absolute capture sample coordinates.
// Phase is the observed tone phase at sample, with a known punctuation sign
// removed after timing acquisition. It retains an unknown propagation/Tx phase.
// sample is also the latest raw observation used for this publication: no
// future input is hidden behind an earlier phase timestamp. Dwell timing is a
// modulo-400 ms measurement, not an authenticated clock.
// Tone uncertainty combines an eight-standard-error local linearized LS term
// (actual finite Gram covariance, estimated white-noise residual), numerical
// floors and measured polynomial-model allowance. It is not a deterministic
// bound for interference or future oscillator drift; callers retain fallback.
struct Reference {
    std::uint64_t sample=0;
    std::uint32_t sample_rate=0;
    double frequency_hz=0,phase_radians=0;
    double frequency_uncertainty_hz=0,phase_uncertainty_radians=0;
    double dwell_start_seconds=0,dwell_uncertainty_seconds=0;
    bool frequency_valid=false,phase_valid=false,timing_valid=false;
    std::optional<Metadata> metadata;
    // Independent frequency component's last observation; zero means sample.
    // Phase remains evaluated at sample. A fused component is never from the
    // future and callers must apply their frequency-age limit to this field.
    std::uint64_t frequency_sample=0;
};

// Fixed public occupied-band geometry in the physical PCM clock. Conditioning
// is optional and refines only already accepted raw tone publications. A band
// that is invalid, too wide, or overlaps the helper search retains the raw fit.
struct OccupiedBand {double lower_hz=0,upper_hz=0;};

// Independent assistance, never payload completion or presence admission.
// Strong coherent fits retain phase/metadata measurement. A separate raw-noise-
// calibrated narrowband fit can publish frequency-only references after several
// independent windows; modulo-pi ambiguity keeps the interval wide. Its fixed
// grid AWGN admission/variance spending totals <=.01 per explicit reset; refined
// frequency intervals remain local linearized LS/model intervals. An unsuitable
// helper yields no useful reference and leaves the original search available.
class Receiver {
public:
    // Receiver-owned storage plus project scratch; the pre-existing external
    // C/C++/libm/libgcc/OpenSSL runtime stack/provider allocations are separate.
    static constexpr std::size_t workspace_bound=9*1024;
    // Only the first receive key is selected for compact encrypted time. No
    // helper metadata is authenticated or used here to adjust a clock/grid.
    explicit Receiver(double expected_hz,double search_half_width_hz=20,
                      std::vector<Crypto> keys={},
                      std::optional<OccupiedBand> ordinary_band={});
    ~Receiver();
    Receiver(Receiver&&) noexcept;
    Receiver& operator=(Receiver&&) noexcept;
    Receiver(const Receiver&)=delete;
    Receiver& operator=(const Receiver&)=delete;
    void push(std::span<const float>,std::uint32_t rate,std::uint64_t first_sample);
    // A decoded metadata publication survives later tone-only fits in the same
    // push, and can consequently be older than the latest coherent tone fit.
    std::optional<Reference> take_update();
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
