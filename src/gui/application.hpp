#pragma once
#include "bitmap.hpp"
#include "control_layout.hpp"
#include "ui_document.hpp"
#include "overlay.hpp"
#include "launch_command.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace datapump::gui {
struct Launch {
    bool color=true,simulation=false,smoke=false;
    double hold=0,timeout=100,scroll=0;
    ui::Page page=ui::Page::console;
    std::filesystem::path smoke_directory;
    std::optional<launch_command::Patch> settings;
};
struct BitmapPresentation {
    BitmapSource source;
    std::uint64_t revision=0;
    std::string title,caption;
    ui::TextTone caption_tone=ui::TextTone::muted;
};
struct ControlPresentation {
    const ui::FieldState& state;
    std::string label;
    bool enabled=true,visible=true;
};
struct MenuPresentation {
    std::vector<ui::Option> options;
    bool enabled=false,visible=false;
};
// Shared lifecycle, presentation and workflow. Adapters pump native events,
// translate declared bindings, and render a tick. This facade exposes only the
// toolkit-neutral vocabulary; models, workers and bitmap producers are private.
class Application {
public:
    explicit Application(Launch options);
    ~Application();
    Launch launch;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    void start();
    bool tick(); // Polls at 25 Hz; true when native state should be presented.
    bool finished() const;
    int result() const;
    void close();
    bool closing() const;
    void edit(ui::Field field,std::string text);
    void edit(const ui::Control& control,std::string text);
    void preset(const ui::Control& control,const std::string& id);
    void select(ui::Field field,std::string option_id);
    void select(const ui::Control& control,std::string option_id);
    void toggle(ui::Field field,bool value);
    void toggle(const ui::Control& control,bool value);
    void activate(ui::Command command);
    void activate(const ui::Control& control);
    void gesture(const ui::Control& control,ui::Command command);
    std::shared_ptr<const ui::OverlayDefinition> overlay() const;
    void show_overlay(ui::OverlayDefinition definition);
    void dismiss_overlay();
    bool overlay_key(ui::KeyStroke key,bool service_active=false,bool popup_active=false);
    ui::OverlayLayers overlay_layers(bool service_active=false) const;
    void set_service_active(bool active);
    // Native callbacks use scoped dispatch. Programmatic activate/select_page
    // remain available to shared workflows independently of displayed layers.
    void dispatch(ui::Command command,std::uint64_t surface=0);
    void navigate(ui::Page page);
    ui::ControlLayout control_layout(const ui::Control& control,int width,int height,
        std::span<const ui::Control> declarations=ui::console_screen()) const;
    ui::Rect page_bounds(int width,int height) const;
    ui::Rect tabs_bounds(int width,int height) const;
    std::vector<ui::TabLayout> tab_layout(int width,int height) const;
    ControlPresentation control(const ui::Control& control) const;
    MenuPresentation menu(std::span<const ui::Control* const> items) const;
    void select_menu(std::span<const ui::Control* const> items,const std::string& id);
    const ui::FieldState& field(ui::Field field) const;
    bool enabled(ui::Command command) const;
    std::string command_label(ui::Command command) const;
    void complete_service(ui::ServiceResult result);
    std::vector<ui::ServiceRequest> take_services();
    void report_error(std::string message);
    std::uint64_t revision() const;
    std::uint64_t poll_count() const; // Native event-loop conformance diagnostic.
    void select_page(ui::Page page);
    ui::Page page() const;
    bool smoke_passed() const;
    bool submit(const ui::Control& control,bool ctrl,bool shift);
    void activate_record(const ui::Control& control,const std::string& id);
    BitmapPresentation bitmap(const ui::Control& control,unsigned pixel_width=640) const;
    std::shared_ptr<const ui::DocumentNode> document(ui::Page page,int width);
private:
    bool page_visible(ui::Page page) const;
    bool accepts_surface(std::uint64_t surface) const;
    bool accepts_input(const ui::Control& control) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Identical flags, defaults and display-free checks for every selected backend.
int gui_main(int argc,char** argv,const char* backend,const std::function<int(Launch)>& run);
void gui_self_check();
}
