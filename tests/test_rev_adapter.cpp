#define DATAPUMP_REV_ADAPTER_TEST
#include <glew/glew.h>
#include "gui_extension_fixture.hpp"
#include "document_geometry_fixture.hpp"
#include "overlay_fixture.hpp"
import Rev.Graphics.FrameBuffer;
#include "../src/gui/backend_rev.cpp"

namespace {
int run_native_probes() {
    // Native conformance is independent of the production workflow, matching
    // FLTK's test runner. CTest runs all shared smoke assertions separately on
    // the real datapump-gui executable through gui_workflow.
    Launch launch;launch.color=false;launch.simulation=true;launch.page=ui::pages().front().id;
    configure_theme(launch.color);std::vector<void*> windows;
    {
        RevApp probe(windows,launch);probe.verify_inline_document_editor();
    }
    {
        auto declarations=test::extension_controls();
        RevApp probe(windows,launch,declarations);probe.verify_extension_contract();
        test::relabel_extension_controls(declarations);probe.apply();probe.verify_updated_labels();probe.verify_service_shutdown();
    }
    {
        auto lifecycle=test::layout_lifecycle_controls();
        RevApp probe(windows,launch,lifecycle);probe.verify_layout_lifecycle(lifecycle);
    }
    {
        auto lifecycle=test::policy_lifecycle_controls();
        RevApp probe(windows,launch,lifecycle);probe.verify_policy_lifecycle(lifecycle);
    }
    {
        RevApp probe(windows,launch);
        probe.verify_palette_roles();probe.verify_editor_contract();probe.verify_clipboard_shortcuts();probe.verify_choice_contract();probe.verify_record_contract();probe.verify_prompt_focus();probe.verify_native_resize();probe.verify_overlay_composition();
    }
    {
        launch.color=true;configure_theme(launch.color);
        RevApp probe(windows,launch);probe.verify_palette_roles();
    }
    if(!windows.empty())throw std::runtime_error("Rev native probes retained a window after conformance checks");
    std::cout<<"Rev native adapter probes passed: declarative extensions, UTF-8 editing, choices, records, scrolling and modal focus.\n";
    return 0;
}
}
int main() {
    try {return run_native_probes();}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
