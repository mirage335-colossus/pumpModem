#pragma once
#include "application.hpp"
#include "ui_surface.hpp"

namespace datapump::gui::framebuffer {
// Pixel desktop widgets. This owns framebuffer-specific focus, hit testing,
// selections, menus and scrolling, independently of the terminal interface.
class Session {
public:
    explicit Session(Application&);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void resize(surface::Viewport);
    bool tick();
    void input(const surface::Event&);
    const surface::Scene& scene() const;
    std::vector<ui::ServiceRequest> take_host_services();
    void complete_host_service(ui::ServiceResult);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
