#include "../src/gui/framebuffer.hpp"
#include "../src/gui/framebuffer_ui.hpp"
#include "../src/gui/theme.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump::gui;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Callable>void rejects(Callable callable,const char* message) {
    bool rejected=false;try{callable();}catch(const std::exception&){rejected=true;}require(rejected,message);
}
surface::Primitive image(ui::Rect bounds,BitmapSource source) {
    surface::Primitive result;result.kind=surface::Primitive::Kind::bitmap;result.bounds=bounds;result.bitmap=std::move(source);return result;
}
std::array<unsigned,4> at(const framebuffer::Frame& frame,unsigned x,unsigned y) {
    const auto* p=frame.pixels.data()+frame.stride_bytes*y+x*4;return {p[0],p[1],p[2],p[3]};
}
void render_and_lifetime() {
    framebuffer::FrameHandle retained;
    {
        framebuffer::Renderer renderer({24,18,1,true});surface::Scene scene;scene.width=24;scene.height=18;
        scene.primitives.push_back(image({2,3,3,2},BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool color) {
            require(request.supports_rgb24&&color,"RGB capability lost");
            const std::array<unsigned char,12> pixels{255,0,0,0,255,0,0,0,255,77,77,77};
            sink(0,0,{3,1,12,PixelFormat::rgb24,pixels.data()});
            const std::array<unsigned char,4> gray{12,100,240,77};sink(0,1,{3,1,4,PixelFormat::gray8,gray.data()});
        })));
        retained=renderer.render(scene);
        require(retained->width==24&&retained->height==18&&retained->stride_bytes==96,"frame geometry");
        require(retained->damage.size()==1&&retained->damage[0].height==18,"initial damage");
        require(at(*retained,2,3)==std::array<unsigned,4>{255,0,0,255},"RGB pixel");
        require(at(*retained,3,4)==std::array<unsigned,4>{100,100,100,255},"padded grayscale pixel");
        require(at(*retained,1,3)==std::array<unsigned,4>{0,0,0,255},"outside bitmap changed");
        require(renderer.render(scene)==retained,"unchanged scene must retain revision");
        scene.caret=ui::Rect{0,7,1,1};const auto changed=renderer.render(scene);
        require(changed->revision==retained->revision+1&&changed->damage.size()==1&&changed->damage[0].y==7&&changed->damage[0].height==1,"precise row damage");
        require(at(*retained,0,7)[0]==0,"retained frame mutated");
        scene.width=25;const auto resized=renderer.render(scene);
        require(resized->damage.size()==1&&resized->damage[0].width==25&&resized->damage[0].height==18,"resize damage");
    }
    require(at(*retained,2,3)[0]==255,"frame lifetime depends on renderer");
}
void clipping_and_mono() {
    framebuffer::Renderer renderer({8,8,1,false});surface::Scene scene;scene.width=8;scene.height=8;
    scene.primitives.push_back(image({-2,-1,5,3},BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool color) {
        require(!color&&!request.supports_rgb24,"monochrome capability");
        require(request.damage.x==2&&request.damage.y==1&&request.damage.width==3&&request.damage.height==2,"clipped bitmap request");
        const unsigned char row=0xa8;for(unsigned y=0;y<3;++y)sink(0,y,{5,1,1,PixelFormat::mono1,&row});
    })));
    auto frame=renderer.render(scene);
    require(at(*frame,0,0)[0]==255&&at(*frame,1,0)[0]==0&&at(*frame,2,0)[0]==255,"MSB first mono / clipping");
    require(at(*frame,3,0)[0]==0&&at(*frame,0,2)[0]==0,"bitmap escaped bounds");
    scene.primitives.clear();surface::Primitive label;label.bounds={-1,-1,5,5};label.text="TEXT";label.border=true;scene.primitives.push_back(label);
    renderer.render(scene);
    scene.primitives[0].bounds={std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),100,100};renderer.render(scene);
    scene.primitives.clear();scene.primitives.push_back(image({0,0,2,2},BitmapSource([](const BitmapRequest&,const BitmapSink& sink,bool) {
        const unsigned char data=0;sink(2,0,{1,1,1,PixelFormat::gray8,&data});
    })));
    rejects([&]{renderer.render(scene);},"out-of-grid producer accepted");
    require(renderer.frame()!=nullptr,"failed render destroyed existing frame");
}
void copy_formats() {
    framebuffer::Frame frame;frame.width=2;frame.height=2;frame.stride_bytes=12;
    frame.pixels={255,0,0,255,0,255,0,255,1,1,1,1,0,0,255,255,255,255,255,255,2,2,2,2};
    std::vector<std::uint8_t> output(24,0xa5);
    framebuffer::Surface target{2,2,12,framebuffer::Format::bgra8888,output};
    framebuffer::copy_frame(frame,target,{1,0,1,2});
    require(output[0]==0xa5&&output[8]==0xa5&&output[20]==0xa5,"copy changed padding / outside damage");
    require(output[4]==0&&output[5]==255&&output[6]==0&&output[7]==255,"BGRA copy");
    require(output[16]==255&&output[17]==255&&output[18]==255,"BGRA row stride");
    framebuffer::copy_frame(frame,target);
    require(output[0]==0&&output[1]==0&&output[2]==255,"BGRA channel order");
    output.assign(12,0xa5);target={2,2,6,framebuffer::Format::rgb565le,output};framebuffer::copy_frame(frame,target);
    require(output[0]==0&&output[1]==0xf8&&output[2]==0xe0&&output[3]==7,"RGB565 little endian");
    require(output[6]==31&&output[7]==0&&output[4]==0xa5&&output[10]==0xa5,"RGB565 padding/blue");
    const auto unchanged=output;target.stride_bytes=1;
    rejects([&]{framebuffer::copy_frame(frame,target);},"undersized stride accepted");require(output==unchanged,"failed validation changed output");
    target.stride_bytes=6;rejects([&]{framebuffer::copy_frame(frame,target,{2,0,1,1});},"out-of-bounds copy accepted");
    target.stride_bytes=std::numeric_limits<std::size_t>::max();rejects([&]{framebuffer::copy_frame(frame,target);},"overflow stride accepted");
    target.stride_bytes=6;target.pixels=std::span(output).first(5);rejects([&]{framebuffer::copy_frame(frame,target);},"undersized storage accepted");
}
void glyphs() {
    for(unsigned scale=1;scale<=4;++scale) {
        framebuffer::Renderer renderer({1600,40,scale,false});const auto metrics=renderer.metrics();
        surface::Scene scene;scene.width=metrics.cell_width*95;scene.height=metrics.line_height;
        surface::Primitive text;text.bounds={0,0,scene.width,scene.height};
        for(char c=32;c<127;++c)text.text+=c;
        scene.primitives.push_back(text);const auto frame=renderer.render(scene);
        bool antialiased=false;
        for(int glyph_index=1;glyph_index<95;++glyph_index) {
            bool visible=false;
            for(int y=0;y<metrics.line_height;++y)for(int x=0;x<metrics.cell_width;++x) {
                const auto pixel=at(*frame,static_cast<unsigned>(glyph_index*metrics.cell_width+x),static_cast<unsigned>(y));
                visible|=pixel[0]!=0;antialiased|=pixel[0]>0&&pixel[0]<theme::text;
                require(pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&pixel[3]==255,"font grayscale/opacity lost");
            }
            require(visible,"printable ASCII glyph missing");
        }
        require(antialiased,"font lost antialiased coverage");
        if(scale==2)require(metrics.cell_width==8&&metrics.line_height==18,"default native-sized font metrics changed");
        scene.primitives[0].clip=ui::Rect{3,2,17,5};const auto clipped=renderer.render(scene);
        for(int y=0;y<scene.height;++y)for(int x=0;x<scene.width;++x)
            if(x<3||x>=20||y<2||y>=7)require(at(*clipped,static_cast<unsigned>(x),static_cast<unsigned>(y))[0]==0,"font escaped clip");
    }
    rejects([]{framebuffer::Renderer bad({0,5,1,true});},"zero width accepted");
    rejects([]{framebuffer::Renderer bad({8192,8192,1,true});},"allocation cap bypassed");
    rejects([]{framebuffer::Renderer bad({12,12,5,true});},"invalid font scale accepted");
}
void widget_raster() {
    framebuffer::Renderer renderer({40,20,2,false});surface::Scene scene;scene.width=40;scene.height=20;
    surface::Primitive base;base.kind=surface::Primitive::Kind::fill;base.bounds={0,0,40,20};base.fill=surface::Fill::hover;
    scene.primitives.push_back(base);
    surface::Primitive arrow;arrow.kind=surface::Primitive::Kind::icon;arrow.bounds={0,0,20,20};arrow.icon=surface::Icon::chevron_down;
    scene.primitives.push_back(arrow);arrow.bounds.x=20;arrow.icon=surface::Icon::check;scene.primitives.push_back(arrow);
    const auto frame=renderer.render(scene);const auto background=theme::widget_rgb(theme::WidgetRole::hover).red;
    for(unsigned origin:{0U,20U}) {
        unsigned changed=0;bool blended=false;
        for(unsigned y=0;y<20;++y)for(unsigned x=origin;x<origin+20;++x) {
            const auto value=at(*frame,x,y)[0];changed+=value!=background;blended|=value>background&&value<theme::text;
        }
        require(changed>15&&changed<130&&blended,"widget icon missing or not antialiased");
    }
    scene.primitives.resize(1);scene.primitives[0].enabled=false;scene.primitives[0].border=true;scene.primitives[0].focused=true;
    const auto disabled=renderer.render(scene);
    require(at(*disabled,0,0)[0]==theme::widget_rgb(theme::WidgetRole::disabled_border).red,"disabled border ignored");
    require(at(*disabled,1,1)[0]==theme::widget_rgb(theme::WidgetRole::disabled_background).red,"disabled fill ignored");
}
const ui::Control& declaration(ui::Field field) {
    for(const auto& value:ui::console_screen())if(value.field==field)return value;
    throw std::runtime_error("Missing framebuffer test declaration");
}
void send(framebuffer::Session& session,surface::Event event){session.input(event);session.tick();}
void click(framebuffer::Session& session,int x,int y,surface::Event::Type type=surface::Event::Type::pointer) {
    surface::Event event;event.type=type;event.x=x;event.y=y;send(session,std::move(event));
}
void key(framebuffer::Session& session,surface::Key value,bool alt=false) {surface::Event event;event.key=value;event.alt=alt;send(session,event);}
void input(framebuffer::Session& session,std::string text,bool paste=false) {surface::Event event;event.type=surface::Event::Type::text;event.text=std::move(text);event.paste=paste;send(session,std::move(event));}
void select_all(framebuffer::Session& session) {surface::Event event;event.ctrl=true;event.text="a";send(session,event);}
void pixel_interaction() {
    Application app({});auto edit=declaration(ui::Field::fast_text);edit.label="Pixel editor";edit.byte_limit=32;edit.placement={20,40,0,0,300,76};
    auto toggle=declaration(ui::Field::developer_mode);toggle.label="Pixel toggle";toggle.placement={350,40,0,0,200,28};
    ui::OverlayDefinition overlay;overlay.controls={edit,toggle};app.show_overlay(std::move(overlay));
    framebuffer::Session session(app);session.resize({1200,1048,{6,18}});session.tick();
    click(session,24,44);input(session,"abcdef");require(app.field(edit.field).text=="abcdef","pixel editor input");
    click(session,30,44);click(session,48,44,surface::Event::Type::pointer_move);click(session,48,44,surface::Event::Type::pointer_up);
    input(session,"Z");require(app.field(edit.field).text=="aZef","pixel drag selection did not replace selected bytes");
    select_all(session);input(session,"A\xc3\xa9");key(session,surface::Key::backspace);require(app.field(edit.field).text=="A","pixel editor split UTF-8");
    input(session,std::string("x\0y",3),true);require(app.field(edit.field).text=="A","pixel editor accepted NUL paste");
    input(session,std::string(33,'x'),true);require(app.field(edit.field).text=="A","pixel editor bypassed byte limit");
    select_all(session);surface::Event rejected;rejected.type=surface::Event::Type::input_rejected;rejected.text="Paste rejected; draft unchanged";send(session,rejected);
    require(app.field(edit.field).text=="A","rejected pixel input changed draft");
    require(std::any_of(session.scene().primitives.begin(),session.scene().primitives.end(),[&](const auto& p){return p.text==rejected.text;}),"rejected pixel input lacked visible reason");
    input(session,"B");require(app.field(edit.field).text=="B","rejected pixel input discarded selection");select_all(session);input(session,"A");
    key(session,surface::Key::space);input(session," ");require(app.field(edit.field).text=="A ","SDL key/text Space inserted twice");
    click(session,360,48);require(app.field(toggle.field).checked,"pixel toggle click");
    click(session,-10,-10);require(app.field(toggle.field).checked,"blank click activated previous focus");
    key(session,surface::Key::space);input(session," ");require(!app.field(toggle.field).checked,"SDL key/text Space toggled twice");
    session.resize({640,480,{6,18}});session.tick();require(session.scene().width==640&&session.scene().height==480,"pixel resize lost dimensions");
    click(session,24,44);select_all(session);input(session,"resize preserved focus");require(app.field(edit.field).text=="resize preserved focus","pixel resize broke hit coordinates");
    app.edit(edit.field,"restricted");select_all(session);input(session,"current");require(app.field(edit.field).text=="current","authoritative editor refresh failed");
    app.close();
}
void pixel_presets() {
    Application app({});app.select(ui::Field::fast_mode,"robust");auto edit=declaration(ui::Field::carrier);edit.label="Pixel preset";edit.placement={20,40,0,0,300,30};
    ui::OverlayDefinition overlay;overlay.controls={edit};app.show_overlay(std::move(overlay));
    framebuffer::Session session(app);session.resize({1200,1048,{6,18}});session.tick();click(session,24,44);select_all(session);input(session,"1234");
    key(session,surface::Key::down,true);key(session,surface::Key::down);key(session,surface::Key::escape);
    require(app.field(edit.field).text=="1234","cancelled pixel preset discarded draft");
    key(session,surface::Key::down,true);const auto wanted=app.field(edit.field).options.front().id;key(session,surface::Key::enter);
    require(app.field(edit.field).text==wanted,"pixel preset acceptance failed");
    app.close();
}
void minimal_embedding() {
    framebuffer::FrameHandle first,second;
    {
        Launch launch;launch.simulation=true;
        framebuffer::Runtime runtime(launch,{320,240,1,true});
        require(!runtime.finished(),"unstarted embedding reported finished");
        first=runtime.update(320,240);
        require(first&&first->width==320&&first->height==240&&first->pixels.size()==320*240*4,"minimal update did not return complete surface");
        surface::Event help;help.key=surface::Key::help;
        const std::array<surface::Event,1> events{help};second=runtime.update(640,480,events);
        require(second&&second->width==640&&second->height==480,"minimal update resize failed");
        require(second->revision>first->revision&&first->pixels.size()==320*240*4,"minimal update mutated old frame");
        runtime.close();
    }
    require(first->pixels.size()==320*240*4&&second->pixels.size()==640*480*4,"runtime destruction invalidated frame handles");
}
void overlay_focus_policy() {
    Application app({});framebuffer::Session session(app);session.resize({1200,1048,{6,18}});session.tick();
    const auto& c=declaration(ui::Field::fast_text);const auto rect=app.control_layout(c,1200,1026).widget;
    click(session,rect.x+4,rect.y+3);input(session,"base");
    auto overlay_control=c;overlay_control.placement={20,40,0,0,300,60};
    ui::OverlayDefinition overlay;overlay.controls={overlay_control};overlay.policy.restore_focus=false;app.show_overlay(overlay);
    require(session.tick(),"external overlay was not presented on next tick");
    app.dismiss_overlay();require(session.tick(),"external overlay dismissal was not presented on next tick");input(session,"x");require(app.field(c.field).text=="base","non-restoring overlay restored old focus");
    click(session,rect.x+4,rect.y+3);key(session,surface::Key::end);
    overlay.policy.restore_focus=true;app.show_overlay(overlay);session.tick();
    click(session,24,44);key(session,surface::Key::home);
    // Replacing an overlay must keep the original desktop focus and caret.
    app.show_overlay(overlay);session.tick();app.dismiss_overlay();session.tick();input(session,"x");
    require(app.field(c.field).text=="basex","replaced overlay lost original desktop focus or caret");app.close();
}
void popup_live_options() {
    Application app({});app.toggle(ui::Field::developer_mode,true);
    auto choice=declaration(ui::Field::fast_coding);choice.placement={20,40,0,0,300,30};
    ui::OverlayDefinition overlay;overlay.controls={choice};app.show_overlay(overlay);
    framebuffer::Session session(app);session.resize({1200,1048,{6,18}});session.tick();click(session,24,44);
    key(session,surface::Key::down); // three-quarters, initially index one
    app.select(ui::Field::fast_profile,"wire");session.tick();
    require(std::any_of(session.scene().primitives.begin(),session.scene().primitives.end(),[](const auto& p){return p.text=="LDPC 2/3";}),"open pixel popup did not refresh options");
    key(session,surface::Key::enter);
    require(app.field(choice.field).selected=="three-quarters","popup refresh lost selected option identity");app.close();
}
void overlay_keyboard_scope() {
    Application app({});app.select(ui::Field::fast_mode,"robust");
    framebuffer::Session session(app);session.resize({1200,1048,{6,18}});session.tick();
    key(session,surface::Key::tab);key(session,surface::Key::enter);
    require(app.page()==ui::Page::console,"initial Tab skipped the first page tab");
    auto toggle=declaration(ui::Field::developer_mode);toggle.placement={20,40,0,0,200,28};
    ui::OverlayDefinition overlay;overlay.controls={toggle};overlay.policy.hide_background=false;overlay.policy.block_background=true;
    app.show_overlay(overlay);session.tick();
    key(session,surface::Key::tab);key(session,surface::Key::space);
    require(app.field(toggle.field).checked,"visible blocked background tab captured overlay focus");app.close();
}
void dropdown_chrome() {
    Application app({});app.toggle(ui::Field::developer_mode,true);
    auto choice=declaration(ui::Field::fast_coding);choice.label="Long dropdown label";
    choice.placement={20,100,0,0,80,30};choice.open_upward=true;
    const auto options=app.field(choice.field).options;require(options.size()>=2,"dropdown fixture has too few options");
    app.select(choice.field,options.front().id);
    ui::OverlayDefinition overlay;overlay.controls={choice};app.show_overlay(overlay);
    framebuffer::Session session(app);session.resize({320,240,{8,18}});session.tick();
    const auto& closed=session.scene().primitives;
    require(std::any_of(closed.begin(),closed.end(),[](const auto& p){return p.kind==surface::Primitive::Kind::icon&&p.icon==surface::Icon::chevron_down&&p.bounds.y>=100&&p.bounds.y+p.bounds.h<=130;}),"dropdown has no independent arrow chrome");
    require(std::any_of(closed.begin(),closed.end(),[](const auto& p){return p.kind==surface::Primitive::Kind::text&&p.bounds.y>=100&&p.bounds.y+p.bounds.h<=130&&p.text.ends_with("...");}),"narrow dropdown did not shorten its value on one baseline");
    click(session,24,104);
    const auto option=std::find_if(session.scene().primitives.begin(),session.scene().primitives.end(),[&](const auto& p){return p.text==options[1].label;});
    require(option!=session.scene().primitives.end(),"popup width cropped an available option label");
    require(option->bounds.y+option->bounds.h<100,"upward dropdown ignored its declared direction");
    const auto second=option->bounds;
    click(session,second.x+2,second.y+2,surface::Event::Type::pointer_move);
    require(app.field(choice.field).selected==options.front().id,"hover committed dropdown value");
    key(session,surface::Key::enter);require(app.field(choice.field).selected==options[1].id,"popup hover did not select the visible option");
    click(session,24,104);session.resize({80,65,{8,18}});session.tick();
    const auto panel=std::find_if(session.scene().primitives.rbegin(),session.scene().primitives.rend(),[](const auto& p){return p.kind==surface::Primitive::Kind::fill&&p.border;});
    require(panel!=session.scene().primitives.rend()&&panel->bounds.x>=0&&panel->bounds.y>=0&&panel->bounds.x+panel->bounds.w<=80&&panel->bounds.y+panel->bounds.h<=65,"resized dropdown panel escaped framebuffer");
    key(session,surface::Key::enter);require(app.field(choice.field).selected==options[1].id,"resized popup lost selected option identity");
    app.close();
}

}
int main() {
    try{render_and_lifetime();clipping_and_mono();copy_formats();glyphs();widget_raster();pixel_interaction();pixel_presets();minimal_embedding();overlay_focus_policy();popup_live_options();overlay_keyboard_scope();dropdown_chrome();std::cout<<"Framebuffer pixel, damage, ownership, clipping, format and interaction checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
