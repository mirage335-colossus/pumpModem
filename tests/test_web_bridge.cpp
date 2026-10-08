#include "web_bridge.hpp"
#include "web_pixels.hpp"
#include "plot_render.hpp"
#include "document_presentation.hpp"
#include "datapump/transfer.hpp"
#include <array>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <thread>

using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::uint64_t id(const std::string& json,const std::string& label) {
    const auto label_at=json.find("\"label\":\""+label+'"');
    check(label_at!=std::string::npos,"Missing generic declaration label");
    const auto begin=json.rfind("{\"id\":\"",label_at);
    check(begin!=std::string::npos,"Missing opaque declaration ID");
    return std::stoull(json.substr(begin+7));
}
web::Event event(const web::Bridge& bridge,web::EventKind kind,std::uint64_t target,std::uint64_t sequence) {
    web::Event value;value.generation=bridge.generation();value.sequence=sequence;value.kind=kind;value.target=target;return value;
}
void pixel_runs() {
    using web::detail::rgb_runs;
    check(rgb_runs({1,2,3}).empty(),"One pixel must use raw fallback");
    check(rgb_runs({1,2,3,4,5,6,7,8,9,10,11,12}).empty(),"Distinct pixels expanded");
    check(rgb_runs({1,2,3,1,2,3,4,5,6,7,8,9}).empty(),"Equal-size encoding must use raw");
    for(const auto count:{2U,255U,256U,257U,640U*320U}) {
        std::vector<unsigned char> pixels;
        for(unsigned i=0;i<count;++i)pixels.insert(pixels.end(),{0,127,255});
        std::vector<unsigned char> expected;
        for(unsigned remaining=count;remaining;) {
            const auto n=std::min(remaining,256U);expected.insert(expected.end(),{static_cast<unsigned char>(n-1),0,127,255});remaining-=n;
        }
        check(rgb_runs(pixels)==expected,"Bounded RGB run vector differs");
    }
}
void bitmap_history() {
    // The HTML backend must preserve the native producer's startup time
    // window. Fitting a partially filled history stretches its first rows.
    for(const unsigned count:{0U,1U,2U,40U,160U}) {
        plots::SpectrumHistory history;
        for(unsigned row=0;row<count;++row)history.push(std::array<double,2>{0,0},20);
        const BitmapSource source=plots::PlotSnapshot::waterfall(history);
        for(const auto& [width,height]:{std::pair{8,4},std::pair{6,160},std::pair{9,320},std::pair{1200,600}}) {
            const auto actual=web::detail::render_bitmap(source,width,height);
            check(actual.width()==static_cast<unsigned>(std::min(width,640))&&
                  actual.height()==static_cast<unsigned>(std::min(height,320)),"Web bitmap raster bounds changed");
            BitmapImage native(actual.width(),actual.height(),PixelFormat::rgb24);
            source.paint(full_bitmap_request(native.width(),native.height(),false,true),
                         [&](unsigned x,unsigned y,PixelBlock block){native.blit(x,y,block);});
            check(actual.pixels()==native.pixels(),"Web waterfall differs from native startup/history fitting");
            const auto visible=std::min<std::size_t>(actual.height(),plots::SpectrumHistory::capacity);
            for(unsigned y=0;y<actual.height();++y) {
                const bool missing=count<visible-static_cast<std::size_t>(y)*visible/actual.height();
                const auto begin=actual.pixels().begin()+static_cast<std::size_t>(y)*actual.width()*3;
                const bool blank=std::all_of(begin,begin+actual.width()*3,[](auto value){return value==0;});
                check(blank==missing,"Startup waterfall invented history or lost a captured row");
            }
        }
    }
    const auto single=web::detail::render_bitmap({},-1,0);
    check(single.width()==1&&single.height()==1,"Web bitmap lower raster bound changed");
}
void envelopes_and_edits() {
    Application app({});web::Bridge bridge(app);
    auto e=event(bridge,web::EventKind::edit,1,1);e.value="x";
    check(!bridge.accept(e).accepted,"Input before snapshot must fail");
    auto snapshot=bridge.snapshot(360,640);const auto message=id(snapshot,"Message");
    e=event(bridge,web::EventKind::edit,message,1);e.version=99;
    check(!bridge.accept(e).accepted,"Protocol mismatch accepted");e.version=web::protocol_version;
    e.value="<script>literal & text</script>";
    check(bridge.accept(e).accepted,"Valid UTF-8 edit rejected");
    check(app.field(ui::Field::fast_text).text==e.value,"Bridge altered locally entered source bytes");
    check(!bridge.accept(e).accepted,"Duplicate edit accepted");
    snapshot=bridge.snapshot();
    check(snapshot.find("\\u003cscript\\u003e")!=std::string::npos&&snapshot.find("<script>")==std::string::npos,"HTML-significant text not encoded literally");
    check(id(snapshot,"Message")==message,"Stable control identity changed on edit");
    check(snapshot.find("\"submitCtrlEnter\":true")!=std::string::npos,"Shared submit policy omitted");
    e=event(bridge,web::EventKind::edit,message,2);e.value=std::string("a\0b",3);
    check(!bridge.accept(e).accepted,"Embedded NUL edit accepted");
    e.sequence=3;e.value="\xc0\xaf";check(!bridge.accept(e).accepted,"Malformed UTF-8 accepted");
    e.sequence=4;e.value=std::string(1024*1024+1,'a');check(!bridge.accept(e).accepted,"Oversized input accepted");
    e=event(bridge,web::EventKind::activate,message,5);check(!bridge.accept(e).accepted,"Wrong primitive activated editor");
    e=event(bridge,web::EventKind::edit,999999,6);check(!bridge.accept(e).accepted,"Unknown ID accepted");
    const auto old=event(bridge,web::EventKind::edit,message,7);bridge.reconnect();bridge.snapshot();
    check(!bridge.accept(old).accepted,"Reconnect accepted old callback");app.close();
}
void layers_and_services() {
    Application app({});web::Bridge bridge(app);auto snapshot=bridge.snapshot();
    const auto message=id(snapshot,"Message");const auto old=event(bridge,web::EventKind::edit,message,1);
    ui::OverlayDefinition overlay;
    ui::Control editor{};editor.kind=ui::Kind::text;editor.field=ui::Field::fast_text;editor.scope=ui::ScreenScope::fast;
    editor.label="Extension editor";editor.multiline=true;
    overlay.controls.push_back(editor);app.show_overlay(std::move(overlay));
    check(!bridge.accept(old).accepted,"Unpublished overlay accepted background callback");
    snapshot=bridge.snapshot();check(snapshot.find("Extension editor")!=std::string::npos,"New shared primitive requires frontend feature registration");
    auto edit=event(bridge,web::EventKind::edit,id(snapshot,"Extension editor"),2);edit.value="extension";
    check(bridge.accept(edit).accepted,"Declared overlay editor unavailable");
    edit=event(bridge,web::EventKind::edit,message,3);edit.value="background";
    check(!bridge.accept(edit).accepted,"Overlay allowed background edit");
    app.dismiss_overlay();bridge.snapshot();app.toggle(ui::Field::fast_encryption,true);app.activate(ui::Command::fast_open_key);
    snapshot=bridge.snapshot();const auto service_at=snapshot.find("\"service\":{\"id\":\"");
    check(service_at!=std::string::npos,"File request omitted");
    const auto service=std::stoull(snapshot.substr(service_at+17));
    check(bridge.service(service).has_value(),"Trusted adapter cannot inspect active service");
    auto result=event(bridge,web::EventKind::service,service,4);result.cancelled=true;
    check(bridge.accept(result).accepted,"File cancellation rejected");
    check(!bridge.service(service).has_value(),"Completed service remains authorized");
    check(!bridge.accept(result).accepted,"Duplicate service completion accepted");app.close();
}
void document_withdrawal() {
    Launch launch;launch.page=ui::Page::planner;
    Application app(launch);web::Bridge bridge(app);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!app.enabled(ui::Command::planner_apply_short)&&std::chrono::steady_clock::now()<deadline) {
        app.tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(app.enabled(ui::Command::planner_apply_short),"Planner worker did not prepare the apply fixture");
    const auto snapshot=bridge.snapshot(1000,760);
    const auto text=snapshot.find("\"text\":\"Use target for short messages\"");
    check(text!=std::string::npos,"Planner fixture requires an available apply action");
    const auto begin=snapshot.find(",\"id\":\"",text);
    check(begin!=std::string::npos,"Document action has no opaque identity");
    const auto target=std::stoull(snapshot.substr(begin+7));
    // The previously rendered action disappears before another snapshot. Its
    // local document eligibility must be checked independently of old handles.
    app.edit(ui::Field::planner_target,"invalid target");
    const auto result=bridge.accept(event(bridge,web::EventKind::activate,target,1));
    check(!result.accepted&&result.error.find("Document declaration")!=std::string::npos,
          "Withdrawn document action retained stale eligibility");app.close();
}
void service_completion_routing() {
    Application app({});web::Bridge bridge(app);bridge.snapshot();
    app.toggle(ui::Field::fast_encryption,true);app.activate(ui::Command::fast_generate_key);
    const auto snapshot=bridge.snapshot();const auto at=snapshot.find("\"service\":{\"id\":\"");
    check(at!=std::string::npos,"Key generation save request omitted");
    const auto target=std::stoull(snapshot.substr(at+17));const auto request=bridge.service(target);
    check(request&&request->kind==ui::ServiceKind::save_file,"Expected a save capability");
    auto result=event(bridge,web::EventKind::service,target,1);
    result.track_completion=true;result.error="Destination capability denied";
    check(bridge.accept(result).accepted,"Failed authorized save result was not routed");
    const auto completed=app.take_service_completions();
    check(completed.size()==1&&completed[0].id==request->id&&completed[0].error==result.error,
          "Completion lost global request identity or failure outcome");
    check(app.take_service_completions().empty(),"Completion was replayed");
    app.enable_service_completions(false);app.activate(ui::Command::fast_generate_key);
    auto requests=app.take_services();check(requests.size()==1,"Native save request omitted");
    ui::ServiceResult native{requests[0].id,false,{},"Destination unavailable",true};
    app.complete_service(std::move(native));
    check(app.take_service_completions().empty(),"Unsubscribed native host retained completion events");app.close();
}
void shared_submit_policy() {
    Application app({});
    for(const auto& c:ui::console_screen()) {
        if(c.field==ui::Field::fast_text) {
            check(!app.submit_gesture(c,false,false)&&app.submit_gesture(c,true,false)&&!app.submit_gesture(c,true,true),"Fast text shared submit policy changed");
        }
        if(c.read_only)check(!app.submit_gesture(c,false,false)&&!app.submit_gesture(c,true,false),"Read-only control can submit");
    }
    app.close();
}
std::string rect(ui::Rect value) {
    std::ostringstream out;out<<"{\"x\":"<<value.x<<",\"y\":"<<value.y<<",\"w\":"<<value.w<<",\"h\":"<<value.h<<'}';return out.str();
}
void shared_geometry_and_stable_snapshots() {
    Launch launch;launch.simulation=true;Application app(launch);web::Bridge bridge(app);
    const auto small=bridge.snapshot(360,640);
    check(small.find("\"layout\":{\"width\":"+std::to_string(ui::min_width)+",\"height\":"+std::to_string(ui::min_height))!=std::string::npos,
          "Small browser viewport must preserve shared minimum desktop extent");
    check(small==bridge.snapshot(360,640),"Unchanged polling repainted bitmaps or advanced presentation revision");
    const int width=ui::default_width,height=ui::default_height;
    const auto snapshot=bridge.snapshot(width,height);
    check(snapshot.find("\"page\":"+rect(app.page_bounds(width,height)))!=std::string::npos,"Shared page rectangle omitted");
    for(const auto& group:ui::control_groups(ui::console_screen())) {
        const auto& c=*group.control;
        if(c.document_only||(!c.persistent&&c.page!=app.page())||
           (group.menu_items.empty()?!app.control(c).visible:!app.menu(group.menu_items).visible))continue;
        const auto geometry=app.control_layout(c,width,height);
        check(snapshot.find("\"geometry\":{\"frame\":"+rect(geometry.frame)+",\"widget\":"+rect(geometry.widget))!=std::string::npos,
              "Browser geometry differs from shared native control geometry");
        if(!group.menu_items.empty())check(snapshot.find("\"kind\":\"menu\",\"label\":\""+std::string(c.menu_label)+'"')!=std::string::npos,
              "Shared action group was expanded instead of rendered as a menu");
    }
    for(const auto& tab:app.tab_layout(width,height))if(tab.visible)
        check(snapshot.find("\"frame\":"+rect(tab.frame))!=std::string::npos,"Shared tab geometry omitted");
    check(snapshot==bridge.snapshot(width,height),"Full unchanged snapshot is not stable");
    app.select(ui::Field::fast_mode,"fast");const auto fast=bridge.snapshot(width,height);
    const auto message=id(fast,"Message");auto edit=event(bridge,web::EventKind::edit,message,1);edit.value="layout";
    check(bridge.accept(edit).accepted,"Geometry-aware snapshot lost editable target");
    const auto updated=bridge.snapshot(width,height);
    check(updated!=snapshot&&updated.find("\"ack\":\"1\"")!=std::string::npos,"Actual edit/ack was suppressed by snapshot cache");
    check(updated==bridge.snapshot(width,height),"Edited presentation remains unstable after acknowledgment");
    app.close();
}
void measured_document_geometry() {
    Launch launch;launch.page=ui::Page::planner;Application app(launch);web::Bridge bridge(app);
    constexpr int width=1200,height=1000;
    auto snapshot=bridge.snapshot(width,height);
    const std::regex request("\\\"measure\\\":\\{\\\"id\\\":\\\"([0-9]+)\\\",\\\"width\\\":([0-9]+),\\\"height\\\":(-?[0-9]+)\\}");
    std::map<std::uint64_t,int> requests;
    for(auto i=std::sregex_iterator(snapshot.begin(),snapshot.end(),request);i!=std::sregex_iterator();++i)
        requests.emplace(std::stoull((*i)[1]),std::stoi((*i)[2]));
    check(!requests.empty()&&requests.size()<=512,"Document did not request bounded browser glyph measurements");
    std::uint64_t sequence=1;
    auto measured=event(bridge,web::EventKind::measure,0,sequence++);
    const auto first=requests.begin()->first;
    for(const auto value:{std::to_string(first)+" -1\n",std::to_string(first)+" 16385\n",std::to_string(first)+" 37",std::to_string(first)+" 37\n999999 37\n",std::to_string(first)+" 37\n"+std::to_string(first)+" 37\n"}) {
        measured.sequence=sequence++;measured.value=value;
        check(!bridge.accept(measured).accepted,"Malformed, duplicate or unknown measurement was accepted");
    }
    measured.sequence=sequence++;measured.value.clear();
    for(const auto& [target,available]:requests){(void)available;measured.value+=std::to_string(target)+" 37\n";}
    check(bridge.accept(measured).accepted,"Browser text measurement batch was rejected");
    snapshot=bridge.snapshot(width,height);
    ui::DocumentPresentation presentation;
    const int content_width=ui::document_content_width(app.page_bounds(width,height).w,16);
    presentation.reset(app.document(app.page(),content_width));
    const auto geometry=presentation.layout(content_width,[](const auto&,int){return 37;});
    for(const auto& item:geometry.nodes) {
        const auto r=item.relative,c=item.content;
        const auto expected="\"geometry\":{\"frame\":"+rect({r.x,r.y,r.width,r.height})+",\"content\":"+rect({c.x,c.y,c.width,c.height});
        check(snapshot.find(expected)!=std::string::npos,"Web document diverged from the shared measured C++ layout");
    }
    check(snapshot==bridge.snapshot(width,height),"Measured document geometry did not stabilize");
    bridge.snapshot(width+400,height);
    measured.sequence=sequence++;measured.value=std::to_string(first)+" 44\n";
    // The first auto-width paragraph changes its measurement identity on resize.
    check(!bridge.accept(measured).accepted,"Resized document accepted a retired glyph-width measurement");
    bridge.reconnect();bridge.snapshot(width,height);measured.sequence=sequence++;
    check(!bridge.accept(measured).accepted,"Reconnected document accepted an old measurement generation");
    app.close();
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--snapshot") {Application app({});web::Bridge bridge(app);std::cout<<bridge.snapshot(360,640);app.close();return 0;}
        pixel_runs();bitmap_history();envelopes_and_edits();layers_and_services();document_withdrawal();service_completion_routing();shared_submit_policy();shared_geometry_and_stable_snapshots();measured_document_geometry();
        std::cout<<"web bridge envelopes, literal text, declarations, generations, layers and services passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
