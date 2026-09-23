#include "legacy/controller.hpp"
#include "datapump/legacy/session.hpp"
#include <iostream>
#include <stdexcept>

namespace fixture {
enum class Completion { none, cancelled, failed };
Completion completion=Completion::none;
unsigned active_reads=0,listens=0;
bool complete_on_cancel=false;
void reset() {completion=Completion::none;active_reads=listens=0;complete_on_cancel=false;}
void arm(Completion value) {completion=value;active_reads=0;}
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
}

// Script the worker completing after the controller observes its active
// snapshot, but before the next active() query. No clock, sleeps or production
// hooks are needed to exercise this legal interleaving deterministically.
namespace datapump::legacy {
struct Session::Impl {Settings settings;Snapshot snapshot;bool closing=false;};
Session::Session():impl_(std::make_unique<Impl>()) {}
Session::~Session()=default;
void Session::configure(const Settings& settings) {
    fixture::check(!impl_->snapshot.active,"Configured a session before closure");
    impl_->settings=settings;
}
void Session::listen() {
    auto& s=impl_->snapshot;
    fixture::check(!s.active,"Restarted capture before closure");
    ++fixture::listens;s.active=s.listening=true;s.transmitting=false;
    s.error.clear();s.status="Listening · simplex";++s.revision;
}
void Session::transmit_text(const std::string&) {
    auto& s=impl_->snapshot;
    fixture::check(!s.active,"Started playback before capture closed");
    s.active=s.transmitting=true;s.listening=false;s.error.clear();
    s.status="Transmitting · simplex";++s.transmission;++s.revision;
}
void Session::cancel() {
    auto& s=impl_->snapshot;
    if(s.transmitting&&fixture::complete_on_cancel) {
        fixture::arm(fixture::Completion::cancelled);return;
    }
    s.active=s.listening=s.transmitting=false;s.status="Legacy audio stopped";++s.revision;
}
Snapshot Session::poll()const {return impl_->snapshot;}
bool Session::active()const {
    auto& s=impl_->snapshot;
    if(fixture::completion!=fixture::Completion::none&&++fixture::active_reads==2) {
        s.active=s.listening=s.transmitting=false;
        if(fixture::completion==fixture::Completion::failed) {
            s.error="scripted playback failure";s.status="Legacy audio error";
        } else s.status="Legacy audio stopped";
        ++s.revision;fixture::completion=fixture::Completion::none;
    }
    return s.active;
}
void Session::close() {impl_->closing=true;cancel();}
bool Session::ready_to_close()const {return !impl_->snapshot.active;}
}

namespace {
using namespace datapump;
using F=gui::ui::Field;
using C=gui::ui::Command;
using fixture::check;
void start_transmission(gui::legacy_ui::Controller& controller) {
    controller.selected(true);controller.poll();
    check(fixture::listens==1,"Initial capture did not start");
    controller.edit(F::legacy_text,"retained draft");controller.transmit();controller.poll();
    check(controller.command_label()=="Cancel","Transmission did not start");
}
void cancellation_completion_between_reads() {
    fixture::reset();gui::legacy_ui::Controller controller([]{return true;});
    start_transmission(controller);
    fixture::complete_on_cancel=true;controller.activate(C::legacy_transmit);
    check(controller.command_label()=="Cancelling…","Cancellation skipped pending closure");
    controller.poll();
    check(fixture::completion==fixture::Completion::none,"Completion interleaving was not exercised");
    check(fixture::listens==2&&controller.active(),"Capture did not resume after playback closed");
    check(controller.command_label()=="Transmit"&&controller.enabled(C::legacy_transmit),
          "Resumed capture retained stale cancellation state");
    check(controller.field(F::legacy_status).text=="Listening · simplex","Resumed capture retained cancellation status");
    check(controller.field(F::legacy_text).text=="retained draft","Cancellation removed the uncommitted draft");
    controller.close();check(controller.ready_to_close(),"Controller did not close");
}
void final_error_between_reads() {
    fixture::reset();gui::legacy_ui::Controller controller([]{return true;});
    start_transmission(controller);
    fixture::arm(fixture::Completion::failed);controller.poll();
    check(fixture::completion==fixture::Completion::none,"Failure interleaving was not exercised");
    check(fixture::listens==1&&!controller.active(),"Final playback error was discarded by restarting capture");
    check(controller.field(F::legacy_status).text=="scripted playback failure","Final playback error was not presented");
    check(controller.field(F::legacy_text).text=="retained draft","Playback failure removed the uncommitted draft");
    controller.close();check(controller.ready_to_close(),"Failed controller did not close");
}
}
int main() {
    try {
        cancellation_completion_between_reads();final_error_between_reads();
        std::cout<<"Legacy controller observes final closure and errors before restarting audio\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
