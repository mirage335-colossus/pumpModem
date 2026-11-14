#pragma once

#include "triad_receiver_driver.hpp"
#include "triad_probe_json.hpp"
#include <memory>

namespace appraisal {

// Experimental fixture owner. The caller must verify raw IID white-noise IQ
// and retain this owner (or the callbacks returned by supplement()) until all
// native receiver work has joined. Numeric telemetry is bounded by the public
// configured telemetry count; no per-coordinate trace is retained here. Zero
// disables report storage. Overflow/duplicate telemetry never rejects evidence
// and is reported by saturating counters outside inference.
class HfPublicPhaseReceiver {
public:
    explicit HfPublicPhaseReceiver(std::size_t maximum_slot_reports, bool phase_refinement = false);
    datapump::triad::v3::receiver_detail::SlotEvidenceSupplement supplement() const;
    datapump::triad::probe::Json report() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace appraisal
