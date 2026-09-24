#pragma once
#include "../ui_contract.hpp"
#include "../bitmap.hpp"
#include <functional>
#include <memory>
namespace datapump::gui::fast_ui {
// Owns fast settings, key loading, file selections and the independent session.
// The mode host supplies exclusive audio acquisition; no toolkit enters here.
class Controller {
public:
    explicit Controller(std::function<bool()> acquire_audio);
    ~Controller();
    void poll();
    // Selected Fast mode keeps receiving until explicitly paused.
    void set_selected(bool selected);
    void close();
    bool ready_to_close() const;
    bool active() const;
    void edit(ui::Field,std::string);
    void select(ui::Field,std::string);
    void toggle(ui::Field,bool);
    void activate(ui::Command);
    bool enabled(ui::Command) const;
    std::string command_label(ui::Command) const;
    const ui::FieldState& field(ui::Field) const;
    void complete_service(ui::ServiceResult);
    std::vector<ui::ServiceRequest> take_services();
    void report_error(std::string);
    std::uint64_t revision() const;
    BitmapSource bitmap(ui::Bitmap) const;
    std::uint64_t bitmap_revision(ui::Bitmap) const;
    std::string bitmap_caption(ui::Bitmap,unsigned width=640) const;
    std::string bitmap_title(ui::Bitmap) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
