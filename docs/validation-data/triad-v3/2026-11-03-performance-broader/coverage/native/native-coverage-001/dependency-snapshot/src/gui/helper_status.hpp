#pragma once
#include "datapump/live.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace datapump::gui {
// Presence and measured offset alone cannot claim work saved or phase applied.
// The runtime owns the joined result and its decline reason in helper_status.
inline std::string helper_diagnostics(const live::Snapshot& snapshot) {
    const bool measured=snapshot.helper_snr_db && std::isfinite(*snapshot.helper_snr_db);
    const auto level=[&](bool last_reported=false) {
        if(!measured)return std::string{};
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<" | "<<(last_reported?"last reported":"measured")<<" SNR (10 Hz) "
           <<std::fixed<<std::setprecision(1)<<*snapshot.helper_snr_db<<" dB (C/N0 "
           <<*snapshot.helper_snr_db+10<<" dB-Hz)";
        return out.str();
    };
    // Manual reference readiness is separate from the decoder-owned active
    // flag. Waiting/loss cannot inherit active work from an older bank.
    if(snapshot.helper_status.starts_with("Helper reference ")) {
        const bool waiting=snapshot.helper_status.starts_with("Helper reference waiting");
        const bool lost=snapshot.helper_status.starts_with("Helper reference lost");
        const bool locked=snapshot.helper_status.starts_with("Helper reference locked:");
        const bool prior=snapshot.helper_status.find("original oscillator prior retained")!=std::string::npos;
        const bool folded=snapshot.helper_status.find("folded mono")!=std::string::npos;
        const bool active=!waiting && !lost && (locked || prior) && snapshot.helper_frequency_assistance_active;
        // Raw observation can report lock before the joined decoder has used
        // it. Avoid repeating its provisional corrected-reception claim.
        auto text=locked && !active && !folded?std::string{"Helper reference ready"}:snapshot.helper_status;
        text+=level(lost);
        text+=active?" | frequency assistance active":" | frequency assistance not active; acquisition narrowing not active";
        if(active && locked && !folded && !prior)
            text+=" (frequency-corrected input to ordinary Robust reception)";
        text+=" | phase assistance not applied: no independently qualified phase reference";
        if(waiting)text+=" | Robust acquisition waits for helper lock";
        // Old cohort snapshots also used the timing flag. Only the joined
        // route's current dated clock result can report conditional clock use.
        const char* clock_source=nullptr;
        if(snapshot.helper_status.find("; conditional source clock from shared-radio frequency intersected with punctuation cadence [")!=std::string::npos)
            clock_source="shared-radio frequency-derived clock interval intersected with measured dwell spacing applied conditionally";
        else if(snapshot.helper_status.find("; conditional source clock from shared-radio frequency [")!=std::string::npos)
            clock_source="shared-radio frequency-derived clock interval applied conditionally";
        else if(snapshot.helper_status.find("; conditional source clock from punctuation cadence [")!=std::string::npos)
            clock_source="measured dwell-spacing clock interval applied conditionally";
        if(active && snapshot.helper_timing_assistance_active && clock_source) {
            text+=" | ";text+=clock_source;
            text+=" | joint pair totals across owners: selected "+std::to_string(snapshot.helper_selected_search_pairs)+
                ", original "+std::to_string(snapshot.helper_original_search_pairs);
        }
        return text;
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
    if(snapshot.helper_status.empty()&&!measured&&!work&&!compact_work)return {};
    std::ostringstream out;
    if(!snapshot.helper_status.empty())out<<snapshot.helper_status;
    else if(measured&&!snapshot.helper_frequency_assistance_active)
        out<<"Helper present; acquisition not narrowed: no committed frequency assistance";
    else out<<"Helper assistance";
    out<<level();
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
