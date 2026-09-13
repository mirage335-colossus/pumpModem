#pragma once
#include "controller.hpp"
#include "bitmap_sources.hpp"
#include "control_layout.hpp"
#include "ui_document.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace datapump::gui {
struct Launch {
    bool color=true,simulation=false,smoke=false,raw_view=false;
    double hold=0,timeout=100,scroll=0;
    ui::Page page=ui::Page::console;
    std::filesystem::path smoke_directory;
};
struct BitmapPresentation {
    plots::PlotSnapshot source;
    std::uint64_t revision=0;
    std::string title,caption;
    ui::TextTone caption_tone=ui::TextTone::muted;
};
class Smoke;
// Shared lifecycle, presentation and workflow. Adapters pump native events,
// translate declared bindings, and render a tick; no modem policy lives there.
class Application {
public:
    explicit Application(Launch options);
    ~Application();
    Controller controller;
    Launch launch;
    BitmapSources bitmaps;
    void start();
    bool tick(); // Polls at25Hz; true when native state should be presented.
    bool finished() const;
    int result() const;
    void close();
    void select_page(ui::Page page);
    ui::Page page() const;
    bool smoke_passed() const;
    bool submit(const ui::Control& control,bool ctrl,bool shift);
    void activate_record(const ui::Control& control,const std::string& id);
    BitmapPresentation bitmap(const ui::Control& control,unsigned pixel_width=640) const;
    std::shared_ptr<const ui::DocumentNode> document(ui::Page page,int width);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Identical flags, defaults and display-free checks for every selected backend.
int gui_main(int argc,char** argv,const char* backend,const std::function<int(Launch)>& run);
void gui_self_check();
}
