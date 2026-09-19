#include "controller.hpp"
#include "screen.hpp"
#include "presentation.hpp"
#include "plots.hpp"
#include <array>
#include <optional>
#include <stdexcept>
namespace datapump::gui::legacy_ui {
using F=ui::Field;using C=ui::Command;
struct Controller::Impl {
    std::array<ui::FieldState,static_cast<std::size_t>(F::count)> fields;
    legacy::Session session;
    legacy::Settings settings;
    legacy::Snapshot snapshot;
    TextPresentation text;
    Waterfall waterfall;
    std::function<bool()> acquire_audio;
    std::optional<std::string> pending_tx;
    bool selected=false,closing=false,valid=true,failed=false,reconfigure=false;
    std::uint64_t revision=1;
    ui::FieldState& f(F field) {return fields.at(static_cast<std::size_t>(field));}
    const ui::FieldState& f(F field) const {return fields.at(static_cast<std::size_t>(field));}
    explicit Impl(std::function<bool()> acquire):acquire_audio(std::move(acquire)) {
        f(F::legacy_profile).options={{"olivia4-2000","Olivia-4/2k"},{"bpsk31","BPSK31"},{"bpsk125","BPSK125"}};
        f(F::legacy_profile).selected="bpsk31";f(F::legacy_carrier).text="1500";
        f(F::legacy_status).text="Select Legacy Modem to receive.";
    }
    void refresh() {
        const bool edit=!closing&&!snapshot.transmitting&&!pending_tx;
        f(F::legacy_profile).enabled=edit;f(F::legacy_carrier).enabled=edit;
        f(F::legacy_text).enabled=!closing;
    }
    void changed_settings() {
        failed=false;reconfigure=true;session.cancel();++revision;
    }
    void observe() {
        auto next=session.poll();
        if(text.update(next,f(F::legacy_transcript),f(F::legacy_text)))++revision;
        if(next.revision!=snapshot.revision) {
            if(!next.error.empty()) {failed=true;f(F::legacy_status).text=next.error;}
            else if(valid&&!reconfigure)f(F::legacy_status).text=next.status;
            snapshot=std::move(next);++revision;
        }
        if(waterfall.update(snapshot.recent_samples,snapshot.audio_revision,snapshot.active,snapshot.transmitting))++revision;
        refresh();
    }
};
Controller::Controller(std::function<bool()> acquire):impl_(std::make_unique<Impl>(std::move(acquire))) {}
Controller::~Controller()=default;
void Controller::selected(bool value) {
    auto& p=*impl_;if(p.closing||p.selected==value)return;
    p.selected=value;++p.revision;
    if(!value) {p.pending_tx.reset();p.session.cancel();}
    else {p.failed=false;p.f(F::legacy_status).text="Waiting for audio…";}
    p.refresh();
}
void Controller::poll() {
    auto& p=*impl_;
    // Retain the final acknowledgment once, then leave dormant Legacy history
    // alone while another modem owns the interface and receiver budget.
    const bool audio_active=p.session.active();
    if(!p.selected&&!audio_active&&!p.snapshot.active)return;
    if(audio_active||p.snapshot.active)p.observe();
    if(p.closing||!p.selected||!p.valid||p.session.active()||(p.failed&&!p.pending_tx))return;
    try {
        if(p.acquire_audio&&!p.acquire_audio()) {
            if(p.f(F::legacy_status).text!="Waiting for the other modem to release audio…") {
                p.f(F::legacy_status).text="Waiting for the other modem to release audio…";++p.revision;
            }
            return;
        }
        p.session.configure(p.settings);p.reconfigure=false;p.failed=false;
        if(p.pending_tx) {
            const auto submitted=*p.pending_tx;
            p.session.transmit_text(submitted);p.pending_tx.reset();
            p.text.submitted(submitted,p.session.poll().transmission);
        } else p.session.listen();
        p.observe();
    } catch(const std::exception& e) {
        p.pending_tx.reset();p.failed=true;report_error(e.what());p.refresh();
    }
}
void Controller::close() {auto& p=*impl_;p.closing=true;p.selected=false;p.pending_tx.reset();p.session.close();p.refresh();++p.revision;}
bool Controller::ready_to_close() const {return impl_->closing&&impl_->session.ready_to_close();}
bool Controller::active() const {return impl_->session.active()||impl_->pending_tx.has_value();}
void Controller::edit(F field,std::string value) {
    auto& p=*impl_;if(p.closing||!p.f(field).enabled||p.f(field).text==value)return;
    if(field==F::legacy_text) {
        if(const auto error=ui::edit_error(value,true,legacy::text_byte_limit);!error.empty()) {report_error(error);return;}
        p.text.edited(value);p.f(field).text=std::move(value);++p.revision;
    } else if(field==F::legacy_carrier) {
        if(const auto error=ui::edit_error(value,false,32);!error.empty()) {report_error(error);return;}
        p.f(field).text=std::move(value);p.valid=false;
        try {
            std::size_t count=0;const auto carrier=std::stod(p.f(field).text,&count);
            if(count!=p.f(field).text.size())throw std::invalid_argument("invalid carrier");
            auto config=p.settings.config;config.carrier_hz=carrier;legacy::validate(config);
            p.settings.config=config;p.valid=true;p.changed_settings();
        } catch(const std::exception&) {
            p.session.cancel();report_error("Enter a carrier frequency within the selected mode's audio passband.");
        }
    }
}
void Controller::select(F field,std::string value) {
    auto& p=*impl_;if(field!=F::legacy_profile||p.closing||!p.f(field).enabled||value==p.f(field).selected)return;
    auto config=p.settings.config;
    if(value=="olivia4-2000")config.mode=legacy::Mode::olivia4_2000;
    else if(value=="bpsk31")config.mode=legacy::Mode::bpsk31;
    else if(value=="bpsk125")config.mode=legacy::Mode::bpsk125;
    else return;
    try {legacy::validate(config);}
    catch(const std::exception& e) {report_error(e.what());return;}
    p.settings.config=config;p.f(field).selected=std::move(value);p.changed_settings();
}
void Controller::activate(C command) {
    if(!enabled(command))return;
    auto& p=*impl_;p.pending_tx=p.f(F::legacy_text).text;p.failed=false;
    p.session.cancel();p.f(F::legacy_status).text="Preparing transmission…";p.refresh();++p.revision;
}
bool Controller::enabled(C command) const {
    const auto& p=*impl_;
    return command==C::legacy_transmit&&p.selected&&!p.closing&&p.valid&&!p.snapshot.transmitting&&!p.pending_tx&&!p.f(F::legacy_text).text.empty();
}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::report_error(std::string error) {impl_->f(F::legacy_status).text=std::move(error);++impl_->revision;}
std::uint64_t Controller::revision() const {return impl_->revision;}
BitmapSource Controller::bitmap() const {return impl_->waterfall.source();}
std::uint64_t Controller::bitmap_revision() const {return impl_->waterfall.revision();}
std::string Controller::bitmap_caption() const {return impl_->waterfall.caption();}
std::string Controller::bitmap_title() const {return impl_->waterfall.title();}
}
