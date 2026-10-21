#include "raw32_context.hpp"
#include "copied_help_pcm_probe.hpp"
#include "copied_help_candidate_trace.hpp"
#include "audio_trace.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace datapump::diagnostic::copied_help_session {
namespace {
std::unique_ptr<audio::trace::Scope> trace;
bool selected_event(const char* name) noexcept {
    for(const auto* wanted:{"trace_begin","receiver_pattern_drain","receiver_signal_commit","receiver_selection",
            "receiver_bank_build","receiver_bank_admitted","receiver_bank_retired","receiver_allocation_refused",
            "receiver_profile_unavailable"})if(std::strcmp(name,wanted)==0)return true;
    return false;
}
}
void arm_probes() {
    const auto* path=std::getenv("DATAPUMP_COPIED_HELP_SESSION_TRACE");
    if(!path||!*path||trace)throw std::runtime_error("one new Session trace path is required");
    trace=std::make_unique<audio::trace::Scope>(std::filesystem::path(path),true,selected_event);
    copied_help_pcm::arm();
    copied_help_candidate::arm();
    if(!copied_help_pcm::active.load(std::memory_order_acquire))throw std::runtime_error("PCM recorder did not arm");
    if(!copied_help_candidate::active())throw std::runtime_error("candidate recorder did not arm");
}
}
