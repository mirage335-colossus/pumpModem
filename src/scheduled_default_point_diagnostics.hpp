#pragma once
#include "scheduled_default_point_guard_summary.hpp"
#include "datapump/pattern_receiver.hpp"
#include <cstdlib>
#include <optional>
#include <string_view>

namespace datapump::modem::detail::scheduled_default_point_diagnostics {
namespace guard=scheduled_default_point_guard_summary;
namespace events=scheduled_default_linear_events;
enum class Policy { chosen,original };
enum class Outcome { chosen_fft_excluded,chosen_native_guards,original_requested,original_fallback };
inline const char* statistic(Outcome value) {
    switch(value){case Outcome::chosen_fft_excluded:return "chosen FFT-screen excluded";
    case Outcome::chosen_native_guards:return "chosen native full guards";
    case Outcome::original_requested:return "Original deferred";
    case Outcome::original_fallback:return "Original deferred fallback";}
    return "invalid";
}
inline const char* reason(Outcome value) {
    switch(value){case Outcome::chosen_fft_excluded:return "below-retention-screen";
    case Outcome::chosen_native_guards:return "complete-native-summary";
    case Outcome::original_requested:return "explicit-reference";
    case Outcome::original_fallback:return "native-full-guard-unavailable";}
    return "invalid";
}
// Consult only INSIDE the existing explicit experimental Default point gate.
// Ordinary/FP64/raw paths keep their existing Original diagnostics unchanged.
inline Policy policy(const char* text) {
    if(!text||std::string_view(text)=="chosen")return Policy::chosen;
    if(std::string_view(text)=="original")return Policy::original;
    throw datapump::Error("Default point diagnostics requires chosen or original");
}
inline Policy policy(){return policy(std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS"));}
struct Identity {
    std::uint64_t end=0,index=0,event=0,phase=0;
    double frequency=0,threshold=0;
};
inline std::optional<PatternEvidence> evidence(const guard::Summary& value,const Identity& id) {
    const auto score=guard::scores(value,id.threshold);if(!score.valid)return {};
    PatternEvidence result{value.observed_start,id.end,id.index,id.frequency,
        std::max(score.score[0],score.score[1]),std::min(score.score[0],score.score[1]),score.bit,id.phase};
    result.completed_event=id.event;result.admission_threshold=id.threshold;
    result.outer_presence_score=score.outer_presence_score;result.outer_presence_active=score.outer_presence_active;
    return result;
}
} // namespace datapump::modem::detail::scheduled_default_point_diagnostics
