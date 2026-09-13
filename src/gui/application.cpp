#include "application.hpp"
#include "controller.hpp"
#include "bitmap_sources.hpp"
#include "gui_smoke.hpp"
#include "inspection_page.hpp"
#include "text_policy.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>

namespace datapump::gui {
using Clock=std::chrono::steady_clock;
struct Application::Impl {
    explicit Impl(const Launch& launch):controller({launch.simulation||launch.smoke,launch.smoke}) {}
    Controller controller;
    BitmapSources bitmaps;
    Clock::time_point next=Clock::now(),next_presentation=next,started=next,completed=next;
    std::unique_ptr<Smoke> smoke;
    bool started_session=false,passed=false;
    std::uint64_t poll_count=0;
    ui::Page page=ui::Page::console;
    struct Document {
        std::shared_ptr<const Inspection> model;
        std::shared_ptr<const ui::DocumentNode> root;
        std::size_t first=0;
        int width=0;
        bool closing=false;
        std::string pending;
    };
    std::map<ui::Page,Document> documents;
};
Application::Application(Launch options):launch(std::move(options)),impl_(std::make_unique<Impl>(launch)) {
    impl_->page=launch.page;
}
Application::~Application()=default;
void Application::start() {
    if(impl_->started_session)return;
    impl_->started_session=true;
    impl_->next=impl_->next_presentation=impl_->started=Clock::now();
    if(launch.smoke)impl_->smoke=std::make_unique<Smoke>(launch.smoke_directory,launch.timeout);
    impl_->controller.start();impl_->bitmaps.update(impl_->controller);
}
bool Application::tick() {
    const auto now=Clock::now();
    if(now>=impl_->next) {
        impl_->next=now+std::chrono::milliseconds(40);
        impl_->controller.poll();impl_->bitmaps.update(impl_->controller);
        ++impl_->poll_count;
        if(impl_->smoke) {
            impl_->smoke->step(impl_->controller, &impl_->bitmaps);
            if(!impl_->passed&&impl_->smoke->done()) {
                impl_->passed=true;impl_->completed=now;impl_->page=launch.page;
                if(launch.raw_view)impl_->controller.select(ui::Field::source,"binary");
                std::cout<<"Shared GUI smoke passed: keys, text, files, exact bits, cancellation, retained saves, live plots and page switching.\n";
            }
            if(!impl_->passed) {
                const auto& definitions=ui::pages();
                impl_->page=definitions[static_cast<std::size_t>(std::chrono::duration<double>(now-impl_->started).count())%definitions.size()].id;
            } else if(std::chrono::duration<double>(now-impl_->completed).count()>=launch.hold)impl_->controller.close();
        }
    }
    if(now<impl_->next_presentation)return false;
    impl_->next_presentation=now+std::chrono::milliseconds(100);
    return true;
}
bool Application::finished() const { return impl_->controller.ready_to_close(); }
int Application::result() const { return launch.smoke&&!impl_->passed?1:0; }
void Application::close() { impl_->controller.close(); }
bool Application::closing() const { return impl_->controller.closing(); }
void Application::edit(ui::Field field,std::string text) { impl_->controller.edit(field,std::move(text)); }
void Application::edit(const ui::Control& declaration,std::string text) {
    const auto view=control(declaration);
    if(declaration.field==ui::Field::count||!view.enabled||!view.visible)return;
    if(const auto problem=ui::edit_error(declaration,text);!problem.empty()) {report_error(problem);return;}
    edit(declaration.field,std::move(text));
}
void Application::preset(const ui::Control& declaration,const std::string& id) {
    const auto& options=control(declaration).state.options;
    const auto found=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.id==id&&option.enabled;});
    if(found!=options.end())edit(declaration,found->id);
}
void Application::select(ui::Field field,std::string id) { impl_->controller.select(field,std::move(id)); }
void Application::toggle(ui::Field field,bool value) { impl_->controller.toggle(field,value); }
void Application::activate(ui::Command command) { impl_->controller.activate(command); }
void Application::activate(const ui::Control& declaration) {
    const auto view=control(declaration);
    if(view.enabled&&view.visible)activate(declaration.command);
}
ControlPresentation Application::control(const ui::Control& declaration) const {
    static const ui::FieldState empty;
    const auto& state=declaration.field==ui::Field::count?empty:field(declaration.field);
    ControlPresentation view{state,declaration.label,state.enabled,state.visible};
    if(declaration.kind==ui::Kind::label&&declaration.field!=ui::Field::count)view.label=state.text;
    if(declaration.kind==ui::Kind::action) {
        const auto current=command_label(declaration.command);
        if(!current.empty())view.label=current;
        view.enabled=view.enabled&&enabled(declaration.command);
    }
    return view;
}
MenuPresentation Application::menu(std::span<const ui::Control* const> items) const {
    MenuPresentation view;
    for(std::size_t i=0;i<items.size();++i) {
        const auto item=control(*items[i]);
        if(!item.visible)continue;
        view.visible=true;view.enabled=view.enabled||item.enabled;
        view.options.push_back({std::to_string(i),item.label,item.enabled});
    }
    return view;
}
void Application::select_menu(std::span<const ui::Control* const> items,const std::string& id) {
    for(std::size_t i=0;i<items.size();++i)if(id==std::to_string(i)) {activate(*items[i]);return;}
}
const ui::FieldState& Application::field(ui::Field field) const { return impl_->controller.field(field); }
bool Application::enabled(ui::Command command) const { return impl_->controller.enabled(command); }
std::string Application::command_label(ui::Command command) const { return impl_->controller.command_label(command); }
void Application::complete_service(ui::ServiceResult result) { impl_->controller.complete_service(std::move(result)); }
std::vector<ui::ServiceRequest> Application::take_services() { return impl_->controller.take_services(); }
void Application::report_error(std::string message) { impl_->controller.report_error(std::move(message)); }
std::uint64_t Application::revision() const { return impl_->controller.revision(); }
std::uint64_t Application::poll_count() const { return impl_->poll_count; }
void Application::select_page(ui::Page page) {
    if(std::any_of(ui::pages().begin(),ui::pages().end(),[&](const auto& value){return value.id==page;}))impl_->page=page;
}
ui::Page Application::page() const { return impl_->page; }
bool Application::smoke_passed() const { return impl_->passed; }
bool Application::submit(const ui::Control& control,bool ctrl,bool shift) {
    if(control.submit==ui::Command::none||shift)return false;
    const bool wants_ctrl=control.submit_mode!=ui::Field::count&&impl_->controller.field(control.submit_mode).selected=="ctrl-enter";
    if(ctrl!=wants_ctrl)return false;
    if(impl_->controller.enabled(control.submit))impl_->controller.activate(control.submit);
    return true; // Consume the declared submit gesture even when unavailable.
}
void Application::activate_record(const ui::Control& control,const std::string& id) {
    if(control.activate_record==ui::Command::none||control.field==ui::Field::count)return;
    const auto& state=impl_->controller.field(control.field);
    const auto found=std::find_if(state.records.begin(),state.records.end(),[&](const auto& record){return record.id==id;});
    if(!state.enabled||found==state.records.end()||!found->enabled||!found->activatable)return;
    impl_->controller.select(control.field,id);
    if(impl_->controller.field(control.field).selected==id&&impl_->controller.enabled(control.activate_record))impl_->controller.activate(control.activate_record);
}
BitmapPresentation Application::bitmap(const ui::Control& control,unsigned width) const {
    BitmapPresentation view;view.source=impl_->bitmaps.get(control.bitmap);view.revision=impl_->bitmaps.version(control.bitmap);
    view.title=impl_->bitmaps.title(control.bitmap);
    if(view.title.empty())view.title=control.field==ui::Field::count?control.label:impl_->controller.field(control.field).text;
    if(control.bitmap_caption==ui::BitmapCaption::overlay_error) {
        view.caption=impl_->bitmaps.error(control.bitmap);
        view.caption_tone=impl_->controller.field(ui::Field::qr_brightness).selected=="normal"?ui::TextTone::inverse:ui::TextTone::muted;
    } else view.caption=impl_->bitmaps.caption(control.bitmap,width);
    return view;
}
std::shared_ptr<const ui::DocumentNode> Application::document(ui::Page page,int width) {
    const auto definition=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return p.id==page;});
    if(definition==ui::pages().end()||!definition->document)return {};
    width=std::max(220,width);
    auto& cached=impl_->documents[page];
    const auto model=impl_->controller.inspection();
    const auto first=impl_->controller.pattern_first();
    const auto pending=impl_->controller.field(ui::Field::inspection).text;
    if(!cached.root||cached.model!=model||cached.first!=first||cached.width!=width||cached.closing!=impl_->controller.closing()||(!model&&cached.pending!=pending)) {
        auto document=inspection_page::build(model.get(),page==ui::Page::flow,static_cast<float>(width),first,pending);
        if(page==ui::Page::flow&&model&&model->pattern_space)impl_->controller.pattern_page_size(document.page_size);
        std::function<void(ui::DocumentNode&)> enable=[&](auto& node) {
            if(node.kind==ui::DocumentKind::action)node.enabled=impl_->controller.enabled(node.command);
            for(auto& child:node.children)enable(child);
        };
        enable(document.root);
        cached={model,std::make_shared<const ui::DocumentNode>(std::move(document.root)),impl_->controller.pattern_first(),width,impl_->controller.closing(),pending};
    }
    return cached.root;
}
void gui_self_check() {
    controller_self_check();
    Message message;const std::string text="Data Pump shared GUI self-check";message.data=Bytes(text.begin(),text.end());
    transfer::Options options;options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;modem::ChannelConfig channel;channel.snr_db=18;channel.delay_samples=137;
    const auto result=transfer::simulate(message,options,channel);
    if(result.packet.message.data!=message.data)throw Error("Shared GUI transfer self-check failed");
    BitmapImage bitmap(137,101);const auto plot=plots::PlotSnapshot::qr(encode_qr(text),plots::QrBrightness::normal);
    plot.paint(full_bitmap_request(137,101,false,true),[&](unsigned x,unsigned y,PixelBlock block){bitmap.blit(x,y,block);});
    std::cout<<"Data Pump shared GUI self-check passed; no display required.\n";
}
int gui_main(int argc,char** argv,const char* backend,const std::function<int(Launch)>& run) {
    try {
        Launch launch;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--help") {std::cout<<"Data Pump continuous console\nGUI backend: "<<backend<<" (selected at build time)\nUsage: datapump-gui [--color|--monochrome] [--simulation] [--self-check] [--smoke-test]\nSmoke options: --smoke-dir PATH --smoke-hold SECONDS --smoke-timeout SECONDS --smoke-view NAME --smoke-raw-view --smoke-scroll 0..1\n";return 0;}
            if(arg=="--version") {std::cout<<"Data Pump "<<DATAPUMP_VERSION<<" GUI backend: "<<backend<<'\n';return 0;}
            if(arg=="--self-check") {gui_self_check();return 0;}
            if(arg=="--color")launch.color=true;
            else if(arg=="--monochrome")launch.color=false;
            else if(arg=="--simulation")launch.simulation=true;
            else if(arg=="--smoke-test")launch.smoke=true;
            else if(arg=="--smoke-raw-view")launch.raw_view=true;
            else if(arg=="--smoke-dir"&&i+1<argc)launch.smoke_directory=std::filesystem::u8path(argv[++i]);
            else if((arg=="--smoke-hold"||arg=="--smoke-timeout"||arg=="--smoke-scroll")&&i+1<argc) {
                const std::string value=argv[++i];std::size_t used=0;const double number=std::stod(value,&used);
                if(used!=value.size()||!std::isfinite(number)||number<0)throw Error("Invalid smoke argument");
                if(arg=="--smoke-hold"&&number<=60)launch.hold=number;
                else if(arg=="--smoke-timeout"&&number>=10&&number<=600)launch.timeout=number;
                else if(arg=="--smoke-scroll"&&number<=1)launch.scroll=number;
                else throw Error("Smoke argument out of range");
            } else if(arg=="--smoke-view"&&i+1<argc) {
                const std::string name=argv[++i];
                const auto page=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return name==p.name;});
                if(page==ui::pages().end())throw Error("Unknown smoke view");
                launch.page=page->id;
            } else throw Error("Unknown or incomplete option: "+arg);
        }
        return run(launch);
    }catch(const std::exception& error){std::cerr<<"Data Pump "<<backend<<": "<<error.what()<<'\n';return 1;}
}
}
