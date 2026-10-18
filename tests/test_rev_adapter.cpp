#define DATAPUMP_REV_ADAPTER_TEST
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <glew/glew.h>
#include "gui_extension_fixture.hpp"
#include "estimate_warning_fixture.hpp"
#include "document_geometry_fixture.hpp"
#include "overlay_fixture.hpp"
import Rev.Graphics.FrameBuffer;

// Test-only timing: production backend_rev.cpp never includes these helpers.
namespace rev_adapter_timing {
using Clock=std::chrono::steady_clock;
using Micros=std::chrono::microseconds;
inline const auto origin=Clock::now();
inline std::uint64_t frames=0,failed=0,total_us=0,max_us=0,records=0,slow_records=0;
inline auto elapsed_us(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<Micros>(Clock::now()-start).count());
}
inline void stats() {
    std::cerr<<" at_us="<<elapsed_us(origin)<<" frames="<<frames<<" frame_us="<<total_us
             <<" max_frame_us="<<max_us<<" failed_frames="<<failed;
}
struct FixtureTiming {
    std::string_view name;Clock::time_point started=Clock::now(),destroy_started{};
    bool destroying=false;
    explicit FixtureTiming(std::string_view label):name(label) {
        std::cerr<<"Rev adapter fixture="<<name<<" construct_begin";stats();std::cerr<<std::endl;
    }
    void constructed() const {
        std::cerr<<"Rev adapter fixture="<<name<<" construct_end elapsed_us="<<elapsed_us(started);
        stats();std::cerr<<std::endl;
    }
    ~FixtureTiming() {
        std::cerr<<"Rev adapter fixture="<<name
                 <<(destroying?" destroy_end elapsed_us=":" construct_unwound elapsed_us=")
                 <<elapsed_us(destroying?destroy_started:started)<<" total_us="<<elapsed_us(started)
                 <<" unwinding="<<std::uncaught_exceptions();stats();std::cerr<<std::endl;
    }
};
struct BeforeDestroy {
    FixtureTiming& fixture;
    ~BeforeDestroy() {
        fixture.destroy_started=Clock::now();fixture.destroying=true;
        std::cerr<<"Rev adapter fixture="<<fixture.name<<" destroy_begin";stats();std::cerr<<std::endl;
    }
};
struct StepTiming {
    std::string_view name;Clock::time_point started=Clock::now();
    std::uint64_t first_frame=frames,first_us=total_us;
    explicit StepTiming(std::string_view label):name(label) {
        std::cerr<<"Rev adapter step="<<name<<" begin";stats();std::cerr<<std::endl;
    }
    ~StepTiming() {
        std::cerr<<"Rev adapter step="<<name<<" end elapsed_us="<<elapsed_us(started)
                 <<" delta_frames="<<frames-first_frame<<" delta_frame_us="<<total_us-first_us
                 <<" unwinding="<<std::uncaught_exceptions();stats();std::cerr<<std::endl;
    }
};
template<class Function> void step(std::string_view name,Function&& function) {
    StepTiming timing{name};function();
}
struct FrameTiming {
    Clock::time_point started=Clock::now();bool painted=false;
    ~FrameTiming() {
        const auto us=elapsed_us(started);++frames;total_us+=us;max_us=std::max(max_us,us);
        if(!painted)++failed;
        const bool slow=us>=250000&&slow_records<8;
        if(records<64&&(frames<=4||frames%64==0||slow)) {
            ++records;if(slow)++slow_records;
            std::cerr<<"Rev adapter frame="<<frames<<" elapsed_us="<<us
                     <<" painted="<<painted<<" unwinding="<<std::uncaught_exceptions();
            stats();std::cerr<<std::endl;
        }
    }
};
}

#include "../src/gui/backend_rev.cpp"

