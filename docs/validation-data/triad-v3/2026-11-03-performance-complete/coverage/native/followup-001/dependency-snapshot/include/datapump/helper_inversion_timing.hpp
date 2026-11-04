#pragma once

#include "datapump/helper_receiver.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::helper {

// Standalone adapter for the EXISTING auxiliary phase-inversion detector.
// Uses actual complex capture samples (the real quadrature is sufficient),
// not pilot-envelope ramps, probe samples, decoded metadata or simulator truth.
// This is an optional strong-signal measurement, not weak-link qualification.
// Published dates stay in original capture coordinates and include FIR support.
class HelperInversionTiming {
public:
    struct Config {
        std::uint32_t sample_rate=24000;
        // Receiver-derived frequency centers. The existing detector retains
        // its discovery range; these are not tone/phase observations themselves.
        double auxiliary_hz=950,high_hz=2400,low_hz=550;
    };
    explicit HelperInversionTiming(Config);
    // One complete nominal dwell or smaller input chunks; bounded work/memory.
    // A real capture gap resets only this optional detector's evidence. It must
    // not reset the caller's permanent correction reference or fill the gap.
    void observe(std::span<const std::complex<float>>,std::uint64_t first_sample);
    // Drain observations first: an explicit or automatic capture-gap reset
    // clears pending observations as well as the detector's local history.
    void missing(std::uint64_t next_sample);
    // Drain until known=false before supplying more input. Sixty-four dated
    // observations are prepaid; overflow is an error, never a silent overwrite.
    PunctuationObservation take_update() noexcept;
    std::uint64_t distinct_markers() const noexcept{return distinct_markers_;}
    std::size_t workspace_bytes() const noexcept;
private:
    Config config_;
    AuxiliaryMetadataReceiver receiver_;
    std::array<float,1024> real_{};
    std::array<PunctuationObservation,64> pending_{};
    std::size_t pending_first_=0,pending_count_=0;
    std::uint64_t next_=0,distinct_markers_=0;
    bool started_=false;
};
}
