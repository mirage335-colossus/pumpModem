#pragma once
#include "../ui_contract.hpp"
#include "../bitmap.hpp"
#include "datapump/audio.hpp"
#include <functional>
#include <memory>
namespace datapump::gui::legacy_ui {
class Controller {
public:
    explicit Controller(std::function<bool()> acquire_audio);
    ~Controller();
    void selected(bool);
    void set_shellcode_mode(bool enabled);
    void set_devices(const std::vector<audio::Device>& devices);
    void poll();
    void close();
    bool ready_to_close() const;
    bool active() const;
    void edit(ui::Field,std::string);
    void select(ui::Field,std::string);
    void toggle(ui::Field,bool);
    void activate(ui::Command);
    void transmit(); // Start only; repeated send shortcuts never cancel TX.
    bool enabled(ui::Command) const;
    std::string command_label() const;
    const ui::FieldState& field(ui::Field) const;
    void report_error(std::string);
    std::uint64_t revision() const;
    BitmapSource bitmap() const;
    std::uint64_t bitmap_revision() const;
    std::string bitmap_caption() const;
    std::string bitmap_title() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