namespace {
int run_native_probes(std::string_view selected={}) {
    // Native conformance is independent of the production workflow, matching
    // FLTK's test runner. CTest runs all shared smoke assertions separately on
    // the real datapump-gui executable through gui_workflow.
    Launch launch;launch.color=false;launch.simulation=true;launch.page=ui::pages().front().id;
    configure_theme(launch.color);std::vector<void*> windows;
    if(selected=="--choice-presentation") {
        for(bool color:{false,true}) {
            launch.color=color;configure_theme(color);
            rev_adapter_timing::FixtureTiming fixture_timing{"fixture-1"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_choice_contract",[&]{probe.verify_choice_contract();});rev_adapter_timing::step("verify_developer_mode_visibility",[&]{probe.verify_developer_mode_visibility();});
        }
        return 0;
    }
    if(selected=="--inline-document") {
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-2"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_inline_document_editor",[&]{probe.verify_inline_document_editor();});return 0;
    }
    if(selected=="--expanded-bitmap") {
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-3"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};
        probe.application.toggle(ui::Field::developer_mode,true);probe.apply();
        rev_adapter_timing::step("verify_expanded_bitmap",[&]{probe.verify_expanded_bitmap();});return 0;
    }
    if(selected=="--layout-lifecycle") {
        auto controls=test::layout_lifecycle_controls();
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-4"};RevApp probe(windows,launch,controls);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_layout_lifecycle",[&]{probe.verify_layout_lifecycle(controls);});return 0;
    }
    if(selected=="--estimate-colors") {
        for(bool color:{false,true}) {
            launch.color=color;configure_theme(color);
            rev_adapter_timing::FixtureTiming fixture_timing{"fixture-5"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_estimate_warning_colors",[&]{probe.verify_estimate_warning_colors();});
        }
        return 0;
    }
    if(!selected.empty())throw std::runtime_error("Unknown Rev adapter probe");
    {
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-6"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_fast_mode_visibility",[&]{probe.verify_fast_mode_visibility();});rev_adapter_timing::step("verify_developer_mode_visibility",[&]{probe.verify_developer_mode_visibility();});
    }
    {
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-7"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_document_editor_scrolling",[&]{probe.verify_document_editor_scrolling();});rev_adapter_timing::step("verify_launch_command_editor",[&]{probe.verify_launch_command_editor();});
    }
    {
        // The command-editor probe expands details; keep this fixture's initial state independent.
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-8"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_inline_document_editor",[&]{probe.verify_inline_document_editor();});
    }
    {
        auto declarations=test::extension_controls();
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-9"};RevApp probe(windows,launch,declarations);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_extension_contract",[&]{probe.verify_extension_contract();});
        test::relabel_extension_controls(declarations);probe.apply();rev_adapter_timing::step("verify_updated_labels",[&]{probe.verify_updated_labels();});rev_adapter_timing::step("verify_service_shutdown",[&]{probe.verify_service_shutdown();});
    }
    {
        auto lifecycle=test::layout_lifecycle_controls();
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-10"};RevApp probe(windows,launch,lifecycle);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_layout_lifecycle",[&]{probe.verify_layout_lifecycle(lifecycle);});
    }
    {
        auto lifecycle=test::policy_lifecycle_controls();
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-11"};RevApp probe(windows,launch,lifecycle);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};rev_adapter_timing::step("verify_policy_lifecycle",[&]{probe.verify_policy_lifecycle(lifecycle);});
    }
    {
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-12"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};
        probe.application.toggle(ui::Field::developer_mode,true);probe.apply();
        rev_adapter_timing::step("verify_palette_roles",[&]{probe.verify_palette_roles();});rev_adapter_timing::step("verify_editor_contract",[&]{probe.verify_editor_contract();});rev_adapter_timing::step("verify_clipboard_shortcuts",[&]{probe.verify_clipboard_shortcuts();});rev_adapter_timing::step("verify_choice_contract",[&]{probe.verify_choice_contract();});rev_adapter_timing::step("verify_record_contract",[&]{probe.verify_record_contract();});rev_adapter_timing::step("verify_prompt_focus",[&]{probe.verify_prompt_focus();});rev_adapter_timing::step("verify_native_resize",[&]{probe.verify_native_resize();});rev_adapter_timing::step("verify_overlay_composition",[&]{probe.verify_overlay_composition();});rev_adapter_timing::step("verify_estimate_warning_colors",[&]{probe.verify_estimate_warning_colors();});
    }
    {
        launch.color=true;configure_theme(launch.color);
        rev_adapter_timing::FixtureTiming fixture_timing{"fixture-13"};RevApp probe(windows,launch);fixture_timing.constructed();rev_adapter_timing::BeforeDestroy destruction_timing{fixture_timing};probe.application.toggle(ui::Field::developer_mode,true);probe.apply();rev_adapter_timing::step("verify_palette_roles",[&]{probe.verify_palette_roles();});rev_adapter_timing::step("verify_estimate_warning_colors",[&]{probe.verify_estimate_warning_colors();});
    }
    if(!windows.empty())throw std::runtime_error("Rev native probes retained a window after conformance checks");
    std::cout<<"Rev native adapter probes passed: declarative extensions, UTF-8 editing, choices, records, scrolling and modal focus.\n";
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc>2)throw std::runtime_error("Expected one optional Rev adapter probe");
        return run_native_probes(argc==2?argv[1]:"");
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
