#include "gui_smoke_replay.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using datapump::gui::smoke_detail::ReplayCheck;
using datapump::gui::smoke_detail::check_replay;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
ReplayCheck healthy() {
    ReplayCheck replay;
    replay.elapsed=3.1;replay.fraction=1;replay.frames=20;replay.waveform_changes=19;
    replay.saw_symbols=true;replay.pending_required=true;replay.pending_poll=40;replay.phase=17;
    return replay;
}
void expect_failure(const ReplayCheck& replay,std::string_view backend) {
    std::ostringstream warnings;
    bool failed=false;
    try {check_replay(replay,backend,warnings);}
    catch(const datapump::Error&) {failed=true;}
    require(failed,"Incorrect replay was accepted");
    require(warnings.str().empty(),"A hard correctness failure was downgraded to a warning");
}
void recorded_display_cases() {
    auto short_fraction=healthy();short_fraction.fraction=.898309;
    short_fraction.frames=13;short_fraction.waveform_changes=12;
    auto few_frames=healthy();few_frames.frames=6;few_frames.waveform_changes=5;
    for(const auto& replay:{short_fraction,few_frames}) {
        std::ostringstream warnings;
        check_replay(replay,"rev",warnings);
        const auto text=warnings.str();
        require(text.starts_with("WARNING REV_REPLAY_CADENCE: "),"Rev cadence warning lost its stable prefix");
        for(const auto& value:{"elapsed="+std::to_string(replay.elapsed),
                              "frames="+std::to_string(replay.frames),
                              "changes="+std::to_string(replay.waveform_changes),
                              "fraction="+std::to_string(replay.fraction),"phase="+std::to_string(replay.phase)})
            require(text.find(value)!=std::string::npos,"Rev warning lost its measured diagnostics");
        expect_failure(replay,"fltk");
    }
}
void correctness_remains_mandatory() {
    for(const auto backend:{"fltk","rev"}) {
        auto replay=healthy();replay.saw_symbols=false;
        expect_failure(replay,backend);
        replay=healthy();replay.pending_poll=0;
        expect_failure(replay,backend);
        // A simultaneous display miss must not hide missing pending/symbols.
        replay.frames=6;replay.fraction=.898309;expect_failure(replay,backend);
        replay=healthy();replay.frames=6;replay.saw_symbols=false;
        expect_failure(replay,backend);
    }
}
void cadence_thresholds_and_healthy_replay() {
    for(const auto backend:{"fltk","rev"}) {
        std::ostringstream warnings;
        check_replay(healthy(),backend,warnings);
        auto boundary=healthy();boundary.elapsed=2.4;boundary.frames=10;
        boundary.waveform_changes=5;boundary.fraction=.9;
        check_replay(boundary,backend,warnings);boundary.elapsed=8;
        check_replay(boundary,backend,warnings);
        // Preserve the existing raw/pattern exceptions and binary thresholds.
        auto binary=healthy();binary.binary=true;binary.frames=2;binary.waveform_changes=1;
        binary.saw_symbols=false;binary.pending_poll=0;
        check_replay(binary,backend,warnings);
        auto pattern=healthy();pattern.pattern_symbols=true;pattern.saw_symbols=false;pattern.pending_poll=0;
        check_replay(pattern,backend,warnings);
        require(warnings.str().empty(),"Healthy replay emitted a warning");
    }
    for(const auto elapsed:{2.39,8.01}) {
        auto replay=healthy();replay.elapsed=elapsed;
        std::ostringstream warnings;check_replay(replay,"rev",warnings);
        require(!warnings.str().empty(),"Rev elapsed display miss emitted no warning");
        expect_failure(replay,"fltk");
    }
    auto replay=healthy();replay.waveform_changes=4;
    std::ostringstream warnings;check_replay(replay,"rev",warnings);
    require(!warnings.str().empty(),"Rev waveform display miss emitted no warning");
    expect_failure(replay,"fltk");
}
}
int main() {try {
    recorded_display_cases();correctness_remains_mandatory();cadence_thresholds_and_healthy_replay();
    std::cout<<"GUI replay cadence warning policy passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
