#pragma once
#include "datapump/live.hpp"
#include <sstream>
#include <string>

namespace datapump::gui {
// Presence and measured offset alone cannot claim work saved or phase applied.
// The runtime owns the joined result and its decline reason in helper_status.
inline std::string helper_diagnostics(const live::Snapshot& snapshot) {
    const bool work=snapshot.helper_frequency_assistance_active||snapshot.helper_phase_assistance_active||
        snapshot.helper_original_search_pairs||snapshot.helper_selected_search_pairs||
        snapshot.helper_conditional_jobs||snapshot.helper_audit_jobs;
    if(snapshot.helper_status.empty()&&!snapshot.helper_snr_db&&!work)return {};
    std::ostringstream out;
    if(!snapshot.helper_status.empty())out<<snapshot.helper_status;
    else if(snapshot.helper_snr_db&&!snapshot.helper_frequency_assistance_active)
        out<<"Helper present; acquisition not narrowed: no committed frequency assistance";
    else out<<"Helper assistance";
    out<<" | frequency assistance "<<(snapshot.helper_frequency_assistance_active?"applied":"not applied");
    if(snapshot.helper_frequency_assistance_active)out<<" (reference aligned)";
    out<<" | phase assistance "<<(snapshot.helper_phase_assistance_active?"applied":"not applied");
    if(snapshot.helper_original_search_pairs||snapshot.helper_selected_search_pairs)
        out<<" | pair maxima across rows: selected "<<snapshot.helper_selected_search_pairs
           <<", original "<<snapshot.helper_original_search_pairs;
    if(work)out<<" | jobs: conditional "<<snapshot.helper_conditional_jobs<<", audit "<<snapshot.helper_audit_jobs;
    return out.str();
}
}
