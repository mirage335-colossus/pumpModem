#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_probe_json.hpp"

namespace appraisal {

inline constexpr std::size_t hf_gui_varying_maximum_aperture_rows = 32768;

class HfGuiDiagnosticUnavailable : public datapump::Error {
public:
    using datapump::Error::Error;
};

struct HfGuiCleanInput {
    std::span<const datapump::triad::v3::Complex> iq;
    // Present only in the wrong-key evaluator. It selects an independent
    // noisy public preparation, never the receiver's original preparation.
    const datapump::Crypto* transmitter_key = nullptr;
};

// Score two existing branches on one immutable complete public aperture.
// The caller owns the sample/validity lease and its observation digest.
datapump::triad::probe::Json diagnose_hf_gui_varying(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&,
    const datapump::Crypto&, std::span<const datapump::triad::v3::Complex>,
    std::span<const std::uint8_t> validity, unsigned workers, std::size_t total_memory_limit_bytes,
    std::stop_token, const HfGuiCleanInput* = nullptr);

} // namespace appraisal
