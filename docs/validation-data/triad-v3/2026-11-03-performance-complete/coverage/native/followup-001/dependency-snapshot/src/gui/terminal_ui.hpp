#pragma once
#include "application.hpp"
#include "ui_surface.hpp"

namespace datapump::gui::terminal {
using surface::Metrics;using surface::Viewport;using surface::Key;using surface::Event;
using surface::Tone;using surface::Primitive;using surface::Scene;
// Cell-based terminal layout and interaction adapter. Only declarations and
// commands cross the Application facade; no application behavior lives here.
class Session {
public:
    explicit Session(Application&);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void resize(Viewport);
    // Host-owned, nonblocking scheduling: call frequently even when output is
    // throttled. Application owns the receiver's 25 Hz progress cadence.
    bool tick();
    void input(const Event&);
    const Scene& scene() const;
    // Path selectors and text prompts use shared dialogs. Clipboard/folder
    // requests are host services; check the validity token at dispatch time.
    std::vector<ui::ServiceRequest> take_host_services();
    void complete_host_service(ui::ServiceResult);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
