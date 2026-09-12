#include "gui_smoke.hpp"
#include "datapump/runtime.hpp"
#include <fstream>
#include <set>
#include <tuple>
#include <thread>

namespace datapump::gui {
namespace {
using F=ui::Field;
using C=ui::Command;
using Clock=std::chrono::steady_clock;
const std::string message="CQ Rev GUI smoke: exact UTF-8 caf\xc3\xa9. Shared controller and semantic controls.";
const Bytes file_bytes{0,1,2,3,0xff,0xc0,0x80,'D','a','t','a','P','u','m','p','\n'};
void require(bool condition,const char* message_) { if(!condition) throw Error(message_); }
std::string path_text(const std::filesystem::path& path) { const auto s=path.u8string(); return {s.begin(),s.end()}; }
}
struct Smoke::Impl {
    std::filesystem::path directory,input_path,save_path;
    double timeout;
    Clock::time_point started=Clock::now();
    unsigned phase=0;
    bool done=false,pending_packet=false,pending_binary=false,replay_seen=false;
    explicit Impl(std::filesystem::path path,double seconds):directory(std::move(path)),timeout(seconds) {
        if(directory.empty()) directory=std::filesystem::temp_directory_path()/("datapump-shared-smoke-"+std::to_string(started.time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto suffix=std::to_string(started.time_since_epoch().count());
        input_path=directory/("attachment-"+suffix+".bin"); save_path=directory/("received-"+suffix+".bin");
    }
    ui::ServiceRequest take(Controller& controller,ui::ServiceKind kind) {
        auto requests=controller.take_services();
        require(requests.size()==1&&requests.front().kind==kind,"Smoke received an unexpected platform service request");
        return std::move(requests.front());
    }
    void check_copy(Controller& controller,std::string_view expected) {
        controller.activate(C::copy_signal);
        const auto request=take(controller,ui::ServiceKind::clipboard);
        require(request.value==expected,"Shared GUI clipboard request changed the received payload");
        controller.complete_service({request.id,false,{},{}});
    }
    void step(Controller& controller) {
        if(done) return;
        if(std::chrono::duration<double>(Clock::now()-started).count()>timeout) throw Error("Shared GUI smoke timed out: "+controller.field(F::status).text);
        require(controller.settings().simulation,"Shared GUI smoke attempted hardware audio");
        const auto& snapshot=controller.snapshot();
        replay_seen=replay_seen||snapshot.simulation_replay;
        for(const auto& line:controller.signals().lines()) {
            if(!line.binary&&!line.validated) pending_packet=true;
            if(line.binary&&!line.complete) pending_binary=true;
        }
        if(phase==0) {
            controller.edit(F::message,"Discarded draft");
            controller.edit(F::message,message);
            controller.select(F::fec,"off");
            const auto revision=controller.revision();
            controller.select(F::qr_brightness,"dim");
            controller.activate(C::zoom_in); controller.activate(C::reset_zoom);
            require(controller.revision()==revision,"Display-only actions changed modem settings");
            phase=1;
        } else if(phase==1&&controller.enabled(C::transmit)) {
            controller.activate(C::transmit); phase=2;
        } else if(phase==2&&!snapshot.transmitting&&!snapshot.simulation_replay) {
            const auto found=std::find_if(controller.inbox().items().begin(),controller.inbox().items().end(),[](const auto& packet) { return std::string(packet.message.data.begin(),packet.message.data.end())==message; });
            if(found==controller.inbox().items().end()) return;
            require(pending_packet&&replay_seen,"Packet skipped chronological pending/replay presentation");
            bool copied=false;
            for(const auto& line:controller.signals().lines()) if(line.validated&&line.packet_id==id_label(found->message)) {
                controller.select(F::signals,std::to_string(line.id)); check_copy(controller,message); copied=true; break;
            }
            require(copied,"Verified received text was not available through signal selection");
            write_new_file(path_text(input_path),file_bytes);
            controller.activate(C::attach_file); const auto request=take(controller,ui::ServiceKind::open_file);
            controller.complete_service({request.id,false,path_text(input_path),{}}); phase=3;
        } else if(phase==3&&controller.enabled(C::transmit)) {
            controller.activate(C::transmit); phase=4;
        } else if(phase==4&&!snapshot.transmitting&&!snapshot.simulation_replay) {
            const auto files=controller.inbox().file_items();
            const auto found=std::find_if(files.begin(),files.end(),[](const auto* packet) { return packet->message.data==file_bytes; });
            if(found==files.end()) return;
            controller.select(F::files,id_label((*found)->message)); controller.activate(C::save_file);
            const auto request=take(controller,ui::ServiceKind::save_file);
            controller.activate(C::clear_received); // The pending request must own the selected bytes.
            controller.complete_service({request.id,false,path_text(save_path),{}});
            std::ifstream input(save_path,std::ios::binary); require(static_cast<bool>(input),"Shared save service did not create the file");
            require(read_bounded(input,1024)==file_bytes,"Clearing the inbox invalidated a pending save payload");
            controller.activate(C::use_text); controller.select(F::source,"binary"); controller.edit(F::binary,"0 0\n1"); phase=5;
        } else if(phase==5&&controller.enabled(C::transmit)) {
            controller.activate(C::transmit); phase=6;
        } else if(phase==6&&!snapshot.transmitting&&!snapshot.simulation_replay) {
            for(const auto& line:controller.signals().lines()) if(line.binary&&line.complete) {
                require(pending_binary,"Raw binary skipped pending reception");
                require(!line.validated&&line.packet_id.empty()&&line.text=="001"&&line.received_bits==3&&line.expected_bits==3,"Raw binary changed bit count, leading zeros, or verification meaning");
                controller.select(F::signals,std::to_string(line.id)); check_copy(controller,"001");
                controller.select(F::source,"message");
                require(controller.field(F::message).text==message&&controller.field(F::binary).text=="0 0\n1","Source switching discarded an inactive editor");
                require(controller.inbox().items().empty(),"Raw binary was promoted to a verified packet");
                done=true; break;
            }
        }
    }
};
Smoke::Smoke(std::filesystem::path directory,double timeout):impl_(std::make_unique<Impl>(std::move(directory),timeout)) {}
Smoke::~Smoke()=default;
void Smoke::step(Controller& controller) { impl_->step(controller); }
bool Smoke::done() const { return impl_->done; }
void controller_self_check() {
    Controller controller({true,true});
    require(controller.field(F::qr_brightness).selected=="dark","QR brightness must start Dark");
    require(!controller.field(F::binary).enabled,"Inactive binary editor must not control message dispatch");
    controller.edit(F::message,"Preserve this message");
    const auto revision=controller.revision();
    controller.edit(F::message,"Preserve this message");
    controller.select(F::qr_brightness,"normal");
    controller.activate(C::zoom_in); controller.activate(C::reset_zoom);
    require(controller.revision()==revision,"Unchanged input or display actions changed application revision");
    const auto settings=controller.settings().transfer.modem;
    controller.edit(F::bandwidth,"invalid draft");
    require(controller.field(F::bandwidth).text=="invalid draft","Invalid draft was silently replaced");
    require(controller.settings().transfer.modem.bandwidth_hz==settings.bandwidth_hz,"Invalid draft replaced the last valid modem settings");
    require(!controller.enabled(C::transmit),"Invalid settings enabled transmission");
    controller.edit(F::bandwidth,"1.2 kHz");
    controller.select(F::source,"binary"); controller.edit(F::binary,"001");
    controller.select(F::source,"message");
    require(controller.field(F::message).text=="Preserve this message"&&controller.field(F::binary).text=="001","Source switching discarded editor text");
    const auto before=controller.field(F::message).text;
    controller.edit(F::message,std::string(1024*1024+1,'a'));
    require(controller.field(F::message).text==before,"Over-limit edit was truncated or accepted");
    controller.edit(F::message,std::string("bad\xc3",4));
    require(controller.field(F::message).text==before,"Invalid UTF-8 was accepted by shared text state");
    controller.edit(F::callsign,"two\nlines");
    require(controller.field(F::callsign).text.empty(),"Single-line field accepted a newline");
    std::set<std::tuple<ui::Page,int,int>> identities;
    for(const auto* screen:{&ui::console_screen(),&ui::inspection_screen()}) for(const auto& control:*screen) {
        const auto kind=control.kind==ui::Kind::action?1:control.kind==ui::Kind::bitmap?2:0;
        const auto binding=kind==1?static_cast<int>(control.command):kind==2?static_cast<int>(control.bitmap):static_cast<int>(control.field);
        require(identities.emplace(control.page,kind,binding).second,"Screen declares a duplicate binding identity");
    }
    require(controller.field(F::key).selected=="none","Empty key list selected an imaginary key");
    require(!controller.enabled(C::acknowledge_key_failure),"Key failure acknowledgement is available without a failure");
    controller.activate(C::open_keyfile);
    const auto requests=controller.take_services();
    require(requests.size()==1&&requests.front().kind==ui::ServiceKind::open_file,"Key command did not request the platform file service");
    const auto missing=std::filesystem::temp_directory_path()/("missing-datapump-key-"+std::to_string(Clock::now().time_since_epoch().count()));
    controller.complete_service({requests.front().id,false,path_text(missing),{}});
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(!controller.enabled(C::acknowledge_key_failure)&&Clock::now()<deadline) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(controller.enabled(C::acknowledge_key_failure),"Failed key load did not expose explicit acknowledgement");
    controller.select(F::key,"none");
    require(controller.enabled(C::acknowledge_key_failure),"Repeating an unchanged Choice silently acknowledged a key failure");
    controller.activate(C::acknowledge_key_failure);
    require(!controller.enabled(C::acknowledge_key_failure)&&controller.field(F::key).selected=="none","Explicit key failure acknowledgement changed the retained selection");

    // Completion cannot be delivered until poll, so this deterministically
    // tests an already-dispatched attachment finishing after Use text.
    Controller attachment_controller({true,true});
    attachment_controller.edit(F::message,"Preserve this message");
    const auto attachment_path=std::filesystem::temp_directory_path()/("datapump-controller-attachment-"+std::to_string(Clock::now().time_since_epoch().count()));
    write_new_file(path_text(attachment_path),file_bytes);
    attachment_controller.activate(C::attach_file);
    const auto attachment_requests=attachment_controller.take_services();
    require(attachment_requests.size()==1,"Attachment chooser was not requested");
    attachment_controller.complete_service({attachment_requests.front().id,false,path_text(attachment_path),{}});
    attachment_controller.poll();
    attachment_controller.activate(C::use_text);
    const auto text_revision=attachment_controller.revision();
    const auto attachment_deadline=Clock::now()+std::chrono::seconds(3);
    while(!attachment_controller.estimate()&&Clock::now()<attachment_deadline) {
        attachment_controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::filesystem::remove(attachment_path);
    require(attachment_controller.estimate().has_value(),"Estimate did not recover after cancelling a pending attachment");
    require(attachment_controller.revision()==text_revision&&attachment_controller.field(F::message_label).text=="Message"&&attachment_controller.field(F::message).enabled,"Late attachment completion undid Use text");
}
}
