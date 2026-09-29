#pragma once
#include "application.hpp"
#include "ui_surface.hpp"

namespace datapump::gui::framebuffer {
// Pixel desktop widgets. This owns framebuffer-specific focus, hit testing,
// selections, menus and scrolling, independently of the terminal interface.
class Session {
public:
    explicit Session(Application&,bool mfd=true,unsigned mfd_buttons=5);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void resize(surface::Viewport);
    bool tick();
    void input(const surface::Event&);
    void press_mfd_button(unsigned number);
    const surface::Scene& scene() const;
    std::vector<ui::ServiceRequest> take_host_services();
    void complete_host_service(ui::ServiceResult);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
