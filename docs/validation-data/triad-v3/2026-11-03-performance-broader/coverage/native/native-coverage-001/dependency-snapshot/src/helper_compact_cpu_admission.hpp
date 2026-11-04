#pragma once
#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include <cmath>

namespace datapump::modem::detail {
// The helper-only constructor policy waits for this serial shared-pulse
// representation. This identifies its cost model, never a measured CPU cap.
inline bool helper_compact_serial_cpu_model(const Config& c,const PatternSearch& search) {
    const auto symbol=symbol_sample_count(c),chip=pattern_chip_samples(c);
    return search.helper_wait_for_shared_pulse && search.compact_clock_search &&
        search.start_offset_seconds && !search.hypotheses.empty() && !search.couple_clock_to_carrier &&
        c.receiver_strategy==ReceiverStrategy::automatic && c.data_key &&
        c.spreading_mode==SpreadingMode::pattern && (c.scramble || c.dsss) &&
        c.dsss_factor==1 && !c.symbol_burst.enabled() && !scheduled_bursts(c) && !deferred_symbols(c) &&
        pattern_pulse_enabled(c) && symbol>=60ULL*c.sample_rate && chip && symbol%(4*chip)==0;
}
// Call only at a NEW candidate's admission boundary. Existing/started owners
// are never replaced, paused or repriced by this decision. An unknown estimate
// cannot establish that this provisional engineering objective is exceeded.
inline bool helper_compact_cpu_admission_wait(bool manually_enabled,bool new_candidate,
        const Config& c,const PatternSearch& search,const ReceiverStrategyPlan& selected) {
    return manually_enabled && new_candidate && helper_compact_serial_cpu_model(c,search) &&
        selected.supported && !selected.empty_scheduled_epoch &&
        selected.backend==ReceiverBackend::existing_complex &&
        std::isfinite(selected.selected_processing_ratio) && selected.selected_processing_ratio>=0 &&
        std::isfinite(selected.cpu_budget) && selected.cpu_budget>0 &&
        selected.selected_processing_ratio>selected.cpu_budget;
}
}
