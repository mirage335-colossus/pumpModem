#include "controller.hpp"
#include "screen.hpp"
#include "presentation.hpp"
#include "plots.hpp"
#include "../audio_controls.hpp"
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
    bool selected=false,closing=false,valid=true,failed=false,reconfigure=false,cancelling=false;
    std::uint64_t revision=1;
    ui::FieldState& f(F field) {return fields.at(static_cast<std::size_t>(field));}
    const ui::FieldState& f(F field) const {return fields.at(static_cast<std::size_t>(field));}
    explicit Impl(std::function<bool()> acquire):acquire_audio(std::move(acquire)) {
        f(F::legacy_profile).options={{"olivia4-2000","Olivia-4/2k"},{"bpsk31","BPSK31"},{"bpsk125","BPSK125"}};
        f(F::legacy_profile).selected="bpsk31";f(F::legacy_carrier).text="1500";
        f(F::legacy_squelch).options={{"low","Low · weak signals"},{"normal","Normal"},{"high","High · cleaner text"}};
        f(F::legacy_squelch).selected="normal";
        f(F::legacy_mono).options={{"left","Left mono"},{"right","Right mono"},{"stereo","Stereo"}};
        f(F::legacy_mono).selected="left";
        f(F::legacy_device).selected=f(F::legacy_device).text="default";
        f(F::legacy_device).options={{"default","Default audio device"}};
        f(F::legacy_volume).options=audio_controls::volume_options();
        f(F::legacy_volume).selected="100";
        f(F::legacy_status).text="Select Legacy Modem to receive.";
        refresh();
    }
    void refresh() {
        const bool edit=!closing&&!snapshot.transmitting&&!pending_tx&&!cancelling;
        f(F::legacy_profile).enabled=edit;f(F::legacy_carrier).enabled=edit;f(F::legacy_squelch).enabled=edit;f(F::legacy_mono).enabled=edit;
        f(F::legacy_device).enabled=edit;f(F::legacy_volume).enabled=edit;
        f(F::legacy_exclusive).enabled=edit&&audio::exclusive_supported();
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
        if(cancelling) {
            if(!snapshot.active) {cancelling=false;++revision;}
            else f(F::legacy_status).text="Cancelling transmission…";
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
void Controller::set_shellcode_mode(bool enabled) {
    auto& p=*impl_;
    if(p.text.set_shellcode_mode(enabled,p.f(F::legacy_transcript)))++p.revision;
}
void Controller::poll() {
    auto& p=*impl_;
    // Retain the final acknowledgment once, then leave dormant Legacy history
    // alone while another modem owns the interface and receiver budget.
    const bool audio_active=p.session.active();
    if(!p.selected&&!audio_active&&!p.snapshot.active)return;
    if(audio_active||p.snapshot.active)p.observe();
    if(p.closing||!p.selected||!p.valid||p.session.active())return;
    // The worker can close after observe() took its snapshot. Consume the
    // final state before another session makes active() true again, otherwise
    // cancellation remains pending and a final playback error can be lost.
    p.observe();
    if(p.failed&&!p.pending_tx)return;
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
void Controller::set_devices(const std::vector<audio::Device>& devices) {
    auto& p=*impl_;audio_controls::set_device_options(p.f(F::legacy_device),devices);++p.revision;
}
void Controller::edit(F field,std::string value) {
    auto& p=*impl_;if(p.closing||!p.f(field).enabled||p.f(field).text==value)return;
    if(field==F::legacy_text) {
        if(const auto error=ui::edit_error(value,true,legacy::text_byte_limit);!error.empty()) {report_error(error);return;}
        p.text.edited(value);p.f(field).text=std::move(value);++p.revision;
    } else if(field==F::legacy_device) {
        if(const auto error=ui::edit_error(value,false,4096);!error.empty()) {report_error(error);return;}
        p.settings.device=value;p.f(field).selected=value;p.f(field).text=std::move(value);
        auto& options=p.f(field).options;
        if(!p.f(field).selected.empty()&&std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==p.f(field).selected;}))
            options.push_back({p.f(field).selected,p.f(field).selected});
        p.changed_settings();p.refresh();
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
    auto& p=*impl_;if((field!=F::legacy_profile&&field!=F::legacy_squelch&&field!=F::legacy_mono&&field!=F::legacy_device&&field!=F::legacy_volume)||p.closing||!p.f(field).enabled||value==p.f(field).selected)return;
    const auto& options=p.f(field).options;
    if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==value&&option.enabled;}))return;
    if(field==F::legacy_device) {
        p.settings.device=value;p.f(field).text=value;p.f(field).selected=std::move(value);
        p.changed_settings();p.refresh();return;
    }
    if(field==F::legacy_volume) {
        p.settings.transmit_gain=audio_controls::volume_gain(value);p.f(field).selected=std::move(value);
        ++p.revision;return;
    }
    if(field==F::legacy_mono) {
        if(value!="left"&&value!="right"&&value!="stereo")return;
        p.settings.mono=value!="stereo";
        p.settings.channel_mode=value=="right"?audio::ChannelMode::right_mono:
            p.settings.mono?audio::ChannelMode::left_mono:audio::ChannelMode::stereo;
        p.f(field).selected=std::move(value);++p.revision;return;
    }
    auto config=p.settings.config;
    if(field==F::legacy_squelch) {
        if(value!="low"&&value!="normal"&&value!="high")return;
        config.squelch=value=="low"?0:value=="normal"?1:2;
    }
    else if(value=="olivia4-2000")config.mode=legacy::Mode::olivia4_2000;
    else if(value=="bpsk31")config.mode=legacy::Mode::bpsk31;
    else if(value=="bpsk125")config.mode=legacy::Mode::bpsk125;
    else return;
    try {legacy::validate(config);}
    catch(const std::exception& e) {report_error(e.what());return;}
    p.settings.config=config;p.f(field).selected=std::move(value);p.changed_settings();
}
void Controller::toggle(F field,bool value) {
    auto& p=*impl_;
    if(field!=F::legacy_exclusive||p.closing||!p.f(field).enabled||p.f(field).checked==value)return;
    p.settings.exclusive=value;p.f(field).checked=value;p.changed_settings();p.refresh();
}
void Controller::activate(C command) {
    if(!enabled(command))return;
    auto& p=*impl_;
    if(p.pending_tx||p.snapshot.transmitting) {
        p.pending_tx.reset();p.cancelling=p.session.active();p.session.cancel();
        p.f(F::legacy_status).text=p.cancelling?"Cancelling transmission…":"Transmission cancelled";
        p.refresh();++p.revision;return;
    }
    transmit();
}
void Controller::transmit() {
    auto& p=*impl_;
    if(!enabled(C::legacy_transmit)||p.pending_tx||p.snapshot.transmitting||p.cancelling)return;
    p.pending_tx=p.f(F::legacy_text).text;p.failed=false;
    p.session.cancel();p.f(F::legacy_status).text="Preparing transmission…";p.refresh();++p.revision;
}
bool Controller::enabled(C command) const {
    const auto& p=*impl_;
    return command==C::legacy_transmit&&p.selected&&!p.closing&&!p.cancelling&&
        (p.pending_tx||p.snapshot.transmitting||(p.valid&&!p.f(F::legacy_text).text.empty()));
}
std::string Controller::command_label() const {
    const auto& p=*impl_;
    return p.cancelling?"Cancelling…":p.pending_tx||p.snapshot.transmitting?"Cancel":"Transmit";
}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::report_error(std::string error) {impl_->f(F::legacy_status).text=std::move(error);++impl_->revision;}
std::uint64_t Controller::revision() const {return impl_->revision;}
BitmapSource Controller::bitmap() const {return impl_->waterfall.source();}
std::uint64_t Controller::bitmap_revision() const {return impl_->waterfall.revision();}
std::string Controller::bitmap_caption() const {return impl_->waterfall.caption();}
std::string Controller::bitmap_title() const {return impl_->waterfall.title();}
}
