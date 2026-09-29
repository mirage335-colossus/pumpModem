#include "web_bridge.hpp"
#include "control_interactions.hpp"
#include "document_presentation.hpp"
#include "service_queue.hpp"
#include "text_policy.hpp"
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace datapump::gui::web {
namespace {
constexpr std::size_t maximum_text=1024*1024,maximum_snapshot=8*1024*1024;
std::string json_string(std::string_view value) {
    constexpr char hex[]="0123456789abcdef";
    std::string out;out.reserve(value.size()+2);out+='"';
    for(const unsigned char c:value) {
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32||c=='<'||c=='>'||c=='&') {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    out+='"';return out;
}
std::string identifier(std::uint64_t id) {return json_string(std::to_string(id));}
std::string base64(const std::vector<unsigned char>& bytes) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3) {
        const auto n=static_cast<unsigned>(bytes[i])<<16|
            (i+1<bytes.size()?static_cast<unsigned>(bytes[i+1])<<8:0U)|
            (i+2<bytes.size()?static_cast<unsigned>(bytes[i+2]):0U);
        out+=alphabet[n>>18];out+=alphabet[(n>>12)&63];
        out+=i+1<bytes.size()?alphabet[(n>>6)&63]:'=';out+=i+2<bytes.size()?alphabet[n&63]:'=';
    }
    return out;
}
const char* kind_name(ui::Kind kind) {
    switch(kind) {
    case ui::Kind::label:return "label";case ui::Kind::action:return "action";
    case ui::Kind::toggle:return "toggle";case ui::Kind::choice:return "choice";
    case ui::Kind::text:return "text";case ui::Kind::list:return "list";case ui::Kind::bitmap:return "bitmap";
    }
    throw std::invalid_argument("unsupported presentation primitive");
}
const char* service_name(ui::ServiceKind kind) {
    switch(kind) {
    case ui::ServiceKind::open_file:return "open_file";case ui::ServiceKind::save_file:return "save_file";
    case ui::ServiceKind::prompt:return "prompt";case ui::ServiceKind::clipboard:return "clipboard";
    case ui::ServiceKind::open_folder:return "open_folder";
    }
    throw std::invalid_argument("unsupported service primitive");
}
std::string identity(const ui::Control& c) {
    return "control:"+std::to_string(c.surface)+":"+std::to_string(static_cast<int>(c.page))+":"+
        std::to_string(static_cast<int>(c.kind))+":"+std::to_string(static_cast<int>(c.field))+":"+
        std::to_string(static_cast<int>(c.command))+":"+std::to_string(static_cast<int>(c.bitmap))+":"+
        std::to_string(static_cast<int>(c.menu))+":"+std::to_string(c.instance);
}
void bitmap(std::ostream& out,const BitmapSource& source,int width,int height) {
    const unsigned w=static_cast<unsigned>(std::clamp(width,1,640));
    const unsigned h=static_cast<unsigned>(std::clamp(height,1,320));
    BitmapImage image(w,h,PixelFormat::rgb24);
    auto request=full_bitmap_request(w,h,false,true);request.fit_content=true;
    source.paint(request,[&](unsigned x,unsigned y,PixelBlock block){image.blit(x,y,block);});
    out<<"{\"width\":"<<w<<",\"height\":"<<h<<",\"sampling\":"
       <<json_string(source.sampling()==BitmapSampling::discrete?"discrete":"continuous")
       <<",\"rgb\":"<<json_string(base64(image.pixels()))<<'}';
}
ui::Key key(int value) {
    if(value<static_cast<int>(ui::Key::other)||value>static_cast<int>(ui::Key::del))return ui::Key::other;
    return static_cast<ui::Key>(value);
}
}
struct Bridge::Impl {
    explicit Impl(Application& value):app(value) {}
    struct Target {
        enum class Type {control,page,action};
        Type type=Type::control;
        ui::Control control{};
        ui::Page page=ui::Page::console;
        ui::Command action=ui::Command::none;
        bool enabled=true;
        std::string document_key;
        std::optional<ui::DocumentActionIdentity> document_action;
    };
    Application& app;
    std::uint64_t epoch=1,sequence=0,next_id=1,service_id=0;
    std::map<std::string,std::uint64_t> identities;
    std::map<std::string,std::uint64_t> retained;
    std::map<std::uint64_t,Target> targets;
    std::map<std::string,unsigned> occurrences;
    std::string signature;
    ui::ServiceQueue services;
    int width=1000,height=760;
    bool initialized=false;
    std::uint64_t fresh() {
        if(next_id==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("web identifier space exhausted");
        return next_id++;
    }
    std::uint64_t target(std::string name,Target value) {
        name+=':'+std::to_string(occurrences[name]++);
        auto it=identities.find(name);
        const auto id=it==identities.end()?fresh():it->second;
        retained.emplace(std::move(name),id);targets.emplace(id,std::move(value));
        if(targets.size()>8192)throw std::length_error("too many web presentation elements");
        return id;
    }
    std::string current_signature() const {
        std::string out=std::to_string(static_cast<int>(app.page()))+":";
        const auto overlay=app.overlay();out+=std::to_string(overlay?overlay->generation:0);
        for(const auto& [id,item]:targets) {
            out+=':'+std::to_string(id);
            if(item.type==Target::Type::control) {
                const auto p=app.control(item.control);
                out+=':'+std::to_string(p.state.text_history_revision);
            }
        }
        return out;
    }
    void synchronize_services() {
        const auto old=services.current()?services.current()->id:0;
        services.synchronize(app.take_services(),app.closing());
        const auto* request=services.next();
        if(!request)service_id=0;
        else if(!service_id||request->id!=old)service_id=fresh();
        app.set_service_active(request!=nullptr);
    }
    void options(std::ostream& out,const ui::FieldState& state) {
        out<<"[";bool first=true;
        for(const auto& option:state.options) {
            if(!first)out<<',';
            first=false;
            out<<"{\"id\":"<<json_string(option.id)<<",\"label\":"<<json_string(option.label)
               <<",\"enabled\":"<<(option.enabled?"true":"false")<<'}';
        }
        out<<']';
    }
    void control(std::ostream& out,const ui::Control& c,bool overlay=false,
                 std::string document_key={},bool inherited_enabled=true) {
        const auto p=app.control(c);const auto& s=p.state;
        if(!p.visible)return;
        Target t;t.control=c;t.document_key=document_key;t.enabled=inherited_enabled;
        const auto id=target(document_key.empty()?identity(c):document_key,t);
        const auto layout=app.control_layout(c,width,height);
        out<<"{\"id\":"<<identifier(id)<<",\"kind\":"<<json_string(kind_name(c.kind))
           <<",\"label\":"<<json_string(p.label)<<",\"help\":"<<json_string(c.help)
           <<",\"enabled\":"<<(p.enabled&&inherited_enabled?"true":"false")<<",\"persistent\":"<<(c.persistent?"true":"false")
           <<",\"overlay\":"<<(overlay?"true":"false")<<",\"row\":"<<c.row
           <<",\"stretch\":"<<c.stretch<<",\"multiline\":"<<(c.multiline?"true":"false")
           <<",\"readOnly\":"<<(c.read_only?"true":"false")<<",\"byteLimit\":"<<c.byte_limit
           <<",\"followTail\":"<<(c.follow_tail?"true":"false")<<",\"activateOnSelect\":"<<(c.activate_on_select?"true":"false")
           <<",\"submit\":"<<(c.submit!=ui::Command::none?"true":"false")
           <<",\"submitEnter\":"<<(app.submit_gesture(c,false,false)?"true":"false")
           <<",\"submitCtrlEnter\":"<<(app.submit_gesture(c,true,false)?"true":"false")
           <<",\"tabNavigation\":"<<(c.tab_navigation?"true":"false")
           <<",\"text\":"<<json_string(s.text)<<",\"displayText\":"<<json_string(s.display_text)
           <<",\"selected\":"<<json_string(s.selected)<<",\"checked\":"<<(s.checked?"true":"false")
           <<",\"tone\":"<<static_cast<int>(s.text_tone)<<",\"history\":"<<identifier(s.text_history_revision)
           <<",\"cursorEnd\":"<<identifier(s.text_cursor_end_revision)<<",\"options\":";
        options(out,s);
        out<<",\"records\":[";bool first=true;
        for(const auto& record:s.records) {
            if(!first)out<<',';
            first=false;
            out<<"{\"id\":"<<json_string(record.id)<<",\"enabled\":"<<(record.enabled?"true":"false")
               <<",\"activatable\":"<<(record.activatable?"true":"false")<<",\"cells\":[";
            bool first_cell=true;
            for(const auto& cell:record.cells) {
                if(!first_cell)out<<',';
                first_cell=false;
                out<<"{\"text\":"<<json_string(cell.text)<<",\"tone\":"<<static_cast<int>(cell.tone)
                   <<",\"bold\":"<<(cell.bold?"true":"false")<<'}';
            }
            out<<"]}";
        }
        out<<']';
        if(c.kind==ui::Kind::bitmap) {
            const auto picture=app.bitmap(c,static_cast<unsigned>(std::clamp(width,1,640)));
            out<<",\"caption\":"<<json_string(picture.caption)<<",\"bitmap\":";
            bitmap(out,picture.source,std::max(layout.widget.w,240),std::max(layout.widget.h,80));
            out<<",\"click\":"<<(c.click!=ui::Command::none?"true":"false")
               <<",\"doubleClick\":"<<(c.double_click!=ui::Command::none?"true":"false")
               <<",\"wheel\":"<<((c.wheel_up!=ui::Command::none||c.wheel_down!=ui::Command::none)?"true":"false");
        }
        out<<'}';
    }
    std::map<std::string,unsigned> document_occurrences;
    static std::string action_key(ui::DocumentActionIdentity identity) {
        return "document-action:"+std::to_string(static_cast<int>(identity.command))+":"+
            std::to_string(identity.instance)+":"+std::to_string(identity.occurrence);
    }
    std::optional<Target> current_document_target(const Target& wanted) {
        ui::DocumentPresentation current;current.reset(app.document(app.page(),width));
        if(wanted.document_action) {
            const auto* action=current.actions().find(*wanted.document_action);
            if(!action)return {};
            auto result=wanted;result.enabled=action->enabled;result.action=action->node->command;return result;
        }
        std::map<std::string,unsigned> occurrences;
        std::optional<Target> result;
        const auto visit=[&](auto&& self,const ui::DocumentPresentation::Node& presented,unsigned depth)->void {
            if(depth>64)throw std::length_error("web document nesting limit exceeded");
            const auto& node=*presented.source;
            if(node.kind==ui::DocumentKind::control&&node.control&&ui::document_control_supported(*node.control)&&app.control(*node.control).visible) {
                const auto base="document-control:"+identity(*node.control);
                const auto key=base+":"+std::to_string(occurrences[base]++);
                if(key==wanted.document_key) {result=wanted;result->control=*node.control;result->enabled=presented.enabled;}
            }
            for(const auto& child:presented.children)self(self,child,depth+1);
        };
        if(current.root())visit(visit,*current.root(),0);
        return result;
    }
    void document(std::ostream& out,const ui::DocumentPresentation::Node& presented,unsigned depth=0) {
        if(depth>64)throw std::length_error("web document nesting limit exceeded");
        const auto& node=*presented.source;
        constexpr const char* kinds[]={"column","row","text","bitmap","action","control"};
        out<<"{\"kind\":"<<json_string(kinds[static_cast<unsigned>(node.kind)])<<",\"text\":"<<json_string(node.text)
           <<",\"tone\":"<<static_cast<int>(node.tone)<<",\"bold\":"<<(node.bold?"true":"false")
           <<",\"border\":"<<(node.border?"true":"false")<<",\"enabled\":"<<(presented.enabled?"true":"false");
        if(node.kind==ui::DocumentKind::control&&node.control&&ui::document_control_supported(*node.control)) {
            out<<",\"control\":";
            if(app.control(*node.control).visible) {
                const auto base="document-control:"+identity(*node.control);
                control(out,*node.control,false,base+":"+std::to_string(document_occurrences[base]++),presented.enabled);
            } else out<<"null";
        }
        if(node.kind==ui::DocumentKind::action&&presented.action) {
            Target t;t.type=Target::Type::action;t.action=node.command;t.enabled=presented.enabled;
            t.document_action=presented.action;t.document_key=action_key(*presented.action);
            const auto id=target(t.document_key,t);
            out<<",\"id\":"<<identifier(id)<<",\"available\":"<<(presented.enabled&&app.enabled(node.command)?"true":"false");
        }
        if(node.kind==ui::DocumentKind::bitmap) {
            out<<",\"bitmap\":";bitmap(out,node.plot,static_cast<int>(node.width>0?node.width:width),
                static_cast<int>(node.height>0?node.height:200));
        }
        out<<",\"children\":[";bool first=true;
        for(const auto& child:presented.children) {if(!first)out<<',';first=false;document(out,child,depth+1);}
        out<<"]}";
    }
};
Bridge::Bridge(Application& application):impl_(std::make_unique<Impl>(application)) {application.enable_service_completions(true);}
Bridge::~Bridge() {impl_->app.enable_service_completions(false);}
std::uint64_t Bridge::generation() const {return impl_->epoch;}
std::uint64_t Bridge::last_sequence() const {return impl_->sequence;}
void Bridge::reconnect() {
    auto& s=*impl_;
    if(const auto* request=s.services.next()) {
        ui::ServiceResult result{request->id,true,{},{}};
        if(s.services.complete(result))s.app.complete_service(std::move(result));
    }
    s.service_id=0;s.app.set_service_active(false);++s.epoch;s.signature.clear();s.initialized=false;
    s.targets.clear();s.identities.clear();s.sequence=0;
}
std::optional<ui::ServiceRequest> Bridge::service(std::uint64_t id) const {
    const auto& s=*impl_;const auto* request=s.services.current();
    if(!id||id!=s.service_id||!request||(request->valid&&!*request->valid))return {};
    return *request;
}
std::string Bridge::snapshot(int width,int height) {
    auto& s=*impl_;s.width=std::clamp(width,240,4096);s.height=std::clamp(height,240,4096);
    s.synchronize_services();
    if(s.initialized&&s.current_signature()!=s.signature)++s.epoch;
    s.retained.clear();s.targets.clear();s.occurrences.clear();s.document_occurrences.clear();
    std::ostringstream body;
    body<<",\"title\":"<<json_string(ui::window_title())<<",\"revision\":"<<identifier(s.app.revision())
        <<",\"ack\":"<<identifier(s.sequence)<<",\"closing\":"<<(s.app.closing()?"true":"false")<<",\"tabs\":[";
    bool first=true;
    for(const auto& tab:s.app.tab_layout(s.width,s.height)) {
        if(!tab.visible)continue;
        Impl::Target target;target.type=Impl::Target::Type::page;target.page=tab.page;
        const auto id=s.target("page:"+std::to_string(static_cast<int>(tab.page)),target);
        const auto& definition=*std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return p.id==tab.page;});
        if(!first)body<<',';
        first=false;
        body<<"{\"id\":"<<identifier(id)<<",\"label\":"<<json_string(definition.title)
            <<",\"selected\":"<<(s.app.page()==tab.page?"true":"false")<<'}';
    }
    body<<"],\"controls\":[";first=true;
    for(const auto& c:ui::console_screen()) {
        if(c.document_only||(!c.persistent&&c.page!=s.app.page())||!s.app.control(c).visible)continue;
        if(!first)body<<',';
        first=false;s.control(body,c);
    }
    body<<"],\"document\":";
    const auto page=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return p.id==s.app.page();});
    if(page!=ui::pages().end()&&page->document) {
        const auto document=s.app.document(s.app.page(),s.width);
        ui::DocumentPresentation presentation;presentation.reset(document);
        if(presentation.root())s.document(body,*presentation.root());else body<<"null";
    } else body<<"null";
    const auto overlay=s.app.overlay();const auto layers=s.app.overlay_layers(s.service_id!=0);
    body<<",\"layers\":{\"showBackground\":"<<(layers.show_background?"true":"false")
        <<",\"enableBackground\":"<<(layers.enable_background?"true":"false")
        <<",\"showOverlay\":"<<(layers.show_overlay?"true":"false")
        <<",\"enableOverlay\":"<<(layers.enable_overlay?"true":"false")<<"},\"overlay\":[";
    if(overlay) {first=true;for(const auto& c:overlay->controls) {
        if(!s.app.control(c).visible)continue;
        if(!first)body<<',';
        first=false;s.control(body,c,true);
    }}
    body<<"],\"service\":";
    if(const auto request=service(s.service_id);request&&layers.present_services) {
        body<<"{\"id\":"<<identifier(s.service_id)<<",\"kind\":"<<json_string(service_name(request->kind))
            <<",\"title\":"<<json_string(request->title)<<",\"value\":"<<json_string(request->value)
            <<",\"byteLimit\":"<<request->byte_limit<<'}';
    } else body<<"null";
    body<<'}';s.identities=std::move(s.retained);s.signature=s.current_signature();s.initialized=true;
    std::string result="{\"version\":"+std::to_string(protocol_version)+",\"generation\":"+identifier(s.epoch)+body.str();
    if(result.size()>maximum_snapshot)throw std::length_error("web snapshot exceeds bounded message size");
    return result;
}
Result Bridge::accept(const Event& e) {
    auto& s=*impl_;
    const auto reject=[](const char* error){return Result{false,error};};
    if(e.version!=protocol_version)return reject("Web protocol version mismatch");
    if(e.value.size()>maximum_text||e.error.size()>4096)return reject("Web event exceeds input limit");
    if(!s.initialized)return reject("A full snapshot is required before input");
    if(s.current_signature()!=s.signature) {++s.epoch;s.initialized=false;return reject("Presentation changed; obtain a fresh snapshot");}
    if(e.generation!=s.epoch)return reject("Stale web presentation generation");
    if(e.sequence==0||e.sequence<=s.sequence)return reject("Duplicate or out-of-order web event");
    // Consume every admitted envelope once, including invalid operations. A
    // rejected action cannot later become a valid replay under the same number.
    s.sequence=e.sequence;
    if(e.kind==EventKind::close) {s.app.close();return {true,{}};}
    if(e.kind==EventKind::key) {
        if(e.amount<0||e.amount>static_cast<int>(ui::Key::del))return reject("Unknown key primitive");
        return {s.app.overlay_key({key(e.amount),e.ctrl,e.shift,e.alt},s.service_id!=0),{}};
    }
    if(e.kind==EventKind::service) {
        const auto request=service(e.target);
        if(!request)return reject("Unknown or revoked web service");
        ui::ServiceResult result{request->id,e.cancelled,e.value,e.error,e.track_completion};
        if(!s.services.complete(result))return reject("Web service is no longer active");
        s.service_id=0;s.app.set_service_active(false);s.app.complete_service(std::move(result));return {true,{}};
    }
    const auto found=s.targets.find(e.target);
    if(found==s.targets.end())return reject("Unknown web control identifier");
    auto target=found->second;
    if(!target.document_key.empty()) {
        const auto current=s.current_document_target(target);
        if(!current||!current->enabled)return reject("Document declaration was withdrawn or disabled");
        target=*current;
    }
    if(s.service_id)return reject("A platform service is active");
    if(target.type==Impl::Target::Type::page) {
        if(e.kind!=EventKind::navigate)return reject("Input kind does not match navigation");
        if(!s.app.overlay_layers().enable_background)return reject("Background navigation is blocked");
        s.app.navigate(target.page);return {true,{}};
    }
    if(target.type==Impl::Target::Type::action) {
        if(e.kind!=EventKind::activate||!target.enabled||!s.app.enabled(target.action)||!s.app.overlay_layers().enable_background)
            return reject("Document action is unavailable");
        s.app.dispatch(target.action);return {true,{}};
    }
    const auto& c=target.control;const auto p=s.app.control(c);const auto& state=p.state;
    const auto layers=s.app.overlay_layers();
    if(!p.enabled||!p.visible||(c.surface?!layers.enable_overlay:!layers.enable_background))return reject("Web control is unavailable");
    const auto option=[&]{return std::any_of(state.options.begin(),state.options.end(),[&](const auto& v){return v.id==e.value&&v.enabled;});};
    switch(e.kind) {
    case EventKind::edit:
        if(c.kind!=ui::Kind::text||c.read_only)return reject("Control does not accept text edits");
        if(const auto error=ui::edit_error(e.value,c.multiline,c.byte_limit);!error.empty())return {false,error};
        s.app.edit(c,e.value);break;
    case EventKind::select:
        if(c.kind!=ui::Kind::choice&&c.kind!=ui::Kind::list)return reject("Control does not select");
        if(!option()&&!std::any_of(state.records.begin(),state.records.end(),[&](const auto& r){return r.id==e.value&&r.enabled;}))
            return reject("Unknown or disabled option");
        s.app.select(c,e.value);break;
    case EventKind::preset:
        if(c.kind!=ui::Kind::text||c.read_only||!option())return reject("Unknown or disabled preset");
        s.app.preset(c,e.value);break;
    case EventKind::toggle:
        if(c.kind!=ui::Kind::toggle)return reject("Control is not a toggle");
        s.app.toggle(c,e.checked);break;
    case EventKind::activate:
        if(c.kind!=ui::Kind::action)return reject("Control is not an action");
        s.app.activate(c);break;
    case EventKind::submit:
        if(c.kind!=ui::Kind::text||!s.app.submit_gesture(c,e.ctrl,e.shift))return reject("Control does not submit this gesture");
        s.app.submit(c,e.ctrl,e.shift);break;
    case EventKind::record: {
        if(c.kind!=ui::Kind::list)return reject("Control is not a record list");
        const auto record=std::find_if(state.records.begin(),state.records.end(),[&](const auto& v){return v.id==e.value;});
        if(record==state.records.end()||!record->enabled||!record->activatable)return reject("Record is unavailable");
        s.app.select(c,e.value);s.app.activate_record(c,e.value);break;
    }
    case EventKind::click:case EventKind::double_click:case EventKind::wheel: {
        if(c.kind!=ui::Kind::bitmap)return reject("Control does not accept bitmap gestures");
        if(e.kind==EventKind::wheel) {
            if(e.amount==0||e.amount < -4||e.amount>4)return reject("Invalid wheel amount");
            const auto commands=ui::ControlInteractions::wheel(c,e.amount);
            if(!commands)return reject("Gesture is unavailable");
            commands.dispatch([&](auto command){s.app.gesture(c,command);});
        } else {
            const auto command=e.kind==EventKind::click?c.click:c.double_click;
            if(command==ui::Command::none)return reject("Gesture is unavailable");
            s.app.gesture(c,command);
        }
        break;
    }
    default:return reject("Unknown or mismatched web event kind");
    }
    return {true,{}};
}
}
