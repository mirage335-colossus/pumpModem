#pragma once
#include "datapump/live.hpp"
#include <sstream>
#include <string>

namespace datapump::gui {
// Presence and measured offset alone cannot claim work saved or phase applied.
// The runtime owns the joined result and its decline reason in helper_status.
inline std::string helper_diagnostics(const live::Snapshot& snapshot) {
    // The manual reference path owns its lock state. Old acquisition-bank
    // counters may still be present in a transitional snapshot; they cannot
    // turn waiting/loss into a lock or imply broad fallback reception.
    if(snapshot.helper_status.starts_with("Helper reference ")) {
        if(snapshot.helper_status.starts_with("Helper reference waiting"))
            return snapshot.helper_status+" | Robust acquisition waits for helper lock";
        if(snapshot.helper_status.starts_with("Helper reference locked:") &&
           snapshot.helper_status.find("folded mono")!=std::string::npos)
            return snapshot.helper_status;
        if(snapshot.helper_status.starts_with("Helper reference locked:")) {
            auto text=snapshot.helper_status+" | frequency-corrected input to ordinary Robust reception";
            // Old cohort snapshots also used the timing flag. The joined
            // reference route must publish its current dated clock result.
            if(snapshot.helper_timing_assistance_active &&
               snapshot.helper_status.find("conditional source clock mean [")!=std::string::npos) {
                text+=" | measured dwell-spacing clock interval applied conditionally";
                text+=" | joint pair maxima: selected "+std::to_string(snapshot.helper_selected_search_pairs)+
                    ", original "+std::to_string(snapshot.helper_original_search_pairs);
            }
            return text;
        }
        return snapshot.helper_status;
    }
    const bool work=snapshot.helper_frequency_assistance_active||snapshot.helper_phase_assistance_active||
        snapshot.helper_timing_assistance_active||
        snapshot.helper_original_search_pairs||snapshot.helper_selected_search_pairs||
        snapshot.helper_conditional_jobs||snapshot.helper_audit_jobs||
        snapshot.helper_input_view_conditional_jobs||snapshot.helper_input_view_count;
    const bool compact_work=snapshot.helper_compact_projected_samples||snapshot.helper_compact_lane_samples||
        snapshot.helper_compact_skipped_projected_samples||snapshot.helper_compact_skipped_lane_samples||
        snapshot.helper_compact_audit_projected_samples||snapshot.helper_compact_audit_lane_samples||
        snapshot.helper_compact_phase_projected_samples||snapshot.helper_compact_phase_moment_updates;
    if(snapshot.helper_status.empty()&&!snapshot.helper_snr_db&&!work&&!compact_work)return {};
    std::ostringstream out;
    if(!snapshot.helper_status.empty())out<<snapshot.helper_status;
    else if(snapshot.helper_snr_db&&!snapshot.helper_frequency_assistance_active)
        out<<"Helper present; acquisition not narrowed: no committed frequency assistance";
    else out<<"Helper assistance";
    out<<" | frequency assistance "<<(snapshot.helper_frequency_assistance_active?"applied":"not applied");
    if(snapshot.helper_frequency_assistance_active)out<<" (reference aligned)";
    out<<" | phase assistance "<<(snapshot.helper_phase_assistance_active?"applied":"not applied");
    out<<" | punctuation timing assistance "<<(snapshot.helper_timing_assistance_active?"applied":"not applied");
    if(snapshot.helper_original_search_pairs||snapshot.helper_selected_search_pairs)
        out<<" | pair maxima across rows: selected "<<snapshot.helper_selected_search_pairs
           <<", original "<<snapshot.helper_original_search_pairs;
    if(snapshot.helper_conditional_jobs||snapshot.helper_audit_jobs)
        out<<" | native FFT jobs: conditional "<<snapshot.helper_conditional_jobs<<", broad/probe "<<snapshot.helper_audit_jobs;
    if(snapshot.helper_input_view_count||snapshot.helper_input_view_conditional_jobs)
        out<<" | signed input views "<<snapshot.helper_input_view_count
           <<", conditional native FFT jobs "<<snapshot.helper_input_view_conditional_jobs;
    if(compact_work) {
        out<<" | compact carrier-samples: projected "<<snapshot.helper_compact_projected_samples
           <<", skipped "<<snapshot.helper_compact_skipped_projected_samples
           <<", broad/probe "<<snapshot.helper_compact_audit_projected_samples;
        out<<" | compact arrival-lane-samples: accumulated "<<snapshot.helper_compact_lane_samples
           <<", skipped "<<snapshot.helper_compact_skipped_lane_samples
           <<", broad/probe "<<snapshot.helper_compact_audit_lane_samples;
        out<<" | compact phase work: projected samples "<<snapshot.helper_compact_phase_projected_samples
           <<", moment updates "<<snapshot.helper_compact_phase_moment_updates;
    }
    if(snapshot.helper_audit_jobs||snapshot.helper_compact_audit_projected_samples||snapshot.helper_compact_audit_lane_samples)
        out<<" | broad/probe work does not establish complete cohort coverage";
    return out.str();
}
}
