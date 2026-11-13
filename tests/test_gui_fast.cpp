#include "application.hpp"
#include "controller.hpp"
#include "fast/controller.hpp"
#include "fast/presentation.hpp"
#include "fast/plots.hpp"
#include "fast/screen.hpp"
#include "datapump/types.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/preset.hpp"
#include "fixture_keyfile.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
void rate_and_spectrum_presentation() {
    auto profile=fast::resolve_snr_preset(fast::Channel::wire,36).profile;
    const auto payload=fast_ui::expected_modem_bitrate(profile,false);
    check(payload>0&&payload<fast::gross_bitrate(profile)*fast::code_rate_value(profile.code_rate),
        "Displayed expected rate omitted outer parity, integrity or modem framing");
    check(fast_ui::expected_modem_bitrate(profile,true)<payload,
        "Encryption overhead did not reduce expected payload bitrate");
    using Clock=fast::Telemetry::Clock;
    const auto now=Clock::now();
    auto d=std::make_shared<fast::Diagnostics>();
    d->sample_rate=48000;d->samples=48000;d->waveform_count=1024;d->captured_at=now;
    d->spectrum_valid=true;d->spectrum_db.fill(-60);
    fast::Snapshot simultaneous;simultaneous.loopback_test=simultaneous.transmitting=simultaneous.active=true;
    simultaneous.diagnostics=d;
    check(fast_ui::fresh_input(*d,now)&&!fast_ui::loopback_signal_snr(simultaneous,now),
        "Unacquired noise spectrum fabricated a signal SNR");
    d->acquired=true;d->last_loopback_snr=fast::LoopbackSnrEstimate{-3,-42,17500,40000};
    check(fast_ui::loopback_signal_snr(simultaneous,now).has_value(),
        "Captured RX telemetry could not expose SNR while a loopback snapshot also reports transmission");
    check(!fast_ui::loopback_signal_snr(simultaneous,now+std::chrono::seconds(3)),
        "Stalled capture displayed a current loopback SNR");
    d->transmitting=true;
    check(!fast_ui::loopback_signal_snr(simultaneous,now),"TX spectrum was labeled receiver SNR");
    d->transmitting=false;simultaneous.active=false;
    check(fast_ui::loopback_signal_snr(simultaneous,now+std::chrono::seconds(3)).has_value(),
        "Completed loopback lost its retained signal estimate");
    simultaneous.cancelled=true;
    check(!fast_ui::loopback_signal_snr(simultaneous,now),"Cancelled test retained a signal estimate");
    Application app({});
    check(app.field(ui::Field::fast_mode).selected=="fast","Ordinary launch did not default to Fast Modem");
    app.close();
    unsigned attempts=0;
    fast_ui::Controller invalid([&] {++attempts;return true;});
    invalid.edit(ui::Field::fast_device,"");invalid.set_selected(true);
    for(unsigned i=0;i<10;++i)invalid.poll();
    check(attempts==1&&!invalid.active(),"Invalid audio configuration retried on every continuous-listen poll");
    invalid.close();
}
void damaged_reception_presentation() {
    fast::Snapshot s;s.revision=1;s.active=true;s.listening=true;
    s.failed_cycles=1;s.checksum_groups=2;s.verified_bytes=4096;
    check(fast_ui::transfer_stage(s)=="RECEIVING / MISSING DATA",
        "Recoverable cycle damage was presented as a stopped receiver");
    auto label=fast_ui::integrity_label(s);
    check(label.find("2 checksum-verified groups")!=std::string::npos&&
        label.find("1 damaged cycle")!=std::string::npos&&
        label.find("decoding continues; awaiting physical end")!=std::string::npos,
        "Missing-data presentation lost verified progress or physical-end gating");
    ++s.checksum_groups;
    check(fast_ui::integrity_label(s).find("3 checksum-verified groups")!=std::string::npos,
        "Later verified data did not advance damaged-reception presentation");
    s.decoding_stopped=true;
    check(fast_ui::transfer_stage(s)=="RECEIVING / DECODING STOPPED"&&
        fast_ui::integrity_label(s).find("decoding stopped; awaiting physical end")!=std::string::npos,
        "Fatal stop was confused with recoverable coding damage");
    s.decoding_stopped=false;s.active=false;s.listening=false;s.physical_complete=true;
    check(fast_ui::transfer_stage(s)=="INCOMPLETE"&&
        fast_ui::integrity_label(s).find("received bytes available")==std::string::npos,
        "A file with missing data was presented as complete");
    fast::Snapshot finishing;finishing.physical_complete=finishing.complete=finishing.active=true;
    check(fast_ui::transfer_stage(finishing)=="RECEIVED · checksum verified, unauthenticated",
        "A physically completed result was still presented as pending during worker cleanup");
    finishing.simulated=true;
    check(fast_ui::transfer_stage(finishing)=="SIMULATED · checksum verified, unauthenticated",
        "A completed simulation was presented as a live reception");
    fast::Snapshot replay;replay.simulated=replay.transmitting=replay.active=true;replay.failed_cycles=1;
    check(fast_ui::transfer_stage(replay)=="SIMULATING / MISSING DATA",
        "Simulation transmission state hid receiver damage");
    fast::Snapshot loopback;loopback.loopback_test=loopback.transmitting=loopback.active=true;
    check(fast_ui::transfer_stage(loopback)=="LOOPBACK TEST / CAPTURING AND CHECKING",
        "Loopback test was presented as ordinary transmit or receive traffic");
    loopback.complete=loopback.physical_complete=true;loopback.authenticated=true;
    check(fast_ui::transfer_stage(loopback)=="LOOPBACK TEST · authenticated"&&
          fast_ui::integrity_label(loopback).find("LOOPBACK TEST · ")==0,
        "Completed loopback test was not clearly distinguished from regular received content");
    s.cancelled=true;
    check(fast_ui::transfer_stage(s)=="CANCELLED · no completion implied",
        "Damaged reception hid the explicit cancellation state");
}
const ui::Control& control(ui::Field field);
void fast_simulation_controls() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    app.select(F::fast_mode,"fast");
    const auto tabs=app.tab_layout(ui::default_width,ui::default_height);
    check(std::any_of(tabs.begin(),tabs.end(),[](const auto& tab){return tab.page==ui::Page::fast_simulation&&tab.visible;}),
          "Fast simulation/live testing tab is hidden");
    app.select_page(ui::Page::fast_simulation);
    const auto& mode=control(F::fast_simulation);
    check(app.control(mode).visible&&app.field(F::fast_simulation).selected=="live",
          "Fast page did not default to live audio with a visible mode selector");
    for(const auto field:{F::fast_sim_tx_power,F::fast_sim_path_loss,F::fast_sim_noise})
        check(!app.field(field).enabled,"Simulation-only link budget remained editable in live mode");
    const std::array declarations{F::fast_sim_baseband_oscillator,F::fast_sim_shift_oscillator,F::fast_sim_spread,
        F::fast_sim_selective,F::fast_sim_margin,F::fast_sim_shift_hz};
    for(const auto field:declarations)
        check(app.field(field).enabled&&control(field).page==ui::Page::console,
            "Live receiver declaration was disabled or declared on the testing page");
    app.select(mode,"simulation");
    check(app.field(F::fast_sim_noise).text=="-174"&&app.field(F::fast_sim_tx_power).enabled,
          "Simulation did not expose its independent channel assumptions and normal noise default");
    check(app.field(F::fast_sim_tx_power).options.size()>=2&&app.field(F::fast_sim_path_loss).options.size()>=2&&
          app.field(F::fast_sim_noise).options.size()>=2&&app.field(F::fast_sim_margin).options.size()>=2&&
          app.field(F::fast_sim_shift_hz).options.size()>=3&&
          std::any_of(app.field(F::fast_sim_baseband_oscillator).options.begin(),app.field(F::fast_sim_baseband_oscillator).options.end(),
              [](const auto& option){return option.id=="off";})&&
          std::any_of(app.field(F::fast_sim_shift_oscillator).options.begin(),app.field(F::fast_sim_shift_oscillator).options.end(),
              [](const auto& option){return option.id=="off";}),
          "Simulation text controls lack their shared preset dropdowns");
    app.preset(control(F::fast_sim_path_loss),"180");
    check(app.field(F::fast_sim_path_loss).text=="180","Path-loss dropdown did not update the editable value");
    app.preset(control(F::fast_sim_path_loss),"130");
    app.select_page(ui::Page::console);
    for(const auto field:declarations)
        check(app.control(control(field)).visible&&app.field(field).enabled,
            "Receiver declaration was missing from the Console during Simulation");
    app.preset(control(F::fast_sim_shift_hz),"5800000000");
    check(app.field(F::fast_sim_shift_hz).text=="5800000000","RF Shift frequency preset was not applied");
    const auto old_rate=app.field(F::fast_diagnostics).text;
    app.edit(control(F::fast_sim_shift_hz),"14000000");
    check(app.field(F::fast_sim_shift_hz).text=="14000000"&&app.field(F::fast_diagnostics).text==old_rate,
          "An RF shift within the selected preset's bounds changed its audio carrier/passband");
    app.select_page(ui::Page::fast_simulation);
    app.select(mode,"live");
    check(app.field(F::fast_sim_shift_hz).text=="14000000"&&!app.field(F::fast_sim_tx_power).enabled&&
          app.field(F::fast_diagnostics).text==old_rate&&
          app.field(F::fast_detail).text.find("Live audio · Uses declared receiver bounds")!=std::string::npos,
          "Switching to live changed the declaration, applied the link budget, or changed the selected waveform");
    app.select(F::fast_mode,"robust");app.select(F::fast_mode,"fast");
    check(app.field(F::fast_simulation).selected=="live"&&app.field(F::fast_sim_shift_hz).text=="14000000",
          "Switching modem modes did not preserve Fast simulation choices independently");
    app.close();

    unsigned acquisitions=0;fast_ui::Controller controller([&] {++acquisitions;return true;});
    controller.select(F::fast_simulation,"simulation");controller.set_selected(true);controller.poll();
    controller.activate(C::fast_listen);
    check(acquisitions==0&&!controller.enabled(C::fast_listen),
          "Simulation mode attempted live audio acquisition or exposed hardware listening");
    controller.edit(F::fast_text,"Simulation source");
    controller.edit(F::fast_text,"Repair an invalid simulation draft");
    controller.edit(F::fast_sim_noise,"invalid");
    controller.select(F::fast_simulation,"live");controller.select(F::fast_simulation,"simulation");controller.poll();
    check(controller.field(F::fast_simulation).selected=="simulation"&&controller.field(F::fast_sim_noise).enabled&&
          !controller.enabled(C::fast_transmit)&&acquisitions==0,
          "Invalid simulation draft prevented re-entering its editor or started hardware capture");
    controller.edit(F::fast_sim_noise,"-174");
    check(controller.enabled(C::fast_transmit),"Correcting the simulation draft did not restore its start action");
    controller.close();

    acquisitions=0;fast_ui::Controller loopback([&] {++acquisitions;return false;});
    loopback.select(F::fast_simulation,"simulation");
    loopback.edit(F::fast_sim_path_loss,"unfinished draft");
    loopback.select(F::fast_simulation,"loopback");loopback.set_selected(true);loopback.poll();
    check(!loopback.enabled(C::fast_listen)&&!loopback.enabled(C::fast_transmit)&&acquisitions==1&&loopback.active()&&
          loopback.field(F::fast_volume).enabled&&
          loopback.command_label(C::fast_transmit)=="Run loopback test"&&
          loopback.field(F::fast_detail).text.find("selected Console text")!=std::string::npos,
          "Loopback mode did not request monitoring independently of an empty test source");
    for(const auto field:{F::fast_sim_tx_power,F::fast_sim_path_loss,F::fast_sim_noise})
        check(!loopback.field(field).enabled&&loopback.field(field).disabled_text=="Simulation only",
              "Live loopback presented a synthetic link-budget field as active");
    check(loopback.field(F::fast_sim_shift_hz).enabled&&loopback.field(F::fast_sim_spread).enabled,
          "Live loopback disabled oscillator or spread settings along with the link budget");
    loopback.select(F::fast_volume,"30");
    check(loopback.field(F::fast_volume).selected=="30","Live loopback did not accept the selected output volume");
    const auto history_size=loopback.field(F::fast_history).records.size();
    const auto history_selection=loopback.field(F::fast_history).selected;
    loopback.activate(C::fast_transmit);
    check(acquisitions==1,"Loopback accepted an empty Console source");
    loopback.edit(F::fast_text,"fox");
    check(loopback.enabled(C::fast_transmit),"A retained Simulation link-budget draft blocked live loopback");
    loopback.activate(C::fast_transmit);
    check(acquisitions==2&&loopback.active()&&
          loopback.field(F::fast_status).text.find("Waiting for the audio device")!=std::string::npos&&
          loopback.field(F::fast_history).records.size()==history_size&&
          loopback.field(F::fast_history).selected==history_selection&&!loopback.enabled(C::fast_save),
          "Loopback did not acquire audio for the selected text or pending start changed received history");
    loopback.activate(C::fast_cancel);
    check(loopback.field(F::fast_sim_status).text.find("cancelled before audio")!=std::string::npos&&
          loopback.enabled(C::fast_listen)&&!loopback.enabled(C::fast_cancel),
          "Cancelling a queued loopback test did not retain its outcome or allow monitoring");
    loopback.edit(F::fast_text,"Console text draft stays here");
    loopback.select(F::fast_source,"file");loopback.edit(F::fast_file,"unopened-source.bin");
    check(loopback.enabled(C::fast_transmit)&&
          loopback.field(F::fast_detail).text.find("selected Console file")!=std::string::npos,
          "Selected Console file was not presented as the loopback source");
    loopback.activate(C::fast_transmit);
    check(loopback.field(F::fast_text).text=="Console text draft stays here"&&
          loopback.field(F::fast_file).text=="unopened-source.bin"&&
          loopback.field(F::fast_history).records.size()==history_size&&
          loopback.field(F::fast_history).selected==history_selection&&!loopback.enabled(C::fast_save),
          "Loopback test consumed a Console draft or altered received history/file selection");
    loopback.activate(C::fast_cancel);
    loopback.select(F::fast_simulation,"simulation");
    check(loopback.field(F::fast_sim_path_loss).enabled&&loopback.field(F::fast_sim_path_loss).text=="unfinished draft"&&
          !loopback.enabled(C::fast_transmit),
          "Live loopback discarded the Simulation draft or bypassed Simulation validation");
    loopback.select(F::fast_simulation,"live");
    check(loopback.field(F::fast_volume).enabled&&loopback.field(F::fast_volume).display_text.empty()&&
          loopback.field(F::fast_volume).selected=="30",
          "Live audio lost its configured TX volume after loopback");
    loopback.set_selected(false);
    const auto stopped_acquisitions=acquisitions;
    loopback.poll();
    check(!loopback.active()&&acquisitions==stopped_acquisitions,"Hidden Fast mode tried to acquire audio");
    loopback.close();

    fast_ui::FastPlots plots;
    check(plots.title(ui::Bitmap::fast_waveform,true).find("LOOPBACK TEST · ")==0&&
          plots.title(ui::Bitmap::fast_waveform).find("LOOPBACK TEST")==std::string::npos,
          "Loopback plot titles were not distinct or changed ordinary plot labels");
}
void simulation_profile_revalidation() {
    using F=ui::Field;using C=ui::Command;
    unsigned acquisitions=0;
    fast_ui::Controller controller([&] {++acquisitions;return false;});
    controller.edit(F::fast_text,"fox");
    check(controller.field(F::fast_sim_spread).selected=="hf-worst"&&
          controller.field(F::fast_sim_selective).selected=="each-65"&&controller.enabled(C::fast_transmit),
          "Acoustic-short did not begin with an admitted conservative receiver declaration");
    const auto adverse_rate=controller.field(F::fast_diagnostics).text;
    controller.select(F::fast_sim_spread,"none");
    controller.select(F::fast_sim_selective,"off");
    controller.select(F::fast_sim_baseband_oscillator,"off");
    const auto clean_rate=controller.field(F::fast_diagnostics).text;
    check(controller.enabled(C::fast_transmit)&&adverse_rate!=clean_rate,
          "Editing receiver assumptions in Live retained the previous Auto preset");
    for(const auto* mode:{"simulation","loopback","live"}) {
        controller.select(F::fast_simulation,mode);
        check(controller.enabled(C::fast_transmit)&&controller.field(F::fast_diagnostics).text==clean_rate&&
              controller.field(F::fast_sim_spread).selected=="none"&&controller.field(F::fast_text).text=="fox",
              "Changing test mode changed receiver assumptions, selected waveform or text draft");
    }
    controller.select(F::fast_sim_spread,"hf-worst");
    controller.select(F::fast_sim_selective,"each-65");
    controller.select(F::fast_sim_baseband_oscillator,"crystal");
    check(controller.field(F::fast_diagnostics).text==adverse_rate,
          "Restoring the declared channel did not restore the Auto preset");
    controller.edit(F::fast_sim_shift_hz,"5800000000");
    const auto error=controller.field(F::fast_status).text;
    check(error.starts_with("Fast receiver assumptions exceed "),
          "Receiver bounds did not report an actionable out-of-range diagnostic");
    for(const auto* mode:{"simulation","loopback","live"}) {
        controller.select(F::fast_simulation,mode);
        check(!controller.enabled(C::fast_transmit)&&!controller.enabled(C::fast_listen)&&
              controller.field(F::fast_status).text==error&&controller.field(F::fast_sim_shift_hz).text=="5800000000",
              "Changing mode bypassed the invalid receiver declaration or discarded its draft");
    }
    controller.report_error("Unrelated diagnostic");
    controller.edit(F::fast_sim_shift_hz,"0");
    check(controller.enabled(C::fast_transmit)&&controller.enabled(C::fast_listen)&&
          controller.field(F::fast_status).text=="Unrelated diagnostic"&&acquisitions==0,
          "Repairing receiver assumptions failed to restore start actions or cleared an unrelated diagnostic");
    controller.edit(F::fast_sim_margin,"0.5");
    check(!controller.enabled(C::fast_transmit)&&!controller.enabled(C::fast_listen),
          "Live accepted an invalid numeric receiver declaration");
    controller.select(F::fast_profile,"wire");
    check(controller.enabled(C::fast_transmit)&&controller.field(F::fast_sim_spread).selected=="none"&&
          controller.field(F::fast_sim_margin).text=="1",
          "A different channel inherited an invalid declaration");
    controller.select(F::fast_profile,"acoustic-short");
    check(!controller.enabled(C::fast_transmit)&&controller.field(F::fast_sim_margin).text=="0.5",
          "Switching channels discarded the previous declaration draft");
    controller.edit(F::fast_sim_margin,"1");
    check(controller.enabled(C::fast_transmit)&&controller.field(F::fast_diagnostics).text==adverse_rate,
          "Repairing a retained receiver declaration left a stale Auto profile");
    controller.close();
}
const ui::Control& control(ui::Field field) {
    for(const auto& c:ui::console_screen())if(c.field==field)return c;
    throw Error("Missing shared fast field");
}
const ui::Control& action(ui::Command command) {
    for(const auto& c:ui::console_screen())if(c.command==command)return c;
    throw Error("Missing shared fast action");
}
void audio_settings() {
    using F=ui::Field;
    fast_ui::Controller controller([] {return false;});
    check(control(F::fast_device).kind==ui::Kind::choice&&controller.field(F::fast_device).selected=="default",
          "Fast audio device is not a dropdown with the system default");
    controller.set_devices({{"first","First interface"},{"second","Second interface"}});
    controller.select(F::fast_device,"second");
    check(controller.field(F::fast_device).selected=="second"&&controller.field(F::fast_device).text=="second",
          "Fast device dropdown ignored an enumerated interface");
    controller.edit(F::fast_device,"custom-interface");controller.set_devices({{"first","First interface"}});
    check(controller.field(F::fast_device).selected=="custom-interface"&&
          controller.field(F::fast_device).options.back().id=="custom-interface",
          "Fast enumeration discarded a selected custom device");
    controller.select(F::fast_device,"unknown");
    check(controller.field(F::fast_device).selected=="custom-interface","Fast dropdown accepted an unknown device");
    const std::vector<std::string> volumes={"0.01","0.1","0.5","1","2","3","4","5","10","20","30","40","50","60","70","75","80","85","90","95","100","105","110","115","125","150","175"};
    const auto& volume=controller.field(F::fast_volume);
    check(volume.selected=="100"&&volume.options.size()==volumes.size(),"Fast volume default or list changed");
    for(std::size_t i=0;i<volumes.size();++i) {
        check(volume.options[i].id==volumes[i]&&volume.options[i].label==volumes[i]+"%","Fast volume choices differ from requested percentages");
        controller.select(F::fast_volume,volumes[i]);
        check(volume.selected==volumes[i],"Fast volume selection ignored");
    }
    check(!controller.field(F::fast_exclusive).checked&&controller.field(F::fast_exclusive).enabled==audio::exclusive_supported(),
          "Fast exclusive access default or platform support is incorrect");
    controller.toggle(F::fast_exclusive,true);
    check(controller.field(F::fast_exclusive).checked==audio::exclusive_supported(),"Fast exclusive override ignored its platform support");
    controller.edit(F::fast_text,"pending TX");controller.activate(ui::Command::fast_transmit);
    for(const auto field:{F::fast_device,F::fast_volume,F::fast_exclusive})
        check(!controller.field(field).enabled,"Fast pending transmit did not lock audio settings");
    controller.select(F::fast_volume,"100");controller.toggle(F::fast_exclusive,false);
    check(volume.selected=="175"&&controller.field(F::fast_exclusive).checked==audio::exclusive_supported(),
          "Fast pending transmit accepted stale audio settings callbacks");
    controller.close();
}
void message_submit_shortcut() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    const auto& editor=control(F::fast_text);
    check(editor.submit==C::fast_transmit&&editor.submit_mode==F::count,
        "Fast message submit is not independent of the regular send-key preference");
    app.select(F::send_key,"enter");app.select(F::fast_mode,"fast");
    const auto idle=app.field(F::fast_status).text;
    check(!app.enabled(C::fast_transmit)&&app.submit(editor,true,false)&&
        app.field(F::fast_status).text==idle,"Empty Fast draft was transmitted by Ctrl+Enter");
    app.edit(F::fast_device,""); // Exercise dispatch without opening an audio device.
    app.edit(F::fast_text,"Shortcut message");
    check(!app.submit(editor,false,false)&&!app.submit(editor,false,true)&&!app.submit(editor,true,true)&&
        app.field(F::fast_status).text==idle,"Plain/Shift+Enter triggered Fast transmission");
    check(app.enabled(C::fast_transmit)&&app.submit(editor,true,false)&&
        app.field(F::fast_status).text=="Select a fast audio device",
        "Ctrl+Enter did not dispatch the Fast transmit action");
    app.report_error("Keep disabled draft");app.toggle(F::fast_encryption,true);
    check(!app.enabled(C::fast_transmit)&&app.submit(editor,true,false)&&
        app.field(F::fast_status).text=="Keep disabled draft","Ctrl+Enter bypassed missing-key protection");
    app.toggle(F::fast_encryption,false);app.select(F::fast_source,"file");
    app.edit(F::fast_file,"unopened-shortcut-fixture.bin");
    const auto hidden=app.field(F::fast_status).text;
    check(app.submit(editor,true,false)&&app.field(F::fast_status).text==hidden,
        "Hidden message editor submitted a file transfer");
    app.select(F::fast_mode,"robust");
    const auto inactive=app.field(F::fast_status).text;
    check(app.submit(editor,true,false)&&app.field(F::fast_status).text==inactive,
        "Inactive Fast editor submitted from another modem");
    app.close();
}
void snr_and_symbol_rate_controls() {
    using F=ui::Field;using C=ui::Command;
    unsigned acquisitions=0;
    fast_ui::Controller controller([&] {++acquisitions;return false;});
    check(controller.field(F::fast_profile).selected=="acoustic-short"&&controller.field(F::fast_expected_snr).selected=="-6"&&
        controller.field(F::fast_symbol_rate).selected=="auto",
        "Fast GUI did not default to short speakers/microphone at -6 dB");
    for(const auto field:{F::fast_expected_snr,F::fast_symbol_rate}) {
        const auto& c=control(field);
        check(c.kind==ui::Kind::choice&&c.persistent==(field==F::fast_expected_snr)&&c.scope==ui::ScreenScope::fast&&c.help[0],
            "SNR/rate dropdown is missing from the shared Fast screen or lacks help");
    }
    controller.select(F::fast_profile,"acoustic");
    check(controller.field(F::fast_expected_snr).selected=="3"&&
        controller.field(F::fast_symbol_rate).selected=="auto",
        "Acoustic expected-SNR default is not independent of cable settings");
    const auto& choices=controller.field(F::fast_expected_snr).options;
    for(const auto id:{"13","10","6","3","0","-3","-6","-10","-20"})
        check(std::any_of(choices.begin(),choices.end(),[&](const auto& option){return option.id==id;}),
            "Acoustic expected-SNR dropdown omitted a notable target or 40 dB span");
    check(std::any_of(choices.begin(),choices.end(),[](const auto& option){return option.id!="manual"&&std::stod(option.id)<=-27;}),
        "Acoustic expected-SNR range does not cover 40 dB below its default");
    const auto check_narrow_preset=[&](const char* snr,const char* depth) {
        controller.select(F::fast_expected_snr,snr);
        const auto expected=fast::resolve_snr_preset(fast::Channel::acoustic,std::stod(snr)).profile;
        check(expected.acoustic_ofdm&&std::to_string(expected.interleave_depth)==depth,
            "Narrow acoustic preset lost its shorter refresh-cycle depth");
        const auto& selected_depth=controller.field(F::fast_depth);
        check(selected_depth.selected==depth&&std::any_of(selected_depth.options.begin(),selected_depth.options.end(),
            [&](const auto& option){return option.id==selected_depth.selected&&option.enabled;}),
            "Automatic acoustic depth has no selectable dropdown entry");
        check(controller.field(F::fast_expected_snr).selected==snr&&
            controller.field(F::fast_symbol_rate).selected=="auto"&&
            controller.field(F::fast_constellation).selected==std::to_string(expected.constellation)&&
            controller.field(F::fast_coding).selected=="two-thirds"&&
            expected.code_rate==fast::CodeRate::two_thirds,
            "Narrow acoustic preset did not apply matching QAM, LDPC and automatic timing");
        const auto& rate=controller.field(F::fast_symbol_rate);
        check(std::any_of(rate.options.begin(),rate.options.end(),[&](const auto& option){
            return option.id==fast::symbol_rate_option_id(expected);
        }),"Automatic acoustic timing has no matching explicit rate option");
        std::ostringstream band;
        band<<std::fixed<<std::setprecision(0)<<fast::occupied_lower_hz(expected)<<"–"<<fast::occupied_upper_hz(expected)<<" Hz";
        check(controller.field(F::fast_detail).text.find(band.str())!=std::string::npos,
            "Narrow acoustic selection retained a different profile passband");
    };
    check_narrow_preset("3","2");
    check_narrow_preset("0","1");
    controller.select(F::fast_expected_snr,"10");
    const auto p=fast::resolve_snr_preset(fast::Channel::acoustic,10).profile;
    check(controller.field(F::fast_expected_snr).selected=="10"&&
        controller.field(F::fast_symbol_rate).selected=="auto"&&
        controller.field(F::fast_constellation).selected==std::to_string(p.constellation),
        "Expected-SNR selection did not apply the resolved modem settings");
    const auto detail=controller.field(F::fast_detail).text;
    check(detail.find("reference bandwidth")!=std::string::npos&&
        detail.find("Resilient transmission · 10 dB SNR (assumed)")!=std::string::npos&&
        detail.find("does not measure or negotiate")!=std::string::npos&&
        detail.find("not measured throughput or receiver qualification")!=std::string::npos,
        "Automatic preset was presented as measured or negotiated SNR");
    const auto options=fast::symbol_rate_options(p);
    const auto current=fast::symbol_rate_option_id(p);
    const auto rate=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.id!="auto"&&option.id!=current;});
    check(rate!=options.end(),"Acoustic symbol-rate dropdown has no alternate timing");
    controller.select(F::fast_symbol_rate,rate->id);
    check(controller.field(F::fast_symbol_rate).selected==rate->id&&
        controller.field(F::fast_expected_snr).selected=="manual",
        "Explicit symbol timing retained a misleading automatic SNR selection");
    controller.select(F::fast_constellation,"64");controller.toggle(F::fast_mono,false);
    check(controller.field(F::fast_symbol_rate).selected==rate->id,
        "Manual constellation edit reset explicit symbol timing");
    controller.select(F::fast_profile,"wire");
    check(controller.field(F::fast_expected_snr).selected=="36"&&
        controller.field(F::fast_symbol_rate).selected=="auto"&&controller.field(F::fast_mono).selected=="left",
        "Acoustic overrides leaked into the cable profile");
    controller.select(F::fast_expected_snr,"40");
    controller.select(F::fast_profile,"acoustic");
    const auto default_acoustic=fast::resolve_snr_preset(fast::Channel::acoustic,3).profile;
    check(controller.field(F::fast_expected_snr).selected=="3"&&
        controller.field(F::fast_symbol_rate).selected=="auto"&&
        controller.field(F::fast_constellation).selected==std::to_string(default_acoustic.constellation)&&!controller.field(F::fast_mono).checked,
        "Changing channel did not reset the SNR/profile preset while preserving output routing");
    controller.select(F::fast_constellation,"64");
    controller.select(F::fast_symbol_rate,"auto");
    check(controller.field(F::fast_constellation).selected=="64"&&
        controller.field(F::fast_expected_snr).selected=="manual"&&
        controller.field(F::fast_symbol_rate).selected=="auto",
        "Automatic timing discarded independent manual constellation settings");
    controller.select(F::fast_expected_snr,"-10");
    const auto narrow=fast::resolve_snr_preset(fast::Channel::acoustic,-10).profile;
    check(controller.field(F::fast_expected_snr).selected=="-10"&&
        controller.field(F::fast_constellation).selected==std::to_string(narrow.constellation)&&
        controller.field(F::fast_symbol_rate).selected=="auto",
        "Lower-SNR preset did not apply its resolved waveform and clear manual rate override");
    check(!narrow.acoustic_ofdm&&controller.field(F::fast_detail).text.find("symbols/s")!=std::string::npos,
        "Narrow single-carrier preset retained an inactive OFDM timing label");
    controller.select(F::fast_expected_snr,"manual");
    check(controller.field(F::fast_symbol_rate).selected==fast::symbol_rate_option_id(narrow),
        "Changing narrowed waveform to Manual discarded its actual timing");
    controller.select(F::fast_symbol_rate,"auto");
    const auto narrow_auto=fast::apply_symbol_rate_option(narrow,"auto");
    check(std::any_of(controller.field(F::fast_symbol_rate).options.begin(),controller.field(F::fast_symbol_rate).options.end(),
        [&](const auto& option){return option.id==fast::symbol_rate_option_id(narrow_auto);}),
        "Manual narrow SC Auto timing has no matching explicit dropdown option");
    controller.select(F::fast_profile,"acoustic-short");
    const auto short_acoustic=fast::resolve_snr_preset(fast::Channel::acoustic_short,-6).profile;
    check(controller.field(F::fast_profile).selected=="acoustic-short"&&
        controller.field(F::fast_expected_snr).selected=="-6"&&
        controller.field(F::fast_symbol_rate).selected=="auto"&&
        controller.field(F::fast_constellation).selected==std::to_string(short_acoustic.constellation)&&
        controller.field(F::fast_depth).selected==std::to_string(short_acoustic.interleave_depth)&&
        controller.field(F::fast_mono).selected=="left"&&
        controller.field(F::fast_detail).text.find("acoustic-short")!=std::string::npos,
        "Short-transfer acoustic profile did not select its independent -6 dB preset");
    check(controller.field(F::fast_coding).options.size()==(short_acoustic.compact_convolutional?2U:3U)&&
        controller.field(F::fast_coding).options[0].id=="half"&&
        controller.field(F::fast_coding).options.back().id=="three-quarters",
        "Short-transfer acoustic offered unsupported coding rates");
    const auto short_coding=controller.field(F::fast_coding).selected;
    controller.select(F::fast_coding,"nine-tenths");
    check(controller.field(F::fast_coding).selected==short_coding,
        "Unsupported normal-frame LDPC rate was accepted for short-transfer acoustic");
    controller.select(F::fast_expected_snr,"-6");
    check(controller.field(F::fast_coding).selected=="two-thirds"&&
        controller.field(F::fast_coding).options.size()==3&&
        controller.field(F::fast_coding).options[0].label=="LDPC 1/2"&&
        controller.field(F::fast_depth).options.size()==16&&
        controller.field(F::fast_constellation).options.size()==1,
        "Weak short profile did not expose its small LDPC coding geometry");
    controller.select(F::fast_coding,"half");
    check(controller.field(F::fast_coding).selected=="half"&&
        controller.field(F::fast_expected_snr).selected=="manual",
        "Weak short profile rejected the supported stronger LDPC selection");
    controller.select(F::fast_expected_snr,"3");
    controller.edit(F::fast_text,"Check declared-channel selection");
    check(controller.enabled(C::fast_transmit)&&controller.field(F::fast_constellation).options.size()==1&&
        controller.field(F::fast_detail).text.find("symbols/s")!=std::string::npos,
        "Higher SNR with declared fading did not select a supported short single carrier");
    controller.select(F::fast_sim_spread,"none");
    controller.select(F::fast_sim_selective,"off");
    check(controller.enabled(C::fast_transmit)&&controller.field(F::fast_expected_snr).selected=="3"&&
        controller.field(F::fast_coding).options.size()==3&&
        controller.field(F::fast_constellation).options.size()==11&&
        controller.field(F::fast_depth).options.size()==16,
        "Removing declared fading at 3 dB did not restore short OFDM controls");
    controller.select(F::fast_mono,"right");controller.select(F::fast_expected_snr,"0");
    controller.select(F::fast_profile,"acoustic");
    check(controller.field(F::fast_expected_snr).selected=="3"&&
        controller.field(F::fast_depth).selected==std::to_string(default_acoustic.interleave_depth)&&
        controller.field(F::fast_coding).options.size()==6&&
        controller.field(F::fast_mono).selected=="stereo",
        "Short-transfer acoustic settings changed the existing acoustic profile");
    controller.select(F::fast_profile,"acoustic-short");
    check(controller.field(F::fast_expected_snr).selected=="-6"&&
        controller.field(F::fast_depth).selected==std::to_string(short_acoustic.interleave_depth)&&
        controller.field(F::fast_mono).selected=="right",
        "Returning to short-transfer acoustic did not reset SNR while retaining its output routing");
    controller.select(F::fast_profile,"wire");
    check(controller.field(F::fast_expected_snr).selected=="36",
        "Returning to cable restored an old expected SNR instead of its 36 dB default");
    controller.set_selected(true);controller.activate(C::fast_listen);
    check(acquisitions==1&&controller.active()&&controller.field(F::fast_expected_snr).enabled&&
        controller.field(F::fast_symbol_rate).enabled,"Pending listening disabled editable modem settings");
    controller.select(F::fast_expected_snr,"36");controller.edit(F::fast_text,"Draft while waiting for input");
    check(controller.field(F::fast_expected_snr).selected=="36"&&controller.enabled(C::fast_transmit),
        "Listening could not accept a new profile or transmit draft");
    check(!controller.enabled(C::fast_cancel),"A pending listener exposed Cancel transmission");
    controller.activate(C::fast_cancel);check(controller.active(),"Cancel transmission stopped a listener");
    controller.set_selected(false);
    check(!controller.active()&&controller.field(F::fast_expected_snr).enabled&&controller.field(F::fast_symbol_rate).enabled,
        "Leaving Fast mode did not release pending audio acquisition");
    controller.close();
}
void presentation_and_retention() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    const auto& mode=control(F::fast_mode);
    check(mode.kind==ui::Kind::choice&&mode.persistent&&mode.scope==ui::ScreenScope::shared&&app.field(F::fast_mode).selected=="robust",
          "Simulation launch must retain the Robust Modem facilities");
    check(app.field(F::fast_mode).options.size()==3&&app.field(F::fast_mode).options[0].label=="Fast Modem"&&
        app.field(F::fast_mode).options[1].label=="Robust Modem","Modem selector labels changed");
    app.toggle(F::developer_mode,true);app.select_page(ui::Page::compression);
    app.edit(F::binary,"001");const auto bits=app.field(F::binary).text;
    app.select(F::fec,"off");const auto fec=app.field(F::fec).selected;
    app.select(mode,"fast");
    check(app.field(F::fast_mode).selected=="fast","Fast Modem choice was not accepted");
    check(app.field(F::fast_profile).selected=="acoustic-short"&&app.field(F::fast_expected_snr).selected=="-6"&&
        app.field(F::fast_mono).selected=="left","Fast must default to short speakers/microphone at -6 dB with left mono");
    check(app.field(F::fast_qr_brightness).selected=="dark"&&app.field(F::fast_qr_brightness).options.size()==4,
        "Fast QR brightness choices/default differ from the shared convention");
    for(const auto& c:ui::console_screen()) {
        if(c.scope==ui::ScreenScope::regular||c.scope==ui::ScreenScope::legacy)check(!app.control(c).visible,"Other modem controls leaked into the fast interface");
        else if(c.scope==ui::ScreenScope::fast&&c.page==ui::Page::console)
            check(app.control(c).visible==(c.field!=F::fast_file),"Fast interface failed to show the selected source controls");
    }
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))
        check(tab.visible==(tab.page==ui::Page::console||tab.page==ui::Page::fast_simulation||tab.page==ui::Page::fast_modem),"Fast tabs are not scoped to Console, simulation/live testing, and developer Modem details");
    app.toggle(F::developer_mode,false);
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))
        check(tab.visible==(tab.page==ui::Page::console||tab.page==ui::Page::fast_simulation),"Normal Fast view must keep Console and simulation/live testing visible");
    app.toggle(F::developer_mode,true);
    const auto acoustic=fast::resolve_snr_preset(fast::Channel::acoustic,3).profile;
    const auto& profiles=app.field(F::fast_profile).options;
    check(profiles.size()==5&&profiles[0].id=="wire"&&profiles[1].id=="ssb"&&
        profiles[2].id=="fm"&&profiles[3].id=="acoustic"&&profiles[4].id=="acoustic-short"&&
        profiles[4].label=="Speakers / mic · short"&&app.field(F::fast_constellation).options.size()==1,
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
    app.select(control(F::fast_profile),"ssb");
    check(app.field(F::fast_constellation).selected=="4"&&app.field(F::fast_detail).text.find("LDPC frame: 16200 bits")!=std::string::npos,
          "Nominal SSB did not expose its short QPSK frame geometry");
    check(app.field(F::fast_expected_snr).selected=="20",
          "SSB relabeling changed the nominal expected-SNR choice");
    check(std::any_of(app.field(F::fast_expected_snr).options.begin(),app.field(F::fast_expected_snr).options.end(),
        [](const auto& option){return option.id=="20"&&option.label=="Resilient · 20 dB SNR · ref 2.4 kHz";})&&
          std::any_of(app.field(F::fast_expected_snr).options.begin(),app.field(F::fast_expected_snr).options.end(),
        [](const auto& option){return option.id=="10"&&option.label=="Resilient · 10 dB SNR · ref 2.4 kHz";}),
          "SSB SNR options did not present the common resilient transmission training");
    app.select(F::fast_expected_snr,"10");
    check(app.field(F::fast_detail).text.find("Resilient transmission · 10 dB SNR (assumed)")!=std::string::npos&&
          app.field(F::fast_detail).text.find("public training and recurring pilots support SC and OFDM acquisition")!=std::string::npos&&
          app.field(F::fast_detail).text.find("not measured throughput or receiver qualification")!=std::string::npos,
          "SSB expected SNR did not distinguish resilient transmission from estimated rate and receiver qualification");
    app.select(F::fast_expected_snr,"20");
    check(app.field(F::fast_detail).text.find("Resilient transmission · 20 dB SNR (assumed)")!=std::string::npos,
          "Nominal SSB reset did not keep the common resilient-transmission label");
    check(std::any_of(app.field(F::fast_coding).options.begin(),app.field(F::fast_coding).options.end(),
        [](const auto& option){return option.id=="eight-ninths";}),
        "Short SSB hid standard-frame rates needed for manual normalization");
    app.select(F::fast_constellation,"4096");app.select(F::fast_coding,"eight-ninths");app.select(F::fast_depth,"8");
    check(app.field(F::fast_detail).text.find("LDPC frame: 64800 bits")!=std::string::npos&&
          app.field(F::fast_constellation).selected=="4096"&&app.field(F::fast_coding).selected=="eight-ninths"&&
          app.field(F::fast_depth).selected=="8",
          "Manual SSB high-rate choices did not normalize transactionally to the standard frame");
    app.select(F::fast_profile,"wire");
    check(app.field(F::fast_detail).text.find("Resilient transmission")!=std::string::npos&&
          app.field(F::fast_detail).text.find("public training and recurring pilots support SC and OFDM acquisition")!=std::string::npos&&
          app.field(F::fast_detail).text.find("not measured throughput or receiver qualification")!=std::string::npos,
          "Non-SSB SNR details omitted common training or confused estimates with receiver qualification");
    app.select(F::fast_profile,"ssb");
    check(app.field(F::fast_detail).text.find("LDPC frame: 16200 bits")!=std::string::npos&&
          app.field(F::fast_constellation).selected=="4"&&app.field(F::fast_coding).selected=="half"&&
          app.field(F::fast_depth).selected=="1",
          "Returning to the nominal SSB preset did not restore its short frame");
    app.select(F::fast_constellation,"256");
    check(app.field(F::fast_mono).checked,"Radio output routing default changed");
    app.toggle(F::fast_mono,false);check(!app.field(F::fast_mono).checked,"Explicit radio stereo override ignored");
    app.select(F::fast_source,"file");
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
            if(c.kind==ui::Kind::bitmap&&c.bitmap!=ui::Bitmap::fast_qr)check(geometry.widget.w>=300&&geometry.widget.h>=140&&geometry.has_caption,
                "Fast live plots lost useful dimensions or their metadata captions");
        }
        const auto occupied=[&](const ui::Control& c) {
            const auto geometry=app.control_layout(c,size.w,size.h);auto r=geometry.frame;
            if(geometry.has_label&&geometry.label.y<r.y) {r.h+=r.y-geometry.label.y;r.y=geometry.label.y;}
            return r;
        };
        for(const auto field:{F::fast_expected_snr,F::fast_symbol_rate}) {
            const auto r=occupied(control(field));
            check(r.w>=180,"SNR/rate dropdowns lost room for their descriptive choices");
            for(const auto& c:ui::console_screen())if(c.scope==ui::ScreenScope::fast&&c.field!=field&&app.control(c).visible&&
                (c.persistent||control(field).persistent||c.page==control(field).page)) {
                const auto other=occupied(c);
                check(r.x+r.w<=other.x||other.x+other.w<=r.x||r.y+r.h<=other.y||other.y+other.h<=r.y,
                    "SNR/rate dropdown overlaps another Fast control or its label");
            }
        }
        const auto title=ui::DesktopLayout(size.w,size.h)[ui::Slot::header];
        const auto toggle=app.control_layout(mode,size.w,size.h).frame;
        check(toggle.x>=title.x+title.w&&toggle.x<title.x+title.w+24,"Modem selector is not adjacent to DATA PUMP");
    }
    app.select(mode,"robust");
    check(app.page()==ui::Page::compression&&app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,
          "Returning to regular mode changed its selected page or exact draft");
    const auto retained_snr=app.field(F::fast_expected_snr).selected,retained_rate=app.field(F::fast_symbol_rate).selected;
    app.edit(control(F::fast_file),"/tmp/stale.bin");app.select(control(F::fast_profile),"wire");app.activate(action(C::fast_choose_file));
    app.select(F::fast_expected_snr,"10");app.select(F::fast_symbol_rate,"auto");
    check(app.field(F::fast_file).text=="/tmp/independent-source.bin"&&app.field(F::fast_profile).selected=="ssb"&&app.take_services().empty(),
          "Hidden fast callbacks changed state or opened native services");
    check(app.field(F::fast_expected_snr).selected==retained_snr&&app.field(F::fast_symbol_rate).selected==retained_rate,
        "Hidden Fast callbacks changed retained SNR or symbol timing");
    app.select(mode,"fast");check(app.field(F::fast_constellation).selected=="256"&&app.field(F::fast_text).text==fast_text&&app.field(F::fast_source).selected=="text",
        "Fast settings or source draft were discarded on mode switch");
    check(app.field(F::fast_airtime).text.find("Transmit")!=std::string::npos,"Fast draft has no airtime estimate");
    check(app.field(F::fast_detail).text.find("Shannon-Hartley")!=std::string::npos,"Fast capacity explanation missing");
    app.select(F::fast_profile,"acoustic");
    check(app.field(F::fast_mono).checked,"Acoustic profile must default to left mono");
    check(app.field(F::fast_detail).text.starts_with("Next: acoustic"),"Acoustic GUI selection silently used cable identity");
    if(acoustic.capacity_mode) {
        if(acoustic.acoustic_ofdm)check(app.field(F::fast_detail).text.find("OFDM")!=std::string::npos&&
            app.field(F::fast_detail).text.find("ms echo guard")!=std::string::npos&&
            app.field(F::fast_detail).text.find("ms blocks")!=std::string::npos&&
            app.field(F::fast_detail).text.find("symbols/s")==std::string::npos,
            "OFDM detail used inactive single-carrier symbol-rate fields");
        check(app.field(F::fast_constellation).selected==std::to_string(acoustic.constellation)&&app.field(F::fast_depth).selected==std::to_string(acoustic.interleave_depth)&&
            app.field(F::fast_coding).selected=="two-thirds"&&app.field(F::fast_fec).selected=="sparse",
            "Acoustic 3 dB preset defaults were not applied to GUI");
        app.select(F::fast_constellation,"64");app.toggle(F::fast_mono,false);
        check(app.field(F::fast_constellation).selected=="64"&&!app.field(F::fast_mono).checked,
            "Explicit acoustic 64-QAM and both-speaker output remain unavailable");
        app.select(F::fast_profile,"acoustic");
        check(app.field(F::fast_constellation).selected=="64"&&!app.field(F::fast_mono).checked,
            "Reselecting acoustic discarded its retained modulation and output settings");
        app.select(F::fast_expected_snr,"13");app.toggle(F::fast_mono,true);
        check(app.field(F::fast_constellation).selected=="16"&&app.field(F::fast_coding).selected=="three-quarters",
            "Automatic acoustic SNR selection did not restore its modulation and coding defaults");
        app.select(F::fast_coding,"half");
        check(app.field(F::fast_coding).selected=="half","Acoustic LDPC1/2 option missing");
        app.select(F::fast_coding,"two-thirds");
        check(app.field(F::fast_coding).selected=="two-thirds","Acoustic LDPC2/3 option missing");
        app.select(F::fast_profile,"acoustic-classic");
        check(app.field(F::fast_profile).selected=="acoustic"&&app.field(F::fast_constellation).selected=="16"&&
            app.field(F::fast_depth).selected=="8"&&app.field(F::fast_coding).selected=="two-thirds"&&
            app.field(F::fast_fec).selected=="sparse"&&app.field(F::fast_mono).checked,
            "Removed classic acoustic profile changed the current settings");
    }
    app.select(F::fast_profile,"wire");
    check(app.field(F::fast_mono).selected=="left","Cable profile did not retain left mono routing");
    app.toggle(F::fast_mono,true);check(app.field(F::fast_mono).checked,"Explicit cable left-mono override ignored");
    const auto wire=fast::resolve_snr_preset(fast::Channel::wire,36).profile;
    check(app.field(F::fast_constellation).selected==std::to_string(wire.constellation)&&
        app.field(F::fast_depth).selected==std::to_string(wire.interleave_depth)&&
        app.field(F::fast_fec).selected=="sparse", "Cable 36 dB defaults differ from the resolved modem preset");
    app.select(F::fast_constellation,"16384");app.select(F::fast_coding,"nine-tenths");app.select(F::fast_depth,"8");
    check(app.field(F::fast_constellation).selected=="16384"&&app.field(F::fast_coding).selected=="nine-tenths"&&
        app.field(F::fast_depth).selected=="8","Capacity QAM/LDPC choices ignored");
    app.select(F::fast_coding,"half");check(app.field(F::fast_coding).selected=="half","Capacity LDPC1/2 choice ignored");
    app.select(F::fast_profile,"wire-classic");
    check(app.field(F::fast_profile).selected=="wire"&&app.field(F::fast_constellation).selected=="16384"&&
        app.field(F::fast_coding).selected=="half"&&app.field(F::fast_depth).selected=="8"&&
        app.field(F::fast_fec).selected=="sparse"&&app.field(F::fast_mono).checked,
        "Removed classic cable profile changed the current settings");
    app.select(F::fast_profile,"fm");
    check(app.field(F::fast_profile).selected=="fm"&&app.field(F::fast_constellation).selected=="64"&&
        app.field(F::fast_coding).selected=="three-quarters"&&app.field(F::fast_depth).selected=="4"&&
        app.field(F::fast_fec).selected=="sparse"&&app.field(F::fast_mono).checked,
        "FM radio capacity defaults differ from modem profile");
    app.select(F::fast_depth,"16");check(app.field(F::fast_depth).selected=="16","Fast radio LDPC depth choice ignored");
    app.select(F::fast_depth,"62");check(app.field(F::fast_depth).selected=="16","Removed classic radio depth changed capacity settings");
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
    auto decoder_only=std::make_shared<fast::Diagnostics>(*data);++decoder_only->revision;
    check(plots.update(decoder_only,true,start+std::chrono::seconds(3))&&plots.title(B::fast_waveform).find("Stalled")!=std::string::npos&&
          plots.history_size()==1,
        "Receiver progress without fresh PCM falsely marked capture live or advanced the waterfall");
    check(plots.update(decoder_only,false,start+std::chrono::seconds(4))&&plots.title(B::fast_waterfall).find("Retained")!=std::string::npos,
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
    auto resumed=std::make_shared<fast::Diagnostics>(*next);resumed->revision=2;resumed->samples+=512;
    const auto hidden_revision=plots.revision();
    check(plots.update(resumed,true,start+std::chrono::seconds(22)+std::chrono::milliseconds(100))&&
          plots.revision()>hidden_revision&&plots.history_size()==2&&
          plots.title(B::fast_waterfall)=="Live RX waterfall",
          "New telemetry after a hidden display interval did not refresh the retained waterfall");
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

    P single_carrier_plots;
    single_carrier_plots.update(data,true,start);
    auto quiet_tail=std::make_shared<fast::Diagnostics>(*data);
    ++quiet_tail->revision;quiet_tail->samples+=3*quiet_tail->sample_rate;
    single_carrier_plots.update(quiet_tail,true,start+std::chrono::seconds(3));
    check(single_carrier_plots.title(B::fast_constellation)=="Last RX equalized constellation"&&
          single_carrier_plots.title(B::fast_waterfall)=="Live RX waterfall"&&single_carrier_plots.history_size()==2,
          "New single-carrier quiet-tail audio falsely refreshed the constellation or stalled the waterfall");
}
void service_generations() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    const auto regular_key=app.field(F::key).selected;
    app.activate(C::open_keyfile);auto regular=app.take_services();
    check(regular.size()==1,"Robust keyfile service unavailable");
    app.select(F::fast_mode,"fast");app.select(F::fast_source,"file");app.activate(C::fast_choose_file);auto fast=app.take_services();
    check(fast.size()==1&&fast.front().id!=regular.front().id,"Mode services reused a callback identity");
    app.complete_service({regular.front().id,false,"/tmp/stale-regular.key",{}});
    app.complete_service({fast.front().id,false,"/tmp/fast-current.bin",{}});
    check(app.field(F::fast_file).text=="/tmp/fast-current.bin","Fast file service routed to the wrong controller");
    app.activate(C::fast_choose_file);auto old=app.take_services();check(old.size()==1,"Second fast service unavailable");
    app.select(F::fast_mode,"robust");
    check(app.field(F::key).selected==regular_key&&app.control(action(C::open_keyfile)).enabled,
          "Stale Robust service changed keys or started keyfile loading after a mode change");
    app.select(F::fast_mode,"fast");
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
    const auto generated=create_keyring(path,{"GUI retained key"},std::nullopt,fixture_keyfile::generation());
    check(generated.audio_used,"Fast GUI key fixture did not collect synthetic audio");
    controller.activate(C::fast_open_key);const auto request=controller.take_services();check(request.size()==1,"Encrypted key service unavailable");
    controller.complete_service({request.front().id,false,path.string(),{}});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.enabled(C::fast_listen)&&std::chrono::steady_clock::now()<end) {controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(controller.enabled(C::fast_listen),"Fast key did not load");
    const auto key=controller.field(F::fast_key).selected,key_label=controller.field(F::fast_key).options.front().label;
    check(key_label=="GUI retained key - "+path.string(),"Fast key option must show its full keyfile path");
    controller.toggle(F::fast_encryption,false);
    check(controller.enabled(C::fast_listen)&&!controller.field(F::fast_key).enabled&&!controller.enabled(C::fast_generate_key),"Plain mode did not disable retained key controls");
    controller.toggle(F::fast_encryption,true);
    check(controller.enabled(C::fast_listen)&&controller.field(F::fast_key).selected==key&&controller.field(F::fast_key).options.front().label==key_label,"Encryption toggle discarded the loaded key");
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
void robust_keyfile_menu_label() {
    using F=ui::Field;using C=ui::Command;
    const auto path=std::filesystem::temp_directory_path()/(
        "datapump-robust-gui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".key");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{path};
    check(create_keyring(path,{"GUI key"},std::nullopt,fixture_keyfile::generation()).audio_used,
          "Robust GUI fixture did not use synthetic audio");
    launch_command::Patch patch;patch.keyfile=path.string();
    Application app({.simulation=true,.settings=patch});app.select(F::fast_mode,"robust");app.start();
    const auto& item=action(C::acknowledge_key_failure);
    const auto expected="Keep current keys - "+path.filename().string();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(app.control(item).label!=expected&&std::chrono::steady_clock::now()<deadline) {
        app.tick();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(app.control(item).label==expected&&!app.control(item).enabled,
          "Robust key menu did not show loaded filename on disabled current-keys item");
    app.activate(C::unload_keys);
    check(app.control(item).label=="Keep current keys"&&!app.control(item).enabled,
          "Robust key menu retained a filename after unloading keys");
    app.close();
}
void robust_public_key_warning() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});app.select(F::fast_mode,"robust");
    const auto& status=control(F::status);
    check(app.control(status).label.starts_with("WARNING: Amateur key is public"),
          "Public Robust key warning was not visible in the shared status presentation");
    app.activate(C::unload_keys);
    check(!app.control(status).label.starts_with("WARNING: Amateur key is public"),
          "Public key warning remained after the key was unloaded");
    app.activate(C::load_amateur);
    check(app.control(status).label.find("Public amateur key")!=std::string::npos&&
          !app.control(status).label.starts_with("WARNING: Amateur key is public"),
          "Public key load notice was duplicated or suppressed");
    app.close();
}
void robust_boundary_keeps_polling() {
    using F=ui::Field;using C=ui::Command;
    // Configuring a real Robust session must not fabricate reception or lose
    // the independent draft when the shared host switches modems.
    Application app({.simulation=true});
    app.edit(F::short_bits,"001");app.edit(F::bandwidth,"1.2 kHz");app.start();app.tick();
    const auto bits=app.field(F::short_bits).text;
    const auto rate=app.field(F::bandwidth).text;
    const auto key=app.field(F::key).selected;
    check(bits=="001"&&app.field(F::signals).records.empty(),
          "Configuring Robust lost its raw draft or fabricated reception");
    app.select(F::fast_mode,"fast");app.edit(F::fast_text,"Keep this Fast draft");
    const auto before=app.poll_count();
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(app.poll_count()==before&&std::chrono::steady_clock::now()<end) {
        app.tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(app.poll_count()>before,"Mode switch stopped shared event polling");
    app.select(F::fast_mode,"robust");
    check(app.field(F::short_bits).text==bits&&app.field(F::bandwidth).text==rate&&
          app.field(F::key).selected==key&&app.field(F::message).enabled,
          "Fast mode replaced the retained Robust draft or settings");
    check(app.field(F::signals).records.empty(),"Mode switch fabricated Robust reception");
    app.select(F::fast_mode,"fast");
    check(app.field(F::fast_text).text=="Keep this Fast draft"&&app.field(F::fast_text).enabled,
          "Returning to Fast lost its independent draft or editor");
    app.close();
}
void robust_native_settings() {
    using F=ui::Field;
    Controller controller({.simulation=true});
    check(controller.settings().transfer.fec==FecMode::off&&!controller.settings().receiver_memory_limit&&
          controller.settings().transfer.content_limit==1024*1024,
          "Robust must preserve FEC Off, automatic receiver headroom, and the local decoded-byte quota");
    controller.edit(F::link_power,"1 W");controller.edit(F::link_loss,"90 dB");
    controller.edit(F::link_noise,"-164 dBm/Hz");controller.edit(F::snr,"20");
    controller.select(F::dsss_factor,"10");controller.edit(F::bandwidth,"36 Hz");
    controller.select(F::symbol_burst,"2");controller.select(F::symbol_burst_random,"3");
    controller.select(F::fhss,"fake-fcc");controller.select(F::fec,"rs20");
    controller.edit(F::helper_power,"100 mW");controller.select(F::helper_ratio_limit,"20");
    controller.edit(F::helper_callsign,"K1ABC");
    controller.edit(F::calendar_epoch,"1793850000");controller.edit(F::calendar_ordinal,"12");
    controller.edit(F::live_noise_dbfs_hz,"-92.25");
    const auto& s=controller.settings();
    check(s.tx_dbm==30&&s.path_loss_db==90&&s.noise_dbm_hz==-164&&s.target_cn0_db_hz==20,
          "Robust link/target edits did not reach the session");
    check(s.added_interleave&&s.added_symbols==10&&s.transfer.modem.bandwidth_hz==36&&
          s.symbol_burst_wait==2&&s.symbol_burst_random==3&&s.fhss_profile=="fake-fcc",
          "Robust waveform controls remained saved-only preferences");
    check(s.helper.power_w==.1&&s.helper.ratio_limit_db==20&&s.helper.callsign=="K1ABC"&&
          s.transfer.fec==FecMode::rs20&&s.calendar&&s.calendar->epoch==1793850000&&
          s.calendar->initial_ordinal==12&&s.live_noise_dbfs_hz==-92.25,
          "Robust Helper/codec/calendar settings did not reach the session");
    const auto exported=launch_command::parse(controller.field(F::planner_command).text);
    check(exported.calendar_epoch==1793850000ULL&&exported.calendar_ordinal==12&&
          exported.live_noise_dbfs_hz==-92.25&&exported.fec=="rs20"&&exported.workspace_percent==0,
          "Native receiver settings were lost from saved launch parameters");
    controller.edit(F::short_bits,"001");
    check(controller.field(F::fec).selected=="rs20"&&controller.field(F::fec).display_text=="Off (exact raw bits)",
          "Raw transmission must retain the FEC preference while reporting its effective bypass");
    controller.edit(F::calendar_epoch,"none");controller.edit(F::live_noise_dbfs_hz,"unspecified");
    check(!controller.settings().calendar&&!controller.settings().live_noise_dbfs_hz,
          "Explicitly clearing live configuration retained a stale calendar/noise model");
}
}
int main() {
    try {audio_settings();rate_and_spectrum_presentation();damaged_reception_presentation();fast_simulation_controls();simulation_profile_revalidation();snr_and_symbol_rate_controls();message_submit_shortcut();presentation_and_retention();service_generations();retained_key_and_result_presentation();live_plot_presentation();unsynchronized_plot_presentation();robust_keyfile_menu_label();robust_public_key_warning();robust_boundary_keeps_polling();robust_native_settings();std::cout<<"Fast GUI isolation tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
