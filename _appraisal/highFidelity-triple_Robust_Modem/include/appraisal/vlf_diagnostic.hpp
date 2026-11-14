#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_probe_json.hpp"

namespace appraisal {

// This component diagnostic borrows the complete, wholly valid first public
// observation. It runs the unchanged acquisition, then a short persistent
// fine session inside that observation. No bit or quiet decision is admitted.
// The caller separately admits and owns the immutable capture. The total
// memory limit excludes any source/evaluator reservations outside this call.
// Completed phases are written before the next phase can hit the stop token.
void diagnose_vlf_slot(datapump::triad::probe::Json& report,
                       const datapump::triad::v3::ReceiverConfig&,
                       const datapump::triad::v3::ReceiverSlotPlan&,
                       const datapump::Crypto&,
                       std::span<const datapump::triad::v3::Complex>,
                       unsigned workers, std::size_t total_memory_limit_bytes,
                       std::stop_token);

} // namespace appraisal
