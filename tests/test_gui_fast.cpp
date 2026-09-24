#include "application.hpp"
#include "fast/controller.hpp"
#include "fast/presentation.hpp"
#include "fast/plots.hpp"
#include "fast/screen.hpp"
#include "datapump/types.hpp"
#include "datapump/fast/codec.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
const ui::Control& control(ui::Field field) {
    for(const auto& c:ui::console_screen())if(c.field==field)return c;
    throw Error("Missing shared fast field");
}
const ui::Control& action(ui::Command command) {
    for(const auto& c:ui::console_screen())if(c.command==command)return c;
    throw Error("Missing shared fast action");
}
void presentation_and_retention() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    const auto& mode=control(F::fast_mode);
    check(mode.kind==ui::Kind::choice&&mode.persistent&&mode.scope==ui::ScreenScope::shared&&app.field(F::fast_mode).selected=="robust",
          "Modem choice must default to Robust Modem in the shared header");
    check(app.field(F::fast_mode).options.size()==3&&app.field(F::fast_mode).options[0].label=="Robust Modem"&&
        app.field(F::fast_mode).options[1].label=="Fast Modem","Modem selector labels changed");
    app.toggle(F::developer_mode,true);app.select_page(ui::Page::compression);
    app.edit(F::binary,"001");const auto bits=app.field(F::binary).text;
    app.select(F::fec,"off");const auto fec=app.field(F::fec).selected;
    app.select(mode,"fast");
    check(app.field(F::fast_mode).selected=="fast","Fast Modem choice was not accepted");
    check(!app.field(F::fast_mono).checked,"Cable must default to both output channels");
    for(const auto& c:ui::console_screen()) {
        if(c.scope==ui::ScreenScope::regular||c.scope==ui::ScreenScope::legacy)check(!app.control(c).visible,"Other modem controls leaked into the fast interface");
        else check(app.control(c).visible==(c.field!=F::fast_file),"Fast interface failed to show the selected source controls");
    }
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))check(!tab.visible,"Regular tabs leaked into fast interface");
    const auto acoustic=fast::profile(fast::Channel::acoustic);
    check(app.field(F::fast_profile).options.size()==5+static_cast<unsigned>(acoustic.capacity_mode)&&app.field(F::fast_constellation).options.size()==11,
          "Fast channel/constellation selections are incomplete");
    check(!app.field(F::fast_encryption).checked&&app.enabled(C::fast_listen)&&!app.enabled(C::fast_transmit),"Fast defaults must allow plain reception and require nonempty transmit text");
    check(!app.field(F::fast_key).enabled&&!app.enabled(C::fast_open_key)&&!app.enabled(C::fast_generate_key),"Plain mode retained active key controls");
    check(app.field(F::fast_source).selected=="text"&&control(F::fast_text).multiline&&control(F::fast_text).byte_limit==fast::text_byte_limit,
          "Fast text composer must be the default and bound UTF-8 input to 32768 bytes");
    const std::string fast_text="Fast UTF-8 café\nSecond line";
    app.edit(control(F::fast_text),fast_text);
    check(app.enabled(C::fast_transmit)&&app.command_label(C::fast_transmit)=="Transmit text","Plain text source did not enable source-aware transmit");
    app.toggle(F::fast_encryption,true);
    check(!app.enabled(C::fast_listen)&&!app.enabled(C::fast_transmit)&&app.enabled(C::fast_open_key),"Encrypted Fast allowed a missing key or disabled key loading");
    check(app.field(F::fast_auth).text=="Receive integrity · no received stream yet","Unused receiver was labeled with an encryption result before any transfer");
    app.activate(C::fast_listen);check(!app.enabled(C::fast_cancel),"Missing encryption key silently started plain reception");
    app.toggle(F::fast_encryption,false);
    app.edit(control(F::binary),"111");app.select(control(F::fec),"rs60");
    app.edit(F::message,"stale");app.dispatch(C::use_text);app.navigate(ui::Page::planner);
    check(app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,"Inactive regular callbacks mutated retained settings");
    app.select(control(F::fast_profile),"ssb");app.select(control(F::fast_constellation),"256");
    check(app.field(F::fast_mono).checked,"Radio output routing default changed");
    app.toggle(F::fast_mono,false);check(!app.field(F::fast_mono).checked,"Explicit radio stereo override ignored");
    app.select(control(F::fast_source),"file");
    app.edit(control(F::fast_file),"/tmp/independent-source.bin");
    check(!app.control(control(F::fast_text)).visible&&app.control(action(C::fast_choose_file)).visible&&app.command_label(C::fast_transmit)=="Transmit file",
          "File source did not replace the composer and transmit label");
    app.edit(F::fast_text,"stale hidden source edit");
    app.select(F::fast_source,"text");
    check(app.field(F::fast_text).text==fast_text&&app.field(F::fast_file).text=="/tmp/independent-source.bin","Source switching changed independent drafts");
    app.edit(F::fast_text,std::string(fast::text_byte_limit,'x'));
    check(app.field(F::fast_text).text.size()==fast::text_byte_limit,"Fast text rejected its inclusive byte limit");
    app.edit(F::fast_text,std::string(fast::text_byte_limit+1,'x'));
    app.edit(F::fast_text,std::string("\xc3",1));app.edit(F::fast_text,std::string("a\0b",3));
    check(app.field(F::fast_text).text.size()==fast::text_byte_limit,"Invalid or oversized UTF-8 edit replaced the retained draft");
    app.edit(F::fast_text,fast_text);
    check(app.field(F::fast_profile).selected=="ssb"&&app.field(F::fast_constellation).selected=="256","Fast profile did not retain local selections");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
        for(const auto& c:ui::console_screen())if(c.scope==ui::ScreenScope::fast) {
            const auto geometry=app.control_layout(c,size.w,size.h);const auto r=geometry.frame;
            check(r.x>=0&&r.y>=0&&r.w>0&&r.h>0&&r.x+r.w<=size.w&&r.y+r.h<=size.h,"Fast layout escaped desktop bounds");
            if(c.kind==ui::Kind::bitmap)check(geometry.widget.w>=300&&geometry.widget.h>=140&&geometry.has_caption,
                "Fast live plots lost useful dimensions or their metadata captions");
        }
        const auto title=ui::DesktopLayout(size.w,size.h)[ui::Slot::header];
        const auto toggle=app.control_layout(mode,size.w,size.h).frame;
        check(toggle.x>=title.x+title.w&&toggle.x<title.x+title.w+24,"Modem selector is not adjacent to DATA PUMP");
    }
    app.select(mode,"robust");
    check(app.page()==ui::Page::compression&&app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,
          "Returning to regular mode changed its selected page or exact draft");
    app.edit(control(F::fast_file),"/tmp/stale.bin");app.select(control(F::fast_profile),"wire");app.activate(action(C::fast_choose_file));
    check(app.field(F::fast_file).text=="/tmp/independent-source.bin"&&app.field(F::fast_profile).selected=="ssb"&&app.take_services().empty(),
          "Hidden fast callbacks changed state or opened native services");
    app.select(mode,"fast");check(app.field(F::fast_constellation).selected=="256"&&app.field(F::fast_text).text==fast_text&&app.field(F::fast_source).selected=="text",
        "Fast settings or source draft were discarded on mode switch");
    check(app.field(F::fast_source_detail).text.find("Estimated")!=std::string::npos,"Fast draft has no airtime estimate");
    check(app.field(F::fast_detail).text.find("Shannon-Hartley")!=std::string::npos,"Fast capacity explanation missing");
    app.select(F::fast_profile,"acoustic");
    check(app.field(F::fast_mono).checked,"Acoustic profile must default to the right speaker only");
    check(app.field(F::fast_detail).text.starts_with("Next: acoustic"),"Acoustic GUI selection silently used cable identity");
    if(acoustic.capacity_mode) {
        if(acoustic.acoustic_ofdm)check(app.field(F::fast_detail).text.find("OFDM")!=std::string::npos&&
            app.field(F::fast_detail).text.find("ms echo guard")!=std::string::npos&&
            app.field(F::fast_detail).text.find("ms blocks")!=std::string::npos&&
            app.field(F::fast_detail).text.find("symbols/s")==std::string::npos,
            "OFDM detail used inactive single-carrier symbol-rate fields");
        check(app.field(F::fast_constellation).selected=="16"&&app.field(F::fast_depth).selected=="8"&&
            app.field(F::fast_coding).selected=="three-quarters"&&app.field(F::fast_fec).selected=="sparse",
            "Acoustic 16-QAM, LDPC3/4, depth8 defaults were not applied to GUI");
        app.select(F::fast_constellation,"64");app.toggle(F::fast_mono,false);
        check(app.field(F::fast_constellation).selected=="64"&&!app.field(F::fast_mono).checked,
            "Explicit acoustic 64-QAM and both-speaker output remain unavailable");
        app.select(F::fast_profile,"acoustic");
        check(app.field(F::fast_constellation).selected=="16"&&app.field(F::fast_mono).checked,
            "Reselecting acoustic did not restore its modulation and output defaults");
        app.select(F::fast_coding,"half");
        check(app.field(F::fast_coding).selected=="half","Acoustic LDPC1/2 option missing");
        app.select(F::fast_coding,"two-thirds");
        check(app.field(F::fast_coding).selected=="two-thirds","Acoustic LDPC2/3 option missing");
        app.select(F::fast_profile,"acoustic-classic");
        check(app.field(F::fast_profile).selected=="acoustic-classic"&&app.field(F::fast_constellation).selected=="4",
            "Classic acoustic fallback is not selectable");
        check(app.field(F::fast_mono).checked,"Classic acoustic fallback did not restore right-only routing");
    }
    check(app.field(F::fast_depth).selected=="5"&&app.field(F::fast_coding).selected=="three-quarters"&&
        app.field(F::fast_fec).selected=="robust","Acoustic bulk defaults differ from modem profile");
    app.select(F::fast_profile,"wire");
    check(!app.field(F::fast_mono).checked,"Cable profile did not reset both-channel output routing");
    app.toggle(F::fast_mono,true);check(app.field(F::fast_mono).checked,"Explicit cable right-only override ignored");
    check(app.field(F::fast_constellation).selected=="4194304"&&app.field(F::fast_depth).selected=="4"&&
        app.field(F::fast_coding).selected=="eight-ninths"&&app.field(F::fast_fec).selected=="sparse",
        "Cable bulk defaults differ from modem profile");
    app.select(F::fast_constellation,"16384");app.select(F::fast_coding,"nine-tenths");app.select(F::fast_depth,"8");
    check(app.field(F::fast_constellation).selected=="16384"&&app.field(F::fast_coding).selected=="nine-tenths"&&
        app.field(F::fast_depth).selected=="8","Capacity QAM/LDPC choices ignored");
    app.select(F::fast_coding,"half");check(app.field(F::fast_coding).selected=="half","Capacity LDPC1/2 choice ignored");
    app.select(F::fast_profile,"wire-classic");
    check(app.field(F::fast_constellation).selected=="256"&&app.field(F::fast_coding).selected=="seven-eighths",
        "Classic cable profile did not restore its original format");
    app.select(F::fast_depth,"64");check(app.field(F::fast_depth).selected=="64","Fast cable depth choice ignored");
    app.select(F::fast_depth,"62");check(app.field(F::fast_depth).selected=="62","Fast optimized cable depth choice ignored");
    app.close();app.select(mode,"robust");check(app.field(F::fast_mode).selected=="fast","Closed application accepted mode callback");
}
void live_plot_presentation() {
    using P=fast_ui::FastPlots;using B=ui::Bitmap;
    P plots;const auto start=P::Clock::time_point{};
    auto data=std::make_shared<fast::Diagnostics>();data->stream_id=17;data->revision=1;data->samples=1024;
    data->sample_rate=48000;data->constellation=256;data->acquired=true;data->spectrum_valid=true;
    data->waveform_count=data->waveform.size();data->constellation_count=3;
    for(std::size_t i=0;i<data->waveform.size();++i)data->waveform[i]=static_cast<float>(.6*std::sin(i*.13));
    data->spectrum_db.fill(-100);data->spectrum_db[40]=-6;
    data->constellation_points[0]={0,0};data->constellation_points[1]={.28F,.73F};data->constellation_points[2]={-.62F,-.31F};
    check(plots.update(data,true,start)&&plots.history_size()==1,"Fast telemetry did not advance the live plots");
    check(plots.title(B::fast_constellation).find("RX equalized")!=std::string::npos&&plots.caption(B::fast_constellation).find("3 points")!=std::string::npos,
        "RX plot mislabeled actual equalized observations");
    check(plots.caption(B::fast_waveform).find("21.3 ms")!=std::string::npos&&plots.caption(B::fast_waterfall,310).find("120")!=std::string::npos,
        "Fast plot scale metadata did not use the captured sample rate and fixed dBFS scale");
    const auto paint=[](const BitmapSource& source,BitmapRequest request,bool preference=true) {
        BitmapImage image(request.width,request.height);source.paint(request,[&](unsigned x,unsigned y,PixelBlock block){image.blit(x,y,block);},preference);return image;
    };
    for(const auto id:{B::fast_waveform,B::fast_waterfall,B::fast_constellation}) {
        const auto source=plots.source(id);
        for(const auto format:{PixelFormat::gray8,PixelFormat::rgb24,PixelFormat::mono1}) {
            auto request=full_bitmap_request(177,91,format==PixelFormat::mono1,format==PixelFormat::rgb24);
            const auto full=paint(source,request);BitmapImage split(177,91);
            for(const auto damage:{PixelRect{0,0,63,37},PixelRect{63,0,114,37},PixelRect{0,37,63,54},PixelRect{63,37,114,54}}) {
                request.damage=damage;
                source.paint(request,[&](unsigned x,unsigned y,PixelBlock block) {
                    check(block.format==format&&block.height==1&&block.width<=request.width,"Fast source violated the requested row pixel format/bound");
                    split.blit(x,y,block);
                });
            }
            check(split.pixels()==full.pixels(),"Fast damage repaint changed the immutable full pixel grid");
        }
        bool gray=false;source.paint(full_bitmap_request(17,11,false,true),[&](unsigned,unsigned,PixelBlock block){gray=block.format==PixelFormat::gray8;},false);
        check(gray,"Fast plots ignored the shared color preference");
    }
    const auto original=plots.source(B::fast_constellation);
    const auto original_image=paint(original,full_bitmap_request(65,65));
    const auto middle=(32*65+32)*3;
    check(original_image.pixels()[middle]>180,"Fast constellation did not render the actual zero-I/Q observation");
    const auto old_revision=plots.revision();
    check(!plots.update(data,true,start+std::chrono::milliseconds(100))&&plots.revision()==old_revision&&plots.history_size()==1,
        "Polling an unchanged frame duplicated waterfall rows or invalidated native images");
    check(plots.update(data,true,start+std::chrono::seconds(3))&&plots.title(B::fast_waveform).find("Stalled")!=std::string::npos,
        "Stalled telemetry was still labeled live");
    check(plots.update(data,false,start+std::chrono::seconds(4))&&plots.title(B::fast_waterfall).find("Retained")!=std::string::npos,
        "Stopped telemetry was still labeled live");
    for(std::size_t i=0;i<P::history_capacity+10;++i) {
        auto next=std::make_shared<fast::Diagnostics>(*data);next->revision=i+2;next->samples=(i+2)*1024;
        next->transmitting=true;next->constellation_points[0]={.6F,.6F};
        plots.update(next,true,start+std::chrono::seconds(5)+std::chrono::milliseconds(i*100));
    }
    check(plots.history_size()==P::history_capacity&&plots.title(B::fast_constellation).find("TX constellation")!=std::string::npos,
        "Fast waterfall retention grew without bound or TX points were labeled received");
    check(paint(original,full_bitmap_request(65,65)).pixels()==original_image.pixels(),"Later telemetry mutated a retained native bitmap source");
    check(paint(plots.source(B::fast_constellation),full_bitmap_request(65,65)).pixels()[middle]<180,
        "Fast constellation substituted fixed reference points for changed observations");
    plots.reset();check(plots.history_size()==0&&plots.caption(B::fast_waveform).find("Waiting")!=std::string::npos,"New session retained old plot history");
    plots.update(data,false,start+std::chrono::seconds(20));check(plots.history_size()==0,"Delayed previous-session telemetry repopulated cleared plots");
    auto next=std::make_shared<fast::Diagnostics>(*data);next->stream_id=18;next->revision=0;next->waveform_count=0;next->constellation_count=0;next->spectrum_valid=false;
    plots.update(next,true,start+std::chrono::seconds(21));
    check(plots.history_size()==0&&plots.caption(B::fast_constellation).find("Awaiting")!=std::string::npos,
        "Unacquired new session fabricated spectrum or constellation observations");
    next=std::make_shared<fast::Diagnostics>(*data);next->stream_id=18;next->revision=1;
    plots.update(next,true,start+std::chrono::seconds(22));check(plots.history_size()==1,"New stream failed to start fresh waterfall history");
    next=std::make_shared<fast::Diagnostics>(*next);next->stream_id=19;
    plots.update(next,true,start+std::chrono::seconds(23));check(plots.history_size()==1,"Stream identity change mixed distinct waterfall histories");

    P acoustic_plots;
    auto acoustic=std::make_shared<fast::Diagnostics>(*data);
    acoustic->acoustic_ofdm=true;acoustic->constellation_sample=acoustic->samples;
    acoustic_plots.update(acoustic,true,start);
    check(acoustic_plots.caption(B::fast_constellation).find("sampled points")!=std::string::npos&&
          acoustic_plots.title(B::fast_constellation)=="Live RX equalized constellation",
          "OFDM plot did not identify its fresh representative sample");
    acoustic=std::make_shared<fast::Diagnostics>(*acoustic);
    ++acoustic->revision;acoustic->samples+=3*acoustic->sample_rate;
    acoustic_plots.update(acoustic,true,start+std::chrono::seconds(3));
    check(acoustic_plots.title(B::fast_constellation)=="Last RX equalized constellation"&&
          acoustic_plots.title(B::fast_waveform)=="Live RX waveform",
          "Fresh PCM falsely marked an unchanged OFDM constellation live or stopped audio");
    acoustic=std::make_shared<fast::Diagnostics>(*acoustic);
    ++acoustic->revision;acoustic->constellation_sample=acoustic->samples;
    acoustic_plots.update(acoustic,true,start+std::chrono::milliseconds(3100));
    check(acoustic_plots.title(B::fast_constellation)=="Live RX equalized constellation",
          "New OFDM points failed to restore constellation freshness");
    acoustic=std::make_shared<fast::Diagnostics>(*acoustic);
    ++acoustic->revision;acoustic->constellation_count=0;acoustic->input_count=1;
    acoustic->input_points[0]={.001F,.002F};acoustic->input_sample=0;
    acoustic_plots.update(acoustic,true,start+std::chrono::milliseconds(3200));
    check(acoustic_plots.title(B::fast_constellation)=="Last RX input I/Q"&&
          acoustic_plots.caption(B::fast_constellation).find("sampled")!=std::string::npos,
          "OFDM unsynchronized input history lacks sampling or freshness labels");
}
void service_generations() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    app.activate(C::attach_file);auto regular=app.take_services();
    check(regular.size()==1,"Regular file service unavailable");
    app.select(F::fast_mode,"fast");app.select(F::fast_source,"file");app.activate(C::fast_choose_file);auto fast=app.take_services();
    check(fast.size()==1&&fast.front().id!=regular.front().id,"Mode services reused a callback identity");
    app.complete_service({regular.front().id,false,"/tmp/stale-regular.bin",{}});
    app.complete_service({fast.front().id,false,"/tmp/fast-current.bin",{}});
    check(app.field(F::fast_file).text=="/tmp/fast-current.bin","Fast file service routed to the wrong controller");
    app.activate(C::fast_choose_file);auto old=app.take_services();check(old.size()==1,"Second fast service unavailable");
    app.select(F::fast_mode,"robust");app.select(F::fast_mode,"fast");
    app.complete_service({old.front().id,false,"/tmp/stale-fast.bin",{}});
    check(app.field(F::fast_file).text=="/tmp/fast-current.bin","Stale service survived a mode generation change");
    app.close();
}
void unsynchronized_plot_presentation() {
    using B=ui::Bitmap;
    fast_ui::FastPlots plots;
    auto data=std::make_shared<fast::Diagnostics>();data->stream_id=30;data->revision=1;
    data->sample_rate=48000;data->waveform_count=1024;data->waveform_rms=.001;data->waveform_peak=.002;
    data->input_count=3;
    data->input_points[0]={0,0};data->input_points[1]={.00002F,.00001F};data->input_points[2]={-.00001F,-.00002F};
    plots.update(data,true);
    check(plots.title(B::fast_constellation)=="Live RX input I/Q"&&
          plots.caption(B::fast_constellation).find("Unsynchronized · auto ±")!=std::string::npos,
          "Unacquired input was mislabeled as equalized payload symbols");
    check(plots.caption(B::fast_waveform).find("RMS -60 dBFS")!=std::string::npos,
          "Low-level captured audio has no calibrated amplitude readout");
    const auto paint=[](const BitmapSource& source) {
        BitmapImage image(65,65);
        source.paint(full_bitmap_request(65,65),[&](unsigned x,unsigned y,PixelBlock block){image.blit(x,y,block);});
        return image.pixels();
    };
    const auto retained=plots.source(B::fast_constellation);const auto weak=paint(retained);
    bool visible=false;
    for(int y=0;y<65;++y)for(int x=0;x<65;++x)
        if(std::abs(x-32)>8&&std::abs(y-32)>8&&weak[(y*65+x)*3]>180)visible=true;
    check(visible,"Real weak microphone I/Q collapsed into an unhelpful center dot");
    auto next=std::make_shared<fast::Diagnostics>(*data);++next->revision;
    for(auto& point:next->input_points)point*=1000;
    next->waveform_peak=1.;plots.update(next,true);
    check(paint(plots.source(B::fast_constellation))==weak,"Input display changed geometry with microphone gain alone");
    check(plots.caption(B::fast_waveform,310).find("CLIPPING")!=std::string::npos,"Full-scale audio has no clipping indication");
    next=std::make_shared<fast::Diagnostics>(*next);++next->revision;next->acquired=true;
    next->constellation_count=1;next->constellation_points[0]={.7F,.7F};next->input_points[0]={100,100};
    plots.update(next,true);
    check(plots.title(B::fast_constellation)=="Live RX equalized constellation"&&
          plots.caption(B::fast_constellation).find("1 points · I/Q ±1.5")!=std::string::npos,
          "Acquired payload plot mixed unsynchronized input or its automatic scale");
    check(paint(retained)==weak,"Acquisition mutated a retained unsynchronized input snapshot");
    next=std::make_shared<fast::Diagnostics>(*data);++next->revision;next->transmitting=true;
    plots.update(next,true);
    check(plots.title(B::fast_constellation)=="Live TX constellation"&&
          plots.caption(B::fast_constellation)=="Waiting for mapped payload symbols",
          "Transmitter displayed receiver input as mapped symbols");
}
void retained_key_and_result_presentation() {
    using F=ui::Field;using C=ui::Command;
    unsigned acquisitions=0;
    fast_ui::Controller controller([&] {++acquisitions;return false;});
    controller.toggle(F::fast_encryption,true);controller.activate(C::fast_listen);
    check(acquisitions==0,"Encrypted missing-key action reached audio acquisition");
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-gui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".key");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{path};
    create_keyring(path,{"GUI retained key"});
    controller.activate(C::fast_open_key);const auto request=controller.take_services();check(request.size()==1,"Encrypted key service unavailable");
    controller.complete_service({request.front().id,false,path.string(),{}});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.enabled(C::fast_listen)&&std::chrono::steady_clock::now()<end) {controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(controller.enabled(C::fast_listen),"Fast key did not load");
    const auto key=controller.field(F::fast_key).selected,key_path=controller.field(F::fast_key_path).text;
    controller.toggle(F::fast_encryption,false);
    check(controller.enabled(C::fast_listen)&&!controller.field(F::fast_key).enabled&&!controller.enabled(C::fast_generate_key),"Plain mode did not disable retained key controls");
    controller.toggle(F::fast_encryption,true);
    check(controller.enabled(C::fast_listen)&&controller.field(F::fast_key).selected==key&&controller.field(F::fast_key_path).text==key_path,"Encryption toggle discarded the loaded key");
    const auto profile=fast::profile(fast::Channel::wire);
    Bytes source(4200,'x');source[0]='h';source[1]='i';source[2]='\n';source[3]=0;source[4]=255;
    fast::StreamEncoder encoder(profile,std::nullopt,fast::byte_source(source));
    fast::StreamDecoder decoder(profile,std::nullopt);
    Bytes bits(fast::physical_interval_bits);std::vector<float> soft(bits.size());
    while(encoder.next_interval(bits)) {
        for(std::size_t i=0;i<bits.size();++i)soft[i]=bits[i]?10.0F:-10.0F;
        decoder.push_interval(soft);
    }
    decoder.finish(true);check(bool(decoder.result()),"Plain preview fixture failed to decode");
    fast::Snapshot snapshot;snapshot.file=decoder.result();snapshot.checksum_groups=decoder.snapshot().checksum_groups;
    snapshot.physical_complete=true;snapshot.complete=true;
    check(decoder.result()->size()==source.size(),"Raw binary receive size changed");
    for(const auto& control:fast_ui::screen())
        check(std::string_view(control.label).find("preview")==std::string::npos,"Fast source preview surface remains");
    const auto stage=fast_ui::transfer_stage(snapshot),integrity=fast_ui::integrity_label(snapshot);
    check(stage.find("checksum verified, unauthenticated")!=std::string::npos&&integrity.find("checksum-verified")!=std::string::npos,
          "Completed plain reception was mislabeled authenticated");
    controller.toggle(F::fast_encryption,false); // Result rendering has no settings dependency.
    check(fast_ui::transfer_stage(snapshot)==stage&&fast_ui::integrity_label(snapshot)==integrity,"Settings relabeled an existing reception");
    snapshot.encrypted=true;snapshot.authenticated=true;snapshot.authenticated_groups=2;snapshot.checksum_groups=0;
    check(fast_ui::transfer_stage(snapshot)=="RECEIVED · authenticated"&&fast_ui::integrity_label(snapshot).find("2 authenticated groups")!=std::string::npos,
          "Encrypted completion lost its captured authentication result");
    controller.close();
}
void regular_work_keeps_polling() {
    using F=ui::Field;using C=ui::Command;
    // Use the ordinary simulation pipeline; only the link assumption is strong.
    Application app({.simulation=true});
    app.edit(F::link_loss,"6 dB");app.edit(F::binary,"001");app.start();
    const auto wait=[&](auto predicate) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<end) {
            app.tick();if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    check(wait([&]{return app.enabled(C::transmit);}),"Regular simulation did not become ready");
    app.activate(C::transmit);app.select(F::fast_mode,"fast");
    const auto before=app.poll_count();
    check(wait([&]{return !app.field(F::signals).records.empty();}),"Switching to Fast stopped regular reception/progress");
    check(app.poll_count()>before,"Mode switch stopped shared progress polling");
    const auto records=app.field(F::signals).records;
    const auto draft=app.field(F::binary).text; // Normal successful TX may clear its composer.
    app.select(F::fast_mode,"robust");
    check(app.field(F::signals).records==records&&app.field(F::binary).text==draft,"Mode switch replaced regular reception rows or exact source bits");
    app.close();
}
}
int main() {
    try {presentation_and_retention();service_generations();retained_key_and_result_presentation();live_plot_presentation();unsynchronized_plot_presentation();regular_work_keeps_polling();std::cout<<"Fast GUI isolation tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
