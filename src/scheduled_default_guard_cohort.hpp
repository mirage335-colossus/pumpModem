#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string_view>
namespace datapump::modem::detail::scheduled_default_guard_cohort {
inline constexpr std::size_t maximum_lanes=8;
enum class Policy {automatic,single,eight};
inline Policy parse(const char* value){if(!value||std::string_view(value)=="auto")return Policy::automatic;
    if(std::string_view(value)=="1")return Policy::single;if(std::string_view(value)=="8")return Policy::eight;
    throw Error("Default guard cohort requires auto,1or8");}
struct Choice {std::size_t first=0,outputs=0,width=0;};
inline Choice choose(std::size_t origin,std::size_t total,bool whole_prefilter_ready,Policy policy){
    if(!total||origin>=total)throw Error("Default guard cohort origin outside range");
    // A whole-first survivor is one charged current hypothesis, not evidence
    // about future lanes at their different trial thresholds. Avoid computing
    // seven speculative full chip/outer guards for that sparse survivor.
    const auto width=policy==Policy::single||(policy==Policy::automatic&&whole_prefilter_ready)?1U:8U;
    const auto first=origin/width*width;return {first,std::min<std::size_t>(width,total-first),width};
}
inline Choice choose(std::size_t origin,std::size_t total,bool whole_prefilter_ready){
    return choose(origin,total,whole_prefilter_ready,parse(std::getenv("DATAPUMP_SCHED_GUARD_COHORT")));
}
} // namespace datapump::modem::detail::scheduled_default_guard_cohort
