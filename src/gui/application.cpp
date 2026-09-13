#include "application.hpp"
#include "gui_smoke.hpp"
#include "inspection_page.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace datapump::gui {
using Clock=std::chrono::steady_clock;
struct Application::Impl {
    Clock::time_point next=Clock::now(),next_presentation=next,started=next,completed=next;
    std::unique_ptr<Smoke> smoke;
    bool started_session=false,passed=false;
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
Application::Application(Launch options):controller({options.simulation||options.smoke,options.smoke}),launch(std::move(options)),impl_(std::make_unique<Impl>()) {
    impl_->page=launch.page;
}
Application::~Application()=default;
void Application::start() {
    if(impl_->started_session)return;
    impl_->started_session=true;
    impl_->next=impl_->next_presentation=impl_->started=Clock::now();
    if(launch.smoke)impl_->smoke=std::make_unique<Smoke>(launch.smoke_directory,launch.timeout);
    controller.start();bitmaps.update(controller);
}
bool Application::tick() {
    const auto now=Clock::now();
    if(now>=impl_->next) {
        impl_->next=now+std::chrono::milliseconds(40);
        controller.poll();bitmaps.update(controller);
        if(impl_->smoke) {
            impl_->smoke->step(controller, &bitmaps);
            if(!impl_->passed&&impl_->smoke->done()) {
                impl_->passed=true;impl_->completed=now;impl_->page=launch.page;
                if(launch.raw_view)controller.select(ui::Field::source,"binary");
                std::cout<<"Shared GUI smoke passed: keys, text, files, exact bits, cancellation, retained saves, live plots and page switching.\n";
            }
            if(!impl_->passed) {
                const auto& definitions=ui::pages();
                impl_->page=definitions[static_cast<std::size_t>(std::chrono::duration<double>(now-impl_->started).count())%definitions.size()].id;
            } else if(std::chrono::duration<double>(now-impl_->completed).count()>=launch.hold)controller.close();
        }
    }
    if(now<impl_->next_presentation)return false;
    impl_->next_presentation=now+std::chrono::milliseconds(100);
    return true;
}
bool Application::finished() const { return controller.ready_to_close(); }
int Application::result() const { return launch.smoke&&!impl_->passed?1:0; }
void Application::close() { controller.close(); }
void Application::select_page(ui::Page page) {
    if(std::any_of(ui::pages().begin(),ui::pages().end(),[&](const auto& value){return value.id==page;}))impl_->page=page;
}
ui::Page Application::page() const { return impl_->page; }
bool Application::smoke_passed() const { return impl_->passed; }
bool Application::submit(const ui::Control& control,bool ctrl,bool shift) {
    if(control.submit==ui::Command::none||shift)return false;
    const bool wants_ctrl=control.submit_mode!=ui::Field::count&&controller.field(control.submit_mode).selected=="ctrl-enter";
    if(ctrl!=wants_ctrl)return false;
    if(controller.enabled(control.submit))controller.activate(control.submit);
    return true; // Consume the declared submit gesture even when unavailable.
}
void Application::activate_record(const ui::Control& control,const std::string& id) {
    if(control.activate_record==ui::Command::none||control.field==ui::Field::count)return;
    const auto& state=controller.field(control.field);
    const auto found=std::find_if(state.records.begin(),state.records.end(),[&](const auto& record){return record.id==id;});
    if(!state.enabled||found==state.records.end()||!found->enabled||!found->activatable)return;
    controller.select(control.field,id);
    if(controller.field(control.field).selected==id&&controller.enabled(control.activate_record))controller.activate(control.activate_record);
}
BitmapPresentation Application::bitmap(const ui::Control& control,unsigned width) const {
    BitmapPresentation view;view.source=bitmaps.get(control.bitmap);view.revision=bitmaps.version(control.bitmap);
    view.title=bitmaps.title(control.bitmap);
    if(view.title.empty())view.title=control.field==ui::Field::count?control.label:controller.field(control.field).text;
    if(control.bitmap_caption==ui::BitmapCaption::overlay_error) {
        view.caption=bitmaps.error(control.bitmap);
        view.caption_tone=controller.field(ui::Field::qr_brightness).selected=="normal"?ui::TextTone::inverse:ui::TextTone::muted;
    } else view.caption=bitmaps.caption(control.bitmap,width);
    return view;
}
std::shared_ptr<const ui::DocumentNode> Application::document(ui::Page page,int width) {
    const auto definition=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return p.id==page;});
    if(definition==ui::pages().end()||!definition->document)return {};
    width=std::max(220,width);
    auto& cached=impl_->documents[page];
    const auto model=controller.inspection();
    const auto first=controller.pattern_first();
    const auto pending=controller.field(ui::Field::inspection).text;
    if(!cached.root||cached.model!=model||cached.first!=first||cached.width!=width||cached.closing!=controller.closing()||(!model&&cached.pending!=pending)) {
        auto document=inspection_page::build(model.get(),page==ui::Page::flow,static_cast<float>(width),first,pending);
        if(page==ui::Page::flow&&model&&model->pattern_space)controller.pattern_page_size(document.page_size);
        std::function<void(ui::DocumentNode&)> enable=[&](auto& node) {
            if(node.kind==ui::DocumentKind::action)node.enabled=controller.enabled(node.command);
            for(auto& child:node.children)enable(child);
        };
        enable(document.root);
        cached={model,std::make_shared<const ui::DocumentNode>(std::move(document.root)),controller.pattern_first(),width,controller.closing(),pending};
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
